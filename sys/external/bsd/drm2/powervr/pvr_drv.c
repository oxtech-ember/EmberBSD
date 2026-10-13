/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native PowerVR identification and DRM registration. */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/kmem.h>
#include <sys/systm.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_ioctl.h>
#include "pvr_drm.h"
#include "pvr_drv.h"
#include "pvr_heap.h"

/* Mesa 26.2.4 BXM-4-64 profile: one cluster, one phantom. */
#define PVR_A733_BVNC	UINT64_C(0x00240038006800b7)

struct pvr_device {
	uint64_t bvnc;
};

static int
pvr_dev_query(struct drm_device *dev, void *data, struct drm_file *file)
{
	const struct pvr_device *sc = dev->dev_private;
	struct drm_pvr_ioctl_dev_query_args *args = data;
	struct drm_pvr_dev_query_gpu_info info = { 0 };
	static const uint8_t zeros[256];
	uintptr_t address;
	size_t copied, count;
	int error;

	if (args->type == DRM_PVR_DEV_QUERY_HEAP_INFO_GET ||
	    args->type == DRM_PVR_DEV_QUERY_STATIC_DATA_AREAS_GET)
		return pvr_heap_query(args->type, args);
	if (args->type != DRM_PVR_DEV_QUERY_GPU_INFO_GET)
		return -EOPNOTSUPP;
	if (args->pointer == 0) {
		args->size = sizeof(info);
		return 0;
	}
	/* Upstream's last mandatory field is num_phantoms, at bytes 8..11. */
	if (args->size < offsetof(struct drm_pvr_dev_query_gpu_info,
	    _padding_c))
		return -EINVAL;
	if (args->pointer > UINTPTR_MAX ||
	    args->size - 1 > UINTPTR_MAX - args->pointer)
		return -EFAULT;
	address = (uintptr_t)args->pointer;
	info.gpu_id = sc->bvnc;
	info.num_phantoms = 1;
	copied = MIN(args->size, sizeof(info));
	error = copyout(&info, (void *)address, copied);
	if (error != 0)
		return -error;
	/* The upstream ABI clears the caller's extension, without allocating it. */
	while (copied < args->size) {
		count = MIN(args->size - copied, sizeof(zeros));
		error = copyout(zeros, (void *)(address + copied), count);
		if (error != 0)
			return -error;
		copied += count;
	}
	args->size = MIN(args->size, sizeof(info));
	return 0;
}

static void
pvr_release(struct drm_device *dev)
{
	struct pvr_device *sc = dev->dev_private;

	if (sc != NULL)
		kmem_free(sc, sizeof(*sc));
	drm_dev_fini(dev);
	kfree(dev);
}

static const struct drm_ioctl_desc pvr_ioctls[] = {
	DRM_IOCTL_DEF_DRV(PVR_DEV_QUERY, pvr_dev_query,
	    DRM_RENDER_ALLOW | DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(PVR_CREATE_BO, pvr_create_bo,
	    DRM_RENDER_ALLOW | DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(PVR_GET_BO_MMAP_OFFSET, pvr_get_bo_mmap_offset,
	    DRM_RENDER_ALLOW | DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(PVR_CREATE_VM_CONTEXT, pvr_create_vm_context,
	    DRM_RENDER_ALLOW | DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(PVR_DESTROY_VM_CONTEXT, pvr_destroy_vm_context,
	    DRM_RENDER_ALLOW | DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(PVR_VM_MAP, pvr_vm_map_ioctl,
	    DRM_RENDER_ALLOW | DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(PVR_VM_UNMAP, pvr_vm_unmap_ioctl,
	    DRM_RENDER_ALLOW | DRM_UNLOCKED),
};

static struct drm_driver pvr_driver = {
	.driver_features = DRIVER_GEM | DRIVER_RENDER,
	.ioctls = pvr_ioctls,
	.num_ioctls = __arraycount(pvr_ioctls),
	.open = pvr_file_open,
	.postclose = pvr_file_close,
	.release = pvr_release,
	.mmap_object = pvr_mmap_object,
	.gem_uvm_ops = &drm_gem_cma_uvm_ops,
	.name = "powervr",
	.desc = "EmberBSD experimental PowerVR GEM and VM interface",
	.date = "20261013",
	.major = 1,
	.minor = 0,
	.patchlevel = 0,
};

int
pvr_render_attach(device_t self, bus_dma_tag_t dmat, uint64_t bvnc,
    struct drm_device **owner)
{
	struct pvr_device *sc;
	struct drm_device *dev;
	extern int drm_guarantee_initialized(void);
	int error;

	if (owner == NULL || self == NULL || dmat == NULL)
		return EINVAL;
	if (*owner != NULL)
		return EALREADY;
	if (bvnc != PVR_A733_BVNC)
		return ENODEV;
	error = drm_guarantee_initialized();
	if (error != 0)
		return error;
	dev = drm_dev_alloc(&pvr_driver, self);
	if (IS_ERR(dev))
		return -PTR_ERR(dev);
	sc = kmem_zalloc(sizeof(*sc), KM_SLEEP);
	sc->bvnc = bvnc;
	dev->dev_private = sc;
	dev->bus_dmat = dev->bus_dmat32 = dev->dmat = dmat;
	dev->native_platform.version = DRM_NATIVE_PLATFORM_VERSION;
	dev->native_platform.length = sizeof(dev->native_platform);
	dev->native_platform.bus_type = DRM_NATIVE_BUS_PLATFORM;
	strlcpy(dev->native_platform.fullname, device_xname(self),
	    sizeof(dev->native_platform.fullname));
	strlcpy(dev->native_platform.compatible, "allwinner,sun60i-a733-gpu",
	    sizeof(dev->native_platform.compatible));
	error = -drm_dev_register(dev, 0);
	if (error != 0) {
		/* The last reference releases private data, including failed attach. */
		drm_dev_put(dev);
		return error;
	}
	*owner = dev;
	return 0;
}
