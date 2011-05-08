/*
 * nilfs_gc.h - NILFS garbage collection library
 *
 * Licensed under LGPLv2: the complete text of the GNU Lesser General
 * Public License can be found in COPYING file of the nilfs-utils
 * package.
 *
 * Copyright (C) 2008-2011 Nippon Telegraph and Telephone Corporation.
 */

#ifndef NILFS_GC_H
#define NILFS_GC_H

#include <sys/types.h>
#include "nilfs.h"

ssize_t nilfs_reclaim_segment(struct nilfs *nilfs,
			      __u64 *segnums, size_t nsegs,
			      __u64 protseq, nilfs_cno_t protcno);


static inline int nilfs_suinfo_reclaimable(const struct nilfs_suinfo *si)
{
	return nilfs_suinfo_dirty(si) &&
		!nilfs_suinfo_active(si) && !nilfs_suinfo_error(si);
}

extern void (*nilfs_gc_logger)(int priority, const char *fmt, ...);

#endif /* NILFS_GC_H */
