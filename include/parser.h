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

#include <stdint.h>	/* uint64_t */
#include "nilfs.h"	/* nilfs_cno_t */

nilfs_cno_t nilfs_parse_cno(const char *arg, char **endptr, int base);
int nilfs_parse_cno_range(const char *arg, uint64_t *start, uint64_t *end,
			  int base);
int nilfs_parse_protection_period(const char *arg, unsigned long *period);

#endif /* NILFS_PARSER_H */
