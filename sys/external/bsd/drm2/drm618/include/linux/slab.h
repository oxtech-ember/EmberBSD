/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: the uses of kfree in dist618/drm/i915 (a pointer to a
 * const object); the GPL original was not read.
 *
 * Shadow of the shared <linux/slab.h> for the 6.18 sources: kfree takes a const pointer, as in
 * Linux.  The shared one (for the 5.6 drivers) is renamed and called.
 */
#ifndef _DRM618_LINUX_SLAB_H_
#define _DRM618_LINUX_SLAB_H_

#define	kfree	drm618_shared_kfree
#include_next <linux/slab.h>
#undef	kfree

static inline void
kfree(const void *ptr)
{

	drm618_shared_kfree(__UNCONST(ptr));
}

#endif
