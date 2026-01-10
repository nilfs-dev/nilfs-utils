/*
 * bitops.h - Ext2-fs compatible bitmap operations
 */

#ifndef __BITOPS_H__
#define __BITOPS_H__

int ext2fs_set_bit(int nr, void *addr);
int ext2fs_clear_bit(int nr, void *addr);
int ext2fs_test_bit(int nr, const void *addr);

#endif /* __BITOPS_H__ */
