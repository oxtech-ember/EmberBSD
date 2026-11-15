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
 * constant when it fits, otherwise when it has the width and signedness of T
 * (not necessarily the same type: the layer's pgoff_t is not unsigned long).
 */
/*
 * The test for a constant N is an integer constant expression (static_assert
 * and bit-field widths need one): N is compared with the range of T in the
 * widest types, the sign first.
 */
#define	__drm618_uns(T)		((__typeof__(T))-1 > (__typeof__(T))0)
#define	__drm618_tmax(T)						\
	(__drm618_uns(T) ? (unsigned long long)(__typeof__(T))~(__typeof__(T))0 : \
	    (unsigned long long)(((((__typeof__(T))1 <<			\
	    (sizeof(__typeof__(T)) * 8 - 2)) - 1) * 2) + 1))
#define	__drm618_overflows_const(N, T)					\
	((N) < 0 ?							\
	    (__drm618_uns(T) ||						\
	        (long long)(N) < -(long long)__drm618_tmax(T) - 1) :	\
	    (unsigned long long)(N) > __drm618_tmax(T))
/*
 * pgoff_t is a signed type here (uvm's), Linux's is unsigned long: for it a
 * variable is castable when it is a pgoff_t or an unsigned long wide value, as
 * for unsigned long.  Other targets: the same width and signedness.
 */
#define	__drm618_is_pgoff(T)						\
	__builtin_types_compatible_p(__typeof__(T), pgoff_t)
#ifndef castable_to_type
#define	castable_to_type(N, T)						\
	__builtin_choose_expr(__builtin_constant_p(N),			\
	    !__drm618_overflows_const((N), T),				\
	    (__builtin_types_compatible_p(__typeof__(N), __typeof__(T)) || \
	    (sizeof(N) == sizeof(__typeof__(T)) &&			\
	    __drm618_uns(__typeof__(N)) == __drm618_uns(T)) ||		\
	    (__drm618_is_pgoff(T) && sizeof(N) == sizeof(unsigned long) && \
	    __drm618_uns(__typeof__(N)))))
#endif

#endif
