/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD native. Interface of Linux 6.18 include/linux/swab.h and uapi/linux/swab.h
 * (GPL-2.0 WITH Linux-syscall-note); implemented with compiler byte-swap
 * builtins, usable in constant expressions.
 */

#ifndef _LINUX_SWAB_H_
#define _LINUX_SWAB_H_

#include <linux/types.h>

#define	___constant_swab16(X)	((u16)__builtin_bswap16((u16)(X)))
#define	___constant_swab32(X)	((u32)__builtin_bswap32((u32)(X)))
#define	___constant_swab64(X)	((u64)__builtin_bswap64((u64)(X)))

#define	__swab16(X)		___constant_swab16(X)
#define	__swab32(X)		___constant_swab32(X)
#define	__swab64(X)		___constant_swab64(X)
#define	swab16(X)		___constant_swab16(X)
#define	swab32(X)		___constant_swab32(X)
#define	swab64(X)		___constant_swab64(X)

/* Swap the 16-bit halves of a 32-bit word. */
#define	swahw32(X)		((u32)((((u32)(X)) << 16) | (((u32)(X)) >> 16)))
/* Swap the bytes inside each 16-bit half of a 32-bit word. */
#define	swahb32(X)		((u32)(((((u32)(X)) & 0x00ff00ffU) << 8) | \
				    ((((u32)(X)) & 0xff00ff00U) >> 8)))

static inline u16
swab16p(const u16 *p)
{

	return swab16(*p);
}

static inline u32
swab32p(const u32 *p)
{

	return swab32(*p);
}

static inline u64
swab64p(const u64 *p)
{

	return swab64(*p);
}

static inline void
swab16s(u16 *p)
{

	*p = swab16(*p);
}

static inline void
swab32s(u32 *p)
{

	*p = swab32(*p);
}

static inline void
swab64s(u64 *p)
{

	*p = swab64(*p);
}

#endif	/* _LINUX_SWAB_H_ */
