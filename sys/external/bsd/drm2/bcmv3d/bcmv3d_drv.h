/* Origin: EmberBSD native BCM2712 render ownership, 2026-10-11. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _BCMV3D_DRV_H_
#define _BCMV3D_DRV_H_

#include <sys/bus.h>
#include <sys/device.h>
#include <sys/mutex.h>
#include <sys/vmem.h>
#include <dev/acpi/acpivar.h>
#include <arm/broadcom/bcm2712_v3d_render.h>
#include <drm/drm_device.h>
#include <drm/drm_file.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/v3d_drm.h>
#include "bcmv3d_bo_stats.h"

#define BCMV3D_BO_MAX	(16 * 1024 * 1024)
/* Scale total BO storage with RAM: 1/16 of RAM, from 64 MiB to 256 MiB. */
#define BCMV3D_BO_TOTAL_MIN	((size_t)64 * 1024 * 1024)
#define BCMV3D_BO_TOTAL_MAX	((size_t)256 * 1024 * 1024)
#define BCMV3D_BO_TOTAL_SHIFT	4
#define BCMV3D_VA_START	UINT64_C(0x00100000)
#define BCMV3D_VA_SIZE	UINT64_C(0xffe00000)

struct bcmv3d_job;
struct bcmv3d_mmu4_native;
struct bcmv3d_vblank_native;
struct bcmv3d_scanout;
struct bcmv3d_kms;
struct bcmv3d_device {
	struct drm_device *drm;
	device_t dev;
	bus_dma_tag_t dmat;
	ACPI_HANDLE handle;
	kmutex_t memory_lock;
	vmem_t *va;
	size_t allocated;
	size_t bo_total;
	struct bcmv3d_bo_stats bo_stats;
	struct sysctllog *sysctllog;
	uint32_t ident[7];
	struct bcmv3d_job *job;
	bus_space_tag_t bst;
	struct bcmv3d_mmu4_native *mmu4_native;
	const struct bcmv3d_bootfb_owner *bootfb;
	struct bcmv3d_vblank_native *vblank_native;
	struct bcmv3d_scanout *scanout;
	struct bcmv3d_kms *kms;
};

/* Keep the CMA base layout; allocation, segments and pager are private. */
struct bcmv3d_bo {
	struct drm_gem_cma_object cma;
	bus_dma_segment_t *dmasegs;
	int nsegs, maxsegs;
	vmem_addr_t gpu;
	volatile unsigned int busy;
	volatile unsigned int error;
};

static inline struct bcmv3d_bo *
bcmv3d_to_bo(struct drm_gem_object *obj)
{

	return container_of(to_drm_gem_cma_obj(obj), struct bcmv3d_bo, cma);
}

extern const struct uvm_pagerops bcmv3d_bo_uvm_ops;

/* Verify native BO identity and acquire one actual GEM reference. */
int bcmv3d_bo_reference(struct bcmv3d_device *, struct drm_gem_object *,
    struct bcmv3d_bo **);

/* The engine page table and scratch require contiguous DMA storage. */
struct bcmv3d_bo *bcmv3d_bo_alloc(struct bcmv3d_device *, uint32_t);
int bcmv3d_create_bo(struct drm_device *, void *, struct drm_file *);
void bcmv3d_bo_snapshot(struct bcmv3d_device *, struct bcmv3d_bo_stats *);
int bcmv3d_mmap_bo(struct drm_device *, void *, struct drm_file *);
int bcmv3d_get_bo_offset(struct drm_device *, void *, struct drm_file *);
int bcmv3d_wait_bo(struct drm_device *, void *, struct drm_file *);
int bcmv3d_submit_tfu(struct drm_device *, void *, struct drm_file *);
int bcmv3d_submit_csd(struct drm_device *, void *, struct drm_file *);
int bcmv3d_submit_cl(struct drm_device *, void *, struct drm_file *);
int bcmv3d_job_init(struct bcmv3d_device *);
void bcmv3d_job_fini(struct bcmv3d_device *);

#endif
