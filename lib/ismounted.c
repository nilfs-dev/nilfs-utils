/* check_mount() checks whether DEVICE is a mounted file system.
   Returns 0 if the DEVICE is *not* mounted (which we consider a
   successful outcome), and -1 if DEVICE is mounted or if the mount
   status cannot be determined.

   Derived from e2fsprogs/lib/ext2fs/ismounted.c
   Copyright (C) 1995,1996,1997,1998,1999,2000 Theodore Ts'o,
   LGPL v2
*/
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif	/* HAVE_CONFIG_H */

#include <stdio.h>

#if HAVE_STDLIB_H
#include <stdlib.h>
#endif	/* HAVE_STDLIB_H */

#if HAVE_STRING_H
#include <string.h>
#endif	/* HAVE_STRING_H */

#ifdef HAVE_MNTENT_H
#include <mntent.h>
#endif

#include <sys/stat.h>
#include "pathnames.h"


#define LINE_BUFFER_SIZE	256  /* Line buffer size for reading mtab */

int check_mount(const char *device)
{
	struct mntent *mnt;
	struct stat st_buf;
	FILE *f;
	dev_t file_dev = 0, file_rdev = 0;
	ino_t file_ino = 0;

	f = setmntent(_PATH_MOUNTED, "r");
	if (f == NULL) {
		fprintf(stderr, "Error: cannot open %s!", _PATH_MOUNTED);
		return -1;
	}

	if (stat(device, &st_buf) == 0) {
		if (S_ISBLK(st_buf.st_mode)) {
			file_rdev = st_buf.st_rdev;
		} else {
			file_dev = st_buf.st_dev;
			file_ino = st_buf.st_ino;
		}
	}

	while ((mnt = getmntent(f)) != NULL) {
		if (mnt->mnt_fsname[0] != '/')
			continue;
		if (strcmp(device, mnt->mnt_fsname) == 0)
			break;
		if (stat(mnt->mnt_fsname, &st_buf) == 0) {
			if (S_ISBLK(st_buf.st_mode)) {
				if (file_rdev && (file_rdev == st_buf.st_rdev))
					break;
			} else {
				if (file_dev && ((file_dev == st_buf.st_dev) &&
						 (file_ino == st_buf.st_ino)))
					break;
			}
		}
	}

	endmntent(f);
	return (mnt == NULL) ? 0 : -1;
}
