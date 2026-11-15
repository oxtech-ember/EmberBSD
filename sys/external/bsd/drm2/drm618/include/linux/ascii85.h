/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: dist618/drm/i915/i915_gpu_error.c (the two calls); the Ascii85 definition
 * (five characters from '!' for each 32 bit word, most significant digit first, 'z' for a zero word); the GPL original was not read.
 */
#ifndef _DRM618_LINUX_ASCII85_H_
#define _DRM618_LINUX_ASCII85_H_

#include <sys/types.h>

#define	ASCII85_BUFSZ	6

/* number of 32 bit words that LEN bytes take (the last one padded) */
static inline long
ascii85_encode_len(long len)
{

	return (len + 3) / 4;
}

/* OUT has room for ASCII85_BUFSZ characters; returns it, terminated */
static inline char *
ascii85_encode(uint32_t in, char *out)
{
	int i;

	if (in == 0) {
		out[0] = 'z';
		out[1] = '\0';
		return out;
	}
	out[5] = '\0';
	for (i = 4; i >= 0; i--) {
		out[i] = '!' + (char)(in % 85);
		in /= 85;
	}
	return out;
}

#endif
