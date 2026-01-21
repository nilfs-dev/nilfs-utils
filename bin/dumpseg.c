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
 * Maintained by Ryusuke Konishi <konishi.ryusuke@gmail.com> from 2008.
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
#include "compat.h"	/* getprogname() */
#include "segment.h"

#ifdef _GNU_SOURCE
#include <getopt.h>
static const struct option long_option[] = {
	{"help", no_argument, NULL, 'h'},
	{"version", no_argument, NULL, 'V'},
	{NULL, 0, NULL, 0}
};

#define DUMPSEG_USAGE	\
	"Usage: %s [OPTION]... [DEVICE|NODE] SEGNUM...\n"		\
	"  -h, --help\t\tdisplay this help and exit\n"			\
	"  -V, --version\t\tdisplay version and exit\n"
#else	/* !_GNU_SOURCE */
#define DUMPSEG_USAGE	"Usage: %s [-h] [-V] [device|node] segnum...\n"
#endif	/* _GNU_SOURCE */


#define DUMPSEG_BASE	10
#define DUMPSEG_BUFSIZE	128

static void dumpseg_print_psegment_error(const struct nilfs_psegment *pseg,
					 const char *errstr)
{
	const struct nilfs_segment_summary *segsum = pseg->segsum;
	unsigned int hdrsize;
	uint32_t nblocks, sumbytes, excess;

	switch (pseg->error) {
	case NILFS_PSEGMENT_ERROR_ALIGNMENT:
		hdrsize = le16_to_cpu(segsum->ss_bytes);
		printf("  error %d (%s) - header size = %u\n",
		       pseg->error, errstr, hdrsize);
		break;
	case NILFS_PSEGMENT_ERROR_BIGPSEG:
		nblocks = le32_to_cpu(segsum->ss_nblocks);
		excess = ((uint32_t)(pseg->blocknr - pseg->segment->blocknr) +
			  nblocks) - pseg->segment->nblocks;
		printf("  error %d (%s) - pseg blkcnt = %" PRIu32 ", "
		       "excess blkcnt = %" PRIu32 "\n",
		       pseg->error, errstr, nblocks, excess);
		break;
	case NILFS_PSEGMENT_ERROR_BIGHDR:
		hdrsize = le16_to_cpu(segsum->ss_bytes);
		sumbytes = le32_to_cpu(segsum->ss_sumbytes);
		printf("  error %d (%s) - header size = %u, summary size = %"
		       PRIu32 "\n",
		       pseg->error, errstr, hdrsize, sumbytes);
		break;
	case NILFS_PSEGMENT_ERROR_BIGSUM:
		sumbytes = le32_to_cpu(segsum->ss_sumbytes);
		nblocks = le32_to_cpu(segsum->ss_nblocks);
		printf("  error %d (%s) - summary size = %" PRIu32 ", "
		       "pseg size = %" PRIu64 "\n",
		       pseg->error, errstr, sumbytes,
		       (uint64_t)nblocks << pseg->blkbits);
		break;
	default:
		printf("  error %d (%s)\n", pseg->error, errstr);
		break;
	}
}

static void dumpseg_print_file_error(const struct nilfs_file *file,
				     const char *errstr)
{
	const struct nilfs_psegment *pseg = file->psegment;
	static const char *indent = "    ";
	uint32_t nblocks, ndatablk, sumbytes, pseg_nblocks;

	switch (file->error) {
	case NILFS_FILE_ERROR_MANYBLKS:
		nblocks = le32_to_cpu(file->finfo->fi_nblocks);
		pseg_nblocks = le32_to_cpu(pseg->segsum->ss_nblocks);
		printf("%serror %d (%s) - file blkoff = %" PRIu32 ", "
		       "file blkcnt = %" PRIu32 ", pseg blkcnt = %" PRIu32 "\n",
		       indent, file->error, errstr,
		       (uint32_t)(file->blocknr - pseg->blocknr),
		       nblocks, pseg_nblocks);
		break;
	case NILFS_FILE_ERROR_BLKCNT:
		nblocks = le32_to_cpu(file->finfo->fi_nblocks);
		ndatablk = le32_to_cpu(file->finfo->fi_ndatablk);
		printf("%serror %d (%s) - file blkcnt = %" PRIu32 ", "
		       "data blkcnt = %" PRIu32 "\n",
		       indent, file->error, errstr, nblocks, ndatablk);
		break;
	case NILFS_FILE_ERROR_OVERRUN:
		sumbytes = le32_to_cpu(pseg->segsum->ss_sumbytes);
		printf("%serror %d (%s) - finfo offset = %" PRIu32 ", "
		       "finfo total size = %zu, summary size = %" PRIu32 "\n",
		       indent, file->error, errstr, file->offset, file->sumlen,
		       sumbytes);
		break;
	default:
		printf("%serror %d (%s)\n", indent, file->error, errstr);
		break;
	}
}

static void dumpseg_print_virtual_block(struct nilfs_block *blk)
{
	__le64 *binfo = blk->binfo;

	if (nilfs_block_is_data(blk)) {
		printf("        vblocknr = %" PRIu64 ", blkoff = %" PRIu64 ", "
		       "blocknr = %" PRIu64 "\n",
		       (uint64_t)le64_to_cpu(binfo[0]),
		       (uint64_t)le64_to_cpu(binfo[1]),
		       blk->blocknr);
	} else {
		printf("        vblocknr = %" PRIu64 ", blocknr = %" PRIu64 "\n",
		       (uint64_t)le64_to_cpu(binfo[0]), blk->blocknr);
	}
}

static void dumpseg_print_real_block(struct nilfs_block *blk)
{
	if (nilfs_block_is_data(blk)) {
		__le64 *binfo = blk->binfo;

		printf("        blkoff = %" PRIu64 ", blocknr = %" PRIu64 "\n",
		       (uint64_t)le64_to_cpu(binfo[0]), blk->blocknr);
	} else {
		struct nilfs_binfo_dat *bid = blk->binfo;

		printf("        blkoff = %" PRIu64 ", level = %d, blocknr = %"
		       PRIu64 "\n",
		       (uint64_t)le64_to_cpu(bid->bi_blkoff), bid->bi_level,
		       blk->blocknr);
	}
}

static void dumpseg_print_file(struct nilfs_file *file)
{
	struct nilfs_block blk;
	struct nilfs_finfo *finfo = file->finfo;

	printf("    finfo\n");
	printf("      ino = %" PRIu64 ", cno = %" PRIu64 ", nblocks = %" PRIu32
	       ", ndatblk = %" PRIu32 "\n",
	       (uint64_t)le64_to_cpu(finfo->fi_ino),
	       (uint64_t)le64_to_cpu(finfo->fi_cno),
	       le32_to_cpu(finfo->fi_nblocks),
	       le32_to_cpu(finfo->fi_ndatablk));
	if (!nilfs_file_use_real_blocknr(file)) {
		nilfs_block_for_each(&blk, file) {
			dumpseg_print_virtual_block(&blk);
		}
	} else {
		nilfs_block_for_each(&blk, file) {
			dumpseg_print_real_block(&blk);
		}
	}
}

static void dumpseg_print_psegment(struct nilfs_psegment *pseg)
{
	struct nilfs_file file;
	struct tm tm;
	const char *errstr;
	char timebuf[DUMPSEG_BUFSIZE];
	time_t t;

	printf("  partial segment: blocknr = %" PRIu64 ", nblocks = %" PRIu32 "\n",
	       pseg->blocknr, le32_to_cpu(pseg->segsum->ss_nblocks));

	t = (time_t)le64_to_cpu(pseg->segsum->ss_create);
	localtime_r(&t, &tm);
	strftime(timebuf, DUMPSEG_BUFSIZE, "%F %T", &tm);
	printf("    creation time = %s\n", timebuf);
	printf("    nfinfo = %" PRIu32 "\n",
	       le32_to_cpu(pseg->segsum->ss_nfinfo));
	nilfs_file_for_each(&file, pseg) {
		dumpseg_print_file(&file);
	}
	if (nilfs_file_is_error(&file, &errstr))
		dumpseg_print_file_error(&file, errstr);
}

static void dumpseg_print_segment(const struct nilfs_segment *segment)
{
	struct nilfs_psegment pseg;
	const char *errstr;
	uint64_t next;

	printf("segment: segnum = %" PRIu64 "\n", segment->segnum);
	nilfs_psegment_init(&pseg, segment, segment->nblocks);

	if (!nilfs_psegment_is_end(&pseg)) {
		next = le64_to_cpu(pseg.segsum->ss_next) /
			segment->blocks_per_segment;
		printf("  sequence number = %" PRIu64 ", next segnum = %" PRIu64
		       "\n",
		       (uint64_t)le64_to_cpu(pseg.segsum->ss_seq), next);
		do {
			dumpseg_print_psegment(&pseg);
			nilfs_psegment_next(&pseg);
		} while (!nilfs_psegment_is_end(&pseg));
	}

	if (nilfs_psegment_is_error(&pseg, &errstr))
		dumpseg_print_psegment_error(&pseg, errstr);
}

int main(int argc, char *argv[])
{
	struct nilfs *nilfs;
	uint64_t segnum;
	char *dev, *endptr;
	struct nilfs_segment segment;
	int c, i, status;
	int ret;
#ifdef _GNU_SOURCE
	int option_index;
#endif	/* _GNU_SOURCE */

#ifdef _GNU_SOURCE
	while ((c = getopt_long(argc, argv, "hV",
				long_option, &option_index)) >= 0) {
#else	/* !_GNU_SOURCE */
	while ((c = getopt(argc, argv, "hV")) >= 0) {
#endif	/* _GNU_SOURCE */

		switch (c) {
		case 'h':
			printf(DUMPSEG_USAGE, getprogname());
			exit(EXIT_SUCCESS);
		case 'V':
			printf("%s (%s %s)\n", getprogname(), PACKAGE,
			       PACKAGE_VERSION);
			exit(EXIT_SUCCESS);
		default:
			exit(EXIT_FAILURE);
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

	nilfs = nilfs_open(dev, NULL, NILFS_OPEN_RAW | NILFS_OPEN_SRCHDEV);
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
		ret = nilfs_get_segment(nilfs, segnum, &segment);
		if (ret < 0) {
			warn("failed to read segment");
			status = EXIT_FAILURE;
			goto out;
		}

		dumpseg_print_segment(&segment);

		ret = nilfs_put_segment(&segment);
		if (ret < 0) {
			warn("failed to release segment");
			status = EXIT_FAILURE;
			goto out;
		}
	}

 out:
	nilfs_close(nilfs);
	exit(status);
}
