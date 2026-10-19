/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD, AI-assisted; native DE3.52 bus_dma consumer. */
#ifndef _SUN60I_A733_DE352_DMA_H
#define _SUN60I_A733_DE352_DMA_H

#include <sys/bus.h>
#include <arm/sunxi/sun60i_a733_de352.h>

struct sun60i_a733_de352_buffer {
	bus_dma_segment_t segment;
	bus_dmamap_t map;
	void *cpu;
	size_t bytes;
	bus_addr_t address;
	int nsegments;
	bool loaded;
};

struct sun60i_a733_de352_dma {
	bus_dma_tag_t tag, pixel_tag;
	struct sun60i_a733_de352_buffer pixels, queue;
	struct sun60i_a733_de352_rgb_words layer;
	uint32_t width, height, pitch;
	bool synchronized, published, external_pixels;
};

/*
 * The caller must prove the DE master uses this DMA tag. This allocator does
 * not establish IOMMU bypass. It builds one CPU color-bar plane and a fixed
 * two-descriptor RCQ list. It does not publish an address to hardware.
 * The caller serializes the object. All failures preserve *out.
 */
int sun60i_a733_de352_dma_prepare(bus_dma_tag_t, uint32_t, uint32_t,
    struct sun60i_a733_de352_dma **);
/*
 * Build the same two-descriptor list for a caller-owned loaded map. The caller
 * pins the map and storage before this call and keeps both immutable. The
 * allocator owns only its queue and child tag. It never unloads the pixel map.
 * This profile accepts a single segment below 4 GiB and an offset of zero.
 */
int sun60i_a733_de352_dma_prepare_map(bus_dma_tag_t, bus_dmamap_t, size_t,
    uint32_t, uint32_t, uint32_t, struct sun60i_a733_de352_dma **);
/*
 * The native coordinator may retire an external frame only after a later
 * accepted RCQ list and two fresh TV0 observations. This function does not
 * establish that proof. It releases only the owned queue and child tag.
 */
int sun60i_a733_de352_dma_retire(struct sun60i_a733_de352_dma *);
/* Synchronize both maps, then mark before the first address-register write. */
int sun60i_a733_de352_dma_publish(struct sun60i_a733_de352_dma *);
/* No destruction after publication, including failed or timed-out writes. */
int sun60i_a733_de352_dma_destroy(struct sun60i_a733_de352_dma *);

/* Retained zeroed 16 KiB V2 IOMMU directory. No translated entries exist. */
struct sun60i_a733_de352_guard {
	bus_dma_tag_t tag;
	struct sun60i_a733_de352_buffer directory;
	bool published;
};
int sun60i_a733_de352_guard_prepare(bus_dma_tag_t,
    struct sun60i_a733_de352_guard **);
int sun60i_a733_de352_guard_publish(struct sun60i_a733_de352_guard *);
int sun60i_a733_de352_guard_destroy(struct sun60i_a733_de352_guard *);

#endif
