/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD native implementation of the interface of Linux 6.18
 * include/linux/int_log.h (LGPL-2.1-or-later; no source copied).
 *
 * intlog2(v) is log2(v) * 2^24 and intlog10(v) is log10(v) * 2^24, as
 * unsigned fixed point; both return 0 for 0.  The exported names carry a
 * linux_ prefix (sys/dev/dtv has its own dtv_intlog10).
 */

#ifndef _LINUX_INT_LOG_H_
#define _LINUX_INT_LOG_H_

#include <linux/types.h>

#define	intlog2		linux_intlog2
#define	intlog10	linux_intlog10

unsigned int	intlog2(u32);
unsigned int	intlog10(u32);

#endif	/* _LINUX_INT_LOG_H_ */
