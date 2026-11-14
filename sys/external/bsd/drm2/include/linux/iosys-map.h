/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD native implementation of the interface of Linux 6.18
 * include/linux/iosys-map.h (GPL-2.0-only; no source copied): a pointer to
 * memory that is either system memory or I/O memory.
 *
 * The layer has no generic __iomem or memcpy_toio(); drivers define their own
 * __iomem.  So the I/O side is plain volatile accesses here: single 1, 2, 4
 * or 8 byte accesses for aligned scalars, byte copies otherwise.  This is
 * enough for x86 and for AArch64 mappings that allow unaligned-free access;
 * a mapping that needs special accessors must wrap this header.
 */

#ifndef _LINUX_IOSYS_MAP_H_
#define _LINUX_IOSYS_MAP_H_

#include <linux/types.h>

#ifdef _KERNEL
#include <sys/systm.h>
#endif

struct iosys_map {
	union {
		void	*vaddr_iomem;
		void	*vaddr;
	};
	bool	is_iomem;
};

#define	IOSYS_MAP_INIT_VADDR(VADDR)					\
	{ .vaddr = (VADDR), .is_iomem = false, }

#define	IOSYS_MAP_INIT_VADDR_IOMEM(VADDR_IOMEM)				\
	{ .vaddr_iomem = (VADDR_IOMEM), .is_iomem = true, }

/* A map at OFFSET bytes into MAP, in the same address space. */
#define	IOSYS_MAP_INIT_OFFSET(MAP, OFFSET)				\
	{								\
		.vaddr = (char *)(MAP)->vaddr + (OFFSET),		\
		.is_iomem = (MAP)->is_iomem,				\
	}

static inline void
iosys_map_set_vaddr(struct iosys_map *map, void *vaddr)
{

	map->vaddr = vaddr;
	map->is_iomem = false;
}

static inline void
iosys_map_set_vaddr_iomem(struct iosys_map *map, void *vaddr_iomem)
{

	map->vaddr_iomem = vaddr_iomem;
	map->is_iomem = true;
}

static inline bool
iosys_map_is_equal(const struct iosys_map *lhs, const struct iosys_map *rhs)
{

	if (lhs->is_iomem != rhs->is_iomem)
		return false;
	return lhs->vaddr == rhs->vaddr;
}

static inline bool
iosys_map_is_null(const struct iosys_map *map)
{

	return map->vaddr == NULL;
}

static inline bool
iosys_map_is_set(const struct iosys_map *map)
{

	return !iosys_map_is_null(map);
}

static inline void
iosys_map_clear(struct iosys_map *map)
{

	map->vaddr = NULL;
	map->is_iomem = false;
}

static inline void
iosys_map_incr(struct iosys_map *map, size_t incr)
{

	map->vaddr = (char *)map->vaddr + incr;
}

static inline void
iosys_map_memcpy_to(struct iosys_map *dst, size_t dst_offset,
    const void *src, size_t len)
{
	char *d = (char *)dst->vaddr + dst_offset;
	const char *s = src;

	if (!dst->is_iomem) {
		memcpy(d, src, len);
		return;
	}
	if ((len == 1 || len == 2 || len == 4 || len == 8) &&
	    ((uintptr_t)d & (len - 1)) == 0 && ((uintptr_t)s & (len - 1)) == 0) {
		switch (len) {
		case 1:
			*(volatile uint8_t *)d = *(const uint8_t *)s;
			break;
		case 2:
			*(volatile uint16_t *)d = *(const uint16_t *)s;
			break;
		case 4:
			*(volatile uint32_t *)d = *(const uint32_t *)s;
			break;
		default:
			*(volatile uint64_t *)d = *(const uint64_t *)s;
			break;
		}
		return;
	}
	while (len--)
		*(volatile char *)d++ = *s++;
}

static inline void
iosys_map_memcpy_from(void *dst, const struct iosys_map *src,
    size_t src_offset, size_t len)
{
	const char *s = (const char *)src->vaddr + src_offset;
	char *d = dst;

	if (!src->is_iomem) {
		memcpy(dst, s, len);
		return;
	}
	if ((len == 1 || len == 2 || len == 4 || len == 8) &&
	    ((uintptr_t)d & (len - 1)) == 0 && ((uintptr_t)s & (len - 1)) == 0) {
		switch (len) {
		case 1:
			*(uint8_t *)d = *(const volatile uint8_t *)s;
			break;
		case 2:
			*(uint16_t *)d = *(const volatile uint16_t *)s;
			break;
		case 4:
			*(uint32_t *)d = *(const volatile uint32_t *)s;
			break;
		default:
			*(uint64_t *)d = *(const volatile uint64_t *)s;
			break;
		}
		return;
	}
	while (len--)
		*d++ = *(const volatile char *)s++;
}

static inline void
iosys_map_memset(struct iosys_map *dst, size_t offset, int value, size_t len)
{
	char *d = (char *)dst->vaddr + offset;

	if (!dst->is_iomem) {
		memset(d, value, len);
		return;
	}
	while (len--)
		*(volatile char *)d++ = (char)value;
}

/* Read or write one scalar of TYPE at OFFSET bytes into the map. */
#define	iosys_map_rd(MAP, OFFSET, TYPE) ({				\
	TYPE __im_val;							\
	iosys_map_memcpy_from(&__im_val, (MAP), (OFFSET), sizeof(__im_val)); \
	__im_val;							\
})

#define	iosys_map_wr(MAP, OFFSET, TYPE, VAL) ({				\
	TYPE __im_val = (VAL);						\
	iosys_map_memcpy_to((MAP), (OFFSET), &__im_val, sizeof(__im_val)); \
})

/* Same for a field of a structure that starts at STRUCT_OFFSET. */
#define	iosys_map_rd_field(MAP, STRUCT_OFFSET, STRUCT_TYPE, FIELD) ({	\
	__typeof__(((STRUCT_TYPE *)0)->FIELD) __im_fval;		\
	iosys_map_memcpy_from(&__im_fval, (MAP),			\
	    (STRUCT_OFFSET) + __builtin_offsetof(STRUCT_TYPE, FIELD),	\
	    sizeof(__im_fval));						\
	__im_fval;							\
})

#define	iosys_map_wr_field(MAP, STRUCT_OFFSET, STRUCT_TYPE, FIELD, VAL) ({ \
	__typeof__(((STRUCT_TYPE *)0)->FIELD) __im_fval = (VAL);	\
	iosys_map_memcpy_to((MAP),					\
	    (STRUCT_OFFSET) + __builtin_offsetof(STRUCT_TYPE, FIELD),	\
	    &__im_fval, sizeof(__im_fval));				\
})

#endif	/* _LINUX_IOSYS_MAP_H_ */
