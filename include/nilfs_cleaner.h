/*
 * nilfs_cleaner.h - NILFS cleaner controller routines
 *
 * Licensed under LGPLv2: the complete text of the GNU Lesser General
 * Public License can be found in COPYING file of the nilfs-utils
 * package.
 *
 * Copyright (C) 2007-2011 Nippon Telegraph and Telephone Corporation.
 */

#ifndef NILFS_CLEANER_H
#define NILFS_CLEANER_H

#include <sys/types.h>
#include "nilfs.h"

#define NILFS_CLEANERD_NAME "nilfs_cleanerd"
#define PIDOPT_NAME "gcpid"

struct nilfs_cleaner;

#define NILFS_CLEANER_OPEN_GCPID	(1 << 0)

struct nilfs_cleaner *nilfs_cleaner_launch(const char *device,
					   const char *mntdir,
					   unsigned long protperiod);
struct nilfs_cleaner *nilfs_cleaner_open(const char *device,
					 const char *mntdir, int oflag);

int nilfs_cleaner_ping(struct nilfs_cleaner *cleaner);

pid_t nilfs_cleaner_pid(const struct nilfs_cleaner *cleaner);
const char *nilfs_cleaner_device(const struct nilfs_cleaner *cleaner);

void nilfs_cleaner_close(struct nilfs_cleaner *cleaner);
int nilfs_cleaner_shutdown(struct nilfs_cleaner *cleaner);

/* old interface for mount.nilfs2 and umount.nilfs2 */
int nilfs_launch_cleanerd(const char *device, const char *mntdir,
			  unsigned long protperiod, pid_t *ppid);
int nilfs_ping_cleanerd(pid_t pid);
int nilfs_shutdown_cleanerd(const char *device, pid_t pid);

extern void (*nilfs_cleaner_logger)(int priority, const char *fmt, ...);
extern void (*nilfs_cleaner_printf)(const char *fmt, ...);
extern void (*nilfs_cleaner_flush)(void);

#endif /* NILFS_CLEANER_H */
