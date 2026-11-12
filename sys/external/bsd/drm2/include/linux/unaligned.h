/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD native. Interface of Linux 6.18 include/linux/unaligned.h (GPL-2.0), which replaced
 * <asm/unaligned.h>.  The layer's <asm/unaligned.h> has the little-endian
 * 16/32-bit accessors; the rest is added here with memcpy() and
 * sys/endian.h.  get_unaligned()/put_unaligned() use the native byte order.
 */

#ifndef _LINUX_UNALIGNED_H_
#define _LINUX_UNALIGNED_H_

#include <sys/endian.h>

#include <asm/unaligned.h>

static inline uint16_t
get_unaligned_be16(const void *p)
{

	return be16dec(p);
}

static inline void
put_unaligned_be16(uint16_t v, void *p)
{

	be16enc(p, v);
}

static inline uint32_t
get_unaligned_be32(const void *p)
{

	return be32dec(p);
}

static inline void
put_unaligned_be32(uint32_t v, void *p)
{

	be32enc(p, v);
}

static inline uint64_t
get_unaligned_le64(const void *p)
{

	return le64dec(p);
}

static inline void
put_unaligned_le64(uint64_t v, void *p)
{

	le64enc(p, v);
}

static inline uint64_t
get_unaligned_be64(const void *p)
{

	return be64dec(p);
}

static inline void
put_unaligned_be64(uint64_t v, void *p)
{

	be64enc(p, v);
}

/* (0, *(PTR)) is an rvalue, so the copy is not const even if *(PTR) is. */
#define	get_unaligned(PTR) ({						\
	__typeof__((0, *(PTR))) __gu_v;					\
	memcpy(&__gu_v, (PTR), sizeof(__gu_v));				\
	__gu_v;								\
})

#define	put_unaligned(VAL, PTR) ({					\
	__typeof__(*(PTR)) __pu_v = (VAL);				\
	memcpy((PTR), &__pu_v, sizeof(__pu_v));				\
	(void)0;							\
})

#endif	/* _LINUX_UNALIGNED_H_ */
