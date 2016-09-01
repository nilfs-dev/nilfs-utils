/*
 * compat.h - definitions for compatibility
 */
#ifndef __COMPAT_H__
#define __COMPAT_H__

#if HAVE_TIME_H
#include <time.h>
#endif	/* HAVE_TIME_H */

/* Sizes of integral types */
#ifndef U64_MAX
#define U64_MAX		18446744073709551615LL	/* __u64 (or u64) max */
#endif

#ifndef S64_MAX
#define S64_MAX		9223372036854775807LL	/* __s64 (or s64) max */
#endif

/* Linux specific system clocks */
#ifndef CLOCK_REALTIME_COARSE
#define CLOCK_REALTIME_COARSE	5  /* Supported on Linux 2.6.32 or later */
#endif

#ifndef CLOCK_MONOTONIC_COARSE
#define CLOCK_MONOTONIC_COARSE	6  /* Supported on Linux 2.6.32 or later */
#endif

#ifndef CLOCK_BOOTTIME
#define CLOCK_BOOTTIME		7  /* Supported on Linux 2.6.39 or later */
#endif

/* Operations on timespecs */
#ifndef timespecadd
#define timespecadd(a, b, res)						\
	do {								\
		(res)->tv_sec = (a)->tv_sec + (b)->tv_sec;		\
		(res)->tv_nsec = (a)->tv_nsec + (b)->tv_nsec;		\
		if ((res)->tv_nsec >= 1000000000L) {			\
			(res)->tv_sec++;				\
			(res)->tv_nsec -= 1000000000L;			\
		}							\
	} while (0)
#endif

#ifndef timespecsub
#define timespecsub(a, b, res)						\
	do {								\
		(res)->tv_sec = (a)->tv_sec - (b)->tv_sec;		\
		(res)->tv_nsec = (a)->tv_nsec - (b)->tv_nsec;		\
		if ((res)->tv_nsec < 0) {				\
			(res)->tv_sec--;				\
			(res)->tv_nsec += 1000000000L;			\
		}							\
	} while (0)
#endif

#ifndef timespecclear
#define timespecclear(ts)						\
	do { (ts)->tv_sec = 0; (ts)->tv_nsec = 0; } while (0)
#endif

#ifndef timespecisset
#define timespecisset(ts)	((ts)->tv_sec || (ts)->tv_nsec)
#endif

#ifndef timespeccmp
#define timespeccmp(a, b, cmp)						\
	(((a)->tv_sec == (b)->tv_sec) ?					\
	 ((a)->tv_nsec cmp (b)->tv_nsec) :				\
	 ((a)->tv_sec cmp (b)->tv_sec))
#endif

#endif /* __COMPAT_H__ */
