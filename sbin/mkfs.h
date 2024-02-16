/*
 * mkfs.h - NILFS newfs (mkfs.nilfs2), declarations
 *
 * Copyright (C) 2005-2012 Nippon Telegraph and Telephone Corporation.
 *
 * Licensed under GPLv2: the complete text of the GNU General Public
 * License can be found in COPYING file of the nilfs-utils package.
 *
 * Credits:
 *    Hisashi Hifumi,
 *    Amagai Yoshiji,
 *    Ryusuke Konishi <konishi.ryusuke@gmail.com>.
 */

#ifndef NILFS_MKFS_H
#define NILFS_MKFS_H

#include <linux/nilfs2_ondisk.h>
#include "compat.h"
#include "util.h"	/* BUG_ON() */

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

#define NILFS_MAX_INITIAL_INO		NILFS_USER_INO

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

/* Inline functions to convert record length of directory entries */
static inline unsigned int nilfs_rec_len_from_disk(__le16 dlen)
{
	unsigned int len = le16_to_cpu(dlen);

	return len == NILFS_MAX_REC_LEN ? 1 << 16 : len;
}

static inline __le16 nilfs_rec_len_to_disk(unsigned int len)
{
	BUG_ON(len > (1 << 16));

	return len == (1 << 16) ? cpu_to_le16(NILFS_MAX_REC_LEN) :
		cpu_to_le16(len);
}

#endif /* NILFS_MKFS_H */
