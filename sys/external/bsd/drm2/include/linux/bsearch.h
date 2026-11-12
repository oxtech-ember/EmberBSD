/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD native implementation of the interface of Linux 6.18
 * include/linux/bsearch.h (GPL-2.0; no source copied).  libkern has no
 * bsearch(), so it is a static inline here.  Returns a pointer to a
 * matching element of the num-element array base, or NULL.
 */

#ifndef _LINUX_BSEARCH_H_
#define _LINUX_BSEARCH_H_

#include <sys/types.h>

static inline void *
bsearch(const void *key, const void *base, size_t num, size_t size,
    int (*cmp)(const void *, const void *))
{
	const char *lo = base;

	while (num != 0) {
		const char *mid = lo + (num / 2) * size;
		int c = (*cmp)(key, mid);

		if (c == 0)
			return __UNCONST(mid);
		if (c > 0) {
			lo = mid + size;
			num--;
		}
		num /= 2;
	}
	return NULL;
}

#endif	/* _LINUX_BSEARCH_H_ */
