/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Kernel-side checks of the paths of failure of the 6.18 core: an object that
 * is set up and then given up has to give its locks up as well, or a kernel
 * with LOCKDEBUG stops at the free of the memory.  Without a patch of the
 * core the checks here stop such a kernel; on any other they only check the
 * results.  Selectable with -DLOCKFIX_ONLY=n (a build for a single check).
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>

#include <linux/err.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>

#ifndef LOCKFIX_ONLY
#define	LOCKFIX_ONLY	0
#endif
#define	SEL(n)		(LOCKFIX_ONLY == 0 || LOCKFIX_ONLY == (n))

struct drm_file *drm_file_alloc(struct drm_minor *);	/* drm_internal.h of the core */

static int t_run, t_fail;

#define	CHECK(cond)	do {						\
	t_run++;							\
	if (!(cond)) {							\
		t_fail++;						\
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);	\
	}								\
} while (0)

static int
t_open_fail(struct drm_device *dev __unused, struct drm_file *file __unused)
{

	return -ENOMEM;
}

static struct drm_driver t_fail_driver = {
	.open = t_open_fail,
};

/* Both features at once are refused by drm_dev_init after its first locks are set up. */
static struct drm_driver t_conflict_driver = {
	.driver_features = DRIVER_COMPUTE_ACCEL | DRIVER_RENDER,
};

int	drm2_lockfix_run(void);

int
drm2_lockfix_run(void)
{
	t_run = t_fail = 0;

	/* 1. driver->open fails: drm_file_alloc gives the file up */
	if (SEL(1)) {
		struct drm_device dev;
		struct drm_minor minor;
		struct drm_file *file;

		memset(&dev, 0, sizeof(dev));
		memset(&minor, 0, sizeof(minor));
		dev.driver = &t_fail_driver;
		minor.dev = &dev;
		minor.type = DRM_MINOR_RENDER;
		file = drm_file_alloc(&minor);
		CHECK(IS_ERR(file) && PTR_ERR(file) == -ENOMEM);
	}

	/* 2. drm_dev_init refuses the driver after it has set up the lock of the managed resources */
	if (SEL(2)) {
		struct drm_device *dev;
		char parent[64];

		memset(parent, 0, sizeof(parent));
		dev = drm_dev_alloc(&t_conflict_driver, (struct device *)parent);
		CHECK(IS_ERR(dev));
	}

	printf("drm2-lockfix: %d checks, %d failed\n", t_run, t_fail);
	return t_fail;
}
