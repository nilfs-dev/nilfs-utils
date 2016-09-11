/*
 * compat.h - definitions for compatibility
 */
#ifndef __COMPAT_H__
#define __COMPAT_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif	/* HAVE_CONFIG_H */

#if HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif	/* HAVE_SYS_IOCTL_H */

#if HAVE_TIME_H
#include <time.h>
#endif	/* HAVE_TIME_H */

#include <endian.h>
#include <byteswap.h>

/* Old linux/magic.h may not have the file system magic number of NILFS */
#ifndef NILFS_SUPER_MAGIC
#define NILFS_SUPER_MAGIC	0x3434	/* NILFS filesystem magic number */
#endif

/* Sizes of integral types */
#ifndef U64_MAX
#define U64_MAX		18446744073709551615LL	/* __u64 (or u64) max */
#endif

#ifndef S64_MAX
#define S64_MAX		9223372036854775807LL	/* __s64 (or s64) max */
#endif

/* Ioctls for freeze and thaw */
#ifndef FIFREEZE
#define FIFREEZE	_IOWR('X', 119, int)
#define FITHAW		_IOWR('X', 120, int)
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

/* Byte order */
#if __BYTE_ORDER == __LITTLE_ENDIAN
#define le16_to_cpu(x)	((__u16)(x))
#define le32_to_cpu(x)	((__u32)(x))
#define le64_to_cpu(x)	((__u64)(x))
#define cpu_to_le16(x)	((__u16)(x))
#define cpu_to_le32(x)	((__u32)(x))
#define cpu_to_le64(x)	((__u64)(x))
#define be16_to_cpu(x)	bswap_16(x)
#define be32_to_cpu(x)	bswap_32(x)
#define be64_to_cpu(x)	bswap_64(x)
#define cpu_to_be16(x)	bswap_16(x)
#define cpu_to_be32(x)	bswap_32(x)
#define cpu_to_be64(x)	bswap_64(x)
#elif __BYTE_ORDER == __BIG_ENDIAN
#define le16_to_cpu(x)	bswap_16(x)
#define le32_to_cpu(x)	bswap_32(x)
#define le64_to_cpu(x)	bswap_64(x)
#define cpu_to_le16(x)	bswap_16(x)
#define cpu_to_le32(x)	bswap_32(x)
#define cpu_to_le64(x)	bswap_64(x)
#define be16_to_cpu(x)	((__u16)(x))
#define be32_to_cpu(x)	((__u32)(x))
#define be64_to_cpu(x)	((__u64)(x))
#define cpu_to_be16(x)	((__u16)(x))
#define cpu_to_be32(x)	((__u32)(x))
#define cpu_to_be64(x)	((__u64)(x))
#else
#error "unknown endian"
#endif	/* __BYTE_ORDER */


#endif /* __COMPAT_H__ */
