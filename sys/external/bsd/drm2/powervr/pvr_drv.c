/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native PowerVR identification and DRM registration. */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/kmem.h>
#include <sys/mutex.h>
#include <sys/rbtree.h>
#include <sys/systm.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_prime.h>
#include "pvr_drm.h"
#include "pvr_backend.h"
#include "pvr_drv.h"
#include "pvr_exec.h"
#include "pvr_exec_hwrt.h"
#include "pvr_freelist.h"
#include "pvr_heap.h"
#include "pvr_query.h"

/* Mesa 26.2.4 BXM-4-64 profile: one cluster, one phantom. */
#define PVR_A733_BVNC	UINT64_C(0x00240038006800b7)

struct pvr_orphan {
	struct pvr_orphan *next;
	void *owner;
};

struct pvr_freelist_entry {
	rb_node_t node;
	uint32_t id;
	struct pvr_freelist **owner;
};

struct pvr_device {
	rb_tree_t freelists;
	struct pvr_freelist_entry *grow_pending;
	struct pvr_orphan *orphans;
	uint32_t next_id;
	kmutex_t gate;
	struct pvr_backend backend;
	bool execution;
	uint64_t bvnc;
	struct pvr_query_profile profile;
};

/* Compare the full firmware ID. User handles are private to each file. */
static int
pvr_freelist_compare_key(void *context, const void *node, const void *key)
{
	const struct pvr_freelist_entry *entry = node;
	const uint32_t *id = key;

	(void)context;
	return entry->id < *id ? -1 : entry->id > *id;
}

static int
pvr_freelist_compare_nodes(void *context, const void *a, const void *b)
{
	const struct pvr_freelist_entry *right = b;

	return pvr_freelist_compare_key(context, a, &right->id);
}

static const rb_tree_ops_t pvr_freelist_tree_ops = {
	.rbto_compare_nodes = pvr_freelist_compare_nodes,
	.rbto_compare_key = pvr_freelist_compare_key,
	.rbto_node_offset = offsetof(struct pvr_freelist_entry, node),
};

static int
pvr_device_grow_prepare(void *cookie, uint32_t id,
    struct rogue_fwif_freelist_gs_data *response)
{
	struct pvr_device *sc = cookie;
	struct pvr_freelist_entry *entry;
	int error;

	KASSERT(mutex_owned(&sc->gate));
	if (sc->grow_pending != NULL)
		return EBUSY;
	entry = rb_tree_find_node(&sc->freelists, &id);
	if (entry == NULL)
		return ENOENT;
	if (*entry->owner == NULL)
		return EIO;
	error = pvr_freelist_grow_prepare(*entry->owner, response);
	if (error == 0)
		sc->grow_pending = entry;
	return error;
}

static int
pvr_device_grow_complete(void *cookie, uint32_t id, int completion)
{
	struct pvr_device *sc = cookie;
	struct pvr_freelist_entry *entry = sc->grow_pending;
	int error;

	KASSERT(mutex_owned(&sc->gate));
	if (entry == NULL || entry->id != id)
		return EINVAL;
	if (*entry->owner == NULL)
		return EIO;
	error = pvr_freelist_grow_complete(*entry->owner, completion);
	if (error == 0)
		sc->grow_pending = NULL;
	return error;
}

int
pvr_device_freelist_add(struct drm_device *dev, uint32_t id,
    struct pvr_freelist **owner)
{
	struct pvr_device *sc = dev->dev_private;
	struct pvr_freelist_entry *entry;

	KASSERT(mutex_owned(&sc->gate));
	if (id == 0 || owner == NULL || *owner != NULL)
		return EINVAL;
	if (rb_tree_find_node(&sc->freelists, &id) != NULL)
		return EEXIST;
	entry = kmem_zalloc(sizeof(*entry), KM_SLEEP);
	entry->id = id;
	entry->owner = owner;
	if (rb_tree_insert_node(&sc->freelists, entry) != entry) {
		kmem_free(entry, sizeof(*entry));
		return EEXIST;
	}
	return 0;
}

int
pvr_device_freelist_remove(struct drm_device *dev, uint32_t id,
    struct pvr_freelist **owner)
{
	struct pvr_device *sc = dev->dev_private;
	struct pvr_freelist_entry *entry;

	KASSERT(mutex_owned(&sc->gate));
	entry = rb_tree_find_node(&sc->freelists, &id);
	if (entry == NULL || entry->owner != owner)
		return EINVAL;
	if (entry == sc->grow_pending || *owner != NULL)
		return EBUSY;
	rb_tree_remove_node(&sc->freelists, entry);
	kmem_free(entry, sizeof(*entry));
	return 0;
}

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

	if (args->type >= DRM_PVR_DEV_QUERY_RUNTIME_INFO_GET &&
	    args->type <= DRM_PVR_DEV_QUERY_ENHANCEMENTS_GET)
		return pvr_query(&sc->profile, args->type, args);
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
	info.num_phantoms = sc->profile.num_phantoms;
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

	if (sc != NULL) {
		KASSERT(RB_TREE_MIN(&sc->freelists) == NULL);
		KASSERT(sc->grow_pending == NULL && sc->orphans == NULL);
		mutex_destroy(&sc->gate);
		kmem_free(sc, sizeof(*sc));
	}
	drm_dev_fini(dev);
	kfree(dev);
}

int
pvr_device_hwrt_info(struct drm_device *dev, uint64_t *bvnc,
    uint32_t *minpages)
{
	struct pvr_device *sc = dev->dev_private;

	if (bvnc == NULL || minpages == NULL)
		return EINVAL;
	*bvnc = sc->bvnc;
	*minpages = (uint32_t)sc->profile.runtime.free_list_min_pages;
	return 0;
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
	    DRM_RENDER_ALLOW | DRM_UNLOCKED),DRM_IOCTL_DEF_DRV(PVR_CREATE_CONTEXT, pvr_create_context,
    DRM_RENDER_ALLOW | DRM_UNLOCKED),
DRM_IOCTL_DEF_DRV(PVR_DESTROY_CONTEXT, pvr_destroy_context,
    DRM_RENDER_ALLOW | DRM_UNLOCKED),
DRM_IOCTL_DEF_DRV(PVR_CREATE_FREE_LIST, pvr_create_free_list,
    DRM_RENDER_ALLOW | DRM_UNLOCKED),
DRM_IOCTL_DEF_DRV(PVR_DESTROY_FREE_LIST, pvr_destroy_free_list,
    DRM_RENDER_ALLOW | DRM_UNLOCKED),
DRM_IOCTL_DEF_DRV(PVR_CREATE_HWRT_DATASET, pvr_create_hwrt_dataset,
    DRM_RENDER_ALLOW | DRM_UNLOCKED),
DRM_IOCTL_DEF_DRV(PVR_DESTROY_HWRT_DATASET, pvr_destroy_hwrt_dataset,
    DRM_RENDER_ALLOW | DRM_UNLOCKED),
DRM_IOCTL_DEF_DRV(PVR_SUBMIT_JOBS, pvr_submit_jobs,
    DRM_RENDER_ALLOW | DRM_UNLOCKED),

};

static struct drm_driver pvr_driver = {
	.driver_features = DRIVER_GEM | DRIVER_RENDER |
	    DRIVER_SYNCOBJ | DRIVER_SYNCOBJ_TIMELINE,
	.ioctls = pvr_ioctls,
	.num_ioctls = __arraycount(pvr_ioctls),
	.open = pvr_file_open,
	.postclose = pvr_file_close,
	.release = pvr_release,
	.mmap_object = pvr_mmap_object,
	.prime_handle_to_fd = drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle = drm_gem_prime_fd_to_handle,
	.gem_prime_mmap = pvr_prime_mmap,
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
    const struct pvr_query_input *input, struct drm_device **owner)
{
	struct pvr_device *sc;
	struct drm_device *dev;
	struct pvr_query_profile profile;
	extern int drm_guarantee_initialized(void);
	int error;

	if (owner == NULL || self == NULL || dmat == NULL || input == NULL)
		return EINVAL;
	if (*owner != NULL)
		return EALREADY;
	if (bvnc != PVR_A733_BVNC || input->bvnc != bvnc)
		return ENODEV;
	error = pvr_query_profile_init(input, &profile);
	if (error != 0)
		return error;
	error = drm_guarantee_initialized();
	if (error != 0)
		return error;
	dev = drm_dev_alloc(&pvr_driver, self);
	if (IS_ERR(dev))
		return -PTR_ERR(dev);
	sc = kmem_zalloc(sizeof(*sc), KM_SLEEP);
	mutex_init(&sc->gate, MUTEX_DEFAULT, IPL_NONE);
	rb_tree_init(&sc->freelists, &pvr_freelist_tree_ops);
	sc->bvnc = bvnc;
	sc->profile = profile;
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

int
pvr_backend_attach(struct drm_device *dev, const struct pvr_backend *backend)
{
	struct pvr_device *sc;
	int error = 0;

	if (dev == NULL || dev->dev_private == NULL || backend == NULL ||
	    backend->cookie == NULL || backend->enter == NULL ||
	    backend->leave == NULL || backend->alloc == NULL ||
	    backend->free == NULL || backend->sync == NULL ||
	    backend->command == NULL || backend->flush == NULL ||
	    backend->check == NULL || backend->fail == NULL ||
	    backend->set_events == NULL)
		return EINVAL;
	sc = dev->dev_private;
	mutex_enter(&sc->gate);
	if (sc->execution) {
		error = EALREADY;
	} else {
		const struct pvr_fw_events events = {
			.cookie = sc,
			.grow_prepare = pvr_device_grow_prepare,
			.grow_complete = pvr_device_grow_complete,
		};

		backend->enter(backend->cookie);
		error = backend->set_events(backend->cookie, &events);
		backend->leave(backend->cookie);
		if (error == 0) {
			sc->backend = *backend;
			sc->execution = true;
		} else if (error < 0) {
			error = EPROTO;
		}
	}
	mutex_exit(&sc->gate);
	return error;
}

const struct pvr_backend *
pvr_device_enter(struct drm_device *dev)
{
	struct pvr_device *sc = dev->dev_private;

	mutex_enter(&sc->gate);
	if (!sc->execution)
		return NULL;
	sc->backend.enter(sc->backend.cookie);
	return &sc->backend;
}

void
pvr_device_leave(struct drm_device *dev)
{
	struct pvr_device *sc = dev->dev_private;

	if (sc->execution)
		sc->backend.leave(sc->backend.cookie);
	mutex_exit(&sc->gate);
}

void
pvr_device_retain(struct drm_device *dev, void *owner)
{
	struct pvr_device *sc = dev->dev_private;
	struct pvr_orphan *orphan;

	KASSERT(mutex_owned(&sc->gate));
	KASSERT(owner != NULL);
	orphan = kmem_zalloc(sizeof(*orphan), KM_SLEEP);
	orphan->owner = owner;
	orphan->next = sc->orphans;
	sc->orphans = orphan;
	/* An unacknowledged DMA reference cannot outlive its device owner. */
	drm_dev_get(dev);
}

int
pvr_device_next_id(struct drm_device *dev, uint32_t *id)
{
	struct pvr_device *sc = dev->dev_private;

	KASSERT(mutex_owned(&sc->gate));
	if (sc->next_id == UINT32_MAX)
		return EOVERFLOW;
	*id = ++sc->next_id;
	return 0;
}
