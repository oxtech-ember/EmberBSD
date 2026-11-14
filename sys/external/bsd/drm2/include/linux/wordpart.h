/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD native. Linux 6.18 include/linux/wordpart.h (GPL-2.0) interface.  The 32-bit
 * halves are in <linux/kernel.h>; this adds the 16-bit halves and
 * REPEAT_BYTE().
 */

#ifndef _LINUX_WORDPART_H_
#define _LINUX_WORDPART_H_

#include <linux/kernel.h>

#define	upper_16_bits(N)	((uint16_t)((N) >> 16))
#define	lower_16_bits(N)	((uint16_t)((N) & 0xffff))

#define	REPEAT_BYTE(X)		((~0UL / 0xff) * (X))
#define	REPEAT_BYTE_U32(X)	lower_32_bits(REPEAT_BYTE(X))

#endif	/* _LINUX_WORDPART_H_ */
