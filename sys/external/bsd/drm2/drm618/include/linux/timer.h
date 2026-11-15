/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: the uses of timer_pending in dist618/drm/i915 (a pointer to a
 * const object); the GPL original was not read.
 *
 * Shadow of the shared <linux/timer.h> for the 6.18 sources: timer_pending takes a const pointer, as in
 * Linux.  The shared one (for the 5.6 drivers) is renamed and called.
 */
#ifndef _DRM618_LINUX_TIMER_H_
#define _DRM618_LINUX_TIMER_H_

#define	timer_pending	drm618_shared_timer_pending
#include_next <linux/timer.h>
#undef	timer_pending

static inline bool
timer_pending(const struct timer_list *timer)
{

	return drm618_shared_timer_pending(__UNCONST(timer));
}

#endif
