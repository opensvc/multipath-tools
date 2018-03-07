#ifndef _UNALIGNED_H_
#define _UNALIGNED_H_

#include <stdint.h>

static inline uint16_t get_unaligned_be16(const void *ptr)
{
	const uint8_t *p = ptr;

	return p[0] << 8 | p[1];
}

static inline uint32_t get_unaligned_be32(void *ptr)
{
	const uint8_t *p = ptr;

	return p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
}

static inline void put_unaligned_be16(uint16_t val, void *ptr)
{
	uint8_t *p = ptr;

	p[0] = val >> 8;
	p[1] = val;
}

static inline void put_unaligned_be32(uint32_t val, void *ptr)
{
	uint8_t *p = ptr;

	p[0] = val >> 24;
	p[1] = val >> 16;
	p[2] = val >> 8;
	p[3] = val;
}

#endif /* _UNALIGNED_H_ */
