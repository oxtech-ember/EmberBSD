/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Kernel-side checks of the behaviour that lives in the clean-room headers of
 * the 6.18 DRM core (drm618/include/drm), run by sys/modules/drmkms_linux_test:
 * array allocation overflow, the OF-less helper, the accelerator stubs, the
 * recovery bits, the initialisers and the container_of conversions.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>

#include <linux/errno.h>
#include <linux/slab.h>

#include <drm/drm_accel.h>
#include <drm/drm_device.h>
#include <drm/drm_format_helper.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_of.h>
#include <drm/drm_writeback.h>

/* From drm_internal.h of the core (not on this module's include path). */
void drm_managed_release(struct drm_device *);

static int t_run, t_fail;

#define	CHECK(cond)	do {						\
	t_run++;							\
	if (!(cond)) {							\
		t_fail++;						\
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);	\
	}								\
} while (0)

static void
test_array_alloc(void)
{
	struct drm_device *dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	unsigned char *p;
	size_t i;

	INIT_LIST_HEAD(&dev->managed.resources);
	spin_lock_init(&dev->managed.lock);

	/* n * size overflows: no allocation, no resource. */
	CHECK(drmm_kcalloc(dev, SIZE_MAX / 2 + 1, 2, GFP_KERNEL) == NULL);
	CHECK(drmm_kmalloc_array(dev, SIZE_MAX / 4, 8, GFP_KERNEL) == NULL);
	CHECK(list_empty(&dev->managed.resources));

	p = drmm_kcalloc(dev, 7, 9, GFP_KERNEL);
	CHECK(p != NULL);
	for (i = 0; p != NULL && i < 63; i++)
		CHECK(p[i] == 0);
	p = drmm_kmalloc_array(dev, 0, 16, GFP_KERNEL);
	CHECK(!list_empty(&dev->managed.resources));
	(void)p;
	drm_managed_release(dev);
	CHECK(list_empty(&dev->managed.resources));
	spin_lock_destroy(&dev->managed.lock);
	kfree(dev);
}

static void
test_helpers(void)
{
	struct drm_format_conv_state st = DRM_FORMAT_CONV_STATE_INIT;
	char buf[16];
	struct drm_format_conv_state pre =
	    DRM_FORMAT_CONV_STATE_INIT_PREALLOCATED(buf, sizeof(buf));
	struct drm_shadow_plane_state sh;
	struct drm_writeback_connector wb;

	/* No device tree: the LVDS helper says so. */
	CHECK(drm_of_lvds_get_data_mapping(NULL) < 0);

	/* The core calls these without CONFIG_DRM_ACCEL. */
	CHECK(accel_core_init() == 0);
	accel_core_exit();
	CHECK(ACCEL_MAX_MINORS > 0);

	CHECK(st.tmp.mem == NULL && st.tmp.size == 0 && !st.tmp.preallocated);
	CHECK(pre.tmp.mem == buf && pre.tmp.size == sizeof(buf) &&
	    pre.tmp.preallocated);

	/* Each recovery method is its own single bit; none is zero. */
	CHECK(DRM_WEDGE_RECOVERY_NONE != 0);
	CHECK((DRM_WEDGE_RECOVERY_NONE & DRM_WEDGE_RECOVERY_REBIND) == 0);
	CHECK((DRM_WEDGE_RECOVERY_REBIND & DRM_WEDGE_RECOVERY_BUS_RESET) == 0);
	CHECK((DRM_WEDGE_RECOVERY_BUS_RESET & DRM_WEDGE_RECOVERY_VENDOR) == 0);
	CHECK((DRM_WEDGE_RECOVERY_NONE & (DRM_WEDGE_RECOVERY_NONE - 1)) == 0);
	CHECK((DRM_WEDGE_RECOVERY_VENDOR & (DRM_WEDGE_RECOVERY_VENDOR - 1)) == 0);

	CHECK(to_drm_shadow_plane_state(&sh.base) == &sh);
	CHECK(drm_connector_to_writeback(&wb.base) == &wb);
}

int	drm2_drm618_hdr_run(void);

int
drm2_drm618_hdr_run(void)
{

	t_run = t_fail = 0;
	test_array_alloc();
	test_helpers();
	printf("drm2-drm618-hdr: %d checks, %d failed\n", t_run, t_fail);
	return t_fail;
}
