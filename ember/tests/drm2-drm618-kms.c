/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Kernel-side checks of the small own parts of the 6.18 KMS core: leases
 * (none: drm618_lease.c), the implicit fence helper for planes
 * (drm618_gem_atomic_helper.c), and the functions that say they are not
 * available (drm618_unsupported.c).
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/kmem.h>
#include <sys/systm.h>

#include <linux/dma-fence.h>
#include <linux/dma-resv.h>
#include <linux/errno.h>
#include <linux/spinlock.h>

#include <drm/display/drm_hdmi_audio_helper.h>
#include <drm/drm_auth.h>
#include <drm/drm_connector.h>
#include <drm/drm_device.h>
#include <drm/drm_file.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_lease.h>
#include <drm/drm_plane.h>
#include <drm/drm_writeback.h>

static int t_run, t_fail;

#define	CHECK(cond)	do {						\
	t_run++;							\
	if (!(cond)) {							\
		t_fail++;						\
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);	\
	}								\
} while (0)

static const char *t_name(struct dma_fence *f) { return "t618"; }
static const struct dma_fence_ops t_fence_ops = {
	.get_driver_name = t_name,
	.get_timeline_name = t_name,
};

int	drm2_drm618_kms_run(void);

int
drm2_drm618_kms_run(void)
{
	struct drm_master m1, m2;
	struct drm_file file;
	struct drm_plane_state state;
	struct drm_framebuffer fb;
	struct drm_gem_object obj1, obj2;
	struct dma_fence f1, f2;
	spinlock_t lock;
	struct drm_device *dev;

	t_run = t_fail = 0;
	memset(&m1, 0, sizeof(m1));
	memset(&m2, 0, sizeof(m2));
	memset(&file, 0, sizeof(file));

	/* leases: none are made, every master owns what it sees */
	CHECK(drm_lease_owner(&m1) == &m1);
	m2.lessor = &m1;
	CHECK(drm_lease_owner(&m2) == &m1);
	CHECK(_drm_lease_held(&file, 3));
	CHECK(drm_lease_held(&file, 3));
	CHECK(drm_lease_filter_crtcs(&file, 0xf) == 0xf);
	drm_lease_revoke(&m1);
	drm_lease_destroy(&m1);
	dev = kmem_zalloc(sizeof(*dev), KM_SLEEP);
	CHECK(drm_mode_create_lease_ioctl(dev, NULL, &file) == -EOPNOTSUPP);
	CHECK(drm_mode_list_lessees_ioctl(dev, NULL, &file) == -EOPNOTSUPP);
	CHECK(drm_mode_get_lease_ioctl(dev, NULL, &file) == -EOPNOTSUPP);
	CHECK(drm_mode_revoke_lease_ioctl(dev, NULL, &file) == -EOPNOTSUPP);
	kmem_free(dev, sizeof(*dev));

	/* the implicit fence of a plane: from the write fences of the framebuffer objects */
	spin_lock_init(&lock);
	memset(&state, 0, sizeof(state));
	memset(&fb, 0, sizeof(fb));
	memset(&obj1, 0, sizeof(obj1));
	memset(&obj2, 0, sizeof(obj2));
	CHECK(drm_gem_plane_helper_prepare_fb(NULL, &state) == 0);	/* no framebuffer */
	CHECK(state.fence == NULL);
	dma_resv_init(&obj1._resv);
	obj1.resv = &obj1._resv;
	dma_resv_init(&obj2._resv);
	obj2.resv = &obj2._resv;
	fb.obj[0] = &obj1;
	state.fb = &fb;
	CHECK(drm_gem_plane_helper_prepare_fb(NULL, &state) == 0);	/* idle object: no fence */
	CHECK(state.fence == NULL);
	dma_fence_init(&f1, &t_fence_ops, &lock, 1, 1);
	dma_resv_lock(obj1.resv, NULL);
	CHECK(dma_resv_reserve_fences(obj1.resv, 1) == 0);
	dma_resv_add_fence(obj1.resv, &f1, DMA_RESV_USAGE_WRITE);
	dma_resv_unlock(obj1.resv);
	CHECK(drm_gem_plane_helper_prepare_fb(NULL, &state) == 0);
	CHECK(state.fence == &f1);
	dma_fence_put(state.fence);
	state.fence = NULL;
	/* an explicit fence set by user space is kept */
	state.fence = dma_fence_get(&f1);
	CHECK(drm_gem_plane_helper_prepare_fb(NULL, &state) == 0);
	CHECK(state.fence == &f1);
	dma_fence_put(state.fence);
	state.fence = NULL;
	/* two objects: both fences are waited for */
	dma_fence_init(&f2, &t_fence_ops, &lock, 2, 1);
	dma_resv_lock(obj2.resv, NULL);
	CHECK(dma_resv_reserve_fences(obj2.resv, 1) == 0);
	dma_resv_add_fence(obj2.resv, &f2, DMA_RESV_USAGE_WRITE);
	dma_resv_unlock(obj2.resv);
	fb.obj[1] = &obj2;
	CHECK(drm_gem_plane_helper_prepare_fb(NULL, &state) == 0);
	CHECK(state.fence != NULL && state.fence != &f1 && state.fence != &f2);
	/* waiting enables signalling of the merged (array) fence */
	CHECK(dma_fence_wait_timeout(state.fence, false, hz / 20 + 1) == 0);
	dma_fence_signal(&f1);
	CHECK(dma_fence_wait_timeout(state.fence, false, hz / 20 + 1) == 0);
	dma_fence_signal(&f2);
	CHECK(dma_fence_wait_timeout(state.fence, false, hz) > 0);
	dma_fence_put(state.fence);
	dma_resv_fini(&obj1._resv);
	dma_resv_fini(&obj2._resv);

	/* what is not available says so */
	drm_connector_hdmi_audio_plugged_notify(NULL, true);
	CHECK(drm_writeback_prepare_job(NULL) == -ENODEV);
	CHECK(drm_writeback_get_out_fence(NULL) == NULL);
	drm_writeback_cleanup_job(NULL);

	if (t_fail != 0)
		printf("drm2-drm618-kms: %d of %d checks FAILED\n", t_fail, t_run);
	else
		printf("drm2-drm618-kms: %d checks, 0 failed\n", t_run);
	return t_fail;
}
