/*	$NetBSD: seq_file.h,v 1.5 2021/12/19 12:30:05 riastradh Exp $	*/

/*-
 * Copyright (c) 2013 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Taylor R. Campbell.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _LINUX_SEQ_FILE_H_
#define _LINUX_SEQ_FILE_H_

#include <linux/capability.h>
#include <linux/string.h>
#include <linux/stdarg.h>
#include <linux/types.h>

/*
 * A seq_file here is a fixed buffer (EmberBSD): the
 * 6.18 core prints into one (drm_printer, dp helper dumps) and the caller
 * decides what to do with the text; there is no debugfs.  Output that does
 * not fit is dropped and marked (seq_has_overflowed), where Linux retries
 * with a larger buffer.  Set up with seq_file_init().
 */
struct seq_file {
	char		*buf;
	size_t		size;
	size_t		count;
	void		*private;
};

static inline void
seq_file_init(struct seq_file *m, char *buf, size_t size)
{

	m->buf = buf;
	m->size = size;
	m->count = 0;
	m->private = NULL;
}

static inline bool
seq_has_overflowed(struct seq_file *m)
{

	return m->count == m->size;
}

static inline int
seq_write(struct seq_file *m, const void *data, size_t len)
{

	if (m->size - m->count < len) {
		m->count = m->size;	/* overflowed */
		return -1;
	}
	memcpy(m->buf + m->count, data, len);
	m->count += len;
	return 0;
}

static inline void
seq_putc(struct seq_file *m, char c)
{

	(void)seq_write(m, &c, 1);
}

static inline void
seq_puts(struct seq_file *m, const char *s)
{

	(void)seq_write(m, s, strlen(s));
}

static inline void
seq_vprintf(struct seq_file *m, const char *fmt, va_list ap)
{
	int len;

	if (m->count >= m->size)
		return;
	len = vsnprintf(m->buf + m->count, m->size - m->count, fmt, ap);
	if (len < 0 || (size_t)len >= m->size - m->count)
		m->count = m->size;
	else
		m->count += len;
}

static inline void __printflike(2, 3)
seq_printf(struct seq_file *m, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	seq_vprintf(m, fmt, ap);
	va_end(ap);
}

#endif  /* _LINUX_SEQ_FILE_H_ */
