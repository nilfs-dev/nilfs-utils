/*
 * cldconfig.h - Configuration file of NILFS cleaner daemon.
 *
 * Copyright (C) 2007 Nippon Telegraph and Telephone Corporation.
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
 * You should have received a copy of the GNU General Public License
 * along with NILFS; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * cldconfig.h,v 1.7 2007-06-13 09:34:34 koji Exp
 *
 * Written by Koji Sato <koji@osrg.net>.
 */

#ifndef CLDCONFIG_H
#define CLDCONFIG_H

#include <syslog.h>
#include "nilfs.h"

/**
 * struct nilfs_selection_policy -
 * @p_importance:
 * @p_threshold:
 */
struct nilfs_selection_policy {
	unsigned long long (*p_importance)(const struct nilfs_suinfo *);
	unsigned long long p_threshold;
};

/**
 * struct nilfs_cldconfig -
 * @cf_selection_policy:
 * @cf_protection_period:
 * @cf_nsegments_per_clean
 * @cf_cleaning_interval:
 * @cf_use_mmap:
 * @cf_log_priority:
 */
struct nilfs_cldconfig {
	struct nilfs_selection_policy cf_selection_policy;
	time_t cf_protection_period;
	int cf_nsegments_per_clean;
	time_t cf_cleaning_interval;
	int cf_use_mmap;
	int cf_log_priority;
};

#define NILFS_CLDCONFIG_SELECTION_POLICY_IMPORTANCE	\
			nilfs_cldconfig_selection_policy_timestamp
#define NILFS_CLDCONFIG_SELECTION_POLICY_THRESHOLD	0
#define NILFS_CLDCONFIG_PROTECTION_PERIOD		3600
#define NILFS_CLDCONFIG_NSEGMENTS_PER_CLEAN		2
#define NILFS_CLDCONFIG_CLEANING_INTERVAL		5
#define NILFS_CLDCONFIG_USE_MMAP			1
#define NILFS_CLDCONFIG_LOG_PRIORITY			LOG_INFO

#define NILFS_CLDCONFIG_NSEGMENTS_PER_CLEAN_MAX	32

/**
 * struct nilfs_cldconfig_keyword -
 * @ck_text:
 * @ck_handler:
 */
struct nilfs_cldconfig_keyword {
	const char *ck_text;
	int (*ck_handler)(struct nilfs_cldconfig * , char **, size_t);
};

#define NILFS_CLDCONFIG_COMMENT_CHAR	'#'

/**
 * struct nilfs_cldconfig_polhandle -
 * @cp_name:
 * @cp_handler:
 */
struct nilfs_cldconfig_polhandle {
	const char *cp_name;
	int (*cp_handler)(struct nilfs_cldconfig *, char **, size_t);
};

/**
 * struct nilfs_cldconfig_log_priority -
 * @cl_name:
 * @cl_priority:
 */
struct nilfs_cldconfig_log_priority {
	const char *cl_name;
	int cl_priority;
};


int nilfs_cldconfig_read(struct nilfs_cldconfig *, const char *);

#endif	/* CLDCONFIG_H */

/* Local Variables:		*/
/* eval: (c-set-style "linux")	*/
/* End:				*/
