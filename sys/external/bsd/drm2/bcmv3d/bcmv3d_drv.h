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
#include <drm/drm_device.h>
#include <drm/drm_file.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/v3d_drm.h>

#define BCMV3D_BO_MAX	(16 * 1024 * 1024)
#define BCMV3D_BO_TOTAL	(64 * 1024 * 1024)
#define BCMV3D_VA_START	UINT64_C(0x00100000)
#define BCMV3D_VA_SIZE	UINT64_C(0xffe00000)

struct bcmv3d_job;
struct bcmv3d_device {
	struct drm_device *drm;
	device_t dev;
	bus_dma_tag_t dmat;
	ACPI_HANDLE handle;
	kmutex_t memory_lock;
	vmem_t *va;
	size_t allocated;
	uint32_t ident[7];
	struct bcmv3d_job *job;
};

/* CMA is first for the existing native UVM pager; allocation/free are ours. */
struct bcmv3d_bo {
	struct drm_gem_cma_object cma;
	vmem_addr_t gpu;
	volatile unsigned int busy;
	volatile unsigned int error;
};

static inline struct bcmv3d_bo *
bcmv3d_to_bo(struct drm_gem_object *obj)
{

	return container_of(to_drm_gem_cma_obj(obj), struct bcmv3d_bo, cma);
}

struct bcmv3d_bo *bcmv3d_bo_alloc(struct bcmv3d_device *, uint32_t);
int bcmv3d_create_bo(struct drm_device *, void *, struct drm_file *);
int bcmv3d_mmap_bo(struct drm_device *, void *, struct drm_file *);
int bcmv3d_get_bo_offset(struct drm_device *, void *, struct drm_file *);
int bcmv3d_wait_bo(struct drm_device *, void *, struct drm_file *);
int bcmv3d_submit_tfu(struct drm_device *, void *, struct drm_file *);
int bcmv3d_submit_csd(struct drm_device *, void *, struct drm_file *);
int bcmv3d_submit_cl(struct drm_device *, void *, struct drm_file *);
int bcmv3d_job_init(struct bcmv3d_device *);
void bcmv3d_job_fini(struct bcmv3d_device *);
int bcmv3d_render_attach(device_t, bus_dma_tag_t, ACPI_HANDLE);

#endif
