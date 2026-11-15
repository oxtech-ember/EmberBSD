/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: the uses of BIT_U8/16/32/64 and GENMASK_U8/16/32/64 in the MIT
 * files of dist618/drm/i915 (register definitions, i915_reg_defs.h); the GPL original was not read.
 *
 * Fixed width bit masks: the layer's linux/bits.h (common/include) has the
 * long sized ones.  Added for the Linux 6.18 sources only.
 */
#ifndef _DRM618_LINUX_BITS_H_
#define _DRM618_LINUX_BITS_H_

#include_next <linux/bits.h>

#include <sys/types.h>

#define	BIT_U8(n)	((uint8_t)(UINT8_C(1) << (n)))
#define	BIT_U16(n)	((uint16_t)(UINT16_C(1) << (n)))
#define	BIT_U32(n)	((uint32_t)(UINT32_C(1) << (n)))
#define	BIT_U64(n)	((uint64_t)(UINT64_C(1) << (n)))

/* bits h down to l, both inclusive, in the stated width */
#define	GENMASK_U8(h, l)	((uint8_t)((UINT8_MAX << (l)) & (UINT8_MAX >> (7 - (h)))))
#define	GENMASK_U16(h, l)	((uint16_t)((UINT16_MAX << (l)) & (UINT16_MAX >> (15 - (h)))))
#define	GENMASK_U32(h, l)	((uint32_t)((UINT32_MAX << (l)) & (UINT32_MAX >> (31 - (h)))))
#define	GENMASK_U64(h, l)	((uint64_t)((UINT64_MAX << (l)) & (UINT64_MAX >> (63 - (h)))))

#endif
