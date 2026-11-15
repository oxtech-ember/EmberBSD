/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: the uses of FIELD_FIT in dist618/drm/i915 (i915_reg_defs.h); the GPL original was not read.
 */
#ifndef _DRM618_LINUX_BITFIELD_H_
#define _DRM618_LINUX_BITFIELD_H_

#include_next <linux/bitfield.h>

/* true if VAL, shifted to the field MASK, has no bit outside it */
#ifndef FIELD_FIT
#define	FIELD_FIT(MASK, VAL)						\
	(!((((__typeof__(MASK))(VAL)) << __bf_shf(MASK)) & ~(MASK)))
#endif

#endif
