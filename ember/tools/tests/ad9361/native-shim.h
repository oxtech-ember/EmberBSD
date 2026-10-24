/*
 * Copyright (c) 2026 EmberBSD contributors.
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/* Origin: EmberBSD portable test kernel shims; no hardware implementation. */

#ifndef AD9361_TRACE_SHIM_H
#define AD9361_TRACE_SHIM_H
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <inttypes.h>
typedef void *device_t;
#define KM_SLEEP 0
#define KASSERT assert
#ifndef __arraycount
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#endif
#define howmany(x, y) (((x) + (y) - 1) / (y))
static inline void *kmem_alloc(size_t n, int flags)
{
	void *ptr;

	/* KM_SLEEP does not return NULL. Host OOM must stop this harness. */
	assert(flags == KM_SLEEP);
	ptr = malloc(n);
	assert(ptr != NULL);
	return ptr;
}
static inline void kmem_free(void *p, size_t n)
{
	(void)n;
	free(p);
}
static inline void delay(unsigned n)
{
	(void)n;
}
#ifndef __NetBSD__
static inline uint32_t be32dec(const void *p)
{
	const uint8_t *b = p;
	return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
	       ((uint32_t)b[2] << 8) | b[3];
}
static inline uint64_t be64dec(const void *p)
{
	const uint8_t *b = p;
	return ((uint64_t)be32dec(b) << 32) | be32dec(b + 4);
}
#endif
int OF_getproplen(int, const char *);
int OF_getprop(int, const char *, void *, int);
bool of_hasprop(int, const char *);
int of_compatible(int, const char * const *);
#endif
