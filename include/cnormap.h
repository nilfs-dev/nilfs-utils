/*
 * cnormap.h - checkpoint number reverse mapper
 *
 * Licensed under LGPLv2: the complete text of the GNU Lesser General
 * Public License can be found in COPYING file of the nilfs-utils
 * package.
 */

#ifndef NILFS_CNORMAP_H
#define NILFS_CNORMAP_H

#include <stdint.h>	/* uint64_t */
#include "nilfs.h"	/* nilfs_cno_t, struct nilfs */

struct nilfs_cnormap;

struct nilfs_cnormap *nilfs_cnormap_create(struct nilfs *nilfs);
void nilfs_cnormap_destroy(struct nilfs_cnormap *cnormap);
int nilfs_cnormap_track_back(struct nilfs_cnormap *cnormap, uint64_t period,
			     nilfs_cno_t *cnop);

#endif /* NILFS_CNORMAP_H */
