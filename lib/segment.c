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
#include "util.h"
#include "crc32.h"

__u64 nilfs_get_segment_seqnum(const struct nilfs *nilfs, void *segment,
			       __u64 segnum)
{
	struct nilfs_segment_summary *segsum;
	unsigned long blkoff;

	blkoff = (segnum == 0) ?
		le64_to_cpu(nilfs->n_sb->s_first_data_block) : 0;

	segsum = segment + blkoff * nilfs_get_block_size(nilfs);
	return le64_to_cpu(segsum->ss_seq);
}

/* nilfs_psegment */
static int nilfs_psegment_is_valid(const struct nilfs_psegment *pseg)
{
	int offset;

	if (le32_to_cpu(pseg->p_segsum->ss_magic) != NILFS_SEGSUM_MAGIC)
		return 0;

	offset = sizeof(pseg->p_segsum->ss_datasum) +
		sizeof(pseg->p_segsum->ss_sumsum);
	return le32_to_cpu(pseg->p_segsum->ss_sumsum) ==
		crc32_le(pseg->p_seed,
			 (unsigned char *)pseg->p_segsum + offset,
			 le32_to_cpu(pseg->p_segsum->ss_sumbytes) - offset);
}

void nilfs_psegment_init(struct nilfs_psegment *pseg, __u64 segnum,
			 void *seg, size_t nblocks, const struct nilfs *nilfs)
{
	unsigned long blkoff, nblocks_per_segment;

	blkoff = (segnum == 0) ?
		le64_to_cpu(nilfs->n_sb->s_first_data_block) : 0;
	nblocks_per_segment = nilfs_get_blocks_per_segment(nilfs);

	pseg->p_blksize = nilfs_get_block_size(nilfs);
	pseg->p_nblocks = nblocks;
	pseg->p_maxblocks = nblocks_per_segment - blkoff;
	pseg->p_segblocknr = (sector_t)nblocks_per_segment * segnum + blkoff;
	pseg->p_seed = le32_to_cpu(nilfs->n_sb->s_crc_seed);

	pseg->p_segsum = seg + blkoff * pseg->p_blksize;
	pseg->p_blocknr = pseg->p_segblocknr;
}

int nilfs_psegment_is_end(const struct nilfs_psegment *pseg)
{
	return pseg->p_blocknr >= pseg->p_segblocknr + pseg->p_nblocks ||
		pseg->p_segblocknr + pseg->p_maxblocks - pseg->p_blocknr <
		NILFS_PSEG_MIN_BLOCKS ||
		!nilfs_psegment_is_valid(pseg);
}

void nilfs_psegment_next(struct nilfs_psegment *pseg)
{
	unsigned long nblocks;

	nblocks = le32_to_cpu(pseg->p_segsum->ss_nblocks);
	pseg->p_segsum = (void *)pseg->p_segsum + nblocks * pseg->p_blksize;
	pseg->p_blocknr += nblocks;
}

/* nilfs_file */
void nilfs_file_init(struct nilfs_file *file,
		     const struct nilfs_psegment *pseg)
{
	size_t blksize, rest, hdrsize;

	file->f_psegment = pseg;
	blksize = file->f_psegment->p_blksize;
	hdrsize = le16_to_cpu(pseg->p_segsum->ss_bytes);

	file->f_finfo = (void *)pseg->p_segsum + hdrsize;
	file->f_blocknr = pseg->p_blocknr +
		(le32_to_cpu(pseg->p_segsum->ss_sumbytes) + blksize - 1) /
		blksize;
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
		le32_to_cpu(file->f_psegment->p_segsum->ss_nfinfo);
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

	blksize = file->f_psegment->p_blksize;

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
	blksize = blk->b_file->f_psegment->p_blksize;

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

	blksize = blk->b_file->f_psegment->p_blksize;

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

