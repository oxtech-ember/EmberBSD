/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD native. Interface of Linux 6.18 include/linux/seq_buf.h (GPL-2.0; no source
 * copied): a bounded text buffer.  seq_buf_printf() needs room for the
 * terminating NUL; the raw writers (putmem, puts, putc) may fill the buffer
 * to its size, and seq_buf_str() then terminates at size - 1.  A write that
 * does not fit returns -1 and leaves the buffer overflowed (len > size) until
 * seq_buf_clear().
 */

#ifndef _LINUX_SEQ_BUF_H_
#define _LINUX_SEQ_BUF_H_

#include <linux/stdarg.h>
#include <linux/types.h>

#ifdef _KERNEL
#include <sys/systm.h>
#endif

struct seq_buf {
	char	*buffer;
	size_t	size;
	size_t	len;
};

/* A seq_buf with its own storage on the stack. */
#define DECLARE_SEQ_BUF(NAME, SIZE)					\
	char __##NAME##_buffer[SIZE] = "";				\
	struct seq_buf NAME = {						\
		.buffer = __##NAME##_buffer,				\
		.size = SIZE,						\
	}

static inline void
seq_buf_clear(struct seq_buf *s)
{

	s->len = 0;
}

static inline void
seq_buf_init(struct seq_buf *s, char *buf, unsigned int size)
{

	s->buffer = buf;
	s->size = size;
	seq_buf_clear(s);
}

static inline bool
seq_buf_has_overflowed(struct seq_buf *s)
{

	return s->len > s->size;
}

static inline void
seq_buf_set_overflow(struct seq_buf *s)
{

	s->len = s->size + 1;
}

static inline size_t
seq_buf_used(struct seq_buf *s)
{

	return s->len < s->size ? s->len : s->size;
}

static inline unsigned int
seq_buf_buffer_left(struct seq_buf *s)
{

	if (seq_buf_has_overflowed(s))
		return 0;
	return s->size - s->len;
}

/* The text as a C string: terminated at the used length, or at size - 1. */
static inline const char *
seq_buf_str(struct seq_buf *s)
{

	if (s->size == 0)
		return "";
	if (s->len < s->size)
		s->buffer[s->len] = '\0';
	else
		s->buffer[s->size - 1] = '\0';
	return s->buffer;
}

/* Free space for direct writes (then seq_buf_commit()); 0 if overflowed. */
static inline unsigned int
seq_buf_get_buf(struct seq_buf *s, char **bufp)
{

	if (s->len < s->size) {
		*bufp = s->buffer + s->len;
		return s->size - s->len;
	}
	*bufp = NULL;
	return 0;
}

static inline void
seq_buf_commit(struct seq_buf *s, int num)
{

	if (num < 0)
		seq_buf_set_overflow(s);
	else
		s->len += num;
}

static inline int
seq_buf_vprintf(struct seq_buf *s, const char *fmt, va_list args)
{
	int len;

	if (s->len < s->size) {
		len = vsnprintf(s->buffer + s->len, s->size - s->len, fmt,
		    args);
		if (len >= 0 && s->len + len < s->size) {
			s->len += len;
			return 0;
		}
	}
	seq_buf_set_overflow(s);
	return -1;
}

static inline int
seq_buf_printf(struct seq_buf *s, const char *fmt, ...)
	__attribute__((__format__(__printf__, 2, 3)));

static inline int
seq_buf_printf(struct seq_buf *s, const char *fmt, ...)
{
	va_list args;
	int ret;

	va_start(args, fmt);
	ret = seq_buf_vprintf(s, fmt, args);
	va_end(args);
	return ret;
}

static inline int
seq_buf_putmem(struct seq_buf *s, const void *mem, unsigned int len)
{

	if (s->len <= s->size && len <= s->size - s->len) {
		memcpy(s->buffer + s->len, mem, len);
		s->len += len;
		return 0;
	}
	seq_buf_set_overflow(s);
	return -1;
}

static inline int
seq_buf_puts(struct seq_buf *s, const char *str)
{

	return seq_buf_putmem(s, str, strlen(str));
}

static inline int
seq_buf_putc(struct seq_buf *s, unsigned char c)
{

	return seq_buf_putmem(s, &c, 1);
}

#endif	/* _LINUX_SEQ_BUF_H_ */
