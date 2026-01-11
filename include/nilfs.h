/*
 * nilfs.h - Header file of NILFS library
 *
 * Copyright (C) 2007-2012 Nippon Telegraph and Telephone Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 */

#ifndef NILFS_H
#define NILFS_H

#include <inttypes.h>	/* uint32_t, PRIu64, etc */
#include <sys/types.h>	/* off_t, size_t */
#include <linux/nilfs2_api.h>

typedef uint64_t nilfs_cno_t;

#define PRIcno		PRIu64
#define SCNcno		SCNu64

#define NILFS_FSTYPE	"nilfs2"

#define NILFS_CNO_MIN	((nilfs_cno_t)1)
#define NILFS_CNO_MAX	(~(nilfs_cno_t)0)

#define NILFS_SB_LABEL			0x0001
#define NILFS_SB_UUID			0x0002
#define NILFS_SB_FEATURES		0x0004
#define NILFS_SB_COMMIT_INTERVAL	0x4000
#define NILFS_SB_BLOCK_MAX		0x8000

struct nilfs_super_block;
struct nilfs;

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
/*00h*/	uint32_t rev_level;
	uint16_t minor_rev_level;
	uint16_t flags;
	uint32_t blocksize_bits;
	uint32_t blocksize;
/*10h*/	uint64_t devsize;
	uint32_t crc_seed;
	uint32_t pad;
/*20h*/	uint64_t nsegments;
	uint32_t blocks_per_segment;
	uint32_t reserved_segments_ratio;
/*30h*/	uint64_t first_segment_blkoff;
	uint64_t feature_compat;
/*40h*/	uint64_t feature_compat_ro;
	uint64_t feature_incompat;
};

#define NILFS_OPEN_RAW		0x0001	/* Open RAW device */
#define NILFS_OPEN_RDONLY	0x0002	/* Open NILFS API in read only mode */
#define NILFS_OPEN_WRONLY	0x0004	/* Open NILFS API in write only mode */
#define NILFS_OPEN_RDWR		0x0008	/* Open NILFS API in read/write mode */
#define NILFS_OPEN_GCLK		0x1000	/* Open GC lock primitive */


struct nilfs *nilfs_open(const char *dev, const char *dir, int flags);
void nilfs_close(struct nilfs *nilfs);

const char *nilfs_get_dev(const struct nilfs *nilfs);
const char *nilfs_get_root_path(const struct nilfs *nilfs);
int nilfs_get_root_fd(const struct nilfs *nilfs);

int nilfs_opt_test(const struct nilfs *nilfs, unsigned int index);
int nilfs_opt_set(struct nilfs *nilfs, unsigned int index);
int nilfs_opt_clear(struct nilfs *nilfs, unsigned int index);

#define NILFS_OPT_FNS(name, index)					\
static inline int nilfs_opt_test_##name(const struct nilfs *nilfs)	\
{									\
	return nilfs_opt_test(nilfs, index);				\
}									\
static inline int nilfs_opt_set_##name(struct nilfs *nilfs)		\
{									\
	return nilfs_opt_set(nilfs, index);				\
}									\
static inline int nilfs_opt_clear_##name(struct nilfs *nilfs)		\
{									\
	return nilfs_opt_clear(nilfs, index);				\
}

NILFS_OPT_FNS(mmap, 0)
NILFS_OPT_FNS(set_suinfo, 1)

nilfs_cno_t nilfs_get_oldest_cno(struct nilfs *nilfs);

ssize_t nilfs_get_layout(const struct nilfs *nilfs,
			 struct nilfs_layout *layout, size_t layout_size);

int nilfs_lock(struct nilfs *nilfs, unsigned int index);
int nilfs_trylock(struct nilfs *nilfs, unsigned int index);
int nilfs_unlock(struct nilfs *nilfs, unsigned int index);

#define NILFS_LOCK_FNS(name, index)					\
static inline int nilfs_lock_##name(struct nilfs *nilfs)		\
{									\
	return nilfs_lock(nilfs, index);				\
}									\
static inline int nilfs_trylock_##name(struct nilfs *nilfs)		\
{									\
	return nilfs_trylock(nilfs, index);				\
}									\
static inline int nilfs_unlock_##name(struct nilfs *nilfs)		\
{									\
	return nilfs_unlock(nilfs, index);				\
}

NILFS_LOCK_FNS(cleaner, 0)


struct nilfs_super_block *nilfs_sb_read(int devfd);
int nilfs_sb_write(int devfd, struct nilfs_super_block *sbp, int mask);

/**
 * struct nilfs_segment - segment object
 * @addr: start address of segment on mapped or allocated region
 * @segsize: segment size
 * @segnum: segment number
 * @seqnum: sequence number of the segment
 * @blocknr: start block number
 * @nblocks: number of blocks in the segment
 * @blocks_per_segment: number of blocks per segment
 * @blkbits: bit shift for block size
 * @seed: crc seed
 * @mmapped: flag to indicate that @addr is mapped with mmap()
 * @adjusted: flag to indicate that @addr is adjusted to page boundary
 */
struct nilfs_segment {
	void *addr;
	uint64_t segsize;
	uint64_t segnum;
	uint64_t seqnum;
	uint64_t blocknr;
	uint32_t nblocks;
	uint32_t blocks_per_segment;
	uint32_t blkbits;
	uint32_t seed;
	unsigned int mmapped : 1;
	unsigned int adjusted : 1;
};

int nilfs_get_segment(struct nilfs *nilfs, uint64_t segnum,
		      struct nilfs_segment *segment);
int nilfs_put_segment(struct nilfs_segment *segment);
int nilfs_get_segment_seqnum(const struct nilfs *nilfs, uint64_t segnum,
			     uint64_t *seqnum);

size_t nilfs_get_block_size(const struct nilfs *nilfs);
uint64_t nilfs_get_nsegments(const struct nilfs *nilfs);
uint32_t nilfs_get_blocks_per_segment(const struct nilfs *nilfs);
uint32_t nilfs_get_reserved_segments_ratio(const struct nilfs *nilfs);

int nilfs_change_cpmode(struct nilfs *nilfs, nilfs_cno_t cno, int mode);
ssize_t nilfs_get_cpinfo(struct nilfs *nilfs, nilfs_cno_t cno, int mode,
			 struct nilfs_cpinfo *cpinfo, size_t nci);
int nilfs_delete_checkpoint(struct nilfs *nilfs, nilfs_cno_t cno);
int nilfs_get_cpstat(const struct nilfs *nilfs, struct nilfs_cpstat *cpstat);
ssize_t nilfs_get_suinfo(const struct nilfs *nilfs, uint64_t segnum,
			 struct nilfs_suinfo *suinfo, size_t nsi);
int nilfs_set_suinfo(const struct nilfs *nilfs,
		     struct nilfs_suinfo_update *sup, size_t nsup);
int nilfs_get_sustat(const struct nilfs *nilfs, struct nilfs_sustat *sustat);
ssize_t nilfs_get_vinfo(const struct nilfs *nilfs, struct nilfs_vinfo *vinfo,
			size_t nvi);
ssize_t nilfs_get_bdescs(const struct nilfs *nilfs, struct nilfs_bdesc *bdescs,
			 size_t nbdescs);
int nilfs_clean_segments(struct nilfs *nilfs,
			 struct nilfs_vdesc *vdescs, size_t nvdescs,
			 struct nilfs_period *periods, size_t nperiods,
			 uint64_t *vblocknrs, size_t nvblocknrs,
			 struct nilfs_bdesc *bdescs, size_t nbdescs,
			 uint64_t *segnums, size_t nsegs);
int nilfs_sync(const struct nilfs *nilfs, nilfs_cno_t *cnop);
int nilfs_resize(struct nilfs *nilfs, off_t size);
int nilfs_set_alloc_range(struct nilfs *nilfs, off_t start, off_t end);
int nilfs_freeze(struct nilfs *nilfs);
int nilfs_thaw(struct nilfs *nilfs);

#endif	/* NILFS_H */
