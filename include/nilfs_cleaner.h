/*
 * nilfs_cleaner.h - NILFS cleaner controller routines
 *
 * Licensed under LGPLv2: the complete text of the GNU Lesser General
 * Public License can be found in COPYING file of the nilfs-utils
 * package.
 *
 * Copyright (C) 2007-2012 Nippon Telegraph and Telephone Corporation.
 */

#ifndef NILFS_CLEANER_H
#define NILFS_CLEANER_H

#include <sys/types.h>
#include <stdint.h>
#include "nilfs.h"

struct nilfs_cleaner;

#define NILFS_CLEANER_OPEN_GCPID	(1 << 0)
#define NILFS_CLEANER_OPEN_QUEUE	(1 << 1)

struct nilfs_cleaner *nilfs_cleaner_launch(const char *device,
					   const char *mntdir,
					   unsigned long protperiod);
struct nilfs_cleaner *nilfs_cleaner_open(const char *device,
					 const char *mntdir, int oflag);

int nilfs_cleaner_ping(struct nilfs_cleaner *cleaner);

pid_t nilfs_cleaner_pid(const struct nilfs_cleaner *cleaner);
const char *nilfs_cleaner_device(const struct nilfs_cleaner *cleaner);

void nilfs_cleaner_close(struct nilfs_cleaner *cleaner);

/* cleaner command arguments */
struct nilfs_cleaner_args {
	uint16_t valid;
	uint16_t npasses; /* number of passes */
	uint16_t usage_rate_threshold;
	uint16_t nsegments_per_clean;
	uint8_t pad;
	uint8_t min_reclaimable_blocks_unit;
	uint16_t cleaning_interval;
	uint32_t cleaning_interval_nsec;
	uint64_t protection_period; /* protection period in seconds */
	uint64_t start_segnum;	/* start segment number */
	uint64_t nsegs;		/* number of segments */
	uint32_t runtime; /* runtime in seconds */
	uint32_t min_reclaimable_blocks;
};

enum nilfs_cleaner_args_unit {
	NILFS_CLEANER_ARG_UNIT_NONE = 0,
	NILFS_CLEANER_ARG_UNIT_PERCENT,
	NILFS_CLEANER_ARG_UNIT_KB,     /* kilo-byte (kB) */
	NILFS_CLEANER_ARG_UNIT_KIB,    /* kibi-byte (KiB) */
	NILFS_CLEANER_ARG_UNIT_MB,     /* mega-byte (MB) */
	NILFS_CLEANER_ARG_UNIT_MIB,    /* mebi-byte (MiB) */
	NILFS_CLEANER_ARG_UNIT_GB,     /* giga-byte (GB) */
	NILFS_CLEANER_ARG_UNIT_GIB,    /* gibi-byte (GiB) */
	NILFS_CLEANER_ARG_UNIT_TB,     /* tera-byte (TB) */
	NILFS_CLEANER_ARG_UNIT_TIB,    /* tebi-byte (TiB) */
	NILFS_CLEANER_ARG_UNIT_PB,     /* peta-byte (PB) */
	NILFS_CLEANER_ARG_UNIT_PIB,    /* pebi-byte (PiB) */
	NILFS_CLEANER_ARG_UNIT_EB,     /* exa-byte (EB) */
	NILFS_CLEANER_ARG_UNIT_EIB,    /* exbi-byte (EiB) */

	NILFS_CLEANER_ARG_MIN_BINARY_SUFFIX = NILFS_CLEANER_ARG_UNIT_KB,
	NILFS_CLEANER_ARG_MAX_BINARY_SUFFIX = NILFS_CLEANER_ARG_UNIT_EIB,
};

/* valid flags */
#define NILFS_CLEANER_ARG_PROTECTION_PERIOD		(1 << 0)
#define NILFS_CLEANER_ARG_NSEGMENTS_PER_CLEAN		(1 << 1)
#define NILFS_CLEANER_ARG_CLEANING_INTERVAL		(1 << 2)
#define NILFS_CLEANER_ARG_USAGE_RATE_THRESHOLD		(1 << 3) /* reserved */
#define NILFS_CLEANER_ARG_START_SEGNUM			(1 << 4) /* reserved */
#define NILFS_CLEANER_ARG_NSEGS				(1 << 5) /* reserved */
#define NILFS_CLEANER_ARG_NPASSES			(1 << 6) /* reserved */
#define NILFS_CLEANER_ARG_RUNTIME			(1 << 7) /* reserved */
#define NILFS_CLEANER_ARG_MIN_RECLAIMABLE_BLOCKS	(1 << 8)

enum {
	NILFS_CLEANER_STATUS_IDLE,
	NILFS_CLEANER_STATUS_RUNNING,
	NILFS_CLEANER_STATUS_SUSPENDED,
};

int nilfs_cleaner_get_status(struct nilfs_cleaner *cleaner, int *status);
int nilfs_cleaner_run(struct nilfs_cleaner *cleaner,
		      const struct nilfs_cleaner_args *args, uint32_t *jobid);
int nilfs_cleaner_suspend(struct nilfs_cleaner *cleaner);
int nilfs_cleaner_resume(struct nilfs_cleaner *cleaner);
int nilfs_cleaner_tune(struct nilfs_cleaner *cleaner,
		       const struct nilfs_cleaner_args *args);
int nilfs_cleaner_reload(struct nilfs_cleaner *cleaner, const char *conffile);
int nilfs_cleaner_wait(struct nilfs_cleaner *cleaner, uint32_t jobid,
		       const struct timespec *abs_timeout);
int nilfs_cleaner_wait_r(struct nilfs_cleaner *cleaner, uint32_t jobid,
			 const struct timespec *timeout);
int nilfs_cleaner_stop(struct nilfs_cleaner *cleaner);
int nilfs_cleaner_shutdown(struct nilfs_cleaner *cleaner);

extern void (*nilfs_cleaner_logger)(int priority, const char *fmt, ...);
extern void (*nilfs_cleaner_printf)(const char *fmt, ...);
extern void (*nilfs_cleaner_flush)(void);

#endif /* NILFS_CLEANER_H */
