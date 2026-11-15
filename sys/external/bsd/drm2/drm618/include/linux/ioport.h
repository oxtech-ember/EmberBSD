/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: the uses of resource_size and resource_contains in dist618/drm/i915 (a pointer to a
 * const object); the GPL original was not read.
 *
 * Shadow of the shared <linux/ioport.h> for the 6.18 sources: resource_size and resource_contains takes a const pointer, as in
 * Linux.  The shared one (for the 5.6 drivers) is renamed and called.
 */
#ifndef _DRM618_LINUX_IOPORT_H_
#define _DRM618_LINUX_IOPORT_H_

#define	resource_size		drm618_shared_resource_size
#define	resource_contains	drm618_shared_resource_contains
#include_next <linux/ioport.h>
#undef	resource_size
#undef	resource_contains

static inline resource_size_t
resource_size(const struct resource *resource)
{

	return resource->end - resource->start + 1;
}

static inline bool
resource_contains(const struct resource *r1, const struct resource *r2)
{

	return r1->start <= r2->start && r1->end >= r2->end;
}

#endif
