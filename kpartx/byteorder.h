#ifndef BYTEORDER_H_INCLUDED
#define BYTEORDER_H_INCLUDED

#ifdef __linux__
#  include <endian.h>
#  include <byteswap.h>
#else
#  error unsupported
#endif

#if BYTE_ORDER == LITTLE_ENDIAN
#  define le32_to_cpu(x) (x)
#elif BYTE_ORDER == BIG_ENDIAN
#  define le32_to_cpu(x) bswap_32(x)
#else
#  error unsupported
#endif

#endif				/* BYTEORDER_H_INCLUDED */
