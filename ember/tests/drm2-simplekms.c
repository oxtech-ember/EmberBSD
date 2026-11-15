/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Kernel-side check of drm_simple_encoder_init of the 6.18 core
 * (drm618/drm618_simple_kms.c), run inside drmkms618_test: the encoder is
 * registered with the device and cleaned up by drm_encoder_cleanup, its type
 * is the one asked for, and the mode configuration counts it.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/device.h>
#include <sys/systm.h>

#include <linux/err.h>

#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_encoder.h>
#include <drm/drm_mode_config.h>
#include <drm/drm_mode.h>
#include <drm/drm_modeset_helper.h>
#include <drm/drm_simple_kms_helper.h>

static int t_run, t_fail;

#define	CHECK(cond)	do {						\
	t_run++;							\
	if (!(cond)) {							\
		t_fail++;						\
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);	\
	}								\
} while (0)

static const struct drm_driver t_driver = {
	.driver_features = DRIVER_GEM | DRIVER_MODESET,
	.name = __UNCONST("t-simple"), .desc = __UNCONST("simple kms test"),
	.major = 1,
};

int	drm2_simplekms_run(void);

int
drm2_simplekms_run(void)
{
	struct drm_device *dev;
	struct drm_encoder enc, enc2;
	int ret;

	t_run = t_fail = 0;
	dev = drm_dev_alloc(&t_driver, root_device);
	CHECK(!IS_ERR(dev));
	if (IS_ERR(dev))
		goto out;
	ret = drmm_mode_config_init(dev);
	CHECK(ret == 0);
	memset(&enc, 0, sizeof(enc));
	memset(&enc2, 0, sizeof(enc2));
	CHECK(dev->mode_config.num_encoder == 0);
	CHECK(drm_simple_encoder_init(dev, &enc, DRM_MODE_ENCODER_VIRTUAL) == 0);
	CHECK(enc.encoder_type == DRM_MODE_ENCODER_VIRTUAL);
	CHECK(enc.dev == dev && enc.funcs != NULL && enc.funcs->destroy != NULL);
	CHECK(dev->mode_config.num_encoder == 1);
	CHECK(drm_simple_encoder_init(dev, &enc2, DRM_MODE_ENCODER_DAC) == 0);
	CHECK(enc2.encoder_type == DRM_MODE_ENCODER_DAC);
	CHECK(drm_encoder_index(&enc2) == 1);
	CHECK(dev->mode_config.num_encoder == 2);
	enc2.funcs->destroy(&enc2);
	CHECK(dev->mode_config.num_encoder == 1);
	drm_encoder_cleanup(&enc);
	CHECK(dev->mode_config.num_encoder == 0);
	drm_dev_put(dev);
out:
	if (t_fail != 0)
		printf("drm2-simplekms: %d of %d checks FAILED\n", t_fail, t_run);
	else
		printf("drm2-simplekms: %d checks, 0 failed\n", t_run);
	return t_fail;
}
