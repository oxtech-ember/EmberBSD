/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD native implementation of intlog2() and intlog10() of
 * Linux 6.18 lib/math/int_log.c (LGPL-2.1-or-later; no source copied).
 *
 * log2(v) = n + log2(m) with 2^n <= v and m = v / 2^n in [1, 2).  The
 * fraction is produced one bit per step by squaring m: if m*m >= 2 the next
 * bit is 1 and m*m is halved.  m is kept as a 64-bit Q1.63 number, so the
 * error stays far below one unit of the 24-bit result.  The fraction is
 * truncated.  intlog10(v) = intlog2(v) * log10(2); 646456993 = log10(2) * 2^31.
 */

#ifdef _KERNEL
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");
#endif

#include <linux/int_log.h>

/* High 64 bits of the 128-bit product a * b. */
static uint64_t
int_log_mulhi64(uint64_t a, uint64_t b)
{
	uint64_t a_lo = a & 0xffffffffU, a_hi = a >> 32;
	uint64_t b_lo = b & 0xffffffffU, b_hi = b >> 32;
	uint64_t ll = a_lo * b_lo;
	uint64_t lh = a_lo * b_hi;
	uint64_t hl = a_hi * b_lo;
	uint64_t hh = a_hi * b_hi;
	uint64_t mid = (ll >> 32) + (lh & 0xffffffffU) + (hl & 0xffffffffU);

	return hh + (lh >> 32) + (hl >> 32) + (mid >> 32);
}

unsigned int
intlog2(u32 value)
{
	uint64_t m, hi;
	unsigned int n, frac = 0, i;

	if (value == 0)
		return 0;
	n = 31 - (unsigned int)__builtin_clz(value);
	m = (uint64_t)value << (63 - n);		/* Q1.63 in [1, 2) */
	for (i = 0; i < 24; i++) {
		hi = int_log_mulhi64(m, m);		/* m*m as Q2.62 */
		if ((hi >> 63) != 0) {			/* m*m >= 2 */
			frac = (frac << 1) | 1;
			m = hi;				/* m*m / 2 as Q1.63 */
		} else {
			frac <<= 1;
			m = hi << 1;
		}
	}
	return (n << 24) | frac;
}

unsigned int
intlog10(u32 value)
{

	return (unsigned int)(((uint64_t)intlog2(value) * 646456993ULL) >> 31);
}
