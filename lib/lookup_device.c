/*
 * lookup_device.c - device lookup helper
 *
 * Licensed under LGPLv2: the complete text of the GNU Lesser General
 * Public License can be found in COPYING file of the nilfs-utils
 * package.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif	/* HAVE_CONFIG_H */

#include <stdio.h>

#if HAVE_STDLIB_H
#include <stdlib.h>
#endif	/* HAVE_STDLIB_H */

#if HAVE_STRING_H
#include <string.h>		/* strrchr(), strdup() */
#endif	/* HAVE_STRING_H */

#include <unistd.h>		/* sysconf() */
#include <sys/stat.h>
#include <errno.h>

#include "compat.h"		/* PATH_MAX, major(), minor() */
#include "util.h"		/* unlikely() */
#include "lookup_device.h"	/* nilfs_lookup_device() */

/*
 * Pathname buffer size of block device entry in sysfs
 *
 * Logically 16 + 10 + 10 + 1 = 37 bytes would be enough to store output in
 * the format "/sys/dev/block/%u:%u", but rounding up to the power of 2.
 */
#define SYSFS_BLOCK_DIR_PATH_BUFSZ  64

/**
 * nilfs_get_devname_by_devid - retrieve device name from sysfs
 * @dev: device ID (dev_t)
 * @buf: buffer to store the device path (e.g., "/dev/sda1")
 * @bufsz: size of the buffer
 *
 * Description: Resolves the kernel device name for the given device ID by
 * reading the symlink at /sys/dev/block/<maj>:<min>.
 *
 * On success, the string stored in @buf is guaranteed to be null-terminated.
 *
 * Return: The length of the device path on success, or -1 on failure.
 */
static ssize_t nilfs_get_devname_by_devid(dev_t dev, char *buf, size_t bufsz)
{
	char syspath[SYSFS_BLOCK_DIR_PATH_BUFSZ];
	char target[PATH_MAX + 1];
	char *devname;
	ssize_t ret, link_len;

	/* Construct sysfs path: /sys/dev/block/M:m */
	ret = snprintf(syspath, sizeof(syspath),
		       "/sys/dev/block/%u:%u", major(dev), minor(dev));
	if (unlikely(ret < 0))
		return -1;

	if (unlikely(ret >= sizeof(syspath))) {
		errno = ENAMETOOLONG;
		return -1;
	}

	/* Read the symlink */
	link_len = readlink(syspath, target, sizeof(target) - 1);
	if (unlikely(link_len < 0)) {
		if (errno == ENOENT)
			errno = ENODEV;
		return -1;
	}
	target[link_len] = '\0';

	/*
	 * The link points to the device directory in sysfs.  The last
	 * component of the path is the kernel device name.
	 */
	devname = strrchr(target, '/');
	if (unlikely(!devname)) {
		errno = ENODEV;
		return -1;
	}
	devname++; /* Skip the slash */

	ret = snprintf(buf, bufsz, "/dev/%s", devname);
	if (unlikely(ret < 0))
		return -1;

	if (unlikely(ret >= bufsz)) {
		errno = ENAMETOOLONG;
		return -1;
	}
	return ret;
}

/**
 * nilfs_lookup_device - find the block device associated with a filesystem
 *                       node
 * @node: path to the file system node (file, directory, etc.)
 * @devpath: pointer to a string pointer to store the result
 *
 * Description: This function identifies the block device on which the file
 * system node specified by @node resides.
 *
 * If @node itself is a block device, the function returns 0 and does nothing.
 * Otherwise, it resolves the device name from the device ID of @node using
 * sysfs.  On success, a newly allocated string containing the device path
 * (e.g. "/dev/sda1") is stored in @devpath, and the function returns 1.
 * The caller is responsible for freeing the memory pointed to by @devpath.
 *
 * Return: 1 on success, 0 if @node is a block device, or -1 on failure.
 */
int nilfs_lookup_device(const char *node, char **devpath)
{
	char pathbuf[PATH_MAX + 1];
	struct stat st;
	char *dev;
	ssize_t ret;

	ret = stat(node, &st);
	if (unlikely(ret < 0))
		return -1;

	if (S_ISBLK(st.st_mode))
		return 0;

	ret = nilfs_get_devname_by_devid(st.st_dev, pathbuf, PATH_MAX);
	if (unlikely(ret < 0))
		return -1;

	dev = strdup(pathbuf);
	if (unlikely(!dev))
		return -1;

	/* Caller must free '*devpath' */
	*devpath = dev;
	return 1;
}
