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

#include <unistd.h>		/* sysconf() */
#include <sys/stat.h>
#include <fcntl.h>		/* O_* flags */
#include <errno.h>
#include <assert.h>

#include "pathnames.h"
#include "compat.h"		/* PATH_MAX, major(), minor() */
#include "util.h"		/* unlikely() */
#include "check_mount.h"


#define LINE_BUFFER_SIZE	256  /* Line buffer size for reading mtab */


/*
 * Pathname buffer size of loop device entry in sysfs
 *
 * Logically 16 + 10 + 10 + 1 = 37 bytes would be enough to store output in
 * the format "/sys/dev/block/%d:%d", but rounding up to the power of 2.
 */
#define SYSFS_LOOP_DIR_PATH_BUFSZ  64

/**
 * loop_get_backing_file - get the name of the loop device's backing file
 * @dev: device id of loop device
 * @buf: buffer to store device name
 * @bufsz: buffer size
 *
 * This function obtains the path name of the backing file for the loopback
 * device with device number @dev and stores it in @buf.
 *
 * Return: 0 if the backing file cannot be traced, the length of the buffered
 * path string if the backing file is found, -1 on error.
 */
static ssize_t loop_get_backing_file(dev_t dev, char *buf, size_t bufsz)
{
	char dir_path[SYSFS_LOOP_DIR_PATH_BUFSZ];
	int dir_fd, fd;
	ssize_t ret;

	assert(bufsz > 1);

	ret = snprintf(dir_path, SYSFS_LOOP_DIR_PATH_BUFSZ,
		       "/sys/dev/block/%u:%u", major(dev), minor(dev));
	if (unlikely(ret < 0))
		return -1;
	if (unlikely(ret >= SYSFS_LOOP_DIR_PATH_BUFSZ)) {
		errno = ENAMETOOLONG;
		return -1;
	}

	dir_fd = openat(AT_FDCWD, dir_path, O_RDONLY | O_CLOEXEC);
	if (unlikely(dir_fd < 0))
		return -1;

	fd = openat(dir_fd, "loop/backing_file", O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		if (errno == ENOENT) {
			buf[0] = '\0';
			ret = 0;  /* not found */
		} else {
			ret = -1; /* error */
		}
		goto out_close_dir;
	}

	ret = read(fd, buf, bufsz - 1);
	if (ret >= 0) {
		char *s;

		buf[ret] = '\0';

		/* Replace newline with a null character */
		s = strchrnul(buf, '\n');
		if (*s == '\n')
			*s = '\0';
		ret = s - buf;
	}
	close(fd);

out_close_dir:
	close(dir_fd);
	return ret;
}

/**
 * check_mount - determine if a block device or file is mounted
 * @device:  pathname of block device or disk image file
 *
 * Return: 1 if mounted, 0 if not, -1 if error.
 */
int check_mount(const char *device)
{
	struct mntent *mnt;
	struct stat st_buf;
	FILE *f;
	dev_t file_dev = 0, file_rdev = 0;
	ino_t file_ino = 0;
	long pagesize = sysconf(_SC_PAGE_SIZE);
	size_t path_buf_size = PATH_MAX + 1;
	char *path_buf;
	ssize_t len;

	/*
	 * Since the loop device source is obtained via sysfs, truncate
	 * the path buffer size to the same or smaller than the page size.
	 */
	if (pagesize > 0 && (size_t)pagesize < path_buf_size)
		path_buf_size = (size_t)pagesize;

	path_buf = malloc(path_buf_size);
	if (!path_buf)
		goto failed;

	f = setmntent(_PATH_MOUNTED, "r");
	if (!f)
		goto failed;

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
				len = loop_get_backing_file(
					st_buf.st_rdev, path_buf,
					path_buf_size);
				if (unlikely(len < 0))
					goto failed_endmntent;
				if (len > 0 &&
				    !strncmp(path_buf, device, path_buf_size))
					break;
			} else {
				if (file_dev && ((file_dev == st_buf.st_dev) &&
						 (file_ino == st_buf.st_ino)))
					break;
			}
		}
	}

	endmntent(f);
	free(path_buf);
	return (mnt == NULL) ? 0 : 1;

failed_endmntent:
	endmntent(f);
failed:
	free(path_buf);
	return -1;
}
