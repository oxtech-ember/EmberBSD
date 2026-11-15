/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: dist618/drm/i915/gem/i915_gem_shrinker.c (the fields of
 * the shrinker it fills in: count_objects, scan_objects, seeks, batch, private_data; the calls
 * shrinker_alloc, shrinker_register, shrinker_free); the GPL original was not read.
 *
 * Shadow of the shared <linux/shrinker.h> for the 6.18 sources: the shrinker is
 * allocated by the kernel and registered, and carries a private pointer.  The shared
 * struct (for the 5.6 drivers, registered by an inline that does nothing) is renamed.
 * shrinker_alloc, shrinker_register and shrinker_free are declared, not defined: a
 * driver whose memory the kernel cannot ask back must not link until the kernel's
 * demand for memory reaches it.
 */
#ifndef _DRM618_LINUX_SHRINKER_H_
#define _DRM618_LINUX_SHRINKER_H_

#define	shrinker	drm618_shared_shrinker
#include_next <linux/shrinker.h>
#undef	shrinker

struct shrinker {
	unsigned long	(*count_objects)(struct shrinker *, struct shrink_control *);
	unsigned long	(*scan_objects)(struct shrinker *, struct shrink_control *);
	long		seeks;
	long		batch;
	void		*private_data;
};

#endif
