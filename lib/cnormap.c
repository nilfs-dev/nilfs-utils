/*
 * cnormap.c - checkpoint number reverse mapper
 *
 * Licensed under LGPLv2: the complete text of the GNU Lesser General
 * Public License can be found in COPYING file of the nilfs-utils
 * package.
 *
 * Credits:
 *     Ryusuke Konishi <konishi.ryusuke@lab.ntt.co.jp>
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif	/* HAVE_CONFIG_H */

#include <stdio.h>

#if HAVE_STDLIB_H
#include <stdlib.h>
#endif	/* HAVE_STDLIB_H */

#if HAVE_STRING_H
#include <string.h>	/* memset() */
#endif	/* HAVE_STRING_H */

#if HAVE_TIME_H
#include <time.h>	/* clock_gettime() */
#endif	/* HAVE_TIME_H */

#include <errno.h>
#include "compat.h"
#include "util.h"
#include "cnormap.h"
#include "vector.h"


/* Checkpoint number and the corresponding clock time */
struct nilfs_cptime {
	nilfs_cno_t cno;	/* Checkpoint number */
	__s64 time;		/* Corresponding clock time */
};

/* Span of checkpoints */
struct nilfs_cpspan {
	struct nilfs_cptime start;	/* Start time */
	struct nilfs_cptime end;	/* End time */
	unsigned int approx_ncp;	/* Approximate number of checkpoints */
};

#define NCP_PER_SPAN		4096	/* Number of checkpoints per span */
#define INTERVAL_ON_REWIND	1	/*
					 * Approximate interval value
					 * (in seconds) used between two spans
					 * where rewind occurs.
					 */

/* Checkpoint number/time reverse mapper */
struct nilfs_cnormap {
	struct nilfs *nilfs;
	struct nilfs_vector *cphist;	/* History of time progress */
	__u64 cphist_elapsed_time;	/* Elapsed time of cphist */
	__s64 base_time;		/* Base time */
	__s64 base_clock;		/* Monotonic clock at the base time */

	int has_clock_boottime : 1;	/*
					 * Flag that indicates whether
					 * clock_gettime(CLOCK_BOOTTIME, )
					 * is supported or not on the system.
					 */
	int has_clock_realtime_coarse : 1; /* Has CLOCK_REALTIME_COARSE */
	int has_clock_monotonic_coarse : 1; /* Has CLOCK_MONOTONIC_COARSE */
};

struct nilfs_cnormap *nilfs_cnormap_create(struct nilfs *nilfs)
{
	struct nilfs_cnormap *cnormap;
	struct timespec ts;
	int errsv;
	int ret;

	cnormap = malloc(sizeof(*cnormap));
	if (!cnormap)
		return NULL;

	memset(cnormap, 0, sizeof(*cnormap));
	cnormap->nilfs = nilfs;

	/* Begin of the clock feature test */
	errsv = errno;

	ret = clock_gettime(CLOCK_REALTIME_COARSE, &ts);
	cnormap->has_clock_realtime_coarse = !(ret < 0 && errno == EINVAL);

	ret = clock_gettime(CLOCK_MONOTONIC_COARSE, &ts);
	cnormap->has_clock_monotonic_coarse = !(ret < 0 && errno == EINVAL);

	ret = clock_gettime(CLOCK_BOOTTIME, &ts);
	cnormap->has_clock_boottime = !(ret < 0 && errno == EINVAL);

	errno = errsv;
	/* End of the clock feature test */

	cnormap->cphist = nilfs_vector_create(sizeof(struct nilfs_cpspan));
	if (!cnormap->cphist) {
		free(cnormap);
		return NULL;
	}
	return cnormap;
}

void nilfs_cnormap_destroy(struct nilfs_cnormap *cnormap)
{
	nilfs_vector_destroy(cnormap->cphist);
	free(cnormap);
}

/**
 * nilfs_enum_cpinfo_forward - enumrate checkpoints forward
 * @nilfs: nilfs object
 * @cpstat: nilfs_cpstat struct
 * @opt_start: start checkpoint number [optional]
 * @opt_count: number of checkpoints to be enumerated [optional]
 * @out: callback function used to process a checkpoint
 * @ctx: context information passed to @out function
 *
 * If function @out returns a negative value,
 * nilfs_enum_cpinfo_forward() returns immediately with a minus one.
 * If @out returns zero, the current checkpoint is skipped.  If it
 * returns one, then the rest count is decreased and
 * nilfs_enum_cpinfo_forward() returns successfully when the count
 * reaches zero.  If @out returns a value larger than one, then
 * nilfs_enum_cpinfo_forward() returns immediately with a success
 * value (0).
 */
static int nilfs_enum_cpinfo_forward(struct nilfs *nilfs,
				     const struct nilfs_cpstat *cpstat,
				     nilfs_cno_t opt_start,
				     __u64 opt_count,
				     int (*out)(const struct nilfs_cpinfo *,
						void *),
				     void *ctx)
{
	const size_t _NCPINFO = 512;
	struct nilfs_cpinfo *cpibuf, *cpi;
	nilfs_cno_t sidx; /* start index (inclusive) */
	__u64 rest;
	size_t req_count;
	ssize_t n;

	rest = opt_count && opt_count < cpstat->cs_ncps ?
		opt_count : cpstat->cs_ncps;
	sidx = opt_start ? opt_start : NILFS_CNO_MIN;

	cpibuf = malloc(sizeof(*cpibuf) * _NCPINFO);
	if (cpibuf == NULL)
		return -1;

	while (rest > 0 && sidx < cpstat->cs_cno) {
		req_count = min_t(size_t, rest, _NCPINFO);

		n = nilfs_get_cpinfo(nilfs, sidx, NILFS_CHECKPOINT, cpibuf,
				     req_count);
		if (n < 0)
			goto failed;
		if (!n)
			break;

		for (cpi = cpibuf; cpi < cpibuf + n; cpi++) {
			int ret;

			ret = out(cpi, ctx);
			if (ret < 0)
				goto failed;
			else if (ret == 0)
				continue;
			else if (ret > 1)
				goto out;

			rest--;
		}
		sidx = cpibuf[n - 1].ci_cno + 1;
	}
out:
	free(cpibuf);
	return 0;
failed:
	free(cpibuf);
	return -1;
}

/**
 * nilfs_enum_cpinfo_backward - enumrate checkpoints backward
 * @nilfs: nilfs object
 * @cpstat: nilfs_cpstat struct
 * @opt_start: start checkpoint number [optional]
 * @opt_count: number of checkpoints to be enumerated [optional]
 * @out: callback function used to process a checkpoint
 * @ctx: context information passed to @out function
 *
 * If function @out returns a negative value,
 * nilfs_enum_cpinfo_backward() returns immediately with a minus one.
 * If @out returns zero, the current checkpoint is skipped.  If it
 * returns one, then the rest count is decreased and
 * nilfs_enum_cpinfo_backward() returns successfully when the count
 * reaches zero.  If @out returns a value larger than one, then
 * nilfs_enum_cpinfo_backward() returns immediately with a success
 * value (0).
 */
static int nilfs_enum_cpinfo_backward(struct nilfs *nilfs,
				      const struct nilfs_cpstat *cpstat,
				      nilfs_cno_t opt_start,
				      __u64 opt_count,
				      int (*out)(const struct nilfs_cpinfo *,
						 void *),
				      void *ctx)
{
	const size_t _MINDELTA = 64;	/* Minimum delta for backward search */
	const size_t _NCPINFO = 512;
	struct nilfs_cpinfo *cpibuf, *cpi;
	nilfs_cno_t sidx; /* start index (inclusive) */
	nilfs_cno_t eidx; /* end index (exclusive) */
	nilfs_cno_t prev_head = 0;
	enum {
		INIT_ST,	/* Initial state */
		NORMAL_ST,	/* Normal state */
		ACCEL_ST,	/* Accelerate state */
		DECEL_ST,	/* Decelerate state */
	};
	int state = INIT_ST;
	__u64 rest, delta, v;
	size_t req_count;
	ssize_t n;

	rest = opt_count && opt_count < cpstat->cs_ncps ?
		opt_count : cpstat->cs_ncps;
	if (!rest)
		return 0;

	eidx = opt_start && opt_start < cpstat->cs_cno ?
		opt_start + 1 : cpstat->cs_cno;

	cpibuf = malloc(sizeof(*cpibuf) * _NCPINFO);
	if (cpibuf == NULL)
		return -1;

recalc_delta:
	delta = min_t(__u64, _NCPINFO, max_t(__u64, rest, _MINDELTA));
	v = delta;

	while (eidx > NILFS_CNO_MIN) {
		if (eidx < NILFS_CNO_MIN + v || state == INIT_ST)
			sidx = NILFS_CNO_MIN;
		else
			sidx = eidx - v;

		req_count = min_t(size_t, state == NORMAL_ST ? eidx - sidx : 1,
				  _NCPINFO);

		n = nilfs_get_cpinfo(nilfs, sidx, NILFS_CHECKPOINT, cpibuf,
				     req_count);
		if (n < 0)
			goto failed;
		if (!n)
			break;

		if (state == INIT_ST) {
			/*
			 * This state makes succesive
			 * nilfs_get_cpinfo() calls much faster by
			 * setting minimum checkpoint number in nilfs
			 * struct.
			 */
			if (cpibuf[0].ci_cno >= eidx)
				goto out; /* out of range */
			state = NORMAL_ST;
			continue;
		} else if (cpibuf[0].ci_cno == prev_head) {
			/* No younger checkpoint was found */

			if (sidx == NILFS_CNO_MIN)
				break;

			/* go further back */
			switch (state) {
			case NORMAL_ST:
				state = ACCEL_ST;
				/* fall through */
			case ACCEL_ST:
				if ((v << 1) > v)
					v <<= 1;
				break;
			case DECEL_ST:
				state = NORMAL_ST;
				v = delta;
				break;
			}
			eidx = sidx;
			continue;
		} else {
			switch (state) {
			case ACCEL_ST:
			case DECEL_ST:
				if (cpibuf[n - 1].ci_cno + 1 < prev_head) {
					/* search again more slowly */
					v >>= 1;
					if (v <= delta) {
						state = NORMAL_ST;
						v = delta;
					} else {
						state = DECEL_ST;
					}
					continue;
				}
				break;
			default:
				break;
			}
		}

		state = NORMAL_ST;
		cpi = &cpibuf[n - 1];
		do {
			int ret;

			if (cpi->ci_cno >= eidx)
				continue;

			ret = out(cpi, ctx);
			if (ret < 0)
				goto failed;
			else if (ret == 0)
				continue;
			else if (ret > 1)
				goto out;

			rest--;
			if (rest == 0)
				goto out;
		} while (--cpi >= cpibuf);

		prev_head = cpibuf[0].ci_cno;
		eidx = sidx;
		goto recalc_delta;
	}
out:
	free(cpibuf);
	return 0;
failed:
	free(cpibuf);
	return -1;
}

static int nilfs_cnormap_get_monotonic_clock(struct nilfs_cnormap *cnormap,
					     __s64 *monotonic_clock)
{
	struct timespec ts;
	clockid_t clkid;
	int errsv = errno;
	int ret;

retry_gettime:
	clkid = cnormap->has_clock_boottime ?
		CLOCK_BOOTTIME : (cnormap->has_clock_monotonic_coarse ?
				  CLOCK_MONOTONIC_COARSE : CLOCK_MONOTONIC);
	ret = clock_gettime(clkid, &ts);
	if (ret < 0) {
		if (errno == EINVAL) {
			/* System has been changed ? */
			if (cnormap->has_clock_boottime)
				cnormap->has_clock_boottime = 0;
			else if (cnormap->has_clock_monotonic_coarse)
				cnormap->has_clock_monotonic_coarse = 0;
			else
				return -1;

			errno = errsv;
			goto retry_gettime;
		}
		return -1;
	}

	*monotonic_clock = ts.tv_sec;
	return 0;
}

static int nilfs_cnormap_get_realtime_clock(struct nilfs_cnormap *cnormap,
					    __s64 *realtime_clock)
{
	struct timespec ts;
	clockid_t clkid;
	int errsv = errno;
	int ret;

retry_gettime:
	clkid = cnormap->has_clock_realtime_coarse ?
		CLOCK_REALTIME_COARSE : CLOCK_REALTIME;
	ret = clock_gettime(clkid, &ts);
	if (ret < 0) {
		if (cnormap->has_clock_realtime_coarse && errno == EINVAL) {
			/* System has been changed ? */
			cnormap->has_clock_realtime_coarse = 0;
			errno = errsv;
			goto retry_gettime;
		}
		return -1;
	}

	*realtime_clock = ts.tv_sec;
	return 0;
}

/* Scan state for nilfs_enum_cpinfo_backward() */
enum nilfs_cpinfo_scan_state {
	NILFS_CPINFO_SCAN_INIT_ST,	/* Initial state */
	NILFS_CPINFO_SCAN_NORMAL_ST,	/* On going */
	NILFS_CPINFO_SCAN_DONE_ST,	/* Finished */
};

/* Context of the callback function passed to nilfs_enum_cpinfo_backward() */
struct nilfs_cpinfo_scan_context {
	struct nilfs_cnormap *cnormap;	/* cnormap struct */
	unsigned int index;		/* Index of the current span */
	int state;			/* Scan state */
	__s64 time;			/* Base time in seconds */
	__u64 period;			/* Period to be tracked back */
	nilfs_cno_t min_incl_cno;	/* Min. included checkpoint number */
};

static __u64 gap_of_spans(__s64 gap_from, __s64 gap_to)
{
	return gap_to >= gap_from ? gap_to - gap_from : INTERVAL_ON_REWIND;
}

static int nilfs_cpinfo_scan_backward(const struct nilfs_cpinfo *cpinfo,
				      void *arg)
{
	struct nilfs_cpinfo_scan_context *ctx = arg;
	struct nilfs_cnormap *cnormap = ctx->cnormap;
	struct nilfs_cpspan *cpspan;

	if (ctx->state == NILFS_CPINFO_SCAN_INIT_ST) {
		cpspan = nilfs_vector_get_new_element(cnormap->cphist);
		if (!cpspan)
			return -1;
		cpspan->start.cno = cpinfo->ci_cno;
		cpspan->start.time = cpinfo->ci_create;
		cpspan->end.cno = cpinfo->ci_cno;
		cpspan->end.time = cpinfo->ci_create;
		cpspan->approx_ncp = 1;
		cnormap->cphist_elapsed_time = 0;
		if (ctx->time > cpinfo->ci_create) {
			if (ctx->period < ctx->time - cpinfo->ci_create) {
				ctx->state = NILFS_CPINFO_SCAN_DONE_ST;
				return 2; /* Escape */
			}
			ctx->period -= ctx->time - cpinfo->ci_create;
		}
		ctx->state = NILFS_CPINFO_SCAN_NORMAL_ST;
		goto out;
	}

	cpspan = nilfs_vector_get_element(cnormap->cphist, ctx->index);
	if (!cpspan) {
		errno = EINVAL;
		return -1;
	}

	if (ctx->state == NILFS_CPINFO_SCAN_NORMAL_ST) {
		__u64 elapsed_time;

		if (cpinfo->ci_create == cpspan->start.time) {
			cpspan->start.cno = cpinfo->ci_cno;
			cpspan->approx_ncp++;
			/*
			 * approx_ncp may exceed NCP_PER_SPAN if
			 * checkpoints with the same ci_create continue.
			 */
			goto out;
		}

		elapsed_time = cnormap->cphist_elapsed_time +
			gap_of_spans(cpinfo->ci_create, cpspan->start.time);

		if (cpinfo->ci_create > cpspan->start.time ||
		    cpspan->approx_ncp >= NCP_PER_SPAN) {
			cpspan = nilfs_vector_get_new_element(cnormap->cphist);
			if (!cpspan)
				return -1;
			cpspan->start.cno = cpinfo->ci_cno;
			cpspan->start.time = cpinfo->ci_create;
			cpspan->end.cno = cpinfo->ci_cno;
			cpspan->end.time = cpinfo->ci_create;
			cpspan->approx_ncp = 1;
			ctx->index++;
		} else {
			cpspan->start.cno = cpinfo->ci_cno;
			cpspan->start.time = cpinfo->ci_create;
			cpspan->approx_ncp++;
		}

		cnormap->cphist_elapsed_time = elapsed_time;

		if (elapsed_time > ctx->period) {
			ctx->state = NILFS_CPINFO_SCAN_DONE_ST;
			return 2; /* Escape */
		}
	} else {
		errno = EINVAL;
		return -1;
	}
out:
	ctx->min_incl_cno = cpspan->start.cno;
	return 1; /* Get next */
}

static int nilfs_cpinfo_scan_forward(const struct nilfs_cpinfo *cpinfo,
				     void *arg)
{
	struct nilfs_cpinfo_scan_context *ctx = arg;
	struct nilfs_cnormap *cnormap = ctx->cnormap;
	struct nilfs_cpspan *cpspan;
	__u64 elapsed_time;

	cpspan = nilfs_vector_get_element(cnormap->cphist, 0);
	if (!cpspan || ctx->state != NILFS_CPINFO_SCAN_NORMAL_ST) {
		errno = EINVAL;
		return -1;
	}

	if (cpinfo->ci_create == cpspan->end.time) {
		cpspan->end.cno = cpinfo->ci_cno;
		cpspan->approx_ncp++;
		/*
		 * approx_ncp may exceed NCP_PER_SPAN if
		 * checkpoints with the same ci_create continue.
		 */
		return 1; /* Get next */
	}

	elapsed_time = cnormap->cphist_elapsed_time +
		gap_of_spans(cpspan->end.time, cpinfo->ci_create);

	if (cpinfo->ci_create < cpspan->end.time ||
	    cpspan->approx_ncp >= NCP_PER_SPAN) {
		cpspan = nilfs_vector_insert_element(cnormap->cphist, 0);
		if (!cpspan)
			return -1;
		cpspan->start.cno = cpinfo->ci_cno;
		cpspan->start.time = cpinfo->ci_create;
		cpspan->end.cno = cpinfo->ci_cno;
		cpspan->end.time = cpinfo->ci_create;
		cpspan->approx_ncp = 1;
		ctx->index++; /* update index of the original span */
	} else {
		cpspan->end.cno = cpinfo->ci_cno;
		cpspan->end.time = cpinfo->ci_create;
		cpspan->approx_ncp++;
	}

	cnormap->cphist_elapsed_time = elapsed_time;

	if (elapsed_time >= ctx->period) {
		ctx->state = NILFS_CPINFO_SCAN_DONE_ST;
		ctx->min_incl_cno = cpinfo->ci_cno;
		return 2; /* Escape */
	}

	return 1; /* Get next */
}

/**
 * nilfs_cnormap_cphist_init - generate cphist tracking back checkpoints
 * @cnormap: nilfs_cnormap struct
 * @cpstat: pointer to cpstat struct
 * @monotonic_clock: the current value of system clock (monotonic clock)
 * @period: period to be tracked back
 * @cnop: buffer to store the minimum included checkpoint number
 */
static int nilfs_cnormap_cphist_init(struct nilfs_cnormap *cnormap,
				     const struct nilfs_cpstat *cpstat,
				     __s64 monotonic_clock, __u64 period,
				     nilfs_cno_t *cnop)
{
	struct nilfs_cpinfo_scan_context ctx;
	__s64 realtime_clock;
	int ret;

	ret = nilfs_cnormap_get_realtime_clock(cnormap, &realtime_clock);
	if (ret < 0)
		goto out;

	ctx.cnormap = cnormap;
	ctx.index = 0;
	ctx.time = realtime_clock;
	ctx.period = period;
	ctx.state = NILFS_CPINFO_SCAN_INIT_ST;
	ctx.min_incl_cno = NILFS_CNO_MAX;

	ret = nilfs_enum_cpinfo_backward(cnormap->nilfs, cpstat, 0, 0,
					 nilfs_cpinfo_scan_backward, &ctx);
	if (ctx.state == NILFS_CPINFO_SCAN_INIT_ST) {
		if (!ret)
			*cnop = NILFS_CNO_MAX;
		goto out;
	}

	/* Got one or more valid checkpoints */
	cnormap->base_time = realtime_clock;
	cnormap->base_clock = monotonic_clock;
	if (ret == 0)
		*cnop = ctx.min_incl_cno;
out:
	return ret;
}

static int
nilfs_cnormap_cphist_extend_forward(struct nilfs_cnormap *cnormap,
				    const struct nilfs_cpstat *cpstat,
				    __s64 monotonic_clock, __u64 period,
				    nilfs_cno_t end_cno, nilfs_cno_t *cnop)
{
	struct nilfs_cpinfo_scan_context ctx;
	struct nilfs_cpspan *latest;
	unsigned int max_index;
	__s64 realtime_clock;
	int ret;

	ret = nilfs_cnormap_get_realtime_clock(cnormap, &realtime_clock);
	if (ret < 0)
		goto out;

	latest = nilfs_vector_get_element(cnormap->cphist, 0);
	BUG_ON(!latest);

	max_index = nilfs_vector_get_size(cnormap->cphist) - 1;
	if (max_index > 0) {
		nilfs_vector_delete_elements(cnormap->cphist, 1, max_index);
		cnormap->cphist_elapsed_time =
			latest->end.time - latest->start.time;
	}

	ctx.cnormap = cnormap;
	ctx.index = 0;
	ctx.time = 0;
	ctx.period = period;
	ctx.state = NILFS_CPINFO_SCAN_NORMAL_ST;
	ctx.min_incl_cno = NILFS_CNO_MAX;

	ret = nilfs_enum_cpinfo_forward(cnormap->nilfs, cpstat, end_cno + 1,
					0, nilfs_cpinfo_scan_forward, &ctx);

	/* Got one or more valid checkpoints */
	cnormap->base_time = realtime_clock;
	cnormap->base_clock = monotonic_clock;
	if (ret == 0)
		*cnop = ctx.min_incl_cno;
out:
	return ret;
}

static int
nilfs_cnormap_cphist_extend_backward(struct nilfs_cnormap *cnormap,
				     const struct nilfs_cpstat *cpstat,
				     __u64 period, unsigned int index,
				     nilfs_cno_t start_cno, nilfs_cno_t *cnop)
{
	struct nilfs_cpinfo_scan_context ctx;
	int ret;

	if (start_cno == 0) {
		errno = EINVAL;
		return -1;
	}
	if (start_cno == NILFS_CNO_MIN) {
		*cnop = NILFS_CNO_MIN;
		return 0;
	}

	ctx.cnormap = cnormap;
	ctx.index = index;
	ctx.time = 0;
	ctx.period = period;
	ctx.state = NILFS_CPINFO_SCAN_NORMAL_ST;
	ctx.min_incl_cno = start_cno;

	ret = nilfs_enum_cpinfo_backward(cnormap->nilfs, cpstat, start_cno - 1,
					 0, nilfs_cpinfo_scan_backward, &ctx);
	if (ret == 0)
		*cnop = ctx.min_incl_cno;
	return ret;
}

struct nilfs_cpinfo_find_context {
	__s64 time;			/* Target time */
	nilfs_cno_t end_cno;		/* End checkpoint number */
	struct nilfs_cptime min_incl_cp;/* Min. included checkpoint */
	struct nilfs_cptime max_excl_cp;/* Max. excluded checkpoint */
	unsigned int nskips;		/* Passed checkpoint count */
};

static int nilfs_cpinfo_find(const struct nilfs_cpinfo *cpinfo, void *arg)
{
	struct nilfs_cpinfo_find_context *ctx = arg;

	if (cpinfo->ci_cno > ctx->end_cno)
		return 2; /* Abort (end checkpoint was lost) */

	if (cpinfo->ci_create >= ctx->time) {
		ctx->min_incl_cp.cno = cpinfo->ci_cno;
		ctx->min_incl_cp.time = cpinfo->ci_create;
		return 2; /* Escape (target checkpoint was found) */
	}
	ctx->max_excl_cp.cno = cpinfo->ci_cno;
	ctx->max_excl_cp.time = cpinfo->ci_create;
	ctx->nskips++;
	return 0; /* Otherwise, skip */
}

/**
 * nilfs_cnormap_cphist_search - search min. inclusive checkpoint on cphist
 * @cnormap: nilfs_cnormap struct
 * @cpstat: pointer to cpstat struct
 * @period: target period from the start of cphist
 * @cnop: buffer to store the minimum included checkpoint number
 */
static int nilfs_cnormap_cphist_search(struct nilfs_cnormap *cnormap,
				       const struct nilfs_cpstat *cpstat,
				       __u64 period, nilfs_cno_t *cnop)
{
	struct nilfs_cpspan *target;
	unsigned int max_index, index;
	nilfs_cno_t  min_incl_cno;
	__u64 delta;
	int ret;

	if (nilfs_vector_get_size(cnormap->cphist) == 0 || period == 0) {
		errno = EINVAL; /* Bug */
		return -1;
	}

	max_index = nilfs_vector_get_size(cnormap->cphist) - 1;
	target = nilfs_vector_get_element(cnormap->cphist, max_index);
	BUG_ON(!target);

	index = max_index;
	delta = 0;
	while (period > delta + target->end.time - target->start.time) {
		struct nilfs_cpspan *curr = target;
		__u64 width, gap;

		width = curr->end.time - curr->start.time;
		if (index == 0) {
			errno = EINVAL;
			return -1;
		}
		target = nilfs_vector_get_element(cnormap->cphist, index - 1);
		BUG_ON(!target);

		gap = gap_of_spans(curr->end.time, target->start.time);
		if (delta + width + gap >= period) {
			/* Drop too old checkpoint spans from cphist */
			min_incl_cno = target->start.cno;
			goto out_trim;
		}
		delta += width + gap;
		index--;
	}

	/*
	 * period > 0 && period > delta &&
	 * target->start.time + (period - delta) <= target->end.time
	 */
	if (target->end.cno > target->start.cno) {
		struct nilfs_cpinfo_find_context ctx;

		ctx.end_cno = target->end.cno;
		ctx.time = target->start.time + (period - delta);
		ctx.min_incl_cp.cno = target->end.cno;
		ctx.min_incl_cp.time = target->end.time;
		ctx.max_excl_cp.cno = target->start.cno;
		ctx.max_excl_cp.time = target->start.time;
		ctx.nskips = 0;

		ret = nilfs_enum_cpinfo_forward(cnormap->nilfs, cpstat,
						target->start.cno + 1, 1,
						nilfs_cpinfo_find, &ctx);
		if (ret < 0)
			goto out;

		if (ctx.max_excl_cp.cno > target->start.cno) {
			delta += ctx.max_excl_cp.time - target->start.time;
			target->start.cno = ctx.max_excl_cp.cno;
			target->start.time = ctx.max_excl_cp.time;
			target->approx_ncp -= min_t(unsigned int, ctx.nskips,
						    target->approx_ncp - 1);
		}
		min_incl_cno = ctx.min_incl_cp.cno;
	} else {
		min_incl_cno = target->end.cno;
	}

out_trim:
	/* Drop too old checkpoint spans from cphist */
	if (max_index > index)
		nilfs_vector_delete_elements(cnormap->cphist, index + 1,
					     max_index - index);
	cnormap->cphist_elapsed_time -= delta;
	*cnop = min_incl_cno;
	ret = 0;

out:
	return ret;
}


/**
 * nilfs_cnormap_track_back - get checkpoint number back for a period of time
 * @cnormap: nilfs_cnormap struct
 * @period: interval to track back
 * @cnop: buffer to store resultant checkpoint number
 *
 * If no valid checkpoint was found within the period, NILFS_CNO_MAX
 * is set in the buffer @cnop.
 */
int nilfs_cnormap_track_back(struct nilfs_cnormap *cnormap, __u64 period,
			     nilfs_cno_t *cnop)
{
	struct nilfs_cpstat cpstat;
	struct nilfs_cpspan *target, *latest;
	__s64 monotonic_clock;	/* The current value of the monotonic clock */
	__u64 call_interval;	/* Interval from the previous call */
	__u64 cphist_offset;	/* Period b/w now and the last cp on cphist */
	unsigned int max_index;
	int ret;

	if (period == 0) {
		*cnop = NILFS_CNO_MAX;
		return 0;
	}

	ret = nilfs_get_cpstat(cnormap->nilfs, &cpstat);
	if (ret < 0)
		return -1;

	ret = nilfs_cnormap_get_monotonic_clock(cnormap, &monotonic_clock);
	if (ret < 0)
		return -1;

	if (nilfs_vector_get_size(cnormap->cphist) == 0) {
		ret = nilfs_cnormap_cphist_init(cnormap, &cpstat,
						monotonic_clock, period, cnop);
		goto out;
	}

	latest = nilfs_vector_get_element(cnormap->cphist, 0);
	BUG_ON(!latest);  /* always succeeds */

	call_interval = monotonic_clock - cnormap->base_clock;
	cphist_offset = call_interval +
		(cnormap->base_time > latest->end.time ?
		 cnormap->base_time - latest->end.time : 0);
	if (period < cphist_offset / 2) {
		nilfs_vector_clear(cnormap->cphist);
		cnormap->cphist_elapsed_time = 0;

		ret = nilfs_cnormap_cphist_init(cnormap, &cpstat,
						monotonic_clock, period, cnop);
		goto out;
	}

	if (period < cphist_offset) {
		ret = nilfs_cnormap_cphist_extend_forward(
			cnormap, &cpstat, monotonic_clock,
			cphist_offset - period, latest->end.cno, cnop);
		goto out;
	}

	if (period < cphist_offset + cnormap->cphist_elapsed_time) {
		__u64 t;

		t = cphist_offset + cnormap->cphist_elapsed_time - period;
		ret = nilfs_cnormap_cphist_search(cnormap, &cpstat, t, cnop);
		goto out;
	}

	max_index = nilfs_vector_get_size(cnormap->cphist) - 1;
	target = nilfs_vector_get_element(cnormap->cphist, max_index);
	BUG_ON(!target);

	if (period > cphist_offset + cnormap->cphist_elapsed_time) {
		ret = nilfs_cnormap_cphist_extend_backward(
			cnormap, &cpstat, period - cphist_offset, max_index,
			target->start.cno, cnop);
		goto out;
	}

	/* period == cphist_offset + cnormap->cphist_elapsed_time */
	*cnop = target->start.cno;
	ret = 0;

out:
	return ret;
}
