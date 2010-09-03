/*
 * nilfs.c - NILFS library.
 *
 * Copyright (C) 2007 Nippon Telegraph and Telephone Corporation.
 *
 * This file is part of NILFS.
 *
 * NILFS is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * NILFS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with NILFS; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Written by Koji Sato <koji@osrg.net>.
 *
 * Maintained by Ryusuke Konishi <ryusuke@osrg.net> from 2008.
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

#if HAVE_MMAP
#include <sys/mman.h>
#endif	/* HAVE_MMAP */

#if HAVE_TIME_H
#include <time.h>
#endif	/* HAVE_TIME_H */

#if HAVE_LINUX_TYPES_H
#include <linux/types.h>
#endif	/* HAVE_LINUX_TYPES_H */

#include <errno.h>
#include <assert.h>
#include "nilfs.h"
#include "realpath.h"


static inline int iseol(int c)
{
	return (c == '\n' || c == '\0');
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
		if ((q = strchr(p, MNTOPT_SEP)) != NULL) {
			n = (q - p < len) ? len : q - p;
			q++;
		} else
			n = (strlen(p) < len) ? len : strlen(p);
		if (strncmp(p, opt, n) == 0)
			return 1;
		p = q;
	}
	return 0;
}

#ifndef LINE_MAX
#define LINE_MAX	2048
#endif	/* LINE_MAX */
#define PROCMOUNTS	"/proc/mounts"

static int nilfs_find_fs(struct nilfs *nilfs, const char *dev, const char *dir,
			 const char *opt)
{
	FILE *fp;
	char line[LINE_MAX], *mntent[NMNTFLDS];
	size_t len;
	int ret, n;
	char canonical[PATH_MAX + 2];
	char *cdev = NULL, *cdir = NULL;
	char *mdev, *mdir;

	ret = -1;
	if (dev && myrealpath(dev, canonical, sizeof(canonical))) {
		cdev = strdup(canonical);
		if (!cdev)
			goto failed;
		dev = cdev;
	}

	if (dir && myrealpath(dir, canonical, sizeof(canonical))) {
		cdir = strdup(canonical);
		if (!cdir)
			goto failed_dev;
		dir = cdir;
	}

	fp = fopen(PROCMOUNTS, "r");
	if (fp == NULL)
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
			if (nilfs->n_dev == NULL)
				goto failed_proc_mounts;
			len = strlen(mntent[MNTFLD_DIR]) +
				strlen(NILFS_IOC) + 2;
			nilfs->n_ioc = malloc(sizeof(char) * len);
			if (nilfs->n_ioc == NULL) {
				free(nilfs->n_dev);
				nilfs->n_dev = NULL;
				goto failed_proc_mounts;
			}
			snprintf(nilfs->n_ioc, len, "%s/%s",
				 mntent[MNTFLD_DIR], NILFS_IOC);
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

size_t nilfs_get_block_size(struct nilfs *nilfs)
{
	return 1UL << (le32_to_cpu(nilfs->n_sb->s_log_block_size) +
		       NILFS_SB_BLOCK_SIZE_SHIFT);
}

__u64 nilfs_get_reserved_segments(const struct nilfs *nilfs)
{
	const struct nilfs_super_block *sb = nilfs->n_sb;
	__u64 rn;

	rn = (le64_to_cpu(sb->s_nsegments) *
	      le32_to_cpu(sb->s_r_segments_percentage) + 99) / 100;
	if (rn < NILFS_MIN_NRSVSEGS)
		rn = NILFS_MIN_NRSVSEGS;
	return rn;
}

int nilfs_opt_set_mmap(struct nilfs *nilfs)
{
	long pagesize;
	size_t segsize;

	if ((pagesize = sysconf(_SC_PAGESIZE)) < 0)
		return -1;
	segsize = le32_to_cpu(nilfs->n_sb->s_blocks_per_segment) *
		nilfs_get_block_size(nilfs);
	if (segsize % pagesize != 0)
		return -1;

	nilfs->n_opts |= NILFS_OPT_MMAP;
	return 0;
}

void nilfs_opt_clear_mmap(struct nilfs *nilfs)
{
	nilfs->n_opts &= ~NILFS_OPT_MMAP;
}

int nilfs_opt_test_mmap(struct nilfs *nilfs)
{
	return !!(nilfs->n_opts & NILFS_OPT_MMAP);
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
	int oflags;

	if (!(flags & (NILFS_OPEN_RAW | NILFS_OPEN_RDONLY |
		       NILFS_OPEN_WRONLY | NILFS_OPEN_RDWR))) {
		errno = EINVAL;
		return NULL;
	}

	nilfs = malloc(sizeof(*nilfs));
	if (nilfs == NULL)
		return NULL;

	nilfs->n_sb = NULL;
	nilfs->n_devfd = -1;
	nilfs->n_iocfd = -1;
	nilfs->n_dev = NULL;
	nilfs->n_ioc = NULL;
	nilfs->n_mincno = NILFS_CNO_MIN;

	if (flags & NILFS_OPEN_RAW) {
		if (dev == NULL) {
			if (nilfs_find_fs(nilfs, dev, dir, MNTOPT_RW) < 0)
				goto out_fd;
		} else {
			nilfs->n_dev = strdup(dev);
			if (nilfs->n_dev == NULL)
				goto out_fd;
		}
		nilfs->n_devfd = open(nilfs->n_dev, O_RDONLY);
		if (nilfs->n_devfd < 0)
			goto out_fd;
		if (nilfs_read_sb(nilfs) < 0)
			goto out_fd;

		features = le64_to_cpu(nilfs->n_sb->s_feature_incompat) &
			~NILFS_FEATURE_INCOMPAT_SUPP;
		if (features) {
			errno = ENOTSUP;
			goto out_fd;
		}
	}

	if (flags &
	    (NILFS_OPEN_RDONLY | NILFS_OPEN_WRONLY | NILFS_OPEN_RDWR)) {
		if (nilfs_find_fs(nilfs, dev, dir, MNTOPT_RW) < 0) {
			if (!(flags & NILFS_OPEN_RDONLY))
				goto out_nilfs;
			if (nilfs_find_fs(nilfs, dev, dir, MNTOPT_RO) < 0)
				goto out_nilfs;
		}
		oflags = O_CREAT;
		if (flags & NILFS_OPEN_RDONLY)
			oflags |= O_RDONLY;
		else if (flags & NILFS_OPEN_WRONLY)
			oflags |= O_WRONLY;
		else if (flags & NILFS_OPEN_RDWR)
			oflags |= O_RDWR;
		nilfs->n_iocfd = open(nilfs->n_ioc, oflags,
				      S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP |
				      S_IROTH | S_IWOTH);
		if (nilfs->n_iocfd < 0)
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
	if (nilfs->n_dev != NULL)
		free(nilfs->n_dev);
	if (nilfs->n_ioc != NULL)
		free(nilfs->n_ioc);
	if (nilfs->n_sb != NULL)
		free(nilfs->n_sb);

 out_nilfs:
	free(nilfs);
	return NULL;
}

/**
 * nilfs_close - destroy a NILFS object
 * @nilfs: NILFS object
 */
void nilfs_close(struct nilfs *nilfs)
{
	if (nilfs->n_devfd >= 0)
		close(nilfs->n_devfd);
	if (nilfs->n_iocfd >= 0)
		close(nilfs->n_iocfd);
	if (nilfs->n_dev != NULL)
		free(nilfs->n_dev);
	if (nilfs->n_ioc != NULL)
		free(nilfs->n_ioc);
	if (nilfs->n_sb != NULL)
		free(nilfs->n_sb);
	free(nilfs);
}

/**
 * nilfs_get_dev -
 * @nilfs:
 */
const char *nilfs_get_dev(const struct nilfs *nilfs)
{
	return nilfs->n_dev;
}

/**
 * nilfs_change_cpmode -
 * @nilfs:
 * @cno:
 * @mode:
 */
int nilfs_change_cpmode(struct nilfs *nilfs, nilfs_cno_t cno, int mode)
{
	struct nilfs_cpmode cpmode;

	if (nilfs->n_iocfd < 0) {
		errno = EBADF;
		return -1;
	}
	if (cno < NILFS_CNO_MIN) {
		errno = EINVAL;
		return -1;
	}

	cpmode.cm_cno = cno;
	cpmode.cm_mode = mode;
	return ioctl(nilfs->n_iocfd, NILFS_IOCTL_CHANGE_CPMODE, &cpmode);
}

/**
 * nilfs_get_cpinfo -
 * @nilfs:
 * @cno:
 * @mode:
 * @cpinfo:
 * @nci:
 */
ssize_t nilfs_get_cpinfo(struct nilfs *nilfs, nilfs_cno_t cno, int mode,
			 struct nilfs_cpinfo *cpinfo, size_t nci)
{
	struct nilfs_argv argv;

	if (nilfs->n_iocfd < 0) {
		errno = EBADF;
		return -1;
	}
	if (mode == NILFS_CHECKPOINT) {
		if (cno < NILFS_CNO_MIN) {
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
	if (ioctl(nilfs->n_iocfd, NILFS_IOCTL_GET_CPINFO, &argv) < 0)
		return -1;
	if (mode == NILFS_CHECKPOINT && argv.v_nmembs > 0 &&
	    cno == nilfs->n_mincno) {
		if (cpinfo[0].ci_cno > nilfs->n_mincno)
			nilfs->n_mincno = cpinfo[0].ci_cno;
	}
	return argv.v_nmembs;
}

/**
 * nilfs_delete_checkpoint -
 * @nilfs:
 * @cno:
 */
int nilfs_delete_checkpoint(struct nilfs *nilfs, nilfs_cno_t cno)
{
	if (nilfs->n_iocfd < 0) {
		errno = EBADF;
		return -1;
	}
	return ioctl(nilfs->n_iocfd, NILFS_IOCTL_DELETE_CHECKPOINT, &cno);
}

/**
 * nilfs_get_cpstat -
 * @nilfs:
 * @cpstat:
 */
int nilfs_get_cpstat(const struct nilfs *nilfs, struct nilfs_cpstat *cpstat)
{
	if (nilfs->n_iocfd < 0) {
		errno = EBADF;
		return -1;
	}
	return ioctl(nilfs->n_iocfd, NILFS_IOCTL_GET_CPSTAT, cpstat);
}

/**
 * nilfs_get_suinfo -
 * @nilfs:
 * @segnum:
 * @si:
 * @nsi:
 */
ssize_t nilfs_get_suinfo(const struct nilfs *nilfs, __u64 segnum,
			 struct nilfs_suinfo *si, size_t nsi)
{
	struct nilfs_argv argv;

	if (nilfs->n_iocfd < 0) {
		errno = EBADF;
		return -1;
	}

	argv.v_base = (unsigned long)si;
	argv.v_nmembs = nsi;
	argv.v_size = sizeof(struct nilfs_suinfo);
	argv.v_index = segnum;
	if (ioctl(nilfs->n_iocfd, NILFS_IOCTL_GET_SUINFO, &argv) < 0)
		return -1;
	return argv.v_nmembs;
}

/**
 * nilfs_get_sustat -
 * @nilfs:
 * @sustat:
 */
int nilfs_get_sustat(const struct nilfs *nilfs, struct nilfs_sustat *sustat)
{
	if (nilfs->n_iocfd < 0) {
		errno = EBADF;
		return -1;
	}

	return ioctl(nilfs->n_iocfd, NILFS_IOCTL_GET_SUSTAT, sustat);
}

/**
 * nilfs_get_vinfo -
 * @nilfs:
 * @vinfo:
 * @nvi:
 */
ssize_t nilfs_get_vinfo(const struct nilfs *nilfs,
			struct nilfs_vinfo *vinfo, size_t nvi)
{
	struct nilfs_argv argv;

	if (nilfs->n_iocfd < 0) {
		errno = EBADF;
		return -1;
	}

	argv.v_base = (unsigned long)vinfo;
	argv.v_nmembs = nvi;
	argv.v_size = sizeof(struct nilfs_vinfo);
	if (ioctl(nilfs->n_iocfd, NILFS_IOCTL_GET_VINFO, &argv) < 0)
		return -1;
	return argv.v_nmembs;
}

/**
 * nilfs_get_bdescs:
 * @nilfs:
 * @bdescs:
 * @nbdescs:
 */
ssize_t nilfs_get_bdescs(const struct nilfs *nilfs,
			 struct nilfs_bdesc *bdescs, size_t nbdescs)
{
	struct nilfs_argv argv;

	if (nilfs->n_iocfd < 0) {
		errno = EBADF;
		return -1;
	}

	argv.v_base = (unsigned long)bdescs;
	argv.v_nmembs = nbdescs;
	argv.v_size = sizeof(struct nilfs_bdesc);
	if (ioctl(nilfs->n_iocfd, NILFS_IOCTL_GET_BDESCS, &argv) < 0)
		return -1;
	return argv.v_nmembs;
}

/**
 * nilfs_clean_segments -
 * @nilfs:
 * @vdescs:
 * @nvdescs:
 * @periods:
 * @nperiods:
 * @vblocknrs:
 * @nvblocknrs:
 * @bdescs:
 * @nbdescs:
 * @segnums:
 * @nsegs:
 */
int nilfs_clean_segments(struct nilfs *nilfs,
			 struct nilfs_vdesc *vdescs, size_t nvdescs,
			 struct nilfs_period *periods, size_t nperiods,
			 __u64 *vblocknrs, size_t nvblocknrs,
			 struct nilfs_bdesc *bdescs, size_t nbdescs,
			 __u64 *segnums, size_t nsegs)
{
	struct nilfs_argv argv[5];

	if (nilfs->n_iocfd < 0) {
		errno = EBADF;
		return -1;
	}

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
 * nilfs_sync -
 * @nilfs:
 */
int nilfs_sync(const struct nilfs *nilfs, nilfs_cno_t *cnop)
{
	int ret;

	if (nilfs->n_iocfd < 0) {
		errno = EBADF;
		return -1;
	}

	if (((ret = ioctl(nilfs->n_iocfd, NILFS_IOCTL_SYNC, cnop)) < 0) &&
	    (ret == -EROFS))
		/* syncing read-only filesystem */
		ret = 0;

	return ret;
}


/* raw */

/**
 * nilfs_get_segment -
 * @nilfs: NILFS object
 * @segnum: segment number
 */
ssize_t nilfs_get_segment(struct nilfs *nilfs, unsigned long segnum,
			  void **segmentp)
{
	void *segment;
	size_t segsize;
	off_t offset;

	if (nilfs->n_devfd < 0) {
		errno = EBADF;
		return -1;
	}

	if (segnum >= le64_to_cpu(nilfs->n_sb->s_nsegments)) {
		errno = EINVAL;
		return -1;
	}

	segsize = le32_to_cpu(nilfs->n_sb->s_blocks_per_segment) *
		(1UL << (le32_to_cpu(nilfs->n_sb->s_log_block_size) +
			 NILFS_SB_BLOCK_SIZE_SHIFT));
	offset = ((off_t)segnum) * segsize;

#ifdef HAVE_MMAP
	if (nilfs_opt_test_mmap(nilfs)) {
		if ((segment = mmap(0, segsize, PROT_READ, MAP_SHARED,
				    nilfs->n_devfd, offset)) == MAP_FAILED)
			return -1;
	} else {
#endif	/* HAVE_MMAP */
		if ((segment = malloc(segsize)) == NULL)
			return -1;
		if (lseek(nilfs->n_devfd, offset, SEEK_SET) != offset) {
			free(segment);
			return -1;
		}
		if (read(nilfs->n_devfd, segment, segsize) != segsize) {
			free(segment);
			return -1;
		}
#ifdef HAVE_MMAP
	}
#endif	/* HAVE_MMAP */

	*segmentp = segment;
	return segsize;
}

/**
 * nilfs_put_segment -
 * @nilfs: NILFS object
 * @seg: segment
 */
int nilfs_put_segment(struct nilfs *nilfs, void *segment)
{
	size_t segsize;

	if (nilfs->n_devfd < 0) {
		errno = EBADF;
		return -1;
	}

#ifdef HAVE_MUNMAP
	if (nilfs_opt_test_mmap(nilfs)) {
		segsize = le32_to_cpu(nilfs->n_sb->s_blocks_per_segment) *
			(1UL << (le32_to_cpu(nilfs->n_sb->s_log_block_size) +
				 NILFS_SB_BLOCK_SIZE_SHIFT));
		return munmap(segment, segsize);
	} else {
#endif	/* HAVE_MUNMAP */
		free(segment);
		return 0;
#ifdef HAVE_MUNMAP
	}
#endif	/* HAVE_MUNMAP */
}

__u64 nilfs_get_segment_seqnum(const struct nilfs *nilfs, void *segment,
			       __u64 segnum)
{
	struct nilfs_segment_summary *segsum;
	unsigned long blkoff;

	blkoff = (segnum == 0) ?
		le64_to_cpu(nilfs->n_sb->s_first_data_block) : 0;

	segsum = segment + blkoff *
		(1UL << (le32_to_cpu(nilfs->n_sb->s_log_block_size) +
			 NILFS_SB_BLOCK_SIZE_SHIFT));
	return le64_to_cpu(segsum->ss_seq);
}

/* internal use only */

/* nilfs_psegment */
static int nilfs_psegment_is_valid(const struct nilfs_psegment *pseg)
{
	int offset;

	if (le32_to_cpu(pseg->p_segsum->ss_magic) != NILFS_SEGSUM_MAGIC)
		return 0;

	offset = sizeof(pseg->p_segsum->ss_datasum) +
		sizeof(pseg->p_segsum->ss_sumsum);
	return le32_to_cpu(pseg->p_segsum->ss_sumsum) ==
		nilfs_crc32(pseg->p_seed,
			    (unsigned char *)pseg->p_segsum + offset,
			    le32_to_cpu(pseg->p_segsum->ss_sumbytes) - offset);
}

void nilfs_psegment_init(struct nilfs_psegment *pseg, __u64 segnum,
			 void *seg, size_t nblocks, const struct nilfs *nilfs)
{
	unsigned long blkoff, nblocks_per_segment;

	blkoff = (segnum == 0) ?
		le64_to_cpu(nilfs->n_sb->s_first_data_block) : 0;
	nblocks_per_segment = le32_to_cpu(nilfs->n_sb->s_blocks_per_segment);

	pseg->p_blksize = 1UL << (le32_to_cpu(nilfs->n_sb->s_log_block_size) +
				  NILFS_SB_BLOCK_SIZE_SHIFT);
	pseg->p_nblocks = nblocks;
	pseg->p_maxblocks = nblocks_per_segment - blkoff;
	pseg->p_segblocknr = (sector_t)nblocks_per_segment * segnum + blkoff;
	pseg->p_seed = le32_to_cpu(nilfs->n_sb->s_crc_seed);

	pseg->p_segsum = seg + blkoff * pseg->p_blksize;
	pseg->p_blocknr = pseg->p_segblocknr;
}

int nilfs_psegment_is_end(const struct nilfs_psegment *pseg)
{
	return pseg->p_blocknr >= pseg->p_segblocknr + pseg->p_nblocks ||
		pseg->p_segblocknr + pseg->p_maxblocks - pseg->p_blocknr <
		NILFS_PSEG_MIN_BLOCKS ||
		!nilfs_psegment_is_valid(pseg);
}

void nilfs_psegment_next(struct nilfs_psegment *pseg)
{
	unsigned long nblocks;

	nblocks = le32_to_cpu(pseg->p_segsum->ss_nblocks);
	pseg->p_segsum = (void *)pseg->p_segsum + nblocks * pseg->p_blksize;
	pseg->p_blocknr += nblocks;
}

/* nilfs_file */
void nilfs_file_init(struct nilfs_file *file,
		     const struct nilfs_psegment *pseg)
{
	size_t blksize, rest, hdrsize;

	file->f_psegment = pseg;
	blksize = file->f_psegment->p_blksize;
	hdrsize = le16_to_cpu(pseg->p_segsum->ss_bytes);

	file->f_finfo = (void *)pseg->p_segsum + hdrsize;
	file->f_blocknr = pseg->p_blocknr +
		(le32_to_cpu(pseg->p_segsum->ss_sumbytes) + blksize - 1) /
		blksize;
	file->f_index = 0;
	file->f_offset = hdrsize;

	rest = blksize - file->f_offset % blksize;
	if (sizeof(struct nilfs_finfo) > rest) {
		file->f_finfo = (void *)file->f_finfo + rest;
		file->f_offset += rest;
	}
}

int nilfs_file_is_end(const struct nilfs_file *file)
{
	return file->f_index >=
		le32_to_cpu(file->f_psegment->p_segsum->ss_nfinfo);
}

static size_t nilfs_binfo_total_size(unsigned long offset,
				     size_t blksize, size_t bisize, size_t n)
{
	size_t binfo_per_block, rest = blksize - offset % blksize;

	if (bisize * n <= rest)
		return bisize * n;

	n -= rest / bisize;
	binfo_per_block = blksize / bisize;
	return rest + (n / binfo_per_block) * blksize +
		(n % binfo_per_block) * bisize;
}

void nilfs_file_next(struct nilfs_file *file)
{
	size_t blksize, rest, delta;
	size_t dsize, nsize;
	unsigned long ndatablk, nblocks;

	blksize = file->f_psegment->p_blksize;

	if (!nilfs_file_is_super(file)) {
		dsize = NILFS_BINFO_DATA_SIZE;
		nsize = NILFS_BINFO_NODE_SIZE;
	} else {
		dsize = NILFS_BINFO_DAT_DATA_SIZE;
		nsize = NILFS_BINFO_DAT_NODE_SIZE;
	}

	nblocks = le32_to_cpu(file->f_finfo->fi_nblocks);
	ndatablk = le32_to_cpu(file->f_finfo->fi_ndatablk);

	delta = sizeof(struct nilfs_finfo);
	delta += nilfs_binfo_total_size(file->f_offset + delta,
					blksize, dsize, ndatablk);
	delta += nilfs_binfo_total_size(file->f_offset + delta,
					blksize, nsize, nblocks - ndatablk);

	file->f_blocknr += nblocks;
	file->f_offset += delta;
	file->f_finfo = (void *)file->f_finfo + delta;

	rest = blksize - file->f_offset % blksize;
	if (sizeof(struct nilfs_finfo) > rest) {
		file->f_offset += rest;
		file->f_finfo = (void *)file->f_finfo + rest;
	}
	file->f_index++;
}

/* nilfs_block */
void nilfs_block_init(struct nilfs_block *blk, const struct nilfs_file *file)
{
	size_t blksize, bisize, rest;

	blk->b_file = file;
	blksize = blk->b_file->f_psegment->p_blksize;

	blk->b_binfo = (void *)(file->f_finfo + 1);
	blk->b_offset = file->f_offset + sizeof(struct nilfs_finfo);
	blk->b_blocknr = file->f_blocknr;
	blk->b_index = 0;
	if (!nilfs_file_is_super(file)) {
		blk->b_dsize = NILFS_BINFO_DATA_SIZE;
		blk->b_nsize = NILFS_BINFO_NODE_SIZE;
	} else {
		blk->b_dsize = NILFS_BINFO_DAT_DATA_SIZE;
		blk->b_nsize = NILFS_BINFO_DAT_NODE_SIZE;
	}

	bisize = nilfs_block_is_data(blk) ? blk->b_dsize : blk->b_nsize;
	rest = blksize - blk->b_offset % blksize;
	if (bisize > rest) {
		blk->b_binfo += rest;
		blk->b_offset += rest;
	}
}

int nilfs_block_is_end(const struct nilfs_block *blk)
{
	return blk->b_index >= le32_to_cpu(blk->b_file->f_finfo->fi_nblocks);
}

void nilfs_block_next(struct nilfs_block *blk)
{
	size_t blksize, bisize, rest;

	blksize = blk->b_file->f_psegment->p_blksize;

	bisize = nilfs_block_is_data(blk) ? blk->b_dsize : blk->b_nsize;
	blk->b_binfo += bisize;
	blk->b_offset += bisize;
	blk->b_index++;

	bisize = nilfs_block_is_data(blk) ? blk->b_dsize : blk->b_nsize;
	rest = blksize - blk->b_offset % blksize;
	if (bisize > rest) {
		blk->b_binfo += rest;
		blk->b_offset += rest;
	}

	blk->b_blocknr++;
}

nilfs_cno_t nilfs_get_oldest_cno(struct nilfs *nilfs)
{
	struct nilfs_cpinfo cpinfo[1];
	
	nilfs_get_cpinfo(nilfs, nilfs->n_mincno, NILFS_CHECKPOINT, cpinfo, 1);
	return nilfs->n_mincno;
}

struct nilfs_super_block *nilfs_get_sb(struct nilfs *nilfs)
{
	return nilfs->n_sb;
}

int nilfs_read_sb(struct nilfs *nilfs)
{
	assert(nilfs->n_sb == NULL);

	nilfs->n_sb = nilfs_sb_read(nilfs->n_devfd);
	if (!nilfs->n_sb)
		return -1;

	return 0;
}
