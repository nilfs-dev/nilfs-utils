/*
 * segment.h - NILFS segment i/o routines
 *
 * Licensed under LGPLv2: the complete text of the GNU Lesser General
 * Public License can be found in COPYING file of the nilfs-utils
 * package.
 */

#ifndef NILFS_SEGMENT_H
#define NILFS_SEGMENT_H

#include <linux/types.h>

#include "compat.h"
#include "nilfs2_ondisk.h"

typedef __u64 sector_t;

/**
 * struct nilfs_psegment - partial segment iterator
 * @p_segnum: segment number
 * @p_blocknr: block number of partial segment
 * @p_segblocknr: block number of segment
 * @p_nblocks: number of blocks in segment
 * @p_maxblocks: maximum number of blocks in segment
 * @p_blksize: block size
 * @p_seed: CRC seed
 */
struct nilfs_psegment {
	struct nilfs_segment_summary *p_segsum;
	sector_t p_blocknr;

	sector_t p_segblocknr;
	size_t p_nblocks;
	size_t p_maxblocks;
	size_t p_blksize;
	__u32 p_seed;
};

/**
 * struct nilfs_file - file iterator
 * @f_finfo: file information
 * @f_blocknr: block number
 * @f_offset: byte offset from the beginning of segment
 * @f_index: index
 * @f_psegment: partial segment
 */
struct nilfs_file {
	struct nilfs_finfo *f_finfo;
	sector_t f_blocknr;

	unsigned long f_offset;
	int f_index;
	const struct nilfs_psegment *f_psegment;
};

/**
 * struct nilfs_block - block iterator
 * @b_binfo: block information
 * @b_blocknr: block number
 * @b_offset: byte offset from the beginning of segment
 * @b_index: index
 * @b_dsize: size of data block information
 * @b_nsize: size of node block information
 * @b_file: file
 */
struct nilfs_block {
	void *b_binfo;
	sector_t b_blocknr;

	unsigned long b_offset;
	int b_index;
	size_t b_dsize;
	size_t b_nsize;
	const struct nilfs_file *b_file;
};

/* virtual block number and block offset */
#define NILFS_BINFO_DATA_SIZE		(sizeof(__le64) + sizeof(__le64))
/* virtual block number */
#define NILFS_BINFO_NODE_SIZE		sizeof(__le64)
/* block offset */
#define NILFS_BINFO_DAT_DATA_SIZE	sizeof(__le64)
/* block offset and level */
#define NILFS_BINFO_DAT_NODE_SIZE	(sizeof(__le64) + sizeof(__le64))


struct nilfs;

/* partial segment iterator */
void nilfs_psegment_init(struct nilfs_psegment *, __u64,
			 void *, size_t, const struct nilfs *);
int nilfs_psegment_is_end(const struct nilfs_psegment *);
void nilfs_psegment_next(struct nilfs_psegment *);

#define nilfs_psegment_for_each(pseg, segnum, seg, nblocks, nilfs)	\
	for (nilfs_psegment_init(pseg, segnum, seg, nblocks, nilfs);	\
	     !nilfs_psegment_is_end(pseg);				\
	     nilfs_psegment_next(pseg))

/* file iterator */
void nilfs_file_init(struct nilfs_file *, const struct nilfs_psegment *);
int nilfs_file_is_end(const struct nilfs_file *);
void nilfs_file_next(struct nilfs_file *);

static inline int nilfs_file_is_super(const struct nilfs_file *file)
{
	__u64 ino;

	ino = le64_to_cpu(file->f_finfo->fi_ino);
	return ino == NILFS_DAT_INO;
}

#define nilfs_file_for_each(file, pseg)		\
	for (nilfs_file_init(file, pseg);	\
	     !nilfs_file_is_end(file);		\
	     nilfs_file_next(file))

/* block iterator */
void nilfs_block_init(struct nilfs_block *, const struct nilfs_file *);
int nilfs_block_is_end(const struct nilfs_block *);
void nilfs_block_next(struct nilfs_block *);

static inline int nilfs_block_is_data(const struct nilfs_block *blk)
{
	return blk->b_index < le32_to_cpu(blk->b_file->f_finfo->fi_ndatablk);
}

static inline int nilfs_block_is_node(const struct nilfs_block *blk)
{
	return blk->b_index >= le32_to_cpu(blk->b_file->f_finfo->fi_ndatablk);
}

#define nilfs_block_for_each(blk, file)		\
	for (nilfs_block_init(blk, file);	\
	     !nilfs_block_is_end(blk);		\
	     nilfs_block_next(blk))


#endif /* NILFS_SEGMENT_H */
