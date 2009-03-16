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
#ifdef _GNU_SOURCE
#include <getopt.h>
const static struct option long_option[] = {
	{"reverse", no_argument, NULL, 'r'},
	{"snapshot", no_argument, NULL, 's'},
	{"index", required_argument, NULL, 'i'},
	{"lines", required_argument, NULL, 'n'},
	{"help", no_argument, NULL, 'h'},
	{NULL, 0, NULL, 0}
};
#define LSCP_USAGE	"Usage: %s [OPTION]... [DEVICE]\n"		\
			"  -r, --reverse\t\treverse order\n"		\
			"  -s, --snapshot\tlist only snapshots\n"	\
			"  -i, --index\t\tcp/ss index\n"		\
			"  -n, --lines\t\tlines\n"			\
			"  -h, --help\t\tdisplay this help and exit\n"
#else
#define LSCP_USAGE	"Usage: %s [-rsh] [-i cno] [-n lines] [device]\n"
#endif	/* _GNU_SOURCE */

#define LSCP_BUFSIZE	128
#define LSCP_NCPINFO	512


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
	       (unsigned long long)cpinfo->ci_cno,
	       timebuf,
	       nilfs_cpinfo_snapshot(cpinfo) ? "ss" : "cp",
	       nilfs_cpinfo_minor(cpinfo) ? "i" : "-",
	       (unsigned long long)cpinfo->ci_nblk_inc,
	       (unsigned long long)cpinfo->ci_inodes_count);
}

/*
static void lscp_print_cpstat(const struct nilfs_cpstat *cpstat, int mode)
{
	if (mode == NILFS_CHECKPOINT)
		printf("total %llu/%llu\n", cpstat->cs_nsss, cpstat->cs_ncps);
	else
		printf("total %llu\n", cpstat->cs_nsss);
}
*/

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
	size_t rest;
	ssize_t n;
	int i, flag = 0;

	rest = param_lines && param_lines < cpstat->cs_ncps ? param_lines :
		cpstat->cs_ncps;
	sidx = param_index ? param_index : NILFS_CNO_MIN;
	if (!cpstat->cs_ncps)
		goto out;

	while (rest > 0) {
		if (sidx >= cpstat->cs_cno)
			break;
		n = lscp_get_cpinfo(nilfs, sidx, NILFS_CHECKPOINT, rest);
		if (n < 0)
			return 1;
		if (!n)
			break;

		if (!flag) {
			lscp_print_header();
			flag = 1;
		}
		for (i = 0; i < n; i++, rest--)
			lscp_print_cpinfo(&cpinfos[i]);

		sidx = cpinfos[n - 1].ci_cno + 1;
	}

 out:
	if (!flag)
		lscp_print_header();
	return 0;
}

static int lscp_backward_cpinfo(struct nilfs *nilfs,
				struct nilfs_cpstat *cpstat)
{
	nilfs_cno_t sidx, eidx;
	size_t rest;
	ssize_t n;
	int i, flag = 0;

	if (!cpstat->cs_ncps)
		goto out;
	rest = param_lines && param_lines < cpstat->cs_ncps ? param_lines :
		cpstat->cs_ncps;
	eidx = param_index ? param_index + 1 : cpstat->cs_cno;
	sidx = cpstat->cs_ncps < LSCP_NCPINFO || eidx < LSCP_NCPINFO ? 
		NILFS_CNO_MIN : eidx - LSCP_NCPINFO + 1;

	while (rest > 0) {
		if (sidx >= cpstat->cs_cno)
			goto next_block;
		n = lscp_get_cpinfo(nilfs, sidx, NILFS_CHECKPOINT,
				    LSCP_NCPINFO);
		if (n < 0)
			return 1;
		if (!n)
			break;

		if (!flag) {
			lscp_print_header();
			flag = 1;
		}
		for (i = 0; i < n && rest > 0; i++) {
			if (cpinfos[n - i - 1].ci_cno >= eidx)
				continue;
			lscp_print_cpinfo(&cpinfos[n - i - 1]);
			rest--;
		}

 next_block:
		if (sidx <= NILFS_CNO_MIN)
			break;

		eidx = sidx;
		if (sidx < LSCP_NCPINFO)
			sidx = sidx > rest ? sidx - rest : NILFS_CNO_MIN;
		else if (rest < LSCP_NCPINFO)
			sidx -= rest;
		else
			sidx -= LSCP_NCPINFO;
	}

 out:
	if (!flag)
		lscp_print_header();
	return 0;
}

static int lscp_search_snapshot(struct nilfs *nilfs,
				struct nilfs_cpstat *cpstat, nilfs_cno_t *sidx)
{
	nilfs_cno_t cno;
	ssize_t n, i;

	if (!cpstat->cs_nsss)
		return 0;

	for (cno = *sidx ; cno < cpstat->cs_cno; cno += LSCP_NCPINFO) {
		n = lscp_get_cpinfo(nilfs, cno, NILFS_CHECKPOINT,
				    LSCP_NCPINFO);
		if (n < 0)
			return -1;
		else if (!n)
			return 0;
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
	size_t rest;
	ssize_t n;
	int i, found, flag = 0;

	rest = param_lines && param_lines < cpstat->cs_nsss ? param_lines :
		cpstat->cs_nsss;
	sidx = param_index ? param_index : 0;

	if (!cpstat->cs_nsss)
		goto out;
	if (sidx >= cpstat->cs_cno)
		goto out;
	n = nilfs_get_cpinfo(nilfs, sidx, NILFS_CHECKPOINT, cpinfos, 1);
	if (n < 0) {
		return 1;
	} else if (n == 0) {
		goto out;
	} else if (!nilfs_cpinfo_snapshot(&cpinfos[0])) {
		found = lscp_search_snapshot(nilfs, cpstat, &sidx);
		if (found < 0)
			return 1;
		else if (!found)
			goto out;
	} else {
		sidx = cpinfos[0].ci_cno;
	}

	while (rest > 0) {
		if (sidx >= cpstat->cs_cno)
			break;
		n = lscp_get_cpinfo(nilfs, sidx, NILFS_SNAPSHOT, rest);
		if (n < 0)
			return 1;
		if (!n)
			break;

		if (!flag) {
			lscp_print_header();
			flag = 1;
		}
		for (i = 0; i < n; i++) {
			if (!nilfs_cpinfo_snapshot(&cpinfos[n - i - 1]))
				continue;
			lscp_print_cpinfo(&cpinfos[i]);
			rest--;
		}

		sidx = cpinfos[n - 1].ci_next;
		if (!sidx)
			break;
	}

 out:
	if (!flag)
		lscp_print_header();
	return 0;
}

static int lscp_backward_ssinfo(struct nilfs *nilfs,
				struct nilfs_cpstat *cpstat)
{
	nilfs_cno_t sidx, eidx;
	size_t rest;
	ssize_t n;
	int i, flag = 0;

	if (!cpstat->cs_nsss)
		goto out;

	rest = param_lines && param_lines < cpstat->cs_nsss ? param_lines :
		cpstat->cs_nsss;
	eidx = param_index ? param_index + 1 : cpstat->cs_cno;
	sidx = cpstat->cs_nsss < LSCP_NCPINFO || eidx < LSCP_NCPINFO ? 0 :
		eidx - LSCP_NCPINFO + 1;

	while (rest > 0) {
		if (sidx >= cpstat->cs_cno)
			goto next_block;
		if (!sidx)
			n = lscp_get_cpinfo(nilfs, sidx, NILFS_SNAPSHOT,
					    LSCP_NCPINFO);
		else
			n = lscp_get_cpinfo(nilfs, sidx, NILFS_CHECKPOINT,
					    LSCP_NCPINFO);
		if (n < 0)
			return 1;
		if (!n)
			break;

		if (!flag) {
			lscp_print_header();
			flag = 1;
		}
		for (i = 0; i < n && rest > 0; i++) {
			if (cpinfos[n - i - 1].ci_cno >= eidx)
				continue;
			if (!nilfs_cpinfo_snapshot(&cpinfos[n - i - 1]))
				continue;
			lscp_print_cpinfo(&cpinfos[n - i - 1]);
			eidx = cpinfos[n - i - 1].ci_cno;
			rest--;
		}

 next_block:
		if (sidx <= NILFS_CNO_MIN)
			break;

		if (sidx <= LSCP_NCPINFO)
			sidx = 0;
		else
			sidx -= LSCP_NCPINFO;
	}

 out:
	if (!flag)
		lscp_print_header();
	return 0;
}

int main(int argc, char *argv[])
{
	struct nilfs *nilfs;
	struct nilfs_cpstat cpstat;
	char *dev, *progname;
	int c, mode, rvs, status;
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
	while ((c = getopt_long(argc, argv, "rsn:c:h",
				long_option, &option_index)) >= 0) {
#else
	while ((c = getopt(argc, argv, "rsi:n:h")) >= 0) {
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
	} else if (optind == argc - 1) {
		dev = argv[optind++];
	} else {
		dev = NULL;
	}

	nilfs = nilfs_open(dev, NILFS_OPEN_RDONLY);
	if (nilfs == NULL) {
		fprintf(stderr, "%s: %s: cannot open NILFS\n",
			progname, dev);
		exit(1);
	}

	status = 0;

	if (nilfs_get_cpstat(nilfs, &cpstat) < 0)
		goto out;

	if (mode == NILFS_CHECKPOINT) {
		if (!rvs)
			status = lscp_forward_cpinfo(nilfs, &cpstat);
		else
			status = lscp_backward_cpinfo(nilfs, &cpstat);
	} else {
		if (!rvs)
			status = lscp_forward_ssinfo(nilfs, &cpstat);
		else
			status = lscp_backward_ssinfo(nilfs, &cpstat);
	}

	if (status)
		fprintf(stderr, "%s: %s\n", progname, strerror(errno));
 out:
	nilfs_close(nilfs);
	exit(status);
}

/* Local Variables:		*/
/* eval: (c-set-style "linux")	*/
/* End:				*/
