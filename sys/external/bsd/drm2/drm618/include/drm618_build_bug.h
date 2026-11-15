/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: how the i915 files use BUILD_BUG_ON in always-inline
 * functions on their parameters (dist618/drm/i915/i915_drv.h: BUILD_BUG_ON(!__builtin_constant_p(p)));
 * the clang documentation of the error attribute; the GPL original was not read.
 *
 * BUILD_BUG_ON that is decided after inlining, not while parsing.  A call to
 * a function with the error attribute is a compile error only if it is still
 * there once the optimiser has dropped what cannot run; so a condition that
 * depends on a parameter of an always-inline function is judged for each
 * caller.  (The layer's own BUILD_BUG_ON asserts while parsing and cannot take
 * such a condition.)  Needs optimisation (-O2), as the kernel is built.
 */
#ifndef _DRM618_BUILD_BUG_H_
#define _DRM618_BUILD_BUG_H_

#define	__DRM618_BB_PASTE2(a, b)	a##b
#define	__DRM618_BB_PASTE(a, b)		__DRM618_BB_PASTE2(a, b)
#define	__DRM618_BB_ASSERT(cond, msg, id)				\
	do {								\
		extern void id(void) __attribute__((__error__(msg)));	\
		if (cond)						\
			id();						\
	} while (0)

#undef	BUILD_BUG_ON_MSG
#undef	BUILD_BUG_ON
#define	BUILD_BUG_ON_MSG(cond, msg)					\
	__DRM618_BB_ASSERT(cond, msg,					\
	    __DRM618_BB_PASTE(drm618_build_bug_, __COUNTER__))
#define	BUILD_BUG_ON(cond)						\
	BUILD_BUG_ON_MSG(cond, "BUILD_BUG_ON failed: " #cond)

#endif
