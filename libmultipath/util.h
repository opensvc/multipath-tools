#ifndef _UTIL_H
#define _UTIL_H

#include <sys/types.h>
#include <inttypes.h>

size_t strchop(char *);
int basenamecpy (const char *src, char *dst, size_t size);
int filepresent (char * run);
char *get_next_string(char **temp, char *split_char);
int get_word (char * sentence, char ** word);
size_t strlcpy(char *dst, const char *src, size_t size);
size_t strlcat(char *dst, const char *src, size_t size);
int devt2devname (char *, int, char *);
dev_t parse_devt(const char *dev_t);
char *convert_dev(char *dev, int is_path_device);
char *parse_uid_attribute_by_attrs(char *uid_attrs, char *path_dev);
void setup_thread_attr(pthread_attr_t *attr, size_t stacksize, int detached);
int systemd_service_enabled(const char *dev);
int get_linux_version_code(void);
int parse_prkey(char *ptr, uint64_t *prkey);
int safe_write(int fd, const void *buf, size_t count);

#define KERNEL_VERSION(maj, min, ptc) ((((maj) * 256) + (min)) * 256 + (ptc))

#define safe_sprintf(var, format, args...)	\
	snprintf(var, sizeof(var), format, ##args) >= sizeof(var)
#define safe_snprintf(var, size, format, args...)      \
	snprintf(var, size, format, ##args) >= size

#endif /* _UTIL_H */
