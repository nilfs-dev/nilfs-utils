/*
 * lscp.c - NILFS command of listing checkpoint information.
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
#endif	/* HAVE_STDLIB_H */

#if HAVE_UNISTD_H
#include <unistd.h>
#endif	/* HAVE_UNISTD_H */

#if HAVE_ERR_H
#include <err.h>
#endif	/* HAVE_ERR_H */

#if HAVE_STRING_H
#include <string.h>
#endif	/* HAVE_STRING_H */

#if HAVE_TIME_H
#include <time.h>
#endif	/* HAVE_TIME_H */

#include "nilfs.h"
#include "util.h"

#undef CONFIG_PRINT_CPSTAT

#ifdef _GNU_SOURCE
#include <getopt.h>
static const struct option long_option[] = {
	{"all", no_argument, NULL, 'a'},
	{"show-block-count", no_argument, NULL, 'b'},
	{"show-increment", no_argument, NULL, 'g'},
	{"reverse", no_argument, NULL, 'r'},
	{"snapshot", no_argument, NULL, 's'},
	{"index", required_argument, NULL, 'i'},
	{"lines", required_argument, NULL, 'n'},
	{"help", no_argument, NULL, 'h'},
	{"version", no_argument, NULL, 'V'},
	{NULL, 0, NULL, 0}
};
#define LSCP_USAGE	"Usage: %s [OPTION]... [DEVICE]\n"		\
			"  -a, --all\t\tshow all checkpoints\n"		\
			"  -b, --show-block-count\t\tshow block count\n"\
			"  -g, --show-increment\t\tshow increment count\n"\
			"  -r, --reverse\t\treverse order\n"		\
			"  -s, --snapshot\tlist only snapshots\n"	\
			"  -i, --index\t\tcp/ss index\n"		\
			"  -n, --lines\t\tlines\n"			\
			"  -h, --help\t\tdisplay this help and exit\n"	\
			"  -V, --version\t\tdisplay version and exit\n"
#else
#define LSCP_USAGE	"Usage: %s [-bgrshV] [-i cno] [-n lines] [device]\n"
#endif	/* _GNU_SOURCE */

#define LSCP_BUFSIZE	128
#define LSCP_NCPINFO	512
#define LSCP_MINDELTA	64	/* Minimum delta for reverse direction */

enum lscp_state {
	LSCP_INIT_ST,		/* Initial state */
	LSCP_NORMAL_ST,		/* Normal state */
	LSCP_ACCEL_ST,		/* Accelerate state */
	LSCP_DECEL_ST,		/* Decelerate state */
};

static __u64 param_index;
static __u64 param_lines;
static struct nilfs_cpinfo cpinfos[LSCP_NCPINFO];
static int show_block_count = 1;
static int show_all;

static void lscp_print_header(void)
{
	printf("                 CNO        DATE     TIME  MODE  FLG     %s       ICNT\n",
	       show_block_count ? " BLKCNT" : "NBLKINC");
}

static void lscp_print_cpinfo(struct nilfs_cpinfo *cpinfo)
{
	struct tm tm;
	time_t t;
	char timebuf[LSCP_BUFSIZE];

	t = (time_t)cpinfo->ci_create;
	localtime_r(&t, &tm);
	strftime(timebuf, LSCP_BUFSIZE, "%F %T", &tm);

	printf("%20llu  %s   %s    %s %12llu %10llu\n",
	       (unsigned long long)cpinfo->ci_cno, timebuf,
	       nilfs_cpinfo_snapshot(cpinfo) ? "ss" : "cp",
	       nilfs_cpinfo_minor(cpinfo) ? "i" : "-",
	       (unsigned long long)(show_block_count ?
				    cpinfo->ci_blocks_count :
				    cpinfo->ci_nblk_inc),
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
	size_t req_count = min_t(size_t, count, LSCP_NCPINFO);

	return nilfs_get_cpinfo(nilfs, cno, mode, cpinfos, req_count);
}

static int lscp_forward_cpinfo(struct nilfs *nilfs,
			       struct nilfs_cpstat *cpstat)
{
	struct nilfs_cpinfo *cpi;
	nilfs_cno_t sidx;
	__u64 rest;
	ssize_t n;

	rest = param_lines && param_lines < cpstat->cs_ncps ? param_lines :
		cpstat->cs_ncps;
	sidx = param_index ? param_index : NILFS_CNO_MIN;

	while (rest > 0 && sidx < cpstat->cs_cno) {
		n = lscp_get_cpinfo(nilfs, sidx, NILFS_CHECKPOINT, rest);
		if (n < 0)
			return n;
		if (!n)
			break;

		for (cpi = cpinfos; cpi < cpinfos + n; cpi++) {
			if (show_all || nilfs_cpinfo_snapshot(cpi) ||
			    !nilfs_cpinfo_minor(cpi)) {
				lscp_print_cpinfo(cpi);
				rest--;
			}
		}
		sidx = cpinfos[n - 1].ci_cno + 1;
	}
	return 0;
}

static int lscp_backward_cpinfo(struct nilfs *nilfs,
				struct nilfs_cpstat *cpstat)
{
	struct nilfs_cpinfo *cpi;
	nilfs_cno_t sidx; /* start index (inclusive) */
	nilfs_cno_t eidx; /* end index (exclusive) */
	nilfs_cno_t prev_head = 0;
	__u64 rest, delta, v;
	int state = LSCP_INIT_ST;
	ssize_t n;

	rest = param_lines && param_lines < cpstat->cs_ncps ? param_lines :
		cpstat->cs_ncps;
	if (!rest)
		goto out;
	eidx = param_index && param_index < cpstat->cs_cno ? param_index + 1 :
		cpstat->cs_cno;

recalc_delta:
	delta = min_t(__u64, LSCP_NCPINFO, max_t(__u64, rest, LSCP_MINDELTA));
	v = delta;

	while (eidx > NILFS_CNO_MIN) {
		if (eidx < NILFS_CNO_MIN + v || state == LSCP_INIT_ST)
			sidx = NILFS_CNO_MIN;
		else
			sidx = eidx - v;

		n = lscp_get_cpinfo(nilfs, sidx, NILFS_CHECKPOINT,
				    state == LSCP_NORMAL_ST ? eidx - sidx : 1);
		if (n < 0)
			return n;
		if (!n)
			break;

		if (state == LSCP_INIT_ST) {
			/*
			 * This state makes succesive
			 * nilfs_get_cpinfo() calls much faster by
			 * setting minimum checkpoint number in nilfs
			 * struct.
			 */
			if (cpinfos[0].ci_cno >= eidx)
				goto out; /* out of range */
			state = LSCP_NORMAL_ST;
			continue;
		} else if (cpinfos[0].ci_cno == prev_head) {
			/* No younger checkpoint was found */

			if (sidx == NILFS_CNO_MIN)
				break;

			/* go further back */
			switch (state) {
			case LSCP_NORMAL_ST:
				state = LSCP_ACCEL_ST;
				/* fall through */
			case LSCP_ACCEL_ST:
				if ((v << 1) > v)
					v <<= 1;
				break;
			case LSCP_DECEL_ST:
				state = LSCP_NORMAL_ST;
				v = delta;
				break;
			}
			eidx = sidx;
			continue;
		} else {
			switch (state) {
			case LSCP_ACCEL_ST:
			case LSCP_DECEL_ST:
				if (cpinfos[n - 1].ci_cno + 1 < prev_head) {
					/* search again more slowly */
					v >>= 1;
					if (v <= delta) {
						state = LSCP_NORMAL_ST;
						v = delta;
					} else {
						state = LSCP_DECEL_ST;
					}
					continue;
				}
				break;
			default:
				break;
			}
		}

		state = LSCP_NORMAL_ST;
		cpi = &cpinfos[n - 1];
		do {
			if (cpi->ci_cno < eidx &&
			    (show_all || nilfs_cpinfo_snapshot(cpi) ||
			     !nilfs_cpinfo_minor(cpi))) {
				lscp_print_cpinfo(cpi);
				rest--;
				if (rest == 0)
					goto out;
			}
		} while (--cpi >= cpinfos);

		prev_head = cpinfos[0].ci_cno;
		eidx = sidx;
		goto recalc_delta;
	}
out:
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
	while ((c = getopt_long(argc, argv, "abgrsi:n:hV",
				long_option, &option_index)) >= 0) {
#else
	while ((c = getopt(argc, argv, "abgrsi:n:hV")) >= 0) {
#endif	/* _GNU_SOURCE */

		switch (c) {
		case 'a':
			show_all = 1;
			break;
		case 'b':
			show_block_count = 1;
			break;
		case 'g':
			show_block_count = 0;
			break;
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
			errx(EXIT_FAILURE, "invalid option -- %c", optopt);
		}
	}

	if (optind < argc - 1)
		errx(EXIT_FAILURE, "too many arguments");
	else if (optind == argc - 1)
		dev = argv[optind++];
	else
		dev = NULL;

	nilfs = nilfs_open(dev, NULL, NILFS_OPEN_RDONLY);
	if (nilfs == NULL)
		err(EXIT_FAILURE, "cannot open NILFS on %s", dev ? : "device");

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
		warn(NULL);
		status = EXIT_FAILURE;
	}
	nilfs_close(nilfs);
	exit(status);
}
