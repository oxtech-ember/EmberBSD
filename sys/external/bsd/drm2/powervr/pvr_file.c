/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD per-file PowerVR VM ownership.
 * UAPI: Linux 7b63ef2d55f24519e7e9e5f4d15dbea03f126e40, MIT alternative.
 * Firmware publication retains VM tables and every mapped GEM object.
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
#include "pvr_backend.h"
#include "pvr_file.h"
#include "pvr_exec.h"
#include <arm/sunxi/powervr/pvr_rogue_fwif.h>

struct pvr_file_mapping {
	struct pvr_file_mapping *next;
	struct drm_gem_object *object;
	uint64_t address, size, offset;
	unsigned int pins;
};

struct pvr_file_vm {
	struct drm_device *dev;
	const struct pvr_backend *backend;
	struct pvr_fw_object firmware;
	unsigned int refs;
	bool published, failed;
	struct pvr_vm *tables;
	struct pvr_file_mapping *mappings;
};

struct pvr_file {
	struct pvr_exec_file *execution;
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
	pf->execution = pvr_exec_open();
	file->driver_priv = pf;
	return 0;
}

void
pvr_file_vm_put(struct pvr_file_vm *vm)
{
	struct pvr_file_mapping *mapping;
	int error = 0;

	KASSERT(vm->refs != 0);
	if (--vm->refs != 0)
		return;
	/* The last reference excludes contexts, jobs and free lists. */
	if (!vm->failed && vm->published)
		error = pvr_vm_quiesce(vm->tables);
	if (error == 0 && !vm->failed && vm->firmware.owner != NULL)
		error = (vm->backend->free)(vm->backend->cookie, &vm->firmware);
	if (error != 0 || vm->failed) {
		/* No reset is implied by a file close or a failed invalidate. */
		pvr_device_retain(vm->dev, vm);
		return;
	}
	pvr_vm_destroy(vm->tables);
	while ((mapping = vm->mappings) != NULL) {
		KASSERT(mapping->pins == 0);
		vm->mappings = mapping->next;
		drm_gem_object_put_unlocked(mapping->object);
		kmem_free(mapping, sizeof(*mapping));
	}
	kmem_free(vm, sizeof(*vm));
}

static void
pvr_file_close_locked(struct drm_device *dev, struct drm_file *file)
{
	struct pvr_file *pf = file->driver_priv;
	struct pvr_file_vm *vm;
	int id;

	pvr_exec_close(dev, pf->execution);
	/* DRM calls postclose after all file operations and GEM handle release. */
	for (;;) {
		id = 0;
		vm = idr_get_next(&pf->vms, &id);
		if (vm == NULL)
			break;
		idr_remove(&pf->vms, id);
		pvr_file_vm_put(vm);
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

static int
pvr_create_vm_context_locked(struct drm_device *dev, void *data, struct drm_file *file)
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
	vm->dev = dev;
	vm->refs = 1;
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
		pvr_file_vm_put(vm);
		return handle;
	}
	args->handle = (uint32_t)handle;
	return 0;
}

static int
pvr_destroy_vm_context_locked(struct drm_device *dev, void *data, struct drm_file *file)
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
	pvr_file_vm_put(vm);
	return 0;
}

static int
pvr_vm_map_ioctl_locked(struct drm_device *dev, void *data, struct drm_file *file)
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
			mapping->offset = args->offset;
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

static int
pvr_vm_unmap_ioctl_locked(struct drm_device *dev, void *data, struct drm_file *file)
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
			if (mapping->pins != 0) {
				error = EBUSY;
				break;
			}
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

struct pvr_file_vm *
pvr_file_vm_get(struct drm_file *file, uint32_t handle)
{
	struct pvr_file *pf = file->driver_priv;
	struct pvr_file_vm *vm;

	mutex_enter(&pf->lock);
	vm = pvr_file_vm_find(pf, handle);
	if (vm != NULL) {
		if (vm->refs == UINT_MAX)
			vm = NULL;
		else
			vm->refs++;
	}
	mutex_exit(&pf->lock);
	return vm;
}

static int
pvr_file_vm_invalidate(void *cookie)
{
	struct pvr_file_vm *vm = cookie;

	return vm->backend->flush(vm->backend->cookie);
}

int
pvr_file_vm_publish(struct pvr_file_vm *vm, const struct pvr_backend *backend,
    uint32_t *address)
{
	struct rogue_fwif_fwmemcontext *fw;
	int error;

	if (backend == NULL || address == NULL)
		return EOPNOTSUPP;
	if (vm->failed)
		return EIO;
	if (vm->published) {
		*address = vm->firmware.address;
		return 0;
	}
	vm->backend = backend;
	error = backend->alloc(backend->cookie, sizeof(*fw), &vm->firmware);
	if (error != 0) {
		/* A transport error can retain an allocated firmware mapping. */
		vm->failed = vm->firmware.owner != NULL;
		return error;
	}
	fw = vm->firmware.cpu;
	fw->pc_dev_paddr = pvr_vm_root_pa(vm->tables);
	fw->page_cat_base_reg_set = UINT32_MAX;
	backend->sync(backend->cookie, &vm->firmware,
	    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);
	vm->published = true;
	error = pvr_vm_publish(vm->tables, pvr_file_vm_invalidate, vm);
	if (error != 0) {
		vm->failed = true;
		return error;
	}
	*address = vm->firmware.address;
	return 0;
}

/* Caller holds the device and platform gates. Output failure is atomic. */
int
pvr_file_vm_objects(struct pvr_file_vm *vm, struct drm_gem_object ***out,
    size_t *countp)
{
	struct pvr_file_mapping *mapping;
	struct drm_gem_object **objects;
	size_t count = 0, i = 0;

	if (vm == NULL || out == NULL || countp == NULL || *out != NULL)
		return EINVAL;
	if (vm->failed)
		return EIO;
	for (mapping = vm->mappings; mapping != NULL; mapping = mapping->next) {
		if (count == INT_MAX || count == SIZE_MAX / sizeof(objects[0]))
			return E2BIG;
		count++;
	}
	if (count == 0) {
		*countp = 0;
		return 0;
	}
	objects = kmem_alloc(count * sizeof(objects[0]), KM_NOSLEEP);
	if (objects == NULL)
		return ENOMEM;
	for (mapping = vm->mappings; mapping != NULL; mapping = mapping->next) {
		drm_gem_object_get(mapping->object);
		objects[i++] = mapping->object;
	}
	KASSERT(i == count);
	*out = objects;
	*countp = count;
	return 0;
}

/* The referenced array is sorted by reservation address. Duplicates are safe. */
bool
pvr_file_vm_objects_cover(struct pvr_file_vm *vm,
    struct drm_gem_object * const *objects, size_t count)
{
	struct pvr_file_mapping *mapping;
	size_t first, end, middle;
	uintptr_t wanted, candidate;

	if (vm == NULL || vm->failed || (objects == NULL && count != 0))
		return false;
	for (mapping = vm->mappings; mapping != NULL; mapping = mapping->next) {
		wanted = (uintptr_t)mapping->object->resv;
		first = 0;
		end = count;
		while (first < end) {
			middle = first + (end - first) / 2;
			candidate = (uintptr_t)objects[middle]->resv;
			if (candidate < wanted)
				first = middle + 1;
			else
				end = middle;
		}
		if (first == count || objects[first]->resv != mapping->object->resv)
			return false;
	}
	return true;
}

int
pvr_file_vm_prepare(struct pvr_file_vm *vm)
{
	struct pvr_file_mapping *mapping;
	int error;

	if (vm->failed || !vm->published)
		return EIO;
	error = pvr_vm_flush(vm->tables);
	if (error != 0) {
		vm->failed = true;
		return error;
	}
	for (mapping = vm->mappings; mapping != NULL; mapping = mapping->next)
		pvr_bo_sync(mapping->object,
		    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);
	return 0;
}

void
pvr_file_vm_complete(struct pvr_file_vm *vm)
{
	struct pvr_file_mapping *mapping;

	for (mapping = vm->mappings; mapping != NULL; mapping = mapping->next)
		pvr_bo_sync(mapping->object,
		    BUS_DMASYNC_POSTWRITE | BUS_DMASYNC_POSTREAD);
}

void
pvr_file_vm_fail(struct pvr_file_vm *vm)
{

	vm->failed = true;
	if (vm->backend != NULL)
		vm->backend->fail(vm->backend->cookie);
}

int
pvr_file_vm_buffer(struct pvr_file_vm *vm, uint64_t address, size_t size,
    uint64_t required_flags, void **cpu)
{
	struct pvr_file_mapping *mapping;
	uint64_t offset;

	if (vm->failed || size == 0)
		return EINVAL;
	for (mapping = vm->mappings; mapping != NULL; mapping = mapping->next) {
		if (address < mapping->address)
			continue;
		offset = address - mapping->address;
		if (offset < mapping->size && size <= mapping->size - offset)
			return pvr_bo_cpu(mapping->object, mapping->offset + offset,
			    size, required_flags, cpu);
	}
	return ENOENT;
}

void
pvr_file_close(struct drm_device *dev, struct drm_file *file)
{

	(void)pvr_device_enter(dev);
	pvr_file_close_locked(dev, file);
	pvr_device_leave(dev);
}

int
pvr_create_vm_context(struct drm_device *dev, void *data, struct drm_file *file)
{
	int error;

	(void)pvr_device_enter(dev);
	error = pvr_create_vm_context_locked(dev, data, file);
	pvr_device_leave(dev);
	return error;
}

int
pvr_destroy_vm_context(struct drm_device *dev, void *data, struct drm_file *file)
{
	int error;

	(void)pvr_device_enter(dev);
	error = pvr_destroy_vm_context_locked(dev, data, file);
	pvr_device_leave(dev);
	return error;
}

int
pvr_vm_map_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
	int error;

	(void)pvr_device_enter(dev);
	error = pvr_vm_map_ioctl_locked(dev, data, file);
	pvr_device_leave(dev);
	return error;
}

int
pvr_vm_unmap_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
	int error;

	(void)pvr_device_enter(dev);
	error = pvr_vm_unmap_ioctl_locked(dev, data, file);
	pvr_device_leave(dev);
	return error;
}

int
pvr_file_vm_pin(struct pvr_file_vm *vm, uint64_t address, size_t size,
    uint64_t flags, struct pvr_file_pin *pin)
{
	struct pvr_file_mapping *mapping;
	uint64_t offset, pa, pte_flags;
	void *cpu;
	int error;

	if (pin == NULL || pin->mapping != NULL || vm->failed || size == 0)
		return EINVAL;
	for (mapping = vm->mappings; mapping != NULL; mapping = mapping->next) {
		if (address < mapping->address)
			continue;
		offset = address - mapping->address;
		if (offset >= mapping->size || size > mapping->size - offset)
			continue;
		/* Firmware free-list arrays need exclusive backing storage. */
		if (mapping->pins != 0)
			return EBUSY;
		offset += mapping->offset;
		error = pvr_bo_cpu(mapping->object, offset, size, flags, &cpu);
		if (error != 0)
			return error;
		error = pvr_bo_mapping(mapping->object, 0, mapping->object->size,
		    &pa, &pte_flags);
		if (error != 0)
			return error;
		error = pvr_bo_pin(mapping->object);
		if (error != 0)
			return error;
		mapping->pins++;
		pin->mapping = mapping;
		pin->cpu = cpu;
		pin->physical = pa + offset;
		return 0;
	}
	return ENOENT;
}

void
pvr_file_pin_sync(void *cookie, int operations)
{
	struct pvr_file_pin *pin = cookie;
	struct pvr_file_mapping *mapping = pin->mapping;

	KASSERT(mapping != NULL && mapping->pins != 0);
	pvr_bo_sync(mapping->object, operations);
}

void
pvr_file_unpin(struct pvr_file_pin *pin)
{
	struct pvr_file_mapping *mapping = pin->mapping;

	KASSERT(mapping != NULL && mapping->pins != 0);
	pvr_bo_unpin(mapping->object);
	mapping->pins--;
	memset(pin, 0, sizeof(*pin));
}

int
pvr_file_vm_hold(struct pvr_file_vm *vm, uint64_t address, size_t size,
    uint64_t flags, struct pvr_file_hold *hold)
{
	struct pvr_file_mapping *mapping;
	uint64_t offset;
	void *cpu;
	int error;

	if (hold == NULL || hold->mapping != NULL || vm->failed || size == 0)
		return EINVAL;
	for (mapping = vm->mappings; mapping != NULL; mapping = mapping->next) {
		if (address < mapping->address)
			continue;
		offset = address - mapping->address;
		if (offset >= mapping->size || size > mapping->size - offset)
			continue;
		if (mapping->pins == UINT_MAX)
			return EOVERFLOW;
		error = pvr_bo_cpu(mapping->object, mapping->offset + offset,
		    size, flags, &cpu);
		if (error != 0)
			return error;
		error = pvr_bo_hold(mapping->object);
		if (error != 0)
			return error;
		mapping->pins++;
		hold->mapping = mapping;
		return 0;
	}
	return ENOENT;
}

void
pvr_file_unhold(struct pvr_file_hold *hold)
{
	struct pvr_file_mapping *mapping = hold->mapping;

	KASSERT(mapping != NULL && mapping->pins != 0);
	pvr_bo_unhold(mapping->object);
	mapping->pins--;
	hold->mapping = NULL;
}

struct pvr_exec_file *
pvr_file_execution(struct drm_file *file)
{
	struct pvr_file *pf = file->driver_priv;

	return pf->execution;
}
