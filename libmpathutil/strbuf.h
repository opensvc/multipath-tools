/*
 * Copyright (c) 2021 SUSE LLC
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef STRBUF_H_INCLUDED
#define STRBUF_H_INCLUDED
#include <errno.h>
#include <string.h>

struct strbuf {
	char *buf;
	size_t size;
	size_t offs;
};

/**
 * reset_strbuf(): prepare strbuf for new content
 * @param strbuf: string buffer to reset
 *
 * Frees internal buffer and resets size and offset to 0.
 * Can be used to cleanup a struct strbuf on stack.
 */
void reset_strbuf(struct strbuf *buf);

/**
 * free_strbuf(): free resources
 * @param strbuf: string buffer to discard
 *
 * Frees all memory occupied by a struct strbuf.
 */
void free_strbuf(struct strbuf *buf);

/**
 * macro: STRBUF_INIT
 *
 * Use this to initialize a local struct strbuf on the stack,
 * or in a global/static variable.
 */
#define STRBUF_INIT { .buf = NULL, }

/**
 * macro: STRBUF_ON_STACK
 *
 * Define and initialize a local struct @strbuf to be cleaned up when
 * the current scope is left
 */
#define STRBUF_ON_STACK(__x)						\
	struct strbuf __attribute__((cleanup(reset_strbuf))) (__x) = STRBUF_INIT;

/**
 * new_strbuf(): allocate a struct strbuf on the heap
 *
 * @returns: pointer to allocated struct, or NULL in case of error.
 */
struct strbuf *new_strbuf(void);

/**
 * get_strbuf_buf(): retrieve a pointer to the strbuf's buffer
 * @param buf: a struct strbuf
 * @returns: pointer to the string written to the strbuf so far.
 *
 * INTERNAL ONLY.
 * DANGEROUS: Unlike the return value of get_strbuf_str(),
 * this string can be written to, modifying the strbuf's content.
 * USE WITH CAUTION.
 * If @strbuf was never written to, the function returns NULL.
 * The return value of this function must not be free()d.
 */
char *__get_strbuf_buf(struct strbuf *buf);

/**
 * get_strbuf_str(): retrieve string from strbuf
 * @param buf: a struct strbuf
 * @returns: pointer to the string written to the strbuf so far.
 *
 * If @strbuf was never written to, the function returns a zero-
 * length string. The return value of this function must not be
 * free()d.
 */
const char *get_strbuf_str(const struct strbuf *buf);

/**
 * steal_strbuf_str(): retrieve string from strbuf and reset
 * @param buf: a struct strbuf
 * @returns: pointer to the string written to @strbuf, or NULL
 *
 * After calling this function, the @strbuf is empty as if freshly
 * initialized. The caller is responsible to free() the returned pointer.
 * If @strbuf was never written to (not even an empty string was appended),
 * the function returns NULL.
 */
char *steal_strbuf_str(struct strbuf *buf);

/**
 * get_strbuf_len(): retrieve string length from strbuf
 * @param buf: a struct strbuf
 * @returns: the length of the string written to @strbuf so far.
 */
size_t get_strbuf_len(const struct strbuf *buf);

/**
 * truncate_strbuf(): shorten the buffer
 * @param buf: struct strbuf to truncate
 * @param offs: new buffer position / offset
 * @returns: 0 on success, negative error code otherwise.
 *
 * If @strbuf is freshly allocated/reset (never written to), -EFAULT
 * is returned. if @offs must be higher than the current offset as returned
 * by get_strbuf_len(), -ERANGE is returned. The allocated size of the @strbuf
 * remains unchanged.
 */
int truncate_strbuf(struct strbuf *buf, size_t offs);

/**
 * __append_strbuf_str(): append string of known length
 * @param buf: the struct strbuf to write to
 * @param str: the string to append, not necessarily 0-terminated
 * @param slen: max number of characters to append, must be non-negative
 * @returns: @slen = number of appended characters if successful (excluding
 * terminating '\0'); negative error code otherwise.
 *
 * Notes: a 0-byte is always appended to the output buffer after @slen characters.
 * 0-bytes possibly contained in the first @slen characters are copied into
 * the output. If the function returns an error, @strbuf is unchanged.
 */
int __append_strbuf_str(struct strbuf *buf, const char *str, int slen);

/**
 * append_strbuf_str(): append string
 * @param buf: the struct strbuf to write to
 * @param str: the string to append, 0-terminated
 * @returns: number of appended characters if successful (excluding
 * terminating '\0'); negative error code otherwise
 *
 * Appends the given 0-terminated string to @strbuf, expanding @strbuf's size
 * as necessary. If the function returns an error, @strbuf is unchanged.
 */
int append_strbuf_str(struct strbuf *buf, const char *str);

/**
 * fill_strbuf_str(): pad strbuf with a character
 * @param buf: the struct strbuf to write to
 * @param c: the character used for filling
 * @param slen: max number of characters to append, must be non-negative
 * @returns: number of appended characters if successful (excluding
 * terminating '\0'); negative error code otherwise
 *
 * Appends the given character @slen times to @strbuf, expanding @strbuf's size
 * as necessary. If the function returns an error, @strbuf is unchanged.
 */
int fill_strbuf(struct strbuf *buf, char c, int slen);

/**
 * append_strbuf_quoted(): append string in double quotes, escaping quotes in string
 * @param buf: the struct strbuf to write to
 * @param str: the string to append, 0-terminated
 * @returns: number of appended characters if successful (excluding
 * terminating '\0'); negative error code otherwise
 *
 * Appends the given string to @strbuf, with leading and trailing double
 * quotes (") added, expanding @strbuf's size as necessary. Any double quote
 * characters (") in the string are transformed to a pair of double quotes ("").
 * If the function returns an error, @strbuf is unchanged.
 */
int append_strbuf_quoted(struct strbuf *buf, const char *str);

/**
 * print_strbuf(): print to strbuf, formatted
 * @param buf: the struct strbuf to print to
 * @param fmt: printf()-like format string
 * @returns: number of appended characters if successful, (excluding
 * terminating '\0'); negative error code otherwise
 *
 * Appends the arguments following @fmt, formatted as in printf(), to
 * @strbuf, expanding @strbuf's size as necessary. The function makes sure that
 * the output @strbuf is always 0-terminated.
 * If the function returns an error, @strbuf is unchanged.
 */
__attribute__((format(printf, 2, 3)))
int print_strbuf(struct strbuf *buf, const char *fmt, ...);

#endif
