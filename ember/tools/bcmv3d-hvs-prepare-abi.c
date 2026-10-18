/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* AI-assisted sampled configuration/layout preflight. Compile with the selected
 * kernel and actual module flags, then compare this ELF section. The sample is
 * not a source, symbol, option, DMA-address-path or runtime acceptance proof.
 */
#include <sys/param.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <drm/drm_drv.h>
#include <arm/broadcom/bcm2712_display_acpi.h>
#include "drm_internal.h"
#include "bcmv3d_drv.h"
#include "bcmv3d_hvs_prepare.h"

#define OFFSET(t, m) __builtin_offsetof(struct t, m)

const uint64_t bcmv3d_hvs_prepare_abi[]
    __attribute__((__used__, __section__(".rodata.bcmv3d_hvs_prepare_abi"))) = {
#ifdef CONFIG_ARM64
	1,
#else
	0,
#endif
#ifdef CONFIG_ARM
	1,
#else
	0,
#endif
#ifdef CONFIG_DRM_LEGACY
	1,
#else
	0,
#endif
	PAGE_SIZE,
	sizeof(deviter_t),
	_Alignof(deviter_t),
	sizeof(struct bcm2712_display_contract),
	_Alignof(struct bcm2712_display_contract),
	sizeof(void *),
	sizeof(bus_addr_t),
	sizeof(bus_size_t),
	sizeof(bus_dma_tag_t),
	sizeof(bus_dmamap_t),
	sizeof(kmutex_t),
	_Alignof(kmutex_t),
	BUS_DMA_WRITE,
	BUS_DMASYNC_PREWRITE,

	sizeof(struct drm_minor),
	OFFSET(drm_minor, type),
	OFFSET(drm_minor, dev),

	sizeof(struct drm_device),
	OFFSET(drm_device, dev),
	OFFSET(drm_device, dev_private),
	OFFSET(drm_device, driver),
	OFFSET(drm_device, registered),
	OFFSET(drm_device, render),

	sizeof(struct drm_driver),
	OFFSET(drm_driver, name),

	sizeof(struct acpi_devnode),
	OFFSET(acpi_devnode, ad_device),

	sizeof(struct bcmv3d_device),
	OFFSET(bcmv3d_device, drm),
	OFFSET(bcmv3d_device, dev),
	OFFSET(bcmv3d_device, dmat),
	OFFSET(bcmv3d_device, handle),
	OFFSET(bcmv3d_device, memory_lock),
	OFFSET(bcmv3d_device, allocated),
	OFFSET(bcmv3d_device, bo_total),
	OFFSET(bcmv3d_device, bo_stats),

	sizeof(struct bcmv3d_bo),
	OFFSET(bcmv3d_bo, cma),
	OFFSET(bcmv3d_bo, dmasegs),
	OFFSET(bcmv3d_bo, nsegs),
	OFFSET(bcmv3d_bo, maxsegs),
	OFFSET(bcmv3d_bo, gpu),
	OFFSET(bcmv3d_bo, busy),
	OFFSET(bcmv3d_bo, error),

	sizeof(struct drm_gem_cma_object),
	OFFSET(drm_gem_cma_object, base),
	OFFSET(drm_gem_cma_object, dmamap),
	OFFSET(drm_gem_cma_object, dmasize),
	OFFSET(drm_gem_cma_object, dmat),
	OFFSET(drm_gem_cma_object, vaddr),

	sizeof(struct drm_gem_object),
	OFFSET(drm_gem_object, dev),
	OFFSET(drm_gem_object, size),
	OFFSET(drm_gem_object, funcs),

	sizeof(struct bcmv3d_bo_stats),
	OFFSET(bcmv3d_bo_stats, reserved_bytes),
	OFFSET(bcmv3d_bo_stats, limit_bytes),

	sizeof(struct arm32_bus_dmamap),
	OFFSET(arm32_bus_dmamap, dm_mapsize),
	OFFSET(arm32_bus_dmamap, dm_nsegs),
	OFFSET(arm32_bus_dmamap, dm_segs),

	sizeof(struct arm32_bus_dma_segment),
	OFFSET(arm32_bus_dma_segment, ds_addr),
	OFFSET(arm32_bus_dma_segment, ds_len),

	sizeof(struct arm32_bus_dma_tag),
	OFFSET(arm32_bus_dma_tag, _dmamap_create),
	OFFSET(arm32_bus_dma_tag, _dmamap_destroy),
	OFFSET(arm32_bus_dma_tag, _dmamap_load),
	OFFSET(arm32_bus_dma_tag, _dmamap_unload),
	OFFSET(arm32_bus_dma_tag, _dmamap_sync_pre),

	sizeof(struct bcmv3d_hvs_frame),
	OFFSET(bcmv3d_hvs_frame, bo),
	OFFSET(bcmv3d_hvs_frame, dmat),
	OFFSET(bcmv3d_hvs_frame, map),
	OFFSET(bcmv3d_hvs_frame, vaddr),
	OFFSET(bcmv3d_hvs_frame, address),
	OFFSET(bcmv3d_hvs_frame, size),
	OFFSET(bcmv3d_hvs_frame, pitch)
};
