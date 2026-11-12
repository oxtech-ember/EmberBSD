/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD native. Linux 6.18 include/linux/math.h (GPL-2.0) interface.  The rounding macros,
 * mult_frac() and abs64() are in <linux/kernel.h> and sys/param.h; this
 * header adds abs() for any signed integer width, abs_diff(), int_sqrt() and
 * reciprocal_scale().
 */

#ifndef _LINUX_MATH_H_
#define _LINUX_MATH_H_

#include <linux/kernel.h>
#include <linux/types.h>

/*
 * abs() for int, long and long long.  libkern declares abs(int) before this
 * point (via <linux/kernel.h>); the macro wraps the width-specific functions
 * and keeps the identifier usable as a function name afterwards.
 */
#define	abs(X)								\
	_Generic((X),							\
	    long: labs,							\
	    long long: llabs,						\
	    default: (abs))(X)

/* Distance between two values of the same signedness, without overflow. */
#define	abs_diff(A, B) ({						\
	__typeof__(A) __ad_a = (A);					\
	__typeof__(B) __ad_b = (B);					\
	(__ad_a > __ad_b) ? (__ad_a - __ad_b) : (__ad_b - __ad_a);	\
})

static inline unsigned long
int_sqrt(unsigned long x)
{
	unsigned long r = 0, bit = 1UL << (sizeof(x) * 8 - 2);

	while (bit > x)
		bit >>= 2;
	while (bit != 0) {
		if (x >= r + bit) {
			x -= r + bit;
			r = (r >> 1) + bit;
		} else {
			r >>= 1;
		}
		bit >>= 2;
	}
	return r;
}

/* val * ep_ro / 2^32, i.e. scale a 32-bit hash into [0, ep_ro). */
static inline u32
reciprocal_scale(u32 val, u32 ep_ro)
{

	return (u32)(((u64)val * ep_ro) >> 32);
}

#endif	/* _LINUX_MATH_H_ */
