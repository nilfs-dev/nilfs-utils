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

/* virtual block number and block offset */
#define NILFS_BINFO_DATA_SIZE		sizeof(struct nilfs_binfo_v)
/* virtual block number */
#define NILFS_BINFO_NODE_SIZE		sizeof(__le64)
/* block offset */
#define NILFS_BINFO_DAT_DATA_SIZE	sizeof(__le64)
/* block offset and level */
#define NILFS_BINFO_DAT_NODE_SIZE	sizeof(struct nilfs_binfo_dat)


static const char *nilfs_psegment_error_strings[] = {
	"success",
	"bad alignment",
	"too big partial segment",
	"too big summary header",
	"too big summary info",
};

static const char *nilfs_file_error_strings[] = {
	"success",
	"too many payload blocks",
	"inconsistent block count",
	"finfo/binfo overrun",
};

/* nilfs_psegment */
static int nilfs_psegment_is_valid(struct nilfs_psegment *pseg)
{
	__u32 sumbytes, offset, sumblks, nblocks;
	unsigned int hdrsize;
	void *limit;

	if (le32_to_cpu(pseg->segsum->ss_magic) != NILFS_SEGSUM_MAGIC)
		return 0;

	offset = offsetofend(struct nilfs_segment_summary, ss_sumsum);
	sumbytes = le32_to_cpu(pseg->segsum->ss_sumbytes);
	limit = pseg->segment->addr + pseg->segment->segsize;

	if (sumbytes < offset || (void *)pseg->segsum + sumbytes >= limit)
		return 0;

	if (le32_to_cpu(pseg->segsum->ss_sumsum) !=
	    crc32_le(pseg->segment->seed,
		     (unsigned char *)pseg->segsum + offset,
		     sumbytes - offset))
		return 0;

	/* Sanity check to prevent memory access errors */
	hdrsize = le16_to_cpu(pseg->segsum->ss_bytes);
	if (unlikely(!IS_ALIGNED(hdrsize, 8))) {
		pseg->error = NILFS_PSEGMENT_ERROR_ALIGNMENT;
		goto error;
	}

	/* Sanity check on partial segment size */
	nblocks = le32_to_cpu(pseg->segsum->ss_nblocks);
	if (unlikely(pseg->blocknr + nblocks >
		     pseg->segment->blocknr + pseg->segment->nblocks)) {
		pseg->error = NILFS_PSEGMENT_ERROR_BIGPSEG;
		goto error;
	}

	/* Sanity check on summary blocks */
	if (unlikely(hdrsize > sumbytes)) {
		pseg->error = NILFS_PSEGMENT_ERROR_BIGHDR;
		goto error;
	}

	sumblks = (sumbytes + (1UL << pseg->blkbits) - 1) >> pseg->blkbits;
	if (unlikely(sumblks >= nblocks)) {
		pseg->error = NILFS_PSEGMENT_ERROR_BIGSUM;
		goto error;
	}

	return 1;

error:
	errno = EINVAL;
	return 0;
}

void nilfs_psegment_init(struct nilfs_psegment *pseg,
			 const struct nilfs_segment *segment, __u32 blkcnt)
{
	pseg->segment = segment;
	pseg->segsum = segment->addr;
	pseg->blocknr = segment->blocknr;
	pseg->blkcnt = min_t(__u32, blkcnt, segment->nblocks);
	pseg->blkbits = segment->blkbits;
	pseg->error = NILFS_PSEGMENT_SUCCESS;
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

const char *nilfs_psegment_strerror(int errnum)
{
	if (errnum < 0 || errnum >= ARRAY_SIZE(nilfs_psegment_error_strings))
		return "unknown error";

	return nilfs_psegment_error_strings[errnum];
}

/* nilfs_file */
static int nilfs_finfo_use_real_blocknr(const struct nilfs_finfo *finfo)
{
	return le64_to_cpu(finfo->fi_ino) == NILFS_DAT_INO;
}

static void nilfs_file_adjust_finfo_position(struct nilfs_file *file,
					     __u32 blksize)
{
	__u32 rest = blksize - (file->offset & (blksize - 1));

	if (sizeof(struct nilfs_finfo) > rest) {
		file->finfo = (void *)file->finfo + rest;
		file->offset += rest;
	}
}

static size_t nilfs_binfo_total_size(size_t offset, __u32 blksize,
				     unsigned int binfosize, __u32 blkcnt)
{
	__u32 rest, binfo_per_block;

	rest = blksize - (offset & (blksize - 1));
	if (binfosize * blkcnt <= rest)
		return binfosize * blkcnt;

	blkcnt -= rest / binfosize;
	binfo_per_block = blksize / binfosize;
	return rest + (size_t)(blkcnt / binfo_per_block) * blksize +
		(blkcnt % binfo_per_block) * binfosize;
}

static size_t nilfs_file_info_size(struct nilfs_file *file)
{
	const __u32 blksize = 1UL << file->psegment->blkbits;
	unsigned int dsize, nsize;
	__u32 nblocks, ndatablk;
	size_t delta;

	if (file->use_real_blocknr) {
		dsize = NILFS_BINFO_DAT_DATA_SIZE;
		nsize = NILFS_BINFO_DAT_NODE_SIZE;
	} else {
		dsize = NILFS_BINFO_DATA_SIZE;
		nsize = NILFS_BINFO_NODE_SIZE;
	}

	nblocks = le32_to_cpu(file->finfo->fi_nblocks);
	ndatablk = le32_to_cpu(file->finfo->fi_ndatablk);
	delta = sizeof(struct nilfs_finfo);
	delta += nilfs_binfo_total_size(file->offset + delta, blksize, dsize,
					ndatablk);
	delta += nilfs_binfo_total_size(file->offset + delta, blksize, nsize,
					nblocks - ndatablk);
	return delta;
}

void nilfs_file_init(struct nilfs_file *file,
		     const struct nilfs_psegment *pseg)
{
	const __u32 blksize = 1UL << pseg->blkbits;
	unsigned int hdrsize;

	file->psegment = pseg;
	file->nfinfo = le32_to_cpu(pseg->segsum->ss_nfinfo);
	file->sumbytes = le32_to_cpu(pseg->segsum->ss_sumbytes);

	file->blocknr = pseg->blocknr +
		((file->sumbytes + blksize - 1) >> pseg->blkbits);
	file->index = 0;

	hdrsize = le16_to_cpu(pseg->segsum->ss_bytes);
	file->offset = hdrsize;
	file->finfo = (void *)pseg->segsum + hdrsize;
	nilfs_file_adjust_finfo_position(file, blksize);

	file->use_real_blocknr = nilfs_finfo_use_real_blocknr(file->finfo);
	file->sumlen = nilfs_file_info_size(file);
	file->error = NILFS_FILE_SUCCESS;
}

static int nilfs_file_is_valid(struct nilfs_file *file)
{
	const struct nilfs_psegment *pseg = file->psegment;
	__u32 nblocks, pseg_nblocks, ndatablk, blkoff;

	/* Sanity check for payload block count */
	nblocks = le32_to_cpu(file->finfo->fi_nblocks);
	blkoff = file->blocknr - pseg->blocknr;
	pseg_nblocks = le32_to_cpu(pseg->segsum->ss_nblocks);
	if (unlikely(blkoff + nblocks > pseg_nblocks)) {
		file->error = NILFS_FILE_ERROR_MANYBLKS;
		goto error;
	}

	/* Do sanity check on finfo */
	ndatablk = le32_to_cpu(file->finfo->fi_ndatablk);
	if (unlikely(ndatablk > nblocks)) {
		file->error = NILFS_FILE_ERROR_BLKCNT;
		goto error;
	}

	/* Sanity check for total length of finfo + binfos */
	if (unlikely(file->offset + file->sumlen > file->sumbytes)) {
		file->error = NILFS_FILE_ERROR_OVERRUN;
		goto error;
	}

	return 1;

error:
	errno = EINVAL;
	return 0;
}

int nilfs_file_is_end(struct nilfs_file *file)
{
	return file->index >= file->nfinfo || !nilfs_file_is_valid(file);
}

void nilfs_file_next(struct nilfs_file *file)
{
	const __u32 blksize = 1UL << file->psegment->blkbits;

	file->blocknr += le32_to_cpu(file->finfo->fi_nblocks);

	file->offset += file->sumlen;
	file->finfo = (void *)file->finfo + file->sumlen;
	nilfs_file_adjust_finfo_position(file, blksize);

	file->use_real_blocknr = nilfs_finfo_use_real_blocknr(file->finfo);
	file->sumlen = nilfs_file_info_size(file);
	file->index++;
}

const char *nilfs_file_strerror(int errnum)
{
	if (errnum < 0 || errnum >= ARRAY_SIZE(nilfs_file_error_strings))
		return "unknown error";

	return nilfs_file_error_strings[errnum];
}

/* nilfs_block */
static void nilfs_block_adjust_binfo_position(struct nilfs_block *blk,
					      __u32 blksize)
{
	unsigned int binfosize;
	__u32 rest;

	rest = blksize - (blk->offset & (blksize - 1));
	binfosize = nilfs_block_is_data(blk) ? blk->dsize : blk->nsize;
	if (binfosize > rest) {
		blk->binfo += rest;
		blk->offset += rest;
	}
}

void nilfs_block_init(struct nilfs_block *blk, const struct nilfs_file *file)
{
	const __u32 blksize = 1UL << file->psegment->blkbits;

	blk->file = file;
	blk->binfo = (void *)file->finfo + sizeof(struct nilfs_finfo);
	blk->offset = file->offset + sizeof(struct nilfs_finfo);
	blk->blocknr = file->blocknr;
	blk->index = 0;
	blk->nbinfo = le32_to_cpu(file->finfo->fi_nblocks);
	blk->nbinfo_data = le32_to_cpu(file->finfo->fi_ndatablk);

	if (file->use_real_blocknr) {
		blk->dsize = NILFS_BINFO_DAT_DATA_SIZE;
		blk->nsize = NILFS_BINFO_DAT_NODE_SIZE;
	} else {
		blk->dsize = NILFS_BINFO_DATA_SIZE;
		blk->nsize = NILFS_BINFO_NODE_SIZE;
	}

	nilfs_block_adjust_binfo_position(blk, blksize);
}

int nilfs_block_is_end(const struct nilfs_block *blk)
{
	return blk->index >= blk->nbinfo;
}

void nilfs_block_next(struct nilfs_block *blk)
{
	const __u32 blksize = 1UL << blk->file->psegment->blkbits;
	unsigned int binfosize;

	binfosize = nilfs_block_is_data(blk) ? blk->dsize : blk->nsize;
	blk->binfo += binfosize;
	blk->offset += binfosize;
	blk->blocknr++;
	blk->index++;

	nilfs_block_adjust_binfo_position(blk, blksize);
}
