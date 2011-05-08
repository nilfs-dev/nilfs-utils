/*
 * cleanerd.c - NILFS cleaner daemon.
 *
 * Copyright (C) 2007-2008 Nippon Telegraph and Telephone Corporation.
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
 * You should have received a copy of the GNU General Public License
 * along with NILFS; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Written by Koji Sato <koji@osrg.net>.
 *
 * Maintained by Ryusuke Konishi <ryusuke@osrg.net> from 2008.
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

#if HAVE_STRING_H
#include <string.h>
#endif	/* HAVE_STRING_H */

#include <signal.h>

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

#include <errno.h>

#if HAVE_SYSLOG_H
#include <syslog.h>
#endif	/* HAVE_SYSLOG_H */

#include <setjmp.h>
#include <assert.h>
#include "vector.h"
#include "nilfs_gc.h"
#include "cnoconv.h"
#include "cleanerd.h"
#include "realpath.h"

#ifdef _GNU_SOURCE
#include <getopt.h>
const static struct option long_option[] = {
	{"conffile", required_argument, NULL, 'c'},
	{"help", no_argument, NULL, 'h'},
	/* internal option for mount.nilfs2 only */
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

static struct nilfs_cleanerd *nilfs_cleanerd;
static sigjmp_buf nilfs_cleanerd_env;
static volatile sig_atomic_t nilfs_cleanerd_reload_config;
static volatile unsigned long protection_period;

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
	setlogmask(LOG_UPTO(cleanerd->c_config.cf_log_priority));
}

/**
 * nilfs_cleanerd_config - load configuration file
 * @cleanerd: cleanerd object
 */
static int nilfs_cleanerd_config(struct nilfs_cleanerd *cleanerd)
{
	if (nilfs_cldconfig_read(&cleanerd->c_config,
				 cleanerd->c_conffile, cleanerd->c_nilfs) < 0)
		return -1;
#ifdef HAVE_MMAP
	if (cleanerd->c_config.cf_use_mmap)
		nilfs_opt_set_mmap(cleanerd->c_nilfs);
	else
		nilfs_opt_clear_mmap(cleanerd->c_nilfs);
#endif	/* HAVE_MMAP */
	nilfs_cleanerd_set_log_priority(cleanerd);

	if (protection_period != ULONG_MAX) {
		syslog(LOG_INFO, "override protection period to %lu",
		       protection_period);
		cleanerd->c_config.cf_protection_period = protection_period;
	}
	return 0;
}

/**
 * nilfs_cleanerd_reconfig - reload configuration file
 * @cleanerd: cleanerd object
 */
static int nilfs_cleanerd_reconfig(struct nilfs_cleanerd *cleanerd)
{
	struct nilfs_cldconfig *config = &cleanerd->c_config;
	time_t prev_prot_period = config->cf_protection_period;
	int ret;

	ret = nilfs_cleanerd_config(cleanerd);
	if (!ret) {
		if (config->cf_protection_period > prev_prot_period)
			nilfs_cnoconv_reset(cleanerd->c_cnoconv);

		cleanerd->c_ncleansegs =
			cleanerd->c_config.cf_nsegments_per_clean;
		cleanerd->c_cleaning_interval =
			cleanerd->c_config.cf_cleaning_interval;
	}
	return ret;
}

#ifndef PATH_MAX
#define PATH_MAX	8192
#endif	/* PATH_MAX */

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

	cleanerd->c_nilfs = nilfs_open(dev, dir,
				       NILFS_OPEN_RAW | NILFS_OPEN_RDWR |
				       NILFS_OPEN_GCLK);
	if (cleanerd->c_nilfs == NULL) {
		syslog(LOG_ERR, "cannot open nilfs on %s: %m", dev);
		goto out_cleanerd;
	}

	cleanerd->c_cnoconv = nilfs_cnoconv_create(cleanerd->c_nilfs);
	if (cleanerd->c_cnoconv == NULL) {
		syslog(LOG_ERR, "cannot create open nilfs on %s: %m", dev);
		goto out_nilfs;
	}

	cleanerd->c_conffile = strdup(conffile ? : NILFS_CLEANERD_CONFFILE);
	if (cleanerd->c_conffile == NULL)
		goto out_cnoconv;

	if (nilfs_cleanerd_config(cleanerd) < 0)
		goto out_conffile;

	/* success */
	return cleanerd;

	/* error */
out_conffile:
	free(cleanerd->c_conffile);
out_cnoconv:
	nilfs_cnoconv_destroy(cleanerd->c_cnoconv);
out_nilfs:
	nilfs_close(cleanerd->c_nilfs);

out_cleanerd:
	free(cleanerd);
	return NULL;
}

static void nilfs_cleanerd_destroy(struct nilfs_cleanerd *cleanerd)
{
	free(cleanerd->c_conffile);
	nilfs_cnoconv_destroy(cleanerd->c_cnoconv);
	nilfs_close(cleanerd->c_nilfs);
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

#define NILFS_CLEANERD_NSUINFO	512
#define NILFS_CLEANERD_NULLTIME (~(__u64)0)

/**
 * nilfs_cleanerd_select_segments - select segments to be reclaimed
 * @cleanerd: cleanerd object
 * @sustat: status information on segments
 * @segnums: array of segment numbers to store selected segments
 * @prottimep: place to store lower limit of protected period
 * @oldestp: place to store the oldest mod-time
 */
static ssize_t
nilfs_cleanerd_select_segments(struct nilfs_cleanerd *cleanerd,
			       struct nilfs_sustat *sustat, __u64 *segnums,
			       __u64 *prottimep, __u64 *oldestp)
{
	struct nilfs *nilfs;
	struct nilfs_cldconfig *config;
	struct nilfs_vector *smv;
	struct nilfs_segimp *sm;
	struct nilfs_suinfo si[NILFS_CLEANERD_NSUINFO];
	struct timeval tv;
	__u64 prottime, oldest;
	__u64 segnum;
	size_t count, nsegs;
	ssize_t nssegs, n;
	unsigned long long imp, thr;
	int i;

	nsegs = cleanerd->c_ncleansegs;
	nilfs = cleanerd->c_nilfs;
	config = &cleanerd->c_config;

	if ((smv = nilfs_vector_create(sizeof(struct nilfs_segimp))) == NULL)
		return -1;

	/* The segments that were more recently written to disk than
	 * prottime are not selected. */
	if (gettimeofday(&tv, NULL) < 0) {
		nssegs = -1;
		goto out;
	}
	prottime = tv.tv_sec - config->cf_protection_period;
	oldest = tv.tv_sec;

	/* The segments that have larger importance than thr are not
	 * selected. */
	thr = (config->cf_selection_policy.p_threshold != 0) ?
		config->cf_selection_policy.p_threshold :
		sustat->ss_nongc_ctime;

	for (segnum = 0; segnum < sustat->ss_nsegs; segnum += n) {
		count = (sustat->ss_nsegs - segnum < NILFS_CLEANERD_NSUINFO) ?
			sustat->ss_nsegs - segnum : NILFS_CLEANERD_NSUINFO;
		if ((n = nilfs_get_suinfo(nilfs, segnum, si, count)) < 0) {
			nssegs = n;
			goto out;
		}
		for (i = 0; i < n; i++) {
			if (nilfs_suinfo_reclaimable(&si[i]) &&
			    ((imp = (*config->cf_selection_policy.p_importance)(&si[i])) < thr)) {
				if (si[i].sui_lastmod < oldest)
					oldest = si[i].sui_lastmod;
				if (si[i].sui_lastmod < prottime) {
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
			       "inconsistent number of segments: %llu "
			       "(nsegs=%llu)",
			       (unsigned long long)nilfs_vector_get_size(smv),
			       (unsigned long long)sustat->ss_nsegs);
			break;
		}
	}
	nilfs_vector_sort(smv, nilfs_comp_segimp);

	nssegs = (nilfs_vector_get_size(smv) < nsegs) ?
		nilfs_vector_get_size(smv) : nsegs;
	for (i = 0; i < nssegs; i++) {
		sm = nilfs_vector_get_element(smv, i);
		assert(sm != NULL);
		segnums[i] = sm->si_segnum;
	}
	*prottimep = prottime;
	*oldestp = oldest < tv.tv_sec ? oldest : NILFS_CLEANERD_NULLTIME;

 out:
	nilfs_vector_destroy(smv);
	return nssegs;
}

#define DEVNULL	"/dev/null"
#define ROOTDIR	"/"

static int daemonize(int nochdir, int noclose, int nofork)
{
	pid_t pid;

	if (!nofork) {
		if ((pid = fork()) < 0)
			return -1;
		else if (pid != 0)
			/* parent */
			_exit(0);
	}

	/* child or nofork */
	if (setsid() < 0)
		return -1;

	/* umask(0); */

	if (!nochdir && (chdir(ROOTDIR) < 0))
		return -1;

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

	act.sa_handler = handle_sigterm;
	sigfillset(&act.sa_mask);
	act.sa_flags = 0;
	return sigaction(SIGTERM, &act, NULL);
}

static int set_sighup_handler(void)
{
	struct sigaction act;

	act.sa_handler = handle_sighup;
	sigfillset(&act.sa_mask);
	act.sa_flags = 0;
	return sigaction(SIGHUP, &act, NULL);
}

#define timeval_to_timespec(tv, ts)		\
do {						\
	(ts)->tv_sec = (tv)->tv_sec;		\
	(ts)->tv_nsec = (tv)->tv_usec * 1000;	\
} while (0)

static int nilfs_cleanerd_init_interval(struct nilfs_cleanerd *cleanerd)
{
	if (gettimeofday(&cleanerd->c_target, NULL) < 0) {
		syslog(LOG_ERR, "cannot get time: %m");
		return -1;
	}
	cleanerd->c_target.tv_sec += cleanerd->c_config.cf_cleaning_interval;
	return 0;
}

static int nilfs_cleanerd_recalc_interval(struct nilfs_cleanerd *cleanerd,
					  int nchosen, __u64 prottime,
					  __u64 oldest,
					  struct timespec *timeout)
{
	struct nilfs_cldconfig *config = &cleanerd->c_config;
	struct timeval curr, diff;

	if (gettimeofday(&curr, NULL) < 0) {
		syslog(LOG_ERR, "cannot get current time: %m");
		return -1;
	}

	if (nchosen == 0) {
		timeout->tv_nsec = 0;
		if (oldest == NILFS_CLEANERD_NULLTIME) {
			timeout->tv_sec = config->cf_protection_period + 1;
			cleanerd->c_running = 0;
		} else {
			timeout->tv_sec = oldest - prottime + 1;
		}
		return 0;
	}

	if (cleanerd->c_fallback) {
		cleanerd->c_target = curr;
		cleanerd->c_target.tv_sec += config->cf_retry_interval;
		timersub(&cleanerd->c_target, &curr, &diff);
		timeval_to_timespec(&diff, timeout);
		syslog(LOG_DEBUG, "retry interval");
		return 0;
	}
	/* timercmp() does not work for '>=' or '<='. */
	/* curr >= target */
	if (!timercmp(&curr, &cleanerd->c_target, <)) {
		cleanerd->c_target = curr;
		cleanerd->c_target.tv_sec += cleanerd->c_cleaning_interval;
		syslog(LOG_DEBUG, "adjust interval");
		return 1; /* skip a sleep */
	}
	timersub(&cleanerd->c_target, &curr, &diff);
	timeval_to_timespec(&diff, timeout);
	cleanerd->c_target.tv_sec += cleanerd->c_cleaning_interval;
	return 0;
}

static int nilfs_cleanerd_sleep(struct nilfs_cleanerd *cleanerd,
				struct timespec *timeout)
{
	syslog(LOG_DEBUG, "wait %ld.%09ld",
	       timeout->tv_sec, timeout->tv_nsec);
	if (nanosleep(timeout, NULL) < 0) {
		if (errno != EINTR) {
			syslog(LOG_ERR, "cannot sleep: %m");
			return -1;
		}
		cleanerd->c_running = 1;
	}
	syslog(LOG_DEBUG, "wake up");
	return 0;
}

static void nilfs_cleanerd_clean_check_pause(struct nilfs_cleanerd *cleanerd,
					     struct timespec *timeout)
{
	cleanerd->c_running = 0;
	timeout->tv_sec = cleanerd->c_config.cf_clean_check_interval;
	timeout->tv_nsec = 0;
	syslog(LOG_INFO, "pause (clean check)");
}

static void nilfs_cleanerd_clean_check_resume(struct nilfs_cleanerd *cleanerd)
{
	cleanerd->c_running = 1;
	syslog(LOG_INFO, "resume (clean check)");
}

static int nilfs_cleanerd_handle_clean_check(struct nilfs_cleanerd *cleanerd,
					     struct nilfs_sustat *sustat,
					     int r_segments,
					     struct timespec *timeout)
{
	if (cleanerd->c_running) {
		if (sustat->ss_ncleansegs >
		    cleanerd->c_config.cf_max_clean_segments + r_segments) {
			nilfs_cleanerd_clean_check_pause(cleanerd, timeout);
			return -1;
		}
	} else {
		if (sustat->ss_ncleansegs <
		    cleanerd->c_config.cf_min_clean_segments + r_segments)
			nilfs_cleanerd_clean_check_resume(cleanerd);
		else
			return -1;
	}

	if (sustat->ss_ncleansegs <
	    cleanerd->c_config.cf_min_clean_segments + r_segments) {
		cleanerd->c_ncleansegs =
			cleanerd->c_config.cf_mc_nsegments_per_clean;
		cleanerd->c_cleaning_interval =
			cleanerd->c_config.cf_mc_cleaning_interval;
	} else {
		cleanerd->c_ncleansegs =
			cleanerd->c_config.cf_nsegments_per_clean;
		cleanerd->c_cleaning_interval =
			cleanerd->c_config.cf_cleaning_interval;
	}

	return 0;
}

static ssize_t nilfs_cleanerd_clean_segments(struct nilfs_cleanerd *cleanerd,
					     __u64 *segnums, size_t nsegs,
					     __u64 protseq, __u64 prottime)
{
	nilfs_cno_t protcno;
	int ret, i;

	ret = nilfs_cnoconv_time2cno(cleanerd->c_cnoconv, prottime, &protcno);
	if (ret < 0) {
		syslog(LOG_ERR, "cannot convert protection time to checkpoint "
		       "number: %m");
		goto out;
	}

	ret = nilfs_reclaim_segment(cleanerd->c_nilfs, segnums, nsegs,
				    protseq, protcno);
	if (ret > 0) {
		for (i = 0; i < ret; i++)
			syslog(LOG_DEBUG, "segment %llu cleaned",
			       (unsigned long long)segnums[i]);
		cleanerd->c_fallback = 0;
	} else if (ret == 0) {
		syslog(LOG_DEBUG, "no segments cleaned");

	} else if (ret < 0 && errno == ENOMEM) {
		if (cleanerd->c_ncleansegs > 1)
			cleanerd->c_ncleansegs >>= 1;

		cleanerd->c_fallback = 1;
		ret = 0;
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
	__u64 r_segments, prev_nongc_ctime = 0, prottime = 0, oldest = 0;
	__u64 segnums[NILFS_CLDCONFIG_NSEGMENTS_PER_CLEAN_MAX];
	struct timespec timeout;
	sigset_t sigset;
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

	cleanerd->c_running = 1;
	cleanerd->c_fallback = 0;
	nilfs_cnoconv_reset(cleanerd->c_cnoconv);
	nilfs_gc_logger = syslog;

	ret = nilfs_cleanerd_init_interval(cleanerd);
	if (ret < 0)
		return -1;

	cleanerd->c_ncleansegs = cleanerd->c_config.cf_nsegments_per_clean;
	cleanerd->c_cleaning_interval = cleanerd->c_config.cf_cleaning_interval;

	r_segments = nilfs_get_reserved_segments(cleanerd->c_nilfs);

	if (cleanerd->c_config.cf_min_clean_segments > 0)
		nilfs_cleanerd_clean_check_pause(cleanerd, &timeout);

	while (1) {
		if (sigprocmask(SIG_BLOCK, &sigset, NULL) < 0) {
			syslog(LOG_ERR, "cannot set signal mask: %m");
			return -1;
		}

		if (nilfs_cleanerd_reload_config) {
			if (nilfs_cleanerd_reconfig(cleanerd)) {
				syslog(LOG_ERR, "cannot configure: %m");
				return -1;
			}
			nilfs_cleanerd_reload_config = 0;
			syslog(LOG_INFO, "configuration file reloaded");
		}

		if (nilfs_get_sustat(cleanerd->c_nilfs, &sustat) < 0) {
			syslog(LOG_ERR, "cannot get segment usage stat: %m");
			return -1;
		}

		if (cleanerd->c_config.cf_min_clean_segments > 0) {
			if (nilfs_cleanerd_handle_clean_check(
				cleanerd, &sustat, r_segments, &timeout) < 0)
				goto sleep;
		}

		if (sustat.ss_nongc_ctime != prev_nongc_ctime) {
			cleanerd->c_running = 1;
			prev_nongc_ctime = sustat.ss_nongc_ctime;
		}

		if (!cleanerd->c_running)
			goto sleep;

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
		if (ns > 0) {
			__u64 protseq = sustat.ss_prot_seq;

			ret = nilfs_cleanerd_clean_segments(
				cleanerd, segnums, ns, protseq, prottime);
			if (ret < 0)
				return -1;
		}

		ret = nilfs_cleanerd_recalc_interval(
			cleanerd, ns, prottime, oldest, &timeout);
		if (ret < 0)
			return -1;
		else if (ret > 0)
			continue;
 sleep:
		if (sigprocmask(SIG_UNBLOCK, &sigset, NULL) < 0) {
			syslog(LOG_ERR, "cannot set signal mask: %m");
			return -1;
		}

		ret = nilfs_cleanerd_sleep(cleanerd, &timeout);
		if (ret < 0)
			return -1;
	}
}

int main(int argc, char *argv[])
{
	char *progname, *conffile;
	char canonical[PATH_MAX + 2];
	const char *dev, *dir;
	char *endptr;
	int status, nofork, c;
#ifdef _GNU_SOURCE
	int option_index;
#endif	/* _GNU_SOURCE */

	progname = (strrchr(argv[0], '/') != NULL) ?
		strrchr(argv[0], '/') + 1 : argv[0];
	conffile = NILFS_CLEANERD_CONFFILE;
	nofork = 0;
	status = 0;
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
			exit(0);
		case 'n':
			/* internal option for mount.nilfs2 only */
			nofork = 1;
			break;
		case 'p':
			protection_period = strtoul(optarg, &endptr, 10);
			if (endptr == optarg || *endptr != '\0') {
				fprintf(stderr,
					"%s: invalid protection period: %s\n",
					progname, optarg);
				exit(1);
			} else if (protection_period == ULONG_MAX &&
				   errno == ERANGE) {
				fprintf(stderr, "%s: too large period: %s\n",
					progname, optarg);
				exit(1);
			}
			break;
		case 'V':
			nilfs_cleanerd_version(progname);
			exit(0);
		default:
			nilfs_cleanerd_usage(progname);
			exit(1);
		}
	}

	if (optind < argc)
		dev = argv[optind++];

	if (optind < argc)
		dir = argv[optind++];

	if (dev && myrealpath(dev, canonical, sizeof(canonical))) {
		dev = strdup(canonical);
		if (!dev) {
			fprintf(stderr, "%s: %s\n", progname, strerror(ENOMEM));
			exit(1);
		}
	}

	if (dir && myrealpath(dir, canonical, sizeof(canonical))) {
		dir = strdup(canonical);
		if (!dir) {
			fprintf(stderr, "%s: %s\n", progname, strerror(ENOMEM));
			exit(1);
		}
	}

	if (daemonize(0, 0, nofork) < 0) {
		fprintf(stderr, "%s: %s\n", progname, strerror(errno));
		exit(1);
	}

	openlog(progname, LOG_PID, LOG_DAEMON);
	syslog(LOG_INFO, "start");

	nilfs_cleanerd = nilfs_cleanerd_create(dev, dir, conffile);
	if (nilfs_cleanerd == NULL) {
		syslog(LOG_ERR, "cannot create cleanerd on %s: %s", dev,
		       strerror(errno));
		status = 1;
		goto out;
	}

	if (!sigsetjmp(nilfs_cleanerd_env, 1)) {
		if (nilfs_cleanerd_clean_loop(nilfs_cleanerd) < 0)
			status = 1;
	}

	nilfs_cleanerd_destroy(nilfs_cleanerd);

 out:
	syslog(LOG_INFO, "shutdown");
	closelog();

	exit(status);
}
