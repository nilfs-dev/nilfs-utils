/*
 * chcp.c - NILFS command of changing checkpoint mode.
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
 * Written by Koji Sato.
 *
 * Maintained by Ryusuke Konishi <konishi.ryusuke@lab.ntt.co.jp> from 2008.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif	/* HAVE_CONFIG_H */

#include <stdio.h>

#if HAVE_STDLIB_H
#include <stdlib.h>
#endif

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

#include <errno.h>
#include <signal.h>
#include "nilfs.h"
#include "parser.h"


#define CHCP_MODE_CP	"cp"
#define CHCP_MODE_SS	"ss"
#define CHCP_BASE	10

#ifdef _GNU_SOURCE
#include <getopt.h>
static const struct option long_option[] = {
	{"help", no_argument, NULL, 'h'},
	{"version", no_argument, NULL, 'V'},
	{NULL, 0, NULL, 0}
};

#define CHCP_USAGE	\
	"Usage: %s [OPTION]... " CHCP_MODE_CP "|" CHCP_MODE_SS" [DEVICE] CNO...\n"	\
	"  -h, --help\t\tdisplay this help and exit\n"			\
	"  -V, --version\t\tdisplay version and exit\n"
#else	/* !_GNU_SOURCE */
#define CHCP_USAGE	\
	"Usage: %s [option]... " CHCP_MODE_CP "|" CHCP_MODE_SS " [device] cno...\n"
#endif	/* _GNU_SOURCE */


int main(int argc, char *argv[])
{
	struct nilfs *nilfs;
	nilfs_cno_t cno;
	char *dev, *modestr, *progname, *endptr, *last;
	int c, mode, status;
#ifdef _GNU_SOURCE
	int option_index;
#endif	/* _GNU_SOURCE */
	sigset_t sigset, oldset;

	opterr = 0;

	last = strrchr(argv[0], '/');
	progname = last ? last + 1 : argv[0];

#ifdef _GNU_SOURCE
	while ((c = getopt_long(argc, argv, "hV",
				long_option, &option_index)) >= 0) {
#else	/* !_GNU_SOURCE */
	while ((c = getopt(argc, argv, "hV")) >= 0) {
#endif	/* _GNU_SOURCE */

		switch (c) {
		case 'h':
			fprintf(stderr, CHCP_USAGE, progname);
			exit(EXIT_SUCCESS);
		case 'V':
			printf("%s (%s %s)\n", progname, PACKAGE,
			       PACKAGE_VERSION);
			exit(EXIT_SUCCESS);
		default:
			errx(EXIT_FAILURE, "invalid option -- %c", optopt);
		}
	}

	if (optind > argc - 2) {
		errx(EXIT_FAILURE, "too few arguments");
	} else if (optind == argc - 2) {
		modestr = argv[optind++];
		dev = NULL;
	} else {
		modestr = argv[optind++];
		nilfs_parse_cno(argv[optind], &endptr, CHCP_BASE);
		if (*endptr == '\0')
			dev = NULL;
		else
			dev = argv[optind++];
	}

	if (strcmp(modestr, CHCP_MODE_CP) == 0)
		mode = NILFS_CHECKPOINT;
	else if (strcmp(modestr, CHCP_MODE_SS) == 0)
		mode = NILFS_SNAPSHOT;
	else
		errx(EXIT_FAILURE, "%s: invalid checkpoint mode", modestr);

	nilfs = nilfs_open(dev, NULL, NILFS_OPEN_RDWR | NILFS_OPEN_GCLK);
	if (nilfs == NULL)
		err(EXIT_FAILURE, "cannot open NILFS on %s", dev ? : "device");

	status = EXIT_SUCCESS;

	sigemptyset(&sigset);
	sigaddset(&sigset, SIGINT);
	sigaddset(&sigset, SIGTERM);
	if (sigprocmask(SIG_BLOCK, &sigset, &oldset) < 0) {
		warn("cannot block signals");
		status = EXIT_FAILURE;
		goto out;
	}

	if (nilfs_lock_cleaner(nilfs) < 0) {
		warnx("cannot lock NILFS");
		status = EXIT_FAILURE;
		goto out_unblock_signal;
	}

	for (; optind < argc; optind++) {
		sigset_t waitset;

		if (sigpending(&waitset) < 0) {
			warn("cannot test signals");
			status = EXIT_FAILURE;
			break;
		}
		if (sigismember(&waitset, SIGINT) ||
		    sigismember(&waitset, SIGTERM)) {
			warnx("interrupted");
			status = EXIT_FAILURE;
			break;
		}

		cno = nilfs_parse_cno(argv[optind], &endptr, CHCP_BASE);
		if (cno >= NILFS_CNO_MAX || *endptr != '\0') {
			warnx("%s: invalid checkpoint number", argv[optind]);
			status = EXIT_FAILURE;
			continue;
		} else if (cno == ULONG_MAX && errno == ERANGE) {
			warn("%s", argv[optind]);
			status = EXIT_FAILURE;
			continue;
		}

		if (nilfs_change_cpmode(nilfs, cno, mode) < 0) {
			if (errno == ENOENT)
				warnx("%llu: no checkpoint",
				      (unsigned long long)cno);
			else
				warn(NULL);
			status = EXIT_FAILURE;
			continue;
		}
	}

	nilfs_unlock_cleaner(nilfs);

out_unblock_signal:
	sigprocmask(SIG_SETMASK, &oldset, NULL);
out:
	nilfs_close(nilfs);
	exit(status);
}
