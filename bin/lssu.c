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
	{"help", no_argument, NULL, 'h'},
	{NULL, 0, NULL, 0}
};

#define LSSU_USAGE	"Usage: %s [OPTION]... [DEVICE]\n"		\
			"  -a, --all\t\tdo not hide clean segments\n"	\
			"  -h, --help\t\tdisplay this help and exit\n"
#else	/* !_GNU_SOURCE */
#include <unistd.h>
#define LSSU_USAGE	"Usage: %s [-ah] [device]\n"
#endif	/* _GNU_SOURCE */

#define LSSU_BUFSIZE	128
#define LSSU_NSEGS	512

static ssize_t lssu_get_suinfo(struct nilfs *nilfs, struct nilfs_suinfo **sip)
{
	struct nilfs_suinfo *si;
	struct nilfs_sustat sustat;
	nilfs_segnum_t segnum;
	size_t count;
	ssize_t n, total = 0;

	if (nilfs_get_sustat(nilfs, &sustat) < 0)
		return -1;
	if ((si = (struct nilfs_suinfo *)malloc(
		     sizeof(struct nilfs_suinfo) * sustat.ss_nsegs)) == NULL)
		return -1;
	for (segnum = 0; segnum < sustat.ss_nsegs; segnum += n) {
		count = (sustat.ss_nsegs - segnum < LSSU_NSEGS) ?
			sustat.ss_nsegs - segnum : LSSU_NSEGS;
		if ((n = nilfs_get_suinfo(nilfs, segnum,
					  &si[segnum], count)) < 0) {
			free(si);
			return -1;
		}
		if (n == 0)
			break; /* safety valve against broken filesystems */
		total += n;
	}

	*sip = si;
	return total;
}

static void lssu_print_suinfo(const struct nilfs_suinfo *si, size_t nsi,
			      int all)
{
	struct tm tm;
	time_t t;
	char timebuf[LSSU_BUFSIZE];
	nilfs_segnum_t segnum;

	printf("              SEGNUM        DATE     TIME STAT     NBLOCKS\n");

	for (segnum = 0; segnum < nsi; segnum++) {
		if (all || !nilfs_suinfo_clean(&si[segnum])) {
			if ((t = (time_t)(si[segnum].sui_lastmod)) != 0) {
				localtime_r(&t, &tm);
				strftime(timebuf, LSSU_BUFSIZE, "%F %T", &tm);
			} else
				snprintf(timebuf, LSSU_BUFSIZE,
					 "---------- --:--:--");

			printf("%20llu  %s  %c%c%c  %10u\n",
			       (unsigned long long)segnum,
			       timebuf,
			       nilfs_suinfo_active(&si[segnum]) ? 'a' : '-',
			       nilfs_suinfo_dirty(&si[segnum]) ? 'd' : '-',
			       nilfs_suinfo_error(&si[segnum]) ? 'e' : '-',
			       si[segnum].sui_nblocks);
		}
	}
}

int main(int argc, char *argv[])
{
	struct nilfs *nilfs;
	struct nilfs_suinfo *si;
	char *dev, *progname;
	ssize_t n;
	int c, all, status;
#ifdef _GNU_SOURCE
	int option_index;
#endif	/* _GNU_SOURCE */

	all = 0;
	opterr = 0;
	if ((progname = strrchr(argv[0], '/')) == NULL)
		progname = argv[0];
	else
		progname++;

#ifdef _GNU_SOURCE
	while ((c = getopt_long(argc, argv, "ah",
				long_option, &option_index)) >= 0) {
#else	/* !_GNU_SOURCE */
	while ((c = getopt(argc, argv, "ah")) >= 0) {
#endif	/* _GNU_SOURCE */

		switch (c) {
		case 'a':
			all = 1;
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

	if ((nilfs = nilfs_open(dev, NILFS_OPEN_RDONLY)) == NULL) {
		fprintf(stderr, "%s: %s: cannot open NILFS\n",
			progname, dev);
		exit(1);
	}

	status = 1;

	if ((n = lssu_get_suinfo(nilfs, &si)) < 0) {
		fprintf(stderr, "%s: %s\n", progname, strerror(errno));
		goto out;
	}

	lssu_print_suinfo(si, n, all);

	free(si);
	status = 0;

 out:
	nilfs_close(nilfs);
	exit(status);
}

/* Local Variables:		*/
/* eval: (c-set-style "linux")	*/
/* End:				*/
