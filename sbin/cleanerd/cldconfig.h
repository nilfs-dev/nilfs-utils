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
 * Written by Koji Sato <koji@osrg.net>.
 *
 * Maintained by Ryusuke Konishi <ryusuke@osrg.net> from 2008.
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
 * struct nilfs_cldconfig - cleanerd configuration
 * @cf_selection_policy: selection policy
 * @cf_protection_period: protection period
 * @cf_min_clean_segments: low threshold on the number of free segments
 * @cf_max_clean_segments: high threshold on the number of free segments
 * @cf_clean_check_interval: cleaner check interval
 * @cf_nsegments_per_clean number of segments reclaimed per clean cycle
 * @cf_cleaning_interval: cleaning interval
 * @cf_use_mmap: flag that indicate using mmap
 * @cf_log_priority: log priority level
 */
struct nilfs_cldconfig {
	struct nilfs_selection_policy cf_selection_policy;
	time_t cf_protection_period;
	__u64 cf_min_clean_segments;
	__u64 cf_max_clean_segments;
	time_t cf_clean_check_interval;
	int cf_nsegments_per_clean;
	time_t cf_cleaning_interval;
	time_t cf_retry_interval;
	int cf_use_mmap;
	int cf_log_priority;
};

#define NILFS_CLDCONFIG_SELECTION_POLICY_IMPORTANCE	\
			nilfs_cldconfig_selection_policy_timestamp
#define NILFS_CLDCONFIG_SELECTION_POLICY_THRESHOLD	0
#define NILFS_CLDCONFIG_PROTECTION_PERIOD		3600
#define	NILFS_CLDCONFIG_MIN_CLEAN_SEGMENTS		100
#define	NILFS_CLDCONFIG_MAX_CLEAN_SEGMENTS		200
#define	NILFS_CLDCONFIG_CLEAN_CHECK_INTERVAL		60
#define NILFS_CLDCONFIG_NSEGMENTS_PER_CLEAN		2
#define NILFS_CLDCONFIG_CLEANING_INTERVAL		5
#define NILFS_CLDCONFIG_RETRY_INTERVAL			60
#define NILFS_CLDCONFIG_USE_MMAP			1
#define NILFS_CLDCONFIG_LOG_PRIORITY			LOG_INFO

#define NILFS_CLDCONFIG_NSEGMENTS_PER_CLEAN_MAX	32


int nilfs_cldconfig_read(struct nilfs_cldconfig *, const char *);

#endif	/* CLDCONFIG_H */
