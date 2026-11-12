/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD native. Linux 6.18 include/linux/minmax.h (GPL-2.0) interface.  min(), max(),
 * clamp*(), min3(), max3(), swap() are in <linux/kernel.h> (arguments may be
 * evaluated twice); added here are min_not_zero(), umin(), umax(),
 * min_array() and max_array().
 */

#ifndef _LINUX_MINMAX_H_
#define _LINUX_MINMAX_H_

#include <linux/kernel.h>

#define	min_not_zero(X, Y) ({						\
	__typeof__(X) __mnz_x = (X);					\
	__typeof__(Y) __mnz_y = (Y);					\
	(__mnz_x == 0) ? __mnz_y :					\
	    ((__mnz_y == 0) ? __mnz_x : MIN(__mnz_x, __mnz_y));		\
})

/* Compare as unsigned values: negative operands convert to large ones. */
#define	umin(X, Y)	min_t(unsigned long long, X, Y)
#define	umax(X, Y)	max_t(unsigned long long, X, Y)

#define	min_array(ARRAY, LEN) ({					\
	__typeof__(&(ARRAY)[0]) __ma_a = (ARRAY);			\
	__typeof__((ARRAY)[0]) __ma_v = __ma_a[0];			\
	size_t __ma_i;							\
	for (__ma_i = 1; __ma_i < (size_t)(LEN); __ma_i++)		\
		if (__ma_a[__ma_i] < __ma_v)				\
			__ma_v = __ma_a[__ma_i];			\
	__ma_v;								\
})

#define	max_array(ARRAY, LEN) ({					\
	__typeof__(&(ARRAY)[0]) __xa_a = (ARRAY);			\
	__typeof__((ARRAY)[0]) __xa_v = __xa_a[0];			\
	size_t __xa_i;							\
	for (__xa_i = 1; __xa_i < (size_t)(LEN); __xa_i++)		\
		if (__xa_a[__xa_i] > __xa_v)				\
			__xa_v = __xa_a[__xa_i];			\
	__xa_v;								\
})

#endif	/* _LINUX_MINMAX_H_ */
