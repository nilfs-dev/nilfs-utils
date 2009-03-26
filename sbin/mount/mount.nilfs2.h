/*
 * mount.nilfs2.h - NILFS mount, declarations
 */

#ifndef _MOUNT_NILFS2_H
#define _MOUNT_NILFS2_H

#include <sys/types.h>
#include <signal.h>
#include <stdio.h>
#include "sundries.h"

#define NILFS2_FS_NAME		"nilfs2"
#define CLEANERD_NAME		"nilfs_cleanerd"
#define PIDOPT_NAME		"gcpid"

#define CLEANERD_WAIT_RETRY_COUNT	3
#define CLEANERD_WAIT_RETRY_INTERVAL	2  /* in seconds */


extern int start_cleanerd(const char *, const char *, pid_t *);
extern int stop_cleanerd(const char *, pid_t);
extern int check_cleanerd(const char *, pid_t);

#endif /* _MOUNT_NILFS2_H */
