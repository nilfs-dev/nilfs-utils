/*
 * mkcp.c - NILFS command of making checkpoints.
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
 * You should have received a copy of the GNU General Public License
 * along with NILFS; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Written by Koji Sato <koji@osrg.net>.
 *
 * Maintained by Ryusuke Konishi <konishi.ryusuke@lab.ntt.co.jp> from 2008.
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

#include <errno.h>
#include <signal.h>
#include "nilfs.h"


#ifdef _GNU_SOURCE
#include <getopt.h>

static const struct option long_option[] = {
	{"snapshot", no_argument, NULL, 's'},
	{"print", no_argument, NULL, 'p'},
	{"help", no_argument, NULL, 'h'},
	{"version", no_argument, NULL, 'V'},
	{NULL, 0, NULL, 0}
};

#define MKCP_USAGE	"Usage: %s [OPTION] [DEVICE]\n"			\
			"  -s, --snapshot\tcreate a snapshot\n"		\
			"  -p, --print\tprint the created CP number\n"	\
			"  -h, --help\t\tdisplay this help and exit\n"	\
			"  -V, --version\t\tdisplay version and exit\n"
#else	/* !_GNU_SOURCE */
#define MKCP_USAGE	"Usage: %s [-sphV] [device]\n"
#endif	/* _GNU_SOURCE */


int main(int argc, char *argv[])
{
	struct nilfs *nilfs;
	nilfs_cno_t cno;
	char *dev, *progname, *last;
	int ss, print, c, status;
#ifdef _GNU_SOURCE
	int option_index;
#endif	/* _GNU_SOURCE */
	sigset_t sigset, oldset;

	ss = 0;
	print = 0;
	opterr = 0;
	last = strrchr(argv[0], '/');
	progname = last ? last + 1 : argv[0];

#ifdef _GNU_SOURCE
	while ((c = getopt_long(argc, argv, "sphV",
				long_option, &option_index)) >= 0) {
#else	/* !_GNU_SOURCE */
	while ((c = getopt(argc, argv, "sphV")) >= 0) {
#endif	/* _GNU_SOURCE */

		switch (c) {
		case 's':
			ss = 1;
			break;
		case 'p':
			print = 1;
			break;
		case 'h':
			fprintf(stderr, MKCP_USAGE, progname);
			exit(0);
		case 'V':
			printf("%s (%s %s)\n", progname, PACKAGE,
			       PACKAGE_VERSION);
			exit(0);
		default:
			fprintf(stderr, "%s: invalid option -- %c\n",
				progname, optopt);
			exit(1);
		}
	}

	if (optind < argc - 1) {
		fprintf(stderr, "%s: too many arguments\n", progname);
		exit(1);
	} else if (optind > argc - 1)
		dev = NULL;
	else
		dev = argv[optind];

	nilfs = nilfs_open(dev, NULL, NILFS_OPEN_RDWR | NILFS_OPEN_GCLK);
	if (nilfs == NULL) {
		fprintf(stderr, "%s: cannot open NILFS on %s: %m\n",
			progname, dev ? : "device");
		exit(1);
	}

	status = 0;
	if (nilfs_sync(nilfs, &cno) < 0) {
		fprintf(stderr, "%s: %s\n", progname, strerror(errno));
		status = 1;
		goto out;
	}

	sigemptyset(&sigset);
	sigaddset(&sigset, SIGINT);
	sigaddset(&sigset, SIGTERM);
	if (sigprocmask(SIG_BLOCK, &sigset, &oldset) < 0) {
		fprintf(stderr, "%s: cannot block signals: %s\n",
			progname, strerror(errno));
		status = 1;
		goto out;
	}

	if (ss) {
		if (nilfs_lock_cleaner(nilfs) < 0) {
			fprintf(stderr, "%s: %s\n", progname, strerror(errno));
			status = 1;
			goto out_unblock_signal;
		}
		if (nilfs_change_cpmode(nilfs, cno, NILFS_SNAPSHOT) < 0) {
			fprintf(stderr, "%s: %s\n", progname, strerror(errno));
			status = 1;
		}
		if (nilfs_unlock_cleaner(nilfs) < 0) {
			fprintf(stderr, "%s: %s\n", progname, strerror(errno));
			status = 1;
		}
	}

out_unblock_signal:
	sigprocmask(SIG_SETMASK, &oldset, NULL);
out:
	nilfs_close(nilfs);
	if (!status && print)
		printf("%ld\n", (long)cno);
	exit(status);
}
