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

/**
 * nilfs_psegment_is_valid() - check validity of a partial segment
 * @pseg: partial segment iterator to be checked
 *
 * Checks the magic number, checksum, and size consistency of the segment
 * summary associated with @pseg.  If invalid, the error code is set in
 * @pseg->error.
 *
 * Return: %1 if valid, %0 if invalid.
 */
static int nilfs_psegment_is_valid(struct nilfs_psegment *pseg)
{
	uint32_t sumbytes, offset, sumblks, nblocks;
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

/**
 * nilfs_psegment_init() - initialize partial segment iterator
 * @pseg:    partial segment iterator to be initialized
 * @segment: segment object
 * @blkcnt:  number of valid blocks in the buffer
 *
 * Initializes @pseg to point to the first partial segment within the
 * given @segment.  @blkcnt limits the range of the segment buffer to be
 * checked.
 */
void nilfs_psegment_init(struct nilfs_psegment *pseg,
			 const struct nilfs_segment *segment, uint32_t blkcnt)
{
	pseg->segment = segment;
	pseg->segsum = segment->addr;
	pseg->blocknr = segment->blocknr;
	pseg->blkcnt = min_t(uint32_t, blkcnt, segment->nblocks);
	pseg->blkbits = segment->blkbits;
	pseg->error = NILFS_PSEGMENT_SUCCESS;
}

/**
 * nilfs_psegment_is_end() - check if partial segment iteration has finished
 * @pseg: partial segment iterator
 *
 * Return: %true if the iterator has reached the end or encountered an
 * error.  %false if iteration can continue.
 */
int nilfs_psegment_is_end(struct nilfs_psegment *pseg)
{
	return pseg->blkcnt < NILFS_PSEG_MIN_BLOCKS ||
		!nilfs_psegment_is_valid(pseg);
}

/**
 * nilfs_psegment_next() - move to the next partial segment
 * @pseg: partial segment iterator
 *
 * Advances the iterator to the next partial segment within the current
 * full segment.  Updates block counts and pointers accordingly.
 */
void nilfs_psegment_next(struct nilfs_psegment *pseg)
{
	uint32_t nblocks = le32_to_cpu(pseg->segsum->ss_nblocks);

	pseg->segsum = (void *)pseg->segsum +
		((uint64_t)nblocks << pseg->blkbits);
	pseg->blkcnt = pseg->blkcnt >= nblocks ? pseg->blkcnt - nblocks : 0;
	pseg->blocknr += nblocks;
}

/**
 * nilfs_psegment_strerror() - get error message for partial segment error
 * @errnum: error code (NILFS_PSEGMENT_ERROR_*)
 *
 * Return: Pointer to the error message string.
 */
const char *nilfs_psegment_strerror(int errnum)
{
	if (errnum < 0 || errnum >= ARRAY_SIZE(nilfs_psegment_error_strings))
		return "unknown error";

	return nilfs_psegment_error_strings[errnum];
}

/* nilfs_file */

/**
 * nilfs_finfo_use_real_blocknr() - check if real block numbers are used
 * @finfo: file information structure
 *
 * Determines if the file associated with @finfo uses real block numbers
 * (i.e., it is the DAT file) or virtual block numbers.
 *
 * Return: %true if real block numbers are used, %false otherwise.
 */
static int nilfs_finfo_use_real_blocknr(const struct nilfs_finfo *finfo)
{
	return le64_to_cpu(finfo->fi_ino) == NILFS_DAT_INO;
}

/**
 * nilfs_file_adjust_finfo_position() - adjust finfo position for block
 *                                      boundary
 * @file:    file iterator
 * @blksize: block size in bytes
 *
 * Adjusts the finfo pointer and offset in @file if the current position
 * crosses a block boundary, skipping any padding.
 */
static void nilfs_file_adjust_finfo_position(struct nilfs_file *file,
					     uint32_t blksize)
{
	uint32_t rest = blksize - (file->offset & (blksize - 1));

	if (sizeof(struct nilfs_finfo) > rest) {
		file->finfo = (void *)file->finfo + rest;
		file->offset += rest;
	}
}

/**
 * nilfs_binfo_total_size() - calculate total size of block information array
 * @offset:    current byte offset
 * @blksize:   block size in bytes
 * @binfosize: size of a single block information item
 * @blkcnt:    number of blocks
 *
 * Calculates the total size in bytes required for a binfo array of
 * @blkcnt items, accounting for block boundary padding.
 *
 * Return: Total size in bytes.
 */
static size_t nilfs_binfo_total_size(size_t offset, uint32_t blksize,
				     unsigned int binfosize, uint32_t blkcnt)
{
	uint32_t rest, binfo_per_block;

	rest = blksize - (offset & (blksize - 1));
	if (binfosize * blkcnt <= rest)
		return binfosize * blkcnt;

	blkcnt -= rest / binfosize;
	binfo_per_block = blksize / binfosize;
	return rest + (size_t)(blkcnt / binfo_per_block) * blksize +
		(blkcnt % binfo_per_block) * binfosize;
}

/**
 * nilfs_file_info_size() - calculate size of current file information block
 * @file: file iterator
 *
 * Calculates the total size of the current finfo structure and its
 * associated binfo array, including any necessary padding for block
 * boundaries.
 *
 * Return: Size in bytes.
 */
static size_t nilfs_file_info_size(struct nilfs_file *file)
{
	const uint32_t blksize = 1UL << file->psegment->blkbits;
	unsigned int dsize, nsize;
	uint32_t nblocks, ndatablk;
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

/**
 * nilfs_file_init_from_finfo() - initialize file iterator from finfo
 * @file: file iterator
 *
 * Initializes internal members of @file (@use_real_blocknr and @sumlen)
 * based on the current file information pointed to by @file->finfo.
 * Also performs a basic bounds check to ensure the finfo structure fits
 * in the remaining summary buffer.
 */
static void nilfs_file_init_from_finfo(struct nilfs_file *file)
{
	if (file->offset + sizeof(struct nilfs_finfo) > file->sumbytes) {
		/*
		 * Set a dummy value to file->sumlen so that
		 * nilfs_file_is_valid() will always fail.
		 */
		file->sumlen = sizeof(struct nilfs_finfo);
		return;
	}

	file->use_real_blocknr = nilfs_finfo_use_real_blocknr(file->finfo);
	file->sumlen = nilfs_file_info_size(file);
}

/**
 * nilfs_file_init() - initialize file iterator
 * @file: file iterator to be initialized
 * @pseg: parent partial segment iterator
 *
 * Initializes @file to point to the first file information entry within
 * the partial segment @pseg.
 */
void nilfs_file_init(struct nilfs_file *file,
		     const struct nilfs_psegment *pseg)
{
	const uint32_t blksize = 1UL << pseg->blkbits;
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
	nilfs_file_init_from_finfo(file);

	file->error = NILFS_FILE_SUCCESS;
}

/**
 * nilfs_file_is_valid() - check validity of file information
 * @file: file iterator to be checked
 *
 * Performs sanity checks on the file information, such as verifying that
 * sizes do not overrun the summary block and that block counts are
 * consistent.  If invalid, the error code is set in @file->error.
 *
 * Return: %1 if valid, %0 if invalid.
 */
static int nilfs_file_is_valid(struct nilfs_file *file)
{
	const struct nilfs_psegment *pseg = file->psegment;
	uint32_t nblocks, pseg_nblocks, ndatablk, blkoff;

	/* Sanity check for total length of finfo + binfos */
	if (unlikely(file->offset + file->sumlen > file->sumbytes)) {
		file->error = NILFS_FILE_ERROR_OVERRUN;
		goto error;
	}

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

	return 1;

error:
	errno = EINVAL;
	return 0;
}

/**
 * nilfs_file_is_end() - check if file iteration has finished
 * @file: file iterator
 *
 * Return: %true if the iterator has reached the end or encountered an
 * error.  %false if iteration can continue.
 */
int nilfs_file_is_end(struct nilfs_file *file)
{
	return file->index >= file->nfinfo || !nilfs_file_is_valid(file);
}

/**
 * nilfs_file_next() - move to the next file
 * @file: file iterator
 *
 * Advances the iterator to the next file information entry within the
 * current partial segment.
 */
void nilfs_file_next(struct nilfs_file *file)
{
	const uint32_t blksize = 1UL << file->psegment->blkbits;

	file->blocknr += le32_to_cpu(file->finfo->fi_nblocks);

	file->offset += file->sumlen;
	file->finfo = (void *)file->finfo + file->sumlen;
	nilfs_file_adjust_finfo_position(file, blksize);
	nilfs_file_init_from_finfo(file);

	file->index++;
}

/**
 * nilfs_file_strerror() - get error message for file error
 * @errnum: error code (NILFS_FILE_ERROR_*)
 *
 * Return: Pointer to the error message string.
 */
const char *nilfs_file_strerror(int errnum)
{
	if (errnum < 0 || errnum >= ARRAY_SIZE(nilfs_file_error_strings))
		return "unknown error";

	return nilfs_file_error_strings[errnum];
}

/* nilfs_block */

/**
 * nilfs_block_adjust_binfo_position() - adjust binfo position for block
 *                                       boundary
 * @blk:     block iterator
 * @blksize: block size in bytes
 *
 * Adjusts the binfo pointer and offset in @blk if the current position
 * crosses a block boundary, skipping any padding.
 */
static void nilfs_block_adjust_binfo_position(struct nilfs_block *blk,
					      uint32_t blksize)
{
	unsigned int binfosize;
	uint32_t rest;

	rest = blksize - (blk->offset & (blksize - 1));
	binfosize = nilfs_block_is_data(blk) ? blk->dsize : blk->nsize;
	if (binfosize > rest) {
		blk->binfo += rest;
		blk->offset += rest;
	}
}

/**
 * nilfs_block_init() - initialize block iterator
 * @blk:  block iterator to be initialized
 * @file: parent file iterator
 *
 * Initializes @blk to point to the first block information entry associated
 * with @file.
 */
void nilfs_block_init(struct nilfs_block *blk, const struct nilfs_file *file)
{
	const uint32_t blksize = 1UL << file->psegment->blkbits;

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

/**
 * nilfs_block_is_end() - check if block iteration has finished
 * @blk: block iterator
 *
 * Return: %true if all block information entries for the current file
 * have been processed.  %false otherwise.
 */
int nilfs_block_is_end(const struct nilfs_block *blk)
{
	return blk->index >= blk->nbinfo;
}

/**
 * nilfs_block_next() - move to the next block information
 * @blk: block iterator
 *
 * Advances the iterator to the next block information entry.  Adjusts for
 * block boundaries if necessary.
 */
void nilfs_block_next(struct nilfs_block *blk)
{
	const uint32_t blksize = 1UL << blk->file->psegment->blkbits;
	unsigned int binfosize;

	binfosize = nilfs_block_is_data(blk) ? blk->dsize : blk->nsize;
	blk->binfo += binfosize;
	blk->offset += binfosize;
	blk->blocknr++;
	blk->index++;

	nilfs_block_adjust_binfo_position(blk, blksize);
}
