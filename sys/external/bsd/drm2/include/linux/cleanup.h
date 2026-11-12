/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD native implementation of the interface of Linux 6.18
 * include/linux/cleanup.h (GPL-2.0; no source copied): scope-based cleanup on
 * top of the compiler's cleanup attribute (Clang and GCC).
 *
 *   DEFINE_FREE(name, type, free_stmt)    __free(name) variables
 *   no_free_ptr(), retain_and_null_ptr(), return_ptr()
 *   DEFINE_CLASS(), CLASS()
 *   DEFINE_GUARD(), DEFINE_LOCK_GUARD_0/1(), guard(), scoped_guard()
 *
 * Not provided: conditional guards (DEFINE_GUARD_COND(), scoped_cond_guard(),
 * guard(... _try)) and lock-pointer helpers; code using them needs them added.
 * Guards for the layer's own locks are defined in <linux/mutex.h> etc.
 */

#ifndef _LINUX_CLEANUP_H_
#define _LINUX_CLEANUP_H_

#include <linux/types.h>

#ifndef __cleanup
#define	__cleanup(FN)	__attribute__((__cleanup__(FN)))
#endif

#ifndef __always_inline
#define	__always_inline	inline __attribute__((__always_inline__))
#endif

#define	__cleanup_cat_(A, B)	A##B
#define	__cleanup_cat(A, B)	__cleanup_cat_(A, B)
#define	__cleanup_uid(PREFIX)	__cleanup_cat(PREFIX, __COUNTER__)

/*
 * DEFINE_FREE(name, type, stmt): defines the cleanup function for variables
 * declared "type v __free(name) = ...".  stmt sees the value as _T.
 */
#define	DEFINE_FREE(_name, _type, _free)				\
static __always_inline void						\
__free_##_name(void *_p)						\
{									\
	_type _T = *(_type *)_p;					\
	_free;								\
}

#define	__free(_name)	__cleanup(__free_##_name)

#define	__get_and_null(P, NULLVALUE) ({					\
	__auto_type __gn_ptr = &(P);					\
	__auto_type __gn_val = *__gn_ptr;				\
	*__gn_ptr = NULLVALUE;						\
	__gn_val;							\
})

/* Take the pointer out of the variable; its cleanup then sees NULL. */
#define	no_free_ptr(P)		__get_and_null(P, NULL)
#define	retain_and_null_ptr(P)	((void)__get_and_null(P, NULL))
#define	return_ptr(P)		return no_free_ptr(P)

/*
 * DEFINE_CLASS(name, type, exit_stmt, init_expr, init_args...): a constructor
 * (init_expr, arguments init_args) and destructor (exit_stmt, value as _T).
 * CLASS(name, var)(args) declares var and destroys it at scope exit.
 */
#define	DEFINE_CLASS(_name, _type, _exit, _init, _init_args...)		\
typedef _type class_##_name##_t;					\
static __always_inline void						\
class_##_name##_destructor(class_##_name##_t *_p)			\
{									\
	class_##_name##_t _T = *_p;					\
	_exit;								\
}									\
static __always_inline class_##_name##_t				\
class_##_name##_constructor(_init_args)					\
{									\
	class_##_name##_t _t = _init;					\
	return _t;							\
}

#define	CLASS(_name, _var)						\
	class_##_name##_t _var __cleanup(class_##_name##_destructor) =	\
	    class_##_name##_constructor

/* A guard is a class whose value is the lock; lock/unlock statements use _T. */
#define	DEFINE_GUARD(_name, _type, _lock, _unlock)			\
	DEFINE_CLASS(_name, _type, if (_T) { _unlock; }, ({ _lock; _T; }), \
	    _type _T)

/*
 * Lock guards with state: the class is a struct holding the lock pointer and
 * any extra members (e.g. saved interrupt flags); lock/unlock statements use
 * _T->lock and _T->member.  The _1 variant takes the lock as its argument and
 * skips the unlock for a NULL lock; the _0 variant takes no argument.
 */
#define	DEFINE_LOCK_GUARD_1(_name, _type, _lock, _unlock, ...)		\
typedef struct {							\
	_type *lock;							\
	__VA_ARGS__;							\
} class_##_name##_t;							\
static __always_inline void						\
class_##_name##_destructor(class_##_name##_t *_T)			\
{									\
	if (_T->lock) {							\
		_unlock;						\
	}								\
}									\
static __always_inline class_##_name##_t				\
class_##_name##_constructor(_type *_l)					\
{									\
	class_##_name##_t _t = { .lock = _l }, *_T = &_t;		\
	(void)_T;							\
	_lock;								\
	return _t;							\
}

#define	DEFINE_LOCK_GUARD_0(_name, _lock, _unlock, ...)			\
typedef struct {							\
	void *lock;							\
	__VA_ARGS__;							\
} class_##_name##_t;							\
static __always_inline void						\
class_##_name##_destructor(class_##_name##_t *_T)			\
{									\
	_unlock;							\
}									\
static __always_inline class_##_name##_t				\
class_##_name##_constructor(void)					\
{									\
	class_##_name##_t _t = { .lock = NULL }, *_T = &_t;		\
	(void)_T;							\
	_lock;								\
	return _t;							\
}

#define	guard(_name)	CLASS(_name, __cleanup_uid(__guard_))

/*
 * scoped_guard(name, args...) statement: runs the statement once with the
 * guard held; break, continue-free exits and return all release it.
 */
#define	__scoped_guard(_name, _scope, _done, ...)			\
	for (CLASS(_name, _scope)(__VA_ARGS__), *_done = NULL;		\
	    _done == NULL; _done = (void *)1)
#define	scoped_guard(_name, ...)					\
	__scoped_guard(_name, __cleanup_uid(__sg_scope_),		\
	    __cleanup_uid(__sg_done_), ##__VA_ARGS__)

#endif	/* _LINUX_CLEANUP_H_ */
