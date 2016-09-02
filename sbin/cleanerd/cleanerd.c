/*
 * cleanerd.c - NILFS cleaner daemon.
 *
 * Copyright (C) 2007-2012 Nippon Telegraph and Telephone Corporation.
 *
 * This file is part of NILFS.
 *
 * NILFS is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * NILFS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Written by Koji Sato,
 *            Ryusuke Konishi <konishi.ryusuke@lab.ntt.co.jp>.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif	/* HAVE_CONFIG_H */

#include <stdio.h>

#if HAVE_STDLIB_H
#include <stdlib.h>
#endif	/* HAVE_STDLIB_H */

#if HAVE_UNISTD_H
#include <unistd.h>
#endif	/* HAVE_UNISTD_H */

#if HAVE_ERR_H
#include <err.h>
#endif	/* HAVE_ERR_H */

#if HAVE_STRING_H
#include <string.h>
#endif	/* HAVE_STRING_H */

#if HAVE_LIMITS_H
#include <limits.h>
#endif	/* HAVE_LIMITS_H */

#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif	/* HAVE_SYS_TYPES_H */

#if HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif	/* HAVE_SYS_STAT_H */

#if HAVE_SYS_TIME_H
#include <sys/time.h>
#endif	/* HAVE_SYS_TIME */

#if HAVE_TIME_H
#include <time.h>
#endif	/* HAVE_TIME_H */

#if HAVE_SYSLOG_H
#include <syslog.h>
#endif	/* HAVE_SYSLOG_H */

#if HAVE_MQUEUE_H
#include <mqueue.h>
#endif	/* HAVE_MQUEUE_H */

#if HAVE_POLL_H
#include <poll.h>
#endif	/* HAVE_POLL_H */

#include <errno.h>
#include <signal.h>
#include <setjmp.h>
#include <assert.h>
#include <uuid/uuid.h>
#include "nilfs.h"
#include "compat.h"
#include "util.h"
#include "vector.h"
#include "nilfs_gc.h"
#include "nilfs_cleaner.h"
#include "cleaner_msg.h"
#include "cldconfig.h"
#include "cnormap.h"
#include "realpath.h"


#ifndef SYSCONFDIR
#define SYSCONFDIR		"/etc"
#endif	/* SYSCONFDIR */
#define NILFS_CLEANERD_CONFFILE	SYSCONFDIR "/nilfs_cleanerd.conf"


#ifdef _GNU_SOURCE
#include <getopt.h>
static const struct option long_option[] = {
	{"conffile", required_argument, NULL, 'c'},
	{"help", no_argument, NULL, 'h'},
	/* nofork option is obsolete. It does nothing even if passed */
	{"nofork", no_argument, NULL, 'n'},
	{"protection-period", required_argument, NULL, 'p'},
	{"version", no_argument, NULL, 'V'},
	{NULL, 0, NULL, 0}
};
#define NILFS_CLEANERD_OPTIONS	\
	"  -c, --conffile\tspecify configuration file\n"	\
	"  -h, --help    \tdisplay this help and exit\n"	\
	"  -p, --protection-period\tspecify protection period\n" \
	"  -V, --version \tprint version and exit\n"
#else	/* !_GNU_SOURCE */
#define NILFS_CLEANERD_OPTIONS	\
	"  -c            \tspecify configuration file\n"	\
	"  -h            \tdisplay this help and exit\n"	\
	"  -p            \tspecify protection period\n"		\
	"  -V            \tprint version and exit\n"
#endif	/* _GNU_SOURCE */

/**
 * struct nilfs_cleanerd - nilfs cleaner daemon
 * @nilfs: nilfs object
 * @cnormap: checkpoint number reverse mapper
 * @config: config structure
 * @conffile: configuration file name
 * @running: running state
 * @fallback: fallback state
 * @retry_cleaning: retrying reclamation for protected segments
 * @no_timeout: the next timeout will be 0 seconds
 * @ncleansegs: number of segments cleaned per cycle
 * @cleaning_interval: cleaning interval
 * @target: target time for sleeping (monotonic time)
 * @timeout: timeout value for sleeping
 * @min_reclaimable_blocks: min. number of reclaimable blocks
 * @prev_nongc_ctime: previous nongc ctime
 * @recvq: receive queue
 * @recvq_name: receive queue name
 * @sendq: send queue
 * @client_uuid: uuid of the previous message received from a client
 * @pending_cmd: pending client command
 * @jobid: current job id
 * @mm_prev_state: previous status during suspending
 * @mm_nrestpasses: remaining number of passes
 * @mm_nrestsegs: remaining number of segment (1-pass)
 * @mm_ncleansegs: number of segments cleaned per cycle (manual mode)
 * @mm_protection_period: protection period (manual mode)
 * @mm_cleaning_interval: cleaning interval (manual mode)
 * @mm_min_reclaimable_blocks: min. number of reclaimable blocks (manual mode)
 */
struct nilfs_cleanerd {
	struct nilfs *nilfs;
	struct nilfs_cnormap *cnormap;
	struct nilfs_cldconfig config;
	char *conffile;
	int running;
	int fallback;
	int retry_cleaning;
	int no_timeout;
	int shutdown;
	long ncleansegs;
	struct timespec cleaning_interval;
	struct timespec target;
	struct timespec timeout;
	unsigned long min_reclaimable_blocks;
	__u64 prev_nongc_ctime;
	mqd_t recvq;
	char *recvq_name;
	mqd_t sendq;
	uuid_t client_uuid;
	unsigned long jobid;
	int mm_prev_state;
	int mm_nrestpasses;
	long mm_nrestsegs;
	long mm_ncleansegs;
	struct timespec mm_protection_period;
	struct timespec mm_cleaning_interval;
	unsigned long mm_min_reclaimable_blocks;
};

/**
 * struct nilfs_segimp - segment importance
 * @si_segnum: segment number
 * @si_importance: importance of segment
 */
struct nilfs_segimp {
	__u64 si_segnum;
	long long si_importance;
};

/* command line option value */
static unsigned long protection_period;

/* global variables */
static struct nilfs_cleanerd *nilfs_cleanerd;
static sigjmp_buf nilfs_cleanerd_env; /* for siglongjmp */
static volatile sig_atomic_t nilfs_cleanerd_reload_config; /* reload flag */
static char nilfs_cleanerd_msgbuf[NILFS_CLEANER_MSG_MAX_REQSZ];

static const char *nilfs_cleaner_cmd_name[] = {
	"get-status", "run", "suspend", "resume", "tune", "reload", "wait",
	"stop", "shutdown"
};

static void nilfs_cleanerd_version(const char *progname)
{
	printf("%s (%s %s)\n", progname, PACKAGE, PACKAGE_VERSION);
}

static void nilfs_cleanerd_usage(const char *progname)
{
	fprintf(stderr,
		"Usage: %s [option]... dev\n"
		"%s options:\n"
		NILFS_CLEANERD_OPTIONS,
		progname, progname);
}

static void nilfs_cleanerd_set_log_priority(struct nilfs_cleanerd *cleanerd)
{
	setlogmask(LOG_UPTO(cleanerd->config.cf_log_priority));
}

/**
 * nilfs_cleanerd_config - load configuration file
 * @cleanerd: cleanerd object
 */
static int nilfs_cleanerd_config(struct nilfs_cleanerd *cleanerd,
				 const char *conffile)
{
	struct nilfs_cldconfig *config = &cleanerd->config;

	if (nilfs_cldconfig_read(config, conffile ? : cleanerd->conffile,
				 cleanerd->nilfs) < 0)
		return -1;

#ifdef HAVE_MMAP
	if (config->cf_use_mmap)
		nilfs_opt_set_mmap(cleanerd->nilfs);
	else
		nilfs_opt_clear_mmap(cleanerd->nilfs);
#endif	/* HAVE_MMAP */

	if (config->cf_use_set_suinfo)
		nilfs_opt_set_set_suinfo(cleanerd->nilfs);
	else
		nilfs_opt_clear_set_suinfo(cleanerd->nilfs);

	nilfs_cleanerd_set_log_priority(cleanerd);

	if (protection_period != ULONG_MAX) {
		syslog(LOG_INFO, "override protection period to %lu",
		       protection_period);
		config->cf_protection_period.tv_sec = protection_period;
		config->cf_protection_period.tv_nsec = 0;
	}
	return 0;
}

/**
 * nilfs_cleanerd_reconfig - reload configuration file
 * @cleanerd: cleanerd object
 */
static int nilfs_cleanerd_reconfig(struct nilfs_cleanerd *cleanerd,
				   const char *conffile)
{
	struct nilfs_cldconfig *config = &cleanerd->config;
	int ret;

	ret = nilfs_cleanerd_config(cleanerd, conffile);
	if (ret < 0) {
		syslog(LOG_ERR, "cannot configure: %m");
	} else {
		cleanerd->ncleansegs = config->cf_nsegments_per_clean;
		cleanerd->cleaning_interval = config->cf_cleaning_interval;
		cleanerd->min_reclaimable_blocks =
				config->cf_min_reclaimable_blocks;
		syslog(LOG_INFO, "configuration file reloaded");
	}
	return ret;
}

static int nilfs_cleanerd_open_queue(struct nilfs_cleanerd *cleanerd,
				     const char *device)
{
	char nambuf[NAME_MAX - 4];
	struct stat stbuf;
	struct mq_attr attr = {
		.mq_maxmsg = 6,
		.mq_msgsize = NILFS_CLEANER_MSG_MAX_REQSZ
	};
	int ret;

	cleanerd->recvq = -1;
	cleanerd->sendq = -1;
	cleanerd->jobid = 0;
	uuid_clear(cleanerd->client_uuid);

	/* receive queue */
	ret = stat(device, &stbuf);
	if (ret < 0)
		goto failed;

	if (S_ISBLK(stbuf.st_mode)) {
		ret = snprintf(nambuf, sizeof(nambuf),
			       "/nilfs-cleanerq-%llu",
			       (unsigned long long)stbuf.st_rdev);
	} else if (S_ISREG(stbuf.st_mode) || S_ISDIR(stbuf.st_mode)) {
		ret = snprintf(nambuf, sizeof(nambuf),
			       "/nilfs-cleanerq-%llu-%llu",
			       (unsigned long long)stbuf.st_dev,
			       (unsigned long long)stbuf.st_ino);
	} else {
		errno = EINVAL;
		goto failed;
	}

	assert(ret < sizeof(nambuf));

	cleanerd->recvq_name = strdup(nambuf);
	if (!cleanerd->recvq_name)
		goto failed;

	cleanerd->recvq = mq_open(nambuf, O_RDONLY | O_CREAT | O_NONBLOCK,
				  0600, &attr);
	if (cleanerd->recvq < 0) {
		free(cleanerd->recvq_name);
		goto failed;
	}

	assert(ret < sizeof(nambuf));

	return 0;

failed:
	syslog(LOG_ERR, "cannot create mqueue on %s: %m", device);
	return -1;
}

static void nilfs_cleanerd_close_queue(struct nilfs_cleanerd *cleanerd)
{
	if (cleanerd->recvq >= 0) {
		mq_close(cleanerd->recvq);
		mq_unlink(cleanerd->recvq_name);
		free(cleanerd->recvq_name);
		cleanerd->recvq = -1;
		cleanerd->recvq_name = NULL;
	}
	if (cleanerd->sendq >= 0) {
		mq_close(cleanerd->sendq);
		cleanerd->sendq = -1;
	}
}

#ifndef PATH_MAX
#define PATH_MAX	8192
#endif	/* PATH_MAX */

static __attribute__((noinline)) char *get_canonical_path(const char *path)
{
	char buf[PATH_MAX + 2], *canonical = NULL;

	if (path && myrealpath(path, buf, sizeof(buf)))
		canonical = strdup(buf);
	return canonical;
}

/**
 * nilfs_cleanerd_create - create cleanerd object
 * @dev: name of the device on which the cleanerd operates
 * @conffile: pathname of configuration file
 */
static struct nilfs_cleanerd *
nilfs_cleanerd_create(const char *dev, const char *dir, const char *conffile)
{
	struct nilfs_cleanerd *cleanerd;

	cleanerd = malloc(sizeof(*cleanerd));
	if (cleanerd == NULL)
		return NULL;

	memset(cleanerd, 0, sizeof(*cleanerd));

	cleanerd->nilfs = nilfs_open(dev, dir,
				       NILFS_OPEN_RAW | NILFS_OPEN_RDWR |
				       NILFS_OPEN_GCLK);
	if (cleanerd->nilfs == NULL) {
		syslog(LOG_ERR, "cannot open nilfs on %s: %m", dev);
		goto out_cleanerd;
	}

	cleanerd->cnormap = nilfs_cnormap_create(cleanerd->nilfs);
	if (cleanerd->cnormap == NULL) {
		syslog(LOG_ERR,
		       "failed to create checkpoint number reverse mapper: %m");
		goto out_nilfs;
	}

	cleanerd->conffile = strdup(conffile ? : NILFS_CLEANERD_CONFFILE);
	if (cleanerd->conffile == NULL)
		goto out_cnormap;

	if (nilfs_cleanerd_config(cleanerd, NULL) < 0)
		goto out_conffile;

	if (nilfs_cleanerd_open_queue(cleanerd,
				      nilfs_get_dev(cleanerd->nilfs)) < 0)
		goto out_conffile;

	/* success */
	return cleanerd;

	/* error */
out_conffile:
	free(cleanerd->conffile);
out_cnormap:
	nilfs_cnormap_destroy(cleanerd->cnormap);
out_nilfs:
	nilfs_close(cleanerd->nilfs);

out_cleanerd:
	free(cleanerd);
	return NULL;
}

static void nilfs_cleanerd_destroy(struct nilfs_cleanerd *cleanerd)
{
	nilfs_cleanerd_close_queue(cleanerd);
	free(cleanerd->conffile);
	nilfs_cnormap_destroy(cleanerd->cnormap);
	nilfs_close(cleanerd->nilfs);
	free(cleanerd);
}

static int nilfs_comp_segimp(const void *elem1, const void *elem2)
{
	const struct nilfs_segimp *segimp1 = elem1, *segimp2 = elem2;

	if (segimp1->si_importance < segimp2->si_importance)
		return -1;
	else if (segimp1->si_importance > segimp2->si_importance)
		return 1;

	return (segimp1->si_segnum < segimp2->si_segnum) ? -1 : 1;
}

static int nilfs_cleanerd_automatic_suspend(struct nilfs_cleanerd *cleanerd)
{
	return cleanerd->config.cf_min_clean_segments > 0;
}

static long nilfs_cleanerd_ncleansegs(struct nilfs_cleanerd *cleanerd)
{
	return cleanerd->running == 2 ?
		cleanerd->mm_ncleansegs : cleanerd->ncleansegs;
}

static struct timespec *
nilfs_cleanerd_cleaning_interval(struct nilfs_cleanerd *cleanerd)
{
	return cleanerd->running == 2 ?
		&cleanerd->mm_cleaning_interval :
		&cleanerd->cleaning_interval;
}

static struct timespec *
nilfs_cleanerd_protection_period(struct nilfs_cleanerd *cleanerd)
{
	return cleanerd->running == 2 ?
		&cleanerd->mm_protection_period :
		&cleanerd->config.cf_protection_period;
}

static unsigned long
nilfs_cleanerd_min_reclaimable_blocks(struct nilfs_cleanerd *cleanerd)
{
	return cleanerd->running == 2 ?
		cleanerd->mm_min_reclaimable_blocks :
		cleanerd->min_reclaimable_blocks;
}

static void
nilfs_cleanerd_reduce_ncleansegs_for_retry(struct nilfs_cleanerd *cleanerd)
{
	if (cleanerd->running == 2) {
		if (cleanerd->ncleansegs > 1)
			cleanerd->ncleansegs >>= 1;
	} else  {
		if (cleanerd->mm_ncleansegs > 1)
			cleanerd->mm_ncleansegs >>= 1;
	}
}

/**
 * nilfs_segment_is_protected - test if the segment is in protected region
 * @nilfs: nilfs object
 * @segnum: segment number to be tested
 * @protseq: lower limit of sequence numbers of protected segments
 */
static int nilfs_segment_is_protected(struct nilfs *nilfs, __u64 segnum,
				      __u64 protseq)
{
	void *segment;
	__u64 segseq;
	int ret = 0;

	if (nilfs_get_segment(nilfs, segnum, &segment) < 0)
		return -1;

	segseq = nilfs_get_segment_seqnum(nilfs, segment, segnum);
	if (cnt64_ge(segseq, protseq))
		ret = 1;
	nilfs_put_segment(nilfs, segment);
	return ret;
}

/**
 * nilfs_segments_still_reclaimable - examine if segments are still reclaimable
 * @nilfs: nilfs object
 * @segnumv: array of segment numbers
 * @nsegs: number of segment numbers stored in @segnumv
 * @protseq: lower limit of sequence numbers of protected segments
 */
static int nilfs_segments_still_reclaimable(struct nilfs *nilfs,
					    __u64 *segnumv,
					    unsigned long nsegs,
					    __u64 protseq)
{
	struct nilfs_suinfo si;
	int i, ret;

	for (i = 0; i < nsegs; i++) {
		if (nilfs_get_suinfo(nilfs, segnumv[i], &si, 1) == 1 &&
		    !nilfs_suinfo_reclaimable(&si))
			continue;

		ret = nilfs_segment_is_protected(nilfs, segnumv[i], protseq);
		if (ret > 0)
			return 1;
	}
	return 0;
}

#ifndef FIFREEZE
#define FIFREEZE	_IOWR('X', 119, int)
#define FITHAW		_IOWR('X', 120, int)
#endif

/**
 * nilfs_shrink_protected_region - shrink region of protected segments
 * @nilfs: nilfs object
 *
 * This tries to update log cursor written in superblocks to make
 * protected segments reclaimable.
 */
static int nilfs_shrink_protected_region(struct nilfs *nilfs)
{
	nilfs_cno_t cno;
	int arg = 0;
	int ret = -1;

	nilfs_sync(nilfs, &cno);

	if (ioctl(nilfs->n_iocfd, FIFREEZE, &arg) == 0 &&
	    ioctl(nilfs->n_iocfd, FITHAW, &arg) == 0)
		ret = 0;

	return ret;
}

/**
 * nilfs_cleanerd_select_segments - select segments to be reclaimed
 * @cleanerd: cleanerd object
 * @sustat: status information on segments
 * @segnums: array of segment numbers to store selected segments
 * @prottimep: place to store lower limit of protected period
 * @oldestp: place to store the oldest mod-time
 */
#define NILFS_CLEANERD_NSUINFO	512
#define NILFS_CLEANERD_NULLTIME S64_MAX

static ssize_t
nilfs_cleanerd_select_segments(struct nilfs_cleanerd *cleanerd,
			       struct nilfs_sustat *sustat, __u64 *segnums,
			       __s64 *prottimep, __s64 *oldestp)
{
	struct nilfs *nilfs;
	struct nilfs_vector *smv;
	struct nilfs_segimp *sm;
	struct nilfs_suinfo si[NILFS_CLEANERD_NSUINFO];
	struct timespec ts, ts2;
	__s64 prottime, oldest, lastmod, now;
	__u64 segnum;
	size_t count, nsegs;
	ssize_t nssegs, n;
	long long imp, thr;
	int ret;
	int i;

	nsegs = nilfs_cleanerd_ncleansegs(cleanerd);
	nilfs = cleanerd->nilfs;

	smv = nilfs_vector_create(sizeof(struct nilfs_segimp));
	if (!smv)
		return -1;

	/*
	 * The segments that were more recently written to disk than
	 * prottime are not selected.
	 */
	ret = clock_gettime(CLOCK_REALTIME, &ts);
	if (ret < 0) {
		nssegs = -1;
		goto out;
	}
	timespecsub(&ts, nilfs_cleanerd_protection_period(cleanerd), &ts2);
	now = ts.tv_sec;
	prottime = ts2.tv_sec;
	oldest = NILFS_CLEANERD_NULLTIME;

	/*
	 * The segments that have larger importance than thr are not
	 * selected.
	 */
	thr = sustat->ss_nongc_ctime;

	for (segnum = 0; segnum < sustat->ss_nsegs; segnum += n) {
		count = min_t(__u64, sustat->ss_nsegs - segnum,
			      NILFS_CLEANERD_NSUINFO);
		n = nilfs_get_suinfo(nilfs, segnum, si, count);
		if (n < 0) {
			nssegs = n;
			goto out;
		}
		for (i = 0; i < n; i++) {
			if (!nilfs_suinfo_reclaimable(&si[i]))
				continue;

			/*
			 * Use local variable 'lastmod' to treat the
			 * segment timestamp as a signed type value.
			 */
			lastmod = si[i].sui_lastmod;

			/*
			 * Timestamp policy.  The importance value is
			 * adjusted to include segments with a future
			 * timestamp.
			 */
			imp = lastmod <= now ? lastmod : thr - 1;

			if (imp < thr) {
				if (lastmod < oldest)
					oldest = lastmod;
				if (lastmod < prottime || lastmod > now) {
					sm = nilfs_vector_get_new_element(smv);
					if (sm == NULL) {
						nssegs = -1;
						goto out;
					}
					sm->si_segnum = segnum + i;
					sm->si_importance = imp;
				}
			}
		}
		if (n == 0) {
			syslog(LOG_WARNING,
			       "inconsistent number of segments: %llu (nsegs=%llu)",
			       (unsigned long long)nilfs_vector_get_size(smv),
			       (unsigned long long)sustat->ss_nsegs);
			break;
		}
	}
	nilfs_vector_sort(smv, nilfs_comp_segimp);

	nssegs = min_t(size_t, nilfs_vector_get_size(smv), nsegs);
	for (i = 0; i < nssegs; i++) {
		sm = nilfs_vector_get_element(smv, i);
		assert(sm != NULL);
		segnums[i] = sm->si_segnum;
	}
	*prottimep = prottime;
	*oldestp = oldest;

 out:
	nilfs_vector_destroy(smv);
	return nssegs;
}

static int oom_adjust(void)
{
	int fd, err;
	const char *path, *score;
	struct stat st;

	/* Avoid oom-killer */
	path = "/proc/self/oom_score_adj";
	score = "-1000\n";

	if (stat(path, &st)) {
		/* oom_score_adj cannot be used, try oom_adj */
		path = "/proc/self/oom_adj";
		score = "-17\n";
	}

	fd = open(path, O_WRONLY);
	if (fd < 0) {
		syslog(LOG_WARNING,
		       "can't adjust oom-killer's pardon %s: %m", path);
		return -1;
	}

	err = write(fd, score, strlen(score));
	if (err < 0) {
		syslog(LOG_WARNING,
		       "can't adjust oom-killer's pardon %s: %m", path);
		close(fd);
		return -1;
	}
	close(fd);
	return 0;
}

#define DEVNULL	"/dev/null"
#define ROOTDIR	"/"

static int daemonize(int nochdir, int noclose)
{
	pid_t pid;

	pid = fork();
	if (pid < 0)
		return -1;
	else if (pid != 0)
		/* parent */
		_exit(EXIT_SUCCESS);

	/* child */
	if (setsid() < 0)
		return -1;

	/* umask(0); */

	/* for ensuring I'm not a session leader */
	pid = fork();
	if (pid < 0)
		return -1;
	else if (pid != 0)
		/* parent */
		_exit(EXIT_SUCCESS);

	if (!nochdir && (chdir(ROOTDIR) < 0))
		return -1;

	printf("NILFS_CLEANERD_PID=%lu\n", (unsigned long)getpid());
	fflush(stdout);

	if (!noclose) {
		close(0);
		close(1);
		close(2);
		if (open(DEVNULL, O_RDONLY) < 0)
			return -1;
		if (open(DEVNULL, O_WRONLY) < 0)
			return -1;
		if (open(DEVNULL, O_WRONLY) < 0)
			return -1;
	}
	return 0;
}

static RETSIGTYPE handle_sigterm(int signum)
{
	siglongjmp(nilfs_cleanerd_env, 1);
}

static RETSIGTYPE handle_sighup(int signum)
{
	nilfs_cleanerd_reload_config = 1;
}

static int set_sigterm_handler(void)
{
	struct sigaction act;

	memset(&act, 0, sizeof(act));
	act.sa_handler = handle_sigterm;
	sigfillset(&act.sa_mask);
	return sigaction(SIGTERM, &act, NULL);
}

static int set_sighup_handler(void)
{
	struct sigaction act;

	memset(&act, 0, sizeof(act));
	act.sa_handler = handle_sighup;
	sigfillset(&act.sa_mask);
	return sigaction(SIGHUP, &act, NULL);
}

static void nilfs_cleanerd_clean_check_pause(struct nilfs_cleanerd *cleanerd)
{
	cleanerd->running = 0;
	cleanerd->timeout = cleanerd->config.cf_clean_check_interval;
	syslog(LOG_INFO, "pause (clean check)");
}

static void nilfs_cleanerd_clean_check_resume(struct nilfs_cleanerd *cleanerd)
{
	cleanerd->running = 1;
	syslog(LOG_INFO, "resume (clean check)");
}

static void nilfs_cleanerd_manual_suspend(struct nilfs_cleanerd *cleanerd)
{
	cleanerd->mm_prev_state = cleanerd->running;
	cleanerd->running = -1;
	cleanerd->timeout = cleanerd->config.cf_clean_check_interval;
	syslog(LOG_INFO, "suspend (manual)");
}

static void nilfs_cleanerd_manual_resume(struct nilfs_cleanerd *cleanerd)
{
	/* cleanerd->running == -1 */
	cleanerd->mm_prev_state = 0;
	cleanerd->running = cleanerd->mm_prev_state;
	syslog(LOG_INFO, "resume (manual)");
}

static void nilfs_cleanerd_manual_run(struct nilfs_cleanerd *cleanerd)
{
	cleanerd->running = 2;
	syslog(LOG_INFO, "run (manual)");
}

static void nilfs_cleanerd_manual_end(struct nilfs_cleanerd *cleanerd)
{
	cleanerd->running = 0;
	cleanerd->timeout = cleanerd->config.cf_clean_check_interval;
	syslog(LOG_INFO, "manual run completed");
}

static void nilfs_cleanerd_manual_stop(struct nilfs_cleanerd *cleanerd)
{
	cleanerd->running = 0;
	cleanerd->timeout = cleanerd->config.cf_clean_check_interval;
	syslog(LOG_INFO, "manual run aborted");
}

static int nilfs_cleanerd_init_interval(struct nilfs_cleanerd *cleanerd)
{
	int ret;

	ret = clock_gettime(CLOCK_MONOTONIC, &cleanerd->target);
	if (ret < 0) {
		syslog(LOG_ERR, "cannot get monotonic time: %m");
		return -1;
	}
	timespecadd(&cleanerd->target, &cleanerd->config.cf_cleaning_interval,
		    &cleanerd->target);
	return 0;
}

static int nilfs_cleanerd_recalc_interval(struct nilfs_cleanerd *cleanerd,
					  int nchosen, int ndone,
					  __s64 prottime, __s64 oldest)
{
	struct timespec curr, *interval;
	int ret;

	if (nchosen == 0 ||
	    (!cleanerd->fallback && ndone == 0 && !cleanerd->retry_cleaning)) {
		struct timespec pt;

		/* no segment were cleaned */
		if (cleanerd->running == 2) {
			/* Quit in the case of manual run */
			nilfs_cleanerd_manual_end(cleanerd);
			return 0;
		} else if (oldest == NILFS_CLEANERD_NULLTIME) {
			/* no reclaimable segments */
			if (cleanerd->running)
				cleanerd->running = 0;

			pt = *(nilfs_cleanerd_protection_period(cleanerd));
		} else {
			__s64 tgt;

			ret = clock_gettime(CLOCK_REALTIME, &curr);
			if (ret < 0) {
				syslog(LOG_ERR, "cannot get current time: %m");
				return -1;
			}
			tgt = min_t(__s64, oldest, curr.tv_sec);
			pt.tv_sec = tgt > prottime ? tgt - prottime + 1 : 1;
			pt.tv_nsec = 0;
		}
		if (timespeccmp(&pt,
				&cleanerd->config.cf_clean_check_interval, <))
			cleanerd->timeout =
				cleanerd->config.cf_clean_check_interval;
		else
			cleanerd->timeout = pt;
		return 0;
	}

	ret = clock_gettime(CLOCK_MONOTONIC, &curr);
	if (ret < 0) {
		syslog(LOG_ERR, "cannot get monotonic time: %m");
		return -1;
	}

	if (cleanerd->fallback) {
		timespecadd(&curr, &cleanerd->config.cf_retry_interval,
			    &cleanerd->target);
		timespecsub(&cleanerd->target, &curr, &cleanerd->timeout);
		syslog(LOG_DEBUG, "retry interval");
		return 0;
	}

	interval = nilfs_cleanerd_cleaning_interval(cleanerd);
	/* timespeccmp() does not work for '>=' or '<='. */
	/* curr >= target */
	if (!timespeccmp(&curr, &cleanerd->target, <) || cleanerd->no_timeout) {
		timespecclear(&cleanerd->timeout);
		timespecadd(&curr, interval, &cleanerd->target);
		syslog(LOG_DEBUG, "adjust interval");
		return 0;
	}
	timespecsub(&cleanerd->target, &curr, &cleanerd->timeout);
	timespecadd(&cleanerd->target, interval, &cleanerd->target);
	return 0;
}

static int nilfs_cleanerd_respond(struct nilfs_cleanerd *cleanerd,
				  struct nilfs_cleaner_request *req,
				  const struct nilfs_cleaner_response *res)
{
	int ret;

	if (cleanerd->sendq < 0 ||
	    uuid_compare(cleanerd->client_uuid, req->client_uuid) != 0) {
		char nambuf[NAME_MAX - 4];
		char uuidbuf[36 + 1];

		uuid_unparse_lower(req->client_uuid, uuidbuf);
		ret = snprintf(nambuf, sizeof(nambuf),
			       "/nilfs-cleanerq-%s", uuidbuf);
		if (ret < 0)
			goto out;

		if (cleanerd->sendq >= 0)
			mq_close(cleanerd->sendq);
		ret = -1;
		cleanerd->sendq = mq_open(nambuf, O_WRONLY | O_NONBLOCK);
		if (cleanerd->sendq < 0) {
			syslog(LOG_ERR, "cannot open queue to client: %m");
			goto out;
		}
		uuid_copy(cleanerd->client_uuid, req->client_uuid);
	}
	ret = mq_send(cleanerd->sendq, (char *)res, sizeof(*res),
		      NILFS_CLEANER_PRIO_HIGH);
	if (ret < 0) {
		syslog(LOG_ERR, "cannot respond to client: %m");
	} else {
		if (req->cmd >= 0 &&
		    req->cmd < ARRAY_SIZE(nilfs_cleaner_cmd_name))
			syslog(LOG_DEBUG, "command %s %s",
			       nilfs_cleaner_cmd_name[req->cmd],
			       res->result ? "nacked" : "acked");
	}
out:
	return ret;
}

static int nilfs_cleanerd_nak(struct nilfs_cleanerd *cleanerd,
			      struct nilfs_cleaner_request *req,
			      int errcode)
{
	struct nilfs_cleaner_response res = {
		.result = NILFS_CLEANER_RSP_NACK,  .err = errcode
	};
	return nilfs_cleanerd_respond(cleanerd, req, &res);
}

static int nilfs_cleanerd_cmd_getstat(struct nilfs_cleanerd *cleanerd,
				      struct nilfs_cleaner_request *req,
				      size_t argsize)
{
	struct nilfs_cleaner_response res = {0};

	if (cleanerd->running == 0)
		res.status = NILFS_CLEANER_STATUS_IDLE;
	else if (cleanerd->running > 0)
		res.status = NILFS_CLEANER_STATUS_RUNNING;
	else
		res.status = NILFS_CLEANER_STATUS_SUSPENDED;

	res.result = NILFS_CLEANER_RSP_ACK;
	return nilfs_cleanerd_respond(cleanerd, req, &res);
}

static int nilfs_cleanerd_cmd_run(struct nilfs_cleanerd *cleanerd,
				  struct nilfs_cleaner_request *req,
				  size_t argsize)
{
	struct nilfs_cleaner_request_with_args *req2;
	struct nilfs_cleaner_response res = {0};

	if (argsize < sizeof(req2->args))
		goto error_inval;

	req2 = (struct nilfs_cleaner_request_with_args *)req;

	/* protection period */
	if (req2->args.valid & NILFS_CLEANER_ARG_PROTECTION_PERIOD) {
		if (req2->args.protection_period > ULONG_MAX)
			goto error_inval;
		cleanerd->mm_protection_period.tv_sec =
			req2->args.protection_period;
		cleanerd->mm_protection_period.tv_nsec = 0;
	} else {
		cleanerd->mm_protection_period =
			cleanerd->config.cf_protection_period;
	}

	/* gc speed (nsegments per clean) */
	if (req2->args.valid & NILFS_CLEANER_ARG_NSEGMENTS_PER_CLEAN) {
		if (req2->args.nsegments_per_clean == 0 ||
		    req2->args.nsegments_per_clean >
		    NILFS_CLDCONFIG_NSEGMENTS_PER_CLEAN_MAX)
			goto error_inval;
		cleanerd->mm_ncleansegs = req2->args.nsegments_per_clean;
	} else {
		cleanerd->mm_ncleansegs = cleanerd->ncleansegs;
	}
	/* gc speed (cleaning interval) */
	if (req2->args.valid & NILFS_CLEANER_ARG_CLEANING_INTERVAL) {
		cleanerd->mm_cleaning_interval.tv_sec =
			req2->args.cleaning_interval;
		cleanerd->mm_cleaning_interval.tv_nsec =
			req2->args.cleaning_interval_nsec;
	} else {
		cleanerd->mm_cleaning_interval =
			cleanerd->cleaning_interval;
	}
	/* minimal reclaimable blocks */
	if (req2->args.valid & NILFS_CLEANER_ARG_MIN_RECLAIMABLE_BLOCKS) {
		switch (req2->args.min_reclaimable_blocks_unit) {
		case NILFS_CLEANER_ARG_UNIT_NONE:
			if (req2->args.min_reclaimable_blocks >
				nilfs_get_blocks_per_segment(cleanerd->nilfs))
				goto error_inval;

			cleanerd->mm_min_reclaimable_blocks =
				req2->args.min_reclaimable_blocks;
			break;
		case NILFS_CLEANER_ARG_UNIT_PERCENT:
			if (req2->args.min_reclaimable_blocks > 100)
				goto error_inval;

			cleanerd->mm_min_reclaimable_blocks =
				(req2->args.min_reclaimable_blocks *
				nilfs_get_blocks_per_segment(cleanerd->nilfs) +
				99) / 100;
			break;
		default:
			goto error_inval;
		}
	} else {
		cleanerd->mm_min_reclaimable_blocks =
			cleanerd->min_reclaimable_blocks;
	}
	/* number of passes */
	if (req2->args.valid & NILFS_CLEANER_ARG_NPASSES) {
		if (!req2->args.npasses)
			goto error_inval;
		cleanerd->mm_nrestpasses = req2->args.npasses;
		cleanerd->mm_nrestsegs = 0;
	} else {
		cleanerd->mm_nrestpasses = 1;
		cleanerd->mm_nrestsegs = 0;
	}
	nilfs_cleanerd_manual_run(cleanerd);
	res.jobid = ++cleanerd->jobid;
	res.result = NILFS_CLEANER_RSP_ACK;
out:
	return nilfs_cleanerd_respond(cleanerd, req, &res);

error_inval:
	res.result = NILFS_CLEANER_RSP_NACK;
	res.err = EINVAL;
	goto out;
}

static int nilfs_cleanerd_cmd_suspend(struct nilfs_cleanerd *cleanerd,
				      struct nilfs_cleaner_request *req,
				      size_t argsize)
{
	struct nilfs_cleaner_response res = {0};

	if (cleanerd->running != -1)
		nilfs_cleanerd_manual_suspend(cleanerd);
	res.result = NILFS_CLEANER_RSP_ACK;
	return nilfs_cleanerd_respond(cleanerd, req, &res);
}

static int nilfs_cleanerd_cmd_resume(struct nilfs_cleanerd *cleanerd,
				     struct nilfs_cleaner_request *req,
				     size_t argsize)
{
	struct nilfs_cleaner_response res = {0};

	if (cleanerd->running == -1)
		nilfs_cleanerd_manual_resume(cleanerd);
	res.result = NILFS_CLEANER_RSP_ACK;
	return nilfs_cleanerd_respond(cleanerd, req, &res);
}

static int nilfs_cleanerd_cmd_tune(struct nilfs_cleanerd *cleanerd,
				   struct nilfs_cleaner_request *req,
				   size_t argsize)
{
	/* Not yet implemented */
	return nilfs_cleanerd_nak(cleanerd, req, EOPNOTSUPP);
}

static int nilfs_cleanerd_cmd_reload(struct nilfs_cleanerd *cleanerd,
				     struct nilfs_cleaner_request *req,
				     size_t argsize)
{
	struct nilfs_cleaner_request_with_path *req2;
	struct nilfs_cleaner_response res = {0};
	char *conffile;

	req2 = (struct nilfs_cleaner_request_with_path *)req;
	if (argsize == 0 || (argsize == 1 && req2->pathname[0] == '\0')) {
		conffile = NULL;
	} else if (strnlen(req2->pathname, argsize) < argsize) {
		conffile = req2->pathname;
	} else  {
		res.result = NILFS_CLEANER_RSP_NACK;
		res.err = EINVAL;
		goto out_send;
	}
	if (nilfs_cleanerd_reconfig(cleanerd, conffile) < 0) {
		res.result = NILFS_CLEANER_RSP_NACK;
		res.err = errno;
	} else {
		res.result = NILFS_CLEANER_RSP_ACK;
	}
out_send:
	return nilfs_cleanerd_respond(cleanerd, req, &res);
}

static int nilfs_cleanerd_cmd_wait(struct nilfs_cleanerd *cleanerd,
				   struct nilfs_cleaner_request *req,
				   size_t argsize)
{
	/* Not yet implemented */
	return nilfs_cleanerd_nak(cleanerd, req, EOPNOTSUPP);
}

static int nilfs_cleanerd_cmd_stop(struct nilfs_cleanerd *cleanerd,
				   struct nilfs_cleaner_request *req,
				   size_t argsize)
{
	struct nilfs_cleaner_response res = {0};

	if (cleanerd->running == 2 || cleanerd->running == -1)
		nilfs_cleanerd_manual_stop(cleanerd);
	res.result = NILFS_CLEANER_RSP_ACK;
	return nilfs_cleanerd_respond(cleanerd, req, &res);
}

static int nilfs_cleanerd_cmd_shutdown(struct nilfs_cleanerd *cleanerd,
				       struct nilfs_cleaner_request *req,
				       size_t argsize)
{
	struct nilfs_cleaner_response res = {0};

	cleanerd->shutdown = 1;
	res.result = NILFS_CLEANER_RSP_ACK;
	return nilfs_cleanerd_respond(cleanerd, req, &res);
}

static int nilfs_cleanerd_handle_message(struct nilfs_cleanerd *cleanerd,
					 void *msgbuf, size_t bytes)
{
	struct nilfs_cleaner_request *req = msgbuf;
	size_t argsize;
	int ret = -1;

	if (bytes < sizeof(*req) || bytes < sizeof(*req) + req->argsize) {
		syslog(LOG_NOTICE, "too short message");
		goto out;
	}
	argsize = bytes - sizeof(*req);

	if (req->cmd >= 0 && req->cmd < ARRAY_SIZE(nilfs_cleaner_cmd_name))
		syslog(LOG_DEBUG, "received %s command",
		       nilfs_cleaner_cmd_name[req->cmd]);

	switch (req->cmd) {
	case NILFS_CLEANER_CMD_GET_STATUS:
		ret = nilfs_cleanerd_cmd_getstat(cleanerd, req, argsize);
		break;
	case NILFS_CLEANER_CMD_RUN:
		ret = nilfs_cleanerd_cmd_run(cleanerd, req, argsize);
		break;
	case NILFS_CLEANER_CMD_SUSPEND:
		ret = nilfs_cleanerd_cmd_suspend(cleanerd, req, argsize);
		break;
	case NILFS_CLEANER_CMD_RESUME:
		ret = nilfs_cleanerd_cmd_resume(cleanerd, req, argsize);
		break;
	case NILFS_CLEANER_CMD_TUNE:
		ret = nilfs_cleanerd_cmd_tune(cleanerd, req, argsize);
		break;
	case NILFS_CLEANER_CMD_RELOAD:
		ret = nilfs_cleanerd_cmd_reload(cleanerd, req, argsize);
		break;
	case NILFS_CLEANER_CMD_WAIT:
		ret = nilfs_cleanerd_cmd_wait(cleanerd, req, argsize);
		break;
	case NILFS_CLEANER_CMD_STOP:
		ret = nilfs_cleanerd_cmd_stop(cleanerd, req, argsize);
		break;
	case NILFS_CLEANER_CMD_SHUTDOWN:
		ret = nilfs_cleanerd_cmd_shutdown(cleanerd, req, argsize);
		break;
	default:
		syslog(LOG_DEBUG, "received unknown command: %d", req->cmd);
		return nilfs_cleanerd_nak(cleanerd, req, EINVAL);
	}
out:
	return ret;
}

static int nilfs_cleanerd_wait(struct nilfs_cleanerd *cleanerd)
{
	struct pollfd pfd;
	ssize_t bytes;
	int ret;

	syslog(LOG_DEBUG, "wait %ld.%09ld",
	       cleanerd->timeout.tv_sec, cleanerd->timeout.tv_nsec);

	memset(&pfd, 0, sizeof(pfd));
	pfd.fd = cleanerd->recvq;
	pfd.events = POLLIN;

	ret = ppoll(&pfd, 1, &cleanerd->timeout, NULL);
	if (ret < 0) {
		if (errno == EINTR) {
			syslog(LOG_INFO, "wake up (interrupted)");
			goto out;
		}
		syslog(LOG_ERR, "ppoll failed: %m");
		return -1;
	}

	if (!(pfd.revents & POLLIN)) {
		syslog(LOG_DEBUG, "wake up (timed out)");
		goto out;
	}
	syslog(LOG_DEBUG, "wake up to handle message");

	bytes = mq_receive(cleanerd->recvq, nilfs_cleanerd_msgbuf,
			   sizeof(nilfs_cleanerd_msgbuf), NULL);
	if (bytes < 0) {
		if (errno == EINTR || errno == EAGAIN) {
			syslog(LOG_INFO, "mq_receive aborted: %s",
			       errno == EINTR ?
			       "interrupted" : "no message found");
		} else {
			syslog(LOG_ERR, "mq_receive failed: %m");
			return -1;
		}
	} else {
		nilfs_cleanerd_handle_message(cleanerd, nilfs_cleanerd_msgbuf,
					      bytes);
	}
out:
	return 0;
}

static __u64 nilfs_cleanerd_get_nrsvsegs(struct nilfs *nilfs, __u64 nsegs)
{
	const struct nilfs_super_block *sb = nilfs_get_sb(nilfs);
	__u32 r_ratio = le32_to_cpu(sb->s_r_segments_percentage);
	__u64 rn;

	rn = max_t(__u64, (nsegs * r_ratio + 99) / 100, NILFS_MIN_NRSVSEGS);
	return rn;
}

static int nilfs_cleanerd_handle_clean_check(struct nilfs_cleanerd *cleanerd,
					     struct nilfs_sustat *sustat)
{
	struct nilfs_cldconfig *config = &cleanerd->config;
	int r_segments = nilfs_cleanerd_get_nrsvsegs(cleanerd->nilfs,
						     sustat->ss_nsegs);

	if (cleanerd->running == 1) {
		/* running (automatic suspend mode) */
		if (sustat->ss_ncleansegs >
		    config->cf_max_clean_segments + r_segments) {
			nilfs_cleanerd_clean_check_pause(cleanerd);
			return 1; /* immediately sleep */
		}
	} else if (cleanerd->running == 0) {
		/* idle */
		if (sustat->ss_ncleansegs <
		    config->cf_min_clean_segments + r_segments)
			nilfs_cleanerd_clean_check_resume(cleanerd);
		else
			return 1; /* immediately sleep */
	}

	if (sustat->ss_ncleansegs <
	    config->cf_min_clean_segments + r_segments) {
		/* disk space is close to limit -- accelerate cleaning */
		cleanerd->ncleansegs = config->cf_mc_nsegments_per_clean;
		cleanerd->cleaning_interval = config->cf_mc_cleaning_interval;
		cleanerd->min_reclaimable_blocks =
				config->cf_mc_min_reclaimable_blocks;
	} else {
		/* continue to run */
		cleanerd->ncleansegs = config->cf_nsegments_per_clean;
		cleanerd->cleaning_interval = config->cf_cleaning_interval;
		cleanerd->min_reclaimable_blocks =
				config->cf_min_reclaimable_blocks;
	}

	return 0; /* do gc */
}

static ssize_t
nilfs_cleanerd_count_inuse_segments(struct nilfs_cleanerd *cleanerd,
				    struct nilfs_sustat *sustat)
{
	struct nilfs_suinfo si[NILFS_CLEANERD_NSUINFO];
	__u64 segnum;
	unsigned long rest, count;
	ssize_t nsi, i;
	ssize_t nfound = 0;

	segnum = 0;
	rest = sustat->ss_nsegs;
	while (rest > 0 && segnum < sustat->ss_nsegs) {
		count = min_t(unsigned long, rest, NILFS_CLEANERD_NSUINFO);
		nsi = nilfs_get_suinfo(cleanerd->nilfs, segnum, si, count);
		if (nsi < 0) {
			syslog(LOG_ERR, "cannot get segment usage info: %m");
			return -1;
		}
		for (i = 0; i < nsi; i++, segnum++) {
			if (nilfs_suinfo_reclaimable(&si[i])) {
				nfound++;
				rest--;
			}
		}
	}
	return nfound; /* return the number of found segments */
}

static int nilfs_cleanerd_handle_manual_mode(struct nilfs_cleanerd *cleanerd,
					     struct nilfs_sustat *sustat)
{
	ssize_t ret;

	if (cleanerd->mm_nrestsegs == 0) {
		if (cleanerd->mm_nrestpasses > 0) {
			ret = nilfs_cleanerd_count_inuse_segments(cleanerd,
								  sustat);
			if (ret < 0) {
				cleanerd->mm_nrestpasses = 0;
			} else {
				cleanerd->mm_nrestpasses--;
				cleanerd->mm_nrestsegs = ret;
			}
		}
	}
	if (cleanerd->mm_nrestpasses == 0 && cleanerd->mm_nrestsegs == 0)
		nilfs_cleanerd_manual_end(cleanerd);

	return 0;
}

static int nilfs_cleanerd_check_state(struct nilfs_cleanerd *cleanerd,
				      struct nilfs_sustat *sustat)
{
	int ret;

	if (cleanerd->running < 0)
		return 1; /* suspending -> sleep */

	if (cleanerd->running == 2)
		nilfs_cleanerd_handle_manual_mode(cleanerd, sustat);

	if (cleanerd->running < 2 &&
	    nilfs_cleanerd_automatic_suspend(cleanerd)) {
		ret = nilfs_cleanerd_handle_clean_check(cleanerd, sustat);
		if (ret)
			return 1; /* pausing (clean check) -> sleep */
	}

	if (cleanerd->running == 0) {
		/* idle */
		if (sustat->ss_nongc_ctime != cleanerd->prev_nongc_ctime) {
			cleanerd->running = 1;
			cleanerd->prev_nongc_ctime = sustat->ss_nongc_ctime;
		} else {
			return 1; /* fs not updated -> sleep */
		}
	}
	return 0; /* do gc */
}

static void nilfs_cleanerd_progress(struct nilfs_cleanerd *cleanerd, int nsegs)
{
	if (cleanerd->running == 2) {
		/* decrease remaining number of segments */
		cleanerd->mm_nrestsegs =
			max_t(long, cleanerd->mm_nrestsegs - nsegs, 0);
	}
}

static int nilfs_cleanerd_clean_segments(struct nilfs_cleanerd *cleanerd,
					 __u64 *segnums, size_t nsegs,
					 __u64 protseq, size_t *ndone)
{
	struct nilfs_reclaim_params params;
	struct nilfs_reclaim_stat stat;
	struct timespec *pt;
	int ret, i, sumsegs;

	params.flags = NILFS_RECLAIM_PARAM_PROTSEQ |
		       NILFS_RECLAIM_PARAM_PROTCNO |
		       NILFS_RECLAIM_PARAM_MIN_RECLAIMABLE_BLKS;
	params.min_reclaimable_blks =
			nilfs_cleanerd_min_reclaimable_blocks(cleanerd);
	params.protseq = protseq;

	pt = nilfs_cleanerd_protection_period(cleanerd);

	ret = nilfs_cnormap_track_back(cleanerd->cnormap, pt->tv_sec,
				       &params.protcno);
	if (ret < 0) {
		syslog(LOG_ERR,
		       "cannot get checkpoint number from protection period (%llu): %m",
		       (unsigned long long)pt->tv_sec);
		goto out;
	}
	syslog(LOG_DEBUG, "got cno %llu from protection period %lu",
	       (unsigned long long)params.protcno, (unsigned long)pt->tv_sec);

	memset(&stat, 0, sizeof(stat));
	ret = nilfs_xreclaim_segment(cleanerd->nilfs, segnums, nsegs, 0,
				     &params, &stat);
	if (ret < 0) {
		if (errno == ENOMEM) {
			nilfs_cleanerd_reduce_ncleansegs_for_retry(cleanerd);
			cleanerd->fallback = 1;
			*ndone = 0;
			ret = 0;
		}
		goto out;
	}

	*ndone = 0;

	if (stat.cleaned_segs > 0) {
		for (i = 0; i < stat.cleaned_segs; i++)
			syslog(LOG_DEBUG, "segment %llu cleaned",
			       (unsigned long long)segnums[i]);

		nilfs_cleanerd_progress(cleanerd, stat.cleaned_segs);
		cleanerd->fallback = 0;
		cleanerd->retry_cleaning = 0;

		*ndone += stat.cleaned_segs;
	}

	if (stat.deferred_segs > 0) {
		sumsegs = stat.cleaned_segs + stat.deferred_segs;
		for (i = stat.cleaned_segs; i < sumsegs; i++)
			syslog(LOG_DEBUG, "segment %llu deferred",
			       (unsigned long long)segnums[i]);

		nilfs_cleanerd_progress(cleanerd, stat.deferred_segs);
		cleanerd->fallback = 0;
		cleanerd->retry_cleaning = 0;

		if (!stat.cleaned_segs)
			cleanerd->no_timeout = 1;

		*ndone += stat.deferred_segs;
	}

	if (*ndone == 0) {
		syslog(LOG_DEBUG, "no segments cleaned");

		if (!cleanerd->retry_cleaning &&
		    nilfs_segments_still_reclaimable(
			    cleanerd->nilfs, segnums, nsegs, protseq) &&
		    nilfs_shrink_protected_region(cleanerd->nilfs) == 0) {

			syslog(LOG_DEBUG, "retrying protected region");
			cleanerd->retry_cleaning = 1;
		} else {
			cleanerd->retry_cleaning = 0;
		}
	}

out:
	return ret;
}

/**
 * nilfs_cleanerd_clean_loop - main loop of the cleaner daemon
 * @cleanerd: cleanerd object
 */
static int nilfs_cleanerd_clean_loop(struct nilfs_cleanerd *cleanerd)
{
	struct nilfs_sustat sustat;
	__s64 prottime = 0, oldest = 0;
	__u64 segnums[NILFS_CLDCONFIG_NSEGMENTS_PER_CLEAN_MAX];
	sigset_t sigset;
	size_t ndone;
	int ns, ret;

	sigemptyset(&sigset);
	if (sigprocmask(SIG_SETMASK, &sigset, NULL) < 0) {
		syslog(LOG_ERR, "cannot set signal mask: %m");
		return -1;
	}
	sigaddset(&sigset, SIGHUP);

	if (set_sigterm_handler() < 0) {
		syslog(LOG_ERR, "cannot set SIGTERM signal handler: %m");
		return -1;
	}
	if (set_sighup_handler() < 0) {
		syslog(LOG_ERR, "cannot set SIGHUP signal handler: %m");
		return -1;
	}

	nilfs_cleanerd_reload_config = 0;

	cleanerd->running = 1;
	cleanerd->fallback = 0;
	cleanerd->retry_cleaning = 0;
	nilfs_gc_logger = syslog;

	ret = nilfs_cleanerd_init_interval(cleanerd);
	if (ret < 0)
		return -1;

	cleanerd->ncleansegs = cleanerd->config.cf_nsegments_per_clean;
	cleanerd->cleaning_interval = cleanerd->config.cf_cleaning_interval;
	cleanerd->min_reclaimable_blocks =
			cleanerd->config.cf_min_reclaimable_blocks;


	if (nilfs_cleanerd_automatic_suspend(cleanerd))
		nilfs_cleanerd_clean_check_pause(cleanerd);

	while (!cleanerd->shutdown) {
		cleanerd->no_timeout = 0;

		if (sigprocmask(SIG_BLOCK, &sigset, NULL) < 0) {
			syslog(LOG_ERR, "cannot set signal mask: %m");
			return -1;
		}

		if (nilfs_cleanerd_reload_config) {
			if (nilfs_cleanerd_reconfig(cleanerd, NULL) < 0)
				return -1;
			nilfs_cleanerd_reload_config = 0;
		}

		if (nilfs_get_sustat(cleanerd->nilfs, &sustat) < 0) {
			syslog(LOG_ERR, "cannot get segment usage stat: %m");
			return -1;
		}

		if (nilfs_cleanerd_check_state(cleanerd, &sustat))
			goto sleep;

		/* starts garbage collection */
		syslog(LOG_DEBUG, "ncleansegs = %llu",
		       (unsigned long long)sustat.ss_ncleansegs);

		ns = nilfs_cleanerd_select_segments(
			cleanerd, &sustat, segnums, &prottime, &oldest);
		if (ns < 0) {
			syslog(LOG_ERR, "cannot select segments: %m");
			return -1;
		}
		syslog(LOG_DEBUG, "%d segment%s selected to be cleaned",
		       ns, (ns <= 1) ? "" : "s");
		ndone = 0;
		if (ns > 0) {
			ret = nilfs_cleanerd_clean_segments(
				cleanerd, segnums, ns, sustat.ss_prot_seq,
				&ndone);
			if (ret < 0)
				return -1;
		} else {
			cleanerd->retry_cleaning = 0;
		}
		/* done */

		ret = nilfs_cleanerd_recalc_interval(
			cleanerd, ns, ndone, prottime, oldest);
		if (ret < 0)
			return -1;

sleep:
		if (sigprocmask(SIG_UNBLOCK, &sigset, NULL) < 0) {
			syslog(LOG_ERR, "cannot set signal mask: %m");
			return -1;
		}

		ret = nilfs_cleanerd_wait(cleanerd);
		if (ret < 0)
			return -1;
	}
	return 0;
}

int main(int argc, char *argv[])
{
	char *progname, *conffile;
	char *dev, *dir;
	char *endptr;
	int status, c;
#ifdef _GNU_SOURCE
	int option_index;
#endif	/* _GNU_SOURCE */

	progname = (strrchr(argv[0], '/') != NULL) ?
		strrchr(argv[0], '/') + 1 : argv[0];
	conffile = NILFS_CLEANERD_CONFFILE;
	status = EXIT_SUCCESS;
	protection_period = ULONG_MAX;
	dev = NULL;
	dir = NULL;

#ifdef _GNU_SOURCE
	while ((c = getopt_long(argc, argv, "c:hnp:V",
				long_option, &option_index)) >= 0) {
#else	/* !_GNU_SOURCE */
	while ((c = getopt(argc, argv, "c:hnp:V")) >= 0) {
#endif	/* _GNU_SOURCE */

		switch (c) {
		case 'c':
			conffile = optarg;
			break;
		case 'h':
			nilfs_cleanerd_usage(progname);
			exit(EXIT_SUCCESS);
		case 'n':
			/* ignore nofork option, do nothing */
			break;
		case 'p':
			protection_period = strtoul(optarg, &endptr, 10);
			if (endptr == optarg || *endptr != '\0') {
				errx(EXIT_FAILURE,
				     "invalid protection period: %s", optarg);
			} else if (protection_period == ULONG_MAX &&
				   errno == ERANGE) {
				errx(EXIT_FAILURE, "too large period: %s",
				     optarg);
			}
			break;
		case 'V':
			nilfs_cleanerd_version(progname);
			exit(EXIT_SUCCESS);
		default:
			nilfs_cleanerd_usage(progname);
			exit(EXIT_FAILURE);
		}
	}

	if (optind < argc) {
		const char *path = argv[optind++];

		dev = get_canonical_path(path);
		if (path && !dev)
			err(EXIT_FAILURE,
			    "failed to canonicalize device path %s", path);
	}

	if (optind < argc) {
		const char *path = argv[optind++];

		dir = get_canonical_path(path);
		if (path && !dir) {
			warn("failed to canonicalize directory path %s", path);
			status = EXIT_FAILURE;
			goto out_free;
		}
	}

	if (daemonize(0, 0) < 0) {
		warn(NULL);
		status = EXIT_FAILURE;
		goto out_free;
	}

	openlog(progname, LOG_PID, LOG_DAEMON);
	syslog(LOG_INFO, "start");

	if (oom_adjust() < 0)
		syslog(LOG_WARNING,
		       "adjusting the OOM killer failed: %m");

	nilfs_cleanerd = nilfs_cleanerd_create(dev, dir, conffile);
	if (nilfs_cleanerd == NULL) {
		syslog(LOG_ERR, "cannot create cleanerd on %s: %m", dev);
		status = EXIT_FAILURE;
		goto out_close_log;
	}

	if (!sigsetjmp(nilfs_cleanerd_env, 1)) {
		if (nilfs_cleanerd_clean_loop(nilfs_cleanerd) < 0)
			status = EXIT_FAILURE;
	}

	nilfs_cleanerd_destroy(nilfs_cleanerd);

out_close_log:
	syslog(LOG_INFO, "shutdown");
	closelog();

out_free:
	free(dir);	/* free(NULL) is just ignored */
	free(dev);

	exit(status);
}
