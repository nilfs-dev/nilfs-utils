/*
 * sbin/mount/libmount_compat.h - libmount compatibility definitions
 */

#ifndef _LIBMOUNT_COMPAT_H
#define _LIBMOUNT_COMPAT_H

#include <libmount.h>

/*
 * If MNT_EX_* macros are not defined in libmount.h, define them here.
 */
#ifndef MNT_EX_SUCCESS
#define MNT_EX_SUCCESS	0
#endif

#ifndef MNT_EX_USAGE
#define MNT_EX_USAGE	1
#endif

#ifndef MNT_EX_SYSERR
#define MNT_EX_SYSERR	2
#endif

#ifndef MNT_EX_SOFTWARE
#define MNT_EX_SOFTWARE	4
#endif

#ifndef MNT_EX_USER
#define MNT_EX_USER	8
#endif

#ifndef MNT_EX_FILEIO
#define MNT_EX_FILEIO	16
#endif

#ifndef MNT_EX_FAIL
#define MNT_EX_FAIL	32
#endif

#ifndef MNT_EX_SOMEOK
#define MNT_EX_SOMEOK	64
#endif

#endif /* _LIBMOUNT_COMPAT_H */
