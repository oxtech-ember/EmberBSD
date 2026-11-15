/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: the uses of refcount_read in dist618/drm/i915; the GPL original was not read.
 */
#ifndef _DRM618_LINUX_REFCOUNT_H_
#define _DRM618_LINUX_REFCOUNT_H_

#include_next <linux/refcount.h>

static inline unsigned int
refcount_read(const struct refcount *rc)
{

	return atomic_read(&rc->rc_count);
}

#endif
