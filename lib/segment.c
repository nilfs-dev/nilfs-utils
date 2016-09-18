/*
 * segment.c - NILFS segment i/o library
 *
 * Licensed under LGPLv2: the complete text of the GNU Lesser General
 * Public License can be found in COPYING file of the nilfs-utils
 * package.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif	/* HAVE_CONFIG_H */

#if HAVE_STDLIB_H
#include <stdlib.h>
#endif	/* HAVE_STDLIB_H */

#if HAVE_LINUX_TYPES_H
#include <linux/types.h>
#endif	/* HAVE_LINUX_TYPES_H */

#include <errno.h>
#include "nilfs.h"
#include "segment.h"
#include "util.h"
#include "crc32.h"

/* nilfs_psegment */
static int nilfs_psegment_is_valid(struct nilfs_psegment *pseg)
{
	__u32 sumbytes, offset;
	void *limit;

	if (le32_to_cpu(pseg->segsum->ss_magic) != NILFS_SEGSUM_MAGIC)
		return 0;

	offset = offsetofend(struct nilfs_segment_summary, ss_sumsum);
	sumbytes = le32_to_cpu(pseg->segsum->ss_sumbytes);
	limit = pseg->segment->addr + pseg->segment->segsize;

	if (sumbytes < offset || (void *)pseg->segsum + sumbytes >= limit)
		return 0;

	return le32_to_cpu(pseg->segsum->ss_sumsum) ==
		crc32_le(pseg->segment->seed,
			 (unsigned char *)pseg->segsum + offset,
			 sumbytes - offset);
}

void nilfs_psegment_init(struct nilfs_psegment *pseg,
			 const struct nilfs_segment *segment, __u32 blkcnt)
{
	pseg->segment = segment;
	pseg->segsum = segment->addr;
	pseg->blocknr = segment->blocknr;
	pseg->blkcnt = min_t(__u32, blkcnt, segment->nblocks);
	pseg->blkbits = segment->blkbits;
}

int nilfs_psegment_is_end(struct nilfs_psegment *pseg)
{
	return pseg->blkcnt < NILFS_PSEG_MIN_BLOCKS ||
		!nilfs_psegment_is_valid(pseg);
}

void nilfs_psegment_next(struct nilfs_psegment *pseg)
{
	__u32 nblocks = le32_to_cpu(pseg->segsum->ss_nblocks);

	pseg->segsum = (void *)pseg->segsum + ((__u64)nblocks << pseg->blkbits);
	pseg->blkcnt = pseg->blkcnt >= nblocks ? pseg->blkcnt - nblocks : 0;
	pseg->blocknr += nblocks;
}

/* nilfs_file */
void nilfs_file_init(struct nilfs_file *file,
		     const struct nilfs_psegment *pseg)
{
	size_t blksize, rest, hdrsize;

	file->f_psegment = pseg;
	blksize = 1UL << pseg->blkbits;
	hdrsize = le16_to_cpu(pseg->segsum->ss_bytes);

	file->f_finfo = (void *)pseg->segsum + hdrsize;
	file->f_blocknr = pseg->blocknr +
		DIV_ROUND_UP(le32_to_cpu(pseg->segsum->ss_sumbytes), blksize);
	file->f_index = 0;
	file->f_offset = hdrsize;

	rest = blksize - file->f_offset % blksize;
	if (sizeof(struct nilfs_finfo) > rest) {
		file->f_finfo = (void *)file->f_finfo + rest;
		file->f_offset += rest;
	}
}

int nilfs_file_is_end(const struct nilfs_file *file)
{
	return file->f_index >=
		le32_to_cpu(file->f_psegment->segsum->ss_nfinfo);
}

static size_t nilfs_binfo_total_size(unsigned long offset,
				     size_t blksize, size_t bisize, size_t n)
{
	size_t binfo_per_block, rest = blksize - offset % blksize;

	if (bisize * n <= rest)
		return bisize * n;

	n -= rest / bisize;
	binfo_per_block = blksize / bisize;
	return rest + (n / binfo_per_block) * blksize +
		(n % binfo_per_block) * bisize;
}

void nilfs_file_next(struct nilfs_file *file)
{
	size_t blksize, rest, delta;
	size_t dsize, nsize;
	unsigned long ndatablk, nblocks;

	blksize = 1UL << file->f_psegment->blkbits;

	if (!nilfs_file_is_super(file)) {
		dsize = NILFS_BINFO_DATA_SIZE;
		nsize = NILFS_BINFO_NODE_SIZE;
	} else {
		dsize = NILFS_BINFO_DAT_DATA_SIZE;
		nsize = NILFS_BINFO_DAT_NODE_SIZE;
	}

	nblocks = le32_to_cpu(file->f_finfo->fi_nblocks);
	ndatablk = le32_to_cpu(file->f_finfo->fi_ndatablk);

	delta = sizeof(struct nilfs_finfo);
	delta += nilfs_binfo_total_size(file->f_offset + delta,
					blksize, dsize, ndatablk);
	delta += nilfs_binfo_total_size(file->f_offset + delta,
					blksize, nsize, nblocks - ndatablk);

	file->f_blocknr += nblocks;
	file->f_offset += delta;
	file->f_finfo = (void *)file->f_finfo + delta;

	rest = blksize - file->f_offset % blksize;
	if (sizeof(struct nilfs_finfo) > rest) {
		file->f_offset += rest;
		file->f_finfo = (void *)file->f_finfo + rest;
	}
	file->f_index++;
}

/* nilfs_block */
void nilfs_block_init(struct nilfs_block *blk, const struct nilfs_file *file)
{
	size_t blksize, bisize, rest;

	blk->b_file = file;
	blksize = 1UL << blk->b_file->f_psegment->blkbits;

	blk->b_binfo = (void *)(file->f_finfo + 1);
	blk->b_offset = file->f_offset + sizeof(struct nilfs_finfo);
	blk->b_blocknr = file->f_blocknr;
	blk->b_index = 0;
	if (!nilfs_file_is_super(file)) {
		blk->b_dsize = NILFS_BINFO_DATA_SIZE;
		blk->b_nsize = NILFS_BINFO_NODE_SIZE;
	} else {
		blk->b_dsize = NILFS_BINFO_DAT_DATA_SIZE;
		blk->b_nsize = NILFS_BINFO_DAT_NODE_SIZE;
	}

	bisize = nilfs_block_is_data(blk) ? blk->b_dsize : blk->b_nsize;
	rest = blksize - blk->b_offset % blksize;
	if (bisize > rest) {
		blk->b_binfo += rest;
		blk->b_offset += rest;
	}
}

int nilfs_block_is_end(const struct nilfs_block *blk)
{
	return blk->b_index >= le32_to_cpu(blk->b_file->f_finfo->fi_nblocks);
}

void nilfs_block_next(struct nilfs_block *blk)
{
	size_t blksize, bisize, rest;

	blksize = 1UL << blk->b_file->f_psegment->blkbits;

	bisize = nilfs_block_is_data(blk) ? blk->b_dsize : blk->b_nsize;
	blk->b_binfo += bisize;
	blk->b_offset += bisize;
	blk->b_index++;

	bisize = nilfs_block_is_data(blk) ? blk->b_dsize : blk->b_nsize;
	rest = blksize - blk->b_offset % blksize;
	if (bisize > rest) {
		blk->b_binfo += rest;
		blk->b_offset += rest;
	}

	blk->b_blocknr++;
}

