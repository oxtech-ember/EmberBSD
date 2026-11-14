/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Kernel-side checks of the drmm_* resources of the 6.18 DRM core
 * (drm618/drm618_managed.c), run by sys/modules/drmkms_linux_test: release
 * order, memory, freeing early, actions that register more resources while
 * the device is being released.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>

#include <linux/errno.h>
#include <linux/slab.h>

#include <drm/drm_device.h>
#include <drm/drm_managed.h>

/* From drm_internal.h of the core (not on this module's include path). */
void drm_managed_release(struct drm_device *);
void drmm_add_final_kfree(struct drm_device *, void *);

static int t_run, t_fail;

#define	CHECK(cond)	do {						\
	t_run++;							\
	if (!(cond)) {							\
		t_fail++;						\
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);	\
	}								\
} while (0)

static struct drm_device *
t_dev(void)
{
	struct drm_device *dev = kzalloc(sizeof(*dev), GFP_KERNEL);

	INIT_LIST_HEAD(&dev->managed.resources);
	spin_lock_init(&dev->managed.lock);
	return dev;
}

static void
t_dev_free(struct drm_device *dev)
{
	spin_lock_destroy(&dev->managed.lock);
	kfree(dev);
}

static int order[16];
static unsigned norder;

static void
rel(struct drm_device *dev, void *res)
{
	if (norder < 16)
		order[norder] = *(int *)res;
	norder++;
}

static void
test_order_and_memory(void)
{
	struct drm_device *dev = t_dev();
	int a = 1, b = 2, c = 3;
	char *p, *q;
	long *z;

	norder = 0;
	CHECK(drmm_add_action(dev, rel, &a) == 0);
	CHECK(drmm_add_action_or_reset(dev, rel, &b) == 0);
	p = drmm_kstrdup(dev, "managed", GFP_KERNEL);
	CHECK(p != NULL && strcmp(p, "managed") == 0);
	z = drmm_kcalloc(dev, 4, sizeof(*z), GFP_KERNEL);
	CHECK(z != NULL && z[0] == 0 && z[3] == 0);
	CHECK(((uintptr_t)z & 7) == 0);
	q = drmm_kzalloc(dev, 100, GFP_KERNEL);
	CHECK(q != NULL && q[0] == 0 && q[99] == 0);
	CHECK(drmm_add_action(dev, rel, &c) == 0);
	CHECK(drmm_kmalloc_array(dev, SIZE_MAX / 2, 4, GFP_KERNEL) == NULL);
	CHECK(drmm_kstrdup(dev, NULL, GFP_KERNEL) == NULL);

	/* Memory freed early is gone and is not freed again at release. */
	drmm_kfree(dev, q);
	drmm_kfree(dev, NULL);

	CHECK(norder == 0);
	drm_managed_release(dev);
	/* Newest first: c, b, a. */
	CHECK(norder == 3 && order[0] == 3 && order[1] == 2 && order[2] == 1);
	CHECK(list_empty(&dev->managed.resources));
	t_dev_free(dev);
}

static void
test_release_action(struct drm_device *unused)
{
	struct drm_device *dev = t_dev();
	int a = 1, b = 2, c = 3;

	norder = 0;
	CHECK(drmm_add_action(dev, rel, &a) == 0);
	CHECK(drmm_add_action(dev, rel, &b) == 0);
	CHECK(drmm_add_action(dev, rel, &c) == 0);

	/* The one in the middle runs now, only that one. */
	drmm_release_action(dev, rel, &b);
	CHECK(norder == 1 && order[0] == 2);
	drm_managed_release(dev);
	CHECK(norder == 3 && order[1] == 3 && order[2] == 1);
	t_dev_free(dev);
}

static int reent_late = 7;
static void *reent_mem;

static void
rel_registers(struct drm_device *dev, void *res)
{
	/* Registered while the device is being released: still released. */
	CHECK(drmm_add_action(dev, rel, &reent_late) == 0);
	/* Freeing a resource that has not been released yet. */
	drmm_kfree(dev, reent_mem);
	rel(dev, res);
}

static void
test_reentrant(void)
{
	struct drm_device *dev = t_dev();
	int a = 1, b = 2;

	norder = 0;
	CHECK(drmm_add_action(dev, rel, &a) == 0);
	reent_mem = drmm_kmalloc(dev, 64, GFP_KERNEL);
	CHECK(reent_mem != NULL);
	CHECK(drmm_add_action(dev, rel_registers, &b) == 0);
	drm_managed_release(dev);
	/* b, then what b registered (7), then a. */
	CHECK(norder == 3 && order[0] == 2 && order[1] == 7 && order[2] == 1);
	CHECK(list_empty(&dev->managed.resources));
	t_dev_free(dev);
}

static void
test_final_kfree_and_mutex(void)
{
	struct drm_device *dev = t_dev();
	struct mutex *lock;
	void *container = kzalloc(32, GFP_KERNEL);

	drmm_add_final_kfree(dev, container);
	lock = drmm_kzalloc(dev, sizeof(*lock), GFP_KERNEL);
	CHECK(drmm_mutex_init(dev, lock) == 0);
	mutex_lock(lock);
	mutex_unlock(lock);
	drm_managed_release(dev);
	CHECK(dev->managed.final_kfree == NULL);
	t_dev_free(dev);
}

int	drm2_drm618_managed_run(void);

int
drm2_drm618_managed_run(void)
{

	t_run = t_fail = 0;
	test_order_and_memory();
	test_release_action(NULL);
	test_reentrant();
	test_final_kfree_and_mutex();
	printf("drm2-drm618-managed: %d checks, %d failed\n", t_run, t_fail);
	return t_fail;
}
