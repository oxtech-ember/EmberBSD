/*	$NetBSD$	*/

/*-
 * Copyright (c) 2026 The EmberBSD Project.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Minimal Linux kernel API emulation for the rtw89 sources.
 *
 * This header is only included by the files derived from the Linux rtw89
 * driver.  It must never be included together with the NetBSD net80211
 * headers: the Linux mac80211 names defined in compat/net/mac80211.h clash
 * with them.  The NetBSD side talks to the Linux side through rtw89_nb.h.
 *
 * The DMA emulation assumes cache-coherent DMA (x86).
 */

#ifndef _RTW89_COMPAT_H_
#define _RTW89_COMPAT_H_

#include <sys/param.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/atomic.h>
#include <sys/bitops.h>
#include <sys/callout.h>
#include <sys/condvar.h>
#include <sys/device.h>
#include <sys/endian.h>
#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/queue.h>
#include <sys/stdarg.h>

/* ------------------------------------------------------------------ */
/* Compiler and basic types                                            */
/* ------------------------------------------------------------------ */

#define __iomem
#define __rcu
#define __force
#define __must_check
#define __maybe_unused		__unused
#define __always_unused		__unused
#define __printf(a, b)		__printflike(a, b)
#define __counted_by(m)
#define __nonstring
#define noinline		__noinline
#define __cold
#define likely(x)		__predict_true(x)
#define unlikely(x)		__predict_false(x)
#define fallthrough		__attribute__((__fallthrough__))
#define READ_ONCE(x)		(*(const volatile __typeof__(x) *)&(x))
#define WRITE_ONCE(x, v)	(*(volatile __typeof__(x) *)&(x) = (v))
#define barrier()		__insn_barrier()
#define BUILD_BUG_ON(c)		do { } while (0)
#define BUILD_BUG_ON_ZERO(e)	(0)
#define static_assert(expr, ...)	__rtw89_static_assert(expr, ##__VA_ARGS__, #expr)
#define __rtw89_static_assert(expr, msg, ...)	_Static_assert(expr, msg)
#define __stringify_1(x)	#x
#define __stringify(x)		__stringify_1(x)
#define sizeof_field(t, m)	sizeof(((t *)0)->m)
#define struct_size(p, m, n)	(sizeof(*(p)) + sizeof(*(p)->m) * (n))
#define array_size(a, b)	((a) * (b))
#define flex_array_size(p, m, n) (sizeof(*(p)->m) * (n))
#define DECLARE_FLEX_ARRAY(t, n)	struct { struct {} __empty_##n; t n[]; }

typedef uint8_t		u8;
typedef uint16_t	u16;
typedef uint32_t	u32;
typedef unsigned long long u64;
typedef int8_t		s8;
typedef int16_t		s16;
typedef int32_t		s32;
typedef long long	s64;
typedef uint16_t	__le16;
typedef uint32_t	__le32;
typedef uint64_t	__le64;
typedef uint16_t	__be16;
typedef uint32_t	__be32;
typedef uint64_t	__be64;
typedef uint8_t		__u8;
typedef uint16_t	__u16;
typedef uint32_t	__u32;
typedef uint64_t	__u64;
typedef int8_t		__s8;
typedef int16_t		__s16;
typedef int32_t		__s32;
typedef uint64_t	dma_addr_t;
typedef unsigned long	kernel_ulong_t;
typedef unsigned int	gfp_t;
typedef int		irqreturn_t;
typedef int64_t		ktime_t;

#define U8_MAX		((u8)~0U)
#define S8_MAX		((s8)(U8_MAX >> 1))
#define S8_MIN		((s8)(-S8_MAX - 1))
#define U16_MAX		((u16)~0U)
#define S16_MAX		((s16)(U16_MAX >> 1))
#define S16_MIN		((s16)(-S16_MAX - 1))
#define U32_MAX		((u32)~0U)
#define S32_MAX		((s32)(U32_MAX >> 1))
#define S32_MIN		((s32)(-S32_MAX - 1))
#define U64_MAX		((u64)~0ULL)

#define cpu_to_le16(x)	htole16(x)
#define cpu_to_le32(x)	htole32(x)
#define cpu_to_le64(x)	htole64(x)
#define le16_to_cpu(x)	le16toh(x)
#define le32_to_cpu(x)	le32toh(x)
#define le64_to_cpu(x)	le64toh(x)
#define cpu_to_be16(x)	htobe16(x)
#define cpu_to_be32(x)	htobe32(x)
#define be16_to_cpu(x)	be16toh(x)
#define be32_to_cpu(x)	be32toh(x)
#define le16_to_cpup(p)	le16toh(*(const u16 *)(p))
#define le32_to_cpup(p)	le32toh(*(const u32 *)(p))
#define le16_add_cpu(p, v) (*(p) = cpu_to_le16(le16_to_cpu(*(p)) + (v)))
#define le32_add_cpu(p, v) (*(p) = cpu_to_le32(le32_to_cpu(*(p)) + (v)))
#ifndef htons
#define htons(x)	htobe16(x)
#define ntohs(x)	be16toh(x)
#endif

static inline u16 get_unaligned_le16(const void *p)
{
	const u8 *b = p;
	return b[0] | (b[1] << 8);
}
static inline u32 get_unaligned_le32(const void *p)
{
	const u8 *b = p;
	return b[0] | (b[1] << 8) | (b[2] << 16) | ((u32)b[3] << 24);
}
static inline void put_unaligned_le16(u16 v, void *p)
{
	u8 *b = p;
	b[0] = v; b[1] = v >> 8;
}
static inline void put_unaligned_le32(u32 v, void *p)
{
	u8 *b = p;
	b[0] = v; b[1] = v >> 8; b[2] = v >> 16; b[3] = v >> 24;
}

/* ------------------------------------------------------------------ */
/* Arithmetic helpers                                                  */
/* ------------------------------------------------------------------ */

#undef ALIGN
#define ALIGN(x, a)		(((x) + ((a) - 1)) & ~((__typeof__(x))(a) - 1))
#define IS_ALIGNED(x, a)	(((x) & ((__typeof__(x))(a) - 1)) == 0)
#define PTR_ALIGN(p, a)		((__typeof__(p))ALIGN((uintptr_t)(p), (a)))
#define ALIGN_DOWN(x, a)	((x) & ~((__typeof__(x))(a) - 1))
#define DIV_ROUND_UP(n, d)	(((n) + (d) - 1) / (d))
#define DIV_ROUND_DOWN_ULL(n, d)	((unsigned long long)(n) / (d))
#define DIV_ROUND_UP_ULL(n, d)		DIV_ROUND_UP((unsigned long long)(n), (d))
#define DIV_ROUND_CLOSEST(x, d)	(((x) + ((d) / 2)) / (d))
#define round_up(x, y)		((((x) - 1) | ((__typeof__(x))((y) - 1))) + 1)
#define round_down(x, y)	((x) & ~((__typeof__(x))((y) - 1)))
#define ARRAY_SIZE(a)		(sizeof(a) / sizeof((a)[0]))
#undef container_of
#define container_of(p, t, m)	((t *)((char *)(p) - offsetof(t, m)))

#undef min
#undef max
#define min(a, b)	({ __typeof__(a) _a = (a); __typeof__(b) _b = (b); \
			   _a < _b ? _a : _b; })
#define max(a, b)	({ __typeof__(a) _a = (a); __typeof__(b) _b = (b); \
			   _a > _b ? _a : _b; })
#define min3(a, b, c)	min(min(a, b), c)
#define max3(a, b, c)	max(max(a, b), c)
#define min_t(t, a, b)	({ t _a = (a); t _b = (b); _a < _b ? _a : _b; })
#define max_t(t, a, b)	({ t _a = (a); t _b = (b); _a > _b ? _a : _b; })
#define clamp(v, lo, hi)	min(max(v, lo), hi)
#define clamp_t(t, v, lo, hi)	min_t(t, max_t(t, v, lo), hi)
#define clamp_val(v, lo, hi)	clamp_t(__typeof__(v), v, lo, hi)
#undef abs
#define abs(x)		({ __typeof__(x) _x = (x); _x < 0 ? -_x : _x; })
#define swap(a, b)	do { __typeof__(a) _t = (a); (a) = (b); (b) = _t; } while (0)
#define do_div(n, base)	({ u32 _r = (u64)(n) % (base); (n) = (u64)(n) / (base); _r; })
#define div_u64(a, b)	((u64)(a) / (b))
#define div_s64(a, b)	((s64)(a) / (b))
#define mult_frac(x, n, d) (((x) / (d)) * (n) + (((x) % (d)) * (n)) / (d))

static inline s32 sign_extend32(u32 value, int index)
{
	u8 shift = 31 - index;
	return (s32)(value << shift) >> shift;
}

/* ------------------------------------------------------------------ */
/* Bit operations                                                      */
/* ------------------------------------------------------------------ */

#define BIT(n)			(1UL << (n))
#define BIT_ULL(n)		(1ULL << (n))
#define BITS_PER_LONG		(sizeof(long) * NBBY)
#define BITS_PER_BYTE		NBBY
#define BITS_TO_LONGS(n)	howmany((n), BITS_PER_LONG)
#define GENMASK(h, l)		(((~0UL) << (l)) & (~0UL >> (BITS_PER_LONG - 1 - (h))))
#define GENMASK_ULL(h, l)	(((~0ULL) << (l)) & (~0ULL >> (63 - (h))))
#define DECLARE_BITMAP(n, b)	unsigned long n[BITS_TO_LONGS(b)]

#define __bf_shf(x)		(__builtin_ffsll(x) - 1)
#define FIELD_PREP(m, v)	(((__typeof__(m))(v) << __bf_shf(m)) & (m))
#define FIELD_GET(m, r)		((__typeof__(m))(((r) & (m)) >> __bf_shf(m)))
#define FIELD_FIT(m, v)		(!((((__typeof__(m))(v)) << __bf_shf(m)) & ~(m)))
#define FIELD_MAX(m)		((__typeof__(m))((m) >> __bf_shf(m)))

#define u8_get_bits(v, m)	((u8)FIELD_GET(m, (u8)(v)))
#define u16_get_bits(v, m)	((u16)FIELD_GET(m, (u16)(v)))
#define u32_get_bits(v, m)	((u32)FIELD_GET(m, (u32)(v)))
#define u8_encode_bits(v, m)	((u8)FIELD_PREP(m, v))
#define u16_encode_bits(v, m)	((u16)FIELD_PREP(m, v))
#define u32_encode_bits(v, m)	((u32)FIELD_PREP(m, v))
#define le16_get_bits(v, m)	((u16)FIELD_GET(m, le16_to_cpu(v)))
#define le32_get_bits(v, m)	((u32)FIELD_GET(m, le32_to_cpu(v)))
#define le64_get_bits(v, m)	((u64)FIELD_GET(m, le64_to_cpu(v)))
#define le16_encode_bits(v, m)	cpu_to_le16(FIELD_PREP(m, v))
#define le32_encode_bits(v, m)	cpu_to_le32(FIELD_PREP(m, v))
#define le64_encode_bits(v, m)	cpu_to_le64(FIELD_PREP(m, v))
#define u32_replace_bits(o, v, m) (((o) & ~(m)) | FIELD_PREP(m, v))
#define u16_replace_bits(o, v, m) (((o) & ~(m)) | FIELD_PREP(m, v))
#define u8_replace_bits(o, v, m) (((o) & ~(m)) | FIELD_PREP(m, v))
#define le32_replace_bits(o, v, m) \
	cpu_to_le32((le32_to_cpu(o) & ~(m)) | FIELD_PREP(m, v))
#define le16_replace_bits(o, v, m) \
	cpu_to_le16((le16_to_cpu(o) & ~(m)) | FIELD_PREP(m, v))
#define u64_encode_bits(v, m)	((u64)FIELD_PREP(m, (u64)(v)))
#define u64_get_bits(v, m)	((u64)FIELD_GET(m, (u64)(v)))
#define u8p_replace_bits(p, v, m) (*(p) = u8_replace_bits(*(p), v, m))
#define u16p_replace_bits(p, v, m) (*(p) = u16_replace_bits(*(p), v, m))
#define le16p_replace_bits(p, v, m) (*(p) = le16_replace_bits(*(p), v, m))
#define u32p_replace_bits(p, v, m) (*(p) = u32_replace_bits(*(p), v, m))
#define le32p_replace_bits(p, v, m) (*(p) = le32_replace_bits(*(p), v, m))

#define hweight8(x)		popcount32((u8)(x))
#define hweight16(x)		popcount32((u16)(x))
#define hweight32(x)		popcount32((u32)(x))
#define hweight64(x)		popcount64((u64)(x))
#define hweight_long(x)		popcount64((u64)(x))
#define __ffs(x)		((unsigned long)(__builtin_ctzl(x)))
#define __ffs64(x)		((unsigned long)(__builtin_ctzll(x)))
#define __fls(x)		((unsigned long)(BITS_PER_LONG - 1 - __builtin_clzl(x)))
#define fls(x)			((x) ? 32 - __builtin_clz(x) : 0)
#define ffz(x)			__ffs(~(x))
#undef ilog2
#define ilog2(n)		((n) ? (int)(63 - __builtin_clzll((u64)(n))) : -1)
#define is_power_of_2(n)	((n) != 0 && (((n) & ((n) - 1)) == 0))
#define rol32(w, s)		(((w) << (s)) | ((w) >> (32 - (s))))

static inline void set_bit(unsigned int nr, volatile unsigned long *addr)
{
	atomic_or_ulong(&addr[nr / BITS_PER_LONG], 1UL << (nr % BITS_PER_LONG));
}
static inline void clear_bit(unsigned int nr, volatile unsigned long *addr)
{
	atomic_and_ulong(&addr[nr / BITS_PER_LONG], ~(1UL << (nr % BITS_PER_LONG)));
}
static inline int test_bit(unsigned int nr, const volatile unsigned long *addr)
{
	return (addr[nr / BITS_PER_LONG] >> (nr % BITS_PER_LONG)) & 1;
}
static inline int test_and_set_bit(unsigned int nr, volatile unsigned long *addr)
{
	unsigned long m = 1UL << (nr % BITS_PER_LONG);
	unsigned long old, new;
	do {
		old = addr[nr / BITS_PER_LONG];
		new = old | m;
	} while (atomic_cas_ulong(&addr[nr / BITS_PER_LONG], old, new) != old);
	return (old & m) != 0;
}
static inline int test_and_clear_bit(unsigned int nr, volatile unsigned long *addr)
{
	unsigned long m = 1UL << (nr % BITS_PER_LONG);
	unsigned long old, new;
	do {
		old = addr[nr / BITS_PER_LONG];
		new = old & ~m;
	} while (atomic_cas_ulong(&addr[nr / BITS_PER_LONG], old, new) != old);
	return (old & m) != 0;
}
#define __set_bit(nr, a)	set_bit(nr, a)
#define __clear_bit(nr, a)	clear_bit(nr, a)
#define assign_bit(nr, a, v)	((v) ? set_bit(nr, a) : clear_bit(nr, a))

static inline void bitmap_zero(unsigned long *map, unsigned int nbits)
{
	memset(map, 0, BITS_TO_LONGS(nbits) * sizeof(long));
}
static inline void bitmap_fill(unsigned long *map, unsigned int nbits)
{
	unsigned int i;
	bitmap_zero(map, nbits);
	for (i = 0; i < nbits; i++)
		map[i / BITS_PER_LONG] |= 1UL << (i % BITS_PER_LONG);
}
static inline void bitmap_copy(unsigned long *dst, const unsigned long *src,
    unsigned int nbits)
{
	memcpy(dst, src, BITS_TO_LONGS(nbits) * sizeof(long));
}
static inline void bitmap_or(unsigned long *dst, const unsigned long *a,
    const unsigned long *b, unsigned int nbits)
{
	unsigned int i;
	for (i = 0; i < BITS_TO_LONGS(nbits); i++)
		dst[i] = a[i] | b[i];
}
static inline unsigned int bitmap_weight(const unsigned long *map,
    unsigned int nbits)
{
	unsigned int i, w = 0;
	for (i = 0; i < nbits; i++)
		w += test_bit(i, map);
	return w;
}
static inline int bitmap_empty(const unsigned long *map, unsigned int nbits)
{
	return bitmap_weight(map, nbits) == 0;
}
static inline int bitmap_equal(const unsigned long *a, const unsigned long *b,
    unsigned int nbits)
{
	unsigned int i;
	for (i = 0; i < nbits; i++)
		if (test_bit(i, a) != test_bit(i, b))
			return 0;
	return 1;
}
static inline unsigned long find_next_bit(const unsigned long *map,
    unsigned long size, unsigned long off)
{
	for (; off < size; off++)
		if (test_bit(off, map))
			return off;
	return size;
}
static inline unsigned long find_next_zero_bit(const unsigned long *map,
    unsigned long size, unsigned long off)
{
	for (; off < size; off++)
		if (!test_bit(off, map))
			return off;
	return size;
}
#define find_first_bit(m, s)		find_next_bit(m, s, 0)
#define find_first_zero_bit(m, s)	find_next_zero_bit(m, s, 0)
#define for_each_set_bit(b, m, s)					\
	for ((b) = find_first_bit(m, s); (b) < (s);			\
	     (b) = find_next_bit(m, s, (b) + 1))
#define for_each_clear_bit(b, m, s)					\
	for ((b) = find_first_zero_bit(m, s); (b) < (s);		\
	     (b) = find_next_zero_bit(m, s, (b) + 1))

/* ------------------------------------------------------------------ */
/* Atomics                                                             */
/* ------------------------------------------------------------------ */

typedef struct { volatile int counter; } atomic_t;
typedef struct { volatile int64_t counter; } atomic64_t;

#define ATOMIC_INIT(v)		{ (v) }
#define atomic_read(a)		(READ_ONCE((a)->counter))
#define atomic_set(a, v)	(WRITE_ONCE((a)->counter, (v)))
#define atomic_inc(a)		atomic_inc_uint((volatile unsigned *)&(a)->counter)
#define atomic_dec(a)		atomic_dec_uint((volatile unsigned *)&(a)->counter)
#define atomic_add(v, a)	atomic_add_int((volatile unsigned *)&(a)->counter, (v))
#define atomic_sub(v, a)	atomic_add_int((volatile unsigned *)&(a)->counter, -(v))
#define atomic_inc_return(a)	((int)atomic_inc_uint_nv((volatile unsigned *)&(a)->counter))
#define atomic_dec_return(a)	((int)atomic_dec_uint_nv((volatile unsigned *)&(a)->counter))
#define atomic_dec_and_test(a)	(atomic_dec_return(a) == 0)
#define atomic_cmpxchg(a, o, n)	\
	((int)atomic_cas_uint((volatile unsigned *)&(a)->counter, (o), (n)))
#define atomic_xchg(a, n)	\
	((int)atomic_swap_uint((volatile unsigned *)&(a)->counter, (n)))
#define atomic64_read(a)	(READ_ONCE((a)->counter))
#define atomic64_set(a, v)	(WRITE_ONCE((a)->counter, (v)))
#define atomic64_inc_return(a)	\
	((int64_t)atomic_inc_64_nv((volatile uint64_t *)&(a)->counter))
#define smp_mb()		membar_sync()
#define smp_rmb()		membar_consumer()
#define smp_wmb()		membar_producer()
#define wmb()			membar_producer()
#define rmb()			membar_consumer()
#define mb()			membar_sync()
#define dma_wmb()		membar_producer()
#define dma_rmb()		membar_consumer()

/* ------------------------------------------------------------------ */
/* Memory                                                              */
/* ------------------------------------------------------------------ */

#define GFP_KERNEL	0x01
#define GFP_ATOMIC	0x02
#define GFP_NOWAIT	0x02
#define GFP_DMA		0x00
#define __GFP_ZERO	0x10
#define __GFP_NOWARN	0x00

void	*rtw89_kmalloc(size_t, gfp_t);
void	rtw89_kfree(const void *);
#define kmalloc(s, f)		rtw89_kmalloc((s), (f))
#define kzalloc(s, f)		rtw89_kmalloc((s), (f) | __GFP_ZERO)
#define kcalloc(n, s, f)	rtw89_kmalloc((n) * (s), (f) | __GFP_ZERO)
#define kmalloc_array(n, s, f)	rtw89_kmalloc((n) * (s), (f))
#define vmalloc(s)		rtw89_kmalloc((s), GFP_KERNEL)
#define vzalloc(s)		rtw89_kmalloc((s), GFP_KERNEL | __GFP_ZERO)
#define kvzalloc(s, f)		rtw89_kmalloc((s), (f) | __GFP_ZERO)
#define kfree(p)		rtw89_kfree(p)
#define vfree(p)		rtw89_kfree(p)
#define kvfree(p)		rtw89_kfree(p)
#define kfree_sensitive(p)	rtw89_kfree(p)
#define devm_kzalloc(d, s, f)	rtw89_kmalloc((s), (f) | __GFP_ZERO)
static inline void *kmemdup(const void *src, size_t len, gfp_t gfp)
{
	void *p = rtw89_kmalloc(len, gfp);
	if (p)
		memcpy(p, src, len);
	return p;
}

/* ------------------------------------------------------------------ */
/* Errors                                                              */
/* ------------------------------------------------------------------ */

#define ENOTSUPP	EOPNOTSUPP
#define EREMOTEIO	EIO
#ifndef ENODATA
#define ENODATA		ENOENT
#endif
#ifndef ENOLINK
#define ENOLINK		ENXIO
#endif
#ifndef EBADMSG
#define EBADMSG		EINVAL
#endif
#define MAX_ERRNO	4095
#define IS_ERR_VALUE(x)	((unsigned long)(void *)(x) >= (unsigned long)-MAX_ERRNO)
static inline void *ERR_PTR(long e) { return (void *)e; }
static inline long PTR_ERR(const void *p) { return (long)p; }
static inline int IS_ERR(const void *p) { return IS_ERR_VALUE(p); }
static inline int IS_ERR_OR_NULL(const void *p) { return !p || IS_ERR(p); }

/* ------------------------------------------------------------------ */
/* printk                                                              */
/* ------------------------------------------------------------------ */

void	rtw89_dev_printf(const struct device *, const char *, const char *, ...)
	    __attribute__((__format__(__printf__, 3, 4)));
void	rtw89_printk(const char *, ...)
	    __attribute__((__format__(__printf__, 1, 2)));
void	rtw89_hexdump(const char *, const void *, size_t);
#define dev_info(d, ...)	rtw89_dev_printf(d, "", __VA_ARGS__)
#define dev_warn(d, ...)	rtw89_dev_printf(d, "warning: ", __VA_ARGS__)
#define dev_err(d, ...)		rtw89_dev_printf(d, "error: ", __VA_ARGS__)
#define dev_dbg(d, ...)		do { } while (0)
#define dev_info_once		dev_info
#define dev_err_ratelimited	dev_err
#define pr_info(...)		rtw89_printk(__VA_ARGS__)
#define pr_warn(...)		rtw89_printk(__VA_ARGS__)
#define pr_err(...)		rtw89_printk(__VA_ARGS__)
#define pr_debug(...)		do { } while (0)
#define printk(...)		rtw89_printk(__VA_ARGS__)
#define KERN_INFO		""
#define KERN_DEBUG		""
#define KERN_ERR		""
#define DUMP_PREFIX_OFFSET	1
#define print_hex_dump_bytes(pfx, t, b, l)	rtw89_hexdump(pfx, b, l)

#define WARN(c, ...)	({ int _c = !!(c); if (_c) rtw89_printk(__VA_ARGS__); _c; })
#define WARN_ON(c)	({ int _c = !!(c); if (_c) rtw89_printk("rtw89: WARN_ON(%s) at %s:%d\n", #c, __func__, __LINE__); _c; })
#define WARN_ONCE	WARN
#define WARN_ON_ONCE	WARN_ON
#define BUG_ON(c)	KASSERT(!(c))
#define BUG()		panic("rtw89: BUG at %s:%d", __func__, __LINE__)
#define might_sleep()	do { } while (0)
#define lockdep_assert_held(m)	do { } while (0)
#define lockdep_assert_wiphy(w)	do { } while (0)

/* ------------------------------------------------------------------ */
/* Locks                                                               */
/* ------------------------------------------------------------------ */

struct mutex {
	kmutex_t	mtx;
};
static inline void rtw89_kmutex_init(kmutex_t *m, int ipl)
{
	mutex_init(m, MUTEX_DEFAULT, ipl);
}
static inline void rtw89_kmutex_destroy(kmutex_t *m)
{
	mutex_destroy(m);
}
#define mutex_init(m)		rtw89_kmutex_init(&(m)->mtx, IPL_NONE)
#define mutex_destroy(m)	rtw89_kmutex_destroy(&(m)->mtx)
#define mutex_lock(m)		mutex_enter(&(m)->mtx)
#define mutex_unlock(m)		mutex_exit(&(m)->mtx)
#define mutex_is_locked(m)	mutex_owned(&(m)->mtx)

typedef struct {
	kmutex_t	mtx;
} spinlock_t;
#define spin_lock_init(l)	rtw89_kmutex_init(&(l)->mtx, IPL_NET)
#define spin_lock(l)		mutex_enter(&(l)->mtx)
#define spin_unlock(l)		mutex_exit(&(l)->mtx)
#define spin_lock_bh(l)		mutex_enter(&(l)->mtx)
#define spin_unlock_bh(l)	mutex_exit(&(l)->mtx)
#define spin_lock_irq(l)	mutex_enter(&(l)->mtx)
#define spin_unlock_irq(l)	mutex_exit(&(l)->mtx)
#define spin_lock_irqsave(l, f)	do { (void)(f); mutex_enter(&(l)->mtx); } while (0)
#define spin_unlock_irqrestore(l, f) do { (void)(f); mutex_exit(&(l)->mtx); } while (0)
#define local_bh_disable()	do { } while (0)
#define local_bh_enable()	do { } while (0)

/* RCU: the driver has a single interface and station; plain accesses. */
#define rcu_read_lock()		do { } while (0)
#define rcu_read_unlock()	do { } while (0)
#define rcu_dereference(p)	(p)
#define rcu_dereference_protected(p, c)	(p)
#define rcu_access_pointer(p)	(p)
#define rcu_assign_pointer(p, v) do { membar_producer(); (p) = (v); } while (0)
#define RCU_INIT_POINTER(p, v)	((p) = (v))
#define synchronize_rcu()	do { } while (0)
struct rcu_head { void *unused; };
#define kfree_rcu(p, f)		kfree(p)

/* ------------------------------------------------------------------ */
/* Time                                                                */
/* ------------------------------------------------------------------ */

#define HZ			hz
#define jiffies			((unsigned long)getticks())
#define msecs_to_jiffies(ms)	((unsigned long)mstohz(ms))
#define usecs_to_jiffies(us)	((unsigned long)mstohz(howmany((us), 1000)))
#define jiffies_to_msecs(j)	((unsigned int)hztoms(j))
#define round_jiffies_relative(j) (j)
#define time_after(a, b)	((long)((b) - (a)) < 0)
#define time_before(a, b)	time_after(b, a)
#define time_after_eq(a, b)	((long)((a) - (b)) >= 0)
#define time_is_before_jiffies(a) time_after(jiffies, a)
#define time_is_after_jiffies(a) time_before(jiffies, a)
#define USEC_PER_SEC		1000000L
#define NSEC_PER_USEC		1000L
#define MSEC_PER_SEC		1000L

void	rtw89_msleep(unsigned int);
void	rtw89_usleep(unsigned long);
ktime_t	rtw89_ktime_get(void);
#define udelay(us)		DELAY(us)
#define ndelay(ns)		DELAY(howmany((ns), 1000))
#define mdelay(ms)		DELAY((ms) * 1000)
#define msleep(ms)		rtw89_msleep(ms)
#define usleep_range(a, b)	rtw89_usleep(a)
#define fsleep(us)		rtw89_usleep(us)
#define ktime_get()		rtw89_ktime_get()
#define ktime_get_ns()		((u64)rtw89_ktime_get())
#define ktime_get_boottime()	rtw89_ktime_get()
#define ktime_to_us(t)		((s64)(t) / 1000)
#define ktime_to_ms(t)		((s64)(t) / 1000000)
#define ktime_us_delta(a, b)	(((s64)(a) - (s64)(b)) / 1000)
#define ktime_ms_delta(a, b)	(((s64)(a) - (s64)(b)) / 1000000)
#define ktime_add_us(t, us)	((t) + (s64)(us) * 1000)
#define ktime_compare(a, b)	((a) < (b) ? -1 : (a) > (b))

/*
 * read_poll_timeout(): the elapsed time is accounted from the requested
 * sleep interval, which is close enough for register polling.
 */
#define read_poll_timeout(op, val, cond, sleep_us, timeout_us,		\
	    sleep_before_read, args...)					\
({									\
	u64 __elapsed = 0;						\
	u64 __step = (sleep_us) ? (sleep_us) : 1;			\
	int __ret = 0;							\
	if (sleep_before_read && (sleep_us))				\
		rtw89_usleep(sleep_us);					\
	for (;;) {							\
		(val) = op(args);					\
		if (cond)						\
			break;						\
		if ((timeout_us) && __elapsed >= (u64)(timeout_us)) {	\
			(val) = op(args);				\
			__ret = (cond) ? 0 : -ETIMEDOUT;		\
			break;						\
		}							\
		if (sleep_us)						\
			rtw89_usleep(sleep_us);				\
		else							\
			DELAY(1);					\
		__elapsed += __step;					\
	}								\
	__ret;								\
})
#define read_poll_timeout_atomic(op, val, cond, delay_us, timeout_us,	\
	    delay_before_read, args...)					\
({									\
	u64 __elapsed = 0;						\
	u64 __step = (delay_us) ? (delay_us) : 1;			\
	int __ret = 0;							\
	if (delay_before_read && (delay_us))				\
		DELAY(delay_us);					\
	for (;;) {							\
		(val) = op(args);					\
		if (cond)						\
			break;						\
		if ((timeout_us) && __elapsed >= (u64)(timeout_us)) {	\
			(val) = op(args);				\
			__ret = (cond) ? 0 : -ETIMEDOUT;		\
			break;						\
		}							\
		DELAY(__step);						\
		__elapsed += __step;					\
	}								\
	__ret;								\
})
#define readx_poll_timeout(op, addr, val, cond, sleep_us, timeout_us)	\
	read_poll_timeout(op, val, cond, sleep_us, timeout_us, false, addr)

/* ------------------------------------------------------------------ */
/* Lists                                                               */
/* ------------------------------------------------------------------ */

struct list_head {
	struct list_head *next, *prev;
};
#define LIST_HEAD_INIT(n)	{ &(n), &(n) }
static inline void INIT_LIST_HEAD(struct list_head *l)
{
	l->next = l->prev = l;
}
static inline void __list_add(struct list_head *n, struct list_head *p,
    struct list_head *x)
{
	x->prev = n; n->next = x; n->prev = p; p->next = n;
}
static inline void list_add(struct list_head *n, struct list_head *h)
{
	__list_add(n, h, h->next);
}
static inline void list_add_tail(struct list_head *n, struct list_head *h)
{
	__list_add(n, h->prev, h);
}
static inline void list_del(struct list_head *e)
{
	e->next->prev = e->prev;
	e->prev->next = e->next;
	e->next = e->prev = NULL;
}
static inline void list_del_init(struct list_head *e)
{
	e->next->prev = e->prev;
	e->prev->next = e->next;
	INIT_LIST_HEAD(e);
}
static inline int list_empty(const struct list_head *h)
{
	return h->next == h;
}
static inline void list_move_tail(struct list_head *e, struct list_head *h)
{
	list_del(e);
	list_add_tail(e, h);
}
static inline void list_splice_tail_init(struct list_head *l,
    struct list_head *h)
{
	if (!list_empty(l)) {
		struct list_head *first = l->next, *last = l->prev;
		first->prev = h->prev;
		h->prev->next = first;
		last->next = h;
		h->prev = last;
		INIT_LIST_HEAD(l);
	}
}
static inline size_t list_count_nodes(struct list_head *h)
{
	struct list_head *p;
	size_t n = 0;
	for (p = h->next; p != h; p = p->next)
		n++;
	return n;
}
#define list_entry(p, t, m)		container_of(p, t, m)
#define list_first_entry(h, t, m)	list_entry((h)->next, t, m)
#define list_last_entry(h, t, m)	list_entry((h)->prev, t, m)
#define list_first_entry_or_null(h, t, m) \
	(list_empty(h) ? NULL : list_first_entry(h, t, m))
#define list_next_entry(p, m)	list_entry((p)->m.next, __typeof__(*(p)), m)
#define list_for_each(p, h)	for ((p) = (h)->next; (p) != (h); (p) = (p)->next)
#define list_for_each_entry(p, h, m)					\
	for ((p) = list_entry((h)->next, __typeof__(*(p)), m);		\
	     &(p)->m != (h);						\
	     (p) = list_entry((p)->m.next, __typeof__(*(p)), m))
#define list_for_each_entry_safe(p, n, h, m)				\
	for ((p) = list_entry((h)->next, __typeof__(*(p)), m),		\
	     (n) = list_entry((p)->m.next, __typeof__(*(p)), m);	\
	     &(p)->m != (h);						\
	     (p) = (n), (n) = list_entry((n)->m.next, __typeof__(*(n)), m))
#define list_for_each_entry_rcu	list_for_each_entry

/* ------------------------------------------------------------------ */
/* Work queues and completions                                         */
/* ------------------------------------------------------------------ */

struct workqueue_struct;
struct work_struct;
typedef void (*work_func_t)(struct work_struct *);

struct work_struct {
	TAILQ_ENTRY(work_struct)	 w_entry;
	work_func_t			 func;
	struct workqueue_struct		*w_wq;
	volatile int			 w_state;
};
#define RTW89_WORK_QUEUED	0x01
#define RTW89_WORK_RUNNING	0x02

struct delayed_work {
	struct work_struct	work;
	struct callout		dw_callout;
	struct workqueue_struct	*dw_wq;
	volatile int		dw_armed;
};

#define WQ_UNBOUND	0x01
#define WQ_HIGHPRI	0x02
#define WQ_MEM_RECLAIM	0x04
#define WQ_FREEZABLE	0x08

extern struct workqueue_struct *rtw89_system_wq;
#define system_wq		rtw89_system_wq

struct workqueue_struct *rtw89_alloc_workqueue(const char *);
void	rtw89_destroy_workqueue(struct workqueue_struct *);
void	rtw89_init_work(struct work_struct *, work_func_t);
void	rtw89_init_delayed_work(struct delayed_work *, work_func_t);
bool	rtw89_queue_work(struct workqueue_struct *, struct work_struct *);
bool	rtw89_queue_delayed_work(struct workqueue_struct *,
	    struct delayed_work *, unsigned long);
bool	rtw89_cancel_work(struct work_struct *, bool);
bool	rtw89_cancel_delayed_work(struct delayed_work *, bool);
void	rtw89_flush_work(struct work_struct *);
void	rtw89_flush_workqueue(struct workqueue_struct *);

#define alloc_workqueue(n, f, a, ...)	rtw89_alloc_workqueue(n)
#define alloc_ordered_workqueue(n, f, ...) rtw89_alloc_workqueue(n)
#define destroy_workqueue(w)	rtw89_destroy_workqueue(w)
#define flush_workqueue(w)	rtw89_flush_workqueue(w)
#define INIT_WORK(w, f)		rtw89_init_work((w), (f))
#define INIT_DELAYED_WORK(w, f)	rtw89_init_delayed_work((w), (f))
#define queue_work(q, w)	rtw89_queue_work((q), (w))
#define schedule_work(w)	rtw89_queue_work(rtw89_system_wq, (w))
#define queue_delayed_work(q, w, d) rtw89_queue_delayed_work((q), (w), (d))
#define schedule_delayed_work(w, d) rtw89_queue_delayed_work(rtw89_system_wq, (w), (d))
#define mod_delayed_work(q, w, d) \
	(rtw89_cancel_delayed_work((w), false), rtw89_queue_delayed_work((q), (w), (d)))
#define cancel_work(w)		rtw89_cancel_work((w), false)
#define cancel_work_sync(w)	rtw89_cancel_work((w), true)
#define cancel_delayed_work(w)	rtw89_cancel_delayed_work((w), false)
#define cancel_delayed_work_sync(w) rtw89_cancel_delayed_work((w), true)
#define flush_work(w)		rtw89_flush_work(w)
#define flush_delayed_work(w)	rtw89_flush_work(&(w)->work)
#define to_delayed_work(w)	container_of(w, struct delayed_work, work)
#define work_pending(w)		(((w)->w_state & RTW89_WORK_QUEUED) != 0)
#define delayed_work_pending(w)	((w)->dw_armed || work_pending(&(w)->work))

struct completion {
	kmutex_t	c_lock;
	kcondvar_t	c_cv;
	unsigned int	c_done;
};
void	init_completion(struct completion *);
void	reinit_completion(struct completion *);
void	complete(struct completion *);
void	complete_all(struct completion *);
void	wait_for_completion(struct completion *);
unsigned long wait_for_completion_timeout(struct completion *, unsigned long);
bool	completion_done(struct completion *);

/* ------------------------------------------------------------------ */
/* Socket buffers                                                      */
/* ------------------------------------------------------------------ */

struct sk_buff {
	struct sk_buff	*next;
	struct sk_buff	*prev;
	unsigned char	*head;
	unsigned char	*data;
	unsigned char	*tail;
	unsigned char	*end;
	unsigned int	 len;
	unsigned int	 priority;
	u16		 queue_mapping;
	u16		 protocol;
	u16		 network_header;	/* offsets from head */
	u16		 transport_header;
	void		*skb_buf;	/* backing DMA pool buffer */
	char		 cb[48] __aligned(8);
};

struct sk_buff_head {
	struct sk_buff	*next;
	struct sk_buff	*prev;
	u32		 qlen;
	spinlock_t	 lock;
};

struct sk_buff *rtw89_alloc_skb(unsigned int, gfp_t);
void	rtw89_free_skb(struct sk_buff *);
struct sk_buff *rtw89_skb_copy(const struct sk_buff *, gfp_t);
#define NET_SKB_PAD		64
#define alloc_skb(s, f)		rtw89_alloc_skb((s), (f))
#define dev_alloc_skb(s)	rtw89_alloc_skb((s), GFP_ATOMIC)
#define kfree_skb(s)		rtw89_free_skb(s)
#define dev_kfree_skb(s)	rtw89_free_skb(s)
#define dev_kfree_skb_any(s)	rtw89_free_skb(s)
#define dev_kfree_skb_irq(s)	rtw89_free_skb(s)
#define consume_skb(s)		rtw89_free_skb(s)
#define skb_copy(s, f)		rtw89_skb_copy((s), (f))

static inline unsigned int skb_headroom(const struct sk_buff *skb)
{
	return skb->data - skb->head;
}
static inline int skb_tailroom(const struct sk_buff *skb)
{
	return skb->end - skb->tail;
}
static inline void skb_reserve(struct sk_buff *skb, int len)
{
	skb->data += len;
	skb->tail += len;
}
static inline void *skb_put(struct sk_buff *skb, unsigned int len)
{
	void *p = skb->tail;
	KASSERTMSG(skb->tail + len <= skb->end, "rtw89 skb_put overflow");
	skb->tail += len;
	skb->len += len;
	return p;
}
static inline void *skb_put_data(struct sk_buff *skb, const void *d,
    unsigned int len)
{
	void *p = skb_put(skb, len);
	memcpy(p, d, len);
	return p;
}
static inline void *skb_put_zero(struct sk_buff *skb, unsigned int len)
{
	void *p = skb_put(skb, len);
	memset(p, 0, len);
	return p;
}
static inline void *skb_push(struct sk_buff *skb, unsigned int len)
{
	KASSERTMSG(skb->data - len >= skb->head, "rtw89 skb_push underflow");
	skb->data -= len;
	skb->len += len;
	return skb->data;
}
static inline void *skb_pull(struct sk_buff *skb, unsigned int len)
{
	if (len > skb->len)
		return NULL;
	skb->len -= len;
	skb->data += len;
	return skb->data;
}
static inline void skb_trim(struct sk_buff *skb, unsigned int len)
{
	if (skb->len > len) {
		skb->len = len;
		skb->tail = skb->data + len;
	}
}
static inline void skb_set_queue_mapping(struct sk_buff *skb, u16 q)
{
	skb->queue_mapping = q;
}
static inline u16 skb_get_queue_mapping(const struct sk_buff *skb)
{
	return skb->queue_mapping;
}
static inline unsigned char *skb_tail_pointer(const struct sk_buff *skb)
{
	return skb->tail;
}
int	rtw89_pskb_expand_head(struct sk_buff *, int, int, gfp_t);
#define pskb_expand_head(s, h, t, f)	rtw89_pskb_expand_head((s), (h), (t), (f))

static inline unsigned char *skb_network_header(const struct sk_buff *skb)
{
	return skb->head + skb->network_header;
}
static inline unsigned char *skb_transport_header(const struct sk_buff *skb)
{
	return skb->head + skb->transport_header;
}
static inline int skb_linearize(struct sk_buff *skb)
{
	return 0;
}

static inline void skb_queue_head_init(struct sk_buff_head *q)
{
	q->next = q->prev = (struct sk_buff *)q;
	q->qlen = 0;
	spin_lock_init(&q->lock);
}
static inline void __skb_queue_head_init(struct sk_buff_head *q)
{
	q->next = q->prev = (struct sk_buff *)q;
	q->qlen = 0;
}
static inline void __skb_insert(struct sk_buff *n, struct sk_buff *p,
    struct sk_buff *x, struct sk_buff_head *q)
{
	n->next = x; n->prev = p; x->prev = n; p->next = n;
	q->qlen++;
}
static inline void __skb_queue_tail(struct sk_buff_head *q, struct sk_buff *n)
{
	__skb_insert(n, q->prev, (struct sk_buff *)q, q);
}
static inline void __skb_queue_head(struct sk_buff_head *q, struct sk_buff *n)
{
	__skb_insert(n, (struct sk_buff *)q, q->next, q);
}
static inline void __skb_unlink(struct sk_buff *s, struct sk_buff_head *q)
{
	q->qlen--;
	s->next->prev = s->prev;
	s->prev->next = s->next;
	s->next = s->prev = NULL;
}
static inline struct sk_buff *__skb_dequeue(struct sk_buff_head *q)
{
	struct sk_buff *s = q->next;
	if (s == (struct sk_buff *)q)
		return NULL;
	__skb_unlink(s, q);
	return s;
}
static inline void skb_queue_tail(struct sk_buff_head *q, struct sk_buff *n)
{
	spin_lock(&q->lock);
	__skb_queue_tail(q, n);
	spin_unlock(&q->lock);
}
static inline void skb_queue_head(struct sk_buff_head *q, struct sk_buff *n)
{
	spin_lock(&q->lock);
	__skb_queue_head(q, n);
	spin_unlock(&q->lock);
}
static inline void skb_unlink(struct sk_buff *s, struct sk_buff_head *q)
{
	spin_lock(&q->lock);
	__skb_unlink(s, q);
	spin_unlock(&q->lock);
}
static inline struct sk_buff *skb_dequeue(struct sk_buff_head *q)
{
	struct sk_buff *s;
	spin_lock(&q->lock);
	s = __skb_dequeue(q);
	spin_unlock(&q->lock);
	return s;
}
static inline u32 skb_queue_len(const struct sk_buff_head *q)
{
	return q->qlen;
}
static inline int skb_queue_empty(const struct sk_buff_head *q)
{
	return q->next == (const struct sk_buff *)q;
}
static inline struct sk_buff *skb_peek(const struct sk_buff_head *q)
{
	struct sk_buff *s = q->next;
	return s == (const struct sk_buff *)q ? NULL : s;
}
static inline void skb_queue_purge(struct sk_buff_head *q)
{
	struct sk_buff *s;
	while ((s = skb_dequeue(q)) != NULL)
		rtw89_free_skb(s);
}
static inline void __skb_queue_purge(struct sk_buff_head *q)
{
	struct sk_buff *s;
	while ((s = __skb_dequeue(q)) != NULL)
		rtw89_free_skb(s);
}
static inline void skb_queue_splice_init(struct sk_buff_head *l,
    struct sk_buff_head *h)
{
	struct sk_buff *s;
	while ((s = __skb_dequeue(l)) != NULL)
		__skb_queue_head(h, s);
}
static inline void skb_queue_splice_tail_init(struct sk_buff_head *l,
    struct sk_buff_head *h)
{
	struct sk_buff *s;
	while ((s = __skb_dequeue(l)) != NULL)
		__skb_queue_tail(h, s);
}
#define skb_queue_walk(q, s)						\
	for ((s) = (q)->next; (s) != (struct sk_buff *)(q); (s) = (s)->next)
#define skb_queue_walk_safe(q, s, t)					\
	for ((s) = (q)->next, (t) = (s)->next;				\
	     (s) != (struct sk_buff *)(q);				\
	     (s) = (t), (t) = (s)->next)

/* ------------------------------------------------------------------ */
/* Ethernet                                                            */
/* ------------------------------------------------------------------ */

#define ETH_ALEN	6
#define ETH_HLEN	14
#define ETH_P_IP	0x0800
#define ETH_P_ARP	0x0806
#define ETH_P_PAE	0x888E
#define ETH_P_IPV6	0x86DD

static inline void ether_addr_copy(u8 *dst, const u8 *src)
{
	memcpy(dst, src, ETH_ALEN);
}
static inline bool ether_addr_equal(const u8 *a, const u8 *b)
{
	return memcmp(a, b, ETH_ALEN) == 0;
}
static inline bool is_zero_ether_addr(const u8 *a)
{
	return (a[0] | a[1] | a[2] | a[3] | a[4] | a[5]) == 0;
}
static inline bool is_broadcast_ether_addr(const u8 *a)
{
	return (a[0] & a[1] & a[2] & a[3] & a[4] & a[5]) == 0xff;
}
static inline bool is_multicast_ether_addr(const u8 *a)
{
	return (a[0] & 0x01) != 0;
}
static inline bool is_valid_ether_addr(const u8 *a)
{
	return !is_multicast_ether_addr(a) && !is_zero_ether_addr(a);
}
static inline void eth_zero_addr(u8 *a)
{
	memset(a, 0, ETH_ALEN);
}
static inline void eth_broadcast_addr(u8 *a)
{
	memset(a, 0xff, ETH_ALEN);
}

struct ethhdr {
	u8	h_dest[ETH_ALEN];
	u8	h_source[ETH_ALEN];
	__be16	h_proto;
} __packed;

struct iphdr {
	u8	ihl:4, version:4;
	u8	tos;
	__be16	tot_len;
	__be16	id;
	__be16	frag_off;
	u8	ttl;
	u8	protocol;
	u16	check;
	__be32	saddr;
	__be32	daddr;
};
struct udphdr {
	__be16	source;
	__be16	dest;
	__be16	len;
	u16	check;
};
#define ip_hdr(skb)	((struct iphdr *)skb_network_header(skb))
#define udp_hdr(skb)	((struct udphdr *)skb_transport_header(skb))
#define IPPROTO_UDP	17
#define IPPROTO_ICMP	1

/* ------------------------------------------------------------------ */
/* DMA                                                                 */
/* ------------------------------------------------------------------ */

enum dma_data_direction {
	DMA_BIDIRECTIONAL = 0,
	DMA_TO_DEVICE = 1,
	DMA_FROM_DEVICE = 2,
	DMA_NONE = 3,
};
#define DMA_MAPPING_ERROR	(~(dma_addr_t)0)
#define DMA_BIT_MASK(n)		(((n) == 64) ? ~0ULL : ((1ULL << (n)) - 1))

void	*rtw89_dma_alloc_coherent(size_t, dma_addr_t *);
void	rtw89_dma_free_coherent(size_t, void *, dma_addr_t);
dma_addr_t rtw89_dma_map_single(void *, size_t);
#define dma_alloc_coherent(d, s, a, f)	rtw89_dma_alloc_coherent((s), (a))
#define dma_free_coherent(d, s, v, a)	rtw89_dma_free_coherent((s), (v), (a))
#define dma_map_single(d, p, s, dir)	rtw89_dma_map_single((p), (s))
#define dma_unmap_single(d, a, s, dir)	do { membar_sync(); } while (0)
#define dma_mapping_error(d, a)		((a) == DMA_MAPPING_ERROR)
#define dma_sync_single_for_cpu(d, a, s, dir)	membar_sync()
#define dma_sync_single_for_device(d, a, s, dir) membar_sync()
#define dma_set_mask(d, m)		0
#define dma_set_coherent_mask(d, m)	0

/* ------------------------------------------------------------------ */
/* MMIO                                                                */
/* ------------------------------------------------------------------ */

static inline u8 readb(const volatile void *a)
{
	u8 v = *(const volatile u8 *)a;
	membar_consumer();
	return v;
}
static inline u16 readw(const volatile void *a)
{
	u16 v = le16toh(*(const volatile u16 *)a);
	membar_consumer();
	return v;
}
static inline u32 readl(const volatile void *a)
{
	u32 v = le32toh(*(const volatile u32 *)a);
	membar_consumer();
	return v;
}
static inline void writeb(u8 v, volatile void *a)
{
	membar_producer();
	*(volatile u8 *)a = v;
}
static inline void writew(u16 v, volatile void *a)
{
	membar_producer();
	*(volatile u16 *)a = htole16(v);
}
static inline void writel(u32 v, volatile void *a)
{
	membar_producer();
	*(volatile u32 *)a = htole32(v);
}

/* ------------------------------------------------------------------ */
/* PCI                                                                 */
/* ------------------------------------------------------------------ */

typedef irqreturn_t (*irq_handler_t)(int, void *);
#define IRQ_NONE		0
#define IRQ_HANDLED		1
#define IRQ_WAKE_THREAD		2
#define IRQF_SHARED		0x80
#define PCI_IRQ_LEGACY		0x01
#define PCI_IRQ_MSI		0x02
#define PCI_IRQ_MSIX		0x04
#define PCI_VENDOR_ID_REALTEK	0x10ec

struct pci_dev {
	struct device	*dev;		/* NetBSD device_t, see dev_of() */
	void		*nb_priv;	/* NetBSD glue context */
	void		*drvdata;
	void __iomem	*mmio;		/* linear mapping of BAR 2 */
	size_t		 mmio_size;
	int		 irq;
	irq_handler_t	 irq_handler;
	irq_handler_t	 irq_thread;
	void		*irq_arg;
};

struct pci_device_id {
	u32		vendor;
	u32		device;
	u32		subvendor;
	u32		subdevice;
	kernel_ulong_t	driver_data;
};
#define PCI_DEVICE(v, d)	.vendor = (v), .device = (d)

/* Implemented by the NetBSD glue, see rtw89_nb.h. */
int	rtw89_nb_pci_cfg_read(struct device *, int, int, uint32_t *);
int	rtw89_nb_pci_cfg_write(struct device *, int, int, uint32_t);
int	rtw89_nb_pci_capability(struct device *, int, bool);
#define rtw89_pci_cfg_read(p, o, s, v)	rtw89_nb_pci_cfg_read((p)->dev, (o), (s), (v))
#define rtw89_pci_cfg_write(p, o, s, v)	rtw89_nb_pci_cfg_write((p)->dev, (o), (s), (v))
#define rtw89_pcie_cap_offset(p)	rtw89_nb_pci_capability((p)->dev, 0x10, false)
#define rtw89_pci_find_ext_capability(p, c) rtw89_nb_pci_capability((p)->dev, (c), true)

static inline int pci_read_config_byte(struct pci_dev *p, int o, u8 *v)
{
	u32 x; int r = rtw89_pci_cfg_read(p, o, 1, &x); *v = x; return r;
}
static inline int pci_read_config_word(struct pci_dev *p, int o, u16 *v)
{
	u32 x; int r = rtw89_pci_cfg_read(p, o, 2, &x); *v = x; return r;
}
static inline int pci_read_config_dword(struct pci_dev *p, int o, u32 *v)
{
	return rtw89_pci_cfg_read(p, o, 4, v);
}
static inline int pci_write_config_byte(struct pci_dev *p, int o, u8 v)
{
	return rtw89_pci_cfg_write(p, o, 1, v);
}
static inline int pci_write_config_word(struct pci_dev *p, int o, u16 v)
{
	return rtw89_pci_cfg_write(p, o, 2, v);
}
static inline int pci_write_config_dword(struct pci_dev *p, int o, u32 v)
{
	return rtw89_pci_cfg_write(p, o, 4, v);
}
static inline int pcie_capability_read_word(struct pci_dev *p, int o, u16 *v)
{
	int c = rtw89_pcie_cap_offset(p);
	*v = 0;
	if (c == 0)
		return -EIO;
	return pci_read_config_word(p, c + o, v);
}
#define PCI_EXP_LNKCTL			0x10
#define PCI_EXP_LNKCTL_ASPM_L0S		0x0001
#define PCI_EXP_LNKCTL_ASPM_L1		0x0002
#define PCI_EXP_LNKCTL_CLKREQ_EN	0x0100
#define PCI_EXT_CAP_ID_L1SS		0x1e
#define PCI_L1SS_CTL1			0x08
#define PCI_L1SS_CTL1_L1SS_MASK		0x0000000f
#define pci_find_ext_capability(p, c)	rtw89_pci_find_ext_capability((p), (c))
#define pci_enable_device(p)		0
#define pci_disable_device(p)		do { } while (0)
#define pci_set_master(p)		do { } while (0)
#define pci_set_drvdata(p, d)		((p)->drvdata = (d))
#define pci_get_drvdata(p)		((p)->drvdata)
#define pci_request_regions(p, n)	0
#define pci_release_regions(p)		do { } while (0)
#define pci_resource_len(p, b)		((p)->mmio_size)
#define pci_iomap(p, b, l)		((p)->mmio)
#define pci_iounmap(p, a)		do { } while (0)
#define pci_alloc_irq_vectors(p, a, b, f) 1
#define pci_free_irq_vectors(p)		do { } while (0)
/* Implemented by the NetBSD glue; dev is the NetBSD device_t. */
int	rtw89_nb_request_irq(struct device *, irq_handler_t, irq_handler_t,
	    void *);
void	rtw89_nb_free_irq(struct device *);
void	rtw89_nb_synchronize_irq(struct device *);
#define devm_request_threaded_irq(d, irq, h, t, f, n, a) \
	rtw89_nb_request_irq((d), (h), (t), (a))
#define devm_free_irq(d, irq, a)	rtw89_nb_free_irq(d)
/* pci.c always has the local "pdev" in scope at its call sites. */
#define synchronize_irq(irq)		rtw89_nb_synchronize_irq(pdev->dev)

struct pci_driver {
	const char *name;
	const struct pci_device_id *id_table;
	int (*probe)(struct pci_dev *, const struct pci_device_id *);
	void (*remove)(struct pci_dev *);
	struct {
		const void *pm;
	} driver;
};
struct dev_pm_ops {
	int unused;
};

/* ------------------------------------------------------------------ */
/* Firmware                                                            */
/* ------------------------------------------------------------------ */

struct firmware {
	size_t		 size;
	const u8	*data;
};
int	rtw89_request_firmware(const struct firmware **, const char *,
	    struct device *);
void	rtw89_release_firmware(const struct firmware *);
#define request_firmware(f, n, d)	rtw89_request_firmware((f), (n), (d))
#define firmware_request_nowarn(f, n, d) rtw89_request_firmware((f), (n), (d))
#define release_firmware(f)		rtw89_release_firmware(f)

struct seq_file;
#define seq_printf(m, ...)	do { (void)(m); } while (0)
#define seq_puts(m, s)		do { (void)(m); } while (0)
#define dev_coredumpv(d, b, l, f) rtw89_kfree(b)

static inline void *memchr_inv(const void *p, int c, size_t n)
{
	const u8 *b = p;
	size_t i;
	for (i = 0; i < n; i++)
		if (b[i] != (u8)c)
			return __UNCONST(&b[i]);
	return NULL;
}

/* ------------------------------------------------------------------ */
/* Misc                                                                */
/* ------------------------------------------------------------------ */

#define EXPORT_SYMBOL(s)
#define EXPORT_SYMBOL_GPL(s)
#define MODULE_AUTHOR(s)
#define MODULE_DESCRIPTION(s)
#define MODULE_LICENSE(s)
#define MODULE_FIRMWARE(s)
#define MODULE_DEVICE_TABLE(t, n)
#define MODULE_PARM_DESC(n, d)
#define module_param_named(n, v, t, p)
#define module_pci_driver(d)
#define KBUILD_MODNAME		"rtw89"
#define SIMPLE_DEV_PM_OPS(n, s, r) const struct dev_pm_ops n = { 0 }
#define dev_get_drvdata(d)	NULL
#define device_set_wakeup_enable(d, e)	do { } while (0)
#define kstrtobool(s, r)	(-EINVAL)

struct device;
const char *rtw89_dev_name(const struct device *);
#define dev_name(d)		rtw89_dev_name(d)

static inline size_t strscpy(char *d, const char *s, size_t n)
{
	return strlcpy(d, s, n);
}

#define get_random_u32()	cprng_fast32()
uint32_t cprng_fast32(void);

#endif /* _RTW89_COMPAT_H_ */
