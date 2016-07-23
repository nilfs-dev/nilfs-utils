/*
 * nilfs-clean.c - run garbage collector for nilfs2 volume
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
#include <setjmp.h>
#include <assert.h>
#include <stdarg.h>	/* va_start, va_end, vfprintf */
#include <errno.h>
#include <signal.h>
#include "nls.h"
#include "nilfs.h"
#include "nilfs_cleaner.h"
#include "parser.h"

#ifdef _GNU_SOURCE
#include <getopt.h>
static const struct option long_option[] = {
	{"break", no_argument, NULL, 'b'},
	{"reload", optional_argument, NULL, 'c'},
	{"help", no_argument, NULL, 'h'},
	{"status", no_argument, NULL, 'l'},
	{"protection-period", required_argument, NULL, 'p'},
	{"quit", no_argument, NULL, 'q'},
	{"resume", no_argument, NULL, 'r'},
	{"stop", no_argument, NULL, 'b'},
	{"suspend", no_argument, NULL, 's'},
	{"speed", required_argument, NULL, 'S'},
	{"min-reclaimable-blocks", required_argument, NULL, 'm'},
	{"verbose", no_argument, NULL, 'v'},
	{"version", no_argument, NULL, 'V'},
	{NULL, 0, NULL, 0}
};
#define NILFS_CLEAN_USAGE						\
	"Usage: %s [options] [device]\n"				\
	"  -b, --break,--stop\tstop running cleaner\n"			\
	"  -c, --reload[=CONFFILE]\n"					\
	"            \t\treload config\n"				\
	"  -h, --help\t\tdisplay this help and exit\n"			\
	"  -l, --status\t\tdisplay cleaner status\n"			\
	"  -p, --protection-period=SECONDS\n"				\
	"               \t\tspecify protection period\n"		\
	"  -m, --min-reclaimable-blocks=COUNT[%%]\n"			\
	"               \t\tset minimum number of reclaimable blocks\n"	\
	"               \t\tbefore a segment can be cleaned\n"		\
	"  -q, --quit\t\tshutdown cleaner\n"				\
	"  -r, --resume\t\tresume cleaner\n"				\
	"  -s, --suspend\t\tsuspend cleaner\n"				\
	"  -S, --speed=COUNT[/SECONDS]\n"				\
	"               \t\tset GC speed\n"				\
	"  -v, --verbose\t\tverbose mode\n"				\
	"  -V, --version\t\tdisplay version and exit\n"
#else
#define NILFS_CLEAN_USAGE						  \
	"Usage: %s [-b] [-c [conffile]] [-h] [-l] [-m blocks]\n"	  \
	"          [-p protection-period] [-q] [-r] [-s] [-S gc-speed]\n" \
	"          [-v] [-V] [device]\n"
#endif	/* _GNU_SOURCE */


enum {
	NILFS_CLEAN_CMD_RUN,
	NILFS_CLEAN_CMD_INFO,
	NILFS_CLEAN_CMD_SUSPEND,
	NILFS_CLEAN_CMD_RESUME,
	NILFS_CLEAN_CMD_RELOAD,
	NILFS_CLEAN_CMD_STOP,
	NILFS_CLEAN_CMD_SHUTDOWN,
};

/* options */
static char *progname;
static int show_version_only;
static int verbose;
static int clean_cmd = NILFS_CLEAN_CMD_RUN;
static const char *conffile;

static unsigned long protection_period = ULONG_MAX;
static int nsegments_per_clean = 2;
static struct timespec cleaning_interval = { 0, 100000000 };   /* 100 msec */
static unsigned long min_reclaimable_blocks = ULONG_MAX;
static unsigned char min_reclaimable_blocks_unit = NILFS_CLEANER_ARG_UNIT_NONE;

static sigjmp_buf nilfs_clean_env;
static struct nilfs_cleaner *nilfs_cleaner;

/* cleaner parameter */

static void nilfs_clean_logger(int priority, const char *fmt, ...)
{
	va_list args;

	if ((verbose && priority > LOG_INFO) || priority >= LOG_INFO)
		return;
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	fputs(_("\n"), stderr);
	va_end(args);
}

static void myprintf(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
}

static RETSIGTYPE nilfs_clean_escape(int signum)
{
	siglongjmp(nilfs_clean_env, 1);
}

static int nilfs_clean_do_run(struct nilfs_cleaner *cleaner)
{
	struct nilfs_cleaner_args args;

	args.npasses = 1;
	args.nsegments_per_clean = nsegments_per_clean;
	args.cleaning_interval = cleaning_interval.tv_sec;
	args.cleaning_interval_nsec = cleaning_interval.tv_nsec;
	args.valid = (NILFS_CLEANER_ARG_NPASSES |
		      NILFS_CLEANER_ARG_CLEANING_INTERVAL |
		      NILFS_CLEANER_ARG_NSEGMENTS_PER_CLEAN);

	if (protection_period != ULONG_MAX) {
		args.protection_period = protection_period;
		args.valid |= NILFS_CLEANER_ARG_PROTECTION_PERIOD;
	}

	if (min_reclaimable_blocks != ULONG_MAX) {
		args.min_reclaimable_blocks = min_reclaimable_blocks;
		args.min_reclaimable_blocks_unit = min_reclaimable_blocks_unit;
		args.valid |= NILFS_CLEANER_ARG_MIN_RECLAIMABLE_BLOCKS;
	}

	if (nilfs_cleaner_run(cleaner, &args, NULL) < 0) {
		myprintf(_("Error: cannot run cleaner: %s\n"),
			 strerror(errno));
		return -1;
	}
	return 0;
}

static int nilfs_clean_do_getinfo(struct nilfs_cleaner *cleaner)
{
	int cleaner_status;

	if (nilfs_cleaner_get_status(cleaner, &cleaner_status) < 0) {
		myprintf(_("Error: cannot get cleaner status: %s\n"),
			 strerror(errno));
		return -1;
	}
	switch (cleaner_status) {
	case NILFS_CLEANER_STATUS_IDLE:
		puts(_("idle"));
		break;
	case NILFS_CLEANER_STATUS_RUNNING:
		puts(_("running"));
		break;
	case NILFS_CLEANER_STATUS_SUSPENDED:
		puts(_("suspended"));
		break;
	default:
		printf(_("%d (unknown)\n"), cleaner_status);
	}
	return 0;
}

static int nilfs_clean_do_suspend(struct nilfs_cleaner *cleaner)
{
	if (nilfs_cleaner_suspend(cleaner) < 0) {
		myprintf(_("Error: suspend failed: %s\n"), strerror(errno));
		return -1;
	}
	return 0;
}

static int nilfs_clean_do_resume(struct nilfs_cleaner *cleaner)
{
	if (nilfs_cleaner_resume(cleaner) < 0) {
		myprintf(_("Error: resume failed: %s\n"), strerror(errno));
		return -1;
	}
	return 0;
}

static int nilfs_clean_do_reload(struct nilfs_cleaner *cleaner)
{
	if (nilfs_cleaner_reload(cleaner, conffile) < 0) {
		myprintf(_("Error: reload failed: %s\n"), strerror(errno));
		return -1;
	}
	return 0;
}

static int nilfs_clean_do_stop(struct nilfs_cleaner *cleaner)
{
	if (nilfs_cleaner_stop(cleaner) < 0) {
		myprintf(_("Error: stop failed: %s\n"), strerror(errno));
		return -1;
	}
	return 0;
}

static int nilfs_clean_do_shutdown(struct nilfs_cleaner *cleaner)
{
	if (nilfs_cleaner_shutdown(cleaner) < 0) {
		myprintf(_("Error: shutdown failed: %s\n"), strerror(errno));
		return -1;
	}
	return 0;
}

static int nilfs_clean_request(struct nilfs_cleaner *cleaner)
{
	int status = EXIT_FAILURE;
	int ret;

	switch (clean_cmd) {
	case NILFS_CLEAN_CMD_RUN:
		ret = nilfs_clean_do_run(cleaner);
		break;
	case NILFS_CLEAN_CMD_INFO:
		ret = nilfs_clean_do_getinfo(cleaner);
		break;
	case NILFS_CLEAN_CMD_SUSPEND:
		ret = nilfs_clean_do_suspend(cleaner);
		break;
	case NILFS_CLEAN_CMD_RESUME:
		ret = nilfs_clean_do_resume(cleaner);
		break;
	case NILFS_CLEAN_CMD_RELOAD:
		ret = nilfs_clean_do_reload(cleaner);
		break;
	case NILFS_CLEAN_CMD_STOP:
		ret = nilfs_clean_do_stop(cleaner);
		break;
	case NILFS_CLEAN_CMD_SHUTDOWN:
		ret = nilfs_clean_do_shutdown(cleaner);
		break;
	default:
		goto out;
	}
	if (!ret)
		status = EXIT_SUCCESS;
out:
	return status;
}

static int nilfs_do_clean(const char *device)
{
	struct sigaction act, oldact[3];
	int status = EXIT_FAILURE;

	nilfs_cleaner = nilfs_cleaner_open(device, NULL,
					   NILFS_CLEANER_OPEN_QUEUE);
	if (!nilfs_cleaner)
		goto out;

	if (!sigsetjmp(nilfs_clean_env, 1)) {
		memset(&act, 0, sizeof(act));
		sigfillset(&act.sa_mask);
		act.sa_handler = nilfs_clean_escape;
		act.sa_flags = 0;

		if (sigaction(SIGINT, &act, &oldact[0]) < 0 ||
		    sigaction(SIGTERM, &act, &oldact[1]) < 0 ||
		    sigaction(SIGHUP, &act, &oldact[2]) < 0)
			siglongjmp(nilfs_clean_env, 1);

		status = nilfs_clean_request(nilfs_cleaner);

		sigaction(SIGINT, &oldact[0], NULL);
		sigaction(SIGTERM, &oldact[1], NULL);
		sigaction(SIGHUP, &oldact[2], NULL);
	}

	nilfs_cleaner_close(nilfs_cleaner);
out:
	return status;
}

static void nilfs_clean_usage(void)
{
	fprintf(stderr, NILFS_CLEAN_USAGE, progname);
}

static int nilfs_clean_parse_gcspeed(const char *arg)
{
	unsigned long nsegs;
	unsigned long interval;
	double interval2;
	char *endptr, *endptr2;

	nsegs = strtoul(arg, &endptr, 10);
	if (endptr == arg || (endptr[0] != '/' && endptr[0] != '\0'))
		goto failed;

	if (nsegs == ULONG_MAX)
		goto failed_too_large;

	if (endptr[0] == '/') {
		interval = strtoul(&endptr[1], &endptr2, 10);
		if (endptr2 == &endptr[1])
			goto failed;
		if (endptr2[0] == '.') {
			errno = 0;
			interval2 = strtod(&endptr[1], &endptr2);
			if (endptr2 == &endptr[1] || endptr2[0] != '\0')
				goto failed;
			if (errno == ERANGE)
				goto failed_too_large;
			cleaning_interval.tv_sec = interval;
			cleaning_interval.tv_nsec =
				(interval2 - interval) * 1000000000;
		} else if (endptr2[0] != '\0') {
			goto failed;
		} else {
			if (interval == ULONG_MAX)
				goto failed_too_large;
			cleaning_interval.tv_sec = interval;
			cleaning_interval.tv_nsec = 0;
		}
	} else {
		cleaning_interval.tv_sec = 1;
		cleaning_interval.tv_nsec = 0;
	}
	nsegments_per_clean = nsegs;
	return 0;
failed:
	myprintf(_("Error: invalid gc speed: %s\n"), arg);
	return -1;

failed_too_large:
	myprintf(_("Error: value too large: %s\n"), arg);
	return -1;
}

static int nilfs_clean_parse_min_reclaimable(const char *arg)
{
	unsigned long blocks;
	char *endptr;

	blocks = strtoul(arg, &endptr, 10);
	if (endptr == arg || (endptr[0] != '\0' && endptr[0] != '%')) {
		myprintf(_("Error: invalid reclaimable blocks: %s\n"), arg);
		return -1;
	}

	if (blocks == ULONG_MAX) {
		myprintf(_("Error: value too large: %s\n"), arg);
		return -1;
	}

	if (endptr[0] == '%') {
		if (blocks > 100) {
			myprintf(_("Error: percent value can't be > 100: %s\n"),
					arg);
			return -1;
		}
		min_reclaimable_blocks_unit = NILFS_CLEANER_ARG_UNIT_PERCENT;
	} else {
		min_reclaimable_blocks_unit = NILFS_CLEANER_ARG_UNIT_NONE;
	}

	min_reclaimable_blocks = blocks;
	return 0;
}

static void nilfs_clean_parse_options(int argc, char *argv[])
{
#ifdef _GNU_SOURCE
	int option_index;
#endif	/* _GNU_SOURCE */
	int c, ret;

#ifdef _GNU_SOURCE
	while ((c = getopt_long(argc, argv, "bc::hlm:p:qrsS:vV",
				long_option, &option_index)) >= 0) {
#else
	while ((c = getopt(argc, argv, "bc::hlm:p:qrsS:vV")) >= 0) {
#endif	/* _GNU_SOURCE */
		switch (c) {
		case 'b':
			clean_cmd = NILFS_CLEAN_CMD_STOP;
			break;
		case 'c':
			if (optarg != 0)
				conffile = optarg;
			clean_cmd = NILFS_CLEAN_CMD_RELOAD;
			break;
		case 'h':
			nilfs_clean_usage();
			exit(EXIT_SUCCESS);
			break;
		case 'l':
			clean_cmd = NILFS_CLEAN_CMD_INFO;
			break;
		case 'm':
			if (nilfs_clean_parse_min_reclaimable(optarg) < 0)
				exit(EXIT_FAILURE);
			break;
		case 'p':
			ret = nilfs_parse_protection_period(
				optarg, &protection_period);
			if (!ret)
				break;

			if (errno == ERANGE) {
				myprintf(_("Error: too large period: %s\n"),
					 optarg);
			} else {
				myprintf(_("Error: invalid protection period: %s\n"),
					 optarg);
			}
			exit(EXIT_FAILURE);
		case 'q':
			clean_cmd = NILFS_CLEAN_CMD_SHUTDOWN;
			break;
		case 'r':
			clean_cmd = NILFS_CLEAN_CMD_RESUME;
			break;
		case 's':
			clean_cmd = NILFS_CLEAN_CMD_SUSPEND;
			break;
		case 'S':
			if (nilfs_clean_parse_gcspeed(optarg) < 0)
				exit(EXIT_FAILURE);
			break;
		case 'v':
			verbose = 1;
			break;
		case 'V':
			show_version_only = 1;
			break;
		default:
			myprintf(_("Error: invalid option -- %c\n"), optopt);
			exit(EXIT_FAILURE);
		}
	}
}

int main(int argc, char *argv[])
{
	char *last;
	struct stat statbuf;
	char *device = NULL;
	int status;

	last = strrchr(argv[0], '/');
	progname = last ? last + 1 : argv[0];

	nilfs_cleaner_logger = nilfs_clean_logger;


	nilfs_clean_parse_options(argc, argv);
	if (show_version_only) {
		myprintf(_("%s version %s\n"), progname, PACKAGE_VERSION);
		status = EXIT_SUCCESS;
		goto out;
	}

	status = EXIT_FAILURE;
	if (optind < argc) {
		device = argv[optind++];

		if (stat(device, &statbuf) < 0) {
			myprintf(_("Error: cannot find %s: %s.\n"), device,
				 strerror(errno));
			goto out;
		} else if (!S_ISBLK(statbuf.st_mode)) {
			myprintf(_("Error: device must be a block device.\n"));
			goto out;
		}
	}
	if (optind < argc) {
		myprintf(_("Error: too many arguments.\n"));
		goto out;
	}

	status = nilfs_do_clean(device);
out:
	exit(status);
}
