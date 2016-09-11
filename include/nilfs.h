/*
 * nilfs.h - NILFS header file.
 *
 * Copyright (C) 2007-2012 Nippon Telegraph and Telephone Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * Written by Koji Sato.
 *
 * Maintained by Ryusuke Konishi <konishi.ryusuke@lab.ntt.co.jp> from 2008.
 */

#ifndef NILFS_H
#define NILFS_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/ioctl.h>
#include <semaphore.h>

#include "nilfs2_api.h"

typedef __u64 nilfs_cno_t;

#define NILFS_FSTYPE	"nilfs2"

#define NILFS_CNO_MIN	((nilfs_cno_t)1)
#define NILFS_CNO_MAX	(~(nilfs_cno_t)0)

#define NILFS_SB_LABEL			0x0001
#define NILFS_SB_UUID			0x0002
#define NILFS_SB_FEATURES		0x0004
#define NILFS_SB_COMMIT_INTERVAL	0x4000
#define NILFS_SB_BLOCK_MAX		0x8000

struct nilfs_super_block;

/**
 * struct nilfs_layout - layout information of nilfs
 * @rev_level: revison level
 * @minor_rev_level: minor revision level
 * @flags: flags (not used at present)
 * @blocksize_bits: bit shift of block size
 * @blocksize: block size
 * @devsize: device size that the file system records
 * @crc_seed: seed of crc
 * @pad: padding (reserved)
 * @nsegments: number of segments
 * @blocks_per_segment: number of blocks per segment
 * @reserved_segments_ratio: ratio of reserved segements in percent
 * @first_segment_blkoff: block offset of the first segment
 * @feature_compat: compatible feature set
 * @feature_compat_ro: read-only compat feature set
 * @feature_incompat: incompatible feature set
 */
struct nilfs_layout {
/*00h*/	__u32 rev_level;
	__u16 minor_rev_level;
	__u16 flags;
	__u32 blocksize_bits;
	__u32 blocksize;
/*10h*/	__u64 devsize;
	__u32 crc_seed;
	__u32 pad;
/*20h*/	__u64 nsegments;
	__u32 blocks_per_segment;
	__u32 reserved_segments_ratio;
/*30h*/	__u64 first_segment_blkoff;
	__u64 feature_compat;
/*40h*/	__u64 feature_compat_ro;
	__u64 feature_incompat;
};

/**
 * struct nilfs - nilfs object
 * @n_sb: superblock
 * @n_dev: device file
 * @n_ioc: ioctl file
 * @n_devfd: file descriptor of device file
 * @n_iocfd: file descriptor of ioctl file
 * @n_opts: options
 * @n_mincno: the minimum of valid checkpoint numbers
 * @n_sems: array of semaphores
 *     sems[0] protects garbage collection process
 */
struct nilfs {
	struct nilfs_super_block *n_sb;
	char *n_dev;
	/* char *n_mnt; */
	char *n_ioc;
	int n_devfd;
	int n_iocfd;
	int n_opts;
	nilfs_cno_t n_mincno;
	sem_t *n_sems[1];
};

#define NILFS_OPEN_RAW		0x0001	/* Open RAW device */
#define NILFS_OPEN_RDONLY	0x0002	/* Open NILFS API in read only mode */
#define NILFS_OPEN_WRONLY	0x0004	/* Open NILFS API in write only mode */
#define NILFS_OPEN_RDWR		0x0008	/* Open NILFS API in read/write mode */
#define NILFS_OPEN_GCLK		0x1000	/* Open GC lock primitive */

#define NILFS_OPT_MMAP		0x01
#define NILFS_OPT_SET_SUINFO	0x02


struct nilfs *nilfs_open(const char *, const char *, int);
void nilfs_close(struct nilfs *);

const char *nilfs_get_dev(const struct nilfs *);

void nilfs_opt_clear_mmap(struct nilfs *);
int nilfs_opt_set_mmap(struct nilfs *);
int nilfs_opt_test_mmap(struct nilfs *);

void nilfs_opt_clear_set_suinfo(struct nilfs *);
int nilfs_opt_set_set_suinfo(struct nilfs *);
int nilfs_opt_test_set_suinfo(struct nilfs *);

nilfs_cno_t nilfs_get_oldest_cno(struct nilfs *);

ssize_t nilfs_get_layout(const struct nilfs *nilfs,
			 struct nilfs_layout *layout, size_t layout_size);


#define NILFS_LOCK_FNS(name, index)					\
static inline int nilfs_lock_##name(struct nilfs *nilfs)		\
{									\
	return sem_wait(nilfs->n_sems[index]);				\
}									\
static inline int nilfs_trylock_##name(struct nilfs *nilfs)		\
{									\
	return sem_trywait(nilfs->n_sems[index]);			\
}									\
static inline int nilfs_unlock_##name(struct nilfs *nilfs)		\
{									\
	return sem_post(nilfs->n_sems[index]);				\
}

NILFS_LOCK_FNS(cleaner, 0)


struct nilfs_super_block *nilfs_sb_read(int devfd);
int nilfs_sb_write(int devfd, struct nilfs_super_block *sbp, int mask);

ssize_t nilfs_get_segment(struct nilfs *, unsigned long, void **);
int nilfs_put_segment(struct nilfs *, void *);
size_t nilfs_get_block_size(const struct nilfs *nilfs);
__u64 nilfs_get_nsegments(const struct nilfs *nilfs);
__u32 nilfs_get_blocks_per_segment(const struct nilfs *nilfs);
__u32 nilfs_get_reserved_segments_ratio(const struct nilfs *nilfs);
__u64 nilfs_get_segment_seqnum(const struct nilfs *, void *, __u64);

int nilfs_change_cpmode(struct nilfs *, nilfs_cno_t, int);
ssize_t nilfs_get_cpinfo(struct nilfs *, nilfs_cno_t, int,
			 struct nilfs_cpinfo *, size_t);
int nilfs_delete_checkpoint(struct nilfs *, nilfs_cno_t);
int nilfs_get_cpstat(const struct nilfs *, struct nilfs_cpstat *);
ssize_t nilfs_get_suinfo(const struct nilfs *, __u64, struct nilfs_suinfo *,
			 size_t);
int nilfs_set_suinfo(const struct nilfs *, struct nilfs_suinfo_update *,
		     size_t);
int nilfs_get_sustat(const struct nilfs *, struct nilfs_sustat *);
ssize_t nilfs_get_vinfo(const struct nilfs *, struct nilfs_vinfo *, size_t);
ssize_t nilfs_get_bdescs(const struct nilfs *, struct nilfs_bdesc *, size_t);
int nilfs_clean_segments(struct nilfs *, struct nilfs_vdesc *, size_t,
			 struct nilfs_period *, size_t, __u64 *, size_t,
			 struct nilfs_bdesc *, size_t, __u64 *, size_t);
int nilfs_sync(const struct nilfs *, nilfs_cno_t *);
int nilfs_resize(struct nilfs *nilfs, off_t size);
int nilfs_set_alloc_range(struct nilfs *nilfs, off_t start, off_t end);
int nilfs_freeze(struct nilfs *nilfs);
int nilfs_thaw(struct nilfs *nilfs);

#endif	/* NILFS_H */
