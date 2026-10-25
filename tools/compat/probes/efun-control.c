/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD (AI-assisted). Native ABI capability probe. */
#include <sys/types.h>
#include <sys/param.h>
#include <err.h>
#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <util.h>
#include <wchar.h>

/* Volatile pointers keep the native symbol references in the link. */
static __typeof__(&strlcpy) volatile p_strlcpy = &strlcpy;
static __typeof__(&strlcat) volatile p_strlcat = &strlcat;
static __typeof__(&strndup) volatile p_strndup = &strndup;
static __typeof__(&vasprintf) volatile p_vasprintf = &vasprintf;
static __typeof__(&strtoimax) volatile p_strtoimax = &strtoimax;
static __typeof__(&strtoumax) volatile p_strtoumax = &strtoumax;
static __typeof__(&fgetln) volatile p_fgetln = &fgetln;

int
main(void)
{
	void *p = calloc(1, 8);
	if (p == NULL)
		return 1;
	free(p);
	return p_strlcpy == NULL || p_strlcat == NULL || p_strndup == NULL ||
	    p_vasprintf == NULL || p_strtoimax == NULL || p_strtoumax == NULL ||
	    p_fgetln == NULL;
}
