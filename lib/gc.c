/*
 * gc.c - nilfs garbage collection library
 *
 * Licensed under LGPLv2: the complete text of the GNU Lesser General
 * Public License can be found in COPYING file of the nilfs-utils
 * package.
 *
 * Copyright (C) 2008-2012 Nippon Telegraph and Telephone Corporation.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif	/* HAVE_CONFIG_H */

#include <stdio.h>

#if HAVE_STDLIB_H
#include <stdlib.h>
#endif	/* HAVE_STDLIB_H */

#if HAVE_STRING_H
#include <string.h>	/* memmove */
#endif	/* HAVE_STRING_H */

#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif	/* HAVE_SYS_TYPES_H */

#if HAVE_SYSLOG_H
#include <syslog.h>
#endif	/* HAVE_SYSLOG_H */

#if HAVE_SYS_TIME_H
#include <sys/time.h>
#endif	/* HAVE_SYS_TIME */

#include <errno.h>
#include <assert.h>
#include <stdarg.h>
#include <signal.h>
#include "util.h"
#include "vector.h"
#include "nilfs_gc.h"

#define NILFS_GC_NBDESCS	512
#define NILFS_GC_NVINFO	512
#define NILFS_GC_NCPINFO	512


static void default_logger(int priority, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	fprintf(stderr, "\n");
	va_end(args);
}

void (*nilfs_gc_logger)(int priority, const char *fmt, ...) = default_logger;


static int nilfs_comp_vdesc_blocknr(const void *elem1, const void *elem2)
{
	const struct nilfs_vdesc *vdesc1 = elem1, *vdesc2 = elem2;

	return (vdesc1->vd_blocknr < vdesc2->vd_blocknr) ? -1 : 1;
}

static int nilfs_comp_vdesc_vblocknr(const void *elem1, const void *elem2)
{
	const struct nilfs_vdesc *vdesc1 = elem1, *vdesc2 = elem2;

	return (vdesc1->vd_vblocknr < vdesc2->vd_vblocknr) ? -1 : 1;
}

static int nilfs_comp_period(const void *elem1, const void *elem2)
{
	const struct nilfs_period *period1 = elem1, *period2 = elem2;

	return (period1->p_start < period2->p_start) ? -1 :
		(period1->p_start == period2->p_start) ? 0 : 1;
}

static int nilfs_comp_bdesc(const void *elem1, const void *elem2)
{
	const struct nilfs_bdesc *bdesc1 = elem1, *bdesc2 = elem2;

	if (bdesc1->bd_ino < bdesc2->bd_ino)
		return -1;
	else if (bdesc1->bd_ino > bdesc2->bd_ino)
		return 1;

	if (bdesc1->bd_level < bdesc2->bd_level)
		return -1;
	else if (bdesc1->bd_level > bdesc2->bd_level)
		return 1;

	if (bdesc1->bd_offset < bdesc2->bd_offset)
		return -1;
	else if (bdesc1->bd_offset > bdesc2->bd_offset)
		return 1;
	else
		return 0;
}

/**
 * nilfs_acc_blocks_file - collect summary of blocks in a file
 * @file: file object
 * @vdescv: vector object to store (descriptors of) virtual block numbers
 * @bdescv: vector object to store (descriptors of) disk block numbers
 */
static int nilfs_acc_blocks_file(struct nilfs_file *file,
				 struct nilfs_vector *vdescv,
				 struct nilfs_vector *bdescv)
{
	struct nilfs_block blk;
	struct nilfs_vdesc *vdesc;
	struct nilfs_bdesc *bdesc;
	union nilfs_binfo *binfo;
	ino_t ino;
	nilfs_cno_t cno;

	ino = le64_to_cpu(file->f_finfo->fi_ino);
	if (nilfs_file_is_super(file)) {
		nilfs_block_for_each(&blk, file) {
			bdesc = nilfs_vector_get_new_element(bdescv);
			if (bdesc == NULL)
				return -1;
			bdesc->bd_ino = ino;
			bdesc->bd_oblocknr = blk.b_blocknr;
			if (nilfs_block_is_data(&blk)) {
				bdesc->bd_offset =
					le64_to_cpu(*(__le64 *)blk.b_binfo);
				bdesc->bd_level = 0;
			} else {
				binfo = blk.b_binfo;
				bdesc->bd_offset =
					le64_to_cpu(binfo->bi_dat.bi_blkoff);
				bdesc->bd_level = binfo->bi_dat.bi_level;
			}
		}
	} else {
		cno = le64_to_cpu(file->f_finfo->fi_cno);
		nilfs_block_for_each(&blk, file) {
			vdesc = nilfs_vector_get_new_element(vdescv);
			if (vdesc == NULL)
				return -1;
			vdesc->vd_ino = ino;
			vdesc->vd_cno = cno;
			vdesc->vd_blocknr = blk.b_blocknr;
			if (nilfs_block_is_data(&blk)) {
				binfo = blk.b_binfo;
				vdesc->vd_vblocknr =
					le64_to_cpu(binfo->bi_v.bi_vblocknr);
				vdesc->vd_offset =
					le64_to_cpu(binfo->bi_v.bi_blkoff);
				vdesc->vd_flags = 0;	/* data */
			} else {
				vdesc->vd_vblocknr =
					le64_to_cpu(*(__le64 *)blk.b_binfo);
				vdesc->vd_flags = 1;	/* node */
			}
		}
	}
	return 0;
}

/**
 * nilfs_acc_blocks_psegment - collect summary of blocks in a log
 * @psegment: partial segment object
 * @vdescv: vector object to store (descriptors of) virtual block numbers
 * @bdescv: vector object to store (descriptors of) disk block numbers
 */
static int nilfs_acc_blocks_psegment(struct nilfs_psegment *psegment,
				     struct nilfs_vector *vdescv,
				     struct nilfs_vector *bdescv)
{
	struct nilfs_file file;

	nilfs_file_for_each(&file, psegment) {
		if (nilfs_acc_blocks_file(&file, vdescv, bdescv) < 0)
			return -1;
	}
	return 0;
}

/**
 * nilfs_acc_blocks_segment - collect summary of blocks in a segment
 * @nilfs: nilfs object
 * @segnum: segment number to be parsed
 * @segment: start address of segment data
 * @nblocks: size of valid logs in the segment (per block)
 * @vdescv: vector object to store (descriptors of) virtual block numbers
 * @bdescv: vector object to store (descriptors of) disk block numbers
 */
static int nilfs_acc_blocks_segment(struct nilfs *nilfs,
				    __u64 segnum, void *segment,
				    size_t nblocks,
				    struct nilfs_vector *vdescv,
				    struct nilfs_vector *bdescv)
{
	struct nilfs_psegment psegment;

	nilfs_psegment_for_each(&psegment, segnum, segment, nblocks, nilfs) {
		if (nilfs_acc_blocks_psegment(&psegment, vdescv, bdescv) < 0)
			return -1;
	}
	return 0;
}

/**
 * nilfs_deselect_segment - deselect a segment
 * @segnums: array of selected segments
 * @nsegs: size of @segnums array
 * @nr: index number for @segnums array to be deselected
 */
static ssize_t nilfs_deselect_segment(__u64 *segnums, size_t nsegs, int nr)
{
	if (nr >= nsegs || nsegs == 0)
		return -1;
	else if (nr < nsegs - 1) {
		__u64 tn = segnums[nr];

		memmove(&segnums[nr], &segnums[nr + 1],
			sizeof(__u64) * (nsegs - 1 - nr));
		segnums[nsegs - 1] = tn;
	}
	return nsegs - 1;
}

/**
 * nilfs_acc_blocks - collect summary of blocks contained in segments
 * @nilfs: nilfs object
 * @segnums: array of selected segments
 * @nsegs: size of @segnums array
 * @protseq: start of sequence number of protected segments
 * @vdescv: vector object to store (descriptors of) virtual block numbers
 * @bdescv: vector object to store (descriptors of) disk block numbers
 */
static ssize_t nilfs_acc_blocks(struct nilfs *nilfs,
				__u64 *segnums, size_t nsegs, __u64 protseq,
				struct nilfs_vector *vdescv,
				struct nilfs_vector *bdescv)
{
	struct nilfs_suinfo si;
	void *segment;
	int ret, i = 0;
	ssize_t n = nsegs;
	__u64 segseq;

	while (i < n) {
		if (nilfs_get_suinfo(nilfs, segnums[i], &si, 1) < 0)
			return -1;

		if (!nilfs_suinfo_reclaimable(&si)) {
			/*
			 * Recheck status of the segment and drop it
			 * if not reclaimable.  This prevents the
			 * target segments from being cleaned twice or
			 * more by duplicate cleaner daemons.
			 */
			n = nilfs_deselect_segment(segnums, n, i);
			continue;
		}

		if (nilfs_get_segment(nilfs, segnums[i], &segment) < 0)
			return -1;

		segseq = nilfs_get_segment_seqnum(nilfs, segment, segnums[i]);
		if (cnt64_ge(segseq, protseq)) {
			n = nilfs_deselect_segment(segnums, n, i);
			if (nilfs_put_segment(nilfs, segment) < 0)
				return -1;
			continue;
		}
		ret = nilfs_acc_blocks_segment(
			nilfs, segnums[i], segment, si.sui_nblocks,
			vdescv, bdescv);
		if (nilfs_put_segment(nilfs, segment) < 0 || ret < 0)
			return -1;
		i++;
	}
	return n;
}

/**
 * nilfs_get_vdesc - get information on virtual block addresses
 * @nilfs: nilfs object
 * @vdescv: vector object storing (descriptors of) virtual block numbers
 */
static int nilfs_get_vdesc(struct nilfs *nilfs, struct nilfs_vector *vdescv)
{
	struct nilfs_vdesc *vdesc;
	struct nilfs_vinfo vinfo[NILFS_GC_NVINFO];
	ssize_t n;
	int i, j;

	nilfs_vector_sort(vdescv, nilfs_comp_vdesc_vblocknr);

	for (i = 0; i < nilfs_vector_get_size(vdescv); i += n) {
		for (j = 0;
		     (j < NILFS_GC_NVINFO) &&
			     (i + j < nilfs_vector_get_size(vdescv));
		     j++) {
			vdesc = nilfs_vector_get_element(vdescv, i + j);
			assert(vdesc != NULL);
			vinfo[j].vi_vblocknr = vdesc->vd_vblocknr;
		}
		n = nilfs_get_vinfo(nilfs, vinfo, j);
		if (n < 0)
			return -1;
		for (j = 0; j < n; j++) {
			vdesc = nilfs_vector_get_element(vdescv, i + j);
			assert((vdesc != NULL) &&
			       (vdesc->vd_vblocknr == vinfo[j].vi_vblocknr));
			vdesc->vd_period.p_start = vinfo[j].vi_start;
			vdesc->vd_period.p_end = vinfo[j].vi_end;
		}
	}

	return 0;
}

/**
 * nilfs_get_snapshot - get checkpoint numbers of snapshots
 * @nilfs: nilfs object
 * @ssp: pointer to store array of checkpoint numbers which are snapshots
 */
static ssize_t nilfs_get_snapshot(struct nilfs *nilfs, nilfs_cno_t **ssp)
{
	struct nilfs_cpstat cpstat;
	struct nilfs_cpinfo cpinfo[NILFS_GC_NCPINFO];
	nilfs_cno_t cno, *ss, prev = 0;
	ssize_t n;
	__u64 nss = 0;
	int i, j;

	if (nilfs_get_cpstat(nilfs, &cpstat) < 0)
		return -1;
	if (cpstat.cs_nsss == 0)
		return 0;

	ss = malloc(sizeof(*ss) * cpstat.cs_nsss);
	if (ss == NULL)
		return -1;

	cno = 0;
	for (i = 0; i < cpstat.cs_nsss; i += n) {
		n = nilfs_get_cpinfo(nilfs, cno, NILFS_SNAPSHOT, cpinfo,
				     NILFS_GC_NCPINFO);
		if (n < 0) {
			free(ss);
			return -1;
		}
		if (n == 0)
			break;
		for (j = 0; j < n; j++) {
			ss[i + j] = cpinfo[j].ci_cno;
			if (prev >= ss[i + j]) {
				nilfs_gc_logger(LOG_ERR,
						"broken snapshot information. snapshot numbers appeared in a non-ascending order: %llu >= %llu",
						(unsigned long long)prev,
						(unsigned long long)ss[i + j]);
				free(ss);
				errno = EIO;
				return -1;
			}
			prev = ss[i + j];
		}
		nss += n;
		cno = cpinfo[n - 1].ci_next;
		if (cno == 0)
			break;
	}
	if (cpstat.cs_nsss != nss)
		nilfs_gc_logger
			(LOG_WARNING, "snapshot count mismatch: %llu != %llu",
			 (unsigned long long)cpstat.cs_nsss,
			 (unsigned long long)nss);
	*ssp = ss;
	return nss;
}

/*
 * nilfs_vdesc_is_live - judge if a virtual block address is live or dead
 * @vdesc: descriptor object of the virtual block address
 * @protect: the minimum of checkpoint numbers to be protected
 * @ss: checkpoint numbers of snapshots
 * @n: size of @ss array
 * @last_hit: the last snapshot number hit
 */
static int nilfs_vdesc_is_live(const struct nilfs_vdesc *vdesc,
			       nilfs_cno_t protect, const nilfs_cno_t *ss,
			       size_t n, nilfs_cno_t *last_hit)
{
	long low, high, index;

	if (vdesc->vd_cno == 0) {
		/*
		 * live/dead judge for sufile and cpfile should not
		 * depend on protection period and snapshots.  Without
		 * this check, gc will cause buffer confliction error
		 * because their checkpoint number is always zero.
		 */
		return vdesc->vd_period.p_end == NILFS_CNO_MAX;
	}

	if (vdesc->vd_period.p_end == vdesc->vd_cno) {
		/*
		 * This block was overwritten in the same logical segment, but
		 * in a different partial segment. Probably because of
		 * fdatasync() or a flush to disk.
		 * Without this check, gc will cause buffer confliction error
		 * if both partial segments are cleaned at the same time.
		 * In that case there will be two vdesc with the same ino,
		 * cno and offset.
		 */
		return 0;
	}

	if (vdesc->vd_period.p_end == NILFS_CNO_MAX ||
	    vdesc->vd_period.p_end > protect)
		return 1;

	if (n == 0 || vdesc->vd_period.p_start > ss[n - 1] ||
	    vdesc->vd_period.p_end <= ss[0])
		return 0;

	/* Try the last hit snapshot number */
	if (*last_hit >= vdesc->vd_period.p_start &&
	    *last_hit < vdesc->vd_period.p_end)
		return 1;

	low = 0;
	high = n - 1;
	index = 0;
	while (low <= high) {
		index = (low + high) / 2;
		if (ss[index] < vdesc->vd_period.p_start) {
			/* drop snapshot numbers ss[low] .. ss[index] */
			low = index + 1;
		} else if (ss[index] >= vdesc->vd_period.p_end) {
			/* drop snapshot numbers ss[index] .. ss[high] */
			high = index - 1;
		} else {
			/* ss[index] is in the range [p_start, p_end) */
			*last_hit = ss[index];
			return 1;
		}
	}
	return 0;
}

/**
 * nilfs_toss_vdescs - deselect deletable virtual block numbers
 * @nilfs: nilfs object
 * @vdescv: vector object storing (descriptors of) virtual block numbers
 * @periodv: vector object to store deletable checkpoint numbers (periods)
 * @vblocknrv: vector object to store deletable virtual block numbers
 * @protcno: start number of checkpoint to be protected
 *
 * nilfs_cleanerd_toss_vdescs() deselects virtual block numbers of files
 * other than the DAT file.
 */
static int nilfs_toss_vdescs(struct nilfs *nilfs,
			     struct nilfs_vector *vdescv,
			     struct nilfs_vector *periodv,
			     struct nilfs_vector *vblocknrv,
			     nilfs_cno_t protcno)
{
	struct nilfs_vdesc *vdesc;
	struct nilfs_period *periodp;
	__u64 *vblocknrp;
	nilfs_cno_t *ss, last_hit;
	ssize_t n;
	int i, j, ret;

	ss = NULL;
	n = nilfs_get_snapshot(nilfs, &ss);
	if (n < 0)
		return n;

	last_hit = 0;
	for (i = 0; i < nilfs_vector_get_size(vdescv); i++) {
		for (j = i; j < nilfs_vector_get_size(vdescv); j++) {
			vdesc = nilfs_vector_get_element(vdescv, j);
			assert(vdesc != NULL);
			if (nilfs_vdesc_is_live(vdesc, protcno, ss, n,
						&last_hit))
				break;

			/*
			 * Add the virtual block number to the candidate
			 * for deletion.
			 */
			vblocknrp = nilfs_vector_get_new_element(vblocknrv);
			if (!vblocknrp) {
				ret = -1;
				goto out;
			}
			*vblocknrp = vdesc->vd_vblocknr;

			/*
			 * Add the period to the candidate for deletion
			 * unless the file is cpfile or sufile.
			 */
			if (vdesc->vd_cno != 0) {
				periodp = nilfs_vector_get_new_element(periodv);
				if (!periodp) {
					ret = -1;
					goto out;
				}
				*periodp = vdesc->vd_period;
			}
		}
		if (j > i)
			nilfs_vector_delete_elements(vdescv, i, j - i);
	}
	ret = 0;
 out:
	free(ss);
	return ret;
}

/**
 * nilfs_unify_period - unify periods of checkpoint numbers
 * @periodv: vector object storing checkpoint numbers
 */
static void nilfs_unify_period(struct nilfs_vector *periodv)
{
	struct nilfs_period *base, *target;
	int i, j;

	nilfs_vector_sort(periodv, nilfs_comp_period);

	for (i = 0; i < nilfs_vector_get_size(periodv); i++) {
		base = nilfs_vector_get_element(periodv, i);
		assert(base != NULL);
		for (j = i + 1; j < nilfs_vector_get_size(periodv); j++) {
			target = nilfs_vector_get_element(periodv, j);
			assert(target != NULL);
			if (base->p_end < target->p_start)
				break;
			if (base->p_end < target->p_end)
				base->p_end = target->p_end;
		}

		if (j > i + 1)
			nilfs_vector_delete_elements(periodv, i + 1,
						     j - i - 1);
	}
}

/**
 * nilfs_get_bdesc - get information on disk block addresses
 * @nilfs: nilfs object
 * @bdescv: vector object storing (descriptors of) disk block numbers
 */
static int nilfs_get_bdesc(struct nilfs *nilfs, struct nilfs_vector *bdescv)
{
	struct nilfs_bdesc *bdescs;
	size_t nbdescs, count;
	ssize_t n;
	int i;

	nilfs_vector_sort(bdescv, nilfs_comp_bdesc);

	bdescs = nilfs_vector_get_data(bdescv);
	nbdescs = nilfs_vector_get_size(bdescv);
	for (i = 0; i < nbdescs; i += n) {
		count = min_t(size_t, nbdescs - i, NILFS_GC_NBDESCS);
		n = nilfs_get_bdescs(nilfs, bdescs + i, count);
		if (n < 0)
			return -1;
	}

	return 0;
}

/**
 * nilfs_bdesc_is_live - judge if a disk block address is live or dead
 * @bdesc: descriptor object of the disk block address
 */
static int nilfs_bdesc_is_live(struct nilfs_bdesc *bdesc)
{
	return bdesc->bd_oblocknr == bdesc->bd_blocknr;
}

/**
 * nilfs_toss_bdescs - deselect deletable disk block numbers
 * @bdescv: vector object storing (descriptors of) disk block numbers
 *
 * This function deselects disk block numbers of the DAT file which
 * don't belong to the latest DAT file.
 */
static int nilfs_toss_bdescs(struct nilfs_vector *bdescv)
{
	struct nilfs_bdesc *bdesc;
	int i, j;

	for (i = 0; i < nilfs_vector_get_size(bdescv); i++) {
		for (j = i; j < nilfs_vector_get_size(bdescv); j++) {
			bdesc = nilfs_vector_get_element(bdescv, j);
			assert(bdesc != NULL);
			if (nilfs_bdesc_is_live(bdesc))
				break;
		}
		if (j > i)
			nilfs_vector_delete_elements(bdescv, i, j - i);
	}
	return 0;
}

/**
 * nilfs_xreclaim_segment - reclaim segments (enhanced API)
 * @nilfs: nilfs object
 * @segnums: array of segment numbers storing selected segments
 * @nsegs: size of the @segnums array
 * @dryrun: dry-run flag
 * @params: reclaim parameters
 * @stat: reclaim statistics
 */
int nilfs_xreclaim_segment(struct nilfs *nilfs,
			   __u64 *segnums, size_t nsegs, int dryrun,
			   const struct nilfs_reclaim_params *params,
			   struct nilfs_reclaim_stat *stat)
{
	struct nilfs_vector *vdescv, *bdescv, *periodv, *vblocknrv, *supv;
	sigset_t sigset, oldset, waitset;
	nilfs_cno_t protcno;
	ssize_t n, i, ret = -1;
	size_t nblocks;
	__u32 reclaimable_blocks;
	struct nilfs_suinfo_update *sup;
	struct timeval tv;

	if (!(params->flags & NILFS_RECLAIM_PARAM_PROTSEQ) ||
	    (params->flags & (~0UL << __NR_NILFS_RECLAIM_PARAMS))) {
		/*
		 * The protseq parameter is mandatory.  Unknown
		 * parameters are rejected.
		 */
		errno = EINVAL;
		return -1;
	}

	if (nsegs == 0)
		return 0;

	vdescv = nilfs_vector_create(sizeof(struct nilfs_vdesc));
	bdescv = nilfs_vector_create(sizeof(struct nilfs_bdesc));
	periodv = nilfs_vector_create(sizeof(struct nilfs_period));
	vblocknrv = nilfs_vector_create(sizeof(__u64));
	supv = nilfs_vector_create(sizeof(struct nilfs_suinfo_update));
	if (!vdescv || !bdescv || !periodv || !vblocknrv || !supv)
		goto out_vec;

	sigemptyset(&sigset);
	sigaddset(&sigset, SIGINT);
	sigaddset(&sigset, SIGTERM);
	ret = sigprocmask(SIG_BLOCK, &sigset, &oldset);
	if (ret < 0) {
		nilfs_gc_logger(LOG_ERR, "cannot block signals: %s",
				strerror(errno));
		goto out_vec;
	}

	ret = nilfs_lock_cleaner(nilfs);
	if (ret < 0)
		goto out_sig;

	/* count blocks */
	n = nilfs_acc_blocks(nilfs, segnums, nsegs, params->protseq, vdescv,
			     bdescv);
	if (n < 0) {
		ret = n;
		goto out_lock;
	}
	if (stat) {
		stat->cleaned_segs = n;
		stat->protected_segs = nsegs - n;
		stat->deferred_segs = 0;
	}
	if (n == 0) {
		ret = 0;
		goto out_lock;
	}

	/* toss virtual blocks */
	ret = nilfs_get_vdesc(nilfs, vdescv);
	if (ret < 0)
		goto out_lock;

	nblocks = nilfs_vector_get_size(vdescv);
	protcno = (params->flags & NILFS_RECLAIM_PARAM_PROTCNO) ?
		params->protcno : NILFS_CNO_MAX;

	ret = nilfs_toss_vdescs(nilfs, vdescv, periodv, vblocknrv, protcno);
	if (ret < 0)
		goto out_lock;

	if (stat) {
		stat->live_vblks = nilfs_vector_get_size(vdescv);
		stat->defunct_vblks = nblocks - stat->live_vblks;
		stat->freed_vblks = nilfs_vector_get_size(vblocknrv);
	}

	nilfs_vector_sort(vdescv, nilfs_comp_vdesc_blocknr);
	nilfs_unify_period(periodv);

	/* toss DAT file blocks */
	ret = nilfs_get_bdesc(nilfs, bdescv);
	if (ret < 0)
		goto out_lock;

	nblocks = nilfs_vector_get_size(bdescv);
	ret = nilfs_toss_bdescs(bdescv);
	if (ret < 0)
		goto out_lock;

	reclaimable_blocks = (nilfs_get_blocks_per_segment(nilfs) * n) -
			(nilfs_vector_get_size(vdescv) +
			nilfs_vector_get_size(bdescv));

	if (stat) {
		stat->live_pblks = nilfs_vector_get_size(bdescv);
		stat->defunct_pblks = nblocks - stat->live_pblks;

		stat->live_blks = stat->live_vblks + stat->live_pblks;
		stat->defunct_blks = reclaimable_blocks;
	}
	if (dryrun)
		goto out_lock;

	ret = sigpending(&waitset);
	if (ret < 0) {
		nilfs_gc_logger(LOG_ERR, "cannot test signals: %s",
				strerror(errno));
		goto out_lock;
	}
	if (sigismember(&waitset, SIGINT) || sigismember(&waitset, SIGTERM)) {
		nilfs_gc_logger(LOG_DEBUG, "interrupted");
		goto out_lock;
	}

	/*
	 * if there are less reclaimable blocks than the minimal
	 * threshold try to update suinfo instead of cleaning
	 */
	if ((params->flags & NILFS_RECLAIM_PARAM_MIN_RECLAIMABLE_BLKS) &&
			nilfs_opt_test_set_suinfo(nilfs) &&
			reclaimable_blocks < params->min_reclaimable_blks * n) {
		if (stat) {
			stat->deferred_segs = n;
			stat->cleaned_segs = 0;
		}

		ret = gettimeofday(&tv, NULL);
		if (ret < 0)
			goto out_lock;

		for (i = 0; i < n; ++i) {
			sup = nilfs_vector_get_new_element(supv);
			if (!sup)
				goto out_lock;

			sup->sup_segnum = segnums[i];
			sup->sup_flags = 0;
			nilfs_suinfo_update_set_lastmod(sup);
			sup->sup_sui.sui_lastmod = tv.tv_sec;
		}

		ret = nilfs_set_suinfo(nilfs, nilfs_vector_get_data(supv), n);

		if (ret == 0)
			goto out_lock;

		if (ret < 0 && errno != ENOTTY) {
			nilfs_gc_logger(LOG_ERR, "cannot set suinfo: %s",
					strerror(errno));
			goto out_lock;
		}

		/* errno == ENOTTY */
		nilfs_gc_logger(LOG_WARNING,
				"set_suinfo ioctl is not supported");
		nilfs_opt_clear_set_suinfo(nilfs);
		if (stat) {
			stat->deferred_segs = 0;
			stat->cleaned_segs = n;
		}
		/* Try nilfs_clean_segments */
	}

	ret = nilfs_clean_segments(nilfs,
				   nilfs_vector_get_data(vdescv),
				   nilfs_vector_get_size(vdescv),
				   nilfs_vector_get_data(periodv),
				   nilfs_vector_get_size(periodv),
				   nilfs_vector_get_data(vblocknrv),
				   nilfs_vector_get_size(vblocknrv),
				   nilfs_vector_get_data(bdescv),
				   nilfs_vector_get_size(bdescv),
				   segnums, n);
	if (ret < 0) {
		nilfs_gc_logger(LOG_ERR, "cannot clean segments: %s",
				strerror(errno));
	}

out_lock:
	if (nilfs_unlock_cleaner(nilfs) < 0) {
		nilfs_gc_logger(LOG_CRIT, "failed to unlock cleaner: %s",
				strerror(errno));
		exit(EXIT_FAILURE);
	}

out_sig:
	sigprocmask(SIG_SETMASK, &oldset, NULL);

out_vec:
	nilfs_vector_destroy(vdescv);
	nilfs_vector_destroy(bdescv);
	nilfs_vector_destroy(periodv);
	nilfs_vector_destroy(vblocknrv);
	nilfs_vector_destroy(supv);
	/*
	 * Flags of valid fields in stat->exflags must be unset.
	 */
	return ret;
}

/**
 * nilfs_reclaim_segment - reclaim segments
 * @nilfs: nilfs object
 * @segnums: array of segment numbers storing selected segments
 * @nsegs: size of the @segnums array
 * @protseq: start of sequence number of protected segments
 * @protcno: start checkpoint number of protected period
 */
ssize_t nilfs_reclaim_segment(struct nilfs *nilfs,
			      __u64 *segnums, size_t nsegs,
			      __u64 protseq, nilfs_cno_t protcno)
{
	struct nilfs_reclaim_params params;
	struct nilfs_reclaim_stat stat;
	int ret;

	params.flags =
		NILFS_RECLAIM_PARAM_PROTSEQ | NILFS_RECLAIM_PARAM_PROTCNO;
	params.min_reclaimable_blks = 0;
	params.protseq = protseq;
	params.protcno = protcno;
	memset(&stat, 0, sizeof(stat));

	ret = nilfs_xreclaim_segment(nilfs, segnums, nsegs, 0,
				     &params, &stat);
	if (!ret)
		ret = stat.cleaned_segs;
	return ret;
}
