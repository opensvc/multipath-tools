#ifndef BYTEORDER_H_INCLUDED
#define BYTEORDER_H_INCLUDED

#ifdef __linux__
#  include <endian.h>
#  include <byteswap.h>
#else
#  error unsupported
#endif

#if BYTE_ORDER == LITTLE_ENDIAN
#  define le16_to_cpu(x) (x)
#  define be16_to_cpu(x) bswap_16(x)
#  define le32_to_cpu(x) (x)
#  define le64_to_cpu(x) (x)
#  define be32_to_cpu(x) bswap_32(x)
#  define be64_to_cpu(x) bswap_64(x)
#elif BYTE_ORDER == BIG_ENDIAN
#  define le16_to_cpu(x) bswap_16(x)
#  define be16_to_cpu(x) (x)
#  define le32_to_cpu(x) bswap_32(x)
#  define le64_to_cpu(x) bswap_64(x)
#  define be32_to_cpu(x) (x)
#  define be64_to_cpu(x) (x)
#else
#  error unsupported
#endif

#define cpu_to_le16(x) le16_to_cpu(x)
#define cpu_to_be16(x) be16_to_cpu(x)
#define cpu_to_le32(x) le32_to_cpu(x)
#define cpu_to_be32(x) be32_to_cpu(x)
#define cpu_to_le64(x) le64_to_cpu(x)
#define cpu_to_be64(x) be64_to_cpu(x)

struct be64 {
	uint64_t _v;
};

#define get_be64(x) be64_to_cpu((x)._v)
#define put_be64(x, y) do { (x)._v = cpu_to_be64(y); } while (0)


#endif				/* BYTEORDER_H_INCLUDED */
