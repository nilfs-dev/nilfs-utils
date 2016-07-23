/*
 * mkfs.c - NILFS newfs (mkfs.nilfs2)
 *
 * Copyright (C) 2005-2012 Nippon Telegraph and Telephone Corporation.
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
 * Written by Hisashi Hifumi,
 *            Amagai Yoshiji.
 * Revised by Ryusuke Konishi <konishi.ryusuke@lab.ntt.co.jp>.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif	/* HAVE_CONFIG_H */

#undef CONFIG_ATIME_FILE

#include <stdio.h>

#if HAVE_STDLIB_H
#include <stdlib.h>
#endif	/* HAVE_STDLIB_H */

#if HAVE_UNISTD_H
#include <unistd.h>
#endif	/* HAVE_UNISTD_H */

#if HAVE_FCNTL_H
#include <fcntl.h>
#endif	/* HAVE_FCNTL_H */

#if HAVE_STRINGS_H
#include <strings.h>
#endif	/* HAVE_STRINGS_H */

#include <stdarg.h>

#if HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif	/* HAVE_SYS_IOCTL_H */

#if HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif	/* HAVE_SYS_STAT_H */

#if HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif	/* HAVE_SYS_WAIT_H */

#include <uuid/uuid.h>

#if HAVE_STRING_H
#include <string.h>
#endif	/* HAVE_STRING_H */

#include <errno.h>

#if HAVE_BLKID_BLKID_H
#include <blkid/blkid.h>
#endif	/* HAVE_BLKID_BLKID_H */

#include "nilfs.h"
#include "util.h"
#include "nilfs_feature.h"
#include "mkfs.h"
#include "pathnames.h"


typedef __u64  blocknr_t;

extern __u32 crc32_le(__u32 seed, unsigned char const *data, size_t length);
#define nilfs_crc32(seed, data, length)  crc32_le(seed, data, length)

/*
 * System primitives
 */
#define LINE_BUFFER_SIZE	256  /* Line buffer size for reading mtab */
#define BLOCKSIZE_BUFFER_SIZE	100

#define BADBLOCKS_NAME		"badblocks"

static const char badblocks[] = "/sbin/" BADBLOCKS_NAME;

/*
 * Command interface primitives
 */
char *progname = "mkfs.nilfs2";

/* Options */
static int quiet;
static int cflag;
static int nflag;
static int verbose;
static int discard = 1;
static int force_overwrite;
static unsigned long blocksize = NILFS_DEF_BLOCKSIZE;
static unsigned long blocks_per_segment = NILFS_DEF_BLKS_PER_SEG;
static unsigned long r_segments_percentage = NILFS_DEF_RESERVED_SEGMENTS;

static time_t creation_time;
static char volume_label[80];
static __u64 compat_array[NILFS_MAX_FEATURE_TYPES] = {
	/* Compat */
	0,
	/* Read-only compat */
	0,
	/* Incompat */
	0
};

static void parse_options(int argc, char *argv[]);


/*
 * Initial disk layout:
 *
 * blk 0      1         2      3 ~ 5    6        7        8 - 10     11
 *  +-------+---------+------+--------+--------+--------+----------+-------+
 *  + Super | Segment | Root | ifile  | cpfile | sufile | DAT file | Super |
 *  + block | summary | dir  | blocks | block  | block  | blocks   | root  |
 *  +-------+---------+------+--------+--------+--------+----------+-------+
 *           ^
 *           sb->s_first_data_block
 *
 * Initial layout of ifile and DAT file:
 *
 * blk +0                  +1       +2
 *  +------------------+--------+---------+
 *  + Group descriptor | Bitmap | Initial |
 *  + block            | block  | entries |
 *  +------------------+--------+---------+
 *
 * Notes:
 *  - The B-trees of the root directory and meta data files are embedded in
 *    their disk inodes.
 *  - The number of ifile (entry) blocks depends on the blocksize.
 *    For small block sizes, it requires multiple blocks to store all initial
 *    inodes.
 */

static const unsigned group_desc_blocks_per_group = 1;
static const unsigned bitmap_blocks_per_group = 1;
static const unsigned nr_initial_segments = 2; /* initial segment + next */
static const unsigned nr_initial_inodes = 2;  /* root directory + .nilfs */
static const __u64 first_cno = 1; /* Number of the first checkpoint */

/* Segment layout information (per partial segment) */
#define MAX_FILES        16

struct nilfs_file_info {
	ino_t ino;
	blocknr_t start;
	unsigned nblocks;
	struct nilfs_inode *raw_inode;
};

struct nilfs_segment_info {
	blocknr_t       start;
	unsigned        nblocks;
	unsigned        nfinfo;
	unsigned        nfiles;
	unsigned long   sumbytes;
	unsigned        nblk_sum;
	struct nilfs_file_info files[MAX_FILES];

	unsigned        nvblocknrs;
	/* + super root (1 block)*/
};

/* Disk layout information */
struct nilfs_disk_info {
	const char      *device;
	__u64           dev_size;
	int             blkbits;
	time_t          ctime;
	__u32           crc_seed;

	unsigned long   blocks_per_segment;
	unsigned long   nsegments;
	blocknr_t       first_segment_block;

	unsigned long   nblocks_to_write;
	unsigned long   nblocks_used;
	unsigned        nsegments_to_write;

	struct nilfs_segment_info seginfo[1];  /* Organization of the initial
						  segment */
	unsigned nseginfo;
};

struct nilfs_segment_ref {
	__u64 seq;                      /* Sequence number of a full segment */
	blocknr_t start;                /* Start block of the partial segment
					   having a valid super root */
	blocknr_t free_blocks_count;
	__u64 cno;                /* checkpoint number */
};

static void init_disk_layout(struct nilfs_disk_info *di, int fd,
			     const char *device);

static inline blocknr_t count_free_blocks(struct nilfs_disk_info *di)
{
	return di->blocks_per_segment *
		(di->nsegments - di->nsegments_to_write);
}

static inline blocknr_t
segment_start_blocknr(struct nilfs_disk_info *di, unsigned long segnum)
{
	return segnum > 0 ? di->blocks_per_segment * segnum :
		di->first_segment_block;
}

/*
 * I/O primitives
 */
static void **disk_buffer;
static unsigned long disk_buffer_size;

static void init_disk_buffer(long max_blocks);
static void destroy_disk_buffer(void);
static void *map_disk_buffer(blocknr_t blocknr, int clear_flag);

static void read_disk_header(int fd, const char *device);
static void write_disk(int fd, struct nilfs_disk_info *di);

/*
 * Routines to format blocks
 */
static struct nilfs_super_block *raw_sb;

static void prepare_super_block(struct nilfs_disk_info *di);
static void commit_super_block(struct nilfs_disk_info *di,
			       const struct nilfs_segment_ref *segref);

struct nilfs_fs_info {
	struct nilfs_disk_info *diskinfo;
	struct nilfs_segment_info *current_segment;

	struct nilfs_segment_ref last_segment_ref;
	struct nilfs_segment_summary *segsum;
	struct nilfs_checkpoint *checkpoint;
	struct nilfs_super_root *super_root;

	struct nilfs_file_info *files[NILFS_MAX_INITIAL_INO];

	blocknr_t next, altnext;
	unsigned seq;
	__u64 cno;
	blocknr_t vblocknr;
};

static struct nilfs_fs_info nilfs;

static void init_nilfs(struct nilfs_disk_info *di);
static void prepare_segment(struct nilfs_segment_info *);
static void commit_segment(void);
static void nilfs_mkfs_make_rootdir(void);
static void nilfs_mkfs_make_dot_nilfs(void);
static void nilfs_mkfs_make_reserved_files(void);


static inline struct nilfs_segment_ref *get_last_segment(void)
{
	return &nilfs.last_segment_ref;
}


/* Print routines */
static void usage(void);
static void show_version(void);
static void pinfo(const char *, ...);
static void perr(const char *, ...);
static void cannot_rw_device(int, const char *, int);
static void cannot_allocate_memory(void);
static void too_small_segment(unsigned long, unsigned long);

/* I/O routines */
#ifdef __linux__

#ifndef BLKDISCARD
#define BLKDISCARD	_IO(0x12, 119)
#endif

#ifndef BLKDISCARDZEROES
#define BLKDISCARDZEROES _IO(0x12, 124)
#endif

/**
 * nilfs_mkfs_discard_range - issue discard command to the device
 * @fd: file descriptor of the device
 * @start: start offset of the region to discard (in bytes)
 * @len: length of the region to discard (in bytes)
 *
 * Returns zero if the discard succeeds.  Otherwise, -1 is returned.
 */
static int nilfs_mkfs_discard_range(int fd, __u64 start, __u64 len)
{
	__u64 range[2] = { start, len };
	int ret;

	ret = ioctl(fd, BLKDISCARD, &range);
	if (verbose) {
		pinfo("Discard device from %llu to %llu: %s.",
		      (unsigned long long)start,
		      (unsigned long long)start + len,
		      ret ? "failed" : "succeeded");
	}
	return ret;
}

/**
 * nilfs_mkfs_discard_zeroes_data - get if discarded blocks are zeroed or not
 * @fd: file descriptor of the device
 */
static int nilfs_mkfs_discard_zeroes_data(int fd)
{
	int discard_zeroes_data = 0;

	ioctl(fd, BLKDISCARDZEROES, &discard_zeroes_data);
	return discard_zeroes_data;
}
#else
#define nilfs_mkfs_discard_range(fd, start, len)	1
#define nilfs_mkfs_discard_zeroes_data(fd)		0
#endif

static void disk_scan(const char *device);
static void check_mount(int fd, const char *device);

#if HAVE_LIBBLKID
static void check_safety_of_device_overwrite(int fd, const char *device);
#else
#define check_safety_of_device_overwrite(fd, device) do {} while (0)
#endif /* HAVE_LIBBLKID */

/*
 * Routines to decide disk layout
 */
static unsigned count_blockgrouped_file_blocks(unsigned entry_size,
					       unsigned nr_initial_entries)
{
	unsigned long entries_per_block = blocksize / entry_size;

	return group_desc_blocks_per_group + bitmap_blocks_per_group +
		DIV_ROUND_UP(nr_initial_entries, entries_per_block);
}

static unsigned count_ifile_blocks(void)
{
	unsigned long entries_per_group = blocksize * 8; /* CHAR_BIT */
	unsigned nblocks;

	nblocks = count_blockgrouped_file_blocks(sizeof(struct nilfs_inode),
						 NILFS_MAX_INITIAL_INO);
	if (NILFS_MAX_INITIAL_INO > entries_per_group ||
	    nblocks > NILFS_MAX_BMAP_ROOT_PTRS)
		perr("Internal error: too many initial inodes");
	return nblocks;
}

static unsigned count_sufile_blocks(void)
{
	unsigned long sufile_segment_usages_per_block =
		blocksize / sizeof(struct nilfs_segment_usage);

	return DIV_ROUND_UP(nr_initial_segments +
			   NILFS_SUFILE_FIRST_SEGMENT_USAGE_OFFSET,
			   sufile_segment_usages_per_block);
}

static unsigned count_cpfile_blocks(void)
{
	const unsigned nr_initial_checkpoints = 1;
	unsigned long cpfile_checkpoints_per_block =
		blocksize / sizeof(struct nilfs_checkpoint);

	return DIV_ROUND_UP(nr_initial_checkpoints +
			   NILFS_CPFILE_FIRST_CHECKPOINT_OFFSET
			   - 1 /* checkpoint number begins from 1 */,
			   cpfile_checkpoints_per_block);
}

static unsigned count_dat_blocks(unsigned nr_dat_entries)
{
	unsigned long entries_per_group = blocksize * 8; /* CHAR_BIT */
	unsigned nblocks;

	nblocks = count_blockgrouped_file_blocks(
		sizeof(struct nilfs_dat_entry), nr_dat_entries);
	if (nr_dat_entries > entries_per_group ||
	    nblocks > NILFS_MAX_BMAP_ROOT_PTRS)
		perr("Internal error: too many initial dat entries");
	return nblocks;
}

static __attribute__((used)) void nilfs_check_ondisk_sizes(void)
{
	BUILD_BUG_ON(sizeof(struct nilfs_inode) > NILFS_MIN_BLOCKSIZE);
	BUILD_BUG_ON(sizeof(struct nilfs_sufile_header) > NILFS_MIN_BLOCKSIZE);
	BUILD_BUG_ON(sizeof(struct nilfs_segment_usage) > NILFS_MIN_BLOCKSIZE);
	BUILD_BUG_ON(sizeof(struct nilfs_cpfile_header) > NILFS_MIN_BLOCKSIZE);
	BUILD_BUG_ON(sizeof(struct nilfs_checkpoint) > NILFS_MIN_BLOCKSIZE);
	BUILD_BUG_ON(sizeof(struct nilfs_dat_entry) > NILFS_MIN_BLOCKSIZE);
	BUILD_BUG_ON(sizeof(struct nilfs_super_root) > NILFS_MIN_BLOCKSIZE);
}

static unsigned long
__increment_segsum_size(unsigned long offset, unsigned item_size,
			unsigned count)
{
	unsigned long offset2;
	unsigned rest_items_in_block =
		(blocksize - offset % blocksize) / item_size;

	if (count <= rest_items_in_block)
		offset2 = offset + item_size * count;
	else {
		unsigned nitems_per_block = blocksize / item_size;

		count -= rest_items_in_block;
		offset2 = blocksize *
			(offset / blocksize + 1 + count / nitems_per_block) +
			(count % nitems_per_block * item_size);
	}
	return offset2;
}

static void increment_segsum_size(struct nilfs_segment_info *si,
				  unsigned nblocks_in_file, int dat_flag)
{
	unsigned binfo_size = dat_flag ?
		sizeof(__le64) /* offset */ : sizeof(struct nilfs_binfo_v);

	si->sumbytes = __increment_segsum_size(si->sumbytes,
					       sizeof(struct nilfs_finfo), 1);
	si->sumbytes = __increment_segsum_size(si->sumbytes,
					       binfo_size, nblocks_in_file);
}

static inline int my_log2(long i)
{
	int n;

	for (n = 0; i > 1; i >>= 1)
		n++;
	return n;
}

static unsigned long nilfs_min_nsegments(struct nilfs_disk_info *di, long rp)
{
	/* Minimum number of full segments */
	return max_t(unsigned long,
		     (rp * di->nsegments - 1) / 100 + 1, NILFS_MIN_NRSVSEGS) +
		max_t(unsigned long, nr_initial_segments, NILFS_MIN_NUSERSEGS);
}

static void init_disk_layout(struct nilfs_disk_info *di, int fd,
			     const char *device)
{
	__u64 dev_size;
	time_t nilfs_time = time(NULL);
	unsigned long min_nsegments;
	unsigned long segment_size;
	blocknr_t first_segblk;
	struct stat stat;

	if (fstat(fd, &stat) != 0)
		perr("Cannot stat device (%s)", device);

	if (S_ISBLK(stat.st_mode)) {
		if (ioctl(fd, BLKGETSIZE64, &dev_size) != 0)
			perr("Error: cannot get device size! (%s)", device);
	} else
		dev_size = stat.st_size;

	di->device = device;
	di->dev_size = dev_size;
	di->blkbits = my_log2(blocksize);
	di->ctime = (creation_time ? : nilfs_time);
	srand48(nilfs_time);
	di->crc_seed = (__u32)mrand48();

	di->blocks_per_segment = blocks_per_segment;
	segment_size = di->blocks_per_segment * blocksize;
	first_segblk = DIV_ROUND_UP(NILFS_DISKHDR_SIZE, blocksize);
	di->first_segment_block = first_segblk;
	if (first_segblk + NILFS_PSEG_MIN_BLOCKS > di->blocks_per_segment)
		too_small_segment(di->blocks_per_segment,
				  first_segblk + NILFS_PSEG_MIN_BLOCKS);

	di->nsegments = (NILFS_SB2_OFFSET_BYTES(dev_size) >> di->blkbits) /
		di->blocks_per_segment;
	min_nsegments = nilfs_min_nsegments(di, r_segments_percentage);
	if (di->nsegments < min_nsegments)
		perr("Error: too small device.\n"
		     "       device size=%llu bytes, required size=%llu bytes.\n"
		     "       Please enlarge the device, or shorten segments with -B option.",
		     dev_size,
		     (unsigned long long)segment_size * min_nsegments);
	di->nseginfo = 0;
}

static struct nilfs_segment_info *new_segment(struct nilfs_disk_info *di)
{
	struct nilfs_segment_info *si;

	if (di->nseginfo)
		perr("Internal error: too many segments");
	si = &di->seginfo[di->nseginfo++];
	memset(si, 0, sizeof(*si));

	si->sumbytes = sizeof(struct nilfs_segment_summary);
	si->nblk_sum = DIV_ROUND_UP(si->sumbytes, blocksize);
	si->start = di->first_segment_block; /* for segment 0 */
	return si;
}

static void fix_disk_layout(struct nilfs_disk_info *di)
{
	struct nilfs_segment_info *si;
	int i, j;

	di->nblocks_used = 0;
	di->nblocks_to_write = di->first_segment_block;
	for (i = 0, si = di->seginfo; i < di->nseginfo; i++, si++) {
		blocknr_t blocknr = si->start + si->nblk_sum;

		si->nblocks += si->nblk_sum + 1 /* summary and super root */;
		if (si->nblocks > di->blocks_per_segment)
			too_small_segment(di->blocks_per_segment, si->nblocks);

		for (j = 0; j < si->nfiles; j++) {
			struct nilfs_file_info *fi = &si->files[j];

			if (!fi->nblocks)
				continue;
			fi->start = blocknr;
			blocknr += fi->nblocks;

			if (fi->ino != NILFS_DAT_INO &&
			    fi->ino != NILFS_SUFILE_INO &&
			    fi->ino != NILFS_CPFILE_INO)
				di->nblocks_used += fi->nblocks;
		}
		if (di->nblocks_to_write < si->start + si->nblocks)
			di->nblocks_to_write = si->start + si->nblocks;
	}
	di->nsegments_to_write = DIV_ROUND_UP(di->nblocks_to_write,
					     di->blocks_per_segment);
}

static void add_file(struct nilfs_segment_info *si, ino_t ino,
		     unsigned nblocks, int dat_flag)
{
	struct nilfs_file_info *fi;

	if (si->nfiles >= MAX_FILES)
		perr("Internal error: too many files");
	if (ino >= NILFS_MAX_INITIAL_INO)
		perr("Internal error: inode number out of range");

	fi = &si->files[si->nfiles++];
	fi->ino = ino;
	fi->start = 0;
	fi->nblocks = nblocks;
	si->nblocks += nblocks;
	if (nblocks > 0) {
		si->nfinfo++;
		increment_segsum_size(si, nblocks, dat_flag);
		if (!dat_flag)
			si->nvblocknrs += nblocks;
		si->nblk_sum = DIV_ROUND_UP(si->sumbytes, blocksize);
	}
}


int main(int argc, char *argv[])
{
	struct nilfs_disk_info diskinfo, *di = &diskinfo;
	struct nilfs_segment_info *si;
	struct stat statbuf;
	const char *device;
	int fd;

	parse_options(argc, argv);
	device = argv[optind];

	if (stat(device, &statbuf) != 0)
		perr("Error: cannot find %s: %s", device, strerror(errno));
	else if (!S_ISREG(statbuf.st_mode) && !S_ISBLK(statbuf.st_mode))
		perr("Error: device must be a block device or a file");

	if (cflag)
		disk_scan(device);  /* check the block device */

	fd = open(device, O_RDWR);
	if (fd < 0)
		perr("Error: cannot open device: %s", device);
	check_mount(fd, device);
	check_safety_of_device_overwrite(fd, device);

	init_disk_layout(di, fd, device);
	si = new_segment(di);

	add_file(si, NILFS_ROOT_INO, 1, 0);
	add_file(si, NILFS_NILFS_INO, 0, 0);
	add_file(si, NILFS_ATIME_INO, 0, 0);
	add_file(si, 1, 0, 0);
	add_file(si, 8, 0, 0);
	add_file(si, 9, 0, 0);
	add_file(si, 10, 0, 0);
	add_file(si, NILFS_IFILE_INO, count_ifile_blocks(), 0);
	add_file(si, NILFS_CPFILE_INO, count_cpfile_blocks(), 0);
	add_file(si, NILFS_SUFILE_INO, count_sufile_blocks(), 0);
	add_file(si, NILFS_DAT_INO, count_dat_blocks(si->nvblocknrs), 1);

	fix_disk_layout(di);

	/* making the initial segment */
	init_disk_buffer(di->nblocks_to_write);
	read_disk_header(fd, device);

	prepare_super_block(di);
	init_nilfs(di);

	prepare_segment(&di->seginfo[0]);
	nilfs_mkfs_make_dot_nilfs(); /* Make .nilfs */
	nilfs_mkfs_make_rootdir();  /* Make root directory */
	nilfs_mkfs_make_reserved_files();
	commit_segment();

	commit_super_block(di, get_last_segment());

	write_disk(fd, di); /* Writing to the device */

	close(fd);
	exit(EXIT_SUCCESS);
}

/*
 * I/O routines & primitives
 */
static void disk_scan(const char *device)
{
	struct stat statbuf;
	pid_t pid;
	int status;

	if (stat(badblocks, &statbuf) != 0) {
		pinfo("Warning: %s not found.", badblocks);
		return;
	}

	if (!quiet)
		pinfo("checking blocks");

	pid = fork();
	if (pid == 0) {
		char bszbuf[BLOCKSIZE_BUFFER_SIZE];
		const char *args[7];
		ssize_t n;
		int i = 0;

		if (setgid(getgid()) < 0)
			perr("Error: failed to drop setgid privileges");
		if (setuid(getuid()) < 0)
			perr("Error: failed to drop setuid privileges");
		args[i++] = badblocks;
		args[i++] = "-b";
		n = snprintf(bszbuf, BLOCKSIZE_BUFFER_SIZE, "%ld", blocksize);
		if (n < 0 || n >= BLOCKSIZE_BUFFER_SIZE)
			perr("Internal error: blocksize buffer overflow");
		args[i++] = bszbuf;

		if (!quiet)
			args[i++] = "-s";
		if (cflag > 1)
			args[i++] = "-w";
		args[i++] = device;
		args[i] = NULL;
		execv(badblocks, (char **)args);
		exit(EXIT_FAILURE); /* reach only if failed */
	} else if (pid != -1) {
		if (wait(&status) < 0)
			perr("Error: cannot wait child");
		else if (!WIFEXITED(status))
			perr("Error: check aborted");
		else if (WEXITSTATUS(status))
			perr("Error: check failed with status(%d)",
			     WEXITSTATUS(status));
	} else {
		perr("Error: cannot fork: %s", strerror(errno));
	}
}

static void check_mount(int fd, const char *device)
{
	FILE *fp;
	char line[LINE_BUFFER_SIZE];

	fp = fopen(_PATH_MOUNTED, "r");
	if (fp == NULL) {
		close(fd);
		perr("Error: cannot open %s!", _PATH_MOUNTED);
	}

	while (fgets(line, LINE_BUFFER_SIZE, fp) != NULL) {
		if (strncmp(strtok(line, " "), device, strlen(device)) == 0) {
			fclose(fp);
			close(fd);
			perr("Error: %s is currently mounted. You cannot make a filesystem on this device.",
			     device);
		}
	}
	fclose(fp);
}

#if HAVE_LIBBLKID
static void check_safety_of_device_overwrite(int fd, const char *device)
{
	int c, c_next;
	blkid_probe pr = NULL;
	blkid_loff_t size;
	int ret = 0;

	if (!device || !*device)
		return;

	if (force_overwrite == 0) {
		pr = blkid_new_probe_from_filename(device);
		if (!pr) {
			ret = -1;
			goto end_check;
		}

		size = blkid_probe_get_size(pr);
		if (size <= 0)
			goto end_check;

		ret = blkid_probe_enable_partitions(pr, 1);
		if (ret < 0)
			goto end_check;

		ret = blkid_do_fullprobe(pr);
		if (ret < 0) /* error */
			goto end_check;
		else if (ret == 0) { /* some signature was found */
			const char *type;

			if (!blkid_probe_lookup_value(pr, "TYPE",
							&type, NULL)) {
				pinfo("WARNING: Device %s appears to contain an existing %s superblock.",
				      device, type);
			} else if (!blkid_probe_lookup_value(pr, "PTTYPE",
								&type, NULL)) {
				pinfo("WARNING: Device %s appears to contain an partition table (%s).",
				      device, type);
			} else {
				if (quiet == 0) {
					pinfo("Device %s appears to contain something weird.",
					      device);
				}
				goto end_check;
			}

			pinfo("WARNING: All data will be lost after format!");
			pinfo("\nDO YOU REALLY WANT TO FORMAT DEVICE %s?",
				device);

			do {
				fprintf(stderr, "\nContinue? [y/N] ");
				c = getchar();

				if (c == EOF || c == '\n')
					goto abort_format;

				c_next = getchar();
				if (c_next != EOF && c_next != '\n')
					goto clear_input_buffer;

				if (c == 'n' || c == 'N')
					goto abort_format;

clear_input_buffer:
				while (c_next != '\n' && c_next != EOF)
					c_next = getchar();
			} while (c != 'y' && c != 'Y');
		}
	}

end_check:
	if (pr)
		blkid_free_probe(pr);
	if (quiet == 0 && ret < 0)
		pinfo("Probe of %s failed, can't detect fs existence.",
			device);
	return;

abort_format:
	if (pr)
		blkid_free_probe(pr);
	close(fd);
	perr("Abort format of device %s", device);
}
#endif /* HAVE_LIBBLKID */

static void destroy_disk_buffer(void)
{
	if (disk_buffer) {
		void **pb = disk_buffer, **ep = disk_buffer + disk_buffer_size;

		while (pb < ep) {
			if (*pb)
				free(*pb);
			pb++;
		}
		free(disk_buffer);
		disk_buffer = NULL;
	}
}

static void init_disk_buffer(long max_blocks)
{
	disk_buffer = calloc(max_blocks, sizeof(void *));
	if (!disk_buffer)
		cannot_allocate_memory();

	memset(disk_buffer, 0, max_blocks * sizeof(void *));
	disk_buffer_size = max_blocks;

	atexit(destroy_disk_buffer);
}

static void *map_disk_buffer(blocknr_t blocknr, int clear_flag)
{
	if (blocknr >= disk_buffer_size)
		perr("Internal error: illegal disk buffer access (blocknr=%llu)",
		     blocknr);

	if (!disk_buffer[blocknr]) {
		if (posix_memalign(&disk_buffer[blocknr], blocksize,
				   blocksize) != 0)
			cannot_allocate_memory();
		if (clear_flag)
			memset(disk_buffer[blocknr], 0, blocksize);
	}
	return disk_buffer[blocknr];
}

static void read_disk_header(int fd, const char *device)
{
	int i, hdr_blocks = DIV_ROUND_UP(NILFS_SB_OFFSET_BYTES, blocksize);

	lseek(fd, 0, SEEK_SET);
	for (i = 0; i < hdr_blocks; i++) {
		if (read(fd, map_disk_buffer(i, 0), blocksize) < 0)
			cannot_rw_device(fd, device, 1);
	}
}

static int device_has_boot_sector(void)
{
	const __le32 *bssig = map_disk_buffer(0, 0) + 0x1fe;

	return le32_to_cpu(*bssig) == 0xaa55;
}

#define MAX_NBLOCKS_CLEAR_BUFFER	8

static int erase_disk_range(int fd, off_t offset, size_t count)
{
	void *buffer;
	size_t size, bufsz;
	int ret = -1;

	for (bufsz = blocksize * MAX_NBLOCKS_CLEAR_BUFFER;
	     bufsz >= blocksize; bufsz >>= 1) {
		buffer = malloc(bufsz);
		if (buffer != NULL)
			break;
	}
	if (bufsz < blocksize)
		cannot_allocate_memory();

	memset(buffer, 0, bufsz);

	if (lseek(fd, offset, SEEK_SET) < 0)
		goto failed;

	while (count > 0) {
		size = count > bufsz ? bufsz : count;
		if (write(fd, buffer, size) < size)
			goto failed;
		count -= size;
	}
	ret = 0;

 failed:
	free(buffer);
	return ret;
}

static int erase_disk(int fd, struct nilfs_disk_info *di)
{
	const unsigned int sector_size = 512;
	off_t start, end;
	int ret;

	/*
	 * Define range of the partition that nilfs uses.  This should
	 * not depend on the type of underlying device.
	 */
	start = device_has_boot_sector() ? NILFS_SB_OFFSET_BYTES : 0;
	end = di->dev_size & ~((__u64)sector_size - 1);

	BUG_ON(end < NILFS_DISK_ERASE_SIZE ||
	       end - NILFS_DISK_ERASE_SIZE < start);

	if (discard) {
		ret = nilfs_mkfs_discard_range(fd, start, end - start);
		if (!ret && nilfs_mkfs_discard_zeroes_data(fd)) {
			if (verbose)
				pinfo("Discard succeeded and will return 0s  - skip wiping");
			goto out;
		}
	}

	/* Erase tail of partition */
	ret = erase_disk_range(fd, end - NILFS_DISK_ERASE_SIZE,
			       NILFS_DISK_ERASE_SIZE);
	if (ret == 0) {
		/* Erase head of partition */
		ret = erase_disk_range(fd, start,
				       NILFS_DISK_ERASE_SIZE - start);
	}
out:
	return ret;
}

static void write_disk(int fd, struct nilfs_disk_info *di)
{
	blocknr_t blocknr;
	struct nilfs_segment_info *si;
	int i;

	if (!quiet) {
		show_version();
		pinfo("Start writing file system initial data to the device\n"
		      "       Blocksize:%d  Device:%s  Device Size:%llu",
		      blocksize, di->device, di->dev_size);
	}
	if (!nflag) {
		if (erase_disk(fd, di) < 0)
			goto failed_to_write;

		/* Writing segments */
		for (i = 0, si = di->seginfo; i < di->nseginfo; i++, si++) {
			lseek(fd, si->start * blocksize, SEEK_SET);
			for (blocknr = si->start;
			     blocknr < si->start + si->nblocks; blocknr++) {
				if (write(fd, map_disk_buffer(blocknr, 1),
					  blocksize) < 0)
					goto failed_to_write;
			}
		}
		if (fsync(fd) < 0)
			goto failed_to_write;

		/* Writing primary super block */
		if (lseek(fd, NILFS_SB_OFFSET_BYTES, SEEK_SET) < 0 ||
		    write(fd, raw_sb, sizeof(*raw_sb)) < 0)
			goto failed_to_write;

		/* Writing secondary super block */
		if (lseek(fd, NILFS_SB2_OFFSET_BYTES(di->dev_size),
			  SEEK_SET) < 0 ||
		    write(fd, raw_sb, sizeof(*raw_sb)) < 0)
			goto failed_to_write;

		if (fsync(fd) < 0)
			goto failed_to_write;
	}
	if (!quiet)
		pinfo("File system initialization succeeded !! ");
	return;

 failed_to_write:
	cannot_rw_device(fd, di->device, 0);
}

/*
 * Routines for the command line parser
 */
static inline void check_blocksize(long blocksize)
{
	if (blocksize > sysconf(_SC_PAGESIZE) ||
	    blocksize < NILFS_MIN_BLOCKSIZE ||
	    ((blocksize - 1) & blocksize) != 0)
		perr("Error: invalid blocksize: %d", blocksize);
}

static inline void check_blocks_per_segment(long blocks_per_segment)
{
	if (blocks_per_segment < NILFS_SEG_MIN_BLOCKS)
		perr("Error: too few blocks per segment: %d",
		     blocks_per_segment);
	if (((blocks_per_segment - 1) & blocks_per_segment) != 0)
		perr("Error: invalid number of blocks per segment: %d",
		     blocks_per_segment);
}

static inline void
check_reserved_segments_percentage(long r_segments_percentage)
{
	if (r_segments_percentage < 1)
		perr("Error: too small reserved segments percentage: %d",
		     r_segments_percentage);
	if (r_segments_percentage > 99)
		perr("Error: too large reserved segments percentage: %d",
		     r_segments_percentage);
}

static inline void check_ctime(time_t ctime)
{
	if ((long)time(NULL) - (long)ctime < 0) {
		char cbuf[26], *cbufp;

		ctime_r(&ctime, cbuf);
		cbufp = rindex(cbuf, '\n');
		if (cbufp)
			*cbufp = '\0';
		pinfo("Warning: Future time: %s (%ld)",
		      cbuf, (long)ctime);
	}
}

static const __u64 ok_features[NILFS_MAX_FEATURE_TYPES] = {
	/* Compat */
	0,
	/* Read-only compat */
	NILFS_FEATURE_COMPAT_RO_BLOCK_COUNT,
	/* Incompat */
	0
};

static void nilfs_mkfs_edit_feature(const char *str)
{
	if (nilfs_edit_feature(str, compat_array, ok_features, NULL, NULL,
			       NULL) < 0)
		perr("Error: invalid feature set option: %s", str);
}

static void parse_options(int argc, char *argv[])
{
	int c, show_version_only = 0;
	char *fs_features = NULL;

	while ((c = getopt(argc, argv, "b:B:cfhKL:m:nqvO:P:V")) != EOF) {
		switch (c) {
		case 'b':
			blocksize = atol(optarg);
			check_blocksize(blocksize);
			break;
		case 'B':
			blocks_per_segment = atol(optarg);
			break;
		case 'c':
			cflag++;
			break;
		case 'f':
			force_overwrite = 1;
			break;
		case 'h':
			usage();
			exit(EXIT_SUCCESS);
		case 'K':
			discard = 0;
			break;
		case 'L':
			strncpy(volume_label, optarg, sizeof(volume_label));
			break;
		case 'm':
			r_segments_percentage = atol(optarg);
			break;
		case 'n':
			nflag = 1;
			break;
		case 'q':
			quiet = 1;
			break;
		case 'v':
			verbose++;
			break;
		case 'O':
			fs_features = optarg;
			break;
		case 'P': /* Passive mode */
			creation_time = atol(optarg);
			check_ctime(creation_time);
			break;
		case 'V':
			show_version_only = 1;
			break;
		default:
			usage();
			exit(EXIT_FAILURE);
		}
	}

	if (quiet)
		verbose = 0;

	if ((optind == argc) && !show_version_only) {
		printf("%d %d\n", optind, argc);
		usage();
		exit(EXIT_FAILURE);
	}

	if (show_version_only) {
		show_version();
		exit(EXIT_SUCCESS);
	}

	check_blocks_per_segment(blocks_per_segment);
	check_reserved_segments_percentage(r_segments_percentage);

	if (fs_features)
		nilfs_mkfs_edit_feature(fs_features);

	if (argc > 0) {
		char *cp = strrchr(argv[0], '/');

		progname = (cp ? cp + 1 : argv[0]);
	}
}

/*
 * Print routines
 */
static void usage(void)
{
	fprintf(stderr,
		"Usage: %s [-b block-size] [-B blocks-per-segment] [-c] [-f]\n"
		"       [-L volume-label] [-m reserved-segments-percentage]\n"
		"       [-O feature[,...]]\n"
		"       [-hnqvKV] device\n",
		progname);
}

static void show_version(void)
{
	fprintf(stderr, "%s (%s %s)\n", progname, PACKAGE, PACKAGE_VERSION);
}

static void pinfo(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	fprintf(stderr, "\n");
	va_end(args);
}

static void perr(const char *fmt, ...)
{
	va_list args;

	show_version();
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	fprintf(stderr, "\n");
	va_end(args);
	exit(EXIT_FAILURE);
}

static void cannot_rw_device(int fd, const char *device, int rw)
{
	close(fd);
	perr("Error: cannot %s device.", rw ? "read" : "write");
}

static void cannot_allocate_memory(void)
{
	perr("Error: memory allocation failure");
}

static void too_small_segment(unsigned long blocks_per_segment,
			      unsigned long required_blocks)
{
	perr("Error: too small segment.\n"
	     "       segment size=%lu blocks, required segment size=%lu blocks.\n"
	     "       Please enlarge segment with -B option.",
	     blocks_per_segment, required_blocks);
}

/*
 * Filesystem state
 */
static void init_nilfs(struct nilfs_disk_info *di)
{
	memset(&nilfs, 0, sizeof(nilfs));
	nilfs.diskinfo = di;
	nilfs.next = segment_start_blocknr(di, 1);
	nilfs.seq = 0;
	nilfs.cno = 1;
	nilfs.vblocknr = 1;
}

/*
 * Routines to format blocks
 */
static void reserve_ifile_inode(ino_t ino);
static blocknr_t assign_vblocknr(blocknr_t blocknr);

static void init_inode(ino_t ino, unsigned type, int mode, unsigned size)
{
	struct nilfs_inode *raw_inode = nilfs.files[ino]->raw_inode;

	raw_inode->i_mode =		cpu_to_le16((type << 12) | mode);
	raw_inode->i_flags =		0;
	raw_inode->i_size =		cpu_to_le64(size);
	raw_inode->i_blocks =		cpu_to_le64(nilfs.files[ino]->nblocks);
	raw_inode->i_links_count =	cpu_to_le16(1);
	raw_inode->i_ctime =		cpu_to_le64(nilfs.diskinfo->ctime);
	raw_inode->i_mtime =		cpu_to_le64(nilfs.diskinfo->ctime);

	if (ino >= NILFS_USER_INO)
		reserve_ifile_inode(ino);
}

static void inc_link_count(int ino)
{
	struct nilfs_inode *raw_inode = nilfs.files[ino]->raw_inode;

	raw_inode->i_links_count
		= cpu_to_le16(le16_to_cpu(raw_inode->i_links_count) + 1);
}

static volatile struct nilfs_dir_entry *
next_dir_entry(volatile struct nilfs_dir_entry *de)
{
	return (void *)de + nilfs_rec_len_from_disk(de->rec_len);
}

static void nilfs_mkfs_make_rootdir(void)
{
	void *dirbuf = map_disk_buffer(nilfs.files[NILFS_ROOT_INO]->start, 1);
	volatile struct nilfs_dir_entry *de = dirbuf;
		/* volatile keyword is inserted to prevent failure of
		   substitution to de->inode on a certain environment. */
	unsigned rec_len, rec_end;

	init_inode(NILFS_ROOT_INO, DT_DIR, 0755, blocksize);

	de->inode = cpu_to_le64(NILFS_ROOT_INO);
	de->name_len = 1;
	rec_end = rec_len = NILFS_DIR_REC_LEN(1);
	de->rec_len = nilfs_rec_len_to_disk(rec_len);
	de->file_type = NILFS_FT_DIR;
	memcpy((void *)de->name, ".\0\0\0\0\0\0", 8);

	de = next_dir_entry(de);
	de->inode = cpu_to_le64(NILFS_ROOT_INO);
	de->name_len = 2;
	rec_end += (rec_len = NILFS_DIR_REC_LEN(2));
	de->rec_len = nilfs_rec_len_to_disk(rec_len);
	de->file_type = NILFS_FT_DIR;
	memcpy((void *)de->name, "..\0\0\0\0\0", 8);

	de = next_dir_entry(de);
	de->inode = cpu_to_le64(NILFS_NILFS_INO);
	de->name_len = 6;
	de->rec_len = nilfs_rec_len_to_disk(blocksize - rec_end);
	de->file_type = NILFS_FT_REG_FILE;
	memcpy((void *)de->name, ".nilfs\0", 8);

	inc_link_count(NILFS_ROOT_INO);
}

static void nilfs_mkfs_make_dot_nilfs(void)
{
	init_inode(NILFS_NILFS_INO, DT_REG, 0644, 0);
}

static void nilfs_mkfs_make_reserved_files(void)
{
	init_inode(NILFS_ATIME_INO, DT_REG, 0, 0);
	init_inode(1, DT_REG, 0, 0);
	init_inode(8, DT_REG, 0, 0);
	init_inode(9, DT_REG, 0, 0);
	init_inode(10, DT_REG, 0, 0);
}

static void *
map_segsum_info(blocknr_t start, unsigned long *offset, unsigned item_size)
{
	unsigned long block_offset = *offset / blocksize;
	unsigned long offset_in_block = *offset % blocksize;

	if (item_size > blocksize - offset_in_block) {
		offset_in_block = 0;
		*offset = ++block_offset * blocksize;
	}
	*offset += item_size;
	return map_disk_buffer(start + block_offset, 1) + offset_in_block;
}

static void update_blocknr(struct nilfs_file_info *fi,
			   unsigned long *sum_offset)
{
	blocknr_t start = nilfs.current_segment->start;
	struct nilfs_finfo *finfo;
	unsigned i;

	if (!fi->nblocks) {
		fi->raw_inode->i_bmap[0] = 0;
		return;
	}

	finfo = map_segsum_info(start, sum_offset, sizeof(struct nilfs_finfo));
	finfo->fi_ino = cpu_to_le64(fi->ino);
	finfo->fi_ndatablk = finfo->fi_nblocks = cpu_to_le32(fi->nblocks);
	finfo->fi_cno = cpu_to_le64(1);

	if (fi->ino == NILFS_DAT_INO) {
		__le64 *pblkoff;

		fi->raw_inode->i_bmap[0] = 0;
		for (i = 0; i < fi->nblocks; i++) {
			pblkoff = map_segsum_info(start, sum_offset,
						  sizeof(*pblkoff));
			*pblkoff = cpu_to_le64(i);
			fi->raw_inode->i_bmap[i + 1] =
				cpu_to_le64(fi->start + i);
		}
	} else {
		struct nilfs_binfo_v *pbinfo_v;
		blocknr_t vblocknr;

		fi->raw_inode->i_bmap[0] = 0;
		for (i = 0; i < fi->nblocks; i++) {
			pbinfo_v = map_segsum_info(start, sum_offset,
						   sizeof(*pbinfo_v));
			vblocknr = assign_vblocknr(fi->start + i);
			pbinfo_v->bi_vblocknr = cpu_to_le64(vblocknr);
			pbinfo_v->bi_blkoff = cpu_to_le64(i);
			fi->raw_inode->i_bmap[i + 1] = cpu_to_le64(vblocknr);
		}
	}
}

static void prepare_blockgrouped_file(blocknr_t blocknr)
{
	struct nilfs_palloc_group_desc *desc;
	const unsigned group_descs_per_block =
		blocksize / sizeof(struct nilfs_palloc_group_desc);
	int i;

	for (i = 0, desc = map_disk_buffer(blocknr, 1);
	     i < group_descs_per_block; i++, desc++)
		desc->pg_nfrees = cpu_to_le32(blocksize * 8 /* CHAR_BIT */);
	map_disk_buffer(blocknr + 1, 1); /* Initialize bitmap block */
}

static inline void
alloc_blockgrouped_file_entry(blocknr_t blocknr, unsigned long nr)
{
	struct nilfs_palloc_group_desc *desc = map_disk_buffer(blocknr, 1);
					/* always use the first group */
	void *bitmap = map_disk_buffer(blocknr + 1, 1);

	if (nilfs_test_bit(nr, bitmap))
		perr("Internal error: duplicated entry allocation");
	nilfs_set_bit(nr, bitmap);
	BUG_ON(desc->pg_nfrees == 0);
	desc->pg_nfrees = cpu_to_le32(le32_to_cpu(desc->pg_nfrees) - 1);
}

static void prepare_ifile(void)
{
	struct nilfs_file_info *fi = nilfs.files[NILFS_IFILE_INO];
	struct nilfs_inode *raw_inode;
	const unsigned entries_per_block =
		blocksize / sizeof(struct nilfs_inode);
	blocknr_t entry_block;
	blocknr_t blocknr = fi->start;
	int i;
	ino_t ino = 0;

	prepare_blockgrouped_file(blocknr);
	for (entry_block = blocknr + group_desc_blocks_per_group +
		     bitmap_blocks_per_group;
	     entry_block < blocknr + fi->nblocks; entry_block++) {
		raw_inode = map_disk_buffer(entry_block, 1);
		for (i = 0; i < entries_per_block; i++, raw_inode++, ino++) {
			if (ino < NILFS_MAX_INITIAL_INO && nilfs.files[ino] &&
			    !nilfs.files[ino]->raw_inode)
				nilfs.files[ino]->raw_inode = raw_inode;
#if 0 /* these fields are cleared when mapped first */
			raw_inode->i_flags = 0;
#endif
		}
	}
	/* Reserve inodes whose inode number is lower than NILFS_USER_INO */
	for (ino = 0; ino < NILFS_USER_INO; ino++)
		alloc_blockgrouped_file_entry(blocknr, ino);

	init_inode(NILFS_IFILE_INO, DT_REG, 0, 0);
}

static void reserve_ifile_inode(ino_t ino)
{
	struct nilfs_file_info *fi = nilfs.files[NILFS_IFILE_INO];

	alloc_blockgrouped_file_entry(fi->start, ino);
}

static void prepare_cpfile(void)
{
	struct nilfs_file_info *fi = nilfs.files[NILFS_CPFILE_INO];
	const unsigned entries_per_block =
		blocksize / sizeof(struct nilfs_checkpoint);
	blocknr_t blocknr = fi->start;
	blocknr_t entry_block = blocknr;
	struct nilfs_cpfile_header *header;
	struct nilfs_checkpoint *cp;
	__u64 cno = 1;
	int i;

	header = map_disk_buffer(blocknr, 1);
	header->ch_ncheckpoints = cpu_to_le64(1);
#if 0 /* these fields are cleared when mapped first */
	header->ch_nsnapshots = 0;
	header->ch_snapshot_list.ssl_next = 0;
	header->ch_snapshot_list.ssl_prev = 0;
#endif
	for (entry_block = blocknr; entry_block < blocknr + fi->nblocks;
	     entry_block++) {
		i = (entry_block == blocknr) ?
			NILFS_CPFILE_FIRST_CHECKPOINT_OFFSET : 0;
		cp = (struct nilfs_checkpoint *)map_disk_buffer(entry_block, 1)
			+ i;
		for (; i < entries_per_block; i++, cp++, cno++) {
#if 0 /* these fields are cleared when mapped first */
			cp->cp_flags = 0;
			cp->cp_checkpoints_count = 0;
			cp->cp_snapshot_list.ssl_next = 0;
			cp->cp_snapshot_list.ssl_prev = 0;
			cp->cp_inodes_count = 0;
			cp->cp_blocks_count = 0;
			cp->cp_nblk_inc = 0;
#endif
			cp->cp_cno = cpu_to_le64(cno);
			if (cno == first_cno) {
				cp->cp_create =
					cpu_to_le64(nilfs.diskinfo->ctime);
				nilfs.checkpoint = cp;
				nilfs.files[NILFS_IFILE_INO]->raw_inode =
					&cp->cp_ifile_inode;
			} else {
				nilfs_checkpoint_set_invalid(cp);
			}
		}
	}
	init_inode(NILFS_CPFILE_INO, DT_REG, 0, 0);
}

static void commit_cpfile(void)
{
	struct nilfs_checkpoint *cp = nilfs.checkpoint;

	cp->cp_inodes_count = cpu_to_le64(nr_initial_inodes);
	cp->cp_blocks_count = cpu_to_le64(nilfs.diskinfo->nblocks_used);
	cp->cp_nblk_inc = cpu_to_le64(nilfs.current_segment->nblocks);
}

static void prepare_sufile(void)
{
	struct nilfs_file_info *fi = nilfs.files[NILFS_SUFILE_INO];
	const unsigned entries_per_block =
		blocksize / sizeof(struct nilfs_segment_usage);
	blocknr_t blocknr = fi->start;
	blocknr_t entry_block = blocknr;
	struct nilfs_sufile_header *header;
	struct nilfs_segment_usage *su;
	unsigned long segnum = 0;
	int i;

	header = map_disk_buffer(blocknr, 1);
	header->sh_ncleansegs = cpu_to_le64(nilfs.diskinfo->nsegments -
					    nr_initial_segments);
	header->sh_ndirtysegs = cpu_to_le64(nr_initial_segments);
	header->sh_last_alloc = cpu_to_le64(nilfs.diskinfo->nsegments - 1);
	for (entry_block = blocknr;
	     entry_block < blocknr + fi->nblocks; entry_block++) {
		i = (entry_block == blocknr) ?
			NILFS_SUFILE_FIRST_SEGMENT_USAGE_OFFSET : 0;
		su = (struct nilfs_segment_usage *)
			map_disk_buffer(entry_block, 1) + i;
		for (; i < entries_per_block; i++, su++, segnum++) {
#if 0 /* these fields are cleared when mapped first */
			su->su_lastmod = 0;
			su->su_nblocks = 0;
			su->su_flags = 0;
#endif
			if (segnum < nr_initial_segments) {
				nilfs_segment_usage_set_active(su);
				nilfs_segment_usage_set_dirty(su);
			} else
				nilfs_segment_usage_set_clean(su);
		}
	}
	init_inode(NILFS_SUFILE_INO, DT_REG, 0, 0);
}

static void commit_sufile(void)
{
	struct nilfs_file_info *fi = nilfs.files[NILFS_SUFILE_INO];
	const unsigned entries_per_block =
		blocksize / sizeof(struct nilfs_segment_usage);
	struct nilfs_segment_usage *su;
	unsigned segnum = fi->start / nilfs.diskinfo->blocks_per_segment;
	blocknr_t blocknr = fi->start +
		(segnum + NILFS_SUFILE_FIRST_SEGMENT_USAGE_OFFSET) /
		entries_per_block;

	su = map_disk_buffer(blocknr, 1);
	su += (segnum + NILFS_SUFILE_FIRST_SEGMENT_USAGE_OFFSET) %
		entries_per_block;
	su->su_lastmod = cpu_to_le64(nilfs.diskinfo->ctime);
	su->su_nblocks = cpu_to_le32(nilfs.current_segment->nblocks);
}

static void prepare_dat(void)
{
	struct nilfs_file_info *fi = nilfs.files[NILFS_DAT_INO];
	struct nilfs_dat_entry *entry;
	const unsigned entries_per_block =
		blocksize / sizeof(struct nilfs_dat_entry);
	blocknr_t entry_block;
	int i, vblocknr = 0;
	blocknr_t blocknr = fi->start;

	prepare_blockgrouped_file(blocknr);
	for (entry_block = blocknr + group_desc_blocks_per_group +
		     bitmap_blocks_per_group;
	     entry_block < blocknr + fi->nblocks; entry_block++) {
		entry = map_disk_buffer(entry_block, 1);
		for (i = 0; i < entries_per_block; i++, entry++, vblocknr++) {
#if 0 /* dat are cleared when mapped first */
			nilfs_dat_entry_set_blocknr(dat, entry, 0);
			nilfs_dat_entry_set_start(dat, entry, 0);
			nilfs_dat_entry_set_end(dat, entry, 0);
#endif
		}
	}
	/* reserve the dat entry of vblocknr=0 */
	alloc_blockgrouped_file_entry(blocknr, 0);
	init_inode(NILFS_DAT_INO, DT_REG, 0, 0);
}

static blocknr_t assign_vblocknr(blocknr_t blocknr)
{
	struct nilfs_file_info *fi = nilfs.files[NILFS_DAT_INO];
	const unsigned entries_per_block =
		blocksize / sizeof(struct nilfs_dat_entry);
	struct nilfs_dat_entry *entry;
	blocknr_t vblocknr = nilfs.vblocknr++;
	blocknr_t entry_block = fi->start + group_desc_blocks_per_group
		+ bitmap_blocks_per_group + vblocknr / entries_per_block;

	alloc_blockgrouped_file_entry(fi->start, vblocknr);

	BUG_ON(entry_block >= fi->start + fi->nblocks);
	entry = map_disk_buffer(entry_block, 1);

	entry += vblocknr % entries_per_block;
	entry->de_blocknr = cpu_to_le64(blocknr);
	entry->de_start = cpu_to_le64(nilfs.cno);
	entry->de_end = cpu_to_le64(NILFS_CNO_MAX);

	return vblocknr;
}

static void prepare_segment(struct nilfs_segment_info *si)
{
	struct nilfs_disk_info *di = nilfs.diskinfo;
	struct nilfs_file_info *fi;
	struct nilfs_super_root *sr;
	int i;

	nilfs.current_segment = si;
	memset(&nilfs.files, 0, sizeof(nilfs.files));
	for (i = 0, fi = si->files; i < si->nfiles; i++, fi++)
		nilfs.files[fi->ino] = fi;

	/* initialize segment summary */
	nilfs.segsum = map_disk_buffer(si->start, 1);
	nilfs.segsum->ss_magic = cpu_to_le32(NILFS_SEGSUM_MAGIC);
	nilfs.segsum->ss_bytes =
		cpu_to_le16(sizeof(struct nilfs_segment_summary));
	nilfs.segsum->ss_flags =
		cpu_to_le16(NILFS_SS_LOGBGN | NILFS_SS_LOGEND | NILFS_SS_SR);
	nilfs.segsum->ss_seq = cpu_to_le64(nilfs.seq);
	nilfs.segsum->ss_create = cpu_to_le64(di->ctime);
	nilfs.segsum->ss_next = cpu_to_le64(nilfs.next);
	nilfs.segsum->ss_nblocks = cpu_to_le32(si->nblocks);
	nilfs.segsum->ss_nfinfo = cpu_to_le32(si->nfinfo);
	nilfs.segsum->ss_sumbytes = cpu_to_le32(si->sumbytes);
	nilfs.segsum->ss_cno = cpu_to_le64(nilfs.cno);

	/* initialize super root */
	nilfs.super_root = map_disk_buffer(si->start + si->nblocks - 1, 1);
	sr = nilfs.super_root;
	sr->sr_bytes = cpu_to_le16(NILFS_SR_BYTES);
	sr->sr_nongc_ctime = cpu_to_le64(di->ctime);
	sr->sr_flags = 0;

	nilfs.files[NILFS_CPFILE_INO]->raw_inode = &sr->sr_cpfile;
	nilfs.files[NILFS_SUFILE_INO]->raw_inode = &sr->sr_sufile;
	nilfs.files[NILFS_DAT_INO]->raw_inode = &sr->sr_dat;

	prepare_dat();
	prepare_sufile();
	prepare_cpfile();
	prepare_ifile();
}

static void fill_in_checksums(struct nilfs_segment_info *si, __u32 crc_seed)
{
	blocknr_t blocknr;
	unsigned long rest_blocks;
	int crc_offset;
	__u32 sum;

	/* fill in segment summary checksum */
	crc_offset = sizeof(nilfs.segsum->ss_datasum) +
		sizeof(nilfs.segsum->ss_sumsum);
	sum = nilfs_crc32(crc_seed,
			  (unsigned char *)nilfs.segsum + crc_offset,
			  si->sumbytes - crc_offset);
	nilfs.segsum->ss_sumsum = cpu_to_le32(sum);

	/* fill in super root checksum */
	crc_offset = sizeof(nilfs.super_root->sr_sum);
	sum = nilfs_crc32(crc_seed,
			  (unsigned char *)nilfs.super_root + crc_offset,
			  NILFS_SR_BYTES - crc_offset);
	nilfs.super_root->sr_sum = cpu_to_le32(sum);

	/* fill in segment checksum */
	crc_offset = sizeof(nilfs.segsum->ss_datasum);
	blocknr = si->start;
	rest_blocks = si->nblocks;
	BUG_ON(!rest_blocks);

	sum = nilfs_crc32(crc_seed, map_disk_buffer(blocknr, 1) + crc_offset,
			  blocksize - crc_offset);
	while (--rest_blocks > 0) {
		blocknr++;
		sum = nilfs_crc32(sum, map_disk_buffer(blocknr, 1), blocksize);
	}
	nilfs.segsum->ss_datasum = cpu_to_le32(sum);
}

static void commit_segment(void)
{
	struct nilfs_segment_ref *segref = &nilfs.last_segment_ref;
	struct nilfs_disk_info *di = nilfs.diskinfo;
	struct nilfs_segment_info *si = nilfs.current_segment;
	unsigned long sum_offset = sizeof(struct nilfs_segment_summary);
	struct nilfs_file_info *fi;
	int i;

	BUG_ON(!nilfs.segsum);

	/* update disk block numbers */
	for (i = 0, fi = si->files; i < si->nfiles; i++, fi++)
		update_blocknr(fi, &sum_offset);

	/* commit_ifile(); */
	commit_cpfile();
	commit_sufile();
	/* commit_dat(); */

	fill_in_checksums(si, di->crc_seed);

	segref->seq = nilfs.seq;
	segref->start = si->start;
	segref->cno = nilfs.cno;
	segref->free_blocks_count = count_free_blocks(di);
}

static void prepare_super_block(struct nilfs_disk_info *di)
{
	blocknr_t blocknr = NILFS_SB_OFFSET_BYTES / blocksize;
	unsigned long offset = NILFS_SB_OFFSET_BYTES % blocksize;

	if (sizeof(struct nilfs_super_block) > blocksize)
		perr("Internal error: too large super block");
	raw_sb = map_disk_buffer(blocknr, 1) + offset;
	memset(raw_sb, 0, sizeof(struct nilfs_super_block));

	raw_sb->s_rev_level = cpu_to_le32(NILFS_CURRENT_REV);
	raw_sb->s_minor_rev_level = cpu_to_le16(NILFS_MINOR_REV);
	raw_sb->s_magic = cpu_to_le16(NILFS_SUPER_MAGIC);

	raw_sb->s_bytes = cpu_to_le16(NILFS_SB_BYTES);
	raw_sb->s_flags = 0;
	raw_sb->s_crc_seed = cpu_to_le32(di->crc_seed);
	raw_sb->s_sum = 0;

	raw_sb->s_log_block_size = cpu_to_le32(di->blkbits - 10);
	raw_sb->s_nsegments = cpu_to_le64(di->nsegments);
	raw_sb->s_dev_size = cpu_to_le64(di->dev_size);
	raw_sb->s_first_data_block = cpu_to_le64(di->first_segment_block);
	raw_sb->s_blocks_per_segment = cpu_to_le32(di->blocks_per_segment);
	raw_sb->s_r_segments_percentage = cpu_to_le32(r_segments_percentage);

	raw_sb->s_ctime = cpu_to_le64(di->ctime);
	raw_sb->s_mtime = 0;
	raw_sb->s_mnt_count = 0;
	raw_sb->s_max_mnt_count = cpu_to_le16(NILFS_DFL_MAX_MNT_COUNT);
	raw_sb->s_state = cpu_to_le16(NILFS_VALID_FS);
	raw_sb->s_errors = cpu_to_le16(1);
	raw_sb->s_lastcheck = cpu_to_le64(di->ctime);

	raw_sb->s_checkinterval = cpu_to_le32(NILFS_DEF_CHECK_INTERVAL);
	raw_sb->s_creator_os = cpu_to_le32(NILFS_OS_LINUX);
	raw_sb->s_first_ino = cpu_to_le32(NILFS_USER_INO);

	raw_sb->s_inode_size = cpu_to_le16(sizeof(struct nilfs_inode));
	raw_sb->s_dat_entry_size = cpu_to_le16(sizeof(struct nilfs_dat_entry));
	raw_sb->s_checkpoint_size =
		cpu_to_le16(sizeof(struct nilfs_checkpoint));
	raw_sb->s_segment_usage_size =
		cpu_to_le16(sizeof(struct nilfs_segment_usage));

	raw_sb->s_feature_compat =
		cpu_to_le64(compat_array[NILFS_FEATURE_TYPE_COMPAT]);
	raw_sb->s_feature_compat_ro =
		cpu_to_le64(compat_array[NILFS_FEATURE_TYPE_COMPAT_RO]);
	raw_sb->s_feature_incompat =
		cpu_to_le64(compat_array[NILFS_FEATURE_TYPE_INCOMPAT]);

	uuid_generate(raw_sb->s_uuid);	/* set uuid using libuuid */
	memcpy(raw_sb->s_volume_name, volume_label, sizeof(volume_label));
}

static void commit_super_block(struct nilfs_disk_info *di,
			       const struct nilfs_segment_ref *segref)
{
	__u32 sbsum;

	BUG_ON(!raw_sb);

	raw_sb->s_last_cno = cpu_to_le64(segref->cno);
	raw_sb->s_last_pseg = cpu_to_le64(segref->start);
	raw_sb->s_last_seq = cpu_to_le64(segref->seq);
	raw_sb->s_free_blocks_count = cpu_to_le64(segref->free_blocks_count);

	raw_sb->s_wtime = cpu_to_le64(di->ctime);

	/* fill in crc */
	sbsum = nilfs_crc32(di->crc_seed, (unsigned char *)raw_sb,
			    NILFS_SB_BYTES);
	raw_sb->s_sum = cpu_to_le32(sbsum);
}
