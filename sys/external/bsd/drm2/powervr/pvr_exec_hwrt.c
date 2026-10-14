/* Origin: EmberBSD execution handles; Linux PowerVR MIT UAPI is unchanged. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. AI-assisted native interface. */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/kernel.h>
#include <sys/kmem.h>
#include <sys/mutex.h>

#include <linux/idr.h>

#include "pvr_backend.h"
#include "pvr_drv.h"
#include "pvr_drm.h"
#include "pvr_exec.h"
#include "pvr_exec_hwrt.h"
#include "pvr_file.h"
#include "pvr_freelist.h"
#include "pvr_hwrt.h"

/* Pinned profile: 16x16 tiles, parameter format version 2, 4x4 macrotiles. */
#define HWRT_TILE			16
#define HWRT_MACROTILES_XY		4
#define HWRT_GROUP			2
#define HWRT_REGION_HEADER_BYTES	6
#define HWRT_REGION_ALIGN		64
#define HWRT_REGION_STRIDE_UNIT		4096
#define HWRT_TAIL_POINTER		8
#define HWRT_TPC_CACHE_LINE		64
#define HWRT_PM_PAGE_SHIFT		12
#define HWRT_PM_PAGE_SIZE		(1U << HWRT_PM_PAGE_SHIFT)
#define HWRT_PT_PER_PAGE		0x200
#define HWRT_PD_PER_PAGE		0x200
#define HWRT_PC_PER_PAGE		0x400
#define HWRT_PM_SPACES			2
#define HWRT_MLIST_STRIDE		4
#define HWRT_VHEAP_BYTES		1536
#define HWRT_RTC_BYTES			768
#define HWRT_MTA_BYTES			128

struct pvr_exec_hwrt {
	struct pvr_hwrt *firmware;
	struct pvr_exec_freelist *local, *global;
	struct pvr_file_hold hold[2 /* rt */ * 3 + 3 /* geom */ + 2 /* fl-independent */];
	unsigned holds;
	unsigned refs;
};

/* Exact profile geometry from Mesa rogue_hw_utils: tiles per macrotile
 * already contains the sample factor; the grid is four by four. */
static void
hwrt_tiles(uint32_t width, uint32_t height, uint32_t samples,
    uint32_t *tiles_x, uint32_t *tiles_y)
{
	uint32_t sx, sy, tx, ty;

	switch (samples) {
	case 2: sx = 1; sy = 2; break;
	case 4: sx = 2; sy = 2; break;
	default: sx = 1; sy = 1; break;
	}
	tx = (width + HWRT_TILE - 1) / HWRT_TILE;
	ty = (height + HWRT_TILE - 1) / HWRT_TILE;
	tx = ((tx + 7) / 8) * 2 * sx;
	ty = ((ty + 7) / 8) * 2 * sy;
	*tiles_x = HWRT_MACROTILES_XY * tx;
	*tiles_y = HWRT_MACROTILES_XY * ty;
}

static uint32_t
hwrt_next_pow2(uint32_t v)
{

	if (v == 0)
		return 1;
	v--;
	v |= v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16;
	return v + 1;
}

static int
hwrt_spans(const struct drm_pvr_ioctl_create_hwrt_dataset_args *args,
    struct pvr_hwrt_config *config, uint64_t *span)
{
	uint32_t tiles_x, tiles_y, mtiles, tpc;

	hwrt_tiles(args->width, args->height, args->samples, &tiles_x,
	    &tiles_y);
	if (tiles_x == 0 || tiles_y == 0)
		return EINVAL;
	/* Region headers: one per 2x2 tile group, pinned byte size. */
	span[0] = ((uint64_t)(tiles_x / HWRT_GROUP) *
	    (tiles_y / HWRT_GROUP) * HWRT_REGION_HEADER_BYTES +
	    HWRT_REGION_ALIGN - 1) & ~(uint64_t)(HWRT_REGION_ALIGN - 1);
	if (span[0] == 0 || span[0] > UINT32_MAX)
		return EINVAL;
	config->region_header_size = (uint32_t)span[0];
	if (args->layers > 1) {
		span[0] = ((span[0] + HWRT_REGION_STRIDE_UNIT - 1) &
		    ~(uint64_t)(HWRT_REGION_STRIDE_UNIT - 1)) * args->layers;
		if (span[0] > UINT32_MAX)
			return EINVAL;
	}
	/* Tail pointer cache: next power of two squared, per 2x2 group. */
	mtiles = hwrt_next_pow2(tiles_x > tiles_y ? tiles_x : tiles_y);
	tpc = (uint64_t)mtiles * mtiles / (HWRT_GROUP * HWRT_GROUP) *
	    HWRT_TAIL_POINTER;
	if (tpc == 0)
		return EINVAL;
	config->tpc_size = tpc;
	if (args->layers > 1) {
		uint64_t layered = ((uint64_t)tpc + HWRT_PM_PAGE_SIZE - 1) &
		    ~(uint64_t)(HWRT_PM_PAGE_SIZE - 1);

		config->tpc_stride = (uint32_t)(layered / HWRT_PM_PAGE_SIZE);
		layered *= args->layers;
		if (layered > UINT32_MAX)
			return EINVAL;
		span[1] = layered;
	} else {
		config->tpc_stride = 0;
		span[1] = ((uint64_t)tpc + HWRT_TPC_CACHE_LINE - 1) &
		    ~(uint64_t)(HWRT_TPC_CACHE_LINE - 1);
	}
	if (args->geom_data_args.tpc_size != config->tpc_size ||
	    args->geom_data_args.tpc_stride != config->tpc_stride)
		return EINVAL;
	span[2] = HWRT_VHEAP_BYTES;
	span[3] = args->layers > 1 ? HWRT_RTC_BYTES : 0;
	span[4] = HWRT_MTA_BYTES;
	/* The MLIST size derives from both free lists below. */
	return 0;
}

static uint64_t
hwrt_mlist_bytes(uint32_t local_pages, uint32_t global_pages)
{
	uint64_t total, pte, pde, pce;

	/* Inputs are page counts; Mesa shifts byte sizes, we already have pages. */
	total = (uint64_t)local_pages + global_pages;
	pte = (total + HWRT_PT_PER_PAGE - 1) / HWRT_PT_PER_PAGE;
	pde = (pte + HWRT_PD_PER_PAGE - 1) / HWRT_PD_PER_PAGE;
	pce = (pde + HWRT_PC_PER_PAGE - 1) / HWRT_PC_PER_PAGE;
	total = (pte + pde + pce) * HWRT_PM_SPACES * HWRT_MLIST_STRIDE;
	return (total + HWRT_PM_PAGE_SIZE - 1) & ~(uint64_t)(HWRT_PM_PAGE_SIZE - 1);
}

static int
hwrt_hold_one(struct pvr_file_vm *vm, uint64_t addr, uint64_t size,
    uint64_t flags, struct pvr_file_hold *hold)
{

	if (addr == 0 || size == 0) {
		hold->mapping = NULL;
		return 0;
	}
	return pvr_file_vm_hold(vm, addr, size, flags, hold);
}

static void
hwrt_unhold_all(struct pvr_exec_hwrt *hwrt)
{
	unsigned i;

	for (i = 0; i < hwrt->holds; i++) {
		if (hwrt->hold[i].mapping != NULL)
			pvr_file_unhold(&hwrt->hold[i]);
		hwrt->hold[i].mapping = NULL;
	}
	hwrt->holds = 0;
}

static int
pvr_create_hwrt_locked(struct drm_device *dev,
    struct drm_pvr_ioctl_create_hwrt_dataset_args *args,
    struct drm_file *file, const struct pvr_backend *be)
{
	const uint64_t uncached = DRM_PVR_BO_BYPASS_DEVICE_CACHE;
	const uint64_t mlist_flags = DRM_PVR_BO_BYPASS_DEVICE_CACHE |
	    DRM_PVR_BO_PM_FW_PROTECT;
	struct pvr_exec_freelist_info local_info, global_info;
	const struct drm_pvr_create_hwrt_rt_data_args *rt;
	struct pvr_hwrt_config config;
	struct pvr_exec_hwrt *hwrt;
	struct pvr_file_vm *vm;
	uint64_t span[5], mlist;
	uint32_t bvnc, minpages;
	int error, i;

	if (args->handle != 0)
		return EINVAL;
	hwrt = kmem_zalloc(sizeof(*hwrt), KM_SLEEP);
	hwrt->refs = 1;
	/* Two distinct free-list objects from one VM keep that VM alive. */
	if (args->free_list_handles[DRM_PVR_HWRT_FREE_LIST_LOCAL] ==
	    args->free_list_handles[DRM_PVR_HWRT_FREE_LIST_GLOBAL]) {
		error = EINVAL;
		goto fail;
	}
	hwrt->local = pvr_exec_freelist_get(file,
	    args->free_list_handles[DRM_PVR_HWRT_FREE_LIST_LOCAL]);
	hwrt->global = pvr_exec_freelist_get(file,
	    args->free_list_handles[DRM_PVR_HWRT_FREE_LIST_GLOBAL]);
	if (hwrt->local == NULL || hwrt->global == NULL) {
		error = EINVAL;
		goto fail;
	}
	if (pvr_exec_freelist_info(hwrt->local, &local_info) != 0 ||
	    pvr_exec_freelist_info(hwrt->global, &global_info) != 0 ||
	    local_info.vm != global_info.vm ||
	    hwrt->local->firmware == hwrt->global->firmware) {
		error = EINVAL;
		goto fail;
	}
	vm = local_info.vm;
	if (pvr_device_hwrt_info(dev, &bvnc, &minpages) != 0 ||
	    local_info.initial_pages < minpages) {
		error = EINVAL;
		goto fail;
	}
	memset(&config, 0, sizeof(config));
	config.bvnc = bvnc;
	config.width = args->width;
	config.height = args->height;
	config.samples = args->samples;
	config.layers = args->layers;
	config.freelist_fw_addr[0] = local_info.fw_address;
	config.freelist_fw_addr[1] = global_info.fw_address;
	config.tpc_dev_addr = args->geom_data_args.tpc_dev_addr;
	config.vheap_table_dev_addr = args->geom_data_args.vheap_table_dev_addr;
	config.rtc_dev_addr = args->geom_data_args.rtc_dev_addr;
	config.isp_merge_lower_x = args->isp_merge_lower_x;
	config.isp_merge_lower_y = args->isp_merge_lower_y;
	config.isp_merge_upper_x = args->isp_merge_upper_x;
	config.isp_merge_upper_y = args->isp_merge_upper_y;
	config.isp_merge_scale_x = args->isp_merge_scale_x;
	config.isp_merge_scale_y = args->isp_merge_scale_y;
	config.local_pages = local_info.initial_pages;
	config.min_local_pages = minpages;
	mlist = hwrt_mlist_bytes(local_info.max_pages, global_info.max_pages);
	error = hwrt_spans(args, &config, span);
	if (error != 0)
		goto fail;
	for (i = 0; i < 2; i++) {
		rt = &args->rt_data_args[i];
		config.rt[i].pm_mlist_dev_addr = rt->pm_mlist_dev_addr;
		config.rt[i].macrotile_array_dev_addr =
		    rt->macrotile_array_dev_addr;
		config.rt[i].region_header_dev_addr = rt->region_header_dev_addr;
	}
	/* Holds follow the pinned cache policy of every span. */
	error = hwrt_hold_one(vm, config.tpc_dev_addr, span[1], uncached,
	    &hwrt->hold[hwrt->holds++]);
	if (error == 0)
		error = hwrt_hold_one(vm, config.vheap_table_dev_addr, span[2],
		    uncached, &hwrt->hold[hwrt->holds++]);
	if (error == 0)
		error = hwrt_hold_one(vm, config.rtc_dev_addr, span[3],
		    uncached, &hwrt->hold[hwrt->holds++]);
	for (i = 0; error == 0 && i < 2; i++) {
		error = hwrt_hold_one(vm,
		    args->rt_data_args[i].region_header_dev_addr, span[0],
		    uncached, &hwrt->hold[hwrt->holds++]);
		if (error == 0)
			error = hwrt_hold_one(vm,
			    args->rt_data_args[i].macrotile_array_dev_addr,
			    span[4], uncached, &hwrt->hold[hwrt->holds++]);
		if (error == 0)
			error = hwrt_hold_one(vm,
			    args->rt_data_args[i].pm_mlist_dev_addr,
			    mlist, mlist_flags, &hwrt->hold[hwrt->holds++]);
	}
	if (error != 0)
		goto fail;
	error = pvr_hwrt_create(be, &config, &hwrt->firmware);
	if (error != 0)
		goto fail;
	{
		struct idr *handles = pvr_exec_hwrt_handles(file);

		error = pvr_exec_insert(handles, hwrt, &args->handle);
	}
	if (error != 0)
		goto fail;
	return 0;
fail:
	hwrt_unhold_all(hwrt);
	if (hwrt->firmware != NULL)
		(void)pvr_hwrt_destroy(&hwrt->firmware);
	if (hwrt->local != NULL)
		pvr_exec_freelist_put(dev, hwrt->local);
	if (hwrt->global != NULL)
		pvr_exec_freelist_put(dev, hwrt->global);
	kmem_free(hwrt, sizeof(*hwrt));
	return error;
}

int
pvr_create_hwrt_dataset(struct drm_device *dev, void *data,
    struct drm_file *file)
{
	const struct pvr_backend *be = pvr_device_enter(dev);
	int error;

	error = be == NULL ? EOPNOTSUPP :
	    pvr_create_hwrt_locked(dev, data, file, be);
	pvr_device_leave(dev);
	return -error;
}

int
pvr_destroy_hwrt_dataset(struct drm_device *dev, void *data,
    struct drm_file *file)
{
	struct drm_pvr_ioctl_destroy_hwrt_dataset_args *args = data;
	struct idr *handles;
	struct pvr_exec_hwrt *hwrt;
	int error = 0;

	if (args->_padding_4 != 0)
		return -EINVAL;
	(void)pvr_device_enter(dev);
	handles = pvr_exec_hwrt_handles(file);
	hwrt = idr_find(handles, (int)args->handle);
	if (hwrt == NULL) {
		error = EINVAL;
	} else {
		idr_remove(handles, (int)args->handle);
		pvr_exec_hwrt_put(dev, hwrt);
	}
	pvr_device_leave(dev);
	return -error;
}

struct pvr_exec_hwrt *
pvr_exec_hwrt_get(struct drm_file *file, uint32_t handle)
{
	struct idr *handles = pvr_exec_hwrt_handles(file);
	struct pvr_exec_hwrt *hwrt = idr_find(handles, (int)handle);

	if (hwrt != NULL) {
		if (hwrt->refs == UINT_MAX)
			return NULL;
		hwrt->refs++;
	}
	return hwrt;
}

void
pvr_exec_hwrt_put(struct drm_device *dev, struct pvr_exec_hwrt *hwrt)
{
	int error = 0;

	KASSERT(hwrt->refs != 0);
	if (--hwrt->refs != 0)
		return;
	if (hwrt->firmware != NULL) {
		for (unsigned attempt = 0; attempt < 100; attempt++) {
			error = pvr_hwrt_destroy(&hwrt->firmware);
			if (error != EBUSY)
				break;
			(void)kpause("pvrhwrtclean", false, MAX(1, hz / 100),
			    NULL);
		}
	}
	if (error != 0) {
		/*
		 * The firmware never acknowledged cleanup: keep the owner,
		 * both free lists, their VM and every hold reachable through
		 * device orphan retention.
		 */
		pvr_file_vm_fail(pvr_exec_freelist_vm(hwrt->local));
		pvr_device_retain(dev, hwrt);
		return;
	}
	hwrt_unhold_all(hwrt);
	pvr_exec_freelist_put(dev, hwrt->local);
	pvr_exec_freelist_put(dev, hwrt->global);
	kmem_free(hwrt, sizeof(*hwrt));
}

struct pvr_file_vm *
pvr_exec_hwrt_vm(const struct pvr_exec_hwrt *hwrt)
{

	return pvr_exec_freelist_vm(hwrt->local);
}

int
pvr_exec_hwrt_target(struct pvr_exec_hwrt *hwrt, unsigned index,
    struct pvr_hwrt_target *target)
{

	if (hwrt == NULL || target == NULL || index > 1)
		return EINVAL;
	return pvr_hwrt_target(hwrt->firmware, index, target);
}
