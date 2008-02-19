/*
 * rmcp.c - NILFS command of removing checkpoints.
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
 * rmcp.c,v 1.6 2007-10-19 01:19:51 koji Exp
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

#include <stdarg.h>

#if HAVE_LIMITS_H
#include <limits.h>
#endif	/* HAVE_LIMITS_H */

#include <errno.h>
#include "nilfs.h"


#ifdef _GNU_SOURCE
#include <getopt.h>
const static struct option long_options[] = {
	{"force", no_argument, NULL, 'f'},
	{"interactive", no_argument, NULL, 'i'},
	{"help", no_argument, NULL, 'h'},
	{NULL, 0, NULL, 0}
};
#define RMCP_USAGE							\
	"Usage: %s [OPTION]... [DEVICE] CNO...\n"			\
	"  -f, --force\tignore snapshots or nonexistent checkpoints"	\
	"  -i, --interactive\t prompt before any removal\n"		\
	"  -h, --help\t\tdisplay this help and exit\n"
#else	/* !_GNU_SOURCE */
#define RMCP_USAGE	"Usage: %s [-fih] [device] cno...\n"
#endif	/* _GNU_SOURCE */

#define RMCP_BASE 10


static int rmcp_confirm(char *progname, nilfs_cno_t cno)
{
	char ans[MAX_INPUT];

	fprintf(stderr, "%s: remove checkpoint %llu? ",
		progname, (unsigned long long)cno);
	if (fgets(ans, MAX_INPUT, stdin) != NULL)
		return (ans[0] == 'y') || (ans[0] == 'Y');
	return 0;
}

int main(int argc, char *argv[])
{
	struct nilfs *nilfs;
	nilfs_cno_t cno;
	char *dev, *progname, *endptr;
	int c, force, interactive, status;
#ifdef _GNU_SOURCE
	int option_index;
#endif	/* _GNU_SOURCE */

	force = 0;
	interactive = 0;
	opterr = 0;
	if ((progname = strrchr(argv[0], '/')) == NULL)
		progname = argv[0];
	else
		progname++;

#ifdef _GNU_SOURCE
	while ((c = getopt_long(argc, argv, "fih",
				long_options, &option_index)) >= 0) {
#else	/* !_GNU_SOURCE */
	while ((c = getopt(argc, argv, "fih")) >= 0) {
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
			exit(0);
		default:
			fprintf(stderr, "%s: invalid option -- %c\n",
				progname, optopt);
			exit(1);
		}
	}

	if (optind > argc - 1) {
		fprintf(stderr, "%s: too few arguments\n", progname);
		exit(1);
	} else if (optind == argc - 1)
		dev = NULL;
	else {
		strtoul(argv[optind], &endptr, RMCP_BASE);
		if (*endptr == '\0')
			dev = NULL;
		else
			dev = argv[optind++];
	}

	if ((nilfs = nilfs_open(dev, NILFS_OPEN_RDWR)) == NULL) {
		fprintf(stderr, "%s: %s: cannot open NILFS\n", progname, dev);
		exit(1);
	}

	status = 0;
	for (; optind < argc; optind++) {
		cno = strtoul(argv[optind], &endptr, RMCP_BASE);
		errno = 0;
		if (*endptr != '\0') {
			fprintf(stderr, "%s: %s: invalid checkpoint number\n",
				progname, argv[optind]);
			status = 1;
			continue;
		} else if ((cno == ULONG_MAX) && (errno == ERANGE)) {
			fprintf(stderr, "%s: %s: %s\n",
				progname, argv[optind], strerror(errno));
			status = 1;
			continue;
		}

		if (interactive && !rmcp_confirm(progname, cno))
			continue;

		if (nilfs_delete_checkpoint(nilfs, cno) < 0) {
			if (errno == EPERM) {
				fprintf(stderr,	"%s: %llu: operation not permitted\n",
					progname, (unsigned long long)cno);
				status = 1;
			} else if (errno == ENOENT) {
				if (!force) {
					fprintf(stderr, "%s: %llu: no checkpoint\n",
						progname,
						(unsigned long long)cno);
					status = 1;
				}
			} else {
				fprintf(stderr, "%s: %s\n",
					progname, strerror(errno));
				status = 1;
				break;
			}
		}
	}

	nilfs_close(nilfs);
	exit(status);
}

/* Local Variables:		*/
/* eval: (c-set-style "linux")	*/
/* End:				*/
