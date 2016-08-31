/*
 * cnoconv.c - checkpoint number converter (obsolete)
 *
 * Licensed under LGPLv2: the complete text of the GNU Lesser General
 * Public License can be found in COPYING file of the nilfs-utils
 * package.
 *
 * Copyright (C) 2008-2012 Nippon Telegraph and Telephone Corporation.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif	/* HAVE_CONFIG_H */

#include <stdio.h>

#if HAVE_STDLIB_H
#include <stdlib.h>
#endif	/* HAVE_STDLIB_H */

#include "util.h"
#include "cnoconv.h"

/* Context of checkpoint number/time converter */
struct nilfs_cnoconv {
	struct nilfs *nilfs;
	nilfs_cno_t prevcno;	/* start number of protected checkpoint */
	__u64 prevtime;		/* start time of protection */
};

void nilfs_cnoconv_reset(struct nilfs_cnoconv *cnoconv)
{
	cnoconv->prevcno = 0;
	cnoconv->prevtime = 0;
}

struct nilfs_cnoconv *nilfs_cnoconv_create(struct nilfs *nilfs)
{
	struct nilfs_cnoconv *cnoconv;

	cnoconv = malloc(sizeof(*cnoconv));
	if (cnoconv) {
		cnoconv->nilfs = nilfs;
		nilfs_cnoconv_reset(cnoconv);
	}
	return cnoconv;
}

void nilfs_cnoconv_destroy(struct nilfs_cnoconv *cnoconv)
{
	free(cnoconv);
}

#define NILFS_CNOCONV_NCPINFO	512

/**
 * nilfs_conconv_time2cno - reverse map time to checkpoint number
 * @cnoconv: nilfs_cnoconv struct
 * @time: time to be converted
 * @cnop: buffer to store resultant checkpoint number
 */
int nilfs_cnoconv_time2cno(struct nilfs_cnoconv *cnoconv, __u64 time,
			   nilfs_cno_t *cnop)
{
	struct nilfs_cpstat cpstat;
	struct nilfs_cpinfo cpinfo[NILFS_CNOCONV_NCPINFO];
	nilfs_cno_t cno;
	size_t count;
	ssize_t n;
	int i;

	if (nilfs_get_cpstat(cnoconv->nilfs, &cpstat) < 0)
		return -1;

	if (cnoconv->prevtime > time)
		cnoconv->prevcno = 0;
	else if (cnoconv->prevtime == time)
		goto out_unchanged; /* time unchanged */

	cno = (cnoconv->prevcno == 0) ? NILFS_CNO_MIN : cnoconv->prevcno;
	while (cno < cpstat.cs_cno) {
		count = min_t(nilfs_cno_t, cpstat.cs_cno - cno,
			      NILFS_CNOCONV_NCPINFO);
		n = nilfs_get_cpinfo(cnoconv->nilfs, cno, NILFS_CHECKPOINT,
				     cpinfo, count);
		if (n < 0)
			return -1;
		if (n == 0)
			break;

		for (i = 0; i < n; i++) {
			if (cpinfo[i].ci_create >= time) {
				cnoconv->prevcno = cpinfo[i].ci_cno;
				goto out;
			}
		}
		cno = cpinfo[n - 1].ci_cno + 1;
	}
	cnoconv->prevcno = cno;
out:
	cnoconv->prevtime = time;

out_unchanged:
	*cnop = cnoconv->prevcno;
	return 0;
}
