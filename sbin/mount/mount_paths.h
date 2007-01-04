/*
 * paths.h - local declarations of paths
 *
 * Code borrowed from util-linux-2.12r/mount/paths.h
 *
 * modified by Ryusuke Konishi <konishi.ryusuke@lab.ntt.co.jp>
 */

#ifndef MOUNT_PATHS_H
#define MOUNT_PATHS_H

#include <mntent.h>
#define _PATH_FSTAB	"/etc/fstab"
#ifdef _PATH_MOUNTED
#define MOUNTED_LOCK	_PATH_MOUNTED "~"
#define MOUNTED_TEMP	_PATH_MOUNTED ".tmp"
#else
#define MOUNTED_LOCK	"/etc/mtab~"
#define MOUNTED_TEMP	"/etc/mtab.tmp"
#endif
#define LOCK_TIMEOUT	10

#endif /* MOUNT_PATHS_H */
