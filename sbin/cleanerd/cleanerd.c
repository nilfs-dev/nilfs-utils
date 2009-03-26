/*
 * cleanerd.c - NILFS cleaner daemon.
 *
 * Copyright (C) 2007-2008 Nippon Telegraph and Telephone Corporation.
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

#if HAVE_STRING_H
#include <string.h>
#endif	/* HAVE_STRING_H */

#include <signal.h>

#if HAVE_LIMITS_H
#include <limits.h>
#endif	/* HAVE_LIMITS_H */

#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif	/* HAVE_SYS_TYPES_H */

#if HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif	/* HAVE_SYS_STAT_H */

#if HAVE_SYS_TIME_H
#include <sys/time.h>
#endif	/* HAVE_SYS_TIME */

#if HAVE_TIME_H
#include <time.h>
#endif	/* HAVE_TIME_H */

#include <errno.h>

#if HAVE_SYSLOG_H
#include <syslog.h>
#endif	/* HAVE_SYSLOG_H */

#include <setjmp.h>
#include <assert.h>
#include "vector.h"
#include "cleanerd.h"

#ifdef _GNU_SOURCE
#include <getopt.h>
const static struct option long_option[] = {
	{"conffile", required_argument, NULL, 'c'},
	{"help", no_argument, NULL, 'h'},
	/* internal option for mount.nilfs2 only */
	{"nofork", no_argument, NULL, 'n'},
	{NULL, 0, NULL, 0}
};
#define NILFS_CLEANERD_OPTIONS	\
	"  -c, --conffile\tspecify configuration file\n"	\
	"  -h, --help    \tdisplay this help and exit\n"
#else	/* !_GNU_SOURCE */
#define NILFS_CLEANERD_OPTIONS	\
	"  -c            \tspecify configuration file\n"	\
	"  -h            \tdisplay this help and exit\n"
#endif	/* _GNU_SOURCE */

static struct nilfs_cleanerd *nilfs_cleanerd;
static sigjmp_buf nilfs_cleanerd_env;
static volatile sig_atomic_t nilfs_cleanerd_reload_config;


static void nilfs_cleanerd_usage(const char *progname)
{
	fprintf(stderr,
		"Usage: %s [option]... dev\n"
		"%s options:\n"
		NILFS_CLEANERD_OPTIONS,
		progname, progname);
}

static void nilfs_cleanerd_set_log_priority(struct nilfs_cleanerd *cleanerd)
{
	setlogmask(LOG_UPTO(cleanerd->c_config.cf_log_priority));
}

/**
 * nilfs_cleanerd_config - load configuration file
 * @cleanerd: cleanerd object
 */
static int nilfs_cleanerd_config(struct nilfs_cleanerd *cleanerd)
{
	if (nilfs_cldconfig_read(&cleanerd->c_config,
				 cleanerd->c_conffile) < 0)
		return -1;
#ifdef HAVE_MMAP
	if (cleanerd->c_config.cf_use_mmap)
		nilfs_opt_set_mmap(cleanerd->c_nilfs);
	else
		nilfs_opt_clear_mmap(cleanerd->c_nilfs);
#endif	/* HAVE_MMAP */
	nilfs_cleanerd_set_log_priority(cleanerd);

	return 0;
}

/**
 * nilfs_cleanerd_reconfig - reload configuration file
 * @cleanerd: cleanerd object
 */
static int nilfs_cleanerd_reconfig(struct nilfs_cleanerd *cleanerd)
{
	struct nilfs_cldconfig *config = &cleanerd->c_config;
	time_t prev_prot_period = config->cf_protection_period;
	int ret;

	ret = nilfs_cleanerd_config(cleanerd);
	if (!ret) {
		if (config->cf_protection_period > prev_prot_period) {
			cleanerd->c_protcno = 0;
			cleanerd->c_prottime = 0;
		}
	}
	return ret;
}

#ifndef PATH_MAX
#define PATH_MAX	8192
#endif	/* PATH_MAX */

/**
 * nilfs_cleanerd_create - create cleanerd object
 * @dev: name of the device on which the cleanerd operates
 * @conffile: pathname of configuration file
 */
static struct nilfs_cleanerd *
nilfs_cleanerd_create(const char *dev, const char *conffile)
{
	struct nilfs_cleanerd *cleanerd;

	cleanerd = malloc(sizeof(*cleanerd));
	if (cleanerd == NULL)
		return NULL;

	cleanerd->c_nilfs = nilfs_open(dev, NILFS_OPEN_RAW | NILFS_OPEN_RDWR);
	if (cleanerd->c_nilfs == NULL)
		goto out_cleanerd;

	cleanerd->c_conffile = strdup(conffile ? : NILFS_CLEANERD_CONFFILE);
	if (cleanerd->c_conffile == NULL)
		goto out_nilfs;

	if (nilfs_cleanerd_config(cleanerd) < 0)
		goto out_conffile;

	/* success */
	cleanerd->c_running = 0;
	cleanerd->c_protcno = 0; /* means unspecified */
	cleanerd->c_prottime = 0;

	return cleanerd;

	/* error */
 out_conffile:
	free(cleanerd->c_conffile);

 out_nilfs:
	nilfs_close(cleanerd->c_nilfs);

 out_cleanerd:
	free(cleanerd);
	return NULL;
}

static void nilfs_cleanerd_destroy(struct nilfs_cleanerd *cleanerd)
{
	free(cleanerd->c_conffile);
	nilfs_close(cleanerd->c_nilfs);
	free(cleanerd);
}

static int nilfs_comp_segimp(const void *elem1, const void *elem2)
{
	const struct nilfs_segimp *segimp1 = elem1, *segimp2 = elem2;

	if (segimp1->si_importance < segimp2->si_importance)
		return -1;
	else if (segimp1->si_importance > segimp2->si_importance)
		return 1;

	return (segimp1->si_segnum < segimp2->si_segnum) ? -1 : 1;
}

#define NILFS_CLEANERD_NSUINFO	512

/**
 * nilfs_cleanerd_select_segments - select segments to be reclaimed
 * @cleanerd: cleanerd object
 * @sustat: status information on segments
 * @segnums: array of segment numbers to store selected segments
 * @nsegs: size of the @segnums array
 * @ts: pointer to a timespec struct to store sleep time
 * @prottimep: pointer to time to store lower limit of protected period
 */
static ssize_t
nilfs_cleanerd_select_segments(struct nilfs_cleanerd *cleanerd,
			       struct nilfs_sustat *sustat,
			       __u64 *segnums, size_t nsegs,
			       struct timespec *ts, __u64 *prottimep)
{
	struct nilfs *nilfs;
	struct nilfs_cldconfig *config;
	struct nilfs_vector *smv;
	struct nilfs_segimp *sm;
	struct nilfs_suinfo si[NILFS_CLEANERD_NSUINFO];
	struct timeval tv;
	__u64 prottime, oldest;
	__u64 segnum;
	size_t count;
	ssize_t nssegs, n;
	unsigned long long imp, thr;
	int i;

	nilfs = cleanerd->c_nilfs;
	config = &cleanerd->c_config;

	if ((smv = nilfs_vector_create(sizeof(struct nilfs_segimp))) == NULL)
		return -1;

	/* The segments that were more recently written to disk than
	 * prottime are not selected. */
	if (gettimeofday(&tv, NULL) < 0) {
		nssegs = -1;
		goto out;
	}
	prottime = tv.tv_sec - config->cf_protection_period;
	oldest = tv.tv_sec;

	/* The segments that have larger importance than thr are not
	 * selected. */
	thr = (config->cf_selection_policy.p_threshold != 0) ?
		config->cf_selection_policy.p_threshold :
		sustat->ss_nongc_ctime;

	for (segnum = 0; segnum < sustat->ss_nsegs; segnum += n) {
		count = (sustat->ss_nsegs - segnum < NILFS_CLEANERD_NSUINFO) ?
			sustat->ss_nsegs - segnum : NILFS_CLEANERD_NSUINFO;
		if ((n = nilfs_get_suinfo(nilfs, segnum, si, count)) < 0) {
			nssegs = n;
			goto out;
		}
		for (i = 0; i < n; i++) {
			if (nilfs_suinfo_dirty(&si[i]) &&
			    !nilfs_suinfo_active(&si[i]) &&
			    !nilfs_suinfo_error(&si[i]) &&
			    ((imp = (*config->cf_selection_policy.p_importance)(&si[i])) < thr)) {
				if (si[i].sui_lastmod < oldest)
					oldest = si[i].sui_lastmod;
				if (si[i].sui_lastmod < prottime) {
					sm = nilfs_vector_get_new_element(smv);
					if (sm == NULL) {
						nssegs = -1;
						goto out;
					}
					sm->si_segnum = segnum + i;
					sm->si_importance = imp;
				}
			}
		}
		if (n == 0) {
			syslog(LOG_WARNING,
			       "inconsistent number of segments: %llu "
			       "(nsegs=%llu)",
			       (unsigned long long)nilfs_vector_get_size(smv),
			       (unsigned long long)sustat->ss_nsegs);
			break;
		}
	}
	nilfs_vector_sort(smv, nilfs_comp_segimp);

	nssegs = (nilfs_vector_get_size(smv) < nsegs) ?
		nilfs_vector_get_size(smv) : nsegs;
	for (i = 0; i < nssegs; i++) {
		sm = nilfs_vector_get_element(smv, i);
		assert(sm != NULL);
		segnums[i] = sm->si_segnum;
	}
	if (nssegs == 0) {
		ts->tv_nsec = 0;
		if (oldest < tv.tv_sec)
			ts->tv_sec = oldest - prottime + 1;
		else {
			ts->tv_sec = config->cf_protection_period + 1;
			cleanerd->c_running = 0;
		}
	}
	*prottimep = prottime;

 out:
	nilfs_vector_destroy(smv);
	return nssegs;
}

static int nilfs_comp_vdesc_blocknr(const void *elem1, const void *elem2)
{
	const struct nilfs_vdesc *vdesc1 = elem1, *vdesc2 = elem2;

	return (vdesc1->vd_blocknr < vdesc2->vd_blocknr) ? -1 : 1;
}

static int nilfs_comp_vdesc_vblocknr(const void *elem1, const void *elem2)
{
	const struct nilfs_vdesc *vdesc1 = elem1, *vdesc2 = elem2;

	return (vdesc1->vd_vblocknr < vdesc2->vd_vblocknr) ? -1 : 1;
}

static int nilfs_comp_period(const void *elem1, const void *elem2)
{
	const struct nilfs_period *period1 = elem1, *period2 = elem2;

	return (period1->p_start < period2->p_start) ? -1 :
		(period1->p_start == period2->p_start) ? 0 : 1;
}

static int nilfs_comp_bdesc(const void *elem1, const void *elem2)
{
	const struct nilfs_bdesc *bdesc1 = elem1, *bdesc2 = elem2;

	if (bdesc1->bd_ino < bdesc2->bd_ino)
		return -1;
	else if (bdesc1->bd_ino > bdesc2->bd_ino)
		return 1;

	if (bdesc1->bd_level < bdesc2->bd_level)
		return -1;
	else if (bdesc1->bd_level > bdesc2->bd_level)
		return 1;

	if (bdesc1->bd_offset < bdesc2->bd_offset)
		return -1;
	else if (bdesc1->bd_offset > bdesc2->bd_offset)
		return 1;
	else
		return 0;
}

/**
 * nilfs_cleanerd_acc_blocks_file - collect summary of blocks in a file
 * @cleanerd: cleanerd object
 * @file: file object
 * @vdescv: vector object to store (descriptors of) virtual block numbers
 * @bdescv: vector object to store (descriptors of) disk block numbers
 */
static int nilfs_cleanerd_acc_blocks_file(struct nilfs_cleanerd *cleanerd,
					  struct nilfs_file *file,
					  struct nilfs_vector *vdescv,
					  struct nilfs_vector *bdescv)
{
	struct nilfs_block blk;
	struct nilfs_vdesc *vdesc;
	struct nilfs_bdesc *bdesc;
	union nilfs_binfo *binfo;
	ino_t ino;
	nilfs_cno_t cno;

	ino = le64_to_cpu(file->f_finfo->fi_ino);
	if (nilfs_file_is_super(file)) {
		nilfs_block_for_each(&blk, file) {
			bdesc = nilfs_vector_get_new_element(bdescv);
			if (bdesc == NULL)
				return -1;
			bdesc->bd_ino = ino;
			bdesc->bd_oblocknr = blk.b_blocknr;
			if (nilfs_block_is_data(&blk)) {
				bdesc->bd_offset =
					le64_to_cpu(*(__le64 *)blk.b_binfo);
				bdesc->bd_level = 0;
			} else {
				binfo = blk.b_binfo;
				bdesc->bd_offset =
					le64_to_cpu(binfo->bi_dat.bi_blkoff);
				bdesc->bd_level = binfo->bi_dat.bi_level;
			}
		}
	} else {
		cno = le64_to_cpu(file->f_finfo->fi_cno);
		nilfs_block_for_each(&blk, file) {
			vdesc = nilfs_vector_get_new_element(vdescv);
			if (vdesc == NULL)
				return -1;
			vdesc->vd_ino = ino;
			vdesc->vd_cno = cno;
			vdesc->vd_blocknr = blk.b_blocknr;
			if (nilfs_block_is_data(&blk)) {
				binfo = blk.b_binfo;
				vdesc->vd_vblocknr =
					le64_to_cpu(binfo->bi_v.bi_vblocknr);
				vdesc->vd_offset =
					le64_to_cpu(binfo->bi_v.bi_blkoff);
				vdesc->vd_flags = 0;	/* data */
			} else {
				vdesc->vd_vblocknr =
					le64_to_cpu(*(__le64 *)blk.b_binfo);
				vdesc->vd_flags = 1;	/* node */
			}
		}
	}
	return 0;
}

/**
 * nilfs_cleanerd_acc_blocks_psegment - collect summary of blocks in a log
 * @cleanerd: cleanerd object
 * @psegment: partial segment object
 * @vdescv: vector object to store (descriptors of) virtual block numbers
 * @bdescv: vector object to store (descriptors of) disk block numbers
 */
static int nilfs_cleanerd_acc_blocks_psegment(struct nilfs_cleanerd *cleanerd,
					      struct nilfs_psegment *psegment,
					      struct nilfs_vector *vdescv,
					      struct nilfs_vector *bdescv)
{
	struct nilfs_file file;

	nilfs_file_for_each(&file, psegment) {
		if (nilfs_cleanerd_acc_blocks_file(
			    cleanerd, &file, vdescv, bdescv) < 0)
			return -1;
	}
	return 0;
}

/**
 * nilfs_cleanerd_acc_blocks_segment - collect summary of blocks in a segment
 * @cleanerd: cleanerd object
 * @segnum: segment number to be parsed
 * @segment: start address of segment data
 * @nblocks: size of valid logs in the segment (per block)
 * @vdescv: vector object to store (descriptors of) virtual block numbers
 * @bdescv: vector object to store (descriptors of) disk block numbers
 */
static int nilfs_cleanerd_acc_blocks_segment(struct nilfs_cleanerd *cleanerd,
					     __u64 segnum,
					     void *segment,
					     size_t nblocks,
					     struct nilfs_vector *vdescv,
					     struct nilfs_vector *bdescv)
{
	struct nilfs_psegment psegment;

	nilfs_psegment_for_each(&psegment, segnum, segment, nblocks,
				cleanerd->c_nilfs) {
		if (nilfs_cleanerd_acc_blocks_psegment(
			    cleanerd, &psegment, vdescv, bdescv) < 0)
			return -1;
	}
	return 0;
}

/**
 * nilfs_deselect_segment - deselect a segment
 * @segnums: array of selected segments
 * @nsegs: size of @segnums array
 * @nr: index number for @segnums array to be deselected
 */
static ssize_t nilfs_deselect_segment(__u64 *segnums, size_t nsegs, int nr)
{
	if (nr >= nsegs || nsegs == 0)
		return -1;
	else if (nr < nsegs - 1) {
		__u64 tn = segnums[nr];

		memmove(&segnums[nr], &segnums[nr + 1],
			sizeof(__u64) * (nsegs - 1 - nr));
		segnums[nsegs - 1] = tn;
	}
	return nsegs - 1;
}

#define nilfs_cnt64_ge(a, b)	((__s64)(a) - (__s64)(b) >= 0)

/**
 * nilfs_cleanerd_acc_blocks - collect summary of blocks contained in segments
 * @cleanerd: cleanerd object
 * @sustat: status information on segments
 * @segnums: array of selected segments
 * @nsegs: size of @segnums array
 * @vdescv: vector object to store (descriptors of) virtual block numbers
 * @bdescv: vector object to store (descriptors of) disk block numbers
 */
static ssize_t nilfs_cleanerd_acc_blocks(struct nilfs_cleanerd *cleanerd,
					 struct nilfs_sustat *sustat,
					 __u64 *segnums, size_t nsegs,
					 struct nilfs_vector *vdescv,
					 struct nilfs_vector *bdescv)
{
	struct nilfs_suinfo si;
	struct nilfs *nilfs = cleanerd->c_nilfs;
	void *segment;
	int ret, i = 0;
	ssize_t n = nsegs;
	__u64 segseq;

	while (i < n) {
		if (nilfs_get_suinfo(nilfs, segnums[i], &si, 1) < 0)
			return -1;
		if (nilfs_get_segment(nilfs, segnums[i], &segment) < 0)
			return -1;

		segseq = nilfs_get_segment_seqnum(nilfs, segment, segnums[i]);
		if (nilfs_cnt64_ge(segseq, sustat->ss_prot_seq)) {
			n = nilfs_deselect_segment(segnums, n, i);
			if (nilfs_put_segment(nilfs, segment) < 0)
				return -1;
			continue;
		}
		ret = nilfs_cleanerd_acc_blocks_segment(
			cleanerd, segnums[i], segment, si.sui_nblocks,
			vdescv, bdescv);
		if (nilfs_put_segment(nilfs, segment) < 0 || ret < 0)
			return -1;
		i++;
	}
	return n;
}

#define NILFS_CLEANERD_NVINFO		512

/**
 * nilfs_cleanerd_get_vdesc - get information on virtual block addresses
 * @cleanerd: cleanerd object
 * @vdescv: vector object storing (descriptors of) virtual block numbers
 */
static int nilfs_cleanerd_get_vdesc(struct nilfs_cleanerd *cleanerd,
				    struct nilfs_vector *vdescv)
{
	struct nilfs_vdesc *vdesc;
	struct nilfs_vinfo vinfo[NILFS_CLEANERD_NVINFO];
	ssize_t n;
	int i, j;

	nilfs_vector_sort(vdescv, nilfs_comp_vdesc_vblocknr);

	for (i = 0; i < nilfs_vector_get_size(vdescv); i += n) {
		for (j = 0;
		     (j < NILFS_CLEANERD_NVINFO) &&
			     (i + j < nilfs_vector_get_size(vdescv));
		     j++) {
			vdesc = nilfs_vector_get_element(vdescv, i + j);
			assert(vdesc != NULL);
			vinfo[j].vi_vblocknr = vdesc->vd_vblocknr;
		}
		if ((n = nilfs_get_vinfo(cleanerd->c_nilfs, vinfo, j)) < 0)
			return -1;
		for (j = 0; j < n; j++) {
			vdesc = nilfs_vector_get_element(vdescv, i + j);
			assert((vdesc != NULL) &&
			       (vdesc->vd_vblocknr == vinfo[j].vi_vblocknr));
			vdesc->vd_period.p_start = vinfo[j].vi_start;
			vdesc->vd_period.p_end = vinfo[j].vi_end;
		}
	}

	return 0;
}

#define NILFS_CLEANERD_NCPINFO	512

/**
 * nilfs_cleanerd_get_snapshot - get checkpoint numbers of snapshots
 * @cleanerd: cleanerd object
 * @ssp: pointer to store array of checkpoint numbers which are snapshots
 */
static ssize_t
nilfs_cleanerd_get_snapshot(const struct nilfs_cleanerd *cleanerd,
			    nilfs_cno_t **ssp)
{
	struct nilfs_cpstat cpstat;
	struct nilfs_cpinfo cpinfo[NILFS_CLEANERD_NCPINFO];
	nilfs_cno_t cno, *ss;
	ssize_t n;
	__u64 nss = 0;
	int i, j;

	if (nilfs_get_cpstat(cleanerd->c_nilfs, &cpstat) < 0)
		return -1;
	if (cpstat.cs_nsss == 0)
		return 0;

	ss = malloc(sizeof(*ss) * cpstat.cs_nsss);
	if (ss == NULL)
		return -1;

	cno = 0;
	for (i = 0; i < cpstat.cs_nsss; i += n) {
		if ((n = nilfs_get_cpinfo(
			     cleanerd->c_nilfs, cno, NILFS_SNAPSHOT,
			     cpinfo, NILFS_CLEANERD_NCPINFO)) < 0) {
			free(ss);
			return -1;
		}
		if (n == 0)
			break;
		for (j = 0; j < n; j++)
			ss[i + j] = cpinfo[j].ci_cno;
		nss += n;
		cno = cpinfo[n - 1].ci_next;
		if (cno == 0)
			break;
	}
	if (cpstat.cs_nsss != nss)
		syslog(LOG_WARNING, "snapshot count mismatch: %llu != %llu",
		       (unsigned long long)cpstat.cs_nsss,
		       (unsigned long long)nss);
	*ssp = ss;
	return nss;
}

/**
 * nilfs_cleanerd_update_prottime - update the minimum of protected CNOs
 * @cleanerd: cleanerd object
 * @prottime: new protection time
 */
static int nilfs_cleanerd_update_prottime(struct nilfs_cleanerd *cleanerd,
					  __u64 prottime)
{
	struct nilfs_cpstat cpstat;
	struct nilfs_cpinfo cpinfo[NILFS_CLEANERD_NCPINFO];
	nilfs_cno_t cno;
	size_t count;
	ssize_t n;
	int i;

	if (nilfs_get_cpstat(cleanerd->c_nilfs, &cpstat) < 0)
		return -1;

	if (cleanerd->c_prottime > prottime) {
		syslog(LOG_WARNING, "protection time rewinded: "
		       "old period >= %llu, new period >= %llu",
		       (unsigned long long)cleanerd->c_prottime,
		       (unsigned long long)prottime);
		cleanerd->c_protcno = 0;
	} else if (cleanerd->c_prottime == prottime)
		return 0; /* protection time unchanged */

	cno = (cleanerd->c_protcno == 0) ? NILFS_CNO_MIN : cleanerd->c_protcno;
	while (cno < cpstat.cs_cno) {
		count = (cpstat.cs_cno - cno < NILFS_CLEANERD_NCPINFO) ?
			cpstat.cs_cno - cno : NILFS_CLEANERD_NCPINFO;
		n = nilfs_get_cpinfo(cleanerd->c_nilfs, cno, NILFS_CHECKPOINT,
				     cpinfo, count);
		if (n < 0)
			return -1;
		if (n == 0)
			break;

		for (i = 0; i < n; i++) {
			if (cpinfo[i].ci_create >= prottime) {
				cleanerd->c_protcno = cpinfo[i].ci_cno;
				goto out;
			}
		}
		cno = cpinfo[n - 1].ci_cno + 1;
	}
	cleanerd->c_protcno = cno;
 out:
	cleanerd->c_prottime = prottime;
	syslog(LOG_DEBUG, "protected checkpoints = [%llu,%llu] "
	       "(protection period >= %llu)",
	       (unsigned long long)cleanerd->c_protcno,
	       (unsigned long long)cpstat.cs_cno,
	       (unsigned long long)prottime);
	return 0;
}

/*
 * nilfs_vdesc_is_live - judge if a virtual block address is live or dead
 * @vdesc: descriptor object of the virtual block address
 * @protect: the minimum of checkpoint numbers to be protected
 * @ss: checkpoint numbers of snapshots
 * @n: size of @ss array
 */
static int nilfs_vdesc_is_live(const struct nilfs_vdesc *vdesc,
			       nilfs_cno_t protect, const nilfs_cno_t *ss,
			       size_t n)
{
	long low, high, index;
	int s;

	if (vdesc->vd_period.p_end == NILFS_CNO_MAX ||
	    vdesc->vd_period.p_end > protect)
		return 1;

	if (n == 0 || vdesc->vd_period.p_start > ss[n - 1] ||
	    vdesc->vd_period.p_end <= ss[0])
		return 0;

	low = 0;
	high = n - 1;
	index = 0;
	s = 0;
	while (low <= high) {
		index = (low + high) / 2;
		if (ss[index] == vdesc->vd_period.p_start) {
			goto out;
		} else if (ss[index] < vdesc->vd_period.p_start) {
			s = -1;
			low = index + 1;
		} else {
			s = 1;
			high = index - 1;
		}
	}
	/* adjust index */
	if (s < 0)
		index++;

 out:
	return ss[index] < vdesc->vd_period.p_end;
}

/**
 * nilfs_cleanerd_toss_vdescs - deselect deletable virtual block numbers
 * @cleanerd: cleanerd object
 * @vdescv: vector object storing (descriptors of) virtual block numbers
 * @periodv: vector object to store deletable checkpoint numbers (periods)
 * @vblocknrv: vector object to store deletable virtual block numbers
 *
 * nilfs_cleanerd_toss_vdescs() deselects virtual block numbers of files
 * other than the DAT file.
 */
static int nilfs_cleanerd_toss_vdescs(struct nilfs_cleanerd *cleanerd,
				      struct nilfs_vector *vdescv,
				      struct nilfs_vector *periodv,
				      struct nilfs_vector *vblocknrv)
{
	struct nilfs_vdesc *vdesc;
	struct nilfs_period *periodp;
	__u64 *vblocknrp;
	nilfs_cno_t *ss;
	ssize_t n;
	int i, j, ret;

	ss = NULL;
	if ((n = nilfs_cleanerd_get_snapshot(cleanerd, &ss)) < 0)
		return n;

	for (i = 0; i < nilfs_vector_get_size(vdescv); i++) {
		for (j = i; j < nilfs_vector_get_size(vdescv); j++) {
			vdesc = nilfs_vector_get_element(vdescv, j);
			assert(vdesc != NULL);
			if (nilfs_vdesc_is_live(vdesc, cleanerd->c_protcno,
						ss, n)) {
				break;
			}
			if ((periodp =
			     nilfs_vector_get_new_element(periodv)) == NULL ||
			    (vblocknrp =
			     nilfs_vector_get_new_element(vblocknrv)) == NULL) {
				ret = -1;
				goto out;
			}
			*periodp = vdesc->vd_period;
			*vblocknrp = vdesc->vd_vblocknr;
		}
		if (j > i)
			nilfs_vector_delete_elements(vdescv, i, j - i);
	}

	ret = 0;

 out:
	if (ss != NULL)
		free(ss);
	return ret;
}

/**
 * nilfs_cleanerd_unify_period - unify periods of checkpoint numbers
 * @cleanerd: cleanerd object
 * @periodv: vector object storing checkpoint numbers
 */
static void nilfs_cleanerd_unify_period(struct nilfs_cleanerd *cleanerd,
					struct nilfs_vector *periodv)
{
	struct nilfs_period *base, *target;
	int i, j;

	nilfs_vector_sort(periodv, nilfs_comp_period);

	for (i = 0; i < nilfs_vector_get_size(periodv); i++) {
		base = nilfs_vector_get_element(periodv, i);
		assert(base != NULL);
		for (j = i + 1; j < nilfs_vector_get_size(periodv); j++) {
			target = nilfs_vector_get_element(periodv, j);
			assert(target != NULL);
			if (base->p_end < target->p_start)
				break;
			if (base->p_end < target->p_end)
				base->p_end = target->p_end;
		}

		if (j > i + 1)
			nilfs_vector_delete_elements(periodv, i + 1,
						     j - i - 1);
	}
}

#define NILFS_CLEANERD_NBDESCS	512

/**
 * nilfs_cleanerd_get_bdescs - get information on disk block addresses
 * @cleanerd: cleanerd object
 * @bdescv: vector object storing (descriptors of) disk block numbers
 */
static int nilfs_cleanerd_get_bdescs(struct nilfs_cleanerd *cleanerd,
				     struct nilfs_vector *bdescv)
{
	struct nilfs_bdesc *bdescs;
	size_t nbdescs, count;
	ssize_t n;
	int i;

	nilfs_vector_sort(bdescv, nilfs_comp_bdesc);

	bdescs = nilfs_vector_get_data(bdescv);
	nbdescs = nilfs_vector_get_size(bdescv);
	for (i = 0; i < nbdescs; i += n) {
		count = (nbdescs - i < NILFS_CLEANERD_NBDESCS) ?
			(nbdescs - i) : NILFS_CLEANERD_NBDESCS;
		if ((n = nilfs_get_bdescs(cleanerd->c_nilfs,
					  bdescs + i, count)) < 0)
			return -1;
	}

	return 0;
}

/**
 * nilfs_bdesc_is_live - judge if a disk block address is live or dead
 * @bdesc: descriptor object of the disk block address
 */
static int nilfs_bdesc_is_live(struct nilfs_bdesc *bdesc)
{
	return bdesc->bd_oblocknr == bdesc->bd_blocknr;
}

/**
 * nilfs_cleanerd_toss_bdescs - deselect deletable disk block numbers
 * @cleanerd: cleanerd object
 * @bdescv: vector object storing (descriptors of) disk block numbers
 *
 * nilfs_cleanerd_toss_bdescs() deselects disk block numbers of the DAT file
 * which don't belong to the latest DAT file.
 */
static int nilfs_cleanerd_toss_bdescs(struct nilfs_cleanerd *cleanerd,
				      struct nilfs_vector *bdescv)
{
	struct nilfs_bdesc *bdesc;
	int i, j;

	for (i = 0; i < nilfs_vector_get_size(bdescv); i++) {
		for (j = i; j < nilfs_vector_get_size(bdescv); j++) {
			bdesc = nilfs_vector_get_element(bdescv, j);
			assert(bdesc != NULL);
			if (nilfs_bdesc_is_live(bdesc))
				break;
		}
		if (j > i)
			nilfs_vector_delete_elements(bdescv, i, j - i);
	}
	return 0;
}

/**
 * nilfs_cleanerd_clean_segments - reclaim segments
 * @cleanerd: cleanerd object
 * @sustat: status information on segments
 * @segnums: array of segment numbers storing selected segments
 * @nsegs: size of the @segnums array
 * @prottime: lower limit of protected period
 */
static ssize_t nilfs_cleanerd_clean_segments(struct nilfs_cleanerd *cleanerd,
					     struct nilfs_sustat *sustat,
					     __u64 *segnums, size_t nsegs,
					     __u64 prottime)
{
	struct nilfs_vector *vdescv, *bdescv, *periodv, *vblocknrv;
	ssize_t n, ret = -1;

	if (nsegs == 0)
		return 0;

	vdescv = nilfs_vector_create(sizeof(struct nilfs_vdesc));
	bdescv = nilfs_vector_create(sizeof(struct nilfs_bdesc));
	periodv = nilfs_vector_create(sizeof(struct nilfs_period));
	vblocknrv = nilfs_vector_create(sizeof(__u64));
	if (vdescv == NULL || bdescv == NULL || periodv == NULL ||
	    vblocknrv == NULL)
		goto out_vec;

	n = nilfs_cleanerd_acc_blocks(cleanerd, sustat, segnums, nsegs,
				      vdescv, bdescv);
	if (n <= 0) {
		ret = n;
		goto out_vec;
	}

	ret = nilfs_cleanerd_get_vdesc(cleanerd, vdescv);
	if (ret < 0)
		goto out_vec;

	ret = nilfs_lock_write(cleanerd->c_nilfs);
	if (ret < 0)
		goto out_vec;

	ret = nilfs_cleanerd_update_prottime(cleanerd, prottime);
	if (ret < 0)
		goto out_lock;

	ret = nilfs_cleanerd_toss_vdescs(cleanerd, vdescv, periodv, vblocknrv);
	if (ret < 0)
		goto out_lock;

	nilfs_vector_sort(vdescv, nilfs_comp_vdesc_blocknr);
	nilfs_cleanerd_unify_period(cleanerd, periodv);

	ret = nilfs_cleanerd_get_bdescs(cleanerd, bdescv);
	if (ret < 0)
		goto out_lock;

	ret = nilfs_cleanerd_toss_bdescs(cleanerd, bdescv);
	if (ret < 0)
		goto out_lock;

	ret = nilfs_clean_segments(cleanerd->c_nilfs,
				   nilfs_vector_get_data(vdescv),
				   nilfs_vector_get_size(vdescv),
				   nilfs_vector_get_data(periodv),
				   nilfs_vector_get_size(periodv),
				   nilfs_vector_get_data(vblocknrv),
				   nilfs_vector_get_size(vblocknrv),
				   nilfs_vector_get_data(bdescv),
				   nilfs_vector_get_size(bdescv),
				   segnums, n);
	if (ret < 0)
		syslog(LOG_ERR, "%m");
	else
		ret = n;

 out_lock:
	if (nilfs_unlock_write(cleanerd->c_nilfs) < 0)
		ret = -1;

 out_vec:
	if (vdescv != NULL)
		nilfs_vector_destroy(vdescv);
	if (bdescv != NULL)
		nilfs_vector_destroy(bdescv);
	if (periodv != NULL)
		nilfs_vector_destroy(periodv);
	if (vblocknrv != NULL)
		nilfs_vector_destroy(vblocknrv);
	return ret;
}

#define DEVNULL	"/dev/null"
#define ROOTDIR	"/"

static int daemonize(int nochdir, int noclose, int nofork)
{
	pid_t pid;

	if (!nofork) {
		if ((pid = fork()) < 0)
			return -1;
		else if (pid != 0)
			/* parent */
			_exit(0);
	}

	/* child or nofork */
	if (setsid() < 0)
		return -1;

	/* umask(0); */

	if (!nochdir && (chdir(ROOTDIR) < 0))
		return -1;

	if (!noclose) {
		close(0);
		close(1);
		close(2);
		if (open(DEVNULL, O_RDONLY) < 0)
			return -1;
		if (open(DEVNULL, O_WRONLY) < 0)
			return -1;
		if (open(DEVNULL, O_WRONLY) < 0)
			return -1;
	}
	return 0;
}

static RETSIGTYPE handle_sigterm(int signum)
{
	siglongjmp(nilfs_cleanerd_env, 1);
}

static RETSIGTYPE handle_sighup(int signum)
{
	nilfs_cleanerd_reload_config = 1;
}

static int set_sigterm_handler(void)
{
	struct sigaction act;

	act.sa_handler = handle_sigterm;
	sigfillset(&act.sa_mask);
	act.sa_flags = 0;
	return sigaction(SIGTERM, &act, NULL);
}

static int set_sighup_handler(void)
{
	struct sigaction act;

	act.sa_handler = handle_sighup;
	sigfillset(&act.sa_mask);
	act.sa_flags = 0;
	return sigaction(SIGHUP, &act, NULL);
}

#define timeval_to_timespec(tv, ts)		\
do {						\
	(ts)->tv_sec = (tv)->tv_sec;		\
	(ts)->tv_nsec = (tv)->tv_usec * 1000;	\
} while (0)

/**
 * nilfs_cleanerd_clean_loop - main loop of the cleaner daemon
 * @cleanerd: cleanerd object
 */
static int nilfs_cleanerd_clean_loop(struct nilfs_cleanerd *cleanerd)
{
	struct nilfs_sustat sustat;
	struct timeval curr, target, diff;
	struct timespec timeout;
	__u64 prev_nongc_ctime = 0, prottime;
	__u64 segnums[NILFS_CLDCONFIG_NSEGMENTS_PER_CLEAN_MAX];
	sigset_t sigset;
	int i, ns, n;

	sigemptyset(&sigset);
	if (sigprocmask(SIG_SETMASK, &sigset, NULL) < 0) {
		syslog(LOG_ERR, "cannot set signal mask: %m");
		return -1;
	}
	sigaddset(&sigset, SIGHUP);

	if (set_sigterm_handler() < 0) {
		syslog(LOG_ERR, "cannot set SIGTERM signal handler: %m");
		return -1;
	}
	if (set_sighup_handler() < 0) {
		syslog(LOG_ERR, "cannot set SIGHUP signal handler: %m");
		return -1;
	}

	nilfs_cleanerd_reload_config = 0;

	if (gettimeofday(&target, NULL) < 0) {
		syslog(LOG_ERR, "cannot get time: %m");
		return -1;
	}
	target.tv_sec += cleanerd->c_config.cf_cleaning_interval;
	cleanerd->c_running = 1;

	while (1) {
		if (sigprocmask(SIG_BLOCK, &sigset, NULL) < 0) {
			syslog(LOG_ERR, "cannot set signal mask: %m");
			return -1;
		}

		if (nilfs_cleanerd_reload_config) {
			if (nilfs_cleanerd_reconfig(cleanerd)) {
				syslog(LOG_ERR, "cannot configure: %m");
				return -1;
			}
			nilfs_cleanerd_reload_config = 0;
			syslog(LOG_INFO, "configuration file reloaded");
		}

		if (nilfs_get_sustat(cleanerd->c_nilfs, &sustat) < 0) {
			syslog(LOG_ERR, "cannot get segment usage stat: %m");
			return -1;
		}
		if (sustat.ss_nongc_ctime != prev_nongc_ctime) {
			cleanerd->c_running = 1;
			prev_nongc_ctime = sustat.ss_nongc_ctime;
		}
		if (!cleanerd->c_running)
			goto sleep;

		syslog(LOG_DEBUG, "ncleansegs = %llu",
		       (unsigned long long)sustat.ss_ncleansegs);

		ns = nilfs_cleanerd_select_segments(
			cleanerd, &sustat, segnums,
			cleanerd->c_config.cf_nsegments_per_clean,
			&timeout, &prottime);
		if (ns < 0) {
			syslog(LOG_ERR, "cannot select segments: %m");
			return -1;
		}
		syslog(LOG_DEBUG, "%d segment%s selected to be cleaned",
		       ns, (ns <= 1) ? "" : "s");

		if (ns > 0) {
			n = nilfs_cleanerd_clean_segments(cleanerd, &sustat,
							  segnums, ns,
							  prottime);
			if (n < 0) {
				syslog(LOG_ERR, "cannot clean segments: %m");
				return -1;
			} else if (n > 0) {
				for (i = 0; i < n; i++)
					syslog(LOG_DEBUG,
					       "segment %llu cleaned",
					       (unsigned long long)segnums[i]);
			} else {
				syslog(LOG_DEBUG, "no segments cleaned");
			}

			if (gettimeofday(&curr, NULL) < 0) {
				syslog(LOG_ERR, "cannot get current time: %m");
				return -1;
			}
			/* timercmp() does not work for '>=' or '<='. */
			/* curr >= target */
			if (!timercmp(&curr, &target, <)) {
				target = curr;
				target.tv_sec +=
					cleanerd->c_config.cf_cleaning_interval;
				syslog(LOG_DEBUG, "adjust interval");
				continue;
			}
			timersub(&target, &curr, &diff);
			timeval_to_timespec(&diff, &timeout);
			target.tv_sec +=
				cleanerd->c_config.cf_cleaning_interval;
		}

 sleep:
		if (sigprocmask(SIG_UNBLOCK, &sigset, NULL) < 0) {
			syslog(LOG_ERR, "cannot set signal mask: %m");
			return -1;
		}

		syslog(LOG_DEBUG, "wait %ld.%09ld",
		       timeout.tv_sec, timeout.tv_nsec);
		if (nanosleep(&timeout, NULL) < 0) {
			if (errno != EINTR) {
				syslog(LOG_ERR, "cannot sleep: %m");
				return -1;
			}
			cleanerd->c_running = 1;
		}
		syslog(LOG_DEBUG, "wake up");
	}
}

int main(int argc, char *argv[])
{
	char *progname, *conffile;
	const char *dev;
	int status, nofork, c;
#ifdef _GNU_SOURCE
	int option_index;
#endif	/* _GNU_SOURCE */

	progname = (strrchr(argv[0], '/') != NULL) ?
		strrchr(argv[0], '/') + 1 : argv[0];
	conffile = NILFS_CLEANERD_CONFFILE;
	nofork = 0;
	dev = NULL;
	status = 0;
#ifdef _GNU_SOURCE
	while ((c = getopt_long(argc, argv, "c:hn",
				long_option, &option_index)) >= 0) {
#else	/* !_GNU_SOURCE */
	while ((c = getopt(argc, argv, "c:hn")) >= 0) {
#endif	/* _GNU_SOURCE */

		switch (c) {
		case 'c':
			conffile = optarg;
			break;
		case 'h':
			nilfs_cleanerd_usage(progname);
			exit(0);
		case 'n':
			/* internal option for mount.nilfs2 only */
			nofork = 1;
			break;
		default:
			nilfs_cleanerd_usage(progname);
			exit(1);
		}
	}

	if (optind < argc)
		dev = argv[optind++];

	if (daemonize(0, 0, nofork) < 0) {
		fprintf(stderr, "%s: %s\n", progname, strerror(errno));
		exit(1);
	}

	openlog(progname, LOG_PID, LOG_DAEMON);
	syslog(LOG_INFO, "start");

	if ((nilfs_cleanerd = nilfs_cleanerd_create(dev, conffile)) == NULL) {
		syslog(LOG_ERR, "cannot create cleanerd on %s", dev);
		status = 1;
		goto out;
	}

	if (!sigsetjmp(nilfs_cleanerd_env, 1)) {
		if (nilfs_cleanerd_clean_loop(nilfs_cleanerd) < 0)
			status = 1;
	}

	nilfs_cleanerd_destroy(nilfs_cleanerd);

 out:
	syslog(LOG_INFO, "shutdown");
	closelog();

	exit(status);
}
