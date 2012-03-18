/*
 * cleaner_exec.h - old cleaner control routines
 *
 * Licensed under LGPLv2: the complete text of the GNU Lesser General
 * Public License can be found in COPYING file of the nilfs-utils
 * package.
 *
 * Copyright (C) 2007-2012 Nippon Telegraph and Telephone Corporation.
 */

#ifndef NILFS_CLEANER_EXEC_H
#define NILFS_CLEANER_EXEC_H

#include <sys/types.h>
#include <stdint.h>

#define NILFS_CLEANERD_NAME "nilfs_cleanerd"
#define PIDOPT_NAME "gcpid"

int nilfs_launch_cleanerd(const char *device, const char *mntdir,
			  unsigned long protperiod, pid_t *ppid);
int nilfs_ping_cleanerd(pid_t pid);
int nilfs_shutdown_cleanerd(const char *device, pid_t pid);

extern void (*nilfs_cleaner_logger)(int priority, const char *fmt, ...);
extern void (*nilfs_cleaner_printf)(const char *fmt, ...);
extern void (*nilfs_cleaner_flush)(void);

#endif /* NILFS_CLEANER_EXEC_H */
