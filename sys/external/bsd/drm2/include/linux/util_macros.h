/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD native implementation of the interface of Linux 6.18
 * include/linux/util_macros.h (GPL-2.0; no source copied).
 *
 * find_closest(x, a, n): index of the element of the ascending array a[n]
 * nearest to x; on an exact tie (x is the mean of a[i] and a[i+1]) the
 * smaller element wins.  The _descending variant takes a descending array
 * and also prefers the smaller (higher-index) element on a tie.  Compared
 * as 2*x against a[i] + a[i+1], so there is no rounding of the mean.  The
 * rule was derived from, and checked against, a differential run of Linux
 * 6.18 over a range of inputs (a first version using a rounded mean
 * disagreed on odd sums).
 *
 * u64_to_user_ptr() is not defined here: the layer already provides it in
 * <linux/kernel.h>.
 */

#ifndef _LINUX_UTIL_MACROS_H_
#define _LINUX_UTIL_MACROS_H_

#include <linux/types.h>

#ifndef for_each_if
#define	for_each_if(condition)	if (!(condition)) {} else
#endif

#define	__find_closest(x, a, as, op) ({					\
	__typeof__(as) __fc_i;						\
	__typeof__(as) __fc_last = (as) - 1;				\
	__typeof__(x) __fc_x = (x);					\
	__typeof__(*(a)) const *__fc_a = (a);				\
	for (__fc_i = 0; __fc_i < __fc_last; __fc_i++) {		\
		if (2 * __fc_x op __fc_a[__fc_i] + __fc_a[__fc_i + 1])	\
			break;						\
	}								\
	__fc_i;								\
})

#define	find_closest(x, a, as)		__find_closest(x, a, as, <=)
#define	find_closest_descending(x, a, as) __find_closest(x, a, as, >)

#define	PTR_IF(cond, ptr)	((cond) ? (ptr) : (void *)0)

#define	is_insidevar(ptr, var)						\
	((uintptr_t)(ptr) >= (uintptr_t)(var) &&			\
	 (uintptr_t)(ptr) < (uintptr_t)(var) + sizeof(var))

#endif	/* _LINUX_UTIL_MACROS_H_ */
