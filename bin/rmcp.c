/*
 * rmcp.c - NILFS command of removing checkpoints.
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
 * Written by Koji Sato,
 *            Ryusuke Konishi <konishi.ryusuke@lab.ntt.co.jp>.
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

#include <stdarg.h>

#if HAVE_LIMITS_H
#include <limits.h>
#endif	/* HAVE_LIMITS_H */

#include <errno.h>
#include "nilfs.h"
#include "parser.h"


#ifdef _GNU_SOURCE
#include <getopt.h>
static const struct option long_options[] = {
	{"force", no_argument, NULL, 'f'},
	{"interactive", no_argument, NULL, 'i'},
	{"help", no_argument, NULL, 'h'},
	{"version", no_argument, NULL, 'V'},
	{NULL, 0, NULL, 0}
};
#define RMCP_USAGE							\
	"Usage: %s [OPTION]... [DEVICE] CNO...\n"			\
	"  -f, --force\t\tignore snapshots or nonexistent checkpoints\n" \
	"  -i, --interactive\tprompt before any removal\n"		\
	"  -h, --help\t\tdisplay this help and exit\n"			\
	"  -V, --version\t\tdisplay version and exit\n"
#else	/* !_GNU_SOURCE */
#define RMCP_USAGE	"Usage: %s [-fihV] [device] cno...\n"
#endif	/* _GNU_SOURCE */

#define CHCP_PROMPT							\
	"\nTo delete snapshot(s), change them into checkpoints with\n"	\
	"chcp command before removal.\n"

#define RMCP_BASE 10

static char *progname;

static int force;
static int interactive;

static int rmcp_confirm(const char *arg)
{
	char ans[MAX_INPUT];

	fprintf(stderr, "%s: remove checkpoint %s? ", progname, arg);
	if (fgets(ans, MAX_INPUT, stdin) != NULL)
		return ans[0] == 'y' || ans[0] == 'Y';
	return 0;
}

static int rmcp_remove_range(struct nilfs *nilfs,
			     nilfs_cno_t start, nilfs_cno_t end,
			     size_t *ndeleted, size_t *nsnapshots)
{
	nilfs_cno_t cno;
	int nocp = 0, nd = 0, nss = 0;
	int ret = 0;

	for (cno = start; cno <= end; cno++) {
		if (nilfs_delete_checkpoint(nilfs, cno) == 0) {
			nd++;
			continue;
		}
		if (errno == EBUSY) {
			nss++;
			if (!force)
				warnx("%llu: cannot remove snapshot",
				      (unsigned long long)cno);

		} else if (errno == ENOENT) {
			nocp++;
		} else {
			warn("%llu: cannot remove checkpoint",
			     (unsigned long long)cno);
			ret = -1;
			goto out;
		}
	}
	if (!force && (nss > 0 || (nocp > 0 && nd == 0)))
		ret = 1;
 out:
	*ndeleted = nd;
	*nsnapshots = nss;
	return ret;
}

int main(int argc, char *argv[])
{
	char *dev;
	char *last;
	struct nilfs *nilfs;
	struct nilfs_cpstat cpstat;
	nilfs_cno_t start, end, oldest;
	size_t nsnapshots, nss, ndel;
	int c, status, ret;
#ifdef _GNU_SOURCE
	int option_index;
#endif	/* _GNU_SOURCE */

	opterr = 0;
	last = strrchr(argv[0], '/');
	progname = last ? last + 1 : argv[0];

#ifdef _GNU_SOURCE
	while ((c = getopt_long(argc, argv, "fihV",
				long_options, &option_index)) >= 0) {
#else	/* !_GNU_SOURCE */
	while ((c = getopt(argc, argv, "fihV")) >= 0) {
#endif	/* _GNU_SOURCE */

		switch (c) {
		case 'f':
			force = 1;
			interactive = 0;
			break;
		case 'i':
			force = 0;
			interactive = 1;
			break;
		case 'h':
			fprintf(stderr, RMCP_USAGE, progname);
			exit(EXIT_SUCCESS);
		case 'V':
			printf("%s (%s %s)\n", progname, PACKAGE,
			       PACKAGE_VERSION);
			exit(EXIT_SUCCESS);
		default:
			errx(EXIT_FAILURE, "invalid option -- %c", optopt);
		}
	}

	if (optind > argc - 1) {
		errx(EXIT_FAILURE, "too few arguments");
	} else if (optind == argc - 1) {
		dev = NULL;
	} else {
		if (nilfs_parse_cno_range(argv[optind], &start, &end,
					  RMCP_BASE) < 0)
			dev = argv[optind++];
		else
			dev = NULL;
	}

	nilfs = nilfs_open(dev, NULL, NILFS_OPEN_RDWR);
	if (nilfs == NULL)
		err(EXIT_FAILURE, "cannot open NILFS on %s", dev ? : "device");

	if (nilfs_get_cpstat(nilfs, &cpstat) < 0) {
		warn("%s: cannot get checkpoint status", dev);
		status = EXIT_FAILURE;
		goto out_close_nilfs;
	}

	status = EXIT_SUCCESS;
	nsnapshots = 0;
	for ( ; optind < argc; optind++) {
		if (nilfs_parse_cno_range(argv[optind], &start, &end,
					  RMCP_BASE) < 0 ||
		    start > end || start < NILFS_CNO_MIN) {
			warnx("invalid checkpoint range: %s", argv[optind]);
			status = EXIT_FAILURE;
			continue;
		}
		if (interactive && !rmcp_confirm(argv[optind]))
			continue;

		if (start != end) {
			oldest = nilfs_get_oldest_cno(nilfs);
			if (start < oldest)
				start = oldest;
			if (end >= cpstat.cs_cno)
				end = cpstat.cs_cno - 2;

			if (start > end) {
				if (force)
					continue;
				status = EXIT_FAILURE;
				goto warn_on_invalid_checkpoint;
			}
		}

		ret = rmcp_remove_range(nilfs, start, end, &ndel, &nss);
		nsnapshots += nss;
		if (!ret)
			continue;

		status = EXIT_FAILURE;
		if (ret < 0) {
			warnx("remaining checkpoints were not removed.");
			break;
		}

		if (force || ndel != 0 || end - start + 1 - nss == 0)
			continue;

 warn_on_invalid_checkpoint:
		if (start == end)
			warnx("invalid checkpoint: %s", argv[optind]);
		else
			warnx("no valid checkpoints found in %s",
			      argv[optind]);
	}
	if (!force && nsnapshots)
		fprintf(stderr, CHCP_PROMPT);

out_close_nilfs:
	nilfs_close(nilfs);
	exit(status);
}
