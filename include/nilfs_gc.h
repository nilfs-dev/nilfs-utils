/*
 * nilfs_gc.h - NILFS garbage collection library
 *
 * Licensed under LGPLv2: the complete text of the GNU Lesser General
 * Public License can be found in COPYING file of the nilfs-utils
 * package.
 *
 * Copyright (C) 2008-2012 Nippon Telegraph and Telephone Corporation.
 */

#ifndef NILFS_GC_H
#define NILFS_GC_H

#include <sys/types.h>
#include "nilfs.h"

/* flags for nilfs_reclaim_params struct */
#define NILFS_RECLAIM_PARAM_PROTSEQ			(1UL << 0)
#define NILFS_RECLAIM_PARAM_PROTCNO			(1UL << 1)
#define NILFS_RECLAIM_PARAM_MIN_RECLAIMABLE_BLKS	(1UL << 2)
#define __NR_NILFS_RECLAIM_PARAMS	3

/**
 * struct nilfs_reclaim_params - structure to specify GC parameters
 * @flags: flags of valid fields
 * @min_reclaimable_blks: minimum number of reclaimable blocks
 * @protseq: start of sequence number of protected segments
 * @protcno: start number of checkpoint to be protected
 */
struct nilfs_reclaim_params {
	unsigned long flags;
	unsigned long min_reclaimable_blks;
	__u64 protseq;
	nilfs_cno_t protcno;
};

/**
 * struct nilfs_reclaim_stat - structure to store GC statistics
 * @exflags: flags for extended fields (reserved)
 * @cleaned_segs: number of cleaned segments
 * @protected_segs: number of protected (deselected) segments
 * @deferred_segs: number of deferred segments
 * @live_blks: number of live (in-use) blocks
 * @live_vblks: number of live (in-use) virtual blocks
 * @live_pblks: number of live (in-use) DAT file blocks
 * @defunct_blks: number of defunct (reclaimable) blocks
 * @defunct_vblks: number of defunct (reclaimable) virtual blocks
 * @defunct_pblks: number of defunct (reclaimable) DAT file blocks
 * @freed_vblks: number of freed virtual blocks
 */
struct nilfs_reclaim_stat {
	unsigned long exflags;
	size_t cleaned_segs;
	size_t protected_segs;
	size_t deferred_segs;
	size_t live_blks;
	size_t live_vblks;
	size_t live_pblks;
	size_t defunct_blks;
	size_t defunct_vblks;
	size_t defunct_pblks;
	size_t freed_vblks;
};

ssize_t nilfs_reclaim_segment(struct nilfs *nilfs,
			      __u64 *segnums, size_t nsegs,
			      __u64 protseq, nilfs_cno_t protcno);

int nilfs_xreclaim_segment(struct nilfs *nilfs,
			   __u64 *segnums, size_t nsegs, int dryrun,
			   const struct nilfs_reclaim_params *params,
			   struct nilfs_reclaim_stat *stat);

static inline int
nilfs_assess_segment(struct nilfs *nilfs,
		     __u64 *segnums, size_t nsegs,
		     const struct nilfs_reclaim_params *params,
		     struct nilfs_reclaim_stat *stat)
{
	return nilfs_xreclaim_segment(nilfs, segnums, nsegs, 1, params, stat);
}

static inline int nilfs_suinfo_reclaimable(const struct nilfs_suinfo *si)
{
	return nilfs_suinfo_dirty(si) &&
		!nilfs_suinfo_active(si) && !nilfs_suinfo_error(si);
}

extern void (*nilfs_gc_logger)(int priority, const char *fmt, ...);

#endif /* NILFS_GC_H */
