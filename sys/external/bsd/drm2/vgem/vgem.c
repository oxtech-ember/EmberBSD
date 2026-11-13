/* SPDX-License-Identifier: MIT */
/*
 * vgem for EmberBSD: a software-only DRM render device whose only job is to
 * give userland (and tests) a real driver path through dma_fence, dma_resv
 * and dma-buf.  Dumb buffers live in UVM anonymous memory (drm_gem_shmem),
 * PRIME export gives a dma-buf that shares the GEM object's dma_resv, and the
 * VGEM_FENCE_ATTACH / VGEM_FENCE_SIGNAL ioctls attach and signal fences on it.
 *
 * The fence ioctls follow drivers/gpu/drm/vgem/vgem_fence.c of Linux
 * (Copyright 2016 Intel Corporation, MIT).  With VGEM_NEWAPI they use the
 * Linux 6.18 dma_resv calls (usage, reserve_fences, add_fence); without it
 * the Linux 5.6 calls (excl/shared) so the same source runs on a kernel that
 * has only the old layer.
 *
 * Loadable module, one pseudo-device, no hot unload.
 */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/module.h>
#include <sys/kmem.h>
#include <sys/bus.h>

#include <linux/err.h>
#include <linux/idr.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/dma-buf.h>
#include <linux/dma-fence.h>
#include <linux/dma-resv.h>

#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_prime.h>
#include <drm/vgem_drm.h>

extern int drm_guarantee_initialized(void);
#ifdef __x86_64__
extern struct x86_bus_dma_tag pci_bus_dma64_tag;
#endif

#define VGEM_NAME		"vgem"
#define VGEM_MAX_DUMB		(64u * 1024 * 1024)
#define VGEM_FENCE_TIMEOUT	(10*HZ)

struct vgem_file {
	struct idr	fence_idr;
	struct mutex	fence_mutex;
};

struct vgem_fence {
	struct dma_fence	base;
	struct spinlock		lock;
	struct timer_list	timer;
};

struct vgem_softc {
	device_t		sc_dev;
	struct drm_device	*sc_drm;
};

static struct vgem_softc *vgem_sc;
static int vgem_attach_status;

/* ------------------------------------------------------------ fences */

static const char *
vgem_fence_get_driver_name(struct dma_fence *fence)
{
	return "vgem";
}

static const char *
vgem_fence_get_timeline_name(struct dma_fence *fence)
{
	return "unbound";
}

static void
vgem_fence_release(struct dma_fence *base)
{
	struct vgem_fence *fence = container_of(base, typeof(*fence), base);

	del_timer_sync(&fence->timer);
	dma_fence_free(&fence->base);
}

static const struct dma_fence_ops vgem_fence_ops = {
	.get_driver_name = vgem_fence_get_driver_name,
	.get_timeline_name = vgem_fence_get_timeline_name,
	.release = vgem_fence_release,
};

static void
vgem_fence_timeout(struct timer_list *t)
{
	struct vgem_fence *fence = from_timer(fence, t, timer);

	dma_fence_signal(&fence->base);
}

static struct dma_fence *
vgem_fence_create(struct vgem_file *vfile, unsigned int flags)
{
	struct vgem_fence *fence;

	fence = kzalloc(sizeof(*fence), GFP_KERNEL);
	if (!fence)
		return NULL;

	spin_lock_init(&fence->lock);
	dma_fence_init(&fence->base, &vgem_fence_ops, &fence->lock,
	    dma_fence_context_alloc(1), 1);
	timer_setup(&fence->timer, vgem_fence_timeout, 0);

	/* The fence expires within 10 s so a client cannot hang the driver. */
	mod_timer(&fence->timer, jiffies + VGEM_FENCE_TIMEOUT);
	return &fence->base;
}

static int
vgem_fence_attach_ioctl(struct drm_device *dev, void *data,
    struct drm_file *file)
{
	struct drm_vgem_fence_attach *arg = data;
	struct vgem_file *vfile = file->driver_priv;
	struct dma_resv *resv;
	struct drm_gem_object *obj;
	struct dma_fence *fence;
	bool write;
	int ret;

	if (arg->flags & ~VGEM_FENCE_WRITE)
		return -EINVAL;
	if (arg->pad)
		return -EINVAL;
	write = (arg->flags & VGEM_FENCE_WRITE) != 0;

	obj = drm_gem_object_lookup(file, arg->handle);
	if (!obj)
		return -ENOENT;

	fence = vgem_fence_create(vfile, arg->flags);
	if (!fence) {
		ret = -ENOMEM;
		goto err;
	}

	/* A conflicting fence: a write waits for all, a read for the writer. */
	resv = obj->resv;
#ifdef VGEM_NEWAPI
	if (!dma_resv_test_signaled(resv, dma_resv_usage_rw(write))) {
#else
	if (!dma_resv_test_signaled_rcu(resv, write)) {
#endif
		ret = -EBUSY;
		goto err_fence;
	}

	/* Expose the fence through the dma-buf. */
	ret = 0;
	dma_resv_lock(resv, NULL);
#ifdef VGEM_NEWAPI
	ret = dma_resv_reserve_fences(resv, 1);
	if (ret == 0)
		dma_resv_add_fence(resv, fence, write ?
		    DMA_RESV_USAGE_WRITE : DMA_RESV_USAGE_READ);
#else
	if (write)
		dma_resv_add_excl_fence(resv, fence);
	else if ((ret = dma_resv_reserve_shared(resv, 1)) == 0)
		dma_resv_add_shared_fence(resv, fence);
#endif
	dma_resv_unlock(resv);

	/* Remember the fence for the later signal call. */
	if (ret == 0) {
		idr_preload(GFP_KERNEL);
		mutex_lock(&vfile->fence_mutex);
		ret = idr_alloc(&vfile->fence_idr, fence, 1, 0, GFP_KERNEL);
		mutex_unlock(&vfile->fence_mutex);
		idr_preload_end();
		if (ret > 0) {
			arg->out_fence = ret;
			ret = 0;
		}
	}
err_fence:
	if (ret) {
		dma_fence_signal(fence);
		dma_fence_put(fence);
	}
err:
	drm_gem_object_put_unlocked(obj);
	return ret;
}

static int
vgem_fence_signal_ioctl(struct drm_device *dev, void *data,
    struct drm_file *file)
{
	struct vgem_file *vfile = file->driver_priv;
	struct drm_vgem_fence_signal *arg = data;
	struct dma_fence *fence;
	int ret = 0;

	if (arg->flags)
		return -EINVAL;

	mutex_lock(&vfile->fence_mutex);
	fence = idr_replace(&vfile->fence_idr, NULL, arg->fence);
	mutex_unlock(&vfile->fence_mutex);
	if (!fence)
		return -ENOENT;
	if (IS_ERR(fence))
		return PTR_ERR(fence);

	if (dma_fence_is_signaled(fence))
		ret = -ETIMEDOUT;

	dma_fence_signal(fence);
	dma_fence_put(fence);
	return ret;
}

static int
vgem_open(struct drm_device *dev, struct drm_file *file)
{
	struct vgem_file *vfile;

	vfile = kzalloc(sizeof(*vfile), GFP_KERNEL);
	if (!vfile)
		return -ENOMEM;
	file->driver_priv = vfile;
	linux_mutex_init(&vfile->fence_mutex);
	idr_init(&vfile->fence_idr);
	return 0;
}

static int
vgem_fence_idr_fini(int id, void *p, void *data)
{
	dma_fence_signal(p);
	dma_fence_put(p);
	return 0;
}

static void
vgem_postclose(struct drm_device *dev, struct drm_file *file)
{
	struct vgem_file *vfile = file->driver_priv;

	idr_for_each(&vfile->fence_idr, vgem_fence_idr_fini, vfile);
	idr_destroy(&vfile->fence_idr);
	linux_mutex_destroy(&vfile->fence_mutex);
	kfree(vfile);
}

/* --------------------------------------------------------------- GEM */

static const struct drm_gem_object_funcs vgem_gem_funcs = {
	.free		= drm_gem_shmem_free_object,
	.print_info	= drm_gem_shmem_print_info,
	.pin		= drm_gem_shmem_pin,
	.unpin		= drm_gem_shmem_unpin,
	.get_sg_table	= drm_gem_shmem_get_sg_table,
	.vmap		= drm_gem_shmem_vmap,
	.vunmap		= drm_gem_shmem_vunmap,
};

static struct drm_gem_object *
vgem_gem_create_object(struct drm_device *dev, size_t size)
{
	struct drm_gem_shmem_object *bo;

	bo = kzalloc(sizeof(*bo), GFP_KERNEL);
	if (bo == NULL)
		return NULL;
	bo->base.funcs = &vgem_gem_funcs;
	return &bo->base;
}

static int
vgem_dumb_create(struct drm_file *file, struct drm_device *dev,
    struct drm_mode_create_dumb *args)
{
	struct drm_gem_shmem_object *shmem;
	uint64_t pitch, size;
	uint32_t handle;
	int ret;

	if (args->bpp != 32 || args->flags != 0)
		return -EINVAL;
	if (args->width == 0 || args->height == 0 ||
	    args->width > 8192 || args->height > 8192)
		return -EINVAL;
	pitch = (uint64_t)args->width * 4;
	size = pitch * args->height;
	if (size > VGEM_MAX_DUMB)
		return -EINVAL;
	size = roundup(size, PAGE_SIZE);

	shmem = drm_gem_shmem_create(dev, size);
	if (IS_ERR(shmem))
		return PTR_ERR(shmem);
	ret = drm_gem_handle_create(file, &shmem->base, &handle);
	/* The handle (if any) holds the reference now. */
	drm_gem_object_put_unlocked(&shmem->base);
	if (ret)
		return ret;
	args->pitch = (uint32_t)pitch;
	args->size = size;
	args->handle = handle;
	return 0;
}

/* ------------------------------------------------------- DRM driver */

static const struct drm_ioctl_desc vgem_ioctls[] = {
	DRM_IOCTL_DEF_DRV(VGEM_FENCE_ATTACH, vgem_fence_attach_ioctl,
	    DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(VGEM_FENCE_SIGNAL, vgem_fence_signal_ioctl,
	    DRM_RENDER_ALLOW),
};

static struct drm_driver vgem_driver = {
	.driver_features	= DRIVER_GEM | DRIVER_RENDER,
	.open			= vgem_open,
	.postclose		= vgem_postclose,
	.ioctls			= vgem_ioctls,
	.num_ioctls		= ARRAY_SIZE(vgem_ioctls),
	.dumb_create		= vgem_dumb_create,
	.dumb_map_offset	= drm_gem_dumb_map_offset,
	.dumb_destroy		= drm_gem_dumb_destroy,
	.prime_handle_to_fd	= drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle	= drm_gem_prime_fd_to_handle,
	.gem_prime_mmap		= drm_gem_shmem_prime_mmap,
	.gem_create_object	= vgem_gem_create_object,
	.mmap_object		= drm_gem_mmap_object,
	.gem_uvm_ops		= &drm_gem_shmem_uvm_ops,
	.name	= VGEM_NAME,
	.desc	= "Virtual GEM provider",
	.date	= "20261113",
	.major	= 1,
	.minor	= 0,
	.patchlevel = 0,
};

/* ----------------------------------------------------------- autoconf */

static int	vgem_match(device_t, cfdata_t, void *);
static void	vgem_attach(device_t, device_t, void *);
static int	vgem_detach(device_t, int);

CFATTACH_DECL_NEW(vgem, sizeof(struct vgem_softc), vgem_match, vgem_attach,
    vgem_detach, NULL);

static int
vgem_match(device_t parent, cfdata_t cf, void *aux)
{
	return 1;
}

static void
vgem_attach(device_t parent, device_t self, void *aux)
{
	struct vgem_softc *sc = device_private(self);
	struct drm_device *dev;
	int ret;

	sc->sc_dev = self;
	aprint_naive("\n");
	aprint_normal_dev(self, "virtual GEM provider (%s dma_resv API)\n",
#ifdef VGEM_NEWAPI
	    "6.18"
#else
	    "5.6"
#endif
	    );
	ret = -drm_guarantee_initialized();
	if (ret == 0) {
		dev = drm_dev_alloc(&vgem_driver, self);
		if (IS_ERR(dev)) {
			ret = PTR_ERR(dev);
		} else {
			sc->sc_drm = dev;
			dev->dev_private = sc;
#ifdef __x86_64__
			dev->bus_dmat = dev->bus_dmat32 = dev->dmat =
			    &pci_bus_dma64_tag;
			dev->dmat_subregion_p = false;
#endif
			ret = drm_dev_set_unique(dev, device_xname(self));
			if (ret == 0)
				ret = drm_dev_register(dev, 0);
			if (ret) {
				drm_dev_put(dev);
				sc->sc_drm = NULL;
			}
		}
	}
	if (ret) {
		aprint_error_dev(self, "DRM initialization failed: %d\n", ret);
		vgem_attach_status = ret < 0 ? -ret : ret;
		return;
	}
	vgem_attach_status = 0;
	vgem_sc = sc;
	aprint_normal_dev(self, "/dev/dri/card%d, render node %d\n",
	    sc->sc_drm->primary->index,
	    sc->sc_drm->render ? sc->sc_drm->render->index : -1);
}

static int
vgem_detach(device_t self, int flags)
{
	struct vgem_softc *sc = device_private(self);

	if (sc->sc_drm == NULL)
		return 0;
	return EBUSY;
}

/* ----------------------------------------------------------- module */

MODULE(MODULE_CLASS_DRIVER, vgem, "drmkms");

CFDRIVER_DECL(vgem, DV_DULL, NULL);
extern struct cfattach vgem_ca;
static struct cfdata vgem_cfdata[] = {
	{
		.cf_name = "vgem",
		.cf_atname = "vgem",
		.cf_unit = 0,
		.cf_fstate = FSTATE_STAR,
	},
	{ NULL, NULL, 0, FSTATE_NOTFOUND, NULL, 0, NULL }
};

static int
vgem_modcmd(modcmd_t cmd, void *arg)
{
	int error;

	switch (cmd) {
	case MODULE_CMD_INIT:
		error = config_cfdriver_attach(&vgem_cd);
		if (error)
			return error;
		error = config_cfattach_attach(vgem_cd.cd_name, &vgem_ca);
		if (error) {
			config_cfdriver_detach(&vgem_cd);
			return error;
		}
		error = config_cfdata_attach(vgem_cfdata, 1);
		if (error) {
			config_cfattach_detach(vgem_cd.cd_name, &vgem_ca);
			config_cfdriver_detach(&vgem_cd);
			return error;
		}
		vgem_attach_status = ENXIO;
		(void)config_attach_pseudo(vgem_cfdata);
		if (vgem_sc == NULL) {
			error = vgem_attach_status ? vgem_attach_status : ENXIO;
			if (config_cfdata_detach(vgem_cfdata) == 0) {
				config_cfattach_detach(vgem_cd.cd_name,
				    &vgem_ca);
				config_cfdriver_detach(&vgem_cd);
			}
			return error;
		}
		return 0;
	case MODULE_CMD_FINI:
		printf("vgem: unload is not supported (DRM device is live)\n");
		return EBUSY;
	case MODULE_CMD_AUTOUNLOAD:
		return EBUSY;
	default:
		return ENOTTY;
	}
}
