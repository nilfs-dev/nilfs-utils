/*
 * lscp.c - NILFS command of listing checkpoint information.
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

#if HAVE_UNISTD_H
#include <unistd.h>
#endif	/* HAVE_UNISTD_H */

#if HAVE_STRING_H
#include <string.h>
#endif	/* HAVE_STRING_H */

#if HAVE_TIME_H
#include <time.h>
#endif	/* HAVE_TIME_H */

#include <errno.h>
#include "nilfs.h"

#undef CONFIG_PRINT_CPSTAT

#ifdef _GNU_SOURCE
#include <getopt.h>
const static struct option long_option[] = {
	{"reverse", no_argument, NULL, 'r'},
	{"snapshot", no_argument, NULL, 's'},
	{"index", required_argument, NULL, 'i'},
	{"lines", required_argument, NULL, 'n'},
	{"help", no_argument, NULL, 'h'},
	{"version", no_argument, NULL, 'V'},
	{NULL, 0, NULL, 0}
};
#define LSCP_USAGE	"Usage: %s [OPTION]... [DEVICE]\n"		\
			"  -r, --reverse\t\treverse order\n"		\
			"  -s, --snapshot\tlist only snapshots\n"	\
			"  -i, --index\t\tcp/ss index\n"		\
			"  -n, --lines\t\tlines\n"			\
			"  -h, --help\t\tdisplay this help and exit\n"	\
			"  -V, --version\t\tdisplay version and exit\n"
#else
#define LSCP_USAGE	"Usage: %s [-rshV] [-i cno] [-n lines] [device]\n"
#endif	/* _GNU_SOURCE */

#define LSCP_BUFSIZE	128
#define LSCP_NCPINFO	512
#define LSCP_MINDELTA	64	/* Minimum delta for reverse direction */


static __u64 param_index;
static __u64 param_lines;
static struct nilfs_cpinfo cpinfos[LSCP_NCPINFO];

static void lscp_print_header(void)
{
	printf("                 CNO        DATE     TIME  MODE  FLG   NBLKINC"
	       "       ICNT\n");
}

static void lscp_print_cpinfo(struct nilfs_cpinfo *cpinfo)
{
	struct tm tm;
	time_t t;
	char timebuf[LSCP_BUFSIZE];

	t = (time_t)cpinfo->ci_create;
	localtime_r(&t, &tm);
	strftime(timebuf, LSCP_BUFSIZE, "%F %T", &tm);

	printf("%20llu  %s   %s    %s %10llu %10llu\n",
	       (unsigned long long)cpinfo->ci_cno, timebuf,
	       nilfs_cpinfo_snapshot(cpinfo) ? "ss" : "cp",
	       nilfs_cpinfo_minor(cpinfo) ? "i" : "-",
	       (unsigned long long)cpinfo->ci_nblk_inc,
	       (unsigned long long)cpinfo->ci_inodes_count);
}

#ifdef CONFIG_PRINT_CPSTAT
static void lscp_print_cpstat(const struct nilfs_cpstat *cpstat, int mode)
{
	if (mode == NILFS_CHECKPOINT)
		printf("total %llu/%llu\n",
		       (unsigned long long)cpstat->cs_nsss,
		       (unsigned long long)cpstat->cs_ncps);
	else
		printf("total %llu\n",
		       (unsigned long long)cpstat->cs_nsss);
}
#endif

static ssize_t lscp_get_cpinfo(struct nilfs *nilfs, nilfs_cno_t cno, int mode,
			       size_t count)
{
	size_t req_count;

	req_count = (count < LSCP_NCPINFO) ? count : LSCP_NCPINFO;
	return nilfs_get_cpinfo(nilfs, cno, mode, cpinfos, req_count);
}

static int lscp_forward_cpinfo(struct nilfs *nilfs,
			       struct nilfs_cpstat *cpstat)
{
	nilfs_cno_t sidx;
	__u64 rest;
	ssize_t n;
	int i;

	rest = param_lines && param_lines < cpstat->cs_ncps ? param_lines :
		cpstat->cs_ncps;
	sidx = param_index ? param_index : NILFS_CNO_MIN;

	while (rest > 0 && sidx < cpstat->cs_cno) {
		n = lscp_get_cpinfo(nilfs, sidx, NILFS_CHECKPOINT, rest);
		if (n < 0)
			return n;
		if (!n)
			break;

		for (i = 0; i < n; i++, rest--)
			lscp_print_cpinfo(&cpinfos[i]);

		sidx = cpinfos[n - 1].ci_cno + 1;
	}
	return 0;
}

static int lscp_backward_cpinfo(struct nilfs *nilfs,
				struct nilfs_cpstat *cpstat)
{
	nilfs_cno_t sidx; /* start index (inclusive) */
	nilfs_cno_t eidx; /* end index (exclusive) */
	__u64 rest, delta;
	ssize_t n;
	int i;

	rest = param_lines && param_lines < cpstat->cs_ncps ? param_lines :
		cpstat->cs_ncps;
	eidx = param_index && param_index < cpstat->cs_cno ? param_index + 1 :
		cpstat->cs_cno;

	for ( ; rest > 0 && eidx > NILFS_CNO_MIN; eidx = sidx) {
		delta = (rest > LSCP_NCPINFO ? LSCP_NCPINFO :
			 (rest < LSCP_MINDELTA ? LSCP_MINDELTA : rest));
		sidx = (eidx >= NILFS_CNO_MIN + delta) ? eidx - delta :
			NILFS_CNO_MIN;

		n = lscp_get_cpinfo(nilfs, sidx, NILFS_CHECKPOINT, eidx - sidx);
		if (n < 0)
			return n;
		if (!n)
			break;

		for (i = 0 ; i < n && rest > 0; i++) {
			if (cpinfos[n - i - 1].ci_cno >= eidx)
				continue;
			lscp_print_cpinfo(&cpinfos[n - i - 1]);
			rest--;
		}
	}
	return 0;
}

static int lscp_search_snapshot(struct nilfs *nilfs,
				struct nilfs_cpstat *cpstat, nilfs_cno_t *sidx)
{
	nilfs_cno_t cno;
	__u64 nreq;
	ssize_t n, i;

	for (cno = *sidx, nreq = 1; cno < cpstat->cs_cno;
	     cno += n, nreq = cpstat->cs_cno - cno) {
		n = lscp_get_cpinfo(nilfs, cno, NILFS_CHECKPOINT, nreq);
		if (n < 0)
			return n;
		if (!n)
			break;

		for (i = 0; i < n; i++) {
			if (nilfs_cpinfo_snapshot(&cpinfos[i])) {
				*sidx = cpinfos[i].ci_cno;
				return 1;
			}
		}
	}
	return 0;
}

static int lscp_forward_ssinfo(struct nilfs *nilfs,
			       struct nilfs_cpstat *cpstat)
{
	nilfs_cno_t sidx;
	__u64 rest;
	ssize_t n;
	int i, ret;

	rest = param_lines && param_lines < cpstat->cs_nsss ? param_lines :
		cpstat->cs_nsss;
	sidx = param_index;

	if (!rest || sidx >= cpstat->cs_cno)
		return 0;

	if (sidx > 0) {
		/* find first snapshot */
		ret = lscp_search_snapshot(nilfs, cpstat, &sidx);
		if (ret < 0)
			return ret;
		else if (!ret)
			return 0;  /* no snapshot found */
	}

	while (rest > 0 && sidx < cpstat->cs_cno) {
		n = lscp_get_cpinfo(nilfs, sidx, NILFS_SNAPSHOT, rest);
		if (n < 0)
			return n;
		if (!n)
			break;

		for (i = 0; i < n; i++)
			lscp_print_cpinfo(&cpinfos[i]);

		rest -= n;
		sidx = cpinfos[n - 1].ci_next;
		if (!sidx)
			break;
	}
	return 0;
}

static int lscp_backward_ssinfo(struct nilfs *nilfs,
				struct nilfs_cpstat *cpstat)
{
	nilfs_cno_t sidx; /* start index (inclusive) */
	nilfs_cno_t eidx; /* end index (exclusive) */
	__u64 rest;
	__u64 rns; /* remaining number of snapshots (always rest <= rns) */
	ssize_t n;
	int i;

	rns = cpstat->cs_nsss;
	rest = param_lines && param_lines < rns ? param_lines : rns;
	eidx = param_index && param_index < cpstat->cs_cno ? param_index + 1 :
		cpstat->cs_cno;

	for ( ; rest > 0 && eidx > NILFS_CNO_MIN ; eidx = sidx) {
		if (rns <= LSCP_NCPINFO || eidx <= NILFS_CNO_MIN + LSCP_NCPINFO)
			goto remainder;

		sidx = (eidx >= NILFS_CNO_MIN + LSCP_NCPINFO) ?
			eidx - LSCP_NCPINFO : NILFS_CNO_MIN;
		n = lscp_get_cpinfo(nilfs, sidx, NILFS_CHECKPOINT, eidx - sidx);
		if (n < 0)
			return n;
		if (!n)
			break;

		for (i = 0; i < n && rest > 0; i++) {
			if (cpinfos[n - i - 1].ci_cno >= eidx)
				continue;
			if (!nilfs_cpinfo_snapshot(&cpinfos[n - i - 1]))
				continue;
			lscp_print_cpinfo(&cpinfos[n - i - 1]);
			eidx = cpinfos[n - i - 1].ci_cno;
			rest--;
			rns--;
		}
	}
	return 0;

 remainder:
	/* remaining snapshots */
	n = lscp_get_cpinfo(nilfs, 0, NILFS_SNAPSHOT, rns);
	if (n < 0)
		return n;
	for (i = 0; i < n && rest > 0; i++) {
		if (cpinfos[n - i - 1].ci_cno >= eidx)
			continue;
		lscp_print_cpinfo(&cpinfos[n - i - 1]);
		rest--;
	}
	return 0;
}

int main(int argc, char *argv[])
{
	struct nilfs *nilfs;
	struct nilfs_cpstat cpstat;
	char *dev, *progname;
	int c, mode, rvs, status, ret;
#ifdef _GNU_SOURCE
	int option_index;
#endif	/* _GNU_SOURCE */

	mode = NILFS_CHECKPOINT;
	rvs = 0;
	opterr = 0;	/* prevent error message */
	progname = strrchr(argv[0], '/');
	if (progname == NULL)
		progname = argv[0];
	else
		progname++;


#ifdef _GNU_SOURCE
	while ((c = getopt_long(argc, argv, "rsi:n:hV",
				long_option, &option_index)) >= 0) {
#else
	while ((c = getopt(argc, argv, "rsi:n:hV")) >= 0) {
#endif	/* _GNU_SOURCE */

		switch (c) {
		case 'r':
			rvs = 1;
			break;
		case 's':
			mode = NILFS_SNAPSHOT;
			break;
		case 'i':
			param_index = (__u64)atoll(optarg);
			break;
		case 'n':
			param_lines = (__u64)atoll(optarg);
			break;
		case 'h':
			fprintf(stderr, LSCP_USAGE, progname);
			exit(EXIT_SUCCESS);
		case 'V':
			printf("%s (%s %s)\n", progname, PACKAGE,
			       PACKAGE_VERSION);
			exit(EXIT_SUCCESS);
		default:
			fprintf(stderr, "%s: invalid option -- %c\n",
				progname, optopt);
			exit(EXIT_FAILURE);
		}
	}

	if (optind < argc - 1) {
		fprintf(stderr, "%s: too many arguments\n", progname);
		exit(1);
	} else if (optind == argc - 1) {
		dev = argv[optind++];
	} else {
		dev = NULL;
	}

	nilfs = nilfs_open(dev, NULL, NILFS_OPEN_RDONLY);
	if (nilfs == NULL) {
		fprintf(stderr, "%s: %s: cannot open NILFS\n",
			progname, dev);
		exit(EXIT_FAILURE);
	}

	status = EXIT_SUCCESS;

	ret = nilfs_get_cpstat(nilfs, &cpstat);
	if (ret < 0)
		goto out;

#ifdef CONFIG_PRINT_CPSTAT
	lscp_print_cpstat(&cpstat, mode);
#endif
	lscp_print_header();

	if (mode == NILFS_CHECKPOINT) {
		if (!rvs)
			ret = lscp_forward_cpinfo(nilfs, &cpstat);
		else
			ret = lscp_backward_cpinfo(nilfs, &cpstat);
	} else {
		if (!rvs)
			ret = lscp_forward_ssinfo(nilfs, &cpstat);
		else
			ret = lscp_backward_ssinfo(nilfs, &cpstat);
	}

 out:
	if (ret < 0) {
		status = EXIT_FAILURE;
		fprintf(stderr, "%s: %s\n", progname, strerror(errno));
	}
	nilfs_close(nilfs);
	exit(status);
}
