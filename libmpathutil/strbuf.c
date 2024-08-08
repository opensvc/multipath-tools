/*
 * Copyright (c) 2021 SUSE LLC
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <inttypes.h>
#include <stdint.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#include "strbuf.h"

static const char empty_str[] = "";

char *__get_strbuf_buf(struct strbuf *buf)
{
	return buf->buf;
}

const char *get_strbuf_str(const struct strbuf *buf)
{
	return buf->buf ? buf->buf : empty_str;
}

char *steal_strbuf_str(struct strbuf *buf)
{
	char *p = buf->buf;

	buf->buf = NULL;
	buf->size = buf->offs = 0;
	return p;
}

size_t get_strbuf_len(const struct strbuf *buf)
{
	return buf->offs;
}

static bool strbuf_is_sane(const struct strbuf *buf)
{
	return buf && ((!buf->buf && !buf->size && !buf->offs) ||
		       (buf->buf && buf->size && buf->size > buf->offs));
}

void reset_strbuf(struct strbuf *buf)
{
	free(buf->buf);
	buf->buf = NULL;
	buf->size = buf->offs = 0;
}

void free_strbuf(struct strbuf *buf)
{
	if (!buf)
		return;
	reset_strbuf(buf);
	free(buf);
}

struct strbuf *new_strbuf(void)
{
	return calloc(1, sizeof(struct strbuf));
}

int truncate_strbuf(struct strbuf *buf, size_t offs)
{
	if (!buf->buf)
		return -EFAULT;
	if (offs > buf->offs)
		return -ERANGE;

	buf->offs = offs;
	buf->buf[offs] = '\0';
	return 0;
}

#define BUF_CHUNK 64

static int expand_strbuf(struct strbuf *buf, int addsz)
{
	size_t add;
	char *tmp;

	assert(strbuf_is_sane(buf));
	if (addsz < 0)
		return -EINVAL;
	if (buf->size - buf->offs >= (size_t)addsz + 1)
		return 0;

	add = ((addsz - (buf->size - buf->offs)) / BUF_CHUNK + 1)
		* BUF_CHUNK;

	if (buf->size >= SIZE_MAX - add) {
		add = SIZE_MAX - buf->size;
		if (add < (size_t)addsz + 1)
			return -EOVERFLOW;
	}

	tmp = realloc(buf->buf, buf->size + add);
	if (!tmp)
		return -ENOMEM;

	buf->buf = tmp;
	buf->size += add;
	buf->buf[buf->offs] = '\0';

	return 0;
}

int __append_strbuf_str(struct strbuf *buf, const char *str, int slen)
{
	int ret;

	if ((ret = expand_strbuf(buf, slen)) < 0)
		return ret;

	memcpy(buf->buf + buf->offs, str, slen);
	buf->offs += slen;
	buf->buf[buf->offs] = '\0';

	return slen;
}

int append_strbuf_str(struct strbuf *buf, const char *str)
{
	size_t slen;

	if (!str)
		return -EINVAL;

	slen = strlen(str);
	if (slen > INT_MAX)
		return -ERANGE;

	return __append_strbuf_str(buf, str, slen);
}

int fill_strbuf(struct strbuf *buf, char c, int slen)
{
	int ret;

	if ((ret = expand_strbuf(buf, slen)) < 0)
		return ret;

	memset(buf->buf + buf->offs, c, slen);
	buf->offs += slen;
	buf->buf[buf->offs] = '\0';

	return slen;
}

int append_strbuf_quoted(struct strbuf *buff, const char *ptr)
{
	char *quoted, *q;
	const char *p;
	unsigned n_quotes, i;
	size_t qlen;
	int ret;

	if (!ptr)
		return -EINVAL;

	for (n_quotes = 0, p = strchr(ptr, '"'); p; p = strchr(++p, '"'))
		n_quotes++;

	/* leading + trailing quote, 1 extra quote for every quote in ptr */
	qlen = strlen(ptr) + 2 + n_quotes;
	if (qlen > INT_MAX)
		return -ERANGE;
	if ((ret = expand_strbuf(buff, qlen)) < 0)
		return ret;

	quoted = &(buff->buf[buff->offs]);
	*quoted++ = '"';
	for (p = ptr, q = quoted, i = 0; i < n_quotes; i++) {
		char *q1 = memccpy(q, p, '"', qlen - 2 - (q - quoted));

		assert(q1 != NULL);
		p += q1 - q;
		*q1++ = '"';
		q = q1;
	}
	q = mempcpy(q, p, qlen - 2 - (q - quoted));
	*q++ = '"';
	*q = '\0';
	ret = q - &(buff->buf[buff->offs]);
	buff->offs += ret;
	return ret;
}

__attribute__((format(printf, 2, 3)))
int print_strbuf(struct strbuf *buf, const char *fmt, ...)
{
	va_list ap;
	int ret;
	size_t space = buf->size - buf->offs;

	va_start(ap, fmt);
	ret = vsnprintf(buf->buf + buf->offs, space, fmt, ap);
	va_end(ap);

	if (ret < 0)
		return ret;
	else if ((size_t)ret < space) {
		buf->offs += ret;
		return ret;
	}

	ret = expand_strbuf(buf, ret);
	if (ret < 0)
		return ret;

	space = buf->size - buf->offs;
	va_start(ap, fmt);
	ret = vsnprintf(buf->buf + buf->offs, space, fmt, ap);
	va_end(ap);

	if (ret >= 0)
		buf->offs += ret;

	return ret;
}
