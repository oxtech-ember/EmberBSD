/* Origin: EmberBSD native BCM2712 render registration, 2026-10-11. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/kmem.h>
#include <sys/mman.h>
#include <sys/systm.h>
#include <sys/sysctl.h>
#include <uvm/uvm_extern.h>
#include <arm/broadcom/bcm2712_v3d_takeover.h>
#include <linux/err.h>
#include <drm/drm_drv.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_gem.h>
#include <drm/drm_prime.h>
#include "bcmv3d_drv.h"

/* Compute the total BO budget once, before the first reservation. */
static size_t
bcmv3d_bo_total(void)
{
	uint64_t total;

	total = (uint64_t)ctob(physmem) >> BCMV3D_BO_TOTAL_SHIFT;
	if (total < BCMV3D_BO_TOTAL_MIN)
		total = BCMV3D_BO_TOTAL_MIN;
	if (total > BCMV3D_BO_TOTAL_MAX)
		total = BCMV3D_BO_TOTAL_MAX;
	return (size_t)total;
}

/* Copy under memory_lock; user copyout occurs after that lock is released. */
static int
bcmv3d_bo_sysctl(SYSCTLFN_ARGS)
{
	struct sysctlnode node = *rnode;
	struct bcmv3d_device *sc = node.sysctl_data;
	struct bcmv3d_bo_stats snapshot;

	if (newp != NULL)
		return EPERM;
	bcmv3d_bo_snapshot(sc, &snapshot);
	node.sysctl_data = &snapshot;
	node.sysctl_size = sizeof(snapshot);
	return sysctl_lookup(SYSCTLFN_CALL(&node));
}

static void
bcmv3d_bo_sysctl_init(struct bcmv3d_device *sc)
{
	const struct sysctlnode *root;
	int error;

	error = sysctl_createv(&sc->sysctllog, 0, NULL, &root,
	    CTLFLAG_READONLY, CTLTYPE_NODE, device_xname(sc->dev),
	    SYSCTL_DESCR("Native V3D render device"), NULL, 0, NULL, 0,
	    CTL_HW, CTL_CREATE, CTL_EOL);
	if (error == 0)
		error = sysctl_createv(&sc->sysctllog, 0, &root, NULL,
		    CTLFLAG_READONLY, CTLTYPE_STRUCT, "bo_stats",
		    SYSCTL_DESCR("V3D BO reservation and allocation failure snapshot"),
		    bcmv3d_bo_sysctl, 0, sc, sizeof(struct bcmv3d_bo_stats),
		    CTL_CREATE, CTL_EOL);
	if (error != 0) {
		sysctl_teardown(&sc->sysctllog);
		aprint_error_dev(sc->dev, "BO diagnostic sysctl unavailable: %d\n",
		    error);
	}
}

/* A PRIME fd authorizes this BO; the offset remains relative, in bytes. */
static int
bcmv3d_prime_mmap(struct drm_gem_object *obj, off_t *offp, size_t size,
    int prot, int *flagsp, int *advicep, struct uvm_object **uobjp,
    int *maxprotp)
{
	if (*offp < 0 || (*offp & (PAGE_SIZE - 1)) != 0 || size == 0 ||
	    (size & (PAGE_SIZE - 1)) != 0 || (uint64_t)*offp > obj->size ||
	    size > obj->size - (size_t)*offp ||
	    (prot & ~(PROT_READ | PROT_WRITE)) != 0 ||
	    (*flagsp & MAP_SHARED) == 0 || (*flagsp & MAP_PRIVATE) != 0)
		return -EINVAL;
	/* The native BO pager drops this reference when the mapping ends. */
	drm_gem_object_get(obj);
	*uobjp = &obj->gemo_uvmobj;
	*maxprotp = PROT_READ | PROT_WRITE;
	return 0;
}

static int
bcmv3d_get_param(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct bcmv3d_device *sc = dev->dev_private;
	struct drm_v3d_get_param *args = data;

	args->value = 0;
	if (args->pad != 0)
		return -EINVAL;
	if (args->param < __arraycount(sc->ident))
		args->value = sc->ident[args->param];
	else if (args->param > DRM_V3D_PARAM_SUPPORTS_CPU_QUEUE)
		return -EINVAL;
	/* The bounded TFU operation is not general Mesa TFU capability. */
	return 0;
}

static const struct drm_ioctl_desc bcmv3d_ioctls[] = {
	DRM_IOCTL_DEF_DRV(V3D_SUBMIT_CL, bcmv3d_submit_cl,
	    DRM_RENDER_ALLOW | DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(V3D_CREATE_BO, bcmv3d_create_bo,
	    DRM_RENDER_ALLOW | DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(V3D_MMAP_BO, bcmv3d_mmap_bo,
	    DRM_RENDER_ALLOW | DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(V3D_GET_BO_OFFSET, bcmv3d_get_bo_offset,
	    DRM_RENDER_ALLOW | DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(V3D_GET_PARAM, bcmv3d_get_param,
	    DRM_RENDER_ALLOW | DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(V3D_WAIT_BO, bcmv3d_wait_bo,
	    DRM_RENDER_ALLOW | DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(V3D_SUBMIT_TFU, bcmv3d_submit_tfu,
	    DRM_RENDER_ALLOW | DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(V3D_SUBMIT_CSD, bcmv3d_submit_csd,
	    DRM_RENDER_ALLOW | DRM_UNLOCKED),
};

static struct drm_driver bcmv3d_driver = {
	.driver_features = DRIVER_GEM | DRIVER_RENDER | DRIVER_SYNCOBJ,
	.ioctls = bcmv3d_ioctls,
	.num_ioctls = __arraycount(bcmv3d_ioctls),
	.mmap_object = drm_gem_mmap_object,
	.gem_uvm_ops = &bcmv3d_bo_uvm_ops,
	.prime_handle_to_fd = drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle = drm_gem_prime_fd_to_handle,
	.gem_prime_mmap = bcmv3d_prime_mmap,
	/* Core self-import retains our BO; foreign imports have no SG hook. */
	.name = "v3d",
	.desc = "EmberBSD BCM2712 bounded render interface",
	.date = "20261011",
	.major = 1,
	.minor = 0,
	.patchlevel = 0,
};

int
bcmv3d_render_attach(device_t self, bus_space_tag_t bst, bus_dma_tag_t dmat,
    ACPI_HANDLE handle, const struct bcmv3d_bootfb_owner *bootfb)
{
	struct bcmv3d_device *sc;
	struct drm_device *dev;
	extern int drm_guarantee_initialized(void);
	unsigned int i;
	int error;

	if (!bcmv3d_takeover_complete())
		return EPERM;
	error = drm_guarantee_initialized();
	if (error != 0)
		return error;
	dev = drm_dev_alloc(&bcmv3d_driver, self);
	if (IS_ERR(dev))
		return -PTR_ERR(dev);
	sc = kmem_zalloc(sizeof(*sc), KM_SLEEP);
	sc->drm = dev;
	sc->dev = self;
	sc->dmat = dmat;
	sc->bst = bst;
	sc->handle = handle;
	sc->bootfb = bootfb;
	dev->dev_private = sc;
	dev->bus_dmat = dev->bus_dmat32 = dev->dmat = dmat;
	sc->bo_total = bcmv3d_bo_total();
	mutex_init(&sc->memory_lock, MUTEX_DEFAULT, IPL_NONE);
	sc->va = vmem_create("v3dva", BCMV3D_VA_START, BCMV3D_VA_SIZE,
	    PAGE_SIZE, NULL, NULL, NULL, 0, VM_SLEEP, IPL_NONE);
	if (sc->va == NULL) {
		error = ENOMEM;
		goto fail;
	}
	for (i = 0; i < __arraycount(sc->ident); i++) {
		if (i == DRM_V3D_PARAM_V3D_UIFCFG)
			error = bcmv3d_takeover_hub_peek(4, &sc->ident[i]);
		else if (i < 4)
			error = bcmv3d_takeover_hub_peek(8 + 4 * i, &sc->ident[i]);
		else
			error = bcmv3d_takeover_core_peek(4 * (i - 4), &sc->ident[i]);
		if (error != 0)
			goto fail;
	}
	/* The ACPI attachment accepts BCM2850 on BCM2712 only. */
	dev->native_platform.version = DRM_NATIVE_PLATFORM_VERSION;
	dev->native_platform.length = sizeof(dev->native_platform);
	dev->native_platform.bus_type = DRM_NATIVE_BUS_PLATFORM;
	strlcpy(dev->native_platform.fullname, device_xname(self),
	    sizeof(dev->native_platform.fullname));
	strlcpy(dev->native_platform.compatible, "acpi:BCM2850",
	    sizeof(dev->native_platform.compatible));
	error = -drm_dev_set_unique(dev, device_xname(self));
	if (error == 0)
		error = bcmv3d_job_init(sc);
	if (error == 0)
		error = -drm_dev_register(dev, 0);
	if (error != 0)
		goto fail;
	bcmv3d_bo_sysctl_init(sc);
	aprint_normal_dev(self, "native V3D render interface: GEM and "
	    "serialized CL/TFU/CSD with binary syncobj; experimental render interface\n");
	/* Device, IRQ and MMU lifetime ends at reboot; no hot removal. */
	return 0;
fail:
	sysctl_teardown(&sc->sysctllog);
	bcmv3d_job_fini(sc);
	if (sc->va != NULL)
		vmem_destroy(sc->va);
	mutex_destroy(&sc->memory_lock);
	dev->dev_private = NULL;
	kmem_free(sc, sizeof(*sc));
	drm_dev_put(dev);
	return error;
}
