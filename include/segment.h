/*
 * segment.h - NILFS segment i/o routines
 *
 * Licensed under LGPLv2: the complete text of the GNU Lesser General
 * Public License can be found in COPYING file of the nilfs-utils
 * package.
 */

#ifndef NILFS_SEGMENT_H
#define NILFS_SEGMENT_H

#include <stdint.h>	/* uint32_t, etc */
#include <linux/types.h>
#include <linux/nilfs2_ondisk.h>

#include "compat.h"
#include "util.h"

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
	uint64_t blocknr;
	uint32_t blkcnt;
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
 * @psegment: back pointer to nilfs_psegment struct
 * @finfo: file information
 * @blocknr: block number
 * @offset: byte offset from the beginning of partial segment
 * @index: index number of finfo
 * @nfinfo: number of finfo structures contained in the partial segment
 * @sumbytes: size of segment summary information
 * @sumlen: total length of the finfo including successive binfo structures
 * @error: error code
 * @use_real_blocknr: flag to indicate that blocks are not tranlated with DAT
 */
struct nilfs_file {
	const struct nilfs_psegment *psegment;
	struct nilfs_finfo *finfo;
	uint64_t blocknr;
	uint32_t offset;
	uint32_t index;
	uint32_t nfinfo;
	uint32_t sumbytes;
	size_t sumlen;
	int error;
	unsigned int use_real_blocknr : 1;
};

/* Error code of file iterator */
enum {
	NILFS_FILE_SUCCESS = 0,
	NILFS_FILE_ERROR_MANYBLKS,		/* Too many payload blocks */
	NILFS_FILE_ERROR_BLKCNT,		/* Inconsistent block count */
	NILFS_FILE_ERROR_OVERRUN,		/* finfo/binfo overrun */
	__NR_NILFS_FILE_ERROR,
};

/**
 * struct nilfs_block - block iterator
 * @file: back pointer to nilfs_file struct
 * @binfo: block information
 * @blocknr: block number
 * @offset: byte offset from the beginning of partial segment
 * @index: index number of binfo
 * @nbinfo: number of binfo structures contained in the file
 * @nbinfo_data: number of binfos for data blocks
 * @dsize: size of data block information
 * @nsize: size of node block information
 */
struct nilfs_block {
	const struct nilfs_file *file;
	void *binfo;
	uint64_t blocknr;
	uint32_t offset;
	uint32_t index;
	uint32_t nbinfo;
	uint32_t nbinfo_data;
	unsigned int dsize;
	unsigned int nsize;
};


struct nilfs;
struct nilfs_segment;

/* partial segment iterator */
void nilfs_psegment_init(struct nilfs_psegment *pseg,
			 const struct nilfs_segment *segment, uint32_t blkcnt);
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
void nilfs_file_init(struct nilfs_file *file,
		     const struct nilfs_psegment *pseg);
int nilfs_file_is_end(struct nilfs_file *file);
void nilfs_file_next(struct nilfs_file *file);
const char *nilfs_file_strerror(int errnum);

static inline int nilfs_file_use_real_blocknr(const struct nilfs_file *file)
{
	return file->use_real_blocknr;
}

#define nilfs_file_for_each(file, pseg)					\
	for (nilfs_file_init(file, pseg); !nilfs_file_is_end(file);	\
	     nilfs_file_next(file))

static inline int nilfs_file_is_error(const struct nilfs_file *file,
				      const char **errstr)
{
	if (unlikely(file->error)) {
		if (errstr != NULL)
			*errstr = nilfs_file_strerror(file->error);
		return 1;
	}
	return 0;
}

/* block iterator */
void nilfs_block_init(struct nilfs_block *blk, const struct nilfs_file *file);
int nilfs_block_is_end(const struct nilfs_block *blk);
void nilfs_block_next(struct nilfs_block *blk);

static inline int nilfs_block_is_data(const struct nilfs_block *blk)
{
	return blk->index < blk->nbinfo_data;
}

static inline int nilfs_block_is_node(const struct nilfs_block *blk)
{
	return blk->index >= blk->nbinfo_data;
}

#define nilfs_block_for_each(blk, file)					\
	for (nilfs_block_init(blk, file); !nilfs_block_is_end(blk);	\
	     nilfs_block_next(blk))


#endif /* NILFS_SEGMENT_H */
