/*
 * nilfs_feature.h - routines to handle NILFS features set
 *
 * Copyright (C) 2011-2012 Nippon Telegraph and Telephone Corporation.
 *
 * This program can be redistributed under the terms of the GNU Lesser
 * General Public License.
 */

#ifndef NILFS_FEATURE_H
#define NILFS_FEATURE_H

enum nilfs_feature_type {
	NILFS_FEATURE_TYPE_COMPAT = 0,
	NILFS_FEATURE_TYPE_COMPAT_RO,
	NILFS_FEATURE_TYPE_INCOMPAT,
	NILFS_MAX_FEATURE_TYPES,

	/* other definitions */
	NILFS_FEATURE_TYPE_MASK = 3,
	NILFS_FEATURE_TYPE_NEGATE_FLAG = 0x80,
};

extern const char *nilfs_feature2string(int compat_type, __u64 mask);
extern int nilfs_string2feature(const char *str, int *compat_type, __u64 *mask);
extern int nilfs_edit_feature(const char *str, __u64 *compat_array,
			      const __u64 *ok_array,
			      const __u64 *clear_ok_array,
			      int *bad_type, __u64 *bad_mask);

#endif	/* NILFS_FEATURE_H */
