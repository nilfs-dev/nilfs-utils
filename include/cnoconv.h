/*
 * cnoconv.h - checkpoint number converter (obsolete)
 *
 * Licensed under LGPLv2: the complete text of the GNU Lesser General
 * Public License can be found in COPYING file of the nilfs-utils
 * package.
 *
 * Copyright (C) 2008-2012 Nippon Telegraph and Telephone Corporation.
 */

#ifndef NILFS_CNOCONV_H
#define NILFS_CNOCONV_H

#include <sys/types.h>
#include "nilfs.h"

struct nilfs_cnoconv;

struct nilfs_cnoconv *nilfs_cnoconv_create(struct nilfs *nilfs);
void nilfs_cnoconv_destroy(struct nilfs_cnoconv *cnoconv);
void nilfs_cnoconv_reset(struct nilfs_cnoconv *cnoconv);
int nilfs_cnoconv_time2cno(struct nilfs_cnoconv *cnoconv, __u64 time,
			   nilfs_cno_t *cnop);

#endif /* NILFS_CNOCONV */
