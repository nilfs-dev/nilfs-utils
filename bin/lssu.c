/*
 * lssu.c - NILFS command of listing segment usage information.
 *
 * Copyright (C) 2007 Nippon Telegraph and Telephone Corporation.
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

#if HAVE_STRING_H
#include <string.h>
#endif	/* HAVE_STRING_H */

#if HAVE_TIME_H
#include <time.h>
#endif	/* HAVE_TIME_H */

#include <errno.h>
#include "nilfs.h"

#ifdef _GNU_SOURCE
#include <getopt.h>
const static struct option long_option[] = {
	{"all",  no_argument, NULL, 'a'},
	{"index",required_argument, NULL, 'i'},
	{"lines",required_argument, NULL, 'n'},
	{"help", no_argument, NULL, 'h'},
	{NULL, 0, NULL, 0}
};

#define LSSU_USAGE	"Usage: %s [OPTION]... [DEVICE]\n"		\
			"  -a, --all\t\tdo not hide clean segments\n"	\
			"  -i, --index\tstart index\n"			\
			"  -n, --lines\toutput lines\n"			\
			"  -h, --help\t\tdisplay this help and exit\n"
#else	/* !_GNU_SOURCE */
#include <unistd.h>
#define LSSU_USAGE	"Usage: %s [-ah] [-i index] [-n lines] [device]\n"
#endif	/* _GNU_SOURCE */

#define LSSU_BUFSIZE	128
#define LSSU_NSEGS	512

static __u64 param_index;
static __u64 param_lines;
static struct nilfs_suinfo suinfos[LSSU_NSEGS];


static void lssu_print_header(void)
{
	printf("              SEGNUM        DATE     TIME STAT     NBLOCKS\n");
}

static ssize_t lssu_print_suinfo(__u64 segnum, ssize_t nsi, int all)
{
	struct tm tm;
	time_t t;
	char timebuf[LSSU_BUFSIZE];
	ssize_t i, n = 0;

	for (i = 0; i < nsi; i++, segnum++) {
		if (all || !nilfs_suinfo_clean(&suinfos[i])) {
			t = (time_t)suinfos[i].sui_lastmod;
			if (t != 0) {
				localtime_r(&t, &tm);
				strftime(timebuf, LSSU_BUFSIZE, "%F %T", &tm);
			} else
				snprintf(timebuf, LSSU_BUFSIZE,
					 "---------- --:--:--");

			printf("%20llu  %s  %c%c%c  %10u\n",
			       (unsigned long long)segnum,
			       timebuf,
			       nilfs_suinfo_active(&suinfos[i]) ? 'a' : '-',
			       nilfs_suinfo_dirty(&suinfos[i]) ? 'd' : '-',
			       nilfs_suinfo_error(&suinfos[i]) ? 'e' : '-',
			       suinfos[i].sui_nblocks);
			n++;
		}
	}
	return n;
}

static int lssu_list_suinfo(struct nilfs *nilfs, int all)
{
	struct nilfs_sustat sustat;
	__u64 segnum, rest, count;
	ssize_t nsi, n;

	lssu_print_header();
	if (nilfs_get_sustat(nilfs, &sustat) < 0)
		return 1;
	segnum = param_index;
	rest = param_lines && param_lines < sustat.ss_nsegs ? param_lines :
		sustat.ss_nsegs;

	for ( ; rest > 0 && segnum < sustat.ss_nsegs; rest -= n) {
		count = (rest < LSSU_NSEGS) ? rest : LSSU_NSEGS;
		nsi = nilfs_get_suinfo(nilfs, segnum, suinfos, count);
		if (nsi < 0)
			return 1;

		n = lssu_print_suinfo(segnum, nsi, all);
		segnum += nsi;
	}

	return 0;
}

int main(int argc, char *argv[])
{
	struct nilfs *nilfs;
	char *dev, *progname;
	int c, all, status;
#ifdef _GNU_SOURCE
	int option_index;
#endif	/* _GNU_SOURCE */

	all = 0;
	opterr = 0;
	progname = strrchr(argv[0], '/');
	if (progname == NULL)
		progname = argv[0];
	else
		progname++;

#ifdef _GNU_SOURCE
	while ((c = getopt_long(argc, argv, "ai:n:h",
				long_option, &option_index)) >= 0) {
#else	/* !_GNU_SOURCE */
	while ((c = getopt(argc, argv, "ai:n:h")) >= 0) {
#endif	/* _GNU_SOURCE */

		switch (c) {
		case 'a':
			all = 1;
			break;
		case 'i':
			param_index = (__u64)atoll(optarg);
			break;
		case 'n':
			param_lines = (__u64)atoll(optarg);
			break;
		case 'h':
			fprintf(stderr, LSSU_USAGE, progname);
			exit(0);
		default:
			fprintf(stderr, "%s: invalid option -- %c\n",
				progname, optopt);
			exit(1);
		}
	}

	if (optind > argc - 1) {
		dev = NULL;
	} else if (optind == argc - 1) {
		dev = argv[optind++];
	} else {
		fprintf(stderr, "%s: too many arguments\n", progname);
		exit(1);
	}

	nilfs = nilfs_open(dev, NULL, NILFS_OPEN_RDONLY);
	if (nilfs == NULL) {
		fprintf(stderr, "%s: %s: cannot open NILFS\n",
			progname, dev);
		exit(1);
	}

	status = lssu_list_suinfo(nilfs, all);

	nilfs_close(nilfs);
	exit(status);
}
