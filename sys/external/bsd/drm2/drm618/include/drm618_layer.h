/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD; written to the Linux
 * interface, no Linux code.
 *
 * Linux names the 6.18 DRM core needs that the shared layer (include/linux,
 * common/include/linux) lacks and that cannot go into it, either because the
 * header is shared with other drivers (common/include) or because the name
 * is used differently by the 5.6 core.  Included by drm618_prefix.h.
 */
#ifndef _DRM618_LAYER_H_
#define _DRM618_LAYER_H_

#include <sys/param.h>
#include <sys/queue.h>

#include <linux/list.h>
#include <linux/gfp.h>
#include <linux/lockdep.h>
#include <linux/mutex.h>
#include <linux/ww_mutex.h>

/*
 * linux/list.h: the last entry of a list, or NULL when it is empty.
 */
#define	list_last_entry_or_null(ptr, type, member) ({			\
	struct list_head *__llen_head = (ptr);				\
	struct list_head *__llen_pos = __llen_head->prev;		\
	__llen_pos != __llen_head ?					\
	    list_entry(__llen_pos, type, member) : (type *)NULL;	\
})

/* linux/lockdep.h: assertions on a condition; like the layer's _held forms, off. */
#ifndef lockdep_assert
#define	lockdep_assert(cond)		do {} while (0)
/* linux/gfp.h: allocations charged to a memory cgroup; there are none here. */
#ifndef GFP_KERNEL_ACCOUNT
#define	GFP_KERNEL_ACCOUNT	GFP_KERNEL
#endif

/* linux/minmax.h: the constant-expression forms of min_t and max_t. */
#ifndef MIN_T
#define	MIN_T(type, a, b)	((type)(a) < (type)(b) ? (type)(a) : (type)(b))
#endif
#ifndef MAX_T
#define	MAX_T(type, a, b)	((type)(a) > (type)(b) ? (type)(a) : (type)(b))
#endif

/* linux/compiler_attributes.h: the statement that marks a switch fall-through. */
#ifndef fallthrough
#define	fallthrough	__attribute__((__fallthrough__))
#endif

/*
 * Memory and string helpers of linux/slab.h, linux/string.h and
 * linux/uaccess.h.
 */
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>

/* krealloc of n elements of size bytes, NULL (p kept) on overflow. */
static inline void *
krealloc_array(void *p, size_t n, size_t size, gfp_t gfp)
{

	if (size != 0 && n > SIZE_MAX / size)
		return NULL;
	return krealloc(p, n * size, gfp);
}

/* The layer's kvmalloc is kmalloc, so is the reallocation. */
static inline void *
kvrealloc(void *p, size_t size, gfp_t gfp)
{

	return krealloc(p, size, gfp);
}

/* True if all n bytes at s are zero. */
static inline bool
mem_is_zero(const void *s, size_t n)
{
	const unsigned char *p = s;

	while (n-- > 0) {
		if (*p++ != 0)
			return false;
	}
	return true;
}

/* The first c in the first count bytes of s, up to its NUL; NULL if none. */
static inline char *
strnchr(const char *s, size_t count, int c)
{

	for (; count-- > 0 && *s != '\0'; s++) {
		if (*s == (char)c)
			return (char *)__UNCONST(s);
	}
	return NULL;
}

/*
 * A copy of len bytes from user space with a NUL byte added: a kmalloc'ed
 * buffer, or ERR_PTR(-ENOMEM) and ERR_PTR(-EFAULT).
 */
static inline void *
memdup_user_nul(const void *src, size_t len)
{
	char *p;

	p = kmalloc(len + 1, GFP_KERNEL);
	if (p == NULL)
		return ERR_PTR(-ENOMEM);
	if (copy_from_user(p, src, len) != 0) {
		kfree(p);
		return ERR_PTR(-EFAULT);
	}
	p[len] = '\0';
	return p;
}

/*
 * linux/minmax.h: true if the range [start, start + size) does not fit in
 * [0, max).  Overflow-free: no sum is formed.
 */
#define	range_overflows(start, size, max) ({				\
	__typeof__(start) __ro_start = (start);				\
	__typeof__(size) __ro_size = (size);				\
	__typeof__(max) __ro_max = (max);				\
	(void)(&__ro_start == &__ro_size);				\
	(void)(&__ro_start == &__ro_max);				\
	__ro_start >= __ro_max || __ro_size > __ro_max - __ro_start;	\
})
#define	range_overflows_t(type, start, size, max)			\
	range_overflows((type)(start), (type)(size), (type)(max))

/* linux/timer.h of 6.x: new names of del_timer_sync and from_timer. */
#include <linux/timer.h>
#define	timer_delete_sync(timer)	del_timer_sync(timer)
#define	timer_container_of(var, timer, field)	from_timer(var, timer, field)

/* linux/dev_printk.h: a warning that is printed once per use. */
#define	dev_warn_once(dev, fmt, ...) do {				\
	static bool __dev_warned;					\
	if (!__dev_warned) {						\
		__dev_warned = true;					\
		dev_warn((dev), fmt, ##__VA_ARGS__);			\
	}								\
} while (0)

/*
 * Declared, not defined: they belong to the file, memory mapping and
 * scheduler parts of the port (GEM mmap, drm_vblank_work) that come with
 * their users; a call that is linked before then fails to link.
 */
struct address_space;
struct task_struct;
void	unmap_mapping_range(struct address_space *, loff_t, loff_t, int);
void	sched_set_fifo(struct task_struct *);

/*
 * mutex_init and mutex_destroy: Linux struct mutex against NetBSD kmutex_t.
 * The 5.6 sources get linux_mutex_init and linux_mutex_destroy from
 * linux/nbsd-namespace.h; here mutex_init is chosen by its number of
 * arguments (1: Linux, 3: NetBSD) and mutex_destroy by the type of its
 * argument.
 */
#undef mutex_init
#define	__drm618_MUTEX_INIT_SEL(_1, _2, _3, NAME, ...)	NAME
#define	mutex_init(...)							\
	__drm618_MUTEX_INIT_SEL(__VA_ARGS__, mutex_init,		\
	    __drm618_bad_mutex_init, linux_mutex_init)(__VA_ARGS__)
#undef mutex_destroy
#define	mutex_destroy(m)						\
	_Generic((m), struct mutex *: linux_mutex_destroy,		\
	    default: mutex_destroy)(m)

/*
 * ww_mutex_trylock: Linux 6.x takes the acquire context, 5.6 does not.
 */
#undef ww_mutex_trylock
#define	__drm618_WWTRY_SEL(_1, _2, NAME, ...)	NAME
#define	ww_mutex_trylock(...)						\
	__drm618_WWTRY_SEL(__VA_ARGS__, linux_ww_mutex_trylock_ctx,	\
	    linux_ww_mutex_trylock)(__VA_ARGS__)

#endif
#ifndef lockdep_assert_once
#define	lockdep_assert_once(cond)	do {} while (0)
/*
 * mutex_init and mutex_destroy: Linux struct mutex against NetBSD kmutex_t.
 * The 5.6 sources get linux_mutex_init and linux_mutex_destroy from
 * linux/nbsd-namespace.h; here mutex_init is chosen by its number of
 * arguments (1: Linux, 3: NetBSD) and mutex_destroy by the type of its
 * argument.
 */
#undef mutex_init
#define	__drm618_MUTEX_INIT_SEL(_1, _2, _3, NAME, ...)	NAME
#define	mutex_init(...)							\
	__drm618_MUTEX_INIT_SEL(__VA_ARGS__, mutex_init,		\
	    __drm618_bad_mutex_init, linux_mutex_init)(__VA_ARGS__)
#undef mutex_destroy
#define	mutex_destroy(m)						\
	_Generic((m), struct mutex *: linux_mutex_destroy,		\
	    default: mutex_destroy)(m)

/*
 * ww_mutex_trylock: Linux 6.x takes the acquire context, 5.6 does not.
 */
#undef ww_mutex_trylock
#define	__drm618_WWTRY_SEL(_1, _2, NAME, ...)	NAME
#define	ww_mutex_trylock(...)						\
	__drm618_WWTRY_SEL(__VA_ARGS__, linux_ww_mutex_trylock_ctx,	\
	    linux_ww_mutex_trylock)(__VA_ARGS__)

#endif

/*
 * Two names that NetBSD and Linux both define with other meanings.  The old
 * 5.6 sources map them per file with linux/nbsd-namespace.h, which has to be
 * the last include of a .c file; the 6.18 sources are not edited, so the
 * meaning is chosen by the number of arguments at the use:
 *
 *	ALIGN(p)	NetBSD, a pointer rounded up to the machine alignment
 *	ALIGN(x, a)	Linux, x rounded up to a multiple of the power of two a
 *	LIST_HEAD(name)		Linux, struct list_head name = empty list
 *	LIST_HEAD(name, type)	NetBSD, the head structure of a LIST
 *	LIST_HEAD(, type)	same, the name left out
 *
 * (sys/param.h and sys/queue.h have to be included before this header, and
 * every NetBSD header that uses the NetBSD forms keeps working.)
 */
#undef ALIGN
#define	__drm618_bsd_ALIGN(p)	(((uintptr_t)(p) + ALIGNBYTES) & ~ALIGNBYTES)
#define	__drm618_linux_ALIGN(x, a)					\
	(((x) + ((__typeof__(x))(a) - 1)) & ~((__typeof__(x))(a) - 1))
#define	__drm618_ALIGN_SEL(_1, _2, NAME, ...)	NAME
#define	ALIGN(...)							\
	__drm618_ALIGN_SEL(__VA_ARGS__, __drm618_linux_ALIGN,		\
	    __drm618_bsd_ALIGN)(__VA_ARGS__)

#undef LIST_HEAD
#define	__drm618_bsd_LIST_HEAD(name, type)				\
struct name {								\
	struct type *lh_first;	/* first element */			\
}
#define	__drm618_linux_LIST_HEAD(name)					\
	struct list_head name = LIST_HEAD_INIT(name)
#define	__drm618_LIST_HEAD_SEL(_1, _2, NAME, ...)	NAME
#define	LIST_HEAD(...)							\
	__drm618_LIST_HEAD_SEL(__VA_ARGS__, __drm618_bsd_LIST_HEAD,	\
	    __drm618_linux_LIST_HEAD)(__VA_ARGS__)

/*
 * mutex_init and mutex_destroy: Linux struct mutex against NetBSD kmutex_t.
 * The 5.6 sources get linux_mutex_init and linux_mutex_destroy from
 * linux/nbsd-namespace.h; here mutex_init is chosen by its number of
 * arguments (1: Linux, 3: NetBSD) and mutex_destroy by the type of its
 * argument.
 */
#undef mutex_init
#define	__drm618_MUTEX_INIT_SEL(_1, _2, _3, NAME, ...)	NAME
#define	mutex_init(...)							\
	__drm618_MUTEX_INIT_SEL(__VA_ARGS__, mutex_init,		\
	    __drm618_bad_mutex_init, linux_mutex_init)(__VA_ARGS__)
#undef mutex_destroy
#define	mutex_destroy(m)						\
	_Generic((m), struct mutex *: linux_mutex_destroy,		\
	    default: mutex_destroy)(m)

/*
 * ww_mutex_trylock: Linux 6.x takes the acquire context, 5.6 does not.
 */
#undef ww_mutex_trylock
#define	__drm618_WWTRY_SEL(_1, _2, NAME, ...)	NAME
#define	ww_mutex_trylock(...)						\
	__drm618_WWTRY_SEL(__VA_ARGS__, linux_ww_mutex_trylock_ctx,	\
	    linux_ww_mutex_trylock)(__VA_ARGS__)

#endif
