/*
 * mkfs.h - NILFS newfs (mkfs.nilfs2), declarations
 *
 * Copyright (C) 2005-2012 Nippon Telegraph and Telephone Corporation.
 *
 * This file is part of NILFS.
 *
 * NILFS is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * NILFS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Written by Hisashi Hifumi,
 *            Amagai Yoshiji.
 * Revised by Ryusuke Konishi <konishi.ryusuke@lab.ntt.co.jp>.
 */

#include "nilfs2_fs.h"

#define NILFS_DISKHDR_SIZE		4096 /* HDD header (MBR+superblock) */
#define NILFS_DISK_ERASE_SIZE		1048576	/* size of first and last
						   bytes to be erased (except
						   for the partition table) */

#define NILFS_DEF_BLOCKSIZE_BITS	12   /* default blocksize = 2^12
						bytes */
#define NILFS_DEF_BLOCKSIZE	        (1 << NILFS_DEF_BLOCKSIZE_BITS)
#define NILFS_DEF_BLKS_PER_SEG		2048 /* default blocks per segment */
#define NILFS_DEF_CHECK_INTERVAL	(60*60*24*180) /* default check
							  interval: 180 days */
#define NILFS_DEF_RESERVED_SEGMENTS     5    /* default percentage of reserved
						segments: 5% */

#define NILFS_MAX_BMAP_ROOT_PTRS	(NILFS_INODE_BMAP_SIZE - 1)
#define NILFS_MIN_BLOCKSIZE		1024
#define NILFS_MIN_NUSERSEGS		8    /* Minimum number of user
						(non-reserved) segments */

/* Additional inode numbers */
enum {
	NILFS_NILFS_INO = NILFS_USER_INO,  /* .nilfs file */
	NILFS_MAX_INITIAL_INO,
};

/* bit operations */
extern int ext2fs_set_bit(int nr, void *addr);
extern int ext2fs_clear_bit(int nr, void *addr);
extern int ext2fs_test_bit(int nr, const void *addr);

#define nilfs_set_bit			ext2fs_set_bit
#define nilfs_clear_bit			ext2fs_clear_bit
#define nilfs_test_bit			ext2fs_test_bit

/* get device size through ioctl */
#ifndef BLKGETSIZE64
#define BLKGETSIZE64	_IOR(0x12, 114, size_t)
#endif

/*
 * linux/fs.h
 * File types
 *
 * NOTE! These match bits 12..15 of stat.st_mode
 * (ie "(i_mode >> 12) & 15").
 */
#define DT_UNKNOWN	0
#define DT_FIFO		1
#define DT_CHR		2
#define DT_DIR		4
#define DT_BLK		6
#define DT_REG		8
#define DT_LNK		10
#define DT_SOCK		12
#define DT_WHT		14

