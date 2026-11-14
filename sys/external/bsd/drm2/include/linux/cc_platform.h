/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD native.  Interface of Linux 6.18 include/linux/cc_platform.h
 * (GPL-2.0-only; no source copied).
 *
 * Confidential computing platform queries.  No supported guest on this layer
 * uses memory encryption that the DRM code must work around, so every
 * attribute reads as absent.
 */

#ifndef _LINUX_CC_PLATFORM_H_
#define _LINUX_CC_PLATFORM_H_

#include <linux/types.h>

enum cc_attr {
	CC_ATTR_MEM_ENCRYPT,
	CC_ATTR_HOST_MEM_ENCRYPT,
	CC_ATTR_GUEST_MEM_ENCRYPT,
	CC_ATTR_GUEST_STATE_ENCRYPT,
	CC_ATTR_GUEST_UNROLL_STRING_IO,
	CC_ATTR_GUEST_SEV_SNP,
	CC_ATTR_HOST_SEV_SNP,
};

static inline bool
cc_platform_has(enum cc_attr attr __unused)
{

	return false;
}

#endif	/* _LINUX_CC_PLATFORM_H_ */
