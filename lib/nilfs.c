/*
 * nilfs.c - NILFS library.
 *
 * Copyright (C) 2007-2012 Nippon Telegraph and Telephone Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * Credits:
 *    Koji Sato
 *    Ryusuke Konishi <konishi.ryusuke@lab.ntt.co.jp>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif	/* HAVE_CONFIG_H */

#include <stdio.h>

#if HAVE_STDLIB_H
#include <stdlib.h>
#endif	/* HAVE_STDLIB_H */

#if HAVE_UNISTD_H
#include <unistd.h>
#endif	/* HAVE_UNISTD_H */

#include <ctype.h>

#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif	/* HAVE_SYS_TYPES_H */

#if HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif	/* HAVE_SYS_STAT_H */

#if HAVE_STRING_H
#include <string.h>
#endif	/* HAVE_STRING_H */

#if HAVE_FCNTL_H
#include <fcntl.h>
#endif	/* HAVE_FCNTL_H */

#if HAVE_LIMITS_H
#include <limits.h>
#endif	/* HAVE_LIMITS_H */

#if HAVE_SYS_MMAN_H
#include <sys/mman.h>
#endif	/* HAVE_SYS_MMAN_H */

#if HAVE_TIME_H
#include <time.h>
#endif	/* HAVE_TIME_H */

#if HAVE_LINUX_TYPES_H
#include <linux/types.h>
#endif	/* HAVE_LINUX_TYPES_H */

#include <errno.h>
#include <assert.h>
#include "nilfs.h"
#include "compat.h"
#include "nilfs2_ondisk.h"
#include "util.h"
#include "pathnames.h"
#include "realpath.h"

static inline int iseol(int c)
{
	return c == '\n' || c == '\0';
}

static size_t tokenize(char *line, char **tokens, size_t ntoks)
{
	char *p;
	size_t n;

	p = line;
	for (n = 0; n < ntoks; n++) {
		while (isspace(*p))
			p++;
		if (iseol(*p))
			break;
		tokens[n] = p++;
		while (!isspace(*p) && !iseol(*p))
			p++;
		if (isspace(*p))
			*p++ = '\0';
		else
			*p = '\0';
	}
	return n;
}

#define NMNTFLDS	6
#define MNTFLD_FS	0
#define MNTFLD_DIR	1
#define MNTFLD_TYPE	2
#define MNTFLD_OPTS	3
#define MNTFLD_FREQ	4
#define MNTFLD_PASSNO	5
#define MNTOPT_RW	"rw"
#define MNTOPT_RO	"ro"
#define MNTOPT_SEP	','

static int has_mntopt(const char *opts, const char *opt)
{
	const char *p, *q;
	size_t len, n;

	p = opts;
	len = strlen(opt);
	while (p != NULL) {
		q = strchr(p, MNTOPT_SEP);
		if (q) {
			n = max_t(size_t, q - p, len);
			q++;
		} else {
			n = max_t(size_t, strlen(p), len);
		}
		if (strncmp(p, opt, n) == 0)
			return 1;
		p = q;
	}
	return 0;
}

#ifndef LINE_MAX
#define LINE_MAX	2048
#endif	/* LINE_MAX */

static int nilfs_find_fs(struct nilfs *nilfs, const char *dev, const char *dir,
			 const char *opt)
{
	FILE *fp;
	char line[LINE_MAX], *mntent[NMNTFLDS];
	int ret, n;
	char canonical[PATH_MAX + 2];
	char *cdev = NULL, *cdir = NULL;
	char *mdev, *mdir;

	ret = -1;
	if (dev && myrealpath(dev, canonical, sizeof(canonical))) {
		cdev = strdup(canonical);
		if (unlikely(!cdev))
			goto failed;
		dev = cdev;
	}

	if (dir && myrealpath(dir, canonical, sizeof(canonical))) {
		cdir = strdup(canonical);
		if (unlikely(!cdir))
			goto failed_dev;
		dir = cdir;
	}

	fp = fopen(_PATH_PROC_MOUNTS, "r");
	if (unlikely(fp == NULL))
		goto failed_dir;

	while (fgets(line, sizeof(line), fp) != NULL) {
		n = tokenize(line, mntent, NMNTFLDS);
		assert(n == NMNTFLDS);

		if (strcmp(mntent[MNTFLD_TYPE], NILFS_FSTYPE) != 0)
			continue;

		if (dir != NULL) {
			mdir = mntent[MNTFLD_DIR];
			if (myrealpath(mdir, canonical, sizeof(canonical)))
				mdir = canonical;
			if (strcmp(mdir, dir) != 0)
				continue;
		}

		if (dev != NULL) {
			mdev = mntent[MNTFLD_FS];
			if (myrealpath(mdev, canonical, sizeof(canonical)))
				mdev = canonical;
			if (strcmp(mdev, dev) != 0)
				continue;
		}

		if (has_mntopt(mntent[MNTFLD_OPTS], opt)) {
			nilfs->n_dev = strdup(mntent[MNTFLD_FS]);
			if (unlikely(nilfs->n_dev == NULL))
				goto failed_proc_mounts;
			nilfs->n_ioc = strdup(mntent[MNTFLD_DIR]);
			if (unlikely(nilfs->n_ioc == NULL)) {
				free(nilfs->n_dev);
				nilfs->n_dev = NULL;
				goto failed_proc_mounts;
			}
			ret = 0;
			break;
		}
	}
	if (ret < 0)
		errno = ENOENT;

 failed_proc_mounts:
	fclose(fp);

 failed_dir:
	free(cdir);

 failed_dev:
	free(cdev);

 failed:
	return ret;
}

/**
 * nilfs_get_layout - get layout information of nilfs
 * @nilfs: nilfs object
 * @layout: buffer to nilfs_layout struct
 * @layout_size: size of layout structure (used to ensure compatibility)
 */
ssize_t nilfs_get_layout(const struct nilfs *nilfs,
			 struct nilfs_layout *layout, size_t layout_size)
{
	const struct nilfs_super_block *sb = nilfs->n_sb;

	if (unlikely(layout_size < 0x50)) {
		errno = EINVAL;
		return -1;
	}

	if (unlikely(sb == NULL)) {
		errno = EPERM;
		return -1;
	}

	layout->rev_level = le32_to_cpu(sb->s_rev_level);
	layout->minor_rev_level = le16_to_cpu(sb->s_minor_rev_level);
	layout->flags = le16_to_cpu(sb->s_flags);
	layout->blocksize_bits = le32_to_cpu(sb->s_log_block_size) + 10;
	layout->blocksize = 1UL << layout->blocksize_bits;
	layout->devsize = le64_to_cpu(sb->s_dev_size);
	layout->crc_seed = le32_to_cpu(sb->s_crc_seed);
	layout->pad = 0;
	layout->nsegments = le64_to_cpu(sb->s_nsegments);
	layout->blocks_per_segment = le32_to_cpu(sb->s_blocks_per_segment);
	layout->reserved_segments_ratio =
		le32_to_cpu(sb->s_r_segments_percentage);
	layout->first_segment_blkoff = le64_to_cpu(sb->s_first_data_block);
	layout->feature_compat = le64_to_cpu(sb->s_feature_compat);
	layout->feature_compat_ro = le64_to_cpu(sb->s_feature_compat_ro);
	layout->feature_incompat = le64_to_cpu(sb->s_feature_incompat);

	return sizeof(struct nilfs_layout);
}

/**
 * nilfs_get_block_size - get block size of the file system
 * @nilfs: nilfs object
 */
size_t nilfs_get_block_size(const struct nilfs *nilfs)
{
	assert(nilfs->n_sb != NULL);
	return 1UL << (le32_to_cpu(nilfs->n_sb->s_log_block_size) + 10);
}

/**
 * nilfs_get_nsegments - get number of segments
 * @nilfs: nilfs object
 */
__u64 nilfs_get_nsegments(const struct nilfs *nilfs)
{
	assert(nilfs->n_sb != NULL);
	return le64_to_cpu(nilfs->n_sb->s_nsegments);
}

/**
 * nilfs_get_blocks_per_segment - get number of blocks per segment
 * @nilfs: nilfs object
 */
__u32 nilfs_get_blocks_per_segment(const struct nilfs *nilfs)
{
	assert(nilfs->n_sb != NULL);
	return le32_to_cpu(nilfs->n_sb->s_blocks_per_segment);
}

/**
 * nilfs_get_reserved_segments_ratio - get ratio of reserved segments
 * @nilfs: nilfs object
 */
__u32 nilfs_get_reserved_segments_ratio(const struct nilfs *nilfs)
{
	assert(nilfs->n_sb != NULL);
	return le32_to_cpu(nilfs->n_sb->s_r_segments_percentage);
}

/**
 * nilfs_opt_set_mmap - set mmap option
 * @nilfs: nilfs object
 */
int nilfs_opt_set_mmap(struct nilfs *nilfs)
{
	long pagesize;
	size_t segsize;

	pagesize = sysconf(_SC_PAGESIZE);
	if (unlikely(pagesize < 0))
		return -1;
	segsize = nilfs_get_blocks_per_segment(nilfs) *
		nilfs_get_block_size(nilfs);
	if (segsize % pagesize != 0)
		return -1;

	nilfs->n_opts |= NILFS_OPT_MMAP;
	return 0;
}

/**
 * nilfs_opt_clear_mmap - clear mmap option
 * @nilfs: nilfs object
 */
void nilfs_opt_clear_mmap(struct nilfs *nilfs)
{
	nilfs->n_opts &= ~NILFS_OPT_MMAP;
}

/**
 * nilfs_opt_test_mmap - test whether mmap option is set or not
 * @nilfs: nilfs object
 */
int nilfs_opt_test_mmap(struct nilfs *nilfs)
{
	return !!(nilfs->n_opts & NILFS_OPT_MMAP);
}

/**
 * nilfs_opt_set_set_suinfo - set set_suinfo option
 * @nilfs: nilfs object
 */
int nilfs_opt_set_set_suinfo(struct nilfs *nilfs)
{
	nilfs->n_opts |= NILFS_OPT_SET_SUINFO;
	return 0;
}

/**
 * nilfs_opt_clear_set_suinfo - clear set_suinfo option
 * @nilfs: nilfs object
 */
void nilfs_opt_clear_set_suinfo(struct nilfs *nilfs)
{
	nilfs->n_opts &= ~NILFS_OPT_SET_SUINFO;
}

/**
 * nilfs_opt_test_set_suinfo - test whether set_suinfo option is set or not
 * @nilfs: nilfs object
 */
int nilfs_opt_test_set_suinfo(struct nilfs *nilfs)
{
	return !!(nilfs->n_opts & NILFS_OPT_SET_SUINFO);
}

static int nilfs_open_sem(struct nilfs *nilfs)
{
	char semnambuf[NAME_MAX - 4];
	struct stat stbuf;
	int ret;

	ret = stat(nilfs->n_dev, &stbuf);
	if (unlikely(ret < 0))
		return -1;

	if (S_ISBLK(stbuf.st_mode)) {
		ret = snprintf(semnambuf, sizeof(semnambuf),
			       "/nilfs-cleaner-%llu",
			       (unsigned long long)stbuf.st_rdev);
	} else if (S_ISREG(stbuf.st_mode) || S_ISDIR(stbuf.st_mode)) {
		ret = snprintf(semnambuf, sizeof(semnambuf),
			       "/nilfs-cleaner-%llu-%llu",
			       (unsigned long long)stbuf.st_dev,
			       (unsigned long long)stbuf.st_ino);
	} else {
		errno = EINVAL;
		return -1;
	}

	if (unlikely(ret < 0))
		return -1;

	assert(ret < sizeof(semnambuf));

	nilfs->n_sems[0] = sem_open(semnambuf, O_CREAT, S_IRWXU, 1);
	if (unlikely(nilfs->n_sems[0] == SEM_FAILED)) {
		nilfs->n_sems[0] = NULL;
		return -1;
	}
	return 0;
}

/**
 * nilfs_open - create a NILFS object
 * @dev: device
 * @dir: mount directory
 * @flags: open flags
 */
struct nilfs *nilfs_open(const char *dev, const char *dir, int flags)
{
	struct nilfs *nilfs;
	__u64 features;
	int ret;

	if (unlikely(!(flags & (NILFS_OPEN_RAW | NILFS_OPEN_RDONLY |
				NILFS_OPEN_WRONLY | NILFS_OPEN_RDWR)))) {
		errno = EINVAL;
		return NULL;
	}

	nilfs = malloc(sizeof(*nilfs));
	if (unlikely(nilfs == NULL))
		return NULL;

	nilfs->n_sb = NULL;
	nilfs->n_devfd = -1;
	nilfs->n_iocfd = -1;
	nilfs->n_dev = NULL;
	nilfs->n_ioc = NULL;
	nilfs->n_opts = 0;
	nilfs->n_mincno = NILFS_CNO_MIN;
	memset(nilfs->n_sems, 0, sizeof(nilfs->n_sems));

	if (flags & NILFS_OPEN_RAW) {
		if (dev == NULL) {
			if (nilfs_find_fs(nilfs, dev, dir, MNTOPT_RW) < 0)
				goto out_fd;
		} else {
			nilfs->n_dev = strdup(dev);
			if (unlikely(nilfs->n_dev == NULL))
				goto out_fd;
		}
		nilfs->n_devfd = open(nilfs->n_dev, O_RDONLY);
		if (unlikely(nilfs->n_devfd < 0))
			goto out_fd;

		nilfs->n_sb = nilfs_sb_read(nilfs->n_devfd);
		if (unlikely(nilfs->n_sb == NULL))
			goto out_fd;

		features = le64_to_cpu(nilfs->n_sb->s_feature_incompat) &
			~NILFS_FEATURE_INCOMPAT_SUPP;
		if (unlikely(features)) {
			errno = ENOTSUP;
			goto out_fd;
		}
	}

	if (flags &
	    (NILFS_OPEN_RDONLY | NILFS_OPEN_WRONLY | NILFS_OPEN_RDWR)) {
		if (nilfs_find_fs(nilfs, dev, dir, MNTOPT_RW) < 0) {
			if (!(flags & NILFS_OPEN_RDONLY))
				goto out_fd;
			if (nilfs_find_fs(nilfs, dev, dir, MNTOPT_RO) < 0)
				goto out_fd;
		}
		nilfs->n_iocfd = open(nilfs->n_ioc, O_RDONLY);
		if (unlikely(nilfs->n_iocfd < 0))
			goto out_fd;
	}

	if (flags & NILFS_OPEN_GCLK) {
		/* Initialize cleaner semaphore */
		ret = nilfs_open_sem(nilfs);
		if (unlikely(ret < 0))
			goto out_fd;
	}

	/* success */
	return nilfs;

	/* error */
out_fd:
	if (nilfs->n_devfd >= 0)
		close(nilfs->n_devfd);
	if (nilfs->n_iocfd >= 0)
		close(nilfs->n_iocfd);

	free(nilfs->n_dev);
	free(nilfs->n_ioc);
	free(nilfs->n_sb);
	free(nilfs);
	return NULL;
}

/**
 * nilfs_close - destroy a NILFS object
 * @nilfs: NILFS object
 */
void nilfs_close(struct nilfs *nilfs)
{
	if (nilfs->n_sems[0] != NULL)
		sem_close(nilfs->n_sems[0]);
	if (nilfs->n_devfd >= 0)
		close(nilfs->n_devfd);
	if (nilfs->n_iocfd >= 0)
		close(nilfs->n_iocfd);

	free(nilfs->n_dev);
	free(nilfs->n_ioc);
	free(nilfs->n_sb);
	free(nilfs);
}

/**
 * nilfs_get_dev - get the name of a device that nilfs object is opening
 * @nilfs: nilfs object
 */
const char *nilfs_get_dev(const struct nilfs *nilfs)
{
	return nilfs->n_dev;
}

/**
 * nilfs_change_cpmode - change mode of a checkpoint
 * @nilfs: nilfs object
 * @cno: checkpoint number
 * @mode: new mode of checkpoint
 */
int nilfs_change_cpmode(struct nilfs *nilfs, nilfs_cno_t cno, int mode)
{
	struct nilfs_cpmode cpmode;

	if (unlikely(nilfs->n_iocfd < 0)) {
		errno = EBADF;
		return -1;
	}
	if (unlikely(cno < NILFS_CNO_MIN)) {
		errno = EINVAL;
		return -1;
	}

	cpmode.cm_cno = cno;
	cpmode.cm_mode = mode;
	cpmode.cm_pad = 0;
	return ioctl(nilfs->n_iocfd, NILFS_IOCTL_CHANGE_CPMODE, &cpmode);
}

/**
 * nilfs_get_cpinfo - get information of checkpoints
 * @nilfs: nilfs object
 * @cno: start checkpoint number
 * @mode: mode of checkpoints that the caller wants to retrieve
 * @cpinfo: array of nilfs_cpinfo structs to store information in
 * @nci: size of @cpinfo array (number of items)
 */
ssize_t nilfs_get_cpinfo(struct nilfs *nilfs, nilfs_cno_t cno, int mode,
			 struct nilfs_cpinfo *cpinfo, size_t nci)
{
	struct nilfs_argv argv;
	int ret;

	if (unlikely(nilfs->n_iocfd < 0)) {
		errno = EBADF;
		return -1;
	}
	if (mode == NILFS_CHECKPOINT) {
		if (unlikely(cno < NILFS_CNO_MIN)) {
			errno = EINVAL;
			return -1;
		} else if (cno < nilfs->n_mincno)
			cno = nilfs->n_mincno;
	}

	argv.v_base = (unsigned long)cpinfo;
	argv.v_nmembs = nci;
	argv.v_size = sizeof(struct nilfs_cpinfo);
	argv.v_index = cno;
	argv.v_flags = mode;
	ret = ioctl(nilfs->n_iocfd, NILFS_IOCTL_GET_CPINFO, &argv);
	if (unlikely(ret < 0))
		return -1;
	if (mode == NILFS_CHECKPOINT && argv.v_nmembs > 0 &&
	    cno == nilfs->n_mincno) {
		if (cpinfo[0].ci_cno > nilfs->n_mincno)
			nilfs->n_mincno = cpinfo[0].ci_cno;
	}
	return argv.v_nmembs;
}

/**
 * nilfs_delete_checkpoint - delete a checkpoint
 * @nilfs: nilfs object
 * @cno: checkpoint number to be deleted
 */
int nilfs_delete_checkpoint(struct nilfs *nilfs, nilfs_cno_t cno)
{
	if (unlikely(nilfs->n_iocfd < 0)) {
		errno = EBADF;
		return -1;
	}
	return ioctl(nilfs->n_iocfd, NILFS_IOCTL_DELETE_CHECKPOINT, &cno);
}

/**
 * nilfs_get_cpstat - get checkpoint statistics
 * @nilfs: nilfs object
 * @cpstat: buffer of a nilfs_cpstat struct to store statistics in
 */
int nilfs_get_cpstat(const struct nilfs *nilfs, struct nilfs_cpstat *cpstat)
{
	if (unlikely(nilfs->n_iocfd < 0)) {
		errno = EBADF;
		return -1;
	}
	return ioctl(nilfs->n_iocfd, NILFS_IOCTL_GET_CPSTAT, cpstat);
}

/**
 * nilfs_get_suinfo - get information of segment usage
 * @nilfs: nilfs object
 * @segnum: start segment number
 * @si: array of nilfs_suinfo structs to store information in
 * @nsi: size of @si array (number of items)
 */
ssize_t nilfs_get_suinfo(const struct nilfs *nilfs, __u64 segnum,
			 struct nilfs_suinfo *si, size_t nsi)
{
	struct nilfs_argv argv;
	int ret;

	if (unlikely(nilfs->n_iocfd < 0)) {
		errno = EBADF;
		return -1;
	}

	argv.v_base = (unsigned long)si;
	argv.v_nmembs = nsi;
	argv.v_size = sizeof(struct nilfs_suinfo);
	argv.v_flags = 0;
	argv.v_index = segnum;
	ret = ioctl(nilfs->n_iocfd, NILFS_IOCTL_GET_SUINFO, &argv);
	if (unlikely(ret < 0))
		return -1;
	return argv.v_nmembs;
}

/**
 * nilfs_set_suinfo - sets segment usage info
 * @nilfs: nilfs object
 * @sup: an array of nilfs_suinfo_update structs
 * @nsup: number of elements in sup
 *
 * Description: Takes an array of nilfs_suinfo_update structs and updates
 * segment usage info accordingly. Only the fields indicated by sup_flags
 * are updated.
 *
 * Return Value: On success, 0 is returned. On error, -1 is returned.
 */
int nilfs_set_suinfo(const struct nilfs *nilfs,
		     struct nilfs_suinfo_update *sup, size_t nsup)
{
	struct nilfs_argv argv;

	if (unlikely(nilfs->n_iocfd < 0)) {
		errno = EBADF;
		return -1;
	}

	argv.v_base = (unsigned long)sup;
	argv.v_nmembs = nsup;
	argv.v_size = sizeof(struct nilfs_suinfo_update);
	argv.v_index = 0;
	argv.v_flags = 0;

	return ioctl(nilfs->n_iocfd, NILFS_IOCTL_SET_SUINFO, &argv);
}

/**
 * nilfs_get_sustat - get segment usage statistics
 * @nilfs: nilfs object
 * @sustat: buffer of a nilfs_sustat struct to store statistics in
 */
int nilfs_get_sustat(const struct nilfs *nilfs, struct nilfs_sustat *sustat)
{
	if (unlikely(nilfs->n_iocfd < 0)) {
		errno = EBADF;
		return -1;
	}

	return ioctl(nilfs->n_iocfd, NILFS_IOCTL_GET_SUSTAT, sustat);
}

/**
 * nilfs_get_vinfo - get information of virtual block addresses
 * @nilfs: nilfs object
 * @vinfo: array of nilfs_vinfo structs to store information in
 * @nvi: size of @vinfo array (number of items)
 */
ssize_t nilfs_get_vinfo(const struct nilfs *nilfs,
			struct nilfs_vinfo *vinfo, size_t nvi)
{
	struct nilfs_argv argv;
	int ret;

	if (unlikely(nilfs->n_iocfd < 0)) {
		errno = EBADF;
		return -1;
	}

	argv.v_base = (unsigned long)vinfo;
	argv.v_nmembs = nvi;
	argv.v_size = sizeof(struct nilfs_vinfo);
	argv.v_flags = 0;
	argv.v_index = 0;
	ret = ioctl(nilfs->n_iocfd, NILFS_IOCTL_GET_VINFO, &argv);
	if (unlikely(ret < 0))
		return -1;
	return argv.v_nmembs;
}

/**
 * nilfs_get_bdescs - get information of blocks used in DAT file itself
 * @nilfs: nilfs object
 * @bdescs: array of nilfs_bdesc structs to store information in
 * @nbdescs: size of @bdescs array (number of items)
 */
ssize_t nilfs_get_bdescs(const struct nilfs *nilfs,
			 struct nilfs_bdesc *bdescs, size_t nbdescs)
{
	struct nilfs_argv argv;
	int ret;

	if (unlikely(nilfs->n_iocfd < 0)) {
		errno = EBADF;
		return -1;
	}

	argv.v_base = (unsigned long)bdescs;
	argv.v_nmembs = nbdescs;
	argv.v_size = sizeof(struct nilfs_bdesc);
	argv.v_flags = 0;
	argv.v_index = 0;
	ret = ioctl(nilfs->n_iocfd, NILFS_IOCTL_GET_BDESCS, &argv);
	if (unlikely(ret < 0))
		return -1;
	return argv.v_nmembs;
}

/**
 * nilfs_clean_segments - do garbage collection operation
 * @nilfs: nilfs object
 * @vdescs: array of nilfs_vdesc structs to specify live blocks
 * @nvdescs: size of @vdescs array (number of items)
 * @periods: array of nilfs_period structs to specify checkpoints to be deleted
 * @nperiods: size of @periods array (number of items)
 * @vblocknrs: array of virtual block addresses to be freed
 * @nvblocknrs: size of @vblocknrs array (number of items)
 * @bdescs: array of nilfs_bdesc structs to specify live DAT blocks
 * @nbdescs: size of @bdescs array (number of items)
 * @segnums: array of segment numbers to specify segments to be freed
 * @nsegs: size of @segnums array (number of items)
 */
int nilfs_clean_segments(struct nilfs *nilfs,
			 struct nilfs_vdesc *vdescs, size_t nvdescs,
			 struct nilfs_period *periods, size_t nperiods,
			 __u64 *vblocknrs, size_t nvblocknrs,
			 struct nilfs_bdesc *bdescs, size_t nbdescs,
			 __u64 *segnums, size_t nsegs)
{
	struct nilfs_argv argv[5];

	if (unlikely(nilfs->n_iocfd < 0)) {
		errno = EBADF;
		return -1;
	}

	memset(argv, 0, sizeof(argv));
	argv[0].v_base = (unsigned long)vdescs;
	argv[0].v_nmembs = nvdescs;
	argv[0].v_size = sizeof(struct nilfs_vdesc);
	argv[1].v_base = (unsigned long)periods;
	argv[1].v_nmembs = nperiods;
	argv[1].v_size = sizeof(struct nilfs_period);
	argv[2].v_base = (unsigned long)vblocknrs;
	argv[2].v_nmembs = nvblocknrs;
	argv[2].v_size = sizeof(__u64);
	argv[3].v_base = (unsigned long)bdescs;
	argv[3].v_nmembs = nbdescs;
	argv[3].v_size = sizeof(struct nilfs_bdesc);
	argv[4].v_base = (unsigned long)segnums;
	argv[4].v_nmembs = nsegs;
	argv[4].v_size = sizeof(__u64);
	return ioctl(nilfs->n_iocfd, NILFS_IOCTL_CLEAN_SEGMENTS, argv);
}

/**
 * nilfs_sync - sync a NILFS file system
 * @nilfs: nilfs object
 * @cnop: buffer to store the latest checkpoint number in
 */
int nilfs_sync(const struct nilfs *nilfs, nilfs_cno_t *cnop)
{
	if (unlikely(nilfs->n_iocfd < 0)) {
		errno = EBADF;
		return -1;
	}

	return ioctl(nilfs->n_iocfd, NILFS_IOCTL_SYNC, cnop);
}

/**
 * nilfs_resize - resize the filesystem
 * @nilfs: nilfs object
 * @size: new partition size
 */
int nilfs_resize(struct nilfs *nilfs, off_t size)
{
	__u64 range = size;

	if (unlikely(nilfs->n_iocfd < 0)) {
		errno = EBADF;
		return -1;
	}

	return ioctl(nilfs->n_iocfd, NILFS_IOCTL_RESIZE, &range);
}

/**
 * nilfs_set_alloc_range - limit range of segments to be allocated
 * @nilfs: nilfs object
 * @start: lower limit of segments in bytes (inclusive)
 * @end: upper limit of segments in bytes (inclusive)
 */
int nilfs_set_alloc_range(struct nilfs *nilfs, off_t start, off_t end)
{
	__u64 range[2] = { start, end };

	if (unlikely(nilfs->n_iocfd < 0)) {
		errno = EBADF;
		return -1;
	}

	return ioctl(nilfs->n_iocfd, NILFS_IOCTL_SET_ALLOC_RANGE, range);
}

/**
 * nilfs_freeze - freeze file system
 * @nilfs: nilfs object
 */
int nilfs_freeze(struct nilfs *nilfs)
{
	int arg = 0;

	if (unlikely(nilfs->n_iocfd < 0)) {
		errno = EBADF;
		return -1;
	}

	return ioctl(nilfs->n_iocfd, FIFREEZE, &arg);
}

/**
 * nilfs_thaw - thaw file system
 * @nilfs: nilfs object
 */
int nilfs_thaw(struct nilfs *nilfs)
{
	int arg = 0;

	if (unlikely(nilfs->n_iocfd < 0)) {
		errno = EBADF;
		return -1;
	}

	return ioctl(nilfs->n_iocfd, FITHAW, &arg);
}

/**
 * nilfs_get_segment - read or mmap segment to a memory region
 * @nilfs: nilfs object
 * @segnum: segment number
 * @segment: pointer to a segment object (nilfs_segment struct)
 */
int nilfs_get_segment(struct nilfs *nilfs, __u64 segnum,
		      struct nilfs_segment *segment)
{
	const struct nilfs_super_block *sb = nilfs->n_sb;
	struct nilfs_segment_summary *segsum;
	long pagesize;
	__u32 blocks_per_segment, blkbits, nblocks;
	__u64 segblocknr;
	size_t segsize;
	off_t segstart;
	void *addr;
	ssize_t ret;

	if (unlikely(nilfs->n_devfd < 0 || sb == NULL)) {
		errno = EBADF;
		return -1;
	}

	pagesize = sysconf(_SC_PAGESIZE);
	if (unlikely(pagesize <= 0)) {
		errno = EINVAL;
		return -1;
	}

	if (unlikely(segnum >= nilfs_get_nsegments(nilfs))) {
		errno = EINVAL;
		return -1;
	}

	blkbits = le32_to_cpu(sb->s_log_block_size) + 10;
	blocks_per_segment = le32_to_cpu(sb->s_blocks_per_segment);
	if (unlikely(blocks_per_segment < NILFS_SEG_MIN_BLOCKS)) {
		errno = EINVAL;
		return -1;
	}

	if (segnum == 0) {
		segblocknr = le64_to_cpu(sb->s_first_data_block);
		if (unlikely(segblocknr >= blocks_per_segment)) {
			errno = EINVAL;
			return -1;
		}
		nblocks = blocks_per_segment - (__u32)segblocknr;
	} else {
		segblocknr = (__u64)blocks_per_segment * segnum;
		nblocks = blocks_per_segment;
	}
	segsize = (__u64)nblocks << blkbits;
	segstart = segblocknr << blkbits;

#ifdef HAVE_MMAP
	if (nilfs_opt_test_mmap(nilfs)) {
		size_t alloc_size, page_offset;
		int errsv = errno;

		page_offset = segstart % pagesize;
		alloc_size = roundup(segsize + page_offset, pagesize);

		addr = mmap(0, alloc_size, PROT_READ, MAP_SHARED,
			    nilfs->n_devfd, segstart - page_offset);
		if (likely(addr != MAP_FAILED)) {
			segment->mmapped = 1;
			segment->adjusted = (page_offset != 0 ||
					     alloc_size != pagesize);
			goto success;
		}

		if (errno != ENODEV)
			return -1;
		/*
		 * The underlying device does not support memory mapping -
		 * fallback to malloc().
		 */
		errno = errsv;
	}
#endif	/* HAVE_MMAP */

	addr = malloc(segsize);
	if (unlikely(addr == NULL))
		return -1;

	ret = pread(nilfs->n_devfd, addr, segsize, segstart);
	if (unlikely(ret < 0)) {
		free(addr);
		return -1;
	}
	segment->mmapped = 0;
	segment->adjusted = 0;

success:
	segment->addr = addr;
	segment->segsize = segsize;
	segment->segnum = segnum;
	segsum = addr;
	segment->seqnum = le64_to_cpu(segsum->ss_seq);
	segment->blocknr = segblocknr;
	segment->nblocks = nblocks;
	segment->blocks_per_segment = blocks_per_segment;
	segment->blkbits = blkbits;
	segment->seed = le32_to_cpu(sb->s_crc_seed);
	return 0;
}

/**
 * nilfs_put_segment - free memory used for raw segment access
 * @segment: pointer to the segment object to be cleaned up
 */
int nilfs_put_segment(struct nilfs_segment *segment)
{
	if (segment->mmapped) {
#ifdef HAVE_MMAP
		size_t page_offset, size;

		if (segment->adjusted) {
			long pagesize = sysconf(_SC_PAGESIZE);

			if (unlikely(pagesize <= 0)) {
				errno = EINVAL;
				return -1;
			}
			page_offset = (unsigned long)segment->addr % pagesize;
			size = roundup(segment->segsize + page_offset,
				       pagesize);
		} else {
			size = segment->segsize;
			page_offset = 0;
		}
		return munmap(segment->addr - page_offset, size);
#else
		errno = EINVAL;
		return -1;
#endif	/* HAVE_MMAP */
	}

	free(segment->addr);
	return 0;
}

/**
 * nilfs_get_segment_seqnum - get sequence number of segment
 * @nilfs: nilfs object
 * @segnum: segment number
 * @seqnum: buffer to store sequence number of the segment given by @segnum
 */
int nilfs_get_segment_seqnum(const struct nilfs *nilfs, __u64 segnum,
			     __u64 *seqnum)
{
	const struct nilfs_super_block *sb = nilfs->n_sb;
	__u32 blocks_per_segment, blkbits;
	__le64 buf;
	off_t segstart, offset;
	ssize_t ret;

	if (unlikely(nilfs->n_devfd < 0 || sb == NULL)) {
		errno = EBADF;
		return -1;
	}

	if (unlikely(segnum >= nilfs_get_nsegments(nilfs))) {
		errno = EINVAL;
		return -1;
	}

	blkbits = le32_to_cpu(sb->s_log_block_size) + 10;
	blocks_per_segment = le32_to_cpu(sb->s_blocks_per_segment);
	segstart = (segnum == 0 ? le64_to_cpu(sb->s_first_data_block) :
		    blocks_per_segment * segnum) << blkbits;

	offset = segstart + offsetof(struct nilfs_segment_summary, ss_seq);
	ret = pread(nilfs->n_devfd, &buf, sizeof(buf), offset);
	if (unlikely(ret < 0))
		return -1;

	*seqnum = le64_to_cpu(buf);
	return 0;
}

nilfs_cno_t nilfs_get_oldest_cno(struct nilfs *nilfs)
{
	struct nilfs_cpinfo cpinfo[1];

	nilfs_get_cpinfo(nilfs, nilfs->n_mincno, NILFS_CHECKPOINT, cpinfo, 1);
	return nilfs->n_mincno;
}
