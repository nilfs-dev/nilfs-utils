/*
 * paths.h - local declarations of paths
 *
 * Code borrowed from util-linux-2.12r/mount/paths.h
 *
 * modified by Ryusuke Konishi <konishi.ryusuke@lab.ntt.co.jp>
 */

#ifndef _MY_PATHS_H
#define _MY_PATHS_H

#include <paths.h>
#include <mntent.h>

#ifndef _PATH_FSTAB
#define _PATH_FSTAB	"/etc/fstab"
#endif

#ifdef _PATH_MOUNTED
#define	MOUNTED		_PATH_MOUNTED
#define MOUNTED_LOCK	_PATH_MOUNTED "~"
#define MOUNTED_TEMP	_PATH_MOUNTED ".tmp"
#else
#define	MOUNTED		"/etc/mtab"
#define MOUNTED_LOCK	"/etc/mtab~"
#define MOUNTED_TEMP	"/etc/mtab.tmp"
#endif
#define LOCK_TIMEOUT	10

#endif /* _MY_PATHS_H */
