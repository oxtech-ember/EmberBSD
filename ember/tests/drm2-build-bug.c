/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: the uses of the tested names in the MIT files of dist618/drm/i915, the
 * documented behaviour of the Linux interfaces, and the layer headers under test; the GPL original was not read.
 *
 * Host contract for drm618/include/drm618_build_bug.h: a BUILD_BUG_ON on a
 * parameter of an always-inline function is judged for each caller, after
 * inlining.  Built with -O2.  -DBUILD_BUG_NEGATIVE adds a caller whose
 * condition is true: the compilation must fail.
 */
#include <stdio.h>

#define	BUILD_BUG_ON_MSG(c, m)	/* the layer's, replaced by the header */
#define	BUILD_BUG_ON(c)
#include <drm618_build_bug.h>

static inline __attribute__((__always_inline__)) int
need_constant(int p)
{

	BUILD_BUG_ON(!__builtin_constant_p(p));
	BUILD_BUG_ON(p > 100);
	return p + 1;
}

int
main(void)
{
	int r = need_constant(7) + need_constant(100);
	/* two uses on one line must not share a name */
	BUILD_BUG_ON(sizeof(char) != 1); BUILD_BUG_ON(sizeof(int) < 2);
#ifdef BUILD_BUG_NEGATIVE
	r += need_constant(101);
#endif
	printf("drm2-build-bug: %d\n", r);
	return r == 8 + 101 ? 0 : 1;
}
