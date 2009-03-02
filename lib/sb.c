/*
 * sb.c - NILFS super block access library
 *
 * Copyright (C) 2005-2009 Nippon Telegraph and Telephone Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Written by Ryusuke Konishi <ryusuke@osrg.net>.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif	/* HAVE_CONFIG_H */

#define _FILE_OFFSET_BITS 64
#undef _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE
#define __USE_FILE_OFFSET64
#define _XOPEN_SOURCE 600

#include <stdio.h>

#if HAVE_STDLIB_H
#include <stdlib.h>
#endif	/* HAVE_STDLIB_H */

#if HAVE_UNISTD_H
#include <unistd.h>
#endif	/* HAVE_UNISTD_H */

#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif	/* HAVE_SYS_TYPES_H */

#if HAVE_FCNTL_H
#include <fcntl.h>
#endif	/* HAVE_FCNTL_H */

#if HAVE_LIMITS_H
#include <limits.h>
#endif	/* HAVE_LIMITS_H */

#if HAVE_LINUX_TYPES_H
#include <linux/types.h>
#endif	/* HAVE_LINUX_TYPES_H */

#include <linux/fs.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <assert.h>
#include "nilfs.h"

#define NILFS_MAX_SB_SIZE	1024

struct nilfs_super_block *nilfs_get_sb(struct nilfs *nilfs)
{
	return nilfs->n_sb;
}

static int nilfs_sb_is_valid(struct nilfs_super_block *sbp, int check_crc)
{
	__le32 sum;
	__u32 seed, crc;

	if (le16_to_cpu(sbp->s_magic) != NILFS_SUPER_MAGIC)
		return 0;
	if (le16_to_cpu(sbp->s_bytes) > NILFS_MAX_SB_SIZE)
		return 0;
	if (!check_crc)
		return 1;

	seed = le32_to_cpu(sbp->s_crc_seed);
	sum = sbp->s_sum;
	sbp->s_sum = 0;
	crc = crc32_le(seed, (unsigned char *)sbp, le16_to_cpu(sbp->s_bytes));
	sbp->s_sum = sum;
	return crc == le32_to_cpu(sum);
}

int nilfs_read_sb(struct nilfs *nilfs)
{
	struct nilfs_super_block *sbp[2];
	__u64 devsize, sb2_offset;

	assert(nilfs->n_sb == NULL);

	sbp[0] = malloc(NILFS_MAX_SB_SIZE);
	sbp[1] = malloc(NILFS_MAX_SB_SIZE);
	if (sbp[0] == NULL || sbp[1] == NULL)
		goto failed;

	if (ioctl(nilfs->n_devfd, BLKGETSIZE64, &devsize) != 0)
		goto failed;

	if (lseek64(nilfs->n_devfd, NILFS_SB_OFFSET_BYTES, SEEK_SET) < 0 ||
	    read(nilfs->n_devfd, sbp[0], NILFS_MAX_SB_SIZE) < 0 ||
	    !nilfs_sb_is_valid(sbp[0], 0)) {
		free(sbp[0]);
		sbp[0] = NULL;
	}

	sb2_offset = NILFS_SB2_OFFSET_BYTES(devsize);
	if (lseek64(nilfs->n_devfd, sb2_offset, SEEK_SET) < 0 ||
	    read(nilfs->n_devfd, sbp[1], NILFS_MAX_SB_SIZE) < 0 ||
	    !nilfs_sb_is_valid(sbp[1], 0))
		goto sb2_failed;

	if (sb2_offset <
	    (le64_to_cpu(sbp[1]->s_nsegments) *
	     le32_to_cpu(sbp[1]->s_blocks_per_segment)) <<
	    (le32_to_cpu(sbp[1]->s_log_block_size) +
	     NILFS_SB_BLOCK_SIZE_SHIFT))
		goto sb2_failed;

 sb2_done:
	if (!sbp[0]) {
		sbp[0] = sbp[1];
		sbp[1] = NULL;
	}

#if 0
	if (sbp[1] &&
	    le64_to_cpu(sbp[1]->s_wtime) > le64_to_cpu(sbp[0]->s_wtime)) {
		nilfs->n_sb = sbp[1];
		free(sbp[0]);
	} else
#endif
		if (sbp[0]) {
		nilfs->n_sb = sbp[0];
		free(sbp[1]);
	} else
		goto failed;

	return 0;

 failed:
	free(sbp[0]);  /* free(NULL) is just ignored */
	free(sbp[1]);
	return -1;

 sb2_failed:
	free(sbp[1]);
	sbp[1] = NULL;
	goto sb2_done;
}
