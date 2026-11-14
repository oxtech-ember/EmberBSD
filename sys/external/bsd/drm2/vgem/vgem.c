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
 * Two cores can host it.  Without VGEM_CORE618 it is written for the 5.6 core
 * (drm_gem_shmem helpers, uvm pager of the GEM object); with VGEM_CORE618 for
 * the 6.18 core (module vgem618): a buffer is a GEM object over a shmem file
 * whose pages are pinned while the object is mapped, the pages enter the user
 * mapping through the fault handler of vm_ops, and the dma-buf is the one of
 * drm_prime.c.  The fence code is the same for both.
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
#ifndef VGEM_CORE618
#include <drm/drm_gem_shmem_helper.h>
#endif
#include <drm/drm_ioctl.h>
#include <drm/drm_prime.h>
#include <drm/vgem_drm.h>

#ifdef VGEM_CORE618
#define	vgem_obj_put(obj)	drm_gem_object_put(obj)
#else
#define	vgem_obj_put(obj)	drm_gem_object_put_unlocked(obj)
extern int drm_guarantee_initialized(void);
#ifdef __x86_64__
extern struct x86_bus_dma_tag pci_bus_dma64_tag;
#endif
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
	vgem_obj_put(obj);
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

#ifdef VGEM_CORE618

/*
 * A buffer: a shmem file (drm_gem_object_init) whose pages are pinned while
 * something needs them: a mapping (pinned when it is made, once more for every
 * vm_ops open, released by close) or a PRIME importer (pin and unpin of the
 * object functions).  The fault handler therefore never allocates.
 */
struct vgem_bo {
	struct drm_gem_object	base;
	struct mutex		pages_lock;
	struct page		**pages;
	unsigned int		pin_count;
};

/* base is the first member: also for a vm_private_data, which is a void pointer */
#define	to_vgem_bo(o)	((struct vgem_bo *)(o))

static int
vgem_pin_pages(struct vgem_bo *bo)
{
	struct page **pages;

	mutex_lock(&bo->pages_lock);
	if (bo->pin_count == 0) {
		pages = drm_gem_get_pages(&bo->base);
		if (IS_ERR(pages)) {
			mutex_unlock(&bo->pages_lock);
			return PTR_ERR(pages);
		}
		bo->pages = pages;
	}
	bo->pin_count++;
	mutex_unlock(&bo->pages_lock);
	return 0;
}

static void
vgem_unpin_pages(struct vgem_bo *bo)
{

	mutex_lock(&bo->pages_lock);
	KASSERT(bo->pin_count > 0);
	if (--bo->pin_count == 0) {
		drm_gem_put_pages(&bo->base, bo->pages, true, true);
		bo->pages = NULL;
	}
	mutex_unlock(&bo->pages_lock);
}

static vm_fault_t
vgem_gem_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct vgem_bo *bo = to_vgem_bo(vma->vm_private_data);
	pgoff_t page_offset = (vmf->address - vma->vm_start) >> PAGE_SHIFT;
	struct page *page = NULL;

	mutex_lock(&bo->pages_lock);
	if (bo->pages != NULL && page_offset < (bo->base.size >> PAGE_SHIFT))
		page = bo->pages[page_offset];
	mutex_unlock(&bo->pages_lock);
	if (page == NULL)
		return VM_FAULT_SIGBUS;
	return vmf_insert_page(vma, vmf->address, page);
}

/* Another reference of the mapping (fork, split): one more pin, one more object reference. */
static void
vgem_vm_open(struct vm_area_struct *vma)
{
	struct vgem_bo *bo = to_vgem_bo(vma->vm_private_data);

	drm_gem_vm_open(vma);
	mutex_lock(&bo->pages_lock);
	KASSERT(bo->pin_count > 0);
	bo->pin_count++;
	mutex_unlock(&bo->pages_lock);
}

static void
vgem_vm_close(struct vm_area_struct *vma)
{
	struct vgem_bo *bo = to_vgem_bo(vma->vm_private_data);

	vgem_unpin_pages(bo);
	drm_gem_vm_close(vma);
}

static const struct vm_operations_struct vgem_gem_vm_ops = {
	.fault	= vgem_gem_fault,
	.open	= vgem_vm_open,
	.close	= vgem_vm_close,
};

static int
vgem_gem_mmap(struct drm_gem_object *obj, struct vm_area_struct *vma)
{
	int ret;

	ret = vgem_pin_pages(to_vgem_bo(obj));
	if (ret)
		return ret;
	vm_flags_set(vma, VM_DONTEXPAND | VM_DONTDUMP | VM_IO | VM_PFNMAP);
	/* ordinary cached memory: no write combining for pages of the page cache */
	vma->vm_page_prot = vm_get_page_prot(vma->vm_flags);
	return 0;
}

static int
vgem_gem_pin(struct drm_gem_object *obj)
{

	return vgem_pin_pages(to_vgem_bo(obj));
}

static void
vgem_gem_unpin(struct drm_gem_object *obj)
{

	vgem_unpin_pages(to_vgem_bo(obj));
}

static struct sg_table *
vgem_gem_get_sg_table(struct drm_gem_object *obj)
{
	struct vgem_bo *bo = to_vgem_bo(obj);

	/* the caller holds a pin (drm_gem_pin of drm_gem_map_attach) */
	return drm_prime_pages_to_sg(obj->dev, bo->pages,
	    obj->size >> PAGE_SHIFT);
}

static void
vgem_gem_free(struct drm_gem_object *obj)
{
	struct vgem_bo *bo = to_vgem_bo(obj);

	KASSERT(bo->pin_count == 0);
	drm_gem_object_release(obj);
	mutex_destroy(&bo->pages_lock);
	kfree(bo);
}

static const struct drm_gem_object_funcs vgem_gem_funcs = {
	.free		= vgem_gem_free,
	.pin		= vgem_gem_pin,
	.unpin		= vgem_gem_unpin,
	.get_sg_table	= vgem_gem_get_sg_table,
	.mmap		= vgem_gem_mmap,
	.vm_ops		= &vgem_gem_vm_ops,
};

static struct vgem_bo *
vgem_bo_create(struct drm_device *dev, size_t size)
{
	struct vgem_bo *bo;
	int ret;

	bo = kzalloc(sizeof(*bo), GFP_KERNEL);
	if (bo == NULL)
		return ERR_PTR(-ENOMEM);
	bo->base.funcs = &vgem_gem_funcs;
	ret = drm_gem_object_init(dev, &bo->base, size);
	if (ret) {
		kfree(bo);
		return ERR_PTR(ret);
	}
	mutex_init(&bo->pages_lock);
	return bo;
}

static int
vgem_dumb_create(struct drm_file *file, struct drm_device *dev,
    struct drm_mode_create_dumb *args)
{
	struct vgem_bo *bo;
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

	bo = vgem_bo_create(dev, size);
	if (IS_ERR(bo))
		return PTR_ERR(bo);
	ret = drm_gem_handle_create(file, &bo->base, &handle);
	/* The handle (if any) holds the reference now. */
	vgem_obj_put(&bo->base);
	if (ret)
		return ret;
	args->pitch = (uint32_t)pitch;
	args->size = size;
	args->handle = handle;
	return 0;
}

#else	/* 5.6 core */

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
	vgem_obj_put(&shmem->base);
	if (ret)
		return ret;
	args->pitch = (uint32_t)pitch;
	args->size = size;
	args->handle = handle;
	return 0;
}

#endif	/* VGEM_CORE618 */

/* ------------------------------------------------------- DRM driver */

static const struct drm_ioctl_desc vgem_ioctls[] = {
	DRM_IOCTL_DEF_DRV(VGEM_FENCE_ATTACH, vgem_fence_attach_ioctl,
	    DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(VGEM_FENCE_SIGNAL, vgem_fence_signal_ioctl,
	    DRM_RENDER_ALLOW),
};

#ifdef VGEM_CORE618

DEFINE_DRM_GEM_FOPS(vgem_driver_fops);

static const struct drm_driver vgem_driver = {
	.driver_features	= DRIVER_GEM | DRIVER_RENDER,
	.open			= vgem_open,
	.postclose		= vgem_postclose,
	.ioctls			= vgem_ioctls,
	.num_ioctls		= ARRAY_SIZE(vgem_ioctls),
	.fops			= &vgem_driver_fops,
	.dumb_create		= vgem_dumb_create,
	.name			= __UNCONST(VGEM_NAME),
	.desc			= __UNCONST("Virtual GEM provider"),
	.major			= 1,
	.minor			= 0,
	.patchlevel		= 0,
};

#else

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

#endif

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
	aprint_normal_dev(self, "virtual GEM provider (%s dma_resv API, %s core)\n",
#ifdef VGEM_NEWAPI
	    "6.18",
#else
	    "5.6",
#endif
#ifdef VGEM_CORE618
	    "6.18"
#else
	    "5.6"
#endif
	    );
#ifdef VGEM_CORE618
	ret = 0;
#else
	ret = -drm_guarantee_initialized();
#endif
	if (ret == 0) {
		dev = drm_dev_alloc(&vgem_driver, self);
		if (IS_ERR(dev)) {
			ret = PTR_ERR(dev);
		} else {
			sc->sc_drm = dev;
#ifndef VGEM_CORE618
			dev->dev_private = sc;
#ifdef __x86_64__
			dev->bus_dmat = dev->bus_dmat32 = dev->dmat =
			    &pci_bus_dma64_tag;
			dev->dmat_subregion_p = false;
#endif
			ret = drm_dev_set_unique(dev, device_xname(self));
#endif
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

#ifdef VGEM_CORE618
MODULE(MODULE_CLASS_DRIVER, vgem618, "drmkms618");
#define	vgem_modcmd	vgem618_modcmd
#else
MODULE(MODULE_CLASS_DRIVER, vgem, "drmkms");
#endif

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
