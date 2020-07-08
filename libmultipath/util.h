#ifndef _UTIL_H
#define _UTIL_H

#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/types.h>
/* for rlim_t */
#include <sys/resource.h>
#include <inttypes.h>
#include <stdbool.h>

size_t strchop(char *);
int basenamecpy (const char *src, char *dst, size_t size);
int filepresent (const char *run);
char *get_next_string(char **temp, const char *split_char);
int get_word (const char * sentence, char ** word);
size_t strlcpy(char *dst, const char *src, size_t size);
size_t strlcat(char *dst, const char *src, size_t size);
int devt2devname (char *, int, const char *);
dev_t parse_devt(const char *dev_t);
char *convert_dev(char *dev, int is_path_device);
void setup_thread_attr(pthread_attr_t *attr, size_t stacksize, int detached);
int systemd_service_enabled(const char *dev);
int get_linux_version_code(void);
int parse_prkey(const char *ptr, uint64_t *prkey);
int parse_prkey_flags(const char *ptr, uint64_t *prkey, uint8_t *flags);
int safe_write(int fd, const void *buf, size_t count);
void set_max_fds(rlim_t max_fds);

#define KERNEL_VERSION(maj, min, ptc) ((((maj) * 256) + (min)) * 256 + (ptc))
#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))

#define safe_sprintf(var, format, args...)	\
	safe_snprintf(var, sizeof(var), format, ##args)

#define safe_snprintf(var, size, format, args...)      \
	({								\
		size_t __size = size;					\
		int __ret;						\
									\
		__ret = snprintf(var, __size, format, ##args);		\
		__ret < 0 || (size_t)__ret >= __size;			\
	})

#define pthread_cleanup_push_cast(f, arg)		\
	pthread_cleanup_push(((void (*)(void *))&f), (arg))

void close_fd(void *arg);

struct scandir_result {
	struct dirent **di;
	int n;
};
void free_scandir_result(struct scandir_result *);

/*
 * ffsll() is also available on glibc < 2.27 if _GNU_SOURCE is defined.
 * But relying on that would require that every program using this header file
 * set _GNU_SOURCE during compilation, because otherwise the library and the
 * program would use different types for bitfield_t, causing errors.
 * That's too error prone, so if in doubt, use ffs().
 */
#if __GLIBC_PREREQ(2, 27)
typedef unsigned long long int bitfield_t;
#define _ffs(x) ffsll(x)
#else
typedef unsigned int bitfield_t;
#define _ffs(x) ffs(x)
#endif
#define bits_per_slot (sizeof(bitfield_t) * CHAR_BIT)

struct bitfield {
	unsigned int len;
	bitfield_t bits[];
};

struct bitfield *alloc_bitfield(unsigned int maxbit);

void _log_bitfield_overflow(const char *f, unsigned int bit, unsigned int len);
#define log_bitfield_overflow(bit, len) \
	_log_bitfield_overflow(__func__, bit, len)

static inline bool is_bit_set_in_bitfield(unsigned int bit,
				       const struct bitfield *bf)
{
	if (bit >= bf->len) {
		log_bitfield_overflow(bit, bf->len);
		return false;
	}
	return !!(bf->bits[bit / bits_per_slot] &
		  (1ULL << (bit % bits_per_slot)));
}

static inline void set_bit_in_bitfield(unsigned int bit, struct bitfield *bf)
{
	if (bit >= bf->len) {
		log_bitfield_overflow(bit, bf->len);
		return;
	}
	bf->bits[bit / bits_per_slot] |= (1ULL << (bit % bits_per_slot));
}

static inline void clear_bit_in_bitfield(unsigned int bit, struct bitfield *bf)
{
	if (bit >= bf->len) {
		log_bitfield_overflow(bit, bf->len);
		return;
	}
	bf->bits[bit / bits_per_slot] &= ~(1ULL << (bit % bits_per_slot));
}

#endif /* _UTIL_H */
