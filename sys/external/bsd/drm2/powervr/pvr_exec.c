/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. AI-assisted native implementation. */
/* Origin: EmberBSD execution handles; Linux PowerVR MIT UAPI is unchanged. */
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/kauth.h>
#include <sys/kernel.h>
#include <sys/kmem.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <linux/idr.h>
#include <drm/drm_auth.h>
#include <drm/drm_device.h>
#include <drm/drm_file.h>
#include "pvr_backend.h"
#include "pvr_context.h"
#include "pvr_drm.h"
#include "pvr_exec.h"
#include "pvr_file.h"
#include "pvr_freelist.h"
#include "pvr_exec_hwrt.h"
#include "pvr_stream.h"

struct pvr_exec_context {
	struct pvr_context *firmware;
	struct pvr_file_vm *vm;
	unsigned int refs;
};
struct pvr_exec_freelist {
	struct pvr_freelist *firmware;
	struct pvr_file_vm *vm;
	struct pvr_file_pin pin;
	unsigned refs;
};
struct pvr_exec_file {
	/* All handle operations are serialized by the DRM device gate. */
	struct idr contexts;
	struct idr freelists;
	struct idr hwrts;
};

struct pvr_exec_file *
pvr_exec_open(void)
{
	struct pvr_exec_file *ef = kmem_zalloc(sizeof(*ef), KM_SLEEP);

	idr_init(&ef->contexts);
	idr_init(&ef->freelists);
	idr_init(&ef->hwrts);
	return ef;
}

static void *
pvr_exec_find(struct idr *handles, uint32_t handle)
{

	if (handle == 0 || handle > INT_MAX)
		return NULL;
	return idr_find(handles, (int)handle);
}

int
pvr_exec_insert(struct idr *handles, void *owner, uint32_t *out)
{
	int handle;

	idr_preload(GFP_KERNEL);
	handle = idr_alloc(handles, owner, 1, 0, GFP_NOWAIT);
	idr_preload_end();
	if (handle < 0)
		return -handle;
	*out = (uint32_t)handle;
	return 0;
}

struct pvr_exec_context *
pvr_exec_context_get(struct drm_file *file, uint32_t handle)
{
	struct pvr_exec_file *ef = pvr_file_execution(file);
	struct pvr_exec_context *ctx = pvr_exec_find(&ef->contexts, handle);

	if (ctx != NULL) {
		if (ctx->refs == UINT_MAX)
			return NULL;
		ctx->refs++;
	}
	return ctx;
}

void
pvr_exec_context_put(struct drm_device *dev, struct pvr_exec_context *ctx)
{
	unsigned attempt;
	int error = 0;

	KASSERT(ctx->refs != 0);
	if (--ctx->refs != 0)
		return;
	if (ctx->firmware != NULL) {
		for (attempt = 0; attempt < 100; attempt++) {
			error = pvr_context_destroy(&ctx->firmware);
			if (error != EBUSY)
				break;
			(void)kpause("pvrclean", false, MAX(1, hz / 100), NULL);
		}
	}
	if (error != 0) {
		/* Keep the VM, mappings and GEM objects reachable through ctx. */
		pvr_file_vm_fail(ctx->vm);
		pvr_device_retain(dev, ctx);
		return;
	}
	pvr_file_vm_put(ctx->vm);
	kmem_free(ctx, sizeof(*ctx));
}

void
pvr_exec_freelist_put(struct drm_device *dev, struct pvr_exec_freelist *fl)
{
	unsigned attempt;
	int error = 0;

	KASSERT(fl->refs != 0);
	if (--fl->refs != 0)
		return;
	if (fl->firmware != NULL) {
		for (attempt = 0; attempt < 100; attempt++) {
			error = pvr_freelist_destroy(&fl->firmware);
			if (error != EBUSY)
				break;
			(void)kpause("pvrflclean", false, MAX(1, hz / 100), NULL);
		}
	}
	if (error != 0) {
		pvr_file_vm_fail(fl->vm);
		pvr_device_retain(dev, fl);
		return;
	}
	if (fl->pin.mapping != NULL)
		pvr_file_unpin(&fl->pin);
	pvr_file_vm_put(fl->vm);
	kmem_free(fl, sizeof(*fl));
}

void
pvr_exec_close(struct drm_device *dev, struct pvr_exec_file *ef)
{
	struct pvr_exec_context *ctx;
	struct pvr_exec_freelist *fl;
	struct pvr_exec_hwrt *hwrt;
	int id;

	for (;;) {
		id = 0;
		ctx = idr_get_next(&ef->contexts, &id);
		if (ctx == NULL)
			break;
		idr_remove(&ef->contexts, id);
		pvr_exec_context_put(dev, ctx);
	}
	for (;;) {
		id = 0;
		fl = idr_get_next(&ef->freelists, &id);
		if (fl == NULL)
			break;
		idr_remove(&ef->freelists, id);
		pvr_exec_freelist_put(dev, fl);
	}
	for (;;) {
		id = 0;
		hwrt = idr_get_next(&ef->hwrts, &id);
		if (hwrt == NULL)
			break;
		idr_remove(&ef->hwrts, id);
		pvr_exec_hwrt_put(dev, hwrt);
	}
	idr_destroy(&ef->contexts);
	idr_destroy(&ef->freelists);
	idr_destroy(&ef->hwrts);
	kmem_free(ef, sizeof(*ef));
}

static int
pvr_create_context_locked(struct drm_device *dev,
    struct drm_pvr_ioctl_create_context_args *args, struct drm_file *file,
    const struct pvr_backend *be)
{
	struct pvr_exec_file *ef = pvr_file_execution(file);
	struct pvr_exec_context *ctx;
	struct pvr_context_config config;
	struct pvr_file_vm *vm;
	uint8_t state[PVR_STREAM_STATIC_RENDER_SIZE];
	uint32_t handle, id, fw_vm;
	size_t size;
	int error;

	if (args->flags != 0)
		return EOPNOTSUPP;
	memset(&config, 0, sizeof(config));
	switch (args->type) {
	case DRM_PVR_CTX_TYPE_RENDER:
		size = PVR_STREAM_STATIC_RENDER_SIZE;
		break;
	case DRM_PVR_CTX_TYPE_COMPUTE:
		size = PVR_STREAM_STATIC_COMPUTE_SIZE;
		break;
	case DRM_PVR_CTX_TYPE_TRANSFER_FRAG:
		size = 0;
		break;
	default:
		return EINVAL;
	}
	if (args->static_context_state_len != size ||
	    (args->type != DRM_PVR_CTX_TYPE_RENDER && args->callstack_addr != 0))
		return EINVAL;
	switch (args->priority) {
	case DRM_PVR_CTX_PRIORITY_LOW:
		config.priority = 0;
		break;
	case DRM_PVR_CTX_PRIORITY_NORMAL:
		config.priority = 1;
		break;
	case DRM_PVR_CTX_PRIORITY_HIGH:
		if (!drm_is_current_master(file) &&
		    kauth_authorize_generic(kauth_cred_get(),
		    KAUTH_GENERIC_ISSUSER, NULL) != 0)
			return EACCES;
		config.priority = 2;
		break;
	default:
		return EINVAL;
	}
	if (size != 0) {
		if (args->static_context_state == 0 ||
		    args->static_context_state > UINTPTR_MAX - size)
			return EFAULT;
		error = copyin((const void *)(uintptr_t)args->static_context_state,
		    state, size);
		if (error != 0)
			return error;
	}
	vm = pvr_file_vm_get(file, args->vm_context_handle);
	if (vm == NULL)
		return EINVAL;
	error = pvr_device_next_id(dev, &id);
	if (error == 0)
		error = pvr_file_vm_publish(vm, be, &fw_vm);
	if (error != 0) {
		pvr_file_vm_put(vm);
		return error;
	}
	ctx = kmem_zalloc(sizeof(*ctx), KM_SLEEP);
	ctx->vm = vm;
	ctx->refs = 1;
	config.type = args->type;
	config.id = id;
	config.pid = (uint32_t)curproc->p_pid;
	config.vm_address = fw_vm;
	config.callstack = args->callstack_addr;
	config.static_state = size != 0 ? state : NULL;
	config.static_size = size;
	error = pvr_context_create(be, &config, &ctx->firmware);
	if (error == 0)
		error = pvr_exec_insert(&ef->contexts, ctx, &handle);
	if (error != 0) {
		pvr_exec_context_put(dev, ctx);
		return error;
	}
	args->handle = handle;
	return 0;
}

int
pvr_create_context(struct drm_device *dev, void *data, struct drm_file *file)
{
	const struct pvr_backend *be = pvr_device_enter(dev);
	int error;

	error = be == NULL ? EOPNOTSUPP :
	    pvr_create_context_locked(dev, data, file, be);
	pvr_device_leave(dev);
	return -error;
}

int
pvr_destroy_context(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_pvr_ioctl_destroy_context_args *args = data;
	struct pvr_exec_file *ef = pvr_file_execution(file);
	struct pvr_exec_context *ctx;
	int error = 0;

	if (args->_padding_4 != 0)
		return -EINVAL;
	(void)pvr_device_enter(dev);
	ctx = pvr_exec_find(&ef->contexts, args->handle);
	if (ctx == NULL) {
		error = EINVAL;
	} else {
		idr_remove(&ef->contexts, (int)args->handle);
		pvr_exec_context_put(dev, ctx);
	}
	pvr_device_leave(dev);
	return -error;
}

static int
pvr_create_free_list_locked(struct drm_device *dev,
    struct drm_pvr_ioctl_create_free_list_args *args, struct drm_file *file,
    const struct pvr_backend *be)
{
	struct pvr_exec_file *ef = pvr_file_execution(file);
	struct pvr_exec_freelist *fl;
	struct pvr_freelist_config config;
	struct pvr_file_vm *vm;
	uint32_t id, handle;
	size_t size;
	int error;

	size = (size_t)args->max_num_pages * sizeof(uint32_t);
	if (args->max_num_pages == 0 ||
	    size / sizeof(uint32_t) != args->max_num_pages)
		return EINVAL;
	vm = pvr_file_vm_get(file, args->vm_context_handle);
	if (vm == NULL)
		return EINVAL;
	fl = kmem_zalloc(sizeof(*fl), KM_SLEEP);
	fl->vm = vm;
	fl->refs = 1;
	error = pvr_file_vm_pin(vm, args->free_list_gpu_addr, size,
	    DRM_PVR_BO_PM_FW_PROTECT, &fl->pin);
	if (error == 0)
		error = pvr_device_next_id(dev, &id);
	if (error != 0)
		goto fail;
	memset(&config, 0, sizeof(config));
	config.backing_cpu = fl->pin.cpu;
	config.backing_size = size;
	config.backing_pa = fl->pin.physical;
	config.gpu_va = args->free_list_gpu_addr;
	config.sync_cookie = &fl->pin;
	config.backing_sync = pvr_file_pin_sync;
	config.id = id;
	config.initial_pages = args->initial_num_pages;
	config.max_pages = args->max_num_pages;
	config.grow_pages = args->grow_num_pages;
	config.grow_threshold = args->grow_threshold;
	error = pvr_freelist_create(dev->dmat, be, &config, &fl->firmware);
	if (error == 0)
		error = pvr_exec_insert(&ef->freelists, fl, &handle);
	if (error != 0)
		goto fail;
	args->handle = handle;
	return 0;
fail:
	pvr_exec_freelist_put(dev, fl);
	return error;
}

int
pvr_create_free_list(struct drm_device *dev, void *data, struct drm_file *file)
{
	const struct pvr_backend *be = pvr_device_enter(dev);
	int error;

	error = be == NULL ? EOPNOTSUPP :
	    pvr_create_free_list_locked(dev, data, file, be);
	pvr_device_leave(dev);
	return -error;
}

int
pvr_destroy_free_list(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_pvr_ioctl_destroy_free_list_args *args = data;
	struct pvr_exec_file *ef = pvr_file_execution(file);
	struct pvr_exec_freelist *fl;
	int error = 0;

	if (args->_padding_4 != 0)
		return -EINVAL;
	(void)pvr_device_enter(dev);
	fl = pvr_exec_find(&ef->freelists, args->handle);
	if (fl == NULL) {
		error = EINVAL;
	} else {
		idr_remove(&ef->freelists, (int)args->handle);
		pvr_exec_freelist_put(dev, fl);
	}
	pvr_device_leave(dev);
	return -error;
}

struct pvr_file_vm *
pvr_exec_context_vm(struct pvr_exec_context *ctx)
{

	return ctx->vm;
}

struct pvr_context *
pvr_exec_context_firmware(struct pvr_exec_context *ctx)
{

	return ctx->firmware;
}

/* Handle lookup with a reference; requires the device/backend gate. */
struct pvr_exec_freelist *
pvr_exec_freelist_get(struct drm_file *file, uint32_t handle)
{
	struct pvr_exec_file *ef = pvr_file_execution(file);
	struct pvr_exec_freelist *fl = pvr_exec_find(&ef->freelists, handle);

	if (fl != NULL) {
		if (fl->refs == UINT_MAX)
			return NULL;
		fl->refs++;
	}
	return fl;
}

int
pvr_exec_freelist_info(const struct pvr_exec_freelist *fl,
    struct pvr_exec_freelist_info *info)
{

	if (fl == NULL || info == NULL)
		return EINVAL;
	info->vm = fl->vm;
	info->fw_address = pvr_freelist_fw_address(fl->firmware);
	info->initial_pages = fl->firmware->config.initial_pages;
	info->current_pages = pvr_freelist_current_pages(fl->firmware);
	info->max_pages = fl->firmware->config.max_pages;
	return 0;
}

struct pvr_file_vm *
pvr_exec_freelist_vm(const struct pvr_exec_freelist *fl)
{

	return fl->vm;
}

struct idr *
pvr_exec_hwrt_handles(struct drm_file *file)
{
	struct pvr_exec_file *ef = pvr_file_execution(file);

	return &ef->hwrts;
}
