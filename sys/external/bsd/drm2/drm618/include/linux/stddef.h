/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: the use of typeof_member in dist618/drm/i915/intel_runtime_pm.h; the GPL original was not read.
 */
#ifndef _DRM618_LINUX_STDDEF_H_
#define _DRM618_LINUX_STDDEF_H_

#include_next <linux/stddef.h>

#ifndef typeof_member
#define	typeof_member(T, member)	__typeof__(((T *)0)->member)
#endif

#endif
