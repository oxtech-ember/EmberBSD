/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _BCMV3D_HVS_PREPARE_H_
#define _BCMV3D_HVS_PREPARE_H_

#include <sys/bus.h>

#define BCMV3D_HVS_WIDTH 1920U
#define BCMV3D_HVS_HEIGHT 1080U
#define BCMV3D_HVS_PITCH 7680U
#define BCMV3D_HVS_BYTES 8294400U

struct bcmv3d_device;
struct bcmv3d_bo;

/* Private, unexposed preparation only. The caller uses process context,
 * serializes all access, and holds the render/device reference and consumer
 * DMA tag for the whole frame lifetime. No hardware may see this object.
 */
struct bcmv3d_hvs_frame {
	struct bcmv3d_bo *bo;
	bus_dma_tag_t dmat;
	bus_dmamap_t map;
	void *vaddr;
	bus_addr_t address;
	bus_size_t size;
	uint32_t pitch;
};

int bcmv3d_hvs_prepare(struct bcmv3d_device *, bus_dma_tag_t,
    struct bcmv3d_hvs_frame **);
void bcmv3d_hvs_prewrite(struct bcmv3d_hvs_frame *);
/* Invalid after any hardware publication attempt. This API proves no drain. */
void bcmv3d_hvs_destroy(struct bcmv3d_hvs_frame *);

#endif
