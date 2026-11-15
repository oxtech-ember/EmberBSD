/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: the uses of msleep_interruptible in dist618/drm/i915; NetBSD kpause(9); the GPL original was not read.
 */
#ifndef _DRM618_LINUX_DELAY_H_
#define _DRM618_LINUX_DELAY_H_

#include_next <linux/delay.h>

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>

/* sleep for MSEC milliseconds unless a signal arrives; returns the milliseconds left (0 when the time passed) */
static inline unsigned long
msleep_interruptible(unsigned int msec)
{
	unsigned int start = getticks(), elapsed;
	int error;

	error = kpause("lnxmslpi", true, MAX(1, mstohz(msec)), NULL);
	if (error == 0 || error == EWOULDBLOCK)
		return 0;
	elapsed = (getticks() - start) * (1000 / hz);
	return msec > elapsed ? msec - elapsed : 0;
}

#endif
