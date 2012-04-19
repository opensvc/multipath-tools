#ifndef _UTIL_H
#define _UTIL_H

void strchop(char *);
int basenamecpy (const char * src, char * dst, int);
int filepresent (char * run);
int get_word (char * sentence, char ** word);
size_t strlcpy(char *dst, const char *src, size_t size);
size_t strlcat(char *dst, const char *src, size_t size);
void remove_trailing_chars(char *path, char c);
int devt2devname (char *, int, char *);

#define safe_sprintf(var, format, args...)	\
	snprintf(var, sizeof(var), format, ##args) >= sizeof(var)
#define safe_snprintf(var, size, format, args...)      \
	snprintf(var, size, format, ##args) >= size

#endif /* _UTIL_H */
