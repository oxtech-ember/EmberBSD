/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Kernel-side checks of the static object macros of the 6.18 core
 * (drm618/include/drm618_statics.h, drm618_statics.c): the objects defined
 * with DEFINE_MUTEX & co. are usable after their init functions and
 * destroyed by their fini functions.  The table of the core
 * (drm618_statics.c) is checked by drm618/tools/check-statics.sh.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>

#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/srcu.h>
#include <linux/xarray.h>

static int t_run, t_fail;

#define	CHECK(cond)	do {						\
	t_run++;							\
	if (!(cond)) {							\
		t_fail++;						\
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);	\
	}								\
} while (0)

static DEFINE_MUTEX(t_mutex);
DEFINE_SPINLOCK(t_spin);
static DEFINE_XARRAY_ALLOC(t_xa);
static DEFINE_STATIC_SRCU(t_srcu);

int	drm2_drm618_statics_run(void);

int
drm2_drm618_statics_run(void)
{
	static int v;
	u32 id;
	int idx;

	t_run = t_fail = 0;
	drm618_static_init_t_mutex();
	drm618_static_init_t_spin();
	drm618_static_init_t_xa();
	drm618_static_init_t_srcu();

	mutex_lock(&t_mutex);
	CHECK(mutex_is_locked(&t_mutex));
	mutex_unlock(&t_mutex);
	CHECK(!mutex_is_locked(&t_mutex));

	spin_lock(&t_spin);
	spin_unlock(&t_spin);

	CHECK(xa_alloc(&t_xa, &id, &v, xa_limit_32b, GFP_KERNEL) == 0);
	CHECK(xa_load(&t_xa, id) == &v);
	xa_erase(&t_xa, id);
	CHECK(xa_load(&t_xa, id) == NULL);

	idx = srcu_read_lock(&t_srcu);
	srcu_read_unlock(&t_srcu, idx);
	synchronize_srcu(&t_srcu);

	drm618_static_fini_t_srcu();
	drm618_static_fini_t_xa();
	drm618_static_fini_t_spin();
	drm618_static_fini_t_mutex();
	printf("drm2-drm618-statics: %d checks, %d failed\n", t_run, t_fail);
	return t_fail;
}
