/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD per-file PowerVR VM ownership.
 * UAPI: Linux 7b63ef2d55f24519e7e9e5f4d15dbea03f126e40, MIT alternative.
 * These tables are CPU-owned. Firmware publication and jobs are separate.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/kmem.h>
#include <sys/mutex.h>
#include <sys/systm.h>
#include <linux/idr.h>
#include <drm/drm_device.h>
#include <drm/drm_file.h>
#include <drm/drm_gem.h>
#include "pvr_drm.h"
#include "pvr_drv.h"
#include "pvr_heap.h"
#include "pvr_vm.h"

struct pvr_file_mapping {
	struct pvr_file_mapping *next;
	struct drm_gem_object *object;
	uint64_t address, size;
};

struct pvr_file_vm {
	struct pvr_vm *tables;
	struct pvr_file_mapping *mappings;
};

struct pvr_file {
	/* Serialize lookup, table mutation and removal. DMA allocation can sleep. */
	kmutex_t lock;
	struct idr vms;
};

int
pvr_file_open(struct drm_device *dev, struct drm_file *file)
{
	struct pvr_file *pf;

	pf = kmem_zalloc(sizeof(*pf), KM_SLEEP);
	mutex_init(&pf->lock, MUTEX_DEFAULT, IPL_NONE);
	idr_init(&pf->vms);
	file->driver_priv = pf;
	return 0;
}

static void
pvr_file_vm_free(struct pvr_file_vm *vm)
{
	struct pvr_file_mapping *mapping;

	/* No hardware may reference these tables in the current interface. */
	pvr_vm_destroy(vm->tables);
	while ((mapping = vm->mappings) != NULL) {
		vm->mappings = mapping->next;
		drm_gem_object_put_unlocked(mapping->object);
		kmem_free(mapping, sizeof(*mapping));
	}
	kmem_free(vm, sizeof(*vm));
}

void
pvr_file_close(struct drm_device *dev, struct drm_file *file)
{
	struct pvr_file *pf = file->driver_priv;
	struct pvr_file_vm *vm;
	int id;

	/* DRM calls postclose after all file operations and GEM handle release. */
	for (;;) {
		id = 0;
		vm = idr_get_next(&pf->vms, &id);
		if (vm == NULL)
			break;
		idr_remove(&pf->vms, id);
		pvr_file_vm_free(vm);
	}
	idr_destroy(&pf->vms);
	mutex_destroy(&pf->lock);
	file->driver_priv = NULL;
	kmem_free(pf, sizeof(*pf));
}

static struct pvr_file_vm *
pvr_file_vm_find(struct pvr_file *pf, uint32_t handle)
{

	KASSERT(mutex_owned(&pf->lock));
	if (handle == 0 || handle > INT_MAX)
		return NULL;
	return idr_find(&pf->vms, (int)handle);
}

int
pvr_create_vm_context(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_pvr_ioctl_create_vm_context_args *args = data;
	struct pvr_file *pf = file->driver_priv;
	struct pvr_file_vm *vm;
	int error, handle;

	if (args->_padding_4 != 0)
		return -EINVAL;
	/* Other host page sizes need a matching Rogue page-table profile. */
	if (PAGE_SHIFT != 12)
		return -EOPNOTSUPP;
	vm = kmem_zalloc(sizeof(*vm), KM_SLEEP);
	error = pvr_vm_create(dev->dmat, &vm->tables);
	if (error != 0) {
		kmem_free(vm, sizeof(*vm));
		return -error;
	}
	/* Native idr_alloc requires a preloaded node, even for GFP_KERNEL. */
	idr_preload(GFP_KERNEL);
	mutex_enter(&pf->lock);
	handle = idr_alloc(&pf->vms, vm, 1, 0, GFP_NOWAIT);
	mutex_exit(&pf->lock);
	idr_preload_end();
	if (handle < 0) {
		pvr_file_vm_free(vm);
		return handle;
	}
	args->handle = (uint32_t)handle;
	return 0;
}

int
pvr_destroy_vm_context(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_pvr_ioctl_destroy_vm_context_args *args = data;
	struct pvr_file *pf = file->driver_priv;
	struct pvr_file_vm *vm;

	if (args->_padding_4 != 0)
		return -EINVAL;
	mutex_enter(&pf->lock);
	vm = pvr_file_vm_find(pf, args->handle);
	if (vm != NULL)
		idr_remove(&pf->vms, (int)args->handle);
	mutex_exit(&pf->lock);
	if (vm == NULL)
		return -EINVAL;
	pvr_file_vm_free(vm);
	return 0;
}

int
pvr_vm_map_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_pvr_ioctl_vm_map_args *args = data;
	struct pvr_file *pf = file->driver_priv;
	struct pvr_file_vm *vm;
	struct pvr_file_mapping *mapping;
	struct drm_gem_object *obj;
	uint64_t pa, flags;
	int error;

	if (args->flags != 0 || args->_padding_14 != 0 ||
	    ((args->device_addr | args->offset | args->size) &
	    (PAGE_SIZE - 1)) != 0 ||
	    pvr_heap_find(args->device_addr, args->size) == NULL)
		return -EINVAL;
	obj = drm_gem_object_lookup(file, args->handle);
	if (obj == NULL)
		return -ENOENT;
	error = pvr_bo_mapping(obj, args->offset, args->size, &pa, &flags);
	if (error != 0)
		goto put;
	mapping = kmem_zalloc(sizeof(*mapping), KM_SLEEP);
	mutex_enter(&pf->lock);
	vm = pvr_file_vm_find(pf, args->vm_context_handle);
	if (vm == NULL) {
		error = EINVAL;
	} else {
		error = pvr_vm_map(vm->tables, args->device_addr, pa,
		    args->size, flags);
		if (error == 0) {
			mapping->object = obj;
			mapping->address = args->device_addr;
			mapping->size = args->size;
			mapping->next = vm->mappings;
			vm->mappings = mapping;
		}
	}
	mutex_exit(&pf->lock);
	if (error == 0)
		return 0;
	kmem_free(mapping, sizeof(*mapping));
put:
	drm_gem_object_put_unlocked(obj);
	return -error;
}

int
pvr_vm_unmap_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_pvr_ioctl_vm_unmap_args *args = data;
	struct pvr_file *pf = file->driver_priv;
	struct pvr_file_vm *vm;
	struct pvr_file_mapping *mapping, **link;
	int error = ENOENT;

	if (args->_padding_4 != 0 || args->size == 0 ||
	    ((args->device_addr | args->size) & (PAGE_SIZE - 1)) != 0 ||
	    args->size > UINT64_MAX - args->device_addr)
		return -EINVAL;
	mutex_enter(&pf->lock);
	vm = pvr_file_vm_find(pf, args->vm_context_handle);
	if (vm == NULL) {
		mutex_exit(&pf->lock);
		return -EINVAL;
	}
	for (link = &vm->mappings; (mapping = *link) != NULL;
	    link = &mapping->next) {
		if (mapping->address == args->device_addr &&
		    mapping->size == args->size) {
			error = pvr_vm_unmap(vm->tables, args->device_addr,
			    args->size);
			if (error == 0)
				*link = mapping->next;
			break;
		}
	}
	mutex_exit(&pf->lock);
	if (error == 0) {
		drm_gem_object_put_unlocked(mapping->object);
		kmem_free(mapping, sizeof(*mapping));
	}
	return -error;
}
