/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD.
 *
 * Statically initialised kernel objects of the Linux 6.18 sources:
 * DEFINE_MUTEX(x), DEFINE_SPINLOCK(x), DEFINE_XARRAY_ALLOC(x) and
 * DEFINE_STATIC_SRCU(x).  A NetBSD kmutex_t or a spin lock cannot be
 * initialised by a constant, and dist618 is not edited, so each definition
 * also defines two functions with external linkage, drm618_static_init_x()
 * and drm618_static_fini_x(), in the same file as the (usually static)
 * object.  drm618_static_init() (drm618_statics.c) calls the init function
 * of every object of the core, once, before the core is used;
 * drm618_static_fini() undoes it.  The list in drm618_statics.c is checked
 * against dist618 by tools/check-statics.sh (the names are the objects',
 * which are unique in the core).
 *
 * File scope only, as in the core.  Link sets (the way the 5.6 core finds
 * its module parameters) are not used: a loadable module cannot resolve
 * the __start_/__stop_ symbols of its own sections.
 */

#ifndef _DRM618_STATICS_H_
#define _DRM618_STATICS_H_

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>

#define	DRM618_STATIC_OBJECT(NAME, DECL, INIT, FINI)			\
	DECL;								\
	void drm618_static_init_##NAME(void);				\
	void drm618_static_fini_##NAME(void);				\
	void drm618_static_init_##NAME(void) { INIT; }			\
	void drm618_static_fini_##NAME(void) { FINI; }			\
	struct __drm618_static_##NAME

#define	DEFINE_MUTEX(NAME)						\
	DRM618_STATIC_OBJECT(NAME, struct mutex NAME,			\
	    linux_mutex_init(&NAME), linux_mutex_destroy(&NAME))

#define	DEFINE_SPINLOCK(NAME)						\
	DRM618_STATIC_OBJECT(NAME, spinlock_t NAME,			\
	    spin_lock_init(&NAME), spin_lock_destroy(&NAME))

#define	DEFINE_XARRAY_ALLOC(NAME)					\
	DRM618_STATIC_OBJECT(NAME, struct xarray NAME,			\
	    xa_init_flags(&NAME, XA_FLAGS_ALLOC), xa_destroy(&NAME))

#define	DEFINE_STATIC_SRCU(NAME)					\
	DRM618_STATIC_OBJECT(NAME, struct srcu_struct NAME,		\
	    init_srcu_struct(&NAME), cleanup_srcu_struct(&NAME))

void drm618_static_init(void);
void drm618_static_fini(void);

#endif	/* _DRM618_STATICS_H_ */
