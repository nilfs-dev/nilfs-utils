/*
 * kern_compat.h - kernel compat declarations
 *
 * Copyright (C) 2005-2011 Nippon Telegraph and Telephone Corporation.
 *
 * This program can be redistributed under the terms of the GNU Lesser
 * General Public License.
 */

#ifndef NILFS_KERN_COMPAT_H
#define NILFS_KERN_COMPAT_H

#include <linux/types.h>
#include <endian.h>
#include <byteswap.h>

#ifndef __bitwise  /* Tricky workaround; should be replaced */
typedef __u64 __le64;
typedef __u32 __le32;
typedef __u16 __le16;
#endif

#ifndef le32_to_cpu
#if __BYTE_ORDER == __LITTLE_ENDIAN
#define cpu_to_le16(x) (x)
#define cpu_to_le32(x) (x)
#define cpu_to_le64(x) (x)
#define le16_to_cpu(x) (x)
#define le32_to_cpu(x) (x)
#define le64_to_cpu(x) (x)
#elif __BYTE_ORDER == __BIG_ENDIAN
#define le16_to_cpu(x) bswap_16(x)
#define le32_to_cpu(x) bswap_32(x)
#define le64_to_cpu(x) bswap_64(x)
#define cpu_to_le16(x) bswap_16(x)
#define cpu_to_le32(x) bswap_32(x)
#define cpu_to_le64(x) bswap_64(x)
#else
#error "unsupported endian"
#endif /* __BYTE_ORDER */
#endif /* le32_to_cpu */

#ifndef BUG
#define BUG()	abort()
#endif

#endif	/* NILFS_KERN_COMPAT_H */
