/*
 * nilfs-resize.c - resize nilfs2 volume
 *
 * Licensed under GPLv2: the complete text of the GNU General Public License
 * can be found in COPYING file of the nilfs-utils package.
 *
 * Copyright (C) 2011-2012 Nippon Telegraph and Telephone Corporation.
 *
 * Credits:
 *     Ryusuke Konishi <konishi.ryusuke@gmail.com>
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

#include <linux/nilfs2_ondisk.h>
#include <sys/stat.h>
#include <assert.h>
#include <stdarg.h>	/* va_start, va_end, vfprintf */
#include <errno.h>
#include <signal.h>
#include "nilfs.h"
#include "compat.h"
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

/* options */
static char *progname;
static int show_version_only;
static int verbose;
static int assume_yes;
static int show_progress = 1;

/* global variables */
static uint64_t devsize;
static unsigned int sector_size = 512;
static struct nilfs_sustat sustat;
static uint64_t trunc_start, trunc_end;

#define NILFS_RESIZE_NSUINFO	256
#define NILFS_RESIZE_NSEGNUMS	256

static struct nilfs_suinfo suinfo[NILFS_RESIZE_NSUINFO];
static uint64_t segnums[NILFS_RESIZE_NSEGNUMS];

/* filesystem parameters */
static struct nilfs_layout layout;

/* cleaner parameter for shrink */
static int nsegments_per_clean = 2;
static struct timespec clean_interval = { 0, 100000000 };   /* 100 msec */

/* progress meter */
static int pm_width = 60;
static int pm_barwidth;
static int pm_labelwidth = 10;
static uint64_t pm_max;
static uint64_t pm_done;
static int pm_curpos;
static int pm_in_progress;	/* 0: off, 1: on, -1: interrupted */
static const char *pm_label;


/**
 * nilfs_resize_logger - logger function to pass to libraries used
 * @priority: log level
 * @fmt:      format string
 * @...:      variable arguments that provide values for the conversion
 *            descriptors in the format string
 *
 * This function provides a message output function specific to the resize
 * command to relay and display message output from libraries.
 *
 * Here, messages with @priority greater than or equal to %LOG_ERR are
 * redirected to standard error and a newline is inserted at the end of the
 * message.
 */
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

/**
 * nilfs_resize_progress_show - show progress bar on standard error output
 *
 * This function displays a progress bar on the standard error output showing
 * the current progress and flushes the file descriptor.
 */
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

/**
 * nilfs_resize_progress_init - initialize the progress bar and display the
 *                              initial bar
 * @maxval: maximum progress value
 * @label:  label name displayed to the left of the progress bar
 *
 * This function initializes each variable representing the internal state
 * of the progress bar, displays the progress bar in its initial state with
 * a progress value of 0, and sets &pm_in_progress to one indicating that
 * the progress bar is being displayed.
 */
static void nilfs_resize_progress_init(uint64_t maxval, const char *label)
{
	pm_max = maxval;
	pm_done = 0;
	pm_curpos = 0;
	pm_barwidth = max_t(int, pm_width - pm_labelwidth - 3, 10);
	pm_label = label;

	nilfs_resize_progress_show();
	pm_in_progress = 1;
}

/**
 * nilfs_resize_progress_update - update progress bar display
 * @done: progress value
 *
 * This function does nothing if &pm_in_progress is off value (0).
 * Otherwise, updates the progress bar with the progress value given by
 * @done and sets the progress value in &pm_done and the updated cursor
 * position in &pm_curpos.
 * If the progress bar display is interrupted by other messages
 * (&pm_in_progress < 0), it calls nilfs_resize_progress_show() to redisplay
 * the entire progress bar.  Changing the cursor position is done locally
 * using combination of character output and the backspace character code.
 */
static void nilfs_resize_progress_update(uint64_t done)
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
	pos = pm_barwidth * min_t(uint64_t, done, pm_max) / pm_max;

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

/**
 * nilfs_resize_progress_inc - increase/decrease progress
 * @n: difference in progress (positive values are incremented, negative
 *     values are decremented)
 *
 * This function increases the progress by @n (decreases it if it is a
 * negative value) and updates the progress bar display with
 * nilfs_resize_progress_update().
 */
static void nilfs_resize_progress_inc(long n)
{
	nilfs_resize_progress_update(pm_done + n);
}

/**
 * nilfs_resize_progress_exit - stop updating progress bar
 *
 * This function does nothing if the progress bar is not in display mode.
 * If not, it sets &pm_in_progress to display disabled state (0).
 * If the progress bar is being updated and is not interrupted by other
 * messages, it outputs a newline code to the standard error output to
 * advance the line.
 */
static void nilfs_resize_progress_exit(void)
{
	if (!pm_in_progress)
		return;
	if (pm_in_progress > 0)
		fputc('\n', stderr);
	pm_in_progress = 0;
}

/**
 * myprintf - dedicated message output function (for coexistence with
 *            progress bar)
 * @fmt: format string
 * @...: variable arguments that provide values for the conversion
 *       descriptors in the format string
 *
 * This function is used to print a message concurrently with the progress
 * bar.  The message is relayed to standard error.
 * If the progress bar is being updated, it outputs a new line code to
 * change the line, and sets the progress bar status to "interrupted"
 * (&pm_in_progress = -1).
 */
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

/**
 * nilfs_resize_load_layout - get nilfs layout information
 * @nilfs: nilfs object
 *
 * This is a wrapper function for nilfs_get_layout() and stores layout
 * information in the global variable &layout.  This uses myprintf() to
 * output an error message when errors occur.
 *
 * Return: 0 on success, -1 on failure.
 */
static int nilfs_resize_load_layout(const struct nilfs *nilfs)
{
	ssize_t res;

	res = nilfs_get_layout(nilfs, &layout, sizeof(layout));
	if (unlikely(res < 0)) {
		myprintf("Error: failed to get layout information: %s\n",
			 strerror(errno));
		return -1;
	}
	assert(res >= sizeof(layout));
	return 0;
}

/**
 * nilfs_resize_update_sustat - update nilfs segment usage statistics
 * @nilfs: nilfs object
 *
 * This is a wrapper function for nilfs_get_sustat() and stores segment
 * usage statistics in the global variable &sustat.  This uses myprintf()
 * to output an error message when errors occur.
 *
 * Return: 0 on success, -1 on failure.
 */
static int nilfs_resize_update_sustat(struct nilfs *nilfs)
{
	int ret;

	ret = nilfs_get_sustat(nilfs, &sustat);
	if (unlikely(ret < 0)) {
		myprintf("Error: failed to get segment usage status: %s\n",
			 strerror(errno));
		return -1;
	}
	return 0;
}

/**
 * nilfs_resize_lock_cleaner - block signals and lock cleaner
 * @nilfs:  nilfs object
 * @sigset: place to store the old signal set
 *
 * This is a wrapper function for sigprocmask() and nilfs_lock_cleaner(),
 * and uses myprintf() to output an error message when errors occur.
 * On success, %SIGINT and %SIGTERM are blocked, and the cleaner is locked.
 *
 * Return: 0 on success, -1 on failure.
 */
static int nilfs_resize_lock_cleaner(struct nilfs *nilfs, sigset_t *sigset)
{
	sigset_t newset;
	int ret;

	sigemptyset(&newset);
	sigaddset(&newset, SIGINT);
	sigaddset(&newset, SIGTERM);
	ret = sigprocmask(SIG_BLOCK, &newset, sigset);
	if (unlikely(ret < 0)) {
		myprintf("Error: cannot block signals: %s\n", strerror(errno));
		return -1;
	}

	ret = nilfs_lock_cleaner(nilfs);
	if (unlikely(ret < 0)) {
		myprintf("Error: failed to lock cleaner: %s\n",
			 strerror(errno));
		sigprocmask(SIG_SETMASK, sigset, NULL);
		return -1;
	}
	return 0;
}

/**
 * nilfs_resize_unlock_cleaner - unlock cleaner and unblock signals
 * @nilfs:  nilfs object
 * @sigset: place to store the original signal set
 */
static void nilfs_resize_unlock_cleaner(struct nilfs *nilfs,
					const sigset_t *sigset)
{
	nilfs_unlock_cleaner(nilfs);
	sigprocmask(SIG_SETMASK, sigset, NULL);
}

/**
 * nilfs_resize_calc_nrsvsegs - calculate the number of reserved segments
 * @nsegs: number of segments
 */
static uint64_t nilfs_resize_calc_nrsvsegs(uint64_t nsegs)
{
	return max_t(uint64_t, NILFS_MIN_NRSVSEGS,
		     DIV_ROUND_UP(nsegs * layout.reserved_segments_ratio, 100));
}

/**
 * nilfs_resize_calc_size_of_segments - calculate the size of the area on the
 *                                      device where the segments are aligned
 * @nsegs: number of segments
 *
 * Return: total byte count of segment area (including the area of the first
 * super block at the beginning of the partition).
 */
static uint64_t nilfs_resize_calc_size_of_segments(uint64_t nsegs)
{
	return (nsegs * layout.blocks_per_segment) << layout.blocksize_bits;
}

/**
 * nilfs_resize_check_free_space - check if free space remains after resizing
 * @nilfs:    nilfs object
 * @newnsegs: number of segments
 *
 * This function checks whether there is free space for at least the number
 * of reserved segments when resizing to the number of segments specified by
 * @newnsegs, and outputs an error message if not.
 *
 * In verbose mode, this function outputs information about the remaining
 * space if there is enough space.
 *
 * Return: 0 if there is enough space, -1 otherwise.
 */
static int nilfs_resize_check_free_space(struct nilfs *nilfs,
					 uint64_t newnsegs)
{
	unsigned long long nrsvsegs, nsegs, nbytes;

	nrsvsegs = nilfs_resize_calc_nrsvsegs(newnsegs);

	if (sustat.ss_ncleansegs < sustat.ss_nsegs - newnsegs + nrsvsegs) {
		unsigned long long nsegs, nbytes;

		nsegs = (sustat.ss_nsegs - newnsegs) - sustat.ss_ncleansegs
			+ nrsvsegs;
		nbytes = nilfs_resize_calc_size_of_segments(newnsegs + nsegs) +
			4096;

		myprintf("Error: Insufficient free space (needs %llu more segment%s).\n"
			 "       The device size must be at least %llu bytes.\n",
			 nsegs, nsegs != 1 ? "s" : "", nbytes);
		return -1;
	} else if (verbose) {
		nsegs = sustat.ss_ncleansegs - (sustat.ss_nsegs - newnsegs)
			- nrsvsegs;
		nbytes = nilfs_resize_calc_size_of_segments(nsegs);
		myprintf("%llu free segment%s (%llu bytes) will be left after shrinkage.\n",
			 nsegs, nsegs != 1 ? "s" : "", nbytes);
	}
	return 0;
}

/**
 * nilfs_resize_restore_alloc_range - restore the allocatable range of segments
 * @nilfs: nilfs object
 *
 * This function uses the saved original layout information to reset the
 * range of allocatable segments back to before the start of shrinking.
 */
static void nilfs_resize_restore_alloc_range(struct nilfs *nilfs)
{
	int ret;

	ret = nilfs_resize_update_sustat(nilfs);
	if (unlikely(ret < 0))
		return;
	nilfs_set_alloc_range(nilfs, 0, layout.devsize);
}

/**
 * nilfs_resize_find_movable_segments - find movable segments within a
 *                                      specified range
 * @nilfs:      nilfs object
 * @start:      starting segment number of search range (inclusive)
 * @end:        ending segment number of search range (inclusive)
 * @segnumv:    array of 64-bit integers that stores the discovered segment
 *              numbers
 * @maxsegnums: maximum number of segments to search (maximum number of
 *              segment numbers that can be stored in @segnumv)
 *
 * This function searches for segments that are reclaimable and not
 * protected by superblock log pointers within the range of the segment
 * sequence specified by [@start, @end], and stores their numbers in
 * @segnumv.
 *
 * Return: on success, the number of movable segments discovered, -1 on
 * error.
 */
static ssize_t
nilfs_resize_find_movable_segments(struct nilfs *nilfs, uint64_t start,
				   uint64_t end, uint64_t *segnumv,
				   unsigned long maxsegnums)
{
	uint64_t segnum, *snp;
	unsigned long rest, count;
	ssize_t nsi, i;
	int ret;

	assert(start <= end);

	segnum = start;
	rest = min_t(uint64_t, maxsegnums, end - start + 1);
	for (snp = segnumv; rest > 0 && segnum <= end; ) {
		count = min_t(unsigned long, rest, NILFS_RESIZE_NSUINFO);
		nsi = nilfs_get_suinfo(nilfs, segnum, suinfo, count);
		if (unlikely(nsi < 0)) {
			myprintf("Error: operation failed during searching movable segments: %s\n",
				 strerror(errno));
			return -1;
		}
		for (i = 0; i < nsi; i++, segnum++) {
			if (!nilfs_suinfo_reclaimable(&suinfo[i]))
				continue;

			if (nilfs_suinfo_empty(&suinfo[i])) {
				/* Scrapped segments can be removed */
				*snp++ = segnum;
				rest--;
				continue;
			}

			ret = nilfs_segment_is_protected(nilfs, segnum,
							 sustat.ss_prot_seq);
			if (unlikely(ret < 0)) {
				myprintf("Error: failed to read segment: %s\n",
					 strerror(errno));
				return -1;
			}
			if (ret)
				continue;
			*snp++ = segnum;
			rest--;
		}
	}
	return snp - segnumv; /* return the number of found segments */
}

#if 0
/**
 * nilfs_resize_get_latest_segment - find latest segment within a specified
 *                                   range
 * @nilfs:      nilfs object
 * @start:      starting segment number of search range (inclusive)
 * @end:        ending segment number of search range (inclusive)
 * @segnump     place to store the number of the most recently updated
 *              segment
 *
 * This function searches for dirty (in-use) and non-error segments in
 * the range specified by [@start, @end] of the segment sequence, and
 * stores the number of the most recently updated segment in @segnump.
 *
 * Return: 1 on success, 0 if no segments were in use and not in error,
 * -1 on error.
 */
static int
nilfs_resize_get_latest_segment(struct nilfs *nilfs, uint64_t start,
				uint64_t end, uint64_t *segnump)
{
	uint64_t segnum, latest_segnum = (~(uint64_t)0);
	unsigned long count;
	ssize_t nsi, i;
	uint64_t latest_time = 0;
	int ret = 0;

	assert(start <= end);

	for (segnum = start; segnum <= end; segnum += nsi) {
		count = min_t(unsigned long, end - segnum + 1,
			      NILFS_RESIZE_NSUINFO);
		nsi = nilfs_get_suinfo(nilfs, segnum, suinfo, count);
		if (unlikely(nsi < 0)) {
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

/**
 * nilfs_resize_find_active_segments - find active (grabbed by log writer)
 *                                     segments within a specified range
 * @nilfs:      nilfs object
 * @start:      starting segment number of search range (inclusive)
 * @end:        ending segment number of search range (inclusive)
 * @segnumv:    array of 64-bit integers that stores the discovered segment
 *              numbers
 * @maxsegnums: maximum number of segments to search (maximum number of
 *              segment numbers that can be stored in @segnumv)
 *
 * This function searches for segments that are reclaimable and not
 * protected by superblock log pointers within the range of the segment
 * sequence specified by [@start, @end], and stores their numbers in
 * @segnumv.
 *
 * Return: on success, the number of movable segments discovered, -1 on
 * error.
 */
static ssize_t
nilfs_resize_find_active_segments(struct nilfs *nilfs, uint64_t start,
				  uint64_t end, uint64_t *segnumv,
				  unsigned long maxsegnums)
{
	uint64_t segnum, *snp;
	unsigned long rest, count;
	ssize_t nsi, i;

	assert(start <= end);

	segnum = start;
	rest = min_t(uint64_t, maxsegnums, end - start + 1);
	for (snp = segnumv; rest > 0 && segnum <= end; ) {
		count = min_t(unsigned long, rest, NILFS_RESIZE_NSUINFO);
		nsi = nilfs_get_suinfo(nilfs, segnum, suinfo, count);
		if (unlikely(nsi < 0)) {
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

/**
 * nilfs_resize_find_inuse_segments - find reclaimable segments within a
 *                                    specified range
 * @nilfs:      nilfs object
 * @start:      starting segment number of search range (inclusive)
 * @end:        ending segment number of search range (inclusive)
 * @segnumv:    array of 64-bit integers that stores the discovered segment
 *              numbers
 * @maxsegnums: maximum number of segments to search (maximum number of
 *              segment numbers that can be stored in @segnumv)
 *
 * This function searches for reclaimable (dirty, non-error, and non-active)
 * segments within the range of the segment sequence specified by
 * [@start, @end], and stores their numbers in @segnumv.
 *
 * Return: on success, the number of reclaimable segments discovered, -1 on
 * error.
 */
static ssize_t
nilfs_resize_find_inuse_segments(struct nilfs *nilfs, uint64_t start,
				 uint64_t end, uint64_t *segnumv,
				 unsigned long maxsegnums)
{
	uint64_t segnum, *snp;
	unsigned long rest, count;
	ssize_t nsi, i;

	assert(start <= end);

	segnum = start;
	rest = min_t(uint64_t, maxsegnums, end - start + 1);
	for (snp = segnumv; rest > 0 && segnum <= end; ) {
		count = min_t(unsigned long, rest, NILFS_RESIZE_NSUINFO);
		nsi = nilfs_get_suinfo(nilfs, segnum, suinfo, count);
		if (unlikely(nsi < 0)) {
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

/**
 * nilfs_resize_count_inuse_segments - count the number of reclaimable
 *                                     segments within a specified range
 * @nilfs:      nilfs object
 * @start:      starting segment number of search range (inclusive)
 * @end:        ending segment number of search range (inclusive)
 *
 * This function counts and returns the number of reclaimable (dirty,
 * non-error, and non-active) segments within the range of the segment
 * sequence specified by [@start, @end].
 *
 * Return: number of reclaimable segments on success, -1 on error.
 */
static ssize_t
nilfs_resize_count_inuse_segments(struct nilfs *nilfs, uint64_t start,
				  uint64_t end)
{
	uint64_t segnum;
	unsigned long rest, count;
	ssize_t nsi, i;
	ssize_t nfound = 0;

	assert(start <= end);

	segnum = start;
	rest = end - start + 1;
	while (rest > 0 && segnum <= end) {
		count = min_t(unsigned long, rest, NILFS_RESIZE_NSUINFO);
		nsi = nilfs_get_suinfo(nilfs, segnum, suinfo, count);
		if (unlikely(nsi < 0)) {
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

/**
 * nilfs_resize_verify_failure - check the cause of movement failure for a
 *                               segment number array
 * @nilfs:   nilfs object
 * @segnumv: array of segment numbers
 * @nsegs:   number of segment numbers stored in @segnumv
 *
 * This function examines the usage of each segment specified by the segment
 * number array @segnumv, determines why they are not movable (unreclaimable
 * and/or protected), and returns its summary.
 *
 * Return: OR value of the following reason bit flags.
 * * %NILFS_RESIZE_SEGMENT_PROTECTED      - Protected segments exist
 * * %NILFS_RESIZE_SEGMENT_UNRECLAIMABLE  - Unreclaimable segments exist
 */
static int nilfs_resize_verify_failure(struct nilfs *nilfs,
				       uint64_t *segnumv, unsigned long nsegs)
{
	struct nilfs_suinfo si;
	int reason = 0;
	int i, ret;

	for (i = 0; i < nsegs; i++) {
		if (nilfs_get_suinfo(nilfs, segnumv[i], &si, 1) == 1) {
			if (!nilfs_suinfo_reclaimable(&si))
				reason |= NILFS_RESIZE_SEGMENT_UNRECLAIMABLE;
			else if (nilfs_suinfo_empty(&si))
				continue;  /* Scrapped segment */
			if (nilfs_suinfo_active(&si)) {
				/*
				 * Active segments may not have been written
				 * either, so we determine them to be protected
				 * without reading the segment summary.
				 */
				reason |= NILFS_RESIZE_SEGMENT_PROTECTED;
				continue;
			}
		}
		ret = nilfs_segment_is_protected(nilfs, segnumv[i],
						 sustat.ss_prot_seq);
		if (ret > 0)
			reason |= NILFS_RESIZE_SEGMENT_PROTECTED;
	}
	return reason;
}

/**
 * nilfs_resize_move_segments - attempt to move segments and find out why
 *                              some segments cannot be moved
 * @nilfs:   nilfs object
 * @segnumv: array of segment numbers
 * @nsegs:   number of segment numbers stored in @segnumv
 * @reason:  place to store the reason as an OR value of bit flags, if
 *           there are segments that failed to move
 *
 * This function attempts to move each segment specified by the segment
 * number array @segnumv, and if a successfully moved (evicted) segment
 * is in the area to be truncated, it calls nilfs_resize_progress_inc()
 * to advance the displayed truncation progress by the number of successful
 * segments.
 *
 * If there are segments that fail to be moved and @reason is not %NULL, it
 * uses nilfs_resize_verify_failure() to check the reason for the failure
 * and stores it in @reason as an OR value of the following bit flags:
 * * %NILFS_RESIZE_SEGMENT_PROTECTED      - Protected segments exist
 * * %NILFS_RESIZE_SEGMENT_UNRECLAIMABLE  - Unreclaimable segments exist
 *
 * Return: Number of segments moved on success, -1 on error.
 */
static ssize_t nilfs_resize_move_segments(struct nilfs *nilfs,
					  uint64_t *segnumv,
					  unsigned long nsegs, int *reason)
{
	unsigned long rest = nsegs, nc;
	uint64_t *snp = segnumv;
	ssize_t nmoved = 0, i, nhits;
	int rv = 0;
	int ret;

	for (snp = segnumv; rest > 0; snp += nc, rest -= nc) {
		nc = min_t(unsigned long, rest, nsegments_per_clean);

		ret = nilfs_resize_update_sustat(nilfs);
		if (unlikely(ret < 0))
			return -1;

		ret = nilfs_reclaim_segment(nilfs, snp, nc,
					    sustat.ss_prot_seq, 0);
		if (unlikely(ret < 0))
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
	int ret = -1;

	nilfs_sync(nilfs, &cno);

	if (nilfs_freeze(nilfs) == 0 && nilfs_thaw(nilfs) == 0)
		ret = 0;

	nilfs_resize_update_sustat(nilfs);

	return ret;
}

/**
 * nilfs_resize_try_to_update_log_cursor - attempt to update log cursors
 * @nilfs:  nilfs object
 * @reason: string representing the trigger for updating log cursors
 *
 * This function updates "log cursors", the starting points of segment
 * chains pointed to by the two superblocks (the starting segments of log
 * tracking during recovery mount).  Updates are performed using sync,
 * freeze and thaw ioctls, which are effective for converging these
 * cursors.
 * Then, to obtain the latest information on the segments protected by the
 * log cursors, it updates the segment usage statistics &sustat by calling
 * nilfs_resize_update_sustat().
 *
 * In verbose mode, this function outputs a message containing the reason
 * for the execution before updating log cursors, and a message containing
 * the result after updating them.
 *
 * Return: 0 on success, -1 on failure.
 */
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

/**
 * nilfs_resize_reclaim_nibble - somehow move a specified number of segments
 *                               within a specified range or in front of it
 * @nilfs:     nilfs object
 * @start:     starting segment number of the segment range to reclaim
 * @end:       ending segment number of the segment range to reclaim
 * @count:     number of segments to attempt to reclaim
 * @unprotect: flag whether to attempt to unprotect protected segments by
 *             updating log cursors
 *
 * This function moves up to @count movable segments in the range specified
 * by [@start, @end].  If there are not enough movable segments in the range,
 * it tries to move movable segments in the [0, @start) range to force
 * active or protected segments out of the area to be truncated.
 * If no movable segments are found in both ranges, it attempts to update
 * log cursors once.
 *
 * If the @unprotect flag is true and no attempt has been made to update
 * log cursors, an attempt will be made even if the same or more segments
 * have been moved.
 *
 * Return: 0 if the specified number of segments can be moved, -1 if not.
 */
static int nilfs_resize_reclaim_nibble(struct nilfs *nilfs,
				       unsigned long long start,
				       unsigned long long end,
				       unsigned long count, int unprotect)
{
	uint64_t segnumv[2], segnum;
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
		if (unlikely(nfound < 0))
			goto failed;
		if (nfound == 0)
			break;

		segnum = segnumv[nfound - 1] + 1; /* set next segnum */
		nm = nilfs_resize_move_segments(nilfs, segnumv, nfound, NULL);
		if (unlikely(nm < 0))
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

/**
 * nilfs_resize_move_out_active_segments - kick active segments from
 *                                         specified range
 * @nilfs: nilfs object
 * @start: starting segment number of the range to be evicted
 * @end:   ending segment number of the range to be evicted
 *
 * This function kicks out active segments in the range specified by
 * [@start, @end] by calling nilfs_resize_reclaim_nibble() with a count
 * equal to that number.  This active segment eviction will be attempted
 * up to 5 times.
 *
 * Return: 0 if there is no active segment, 1 if the eviction was successful
 * in at least one attempt, or -1 in case of an error or if eviction cannot
 * be achieved.
 */
static int nilfs_resize_move_out_active_segments(struct nilfs *nilfs,
						 unsigned long long start,
						 unsigned long long end)
{
	static const struct timespec retry_interval = { 0, 500000000 };
							/* 500 msec */
	ssize_t nfound;
	int retrycnt = 0;
	int ret;

	while (1) {
		ret = nilfs_resize_update_sustat(nilfs);
		if (unlikely(ret < 0))
			return -1;

		nfound = nilfs_resize_find_active_segments(
			nilfs, start, end, segnums, NILFS_RESIZE_NSEGNUMS);
		if (unlikely(nfound < 0))
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
		ret = nilfs_resize_reclaim_nibble(nilfs, start, end, nfound, 0);
		if (unlikely(ret < 0))
			return -1;
		retrycnt++;
		nanosleep(&retry_interval, NULL);
	}
	return retrycnt > 0;
}

/**
 * nilfs_resize_reclaim_range - reclaim segments to shrink the file system
 *                              to a specified number of segments
 * @nilfs:    nilfs object
 * @newnsegs: target number of segments for shrink
 *
 * This function evicts active segments and in-use (reclaimable) segments
 * from the range that exceeds @newnsegs limit so that the used segment
 * space stays below the @newnsegs limit.
 *
 * It first tries to evict active segments from outside the range, and if
 * successful, reclaims the remaining reclaimable segments.
 * If part of the eviction of reclaimable segments fails due to the
 * presence of protected segments by log cursors, it attempts to remove
 * them using nilfs_resize_reclaim_nibble().
 *
 * While the progress bar is being displayed, the progress is evaluated
 * and the progress bar is updated between these operations.
 *
 * Return: 0 on success, -1 on failure.
 */
static int nilfs_resize_reclaim_range(struct nilfs *nilfs, uint64_t newnsegs)
{
	unsigned long long start, end, segnum;
	ssize_t nfound;
	int ret;

	ret = nilfs_resize_update_sustat(nilfs);
	if (unlikely(ret < 0))
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
	if (unlikely(ret < 0))
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
		if (unlikely(nfound < 0))
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
		if (unlikely(nmoved < 0)) {
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

/**
 * nilfs_print_resize_error - output error message when resize operation fails
 * @err:    error number
 * @shrink: expand or shrink flag (non-zero for shrink)
 */
static void nilfs_print_resize_error(int err, int shrink)
{
	myprintf("Error: failed to %s the filesystem: %s\n",
		 shrink ? "shrink" : "extend", strerror(err));
	if (err == ENOTTY)
		myprintf("       This kernel does not support the resize API.\n");
}

/**
 * nilfs_resize_prompt - output a confirmation prompt to confirm resizing,
 *                       prompting the user to enter "y" (yes) or something
 *                       else
 * @newsize: target device size after resizing (in bytes)
 */
static int nilfs_resize_prompt(unsigned long long newsize)
{
	int c;

	myprintf("Do you wish to proceed (y/N)? ");
	return ((c = getchar()) == 'y' || c == 'Y') ? 0 : -1;
}

/**
 * nilfs_shrink_online - shrink a mounted file system
 * @nilfs:   nilfs object
 * @device:  device pathname
 * @newsize: target device size after resizing (in bytes)
 *
 * This function shrinks the file system so that the device size can be
 * truncated to @newsize bytes.
 *
 * This function first calculates the maximum number of segments that can
 * truncate the file system below the target byte size, taking into account
 * the second superblock.
 *
 * Then, if it is possible to downsize from calculating the remaining
 * capacity, it prompts to confirm shrinking unless the "yes" option is
 * specified.
 *
 * If the user requests execution interactively (or the confirmation is
 * skipped with the "yes" option), it first sets the allocatable area of
 * new segments to [0, @newsize) by calling nilfs_set_alloc_range ioctl.
 *
 * Next, it counts the reclaimable segments (segments in use) in the area
 * to be truncated and displays the initial progress bar.
 *
 * It then calls nilfs_resize_reclaim_range() to move the data in the used
 * segments in the area to be truncated, and if successful, it locks
 * the cleaner and calls nilfs_resize ioctl to instruct the file system
 * to finalize the resize.  If resize ioctl fails with error number %EBUSY,
 * reclamation and resize finalization will be retried at most two more
 * times.
 *
 * Return: %EXIT_SUCCESS on success, %EXIT_FAILURE on failure.
 */
static int nilfs_shrink_online(struct nilfs *nilfs, const char *device,
			       unsigned long long newsize)
{
	static char *label = "progress";
	sigset_t sigset;
	int status = EXIT_FAILURE;
	unsigned long long newsb2off; /* new offset of secondary super block */
	uint64_t newnsegs;
	ssize_t nuses;
	unsigned retry;
	int ret;

	/* set logger callback */
	nilfs_gc_logger = nilfs_resize_logger;

	ret = nilfs_resize_update_sustat(nilfs);
	if (unlikely(ret < 0))
		return -1;

	myprintf("Partition size = %llu bytes.\n"
		 "Shrink the filesystem size from %llu bytes to %llu bytes.\n",
		 devsize, layout.devsize, newsize);

	if (newsize >= 4096) {
		newsb2off = NILFS_SB2_OFFSET_BYTES(newsize);
		newnsegs = (newsb2off >> layout.blocksize_bits) /
			layout.blocks_per_segment;
	} else {
		/*
		 * Set dummy parameters to make the size check below fail
		 * while avoiding underflow.
		 */
		newnsegs = 0;
	}

	ret = nilfs_resize_check_free_space(nilfs, newnsegs);
	if (ret < 0)
		goto out;

	if (newnsegs < sustat.ss_nsegs) {
		uint64_t truncsegs = sustat.ss_nsegs - newnsegs;

		myprintf("%llu segment%s will be truncated from segnum %llu.\n",
			 (unsigned long long)truncsegs, truncsegs != 1 ? "s" : "",
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

	ret = nilfs_set_alloc_range(nilfs, 0, newsize);
	if (unlikely(ret < 0)) {
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
		int err;

		/* shrinker retry loop */
		ret = nilfs_resize_reclaim_range(nilfs, newnsegs);
		if (unlikely(ret < 0))
			goto restore_alloc_range;

		ret = nilfs_resize_lock_cleaner(nilfs, &sigset);
		if (unlikely(ret < 0))
			goto restore_alloc_range;

		if (verbose)
			myprintf("Truncating segments.\n");

		ret = nilfs_resize(nilfs, newsize);
		err = errno;

		nilfs_resize_unlock_cleaner(nilfs, &sigset);
		if (likely(ret == 0)) {
			status = EXIT_SUCCESS;
			goto out;
		}

		if (err != EBUSY) {
			nilfs_print_resize_error(err, 1);
			goto restore_alloc_range;
		}

		ret = nilfs_resize_load_layout(nilfs);
		if (unlikely(ret < 0))
			goto restore_alloc_range;

		ret = nilfs_resize_update_sustat(nilfs);
		if (unlikely(ret < 0))
			goto restore_alloc_range;

		ret = nilfs_resize_check_free_space(nilfs, newnsegs);
		if (ret < 0)
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

/**
 * nilfs_extend_online - extend a mounted file system
 * @nilfs:   nilfs object
 * @device:  device pathname
 * @newsize: target device size after resizing (in bytes)
 *
 * This function expands the file system to use up to @newsize bytes on the
 * device.
 *
 * This function first prompts to confirm the expansion unless the "yes"
 * option is specified.
 *
 * If the user requests execution interactively (or the confirmation is
 * skipped with the "yes" option), it locks the cleaner and calls
 * nilfs_resize ioctl to instruct the file system to finalize the resize.
 *
 * Return: %EXIT_SUCCESS on success, %EXIT_FAILURE on failure.
 */
static int nilfs_extend_online(struct nilfs *nilfs, const char *device,
			       unsigned long long newsize)
{
	int status = EXIT_FAILURE;
	sigset_t sigset;
	int ret;

	myprintf("Partition size = %llu bytes.\n"
		 "Extend the filesystem size from %llu bytes to %llu bytes.\n",
		 devsize, layout.devsize, newsize);
	if (!assume_yes && nilfs_resize_prompt(newsize) < 0)
		goto out;

	ret = nilfs_resize_lock_cleaner(nilfs, &sigset);
	if (unlikely(ret < 0))
		goto out;

	ret = nilfs_resize(nilfs, newsize);
	if (unlikely(ret < 0)) {
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

/**
 * nilfs_resize_online - resize a mounted file system
 * @nilfs:   nilfs object
 * @device:  device pathname
 * @newsize: target device size after resizing (in bytes)
 *
 * This function resizes the file system so that the device size fits
 * @newsize bytes.
 *
 * It first opens nilfs file system with nilfs_open() and reads its
 * layout information.  Then, if @newsize is larger than the current
 * device size recognized by the file system, it calls
 * nilfs_extend_online(), or if it is smaller than the current device size,
 * calls nilfs_shrink_online().  The nilfs object is eventually closed using
 * nilfs_close().
 *
 * Return: %EXIT_SUCCESS on success, %EXIT_FAILURE on failure.
 */
static int nilfs_resize_online(const char *device, unsigned long long newsize)
{
	struct nilfs *nilfs;
	nilfs_cno_t cno;
	int status = EXIT_FAILURE;
	int ret;

	nilfs = nilfs_open(device, NULL,
			   NILFS_OPEN_RAW | NILFS_OPEN_RDWR | NILFS_OPEN_GCLK);
	if (nilfs == NULL) {
		myprintf("Error: cannot open NILFS on %s: %s\n", device,
			 strerror(errno));
		goto out;
	}

	ret = nilfs_resize_load_layout(nilfs);
	if (unlikely(ret < 0))
		goto out_unlock;

	if (newsize == layout.devsize) {
		myprintf("No need to resize the filesystem on %s.\n"
			 "It already fits the device.\n", device);
		status = EXIT_SUCCESS;
		goto out_unlock;
	}

	nilfs_sync(nilfs, &cno);

	if (newsize > layout.devsize)
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

/**
 * nilfs_resize_usage - show command usage
 */
static void nilfs_resize_usage(void)
{
	fprintf(stderr, NILFS_RESIZE_USAGE, progname);
}

/**
 * nilfs_resize_parse_options - parse command options
 * @argc: argument count of command line including command pathname
 * @argv: argument vector
 */
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

/**
 * nilfs_resize_parse_size - parse the size argument
 * @arg:   size argument string
 * @sizep: place to store the size
 */
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

/**
 * nilfs_resize_get_device_size - get the actual size of the device
 * @device: device pathname
 */
static int nilfs_resize_get_device_size(const char *device)
{
	int devfd, ret = -1;

	devfd = open(device, O_RDONLY);
	if (devfd < 0) {
		myprintf("Error: cannot open device: %s.\n", device);
		goto out;
	}

	ret = ioctl(devfd, BLKGETSIZE64, &devsize);
	if (unlikely(ret != 0)) {
		myprintf("Error: cannot get device size: %s.", device);
		goto out_close_dev;
	}
	ret = 0;

out_close_dev:
	close(devfd);
out:
	return ret;
}

/**
 * main - main function of nilfs-resize command
 * @argc: argument count of command line including command pathname
 * @argv: argument vector
 */
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
	if (unlikely(ret < 0))
		goto out;

	if (optind != argc) {
		ret = nilfs_resize_parse_size(argv[optind], &size);
		if (unlikely(ret < 0)) {
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

			size2 = size & ~(unsigned long long)(sector_size - 1);
			myprintf("size %llu is not aligned to sector size. truncated to %llu.\n",
				 size, size2);
			size = size2;
		}
	} else {
		size = devsize;
	}

	ret = check_mount(device);
	if (unlikely(ret < 0)) {
		myprintf("Error checking mount status of %s: %s\n", device,
			 strerror(errno));
	} else if (!ret) {
		myprintf("Error: %s is not currently mounted. Offline resizing\n"
			 "       is not supported at present.\n", device);
	} else {
		status = nilfs_resize_online(device, size);
	}
out:
	exit(status);
}


