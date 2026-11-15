/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: the uses of order_base_2 as a bit-field width in
 * dist618/drm/i915/display/intel_display_types.h; the GPL original was not read.
 *
 * order_base_2 as an integer constant expression for a constant argument (a
 * bit-field width, an array size), a call of the layer's function otherwise.
 */
#ifndef _DRM618_LINUX_LOG2_H_
#define _DRM618_LINUX_LOG2_H_

#include_next <linux/log2.h>

/* ceil(log2(n)); 0 for n <= 1 */
#define	order_base_2(n)							\
	__builtin_choose_expr(__builtin_constant_p(n),			\
	    ((n) <= 1 ? 0 : 64 - __builtin_clzll((unsigned long long)(n) - 1)), \
	    order_base_2(n))

#endif
