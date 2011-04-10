/*
 * nilfs_misc.h - misc library for nilfs utilities
 *
 * Copyright (C) 2005-2011 Nippon Telegraph and Telephone Corporation.
 *
 * This program can be redistributed under the terms of the GNU Lesser
 * General Public License.
 */

#ifndef NILFS_MISC_H
#define NILFS_MISC_H

extern int nilfs_parse_cno_range(const char *arg, __u64 *start, __u64 *end,
				 int base);

#endif /* NILFS_MISC_H */
