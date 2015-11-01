/*
 * cldconfig.h - Configuration file of NILFS cleaner daemon.
 *
 * Copyright (C) 2007-2012 Nippon Telegraph and Telephone Corporation.
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
 * Written by Koji Sato.
 *
 * Maintained by Ryusuke Konishi <konishi.ryusuke@lab.ntt.co.jp> from 2008.
 */

#ifndef CLDCONFIG_H
#define CLDCONFIG_H

#include <sys/time.h>
#include <syslog.h>

struct nilfs_suinfo;

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
 * struct nilfs_param - parameter with unit suffix
 * @num: scanned value
 * @unit: unit given to the parameter
 */
struct nilfs_param {
	unsigned long num;
	int unit;
};

enum nilfs_size_unit {
	NILFS_SIZE_UNIT_NONE = 0,
	NILFS_SIZE_UNIT_PERCENT,
	NILFS_SIZE_UNIT_KB,	/* kilo-byte (kB) */
	NILFS_SIZE_UNIT_KIB,	/* kibi-byte (KiB) */
	NILFS_SIZE_UNIT_MB,	/* mega-byte (MB) */
	NILFS_SIZE_UNIT_MIB,	/* mebi-byte (MiB) */
	NILFS_SIZE_UNIT_GB,	/* giga-byte (GB) */
	NILFS_SIZE_UNIT_GIB,	/* gibi-byte (GiB) */
	NILFS_SIZE_UNIT_TB,	/* tera-byte (TB) */
	NILFS_SIZE_UNIT_TIB,	/* tebi-byte (TiB) */
	NILFS_SIZE_UNIT_PB,	/* peta-byte (PB) */
	NILFS_SIZE_UNIT_PIB,	/* pebi-byte (PiB) */
	NILFS_SIZE_UNIT_EB,	/* exa-byte (EB) */
	NILFS_SIZE_UNIT_EIB,	/* exbi-byte (EiB) */

	NILFS_MIN_BINARY_SUFFIX = NILFS_SIZE_UNIT_KB,
	NILFS_MAX_BINARY_SUFFIX = NILFS_SIZE_UNIT_EIB,
};

/**
 * struct nilfs_cldconfig - cleanerd configuration
 * @cf_selection_policy: selection policy
 * @cf_protection_period: protection period
 * @cf_min_clean_segments: low threshold on the number of free segments
 * @cf_max_clean_segments: high threshold on the number of free segments
 * @cf_clean_check_interval: cleaner check interval
 * @cf_nsegments_per_clean: number of segments reclaimed per clean cycle
 * @cf_mc_nsegments_per_clean: number of segments reclaimed per clean cycle
 * if clean segments < min_clean_segments
 * @cf_cleaning_interval: cleaning interval
 * @cf_mc_cleaning_interval: cleaning interval
 * if clean segments < min_clean_segments
 * @cf_retry_interval: retry interval
 * @cf_use_mmap: flag that indicate using mmap
 * @cf_use_set_suinfo: flag that indicates the use of the set_suinfo ioctl
 * @cf_log_priority: log priority level
 * @cf_min_reclaimable_blocks: minimum reclaimable blocks for cleaning
 * @cf_mc_min_reclaimable_blocks: minimum reclaimable blocks for cleaning
 * if clean segments < min_clean_segments
 */
struct nilfs_cldconfig {
	struct nilfs_selection_policy cf_selection_policy;
	struct timeval cf_protection_period;
	__u64 cf_min_clean_segments;
	__u64 cf_max_clean_segments;
	struct timeval cf_clean_check_interval;
	int cf_nsegments_per_clean;
	int cf_mc_nsegments_per_clean;
	struct timeval cf_cleaning_interval;
	struct timeval cf_mc_cleaning_interval;
	struct timeval cf_retry_interval;
	int cf_use_mmap;
	int cf_use_set_suinfo;
	int cf_log_priority;
	unsigned long cf_min_reclaimable_blocks;
	unsigned long cf_mc_min_reclaimable_blocks;
};

#define NILFS_CLDCONFIG_SELECTION_POLICY_IMPORTANCE	\
			nilfs_cldconfig_selection_policy_timestamp
#define NILFS_CLDCONFIG_SELECTION_POLICY_THRESHOLD	0
#define NILFS_CLDCONFIG_PROTECTION_PERIOD		3600
#define NILFS_CLDCONFIG_MIN_CLEAN_SEGMENTS		10
#define NILFS_CLDCONFIG_MIN_CLEAN_SEGMENTS_UNIT		NILFS_SIZE_UNIT_PERCENT
#define NILFS_CLDCONFIG_MAX_CLEAN_SEGMENTS		20
#define NILFS_CLDCONFIG_MAX_CLEAN_SEGMENTS_UNIT		NILFS_SIZE_UNIT_PERCENT
#define NILFS_CLDCONFIG_CLEAN_CHECK_INTERVAL		10
#define NILFS_CLDCONFIG_NSEGMENTS_PER_CLEAN		2
#define NILFS_CLDCONFIG_MC_NSEGMENTS_PER_CLEAN		4
#define NILFS_CLDCONFIG_CLEANING_INTERVAL		5
#define NILFS_CLDCONFIG_MC_CLEANING_INTERVAL		1
#define NILFS_CLDCONFIG_RETRY_INTERVAL			60
#define NILFS_CLDCONFIG_USE_MMAP			1
#define NILFS_CLDCONFIG_USE_SET_SUINFO			0
#define NILFS_CLDCONFIG_LOG_PRIORITY			LOG_INFO
#define NILFS_CLDCONFIG_MIN_RECLAIMABLE_BLOCKS		10
#define NILFS_CLDCONFIG_MIN_RECLAIMABLE_BLOCKS_UNIT	NILFS_SIZE_UNIT_PERCENT
#define NILFS_CLDCONFIG_MC_MIN_RECLAIMABLE_BLOCKS	1
#define NILFS_CLDCONFIG_MC_MIN_RECLAIMABLE_BLOCKS_UNIT	NILFS_SIZE_UNIT_PERCENT

#define NILFS_CLDCONFIG_NSEGMENTS_PER_CLEAN_MAX	32

struct nilfs;

int nilfs_cldconfig_read(struct nilfs_cldconfig *config, const char *path,
			 struct nilfs *nilfs);

#endif	/* CLDCONFIG_H */
