/*
 * parser.h - NILFS parser library
 *
 * Copyright (C) 2005-2012 Nippon Telegraph and Telephone Corporation.
 *
 * This program can be redistributed under the terms of the GNU Lesser
 * General Public License.
 */

#ifndef NILFS_PARSER_H
#define NILFS_PARSER_H

extern nilfs_cno_t nilfs_parse_cno(const char *arg, char **endptr, int base);
extern int nilfs_parse_cno_range(const char *arg, __u64 *start, __u64 *end,
				 int base);
extern int nilfs_parse_protection_period(const char *arg,
					 unsigned long *period);

#endif /* NILFS_PARSER_H */
