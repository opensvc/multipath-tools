#ifndef _UTIL_H
#define _UTIL_H

int strcmp_chomp(char *, char *);
void strchop(char *);
void basenamecpy (char * src, char * dst);
int filepresent (char * run);
int get_word (char * sentence, char ** word);
size_t strlcpy(char *dst, const char *src, size_t size);
size_t strlcat(char *dst, const char *src, size_t size);
void remove_trailing_chars(char *path, char c);

#define safe_sprintf(var, format, args...)	\
	snprintf(var, sizeof(var), format, ##args) >= sizeof(var)
#define safe_snprintf(var, size, format, args...)      \
	snprintf(var, size, format, ##args) >= size

#endif /* _UTIL_H */
