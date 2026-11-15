/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: the uses of the functions below in the MIT files of
 * dist618/drm/i915 (their arguments, results and what the caller does with them); the documented
 * behaviour of the Linux kernel string and bit helpers (kstrto*: optional sign, base prefix, one
 * trailing newline, EINVAL and ERANGE); NetBSD sys/systm.h, sys/bitops.h and sys/random.h; the GPL original was not read.
 *
 * Small Linux helpers the i915 driver of 6.18 uses and the layer lacks.  They
 * belong to Linux headers the sources name themselves (linux/kstrtox.h,
 * linux/string.h, linux/bitmap.h, ...); written out here once and read by the
 * i915 prefix header and the tests, so the other 6.18 sources see no change.
 */
#ifndef _I915DRM618_COMPAT_H_
#define _I915DRM618_COMPAT_H_

#include <sys/param.h>
#include <sys/cpu.h>
#include <sys/kernel.h>
#include <sys/systm.h>

#include <machine/cpufunc.h>

#include <linux/atomic.h>
#include <linux/bitmap.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/gfp.h>
#include <linux/jiffies.h>
#include <linux/ktime.h>
#include <linux/limits.h>
#include <linux/list.h>
#include <linux/random.h>
#include <linux/ratelimit.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/time64.h>

/* ---- kstrtox ---- */

static inline int
kstrtoull(const char *s, unsigned int base, unsigned long long *res)
{
	unsigned long long v = 0;
	unsigned int d;
	bool any = false;
	char c;

	if (*s == '+')
		s++;
	if (base == 0) {
		if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X') &&
		    isxdigit((unsigned char)s[2])) {
			base = 16;
			s += 2;
		} else if (s[0] == '0' && s[1] != '\0' && s[1] != '\n') {
			base = 8;
			s++;
		} else {
			base = 10;
		}
	} else if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X') &&
	    isxdigit((unsigned char)s[2])) {
		s += 2;
	}
	for (; (c = *s) != '\0'; s++) {
		if (c >= '0' && c <= '9')
			d = c - '0';
		else if (c >= 'a' && c <= 'f')
			d = c - 'a' + 10;
		else if (c >= 'A' && c <= 'F')
			d = c - 'A' + 10;
		else
			break;
		if (d >= base)
			break;
		if (v > (ULLONG_MAX - d) / base)
			return -ERANGE;
		v = v * base + d;
		any = true;
	}
	if (!any)
		return -EINVAL;
	if (*s == '\n')
		s++;
	if (*s != '\0')
		return -EINVAL;
	*res = v;
	return 0;
}

static inline int
kstrtoll(const char *s, unsigned int base, long long *res)
{
	unsigned long long v;
	int error;

	if (*s == '-') {
		error = kstrtoull(s + 1, base, &v);
		if (error != 0)
			return error;
		if (v > (unsigned long long)LLONG_MAX + 1)
			return -ERANGE;
		*res = -(long long)v;
		return 0;
	}
	error = kstrtoull(s, base, &v);
	if (error != 0)
		return error;
	if (v > (unsigned long long)LLONG_MAX)
		return -ERANGE;
	*res = (long long)v;
	return 0;
}

#define	__I915_KSTRTO_U(name, type, max)				\
static inline int							\
name(const char *s, unsigned int base, type *res)			\
{									\
	unsigned long long v;						\
	int error = kstrtoull(s, base, &v);				\
									\
	if (error != 0)							\
		return error;						\
	if (v > (max))							\
		return -ERANGE;						\
	*res = (type)v;							\
	return 0;							\
}
__I915_KSTRTO_U(kstrtoul, unsigned long, ULONG_MAX)
__I915_KSTRTO_U(kstrtouint, unsigned int, UINT_MAX)
__I915_KSTRTO_U(kstrtou32, uint32_t, UINT32_MAX)
__I915_KSTRTO_U(kstrtou16, uint16_t, UINT16_MAX)
__I915_KSTRTO_U(kstrtou8, uint8_t, UINT8_MAX)

static inline int
kstrtoint(const char *s, unsigned int base, int *res)
{
	long long v;
	int error = kstrtoll(s, base, &v);

	if (error != 0)
		return error;
	if (v < INT_MIN || v > INT_MAX)
		return -ERANGE;
	*res = (int)v;
	return 0;
}

/* only the first characters count: y Y 1, n N 0, on, off */
static inline int
kstrtobool(const char *s, bool *res)
{

	if (s == NULL)
		return -EINVAL;
	switch (s[0]) {
	case 'y': case 'Y': case '1':
		*res = true;
		return 0;
	case 'n': case 'N': case '0':
		*res = false;
		return 0;
	case 'o': case 'O':
		switch (s[1]) {
		case 'n': case 'N':
			*res = true;
			return 0;
		case 'f': case 'F':
			*res = false;
			return 0;
		}
		break;
	}
	return -EINVAL;
}

/* the text comes from user memory: at most 15 bytes are read */
static inline int
kstrtobool_from_user(const char *ubuf, size_t count, bool *res)
{
	char buf[16];

	count = MIN(count, sizeof(buf) - 1);
	if (copyin(ubuf, buf, count) != 0)
		return -EFAULT;
	buf[count] = '\0';
	return kstrtobool(buf, res);
}

#define	__I915_KSTRTO_FROM_USER(name, call, type)			\
static inline int							\
name(const char *ubuf, size_t count, unsigned int base, type *res)	\
{									\
	char buf[33];							\
									\
	count = MIN(count, sizeof(buf) - 1);				\
	if (copyin(ubuf, buf, count) != 0)				\
		return -EFAULT;						\
	buf[count] = '\0';						\
	return call(buf, base, res);					\
}
__I915_KSTRTO_FROM_USER(kstrtoint_from_user, kstrtoint, int)
__I915_KSTRTO_FROM_USER(kstrtouint_from_user, kstrtouint, unsigned int)

/* ---- lists, bitmaps, bits ---- */

static inline unsigned long
list_count_nodes(const struct list_head *head)
{
	const struct list_head *pos;
	unsigned long n = 0;

	for (pos = head->next; pos != head; pos = pos->next)
		n++;
	return n;
}

/* the RCU forms: writers are serialised by the caller; the stores are ordered for lockless readers */
static inline void
list_add_tail_rcu(struct list_head *entry, struct list_head *head)
{

	entry->next = head;
	entry->prev = head->prev;
	membar_producer();
	head->prev->next = entry;
	head->prev = entry;
}

/* entry->next stays valid for a reader that stands on it; the entry is not poisoned */
static inline void
list_del_rcu(struct list_head *entry)
{

	entry->next->prev = entry->prev;
	entry->prev->next = entry->next;
}

#define	list_for_each_entry_rcu(pos, head, member, ...)			\
	for (pos = list_entry(*(struct list_head * volatile *)&(head)->next,\
	    __typeof__(*pos), member);					\
	    &pos->member != (head);					\
	    pos = list_entry(*(struct list_head * volatile *)&pos->member.next,\
	    __typeof__(*pos), member))

static inline void
bitmap_fill(unsigned long *dst, unsigned int nbits)
{
	unsigned int n = BITS_TO_LONGS(nbits), i;

	for (i = 0; i < n; i++)
		dst[i] = ~0ul;
	if (nbits % BITS_PER_LONG != 0)
		dst[n - 1] = (1ul << (nbits % BITS_PER_LONG)) - 1;
}

/* every bit of A is also in B */
static inline bool
bitmap_subset(const unsigned long *a, const unsigned long *b,
    unsigned int nbits)
{
	unsigned int i, full = nbits / BITS_PER_LONG;

	for (i = 0; i < full; i++)
		if (a[i] & ~b[i])
			return false;
	if (nbits % BITS_PER_LONG != 0 &&
	    ((a[full] & ~b[full]) & ((1ul << (nbits % BITS_PER_LONG)) - 1)))
		return false;
	return true;
}

static inline bool
bitmap_intersects(const unsigned long *a, const unsigned long *b,
    unsigned int nbits)
{
	unsigned int i, full = nbits / BITS_PER_LONG;

	for (i = 0; i < full; i++)
		if (a[i] & b[i])
			return true;
	return nbits % BITS_PER_LONG != 0 &&
	    ((a[full] & b[full]) & ((1ul << (nbits % BITS_PER_LONG)) - 1)) != 0;
}

#define	__assign_bit(nr, addr, value)					\
	do {								\
		if (value)						\
			__set_bit((nr), (addr));			\
		else							\
			__clear_bit((nr), (addr));			\
	} while (0)

static inline int
atomic_sub_and_test(int i, atomic_t *v)
{

	return atomic_sub_return(i, v) == 0;
}

/* ---- numbers ---- */

static inline uint32_t
get_random_u32_below(uint32_t ceil)
{
	uint32_t v, limit;

	if (ceil == 0)
		return get_random_u32();
	limit = UINT32_MAX - UINT32_MAX % ceil;	/* no modulo bias */
	do {
		v = get_random_u32();
	} while (v >= limit && limit != 0);
	return v % ceil;
}

static inline uint64_t
get_random_u64(void)
{

	return ((uint64_t)get_random_u32() << 32) | get_random_u32();
}

static inline uint64_t
int_pow(uint64_t base, unsigned int exp)
{
	uint64_t r = 1;

	while (exp-- > 0)
		r *= base;
	return r;
}

#define	DIV64_U64_ROUND_CLOSEST(dividend, divisor)			\
	({ uint64_t __d = (divisor);					\
	   ((uint64_t)(dividend) + (__d / 2)) / __d; })

#define	PTR_ALIGN(p, a)	((__typeof__(p))roundup2((uintptr_t)(p), (a)))

static inline unsigned long
jiffies_to_nsecs(const unsigned long j)
{

	return j * (1000000000ul / hz);
}

/* signed: a deadline already passed gives a negative number */
static inline long
jiffies_delta_to_msecs(long delta)
{

	return delta * 1000 / hz;
}

#define	KTIME_MAX	((ktime_t)INT64_MAX)

static inline ktime_t
ktime_set(const int64_t secs, const unsigned long nsecs)
{

	if (secs >= KTIME_MAX / NSEC_PER_SEC)
		return KTIME_MAX;
	return secs * NSEC_PER_SEC + (int64_t)nsecs;
}

static inline ktime_t
ktime_add_ms(const ktime_t kt, const uint64_t msec)
{

	return ktime_add_ns(kt, msec * NSEC_PER_MSEC);
}

/* a sleep of USECS microseconds: short ones may be a little longer, long ones take milliseconds */
static inline void
fsleep(unsigned long usecs)
{

	if (usecs <= 20 * 1000)
		usleep_range(usecs, usecs + usecs / 2);
	else
		msleep(DIV_ROUND_UP(usecs, 1000));
}

/* ---- strings, memory ---- */

static inline char *
strchrnul(const char *s, int c)
{

	while (*s != '\0' && *s != (char)c)
		s++;
	return __UNCONST(s);
}

/* a copy of N elements of SIZE bytes; NULL if the product overflows */
static inline void *
kmemdup_array(const void *src, size_t n, size_t size, gfp_t gfp)
{
	size_t total;

	if (__builtin_mul_overflow(n, size, &total))
		return NULL;
	return kmemdup(src, total, gfp);
}

/* the string without the white space at both ends (the start moves, the end is cut in place) */
static inline char *
strim(char *s)
{
	size_t n;

	while (isspace((unsigned char)*s))
		s++;
	n = strlen(s);
	while (n > 0 && isspace((unsigned char)s[n - 1]))
		n--;
	s[n] = '\0';
	return s;
}

/* replace every OLD by NEW; returns the string */
static inline char *
strreplace(char *s, char old, char new)
{
	char *p;

	for (p = s; *p != '\0'; p++)
		if (*p == old)
			*p = new;
	return s;
}

/* ---- debugging aids, interrupts ---- */

#define	might_alloc(gfp)		((void)(gfp))
#define	lockdep_assert_not_held(l)	((void)(l))

/* kernel preemption of NetBSD never needs the lock dropped for another thread */
#define	cond_resched_lock(l)		((void)(l), 0)

/* interrupts of this CPU: the state of the flags register is the whole state */
#define	local_irq_disable()	x86_disable_intr()
#define	local_irq_enable()	x86_enable_intr()

#define	dev_err_ratelimited(dev, fmt, ...)				\
	do {								\
		static struct ratelimit_state __rs;			\
		static bool __rs_init;					\
		if (!__rs_init) {					\
			ratelimit_state_init(&__rs, 5 * hz, 10);	\
			__rs_init = true;				\
		}							\
		if (__ratelimit(&__rs))					\
			dev_err((dev), fmt, ##__VA_ARGS__);		\
	} while (0)
#define	dev_notice_ratelimited(dev, fmt, ...)				\
	do {								\
		static struct ratelimit_state __rs;			\
		static bool __rs_init;					\
		if (!__rs_init) {					\
			ratelimit_state_init(&__rs, 5 * hz, 10);	\
			__rs_init = true;				\
		}							\
		if (__ratelimit(&__rs))					\
			dev_notice((dev), fmt, ##__VA_ARGS__);		\
	} while (0)

#endif
