/*
 * nilfs-resize.c - resize nilfs2 volume
 *
 * Licensed under GPLv2: the complete text of the GNU General Public License
 * can be found in COPYING file of the nilfs-utils package.
 *
 * Copyright (C) 2011-2012 Nippon Telegraph and Telephone Corporation.
 * Written by Ryusuke Konishi <konishi.ryusuke@lab.ntt.co.jp>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif	/* HAVE_CONFIG_H */

#include <stdio.h>

#if HAVE_STDLIB_H
#include <stdlib.h>
#endif	/* HAVE_STDLIB_H */

#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif	/* HAVE_SYS_TYPES_H */

#if HAVE_LINUX_TYPES_H
#include <linux/types.h>
#endif	/* HAVE_LINUX_TYPES_H */

#if HAVE_FCNTL_H
#include <fcntl.h>
#endif	/* HAVE_FCNTL_H */

#if HAVE_UNISTD_H
#include <unistd.h>
#endif	/* HAVE_UNISTD_H */

#if HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif	/* HAVE_SYS_IOCTL_H */

#if HAVE_LIMITS_H
#include <limits.h>
#endif	/* HAVE_LIMITS_H */

#if HAVE_STRING_H
#include <string.h>
#endif	/* HAVE_SYS_STRING_H */

#if HAVE_SYSLOG_H
#include <syslog.h>	/* LOG_ERR, and so forth */
#endif	/* HAVE_SYSLOG_H */

#if HAVE_TIME_H
#include <time.h>	/* timespec, nanosleep() */
#endif	/* HAVE_TIME_H */

#include <sys/stat.h>
#include <assert.h>
#include <stdarg.h>	/* va_start, va_end, vfprintf */
#include <errno.h>
#include <signal.h>
#include "nilfs.h"
#include "util.h"
#include "nilfs_gc.h"

extern int check_mount(const char *device);

#ifdef _GNU_SOURCE
#include <getopt.h>
static const struct option long_option[] = {
	{"help", no_argument, NULL, 'h'},
	{"verbose", no_argument, NULL, 'v'},
	{"yes", no_argument, NULL, 'y'},
	{"assume-yes", no_argument, NULL, 'y'},
	{"version", no_argument, NULL, 'V'},
	{NULL, 0, NULL, 0}
};
#define NILFS_RESIZE_USAGE						\
	"Usage: %s [options] device [size]\n"				\
	"  -h, --help\t\tdisplay this help and exit\n"			\
	"  -v, --verbose\t\tverbose mode\n"				\
	"  -y, --yes,--assume-yes\n"					\
	"            \t\tAssume Yes to all queries and do not prompt\n"	\
	"  -V, --version\t\tdisplay version and exit\n"
#else
#define NILFS_RESIZE_USAGE						\
	"Usage: %s [-h] [-v] [-y] [-V] device [size]\n"
#endif	/* _GNU_SOURCE */


/* get device size through ioctl */
#ifndef BLKGETSIZE64
#define BLKGETSIZE64	_IOR(0x12, 114, size_t)
#endif

#ifndef FIFREEZE
#define FIFREEZE	_IOWR('X', 119, int)
#define FITHAW		_IOWR('X', 120, int)
#endif

/* options */
static char *progname;
static int show_version_only;
static int verbose;
static int assume_yes;
static int show_progress = 1;

/* global variables */
static __u64 devsize;
static unsigned int sector_size = 512;
static struct nilfs_sustat sustat;
static __u64 trunc_start, trunc_end;

#define NILFS_RESIZE_NSUINFO	256
#define NILFS_RESIZE_NSEGNUMS	256

static struct nilfs_suinfo suinfo[NILFS_RESIZE_NSUINFO];
static __u64 segnums[NILFS_RESIZE_NSEGNUMS];

/* filesystem parameters */
static __u32 blocksize;
static __u32 blocksize_bits;

static __u64 fs_devsize;
static __u32 blocks_per_segment;
static __u32 rsvsegs_percentage;  /* reserved segment percentage */
static __u64 first_data_block;
static __u64 free_blocks_count;

/* cleaner parameter for shrink */
static int nsegments_per_clean = 2;
static struct timespec clean_interval = { 0, 100000000 };   /* 100 msec */

/* progress meter */
static int pm_width = 60;
static int pm_barwidth;
static int pm_labelwidth = 10;
static __u64 pm_max;
static __u64 pm_done;
static int pm_curpos;
static int pm_in_progress;	/* 0: off, 1: on, -1: interrupted */
static const char *pm_label;

static void nilfs_resize_logger(int priority, const char *fmt, ...)
{
	va_list args;

	if (priority > LOG_ERR)
		return;
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	fprintf(stderr, "\n");
	va_end(args);
}

static void nilfs_resize_progress_show(void)
{
	int i, len = strlen(pm_label);
	int delta;

	fwrite(pm_label, sizeof(char), min_t(int, len, pm_labelwidth), stderr);
	fputs(" |", stderr);

	for (i = 0; i < pm_curpos; i++)
		fputc('*', stderr);
	for (; i < pm_barwidth; i++)
		fputc('-', stderr);
	fputc('|', stderr);

	delta = pm_barwidth + 1 - pm_curpos;
	for (i = 0; i < delta; i++)
		fputc('\b', stderr);
	fflush(stderr);
}

static void nilfs_resize_progress_init(__u64 maxval, const char *label)
{
	pm_max = maxval;
	pm_done = 0;
	pm_curpos = 0;
	pm_barwidth = max_t(int, pm_width - pm_labelwidth - 3, 10);
	pm_label = label;

	nilfs_resize_progress_show();
	pm_in_progress = 1;
}

static void nilfs_resize_progress_update(__u64 done)
{
	int pos, delta;
	int i;

	if (!pm_in_progress)
		return;
	if (pm_in_progress < 0) {
		/* resuming */
		nilfs_resize_progress_show();
		pm_in_progress = 1;
	}
	pos = pm_barwidth * min_t(__u64, done, pm_max) / pm_max;

	if (pos > pm_curpos) {
		delta = pos - pm_curpos;
		for (i = 0; i < delta; i++)
			fputc('*', stderr);
	} else if (pos < pm_curpos) {
		delta = pm_curpos - pos;
		for (i = 0; i < delta; i++)
			fputc('\b', stderr);
		for (i = 0; i < delta; i++)
			fputc('-', stderr);
		for (i = 0; i < delta; i++)
			fputc('\b', stderr);
	}
	fflush(stderr);
	pm_curpos = pos;
	pm_done = done;
}

static void nilfs_resize_progress_inc(long n)
{
	nilfs_resize_progress_update(pm_done + n);
}

static void nilfs_resize_progress_exit(void)
{
	if (!pm_in_progress)
		return;
	if (pm_in_progress > 0)
		fputc('\n', stderr);
	pm_in_progress = 0;
}

static void myprintf(const char *fmt, ...)
{
	va_list args;

	if (pm_in_progress > 0) {
		fputc('\n', stderr);
		pm_in_progress = -1;
	}

	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
}

static void nilfs_resize_update_super(const struct nilfs_super_block *sb)
{
	blocksize_bits = le32_to_cpu(sb->s_log_block_size) + 10;
	blocksize = 1UL << blocksize_bits;
	blocks_per_segment = le32_to_cpu(sb->s_blocks_per_segment);

	fs_devsize = le64_to_cpu(sb->s_dev_size);
	rsvsegs_percentage = le32_to_cpu(sb->s_r_segments_percentage);
	first_data_block = le64_to_cpu(sb->s_first_data_block);
	free_blocks_count = le64_to_cpu(sb->s_free_blocks_count);
}

static int nilfs_resize_reload_super(struct nilfs *nilfs)
{
	struct nilfs_super_block *sb;

	sb = nilfs_sb_read(nilfs->n_devfd);
	if (!sb) {
		myprintf("Error: cannot read super block: %s\n",
			 strerror(errno));
		return -1;
	}
	nilfs_resize_update_super(sb);
	free(sb);
	return 0;
}

static int nilfs_resize_update_sustat(struct nilfs *nilfs)
{
	if (nilfs_get_sustat(nilfs, &sustat) < 0) {
		myprintf("Error: failed to get segment usage status: %s\n",
			 strerror(errno));
		return -1;
	}
	return 0;
}

static int nilfs_resize_segment_is_protected(struct nilfs *nilfs, __u64 segnum)
{
	void *segment;
	__u64 segseq;
	__u64 protseq = sustat.ss_prot_seq;
	int ret = 0;

	/* need updating sustat before testing */
	if (nilfs_get_segment(nilfs, segnum, &segment) < 0) {
		myprintf("Error: cannot read segment: %s\n", strerror(errno));
		return -1;
	}
	segseq = nilfs_get_segment_seqnum(nilfs, segment, segnum);
	if (cnt64_ge(segseq, protseq))
		ret = 1;
	nilfs_put_segment(nilfs, segment);
	return ret;
}

static int nilfs_resize_lock_cleaner(struct nilfs *nilfs, sigset_t *sigset)
{
	sigset_t newset;

	sigemptyset(&newset);
	sigaddset(&newset, SIGINT);
	sigaddset(&newset, SIGTERM);
	if (sigprocmask(SIG_BLOCK, &newset, sigset) < 0) {
		myprintf("Error: cannot block signals: %s\n", strerror(errno));
		return -1;
	}

	if (nilfs_lock_cleaner(nilfs) < 0) {
		myprintf("Error: failed to lock cleaner: %s\n",
			 strerror(errno));
		sigprocmask(SIG_SETMASK, sigset, NULL);
		return -1;
	}
	return 0;
}

static void nilfs_resize_unlock_cleaner(struct nilfs *nilfs,
					const sigset_t *sigset)
{
	nilfs_unlock_cleaner(nilfs);
	sigprocmask(SIG_SETMASK, sigset, NULL);
}

static __u64 nilfs_resize_calc_nrsvsegs(__u64 nsegs)
{
	return max_t(__u64, NILFS_MIN_NRSVSEGS,
		     DIV_ROUND_UP(nsegs * rsvsegs_percentage, 100));
}

static int nilfs_resize_check_free_space(struct nilfs *nilfs, __u64 newnsegs)
{
	unsigned long long nrsvsegs, nsegs, nbytes;

	nrsvsegs = nilfs_resize_calc_nrsvsegs(newnsegs);

	if (sustat.ss_ncleansegs < sustat.ss_nsegs - newnsegs + nrsvsegs) {
		unsigned long long nsegs, nbytes;

		nsegs = (sustat.ss_nsegs - newnsegs) - sustat.ss_ncleansegs
			+ nrsvsegs;
		nbytes  = (nsegs * blocks_per_segment) << blocksize_bits;
		myprintf("Error: the filesystem does not have enough free space.\n"
			 "       At least %llu more segments (%llu bytes) are required.\n",
			 nsegs, nbytes);
		return -1;
	} else if (verbose) {
		nsegs = sustat.ss_ncleansegs - (sustat.ss_nsegs - newnsegs)
			- nrsvsegs;
		nbytes  = (nsegs * blocks_per_segment) << blocksize_bits;
		myprintf("%llu free segments (%llu bytes) will be left after shrinkage.\n",
			 nsegs, nbytes);
	}
	return 0;
}

static void nilfs_resize_restore_alloc_range(struct nilfs *nilfs)
{
	if (nilfs_resize_update_sustat(nilfs) < 0)
		return;
	nilfs_set_alloc_range(nilfs, 0, fs_devsize);
}

static ssize_t
nilfs_resize_find_movable_segments(struct nilfs *nilfs, __u64 start,
				   __u64 end, __u64 *segnumv,
				   unsigned long maxsegnums)
{
	__u64 segnum, *snp;
	unsigned long rest, count;
	ssize_t nsi, i;
	int ret;

	assert(start <= end);

	segnum = start;
	rest = min_t(__u64, maxsegnums, end - start + 1);
	for (snp = segnumv; rest > 0 && segnum <= end; ) {
		count = min_t(unsigned long, rest, NILFS_RESIZE_NSUINFO);
		nsi = nilfs_get_suinfo(nilfs, segnum, suinfo, count);
		if (nsi < 0) {
			myprintf("Error: operation failed during searching movable segments: %s\n",
				 strerror(errno));
			return -1;
		}
		for (i = 0; i < nsi; i++, segnum++) {
			if (!nilfs_suinfo_reclaimable(&suinfo[i]))
				continue;
			ret = nilfs_resize_segment_is_protected(nilfs, segnum);
			if (ret < 0)
				return -1;
			else if (ret)
				continue;
			*snp++ = segnum;
			rest--;
		}
	}
	return snp - segnumv; /* return the number of found segments */
}

#if 0
static int
nilfs_resize_get_latest_segment(struct nilfs *nilfs, __u64 start, __u64 end,
				__u64 *segnump)
{
	__u64 segnum, latest_segnum = (~(__u64)0);
	unsigned long count;
	ssize_t nsi, i;
	__u64 latest_time = 0;
	int ret = 0;

	assert(start <= end);

	for (segnum = start; segnum <= end; segnum += nsi) {
		count = min_t(unsigned long, end - segnum + 1,
			      NILFS_RESIZE_NSUINFO);
		nsi = nilfs_get_suinfo(nilfs, segnum, suinfo, count);
		if (nsi < 0) {
			myprintf("Error: operation failed during searching latest segment: %s\n",
				 strerror(errno));
			return -1;
		}
		assert(nsi > 0);
		for (i = 0; i < nsi; i++) {
			if (nilfs_suinfo_dirty(&suinfo[i]) &&
			    !nilfs_suinfo_error(&suinfo[i]) &&
			    suinfo[i].sui_lastmod > latest_time) {
				latest_time = suinfo[i].sui_lastmod;
				latest_segnum = segnum + i;
			}
		}
	}
	if (latest_time > 0) {
		*segnump = latest_segnum;
		ret = 1;
	}
	return ret;
}
#endif

static ssize_t
nilfs_resize_find_active_segments(struct nilfs *nilfs, __u64 start, __u64 end,
				  __u64 *segnumv, unsigned long maxsegnums)
{
	__u64 segnum, *snp;
	unsigned long rest, count;
	ssize_t nsi, i;

	assert(start <= end);

	segnum = start;
	rest = min_t(__u64, maxsegnums, end - start + 1);
	for (snp = segnumv; rest > 0 && segnum <= end; ) {
		count = min_t(unsigned long, rest, NILFS_RESIZE_NSUINFO);
		nsi = nilfs_get_suinfo(nilfs, segnum, suinfo, count);
		if (nsi < 0) {
			myprintf("Error: operation failed during searching active segments: %s\n",
				 strerror(errno));
			return -1;
		}
		for (i = 0; i < nsi; i++, segnum++) {
			if (nilfs_suinfo_active(&suinfo[i]) &&
			    !nilfs_suinfo_error(&suinfo[i])) {
				*snp++ = segnum;
				rest--;
			}
		}
	}
	return snp - segnumv; /* return the number of found segments */
}

static ssize_t
nilfs_resize_find_inuse_segments(struct nilfs *nilfs, __u64 start, __u64 end,
				 __u64 *segnumv, unsigned long maxsegnums)
{
	__u64 segnum, *snp;
	unsigned long rest, count;
	ssize_t nsi, i;

	assert(start <= end);

	segnum = start;
	rest = min_t(__u64, maxsegnums, end - start + 1);
	for (snp = segnumv; rest > 0 && segnum <= end; ) {
		count = min_t(unsigned long, rest, NILFS_RESIZE_NSUINFO);
		nsi = nilfs_get_suinfo(nilfs, segnum, suinfo, count);
		if (nsi < 0) {
			myprintf("Error: operation failed during searching in-use segments: %s\n",
				 strerror(errno));
			return -1;
		}
		for (i = 0; i < nsi; i++, segnum++) {
			if (nilfs_suinfo_reclaimable(&suinfo[i])) {
				*snp++ = segnum;
				rest--;
			}
		}
	}
	return snp - segnumv; /* return the number of found segments */
}

static ssize_t
nilfs_resize_count_inuse_segments(struct nilfs *nilfs, __u64 start, __u64 end)
{
	__u64 segnum;
	unsigned long rest, count;
	ssize_t nsi, i;
	ssize_t nfound = 0;

	assert(start <= end);

	segnum = start;
	rest = end - start + 1;
	while (rest > 0 && segnum <= end) {
		count = min_t(unsigned long, rest, NILFS_RESIZE_NSUINFO);
		nsi = nilfs_get_suinfo(nilfs, segnum, suinfo, count);
		if (nsi < 0) {
			myprintf("Error: operation failed during counting in-use segments: %s\n",
				 strerror(errno));
			return -1;
		}
		for (i = 0; i < nsi; i++, segnum++) {
			if (nilfs_suinfo_reclaimable(&suinfo[i])) {
				nfound++;
				rest--;
			}
		}
	}
	return nfound; /* return the number of found segments */
}

#define	NILFS_RESIZE_SEGMENT_PROTECTED		0x01
#define NILFS_RESIZE_SEGMENT_UNRECLAIMABLE	0x02

static int nilfs_resize_verify_failure(struct nilfs *nilfs,
				       __u64 *segnumv, unsigned long nsegs)
{
	struct nilfs_suinfo si;
	int reason = 0;
	int i, ret;

	for (i = 0; i < nsegs; i++) {
		if (nilfs_get_suinfo(nilfs, segnumv[i], &si, 1) == 1) {
			if (!nilfs_suinfo_reclaimable(&si))
				reason |= NILFS_RESIZE_SEGMENT_UNRECLAIMABLE;
		}
		ret = nilfs_resize_segment_is_protected(nilfs, segnumv[i]);
		if (ret > 0)
			reason |= NILFS_RESIZE_SEGMENT_PROTECTED;
	}
	return reason;
}

static ssize_t nilfs_resize_move_segments(struct nilfs *nilfs,
					  __u64 *segnumv, unsigned long nsegs,
					  int *reason)
{
	unsigned long rest = nsegs, nc;
	__u64 *snp = segnumv;
	ssize_t nmoved = 0, i, nhits;
	int rv = 0;
	int ret;

	for (snp = segnumv; rest > 0; snp += nc, rest -= nc) {
		nc = min_t(unsigned long, rest, nsegments_per_clean);

		if (nilfs_resize_update_sustat(nilfs) < 0)
			return -1;

		ret = nilfs_reclaim_segment(nilfs, snp, nc,
					    sustat.ss_prot_seq, 0);
		if (ret < 0)
			return -1;

		nmoved += ret;

		/* updating progress bar */
		for (i = 0, nhits = 0; i < ret; i++) {
			if (snp[i] >= trunc_start && snp[i] <= trunc_end)
				nhits++;
		}
		if (nhits)
			nilfs_resize_progress_inc(nhits);

		/* check reason of gc failure */
		if (ret < nc && reason)
			rv |= nilfs_resize_verify_failure(nilfs, snp + ret,
							  nc - ret);

		nanosleep(&clean_interval, NULL);
	}
	if (reason)
		*reason = rv;
	return nmoved;
}

static int __nilfs_resize_try_update_log_cursor(struct nilfs *nilfs)
{
	nilfs_cno_t cno;
	int arg = 0;
	int ret = -1;

	nilfs_sync(nilfs, &cno);

	if (ioctl(nilfs->n_iocfd, FIFREEZE, &arg) == 0 &&
	    ioctl(nilfs->n_iocfd, FITHAW, &arg) == 0)
		ret = 0;

	nilfs_resize_update_sustat(nilfs);

	return ret;
}

static int nilfs_resize_try_update_log_cursor(struct nilfs *nilfs,
					      const char *reason)
{
	int ret;

	if (verbose)
		myprintf("%s.\nTrying to update log cursor ..", reason);

	ret = __nilfs_resize_try_update_log_cursor(nilfs);

	if (verbose)
		myprintf(ret == 0 ? " ok.\n" : " failed.\n");
	return ret;
}

static int nilfs_resize_reclaim_nibble(struct nilfs *nilfs,
				       unsigned long long start,
				       unsigned long long end,
				       unsigned long count, int unprotect)
{
	__u64 segnumv[2], segnum;
	ssize_t nfound, nmoved = 0;
	unsigned long nc;
	unsigned long long end2 = end;
	int log_cursor_updated = 0;
	int ret;

	segnum = start;

retry:
	while (segnum <= end) {
		ssize_t nm;

		nc = min_t(unsigned long, count - nmoved, ARRAY_SIZE(segnumv));
		nfound = nilfs_resize_find_movable_segments(
			nilfs, segnum, end, segnumv, nc);
		if (nfound < 0)
			goto failed;
		if (nfound == 0)
			break;

		segnum = segnumv[nfound - 1] + 1; /* set next segnum */
		nm = nilfs_resize_move_segments(nilfs, segnumv, nfound, NULL);
		if (nm < 0)
			goto failed;

		nmoved += nm;
		if (nmoved >= count) {
			if (unprotect && !log_cursor_updated) {
				nilfs_resize_try_update_log_cursor(
					nilfs, "Hit protected segment");
			}
			return 0;
		}
	}

	if (end >= start) {
		segnum = 0;
		end = start - 1;
		goto retry;
	}
	if (!log_cursor_updated) {
		ret = nilfs_resize_try_update_log_cursor(
			nilfs, "No movable segment");
		if (!ret) {
			segnum = start;
			end = end2;
			log_cursor_updated = 1;
			goto retry;
		}
	}
	myprintf("Error: couldn't move any segments.\n");
	return -1;
failed:
	myprintf("Error: operation failed during moving segments: %s\n",
		 strerror(errno));
	return -1;
}

static int nilfs_resize_move_out_active_segments(struct nilfs *nilfs,
						 unsigned long long start,
						 unsigned long long end)
{
	static const struct timespec retry_interval = { 0, 500000000 };
							/* 500 msec */
	ssize_t nfound;
	int retrycnt = 0;

	while (1) {
		if (nilfs_resize_update_sustat(nilfs) < 0)
			return -1;

		nfound = nilfs_resize_find_active_segments(
			nilfs, start, end, segnums, NILFS_RESIZE_NSEGNUMS);
		if (nfound < 0)
			return -1;
		if (!nfound)
			break;

		if (retrycnt >= 6) {
			myprintf("Error: Failed to move active segments -- give up.\n");
			return -1;
		}
		if (verbose && !retrycnt) {
			myprintf("Active segments are found in the range.\n"
				 "Trying to move them.\n");
		}
		if (nilfs_resize_reclaim_nibble(
			    nilfs, start, end, nfound, 0) < 0)
			return -1;
		retrycnt++;
		nanosleep(&retry_interval, NULL);
	}
	return retrycnt > 0;
}

static int nilfs_resize_reclaim_range(struct nilfs *nilfs, __u64 newnsegs)
{
	unsigned long long start, end, segnum;
	ssize_t nfound;
	int ret;

	if (nilfs_resize_update_sustat(nilfs) < 0)
		return -1;

	if (newnsegs > sustat.ss_nsegs) {
		myprintf("Error: Confused. Number of segments became larger than requested size.\n");
		return -1;
	}
	if (newnsegs == sustat.ss_nsegs)
		return 0;

	start = newnsegs;
	end = sustat.ss_nsegs - 1;

	/* Move out active segments first */
	ret = nilfs_resize_move_out_active_segments(nilfs, start, end);
	if (ret < 0)
		goto out;
	if (ret && pm_in_progress) {
		nfound = nilfs_resize_count_inuse_segments(nilfs, start, end);
		if (nfound >= 0) {
			if (nfound > pm_max)
				pm_max = nfound;
			nilfs_resize_progress_update(pm_max - nfound);
		}
	}

	ret = -1;
	segnum = start;
	while (segnum <= end) {
		ssize_t nfound;
		ssize_t nmoved;
		int reason = 0;

		nfound = nilfs_resize_find_inuse_segments(
			nilfs, segnum, end, segnums, NILFS_RESIZE_NSEGNUMS);
		if (nfound < 0)
			goto out;

		if (nfound == 0)
			break;

		/*
		 * Updates @segnum before calling nilfs_resize_move_segments()
		 * because segnums may be changed during reclamation.
		 */
		segnum = segnums[nfound - 1] + 1;
		nmoved = nilfs_resize_move_segments(
			nilfs, segnums, nfound, &reason);
		if (nmoved < 0) {
			myprintf("Error: operation failed during moving in-use segments: %s\n",
				 strerror(errno));
			goto out;
		}

		if (nmoved < nfound &&
		    (reason & NILFS_RESIZE_SEGMENT_PROTECTED)) {
			nilfs_resize_reclaim_nibble(nilfs, start, end, 2, 1);
			if (pm_in_progress) {
				nfound = nilfs_resize_count_inuse_segments(
					nilfs, start, end);
				if (nfound >= 0) {
					if (nfound > pm_max)
						pm_max = nfound;
					nilfs_resize_progress_update(
						pm_max - nfound);
				}
			}
		}
	}
	ret = 0;
out:
	return ret;
}

static void nilfs_print_resize_error(int err, int shrink)
{
	myprintf("Error: failed to %s the filesystem: %s\n",
		 shrink ? "shrink" : "extend", strerror(err));
	if (err == ENOTTY)
		myprintf("       This kernel does not support the resize API.\n");
}

static int nilfs_resize_prompt(unsigned long long newsize)
{
	int c;

	myprintf("Do you wish to proceed (y/N)? ");
	return ((c = getchar()) == 'y' || c == 'Y') ? 0 : -1;
}

static int nilfs_shrink_online(struct nilfs *nilfs, const char *device,
			       unsigned long long newsize)
{
	static char *label = "progress";
	sigset_t sigset;
	int status = EXIT_FAILURE;
	unsigned long long newsb2off; /* new offset of secondary super block */
	__u64 newnsegs, nuses;
	unsigned retry;

	/* set logger callback */
	nilfs_gc_logger = nilfs_resize_logger;

	if (nilfs_resize_update_sustat(nilfs) < 0)
		return -1;

	newsb2off = NILFS_SB2_OFFSET_BYTES(newsize);
	newnsegs = (newsb2off >> blocksize_bits) / blocks_per_segment;

	myprintf("Partition size = %llu bytes.\n"
		 "Shrink the filesystem size from %llu bytes to %llu bytes.\n",
		 devsize, fs_devsize, newsize);

	if (nilfs_resize_check_free_space(nilfs, newnsegs) < 0)
		goto out;

	if (newnsegs < sustat.ss_nsegs) {
		myprintf("%llu segments will be truncated from segnum %llu.\n",
			 (unsigned long long)sustat.ss_nsegs - newnsegs,
			 (unsigned long long)newnsegs);
	} else if (newnsegs == sustat.ss_nsegs) {
		myprintf("No segments will be truncated.\n");
	} else {
		myprintf("Error: Confused. Number of segments became larger than requested size:\n"
			 "  old-nsegs=%llu new-nsegs=%llu\n",
			 (unsigned long long)sustat.ss_nsegs,
			 (unsigned long long)newnsegs);
		goto out;
	}

	if (!assume_yes && nilfs_resize_prompt(newsize) < 0)
		goto out;

	if (nilfs_set_alloc_range(nilfs, 0, newsize) < 0) {
		myprintf("Error: failed to limit allocation range: %s\n",
			 strerror(errno));
		if (errno == ENOTTY)
			myprintf("       This kernel does not support the set allocation range API.\n");
		goto out;
	}

	if (newnsegs < sustat.ss_nsegs) {
		trunc_start = newnsegs;
		trunc_end = sustat.ss_nsegs - 1;

		nuses = nilfs_resize_count_inuse_segments(nilfs, newnsegs,
							  sustat.ss_nsegs - 1);
		if (nuses > 0 && show_progress) {
			myprintf("Moving %zd in-use segment%s.\n",
				 nuses, nuses > 1 ? "s" : "");
			nilfs_resize_progress_init(nuses, label);
		}
	}

	for (retry = 0; retry < 4; retry++) {
		int ret, err;

		/* shrinker retry loop */
		if (nilfs_resize_reclaim_range(nilfs, newnsegs) < 0)
			goto restore_alloc_range;

		if (nilfs_resize_lock_cleaner(nilfs, &sigset) < 0)
			goto restore_alloc_range;

		if (verbose)
			myprintf("Truncating segments.\n");

		ret = nilfs_resize(nilfs, newsize);
		err = errno;

		nilfs_resize_unlock_cleaner(nilfs, &sigset);
		if (ret == 0) {
			status = EXIT_SUCCESS;
			goto out;
		}

		if (err != EBUSY) {
			nilfs_print_resize_error(err, 1);
			goto restore_alloc_range;
		}

		if (nilfs_resize_reload_super(nilfs) < 0 ||
		    nilfs_resize_update_sustat(nilfs) < 0 ||
		    nilfs_resize_check_free_space(nilfs, newnsegs) < 0)
			goto restore_alloc_range;

		if (verbose) {
			myprintf("In-use segment found again. will retry moving them.. (retry = %d)\n",
				 retry + 1);
		}
	}
	myprintf("Error: retries failed -- give up\n");
out:
	nilfs_resize_progress_exit();
	return status;

restore_alloc_range:
	nilfs_resize_restore_alloc_range(nilfs);
	goto out;
}

static int nilfs_extend_online(struct nilfs *nilfs, const char *device,
			       unsigned long long newsize)
{
	int status = EXIT_FAILURE;
	sigset_t sigset;

	myprintf("Partition size = %llu bytes.\n"
		 "Extend the filesystem size from %llu bytes to %llu bytes.\n",
		 devsize, fs_devsize, newsize);
	if (!assume_yes && nilfs_resize_prompt(newsize) < 0)
		goto out;

	if (nilfs_resize_lock_cleaner(nilfs, &sigset) < 0)
		goto out;

	if (nilfs_resize(nilfs, newsize) < 0) {
		int err = errno;

		nilfs_print_resize_error(err, 0);
		goto out_unlock;
	}
	status = EXIT_SUCCESS;

out_unlock:
	nilfs_resize_unlock_cleaner(nilfs, &sigset);
out:
	return status;
}

static int nilfs_resize_online(const char *device, unsigned long long newsize)
{
	struct nilfs *nilfs;
	struct nilfs_super_block *sb;
	nilfs_cno_t cno;
	int status = EXIT_FAILURE;

	nilfs = nilfs_open(device, NULL,
			   NILFS_OPEN_RAW | NILFS_OPEN_RDWR | NILFS_OPEN_GCLK);
	if (nilfs == NULL) {
		myprintf("Error: cannot open NILFS on %s.\n", device);
		goto out;
	}

	sb = nilfs_get_sb(nilfs);
	assert(sb);
	nilfs_resize_update_super(sb);

	if (newsize == fs_devsize) {
		myprintf("No need to resize the filesystem on %s.\n"
			 "It already fits the device.\n", device);
		status = EXIT_SUCCESS;
		goto out_unlock;
	}

	nilfs_sync(nilfs, &cno);

	if (newsize > fs_devsize)
		status = nilfs_extend_online(nilfs, device, newsize);
	else
		status = nilfs_shrink_online(nilfs, device, newsize);

	if (status == EXIT_SUCCESS)
		myprintf("Done.\n");
	else
		myprintf("Aborted.\n");

out_unlock:
	nilfs_close(nilfs);
out:
	return status;
}

static void nilfs_resize_usage(void)
{
	fprintf(stderr, NILFS_RESIZE_USAGE, progname);
}

static void nilfs_resize_parse_options(int argc, char *argv[])
{
#ifdef _GNU_SOURCE
	int option_index;
#endif	/* _GNU_SOURCE */
	int c;

#ifdef _GNU_SOURCE
	while ((c = getopt_long(argc, argv, "hvyMPV",
				long_option, &option_index)) >= 0) {
#else
	while ((c = getopt(argc, argv, "hvyMPV")) >= 0) {
#endif	/* _GNU_SOURCE */
		switch (c) {
		case 'h':
			nilfs_resize_usage();
			exit(EXIT_SUCCESS);
			break;
		case 'v':
			verbose = 1;
			break;
		case 'y':
			assume_yes = 1;
			break;
		case 'V':
			show_version_only = 1;
			break;
		default:
			myprintf("Error: invalid option -- %c\n", optopt);
			exit(EXIT_FAILURE);
		}
	}
}

static int nilfs_resize_parse_size(const char *arg, unsigned long long *sizep)
{
	unsigned long long size;
	char *endptr;

	assert(arg && *arg != '\0');

	size = strtoull(arg, &endptr, 0);
	if (*endptr == '\0') {
		;
	} else if (endptr[1] == '\0') {
		switch (endptr[0]) {
		case 's':
			size <<= 9;
			break;
		case 'K':
			size <<= 10;
			break;
		case 'M':
			size <<= 20;
			break;
		case 'G':
			size <<= 30;
			break;
		case 'T':
			size <<= 40;
			break;
		case 'P':
			size <<= 50;
			break;
		default:
			return -1;
		}
	} else {
		return -1;
	}
	*sizep = size;
	return 0;
}

static int nilfs_resize_get_device_size(const char *device)
{
	int devfd, ret = -1;

	devfd = open(device, O_RDONLY);
	if (devfd < 0) {
		myprintf("Error: cannot open device: %s.\n", device);
		goto out;
	}

	if (ioctl(devfd, BLKGETSIZE64, &devsize) != 0) {
		myprintf("Error: cannot get device size: %s.", device);
		goto out_close_dev;
	}
	ret = 0;

out_close_dev:
	close(devfd);
out:
	return ret;
}

int main(int argc, char *argv[])
{
	char *last;
	unsigned long long size;
	struct stat statbuf;
	char *device;
	int status, ret;

	last = strrchr(argv[0], '/');
	progname = last ? last + 1 : argv[0];

	nilfs_resize_parse_options(argc, argv);
	if (show_version_only) {
		myprintf("%s version %s\n", progname, PACKAGE_VERSION);
		status = EXIT_SUCCESS;
		goto out;
	}

	if (optind == argc) {
		nilfs_resize_usage();
		status = EXIT_FAILURE;
		goto out;
	}

	status = EXIT_FAILURE;

	device = argv[optind++];
	if (stat(device, &statbuf) < 0) {
		myprintf("Error: cannot find %s: %s.\n", device,
			 strerror(errno));
		goto out;
	} else if (!S_ISBLK(statbuf.st_mode)) {
		myprintf("Error: device must be a block device.\n");
		goto out;
	}

	ret = nilfs_resize_get_device_size(device);
	if (ret < 0)
		goto out;

	if (optind != argc) {
		ret = nilfs_resize_parse_size(argv[optind], &size);
		if (ret < 0) {
			myprintf("Error: bad size argument: %s\n",
				 argv[optind]);
			goto out;
		}
		if (size > devsize) {
			myprintf("Error: size larger than partition size (%llu bytes).\n",
				 devsize);
			goto out;
		}

		optind++;
		if (optind < argc) {
			myprintf("Error: too many arguments.\n");
			goto out;
		}

		if (size & (sector_size - 1)) {
			unsigned long long size2;

			size2 = size & ~(sector_size - 1);
			myprintf("size %llu is not aligned to sector size. truncated to %llu.\n",
				 size, size2);
			size = size2;
		}
	} else {
		size = devsize;
	}


	if (check_mount(device) == 0) {
		myprintf("Error: %s is not currently mounted. Offline resizing\n"
			 "       is not supported at present.\n", device);
		goto out;
	} else {
		status = nilfs_resize_online(device, size);
	}
out:
	exit(status);
}


