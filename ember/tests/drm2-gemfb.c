/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Kernel-side checks of the GEM framebuffer helpers of the 6.18 core
 * (drm618/drm618_gem_framebuffer_helper.c), run inside drmkms618_test with the
 * real core loaded: the lookup of the plane objects, the size check against
 * pitch, offset and height, the CPU access bracket.  The creation of a whole
 * framebuffer needs a registered device: the client checks of ADDFB2 do that.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/kmem.h>
#include <sys/systm.h>

#include <linux/err.h>
#include <linux/idr.h>
#include <linux/kref.h>
#include <linux/spinlock.h>

#include <drm/drm_device.h>
#include <drm/drm_file.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_mode.h>

static int t_run, t_fail, t_freed;

#define	CHECK(cond)	do {						\
	t_run++;							\
	if (!(cond)) {							\
		t_fail++;						\
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);	\
	}								\
} while (0)

static void
t_free(struct drm_gem_object *obj __unused)
{

	t_freed++;
}

static const struct drm_gem_object_funcs t_funcs = { .free = t_free };

int	drm2_gemfb_run(void);

int
drm2_gemfb_run(void)
{
	const struct drm_format_info *info = drm_format_info(DRM_FORMAT_XRGB8888);
	struct drm_device *dev;
	struct drm_file file;
	struct drm_gem_object obj;
	struct drm_framebuffer fb;
	struct drm_mode_fb_cmd2 cmd;
	struct drm_framebuffer *res;
	int id;

	t_run = t_fail = t_freed = 0;
	dev = kmem_zalloc(sizeof(*dev), KM_SLEEP);
	memset(&file, 0, sizeof(file));
	memset(&obj, 0, sizeof(obj));
	memset(&fb, 0, sizeof(fb));
	idr_init(&file.object_idr);
	spin_lock_init(&file.table_lock);
	obj.funcs = &t_funcs;
	kref_init(&obj.refcount);
	CHECK(info != NULL);

	/* the object of a plane */
	fb.obj[0] = &obj;
	CHECK(drm_gem_fb_get_obj(&fb, 0) == &obj);
	CHECK(drm_gem_fb_get_obj(&fb, 1) == NULL);
	CHECK(drm_gem_fb_get_obj(&fb, 99) == NULL);

	/* CPU access: an own object passes, an imported one is an explicit error */
	CHECK(drm_gem_fb_begin_cpu_access(&fb, DMA_FROM_DEVICE) == 0);
	drm_gem_fb_end_cpu_access(&fb, DMA_FROM_DEVICE);
	obj.import_attach = (struct dma_buf_attachment *)&obj;
	CHECK(drm_gem_fb_begin_cpu_access(&fb, DMA_FROM_DEVICE) == -EOPNOTSUPP);
	obj.import_attach = NULL;

	/* creation: the error paths that need no registered device */
	memset(&cmd, 0, sizeof(cmd));
	cmd.width = 64;
	cmd.height = 32;
	cmd.pixel_format = DRM_FORMAT_XRGB8888;
	cmd.handles[0] = 1;
	cmd.pitches[0] = 256;
	res = drm_gem_fb_create_with_funcs(dev, &file, NULL, &cmd, NULL);
	CHECK(IS_ERR(res) && PTR_ERR(res) == -EINVAL);	/* no format */
	res = drm_gem_fb_create_with_funcs(dev, &file, info, &cmd, NULL);
	CHECK(IS_ERR(res) && PTR_ERR(res) == -ENOENT);	/* no such handle */

	/* handle 1 is an object with room for 31 rows and a part of the last one */
	obj.size = 256 * 31 + 255;
	idr_preload(GFP_KERNEL);
	id = idr_alloc(&file.object_idr, &obj, 1, 0, GFP_NOWAIT);
	idr_preload_end();
	CHECK(id == 1);
	res = drm_gem_fb_create_with_funcs(dev, &file, info, &cmd, NULL);
	CHECK(IS_ERR(res) && PTR_ERR(res) == -EINVAL);	/* one byte short */
	CHECK(kref_read(&obj.refcount) == 1);		/* the lookup was given back */
	CHECK(t_freed == 0);

	/* pitch and offset from user space do not wrap the 64-bit size */
	obj.size = 1 << 20;
	cmd.pitches[0] = 0xfffffff0u;
	res = drm_gem_fb_create_with_funcs(dev, &file, info, &cmd, NULL);
	CHECK(IS_ERR(res) && PTR_ERR(res) == -EINVAL);
	cmd.pitches[0] = 256;
	cmd.offsets[0] = 0xfffffff0u;
	res = drm_gem_fb_create_with_funcs(dev, &file, info, &cmd, NULL);
	CHECK(IS_ERR(res) && PTR_ERR(res) == -EINVAL);
	CHECK(kref_read(&obj.refcount) == 1);
	CHECK(t_freed == 0);

	idr_remove(&file.object_idr, 1);
	idr_destroy(&file.object_idr);
	spin_lock_destroy(&file.table_lock);
	kmem_free(dev, sizeof(*dev));
	if (t_fail != 0)
		printf("drm2-gemfb: %d of %d checks FAILED\n", t_fail, t_run);
	else
		printf("drm2-gemfb: %d checks, 0 failed\n", t_run);
	return t_fail;
}
