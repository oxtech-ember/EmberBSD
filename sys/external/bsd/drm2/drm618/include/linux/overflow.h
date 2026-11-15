/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: the uses of overflows_type and castable_to_type in
 * dist618/drm/i915 (gem/i915_gem_object.h, i915_gem.h); the GPL original was not read.
 */
#ifndef _DRM618_LINUX_OVERFLOW_H_
#define _DRM618_LINUX_OVERFLOW_H_

#include_next <linux/overflow.h>

#include <linux/compiler.h>

/*
 * overflows_type(N, T): N does not fit the type of T (an expression or a type
 * name).  The sum 0 + N is formed in that type, and the builtin says whether
 * the result is the true sum.
 */
#define	__drm618_overflows_type(N, T)					\
	({								\
		__typeof__(T) __drm618_v = 0;				\
		__builtin_add_overflow(0, (N), &__drm618_v);		\
	})
#ifndef overflows_type
#define	overflows_type(N, T)	__drm618_overflows_type(N, T)
#endif

/*
 * castable_to_type(N, T): N can be assigned to T without loss: for a
 * constant when it fits, otherwise when it already has the type of T.
 */
#ifndef castable_to_type
#define	castable_to_type(N, T)						\
	__builtin_choose_expr(__builtin_constant_p(N),			\
	    !__drm618_overflows_type((N), T), __same_type((N), (T)0))
#endif

#endif
