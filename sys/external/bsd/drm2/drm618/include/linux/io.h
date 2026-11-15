/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: the uses of readb..writeq in dist618/drm/i915 (intel_uncore.h);
 * the NetBSD amd64 memory model; the GPL original was not read.
 *
 * Memory mapped register access of the Linux sources.  An address is a
 * kernel virtual address of a mapping made by the bus space or pmap code, so
 * the access is a volatile load or store of the width asked for (x86 keeps
 * the order of such accesses to uncached memory).
 */
#ifndef _DRM618_LINUX_IO_H_
#define _DRM618_LINUX_IO_H_

#include_next <linux/io.h>

#include <sys/types.h>

/* the prefix header carries a readl for the 6.18 core; this one replaces it */
#undef	readl

static inline uint8_t
readb(const volatile void *addr)
{

	return *(const volatile uint8_t *)addr;
}

static inline uint16_t
readw(const volatile void *addr)
{

	return *(const volatile uint16_t *)addr;
}

static inline uint32_t
readl(const volatile void *addr)
{

	return *(const volatile uint32_t *)addr;
}

static inline uint64_t
readq(const volatile void *addr)
{

	return *(const volatile uint64_t *)addr;
}

static inline void
writeb(uint8_t v, volatile void *addr)
{

	*(volatile uint8_t *)addr = v;
}

static inline void
writew(uint16_t v, volatile void *addr)
{

	*(volatile uint16_t *)addr = v;
}

static inline void
writel(uint32_t v, volatile void *addr)
{

	*(volatile uint32_t *)addr = v;
}

static inline void
writeq(uint64_t v, volatile void *addr)
{

	*(volatile uint64_t *)addr = v;
}

#endif
