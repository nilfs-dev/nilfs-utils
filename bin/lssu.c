/*
 * lssu.c - NILFS command of listing segment usage information.
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

#if HAVE_ERR_H
#include <err.h>
#endif	/* HAVE_ERR_H */

#if HAVE_STRING_H
#include <string.h>
#endif	/* HAVE_STRING_H */

#if HAVE_LIMITS_H
#include <limits.h>
#endif	/* HAVE_LIMITS_H */

#if HAVE_SYS_TIME_H
#include <sys/time.h>
#endif	/* HAVE_SYS_TIME */

#if HAVE_TIME_H
#include <time.h>
#endif	/* HAVE_TIME_H */

#include <errno.h>
#include "nilfs.h"
#include "util.h"
#include "nilfs_gc.h"
#include "cnormap.h"
#include "parser.h"

#ifdef _GNU_SOURCE
#include <getopt.h>
static const struct option long_option[] = {
	{"all",  no_argument, NULL, 'a'},
	{"index", required_argument, NULL, 'i'},
	{"latest-usage", no_argument, NULL, 'l' },
	{"lines", required_argument, NULL, 'n'},
	{"protection-period", required_argument, NULL, 'p'},
	{"help", no_argument, NULL, 'h'},
	{"version", no_argument, NULL, 'V'},
	{NULL, 0, NULL, 0}
};

#define LSSU_USAGE							\
	"Usage: %s [OPTION]... [DEVICE]\n"				\
	"  -a, --all\t\t\tdo not hide clean segments\n"			\
	"  -h, --help\t\t\tdisplay this help and exit\n"		\
	"  -i, --index\t\t\tskip index segments at start of inputs\n"	\
	"  -l, --latest-usage\t\tprint usage status of the moment\n"	\
	"  -n, --lines\t\t\tlist only lines input segments\n"		\
	"  -p, --protection-period\tspecify protection period\n"	\
	"  -V, --version\t\t\tdisplay version and exit\n"
#else	/* !_GNU_SOURCE */
#include <unistd.h>
#define LSSU_USAGE \
	"Usage: %s [-alhV] [-i index] [-n lines] [-p period] [device]\n"
#endif	/* _GNU_SOURCE */

#define LSSU_BUFSIZE	128
#define LSSU_NSEGS	512

enum lssu_mode {
	LSSU_MODE_NORMAL,
	LSSU_MODE_LATEST_USAGE,
};

struct lssu_format {
	char *header;
	char *body;
};

static const struct lssu_format lssu_format[] = {
	{
		"              SEGNUM        DATE     TIME STAT     NBLOCKS",
		"%20llu  %s  %c%c%c  %10u\n"
	},
	{
		"           SEGNUM        DATE     TIME STAT     NBLOCKS       NLIVEBLOCKS",
		"%17llu  %s %c%c%c%c  %10u %10u (%3u%%)\n"
	}
};

static int all;
static int latest;
static int disp_mode;		/* display mode */
static nilfs_cno_t protcno;
static __s64 prottime, now;
static __u64 param_index;
static __u64 param_lines;

static size_t blocks_per_segment;
static struct nilfs_suinfo suinfos[LSSU_NSEGS];

static void lssu_print_header(void)
{
	puts(lssu_format[disp_mode].header);
}

static ssize_t lssu_get_latest_usage(struct nilfs *nilfs,
				     __u64 segnum, __u64 protseq,
				     nilfs_cno_t protcno)
{
	struct nilfs_reclaim_stat stat;
	struct nilfs_reclaim_params params = {
		.flags = NILFS_RECLAIM_PARAM_PROTSEQ,
		.protseq = protseq
	};
	__u64 segnums[1];
	int ret;

	if (protcno != NILFS_CNO_MAX) {
		params.flags |= NILFS_RECLAIM_PARAM_PROTCNO;
		params.protcno = protcno;
	}

	memset(&stat, 0, sizeof(stat));
	segnums[0] = segnum;

	ret = nilfs_assess_segment(nilfs, segnums, 1, &params, &stat);
	if (ret < 0)
		return -1;

	if (stat.protected_segs > 0)
		return -2;

	return stat.live_blks;
}

static ssize_t lssu_print_suinfo(struct nilfs *nilfs, __u64 segnum,
				 ssize_t nsi, __u64 protseq)
{
	struct tm tm;
	time_t t;
	char timebuf[LSSU_BUFSIZE];
	ssize_t i, n = 0, ret;
	int ratio;
	int protected;
	size_t nliveblks;

	for (i = 0; i < nsi; i++, segnum++) {
		if (!all && nilfs_suinfo_clean(&suinfos[i]))
			continue;

		t = (time_t)suinfos[i].sui_lastmod;
		if (t != 0) {
			localtime_r(&t, &tm);
			strftime(timebuf, LSSU_BUFSIZE, "%F %T", &tm);
		} else
			snprintf(timebuf, LSSU_BUFSIZE,
				 "---------- --:--:--");

		switch (disp_mode) {
		case LSSU_MODE_NORMAL:
			printf(lssu_format[disp_mode].body,
			       (unsigned long long)segnum,
			       timebuf,
			       nilfs_suinfo_active(&suinfos[i]) ? 'a' : '-',
			       nilfs_suinfo_dirty(&suinfos[i]) ? 'd' : '-',
			       nilfs_suinfo_error(&suinfos[i]) ? 'e' : '-',
			       suinfos[i].sui_nblocks);
			break;
		case LSSU_MODE_LATEST_USAGE:
			nliveblks = 0;
			ratio = 0;
			protected = (t >= prottime && t <= now);

			if (!nilfs_suinfo_dirty(&suinfos[i]) ||
			    nilfs_suinfo_error(&suinfos[i]))
				goto skip_scan;

			ret = lssu_get_latest_usage(nilfs, segnum, protseq,
						    protcno);
			if (ret >= 0) {
				nliveblks = ret;
				ratio = (ret * 100 + 99) / blocks_per_segment;
			} else if (ret == -2) {
				nliveblks = suinfos[i].sui_nblocks;
				ratio = 100;
				protected = 1;
			} else {
				warn("failed to get usage");
				return -1;
			}

skip_scan:
			printf(lssu_format[disp_mode].body,
			       (unsigned long long)segnum,
			       timebuf,
			       nilfs_suinfo_active(&suinfos[i]) ? 'a' : '-',
			       nilfs_suinfo_dirty(&suinfos[i]) ? 'd' : '-',
			       nilfs_suinfo_error(&suinfos[i]) ? 'e' : '-',
			       protected ? 'p' : '-',
			       suinfos[i].sui_nblocks, nliveblks, ratio);
			break;
		}
		n++;
	}
	return n;
}

static int lssu_list_suinfo(struct nilfs *nilfs)
{
	struct nilfs_sustat sustat;
	__u64 segnum, rest, count;
	ssize_t nsi, n;

	lssu_print_header();
	if (nilfs_get_sustat(nilfs, &sustat) < 0)
		return EXIT_FAILURE;
	segnum = param_index;
	rest = param_lines && param_lines < sustat.ss_nsegs ? param_lines :
		sustat.ss_nsegs;

	for ( ; rest > 0 && segnum < sustat.ss_nsegs; rest -= n) {
		count = min_t(__u64, rest, LSSU_NSEGS);
		nsi = nilfs_get_suinfo(nilfs, segnum, suinfos, count);
		if (nsi < 0)
			return EXIT_FAILURE;

		n = lssu_print_suinfo(nilfs, segnum, nsi, sustat.ss_prot_seq);
		if (n < 0)
			return EXIT_FAILURE;
		segnum += nsi;
	}

	return EXIT_SUCCESS;
}

static int lssu_get_protcno(struct nilfs *nilfs,
			    unsigned long protection_period,
			    __s64 *prottimep, nilfs_cno_t *protcnop)
{
	struct nilfs_cnormap *cnormap;
	int ret;

	if (protection_period == ULONG_MAX) {
		*protcnop = NILFS_CNO_MAX;
		*prottimep = now;
		return 0;
	}

	*prottimep = now - protection_period;

	cnormap = nilfs_cnormap_create(nilfs);
	if (!cnormap) {
		warn("failed to create checkpoint number reverse mapper");
		return -1;
	}

	ret = nilfs_cnormap_track_back(cnormap, protection_period, protcnop);
	if (ret < 0)
		warn("failed to get checkpoint number from protection period (%lu)",
		     protection_period);

	nilfs_cnormap_destroy(cnormap);
	return ret;
}

int main(int argc, char *argv[])
{
	struct nilfs *nilfs;
	char *dev, *progname;
	int c, status;
	int open_flags;
	unsigned long protection_period = ULONG_MAX;
	int ret;
#ifdef _GNU_SOURCE
	int option_index;
#endif	/* _GNU_SOURCE */

	opterr = 0;
	progname = strrchr(argv[0], '/');
	if (progname == NULL)
		progname = argv[0];
	else
		progname++;

#ifdef _GNU_SOURCE
	while ((c = getopt_long(argc, argv, "ai:ln:hp:V",
				long_option, &option_index)) >= 0) {
#else	/* !_GNU_SOURCE */
	while ((c = getopt(argc, argv, "ai:ln:hp:V")) >= 0) {
#endif	/* _GNU_SOURCE */

		switch (c) {
		case 'a':
			all = 1;
			break;
		case 'i':
			param_index = (__u64)atoll(optarg);
			break;
		case 'l':
			latest = 1;
			break;
		case 'n':
			param_lines = (__u64)atoll(optarg);
			break;
		case 'h':
			fprintf(stderr, LSSU_USAGE, progname);
			exit(EXIT_SUCCESS);
		case 'p':
			ret = nilfs_parse_protection_period(
				optarg, &protection_period);
			if (!ret)
				break;

			if (errno == ERANGE)
				errx(EXIT_FAILURE, "too large period: %s",
				     optarg);

			errx(EXIT_FAILURE, "invalid protection period: %s",
			     optarg);
		case 'V':
			printf("%s (%s %s)\n", progname, PACKAGE,
			       PACKAGE_VERSION);
			exit(EXIT_SUCCESS);
		default:
			errx(EXIT_FAILURE, "invalid option -- %c", optopt);
		}
	}

	if (optind > argc - 1)
		dev = NULL;
	else if (optind == argc - 1)
		dev = argv[optind++];
	else
		errx(EXIT_FAILURE, "too many arguments");

	open_flags = NILFS_OPEN_RDONLY;
	if (latest)
		open_flags |= NILFS_OPEN_RAW | NILFS_OPEN_GCLK;

	nilfs = nilfs_open(dev, NULL, open_flags);
	if (nilfs == NULL)
		err(EXIT_FAILURE, "cannot open NILFS on %s", dev ? : "device");

	if (latest) {
		struct timeval tv;

		ret = gettimeofday(&tv, NULL);
		if (ret < 0) {
			warn("cannot get current time");
			status = EXIT_FAILURE;
			goto out_close_nilfs;
		}
		now = tv.tv_sec;

		blocks_per_segment = nilfs_get_blocks_per_segment(nilfs);
		disp_mode = LSSU_MODE_LATEST_USAGE;

		ret = lssu_get_protcno(nilfs, protection_period, &prottime,
				       &protcno);
		if (ret < 0) {
			status = EXIT_FAILURE;
			goto out_close_nilfs;
		}
	}

	status = lssu_list_suinfo(nilfs);

out_close_nilfs:
	nilfs_close(nilfs);
	exit(status);
}
