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
#include "util.h"

typedef __u64 sector_t;

/**
 * struct nilfs_psegment - partial segment iterator
 * @segment: pointer to segment object
 * @segsum: pointer to segment summary of the partial segment
 * @blocknr: block number of the partial segment
 * @blkcnt: count of remaining blocks
 * @blkbits: bit shift for block size
 * @error: error code
 */
struct nilfs_psegment {
	const struct nilfs_segment *segment;
	struct nilfs_segment_summary *segsum;
	sector_t blocknr;
	__u32 blkcnt;
	unsigned int blkbits;
	int error;
};

/* Error code of psegment iterator */
enum {
	NILFS_PSEGMENT_SUCCESS = 0,
	NILFS_PSEGMENT_ERROR_ALIGNMENT,		/* Bad alignment */
	NILFS_PSEGMENT_ERROR_BIGPSEG,		/* Too big partial segment */
	NILFS_PSEGMENT_ERROR_BIGHDR,		/* Too big summary header */
	NILFS_PSEGMENT_ERROR_BIGSUM,		/* Too big summary info */
	__NR_NILFS_PSEGMENT_ERROR,
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
#define NILFS_BINFO_DATA_SIZE		sizeof(struct nilfs_binfo_v)
/* virtual block number */
#define NILFS_BINFO_NODE_SIZE		sizeof(__le64)
/* block offset */
#define NILFS_BINFO_DAT_DATA_SIZE	sizeof(__le64)
/* block offset and level */
#define NILFS_BINFO_DAT_NODE_SIZE	sizeof(struct nilfs_binfo_dat)


struct nilfs;
struct nilfs_segment;

/* partial segment iterator */
void nilfs_psegment_init(struct nilfs_psegment *pseg,
			 const struct nilfs_segment *segment, __u32 blkcnt);
int nilfs_psegment_is_end(struct nilfs_psegment *pseg);
void nilfs_psegment_next(struct nilfs_psegment *pseg);
const char *nilfs_psegment_strerror(int errnum);

#define nilfs_psegment_for_each(pseg, seg, blkcnt)			\
	for (nilfs_psegment_init(pseg, seg, blkcnt);			\
	     !nilfs_psegment_is_end(pseg); nilfs_psegment_next(pseg))	\

static inline int nilfs_psegment_is_error(const struct nilfs_psegment *pseg,
					  const char **errstr)
{
	if (unlikely(pseg->error)) {
		if (errstr != NULL)
			*errstr = nilfs_psegment_strerror(pseg->error);
		return 1;
	}
	return 0;
}

/* file iterator */
void nilfs_file_init(struct nilfs_file *, const struct nilfs_psegment *);
int nilfs_file_is_end(const struct nilfs_file *);
void nilfs_file_next(struct nilfs_file *);

static inline int nilfs_file_use_real_blocknr(const struct nilfs_file *file)
{
	return le64_to_cpu(file->f_finfo->fi_ino) == NILFS_DAT_INO;
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
