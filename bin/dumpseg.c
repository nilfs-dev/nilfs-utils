/*
 * dumpseg.c - NILFS command of printing segment information.
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

#ifdef _GNU_SOURCE
#include <getopt.h>
static const struct option long_option[] = {
	{"help", no_argument, NULL, 'h'},
	{"version", no_argument, NULL, 'V'},
	{NULL, 0, NULL, 0}
};

#define DUMPSEG_USAGE	\
	"Usage: %s [OPTION]... [DEVICE] SEGNUM...\n"	\
	"  -h, --help\t\tdisplay this help and exit\n"	\
	"  -V, --version\t\tdisplay version and exit\n"
#else	/* !_GNU_SOURCE */
#define DUMPSEG_USAGE	"Usage: %s [-h] [-V] [device] segnum...\n"
#endif	/* _GNU_SOURCE */


#define DUMPSEG_BASE	10
#define DUMPSEG_BUFSIZE	128

static void dumpseg_print_block(struct nilfs_block *blk)
{
	__le64 *binfo = blk->b_binfo;

	if (nilfs_block_is_data(blk)) {
		printf("        vblocknr = %llu, blkoff = %llu, blocknr = %llu\n",
		       (unsigned long long)le64_to_cpu(binfo[0]),
		       (unsigned long long)le64_to_cpu(binfo[1]),
		       (unsigned long long)blk->b_blocknr);
	} else {
		printf("        vblocknr = %llu, blocknr = %llu\n",
		       (unsigned long long)le64_to_cpu(binfo[0]),
		       (unsigned long long)blk->b_blocknr);
	}
}

static void dumpseg_print_block_super(struct nilfs_block *blk)
{
	if (nilfs_block_is_data(blk)) {
		__le64 *binfo = blk->b_binfo;

		printf("        blkoff = %llu, blocknr = %llu\n",
		       (unsigned long long)le64_to_cpu(binfo[0]),
		       (unsigned long long)blk->b_blocknr);
	} else {
		struct nilfs_binfo_dat *bid = blk->b_binfo;

		printf("        blkoff = %llu, level = %d, blocknr = %llu\n",
		       (unsigned long long)le64_to_cpu(bid->bi_blkoff),
		       bid->bi_level,
		       (unsigned long long)blk->b_blocknr);
	}
}

static void dumpseg_print_file(struct nilfs_file *file)
{
	struct nilfs_block blk;

	printf("    finfo\n");
	printf("      ino = %llu, cno = %llu, nblocks = %d, ndatblk = %d\n",
	       (unsigned long long)le64_to_cpu(file->f_finfo->fi_ino),
	       (unsigned long long)le64_to_cpu(file->f_finfo->fi_cno),
	       le32_to_cpu(file->f_finfo->fi_nblocks),
	       le32_to_cpu(file->f_finfo->fi_ndatablk));
	if (!nilfs_file_is_super(file)) {
		nilfs_block_for_each(&blk, file) {
			dumpseg_print_block(&blk);
		}
	} else {
		nilfs_block_for_each(&blk, file) {
			dumpseg_print_block_super(&blk);
		}
	}
}

static void dumpseg_print_psegment(struct nilfs_psegment *pseg)
{
	struct nilfs_file file;
	struct tm tm;
	char timebuf[DUMPSEG_BUFSIZE];
	time_t t;

	printf("  partial segment: blocknr = %llu, nblocks = %llu\n",
	       (unsigned long long)pseg->p_blocknr,
	       (unsigned long long)le32_to_cpu(pseg->p_segsum->ss_nblocks));

	t = (time_t)le64_to_cpu(pseg->p_segsum->ss_create);
	localtime_r(&t, &tm);
	strftime(timebuf, DUMPSEG_BUFSIZE, "%F %T", &tm);
	printf("    creation time = %s\n", timebuf);
	printf("    nfinfo = %d\n", le32_to_cpu(pseg->p_segsum->ss_nfinfo));
	nilfs_file_for_each(&file, pseg) {
		dumpseg_print_file(&file);
	}
}

static void dumpseg_print_segment(struct nilfs *nilfs,
				  __u64 segnum,
				  void *seg,
				  size_t segsize)
{
	struct nilfs_psegment pseg;
	size_t blksize;
	__u64 next;

	printf("segment: segnum = %llu\n", (unsigned long long)segnum);
	blksize = nilfs_get_block_size(nilfs);
	nilfs_psegment_init(&pseg, segnum, seg, segsize / blksize, nilfs);

	if (!nilfs_psegment_is_end(&pseg)) {
		next = le64_to_cpu(pseg.p_segsum->ss_next) /
			le32_to_cpu(nilfs_get_sb(nilfs)->s_blocks_per_segment);
		printf("  sequence number = %llu, next segnum = %llu\n",
		       (unsigned long long)le64_to_cpu(pseg.p_segsum->ss_seq),
		       (unsigned long long)next);
		do {
			dumpseg_print_psegment(&pseg);
			nilfs_psegment_next(&pseg);
		} while (!nilfs_psegment_is_end(&pseg));
	}
}

int main(int argc, char *argv[])
{
	struct nilfs *nilfs;
	__u64 segnum;
	char *dev, *endptr, *progname, *last;
	void *seg;
	ssize_t segsize;
	int c, i, status;
#ifdef _GNU_SOURCE
	int option_index;
#endif	/* _GNU_SOURCE */

	opterr = 0;

	last = strrchr(argv[0], '/');
	progname = last ? last + 1 : argv[0];

#ifdef _GNU_SOURCE
	while ((c = getopt_long(argc, argv, "hV",
				long_option, &option_index)) >= 0) {
#else	/* !_GNU_SOURCE */
	while ((c = getopt(argc, argv, "hV")) >= 0) {
#endif	/* _GNU_SOURCE */

		switch (c) {
		case 'h':
			fprintf(stderr, DUMPSEG_USAGE, progname);
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
	} else {
		strtoull(argv[optind], &endptr, DUMPSEG_BASE);
		if (*endptr == '\0')
			dev = NULL;
		else
			dev = argv[optind++];
	}

	nilfs = nilfs_open(dev, NULL, NILFS_OPEN_RAW);
	if (nilfs == NULL)
		err(EXIT_FAILURE, "cannot open NILFS on %s", dev ? : "device");

	if (nilfs_opt_set_mmap(nilfs) < 0)
		warnx("cannot use mmap");

	status = EXIT_SUCCESS;
	for (i = optind; i < argc; i++) {
		segnum = strtoull(argv[i], &endptr, DUMPSEG_BASE);
		if (*endptr != '\0') {
			warnx("%s: invalid segment number", argv[i]);
			status = EXIT_FAILURE;
			continue;
		}
		segsize = nilfs_get_segment(nilfs, segnum, &seg);
		if (segsize < 0) {
			warn(NULL);
			status = EXIT_FAILURE;
			goto out;
		}
		dumpseg_print_segment(nilfs, segnum, seg, segsize);
		if (nilfs_put_segment(nilfs, seg) < 0) {
			warn(NULL);
			status = EXIT_FAILURE;
			goto out;
		}
	}

 out:
	nilfs_close(nilfs);
	exit(status);
}
