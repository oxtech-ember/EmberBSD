/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD.
 *
 * Init and fini of the statically defined objects of the 6.18 core, see
 * include/drm618_statics.h.  The table lists them in the order of
 * initialisation (finalised in reverse); tools/check-statics.sh compares it
 * with the DEFINE_* lines of dist618, so a new object after a re-import
 * fails the check instead of being used uninitialised.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>

#include <drm618_statics.h>

/* drm618-static-objects: begin (the checker reads this block) */
#define	DRM618_STATIC_OBJECTS(X)					\
	X(drm_global_mutex)		/* drm_file.c */		\
	X(drm_minors_xa)		/* drm_drv.c */			\
	X(drm_unplug_srcu)		/* drm_drv.c */			\
	X(connector_list_lock)		/* drm_connector.c */		\
	X(bridge_lock)			/* drm_bridge.c */		\
	X(panel_lock)			/* drm_panel.c */		\
	X(drm618_io_lock)		/* drm618_page.c */
/* drm618-static-objects: end */

#define	DECLARE(NAME)							\
	void drm618_static_init_##NAME(void);				\
	void drm618_static_fini_##NAME(void);
DRM618_STATIC_OBJECTS(DECLARE)
#undef	DECLARE

void
drm618_static_init(void)
{

#define	INIT(NAME)	drm618_static_init_##NAME();
	DRM618_STATIC_OBJECTS(INIT)
#undef	INIT
}

void
drm618_static_fini(void)
{

	/* Reverse order: a list, written out in reverse. */
	drm618_static_fini_drm618_io_lock();
	drm618_static_fini_panel_lock();
	drm618_static_fini_bridge_lock();
	drm618_static_fini_connector_list_lock();
	drm618_static_fini_drm_unplug_srcu();
	drm618_static_fini_drm_minors_xa();
	drm618_static_fini_drm_global_mutex();
}
