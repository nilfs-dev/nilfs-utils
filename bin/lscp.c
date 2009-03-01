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
	{"help", no_argument, NULL, 'h'},
	{NULL, 0, NULL, 0}
};
#define LSCP_USAGE	"Usage: %s [OPTION]... [DEVICE]\n"		\
			"  -r, --reverse\t\treverse order\n"		\
			"  -s, --snapshot\tlist only snapshots\n"	\
			"  -h, --help\t\tdisplay this help and exit\n"
#else
#define LSCP_USAGE	"Usage: %s [-rsh] [device]\n"
#endif	/* _GNU_SOURCE */

#define LSCP_BUFSIZE	128
#define LSCP_NCPINFO	512


static ssize_t lscp_get_cpinfo(struct nilfs *nilfs, int mode,
			       struct nilfs_cpstat *cpstat,
			       struct nilfs_cpinfo **cpinfop)
{
	struct nilfs_cpinfo *cpinfo;
	nilfs_cno_t cno;
	size_t ncp, count;
	ssize_t n, total = 0;
	int i;

	ncp = (mode == NILFS_CHECKPOINT) ? cpstat->cs_ncps : cpstat->cs_nsss;
	if (ncp == 0) {
		cpinfo = NULL;
		goto out;
	}
	if ((cpinfo = (struct nilfs_cpinfo *)malloc(
		     sizeof(struct nilfs_cpinfo) * ncp)) == NULL)
		return -1;
	cno = (mode == NILFS_CHECKPOINT) ? NILFS_CNO_MIN : 0;
	for (i = 0; i < ncp; i += n) {
		count = (ncp - i < LSCP_NCPINFO) ? ncp - i : LSCP_NCPINFO;
		if ((n = nilfs_get_cpinfo(nilfs, cno, mode,
					  cpinfo + i, count)) < 0) {
			free(cpinfo);
			return -1;
		}
		if (n == 0)
			break;
		total += n;
		if (mode == NILFS_CHECKPOINT) {
			cno = cpinfo[i + n - 1].ci_cno + 1;
		} else {
			cno = cpinfo[i + n - 1].ci_next;
			if (cno == 0)
				break;
		}
	}

 out:
	*cpinfop = cpinfo;
	return total;
}

static void lscp_reverse_cpinfo(struct nilfs_cpinfo *cpinfo, size_t n)
{
	struct nilfs_cpinfo tmp;
	int i;

	for (i = 0; i < n / 2; i++) {
		tmp = cpinfo[i];
		cpinfo[i] = cpinfo[n - 1 - i];
		cpinfo[n - 1 - i] = tmp;
	}
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

static void lscp_print_cpinfo(const struct nilfs_cpinfo *cpinfo, size_t n)
{
	struct tm tm;
	time_t t;
	char timebuf[LSCP_BUFSIZE];
	int i;

	printf("                 CNO        DATE     TIME  MODE  FLG   NBLKINC       ICNT\n");

	for (i = 0; i < n; i++) {
		t = (time_t)(cpinfo[i].ci_create);
		localtime_r(&t, &tm);
		strftime(timebuf, LSCP_BUFSIZE, "%F %T", &tm);
				 
		printf("%20llu  %s  %s     %s %10llu %10llu\n",
		       (unsigned long long)cpinfo[i].ci_cno,
		       timebuf,
		       nilfs_cpinfo_snapshot(&cpinfo[i]) ? "ss" : "cp",
		       "-",
		       (unsigned long long)cpinfo[i].ci_nblk_inc,
		       (unsigned long long)cpinfo[i].ci_inodes_count);
	}
}

int main(int argc, char *argv[])
{
	struct nilfs *nilfs;
	struct nilfs_cpstat cpstat;
	struct nilfs_cpinfo *cpinfo;
	char *dev, *progname;
	ssize_t n;
	int c, mode, rvs, status;
#ifdef _GNU_SOURCE
	int option_index;
#endif	/* _GNU_SOURCE */

	mode = NILFS_CHECKPOINT;
	rvs = 0;
	opterr = 0;	/* prevent error message */
	if ((progname = strrchr(argv[0], '/')) == NULL)
		progname = argv[0];
	else
		progname++;


#ifdef _GNU_SOURCE
	while ((c = getopt_long(argc, argv, "rsh",
				long_option, &option_index)) >= 0) {
#else
	while ((c = getopt(argc, argv, "rsh")) >= 0) {
#endif	/* _GNU_SOURCE */

		switch (c) {
		case 'r':
			rvs = 1;
			break;
		case 's':
			mode = NILFS_SNAPSHOT;
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

	if ((nilfs = nilfs_open(dev, NILFS_OPEN_RDONLY)) == NULL) {
		fprintf(stderr, "%s: %s: cannot open NILFS\n",
			progname, dev);
		exit(1);
	}

	status = 0;

	if (nilfs_get_cpstat(nilfs, &cpstat) < 0)
		goto out;
	if ((n = lscp_get_cpinfo(nilfs, mode, &cpstat, &cpinfo)) < 0) {
		fprintf(stderr, "%s: %s\n", progname, strerror(errno));
		status = 1;
		goto out;
	}

	if (rvs)
		lscp_reverse_cpinfo(cpinfo, n);

	//lscp_print_cpstat(&cpstat, mode);
	lscp_print_cpinfo(cpinfo, n);

	if (cpinfo != NULL)
		free(cpinfo);

 out:
	nilfs_close(nilfs);
	exit(status);
}

/* Local Variables:		*/
/* eval: (c-set-style "linux")	*/
/* End:				*/
