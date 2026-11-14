/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD native. Interface of Linux 6.18 include/linux/timekeeping.h (GPL-2.0).  The layer's
 * <linux/ktime.h> has ktime_get() and its variants; this adds the few
 * nanosecond accessors the DRM code uses.
 */

#ifndef _LINUX_TIMEKEEPING_H_
#define _LINUX_TIMEKEEPING_H_

#include <linux/ktime.h>
#include <linux/time64.h>

static inline uint64_t
ktime_get_boottime_ns(void)
{

	return ktime_to_ns(ktime_get_boottime());
}

static inline uint64_t
ktime_get_raw_fast_ns(void)
{

	return ktime_get_raw_ns();
}

static inline uint64_t
ktime_get_real_ns(void)
{

	return ktime_to_ns(ktime_get_real());
}

#endif	/* _LINUX_TIMEKEEPING_H_ */
