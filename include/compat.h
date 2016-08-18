/*
 * compat.h - definitions for compatibility
 */
#ifndef __COMPAT_H__
#define __COMPAT_H__

#if HAVE_TIME_H
#include <time.h>
#endif	/* HAVE_TIME_H */

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

#endif /* __COMPAT_H__ */
