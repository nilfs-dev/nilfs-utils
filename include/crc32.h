/*
 * crc32.h - crc32 calculation
 */

#ifndef __CRC32_H__
#define __CRC32_H__

#include <stddef.h>	/* size_t */
#include <stdint.h>	/* uint32_t */

extern uint32_t crc32_le(uint32_t seed, unsigned char const *data,
			 size_t length);

#endif /* __CRC32_H__ */
