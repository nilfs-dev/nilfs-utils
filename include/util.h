/*
 * util.h - utility definitions
 */
#ifndef __UTIL_H__
#define __UTIL_H__

#include <assert.h>
#include <stdint.h>	/* uint64_t, int64_t */
#include <stddef.h>	/* offsetof() */
#include <stdlib.h>
#include "compat.h"

/* likely() and unlikely() macros */
#define likely(x)	__builtin_expect(!!(x), 1)
#define unlikely(x)	__builtin_expect(!!(x), 0)

#ifndef BUG
#define BUG()	abort()
#endif

#define BUG_ON(x)	   assert(!(x))
/* Force a compilation error if the condition is true */
#define BUILD_BUG_ON(condition) ((void)sizeof(struct { int: -!!(condition); }))

/* Macro to emit a git revision string into binaries */
#if defined(NILFS_UTILS_GIT_REVISION) && defined(__GNUC__)
#define NILFS_UTILS_GITID() 						     \
	__asm__(							     \
	    ".section .comment\n\t"					     \
	    ".string \"nilfs-utils git: " NILFS_UTILS_GIT_REVISION "\"\n\t"  \
	    ".previous")
#else
#define NILFS_UTILS_GITID()	/* muted */
#endif

#define typecheck(type, x)					\
	({							\
	   type __dummy;					\
	   typeof(x) __dummy2;					\
	   (void)(&__dummy == &__dummy2);			\
	   1;							\
	})

#define IS_ALIGNED(x, a)	(((x) & ((typeof(x))(a) - 1)) == 0)

#define DIV_ROUND_UP(n, m)	(((n) + (m) - 1) / (m))
#define ARRAY_SIZE(arr)		(sizeof(arr) / sizeof((arr)[0]))

#define roundup(x, y)						\
	({							\
	   const typeof(y) __y = (y);				\
	   (((x) + __y - 1) / __y) * __y;			\
	})

/* offsetofend */
#define offsetofend(TYPE, MEMBER) \
	(offsetof(TYPE, MEMBER) + sizeof(((TYPE *)0)->MEMBER))

#define min_t(type, x, y) \
	({ type __x = (x); type __y = (y); __x < __y ? __x : __y; })
#define max_t(type, x, y) \
	({ type __x = (x); type __y = (y); __x > __y ? __x : __y; })


#define cnt64_gt(a, b)						\
	(typecheck(uint64_t, a) && typecheck(uint64_t, b) &&	\
	 ((int64_t)(b) - (int64_t)(a) < 0))
#define cnt64_ge(a, b)						\
	(typecheck(uint64_t, a) && typecheck(uint64_t, b) &&	\
	 ((int64_t)(a) - (int64_t)(b) >= 0))
#define cnt64_lt(a, b)		cnt64_gt(b, a)
#define cnt64_le(a, b)		cnt64_ge(b, a)


#define timeval_to_timespec(tv, ts)		\
do {						\
	(ts)->tv_sec = (tv)->tv_sec;		\
	(ts)->tv_nsec = (tv)->tv_usec * 1000;	\
} while (0)

#endif /* __UTIL_H__ */
