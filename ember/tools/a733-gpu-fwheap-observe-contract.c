/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. AI-assisted source regression. */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sun60i_a733_gpu_fwcompute_file.h"

typedef void *bus_dma_tag_t;
struct sun60i_a733_gpu_dma { void *cpu, *map; };
struct sun60i_a733_gpu_fwmem;
#include "heap.h"

#define BUS_DMASYNC_POSTREAD 2
#define BUS_DMASYNC_PREREAD 1
static unsigned syncs, checks;
static bool delayed;
static uint32_t storage[A733_GPU_FWHEAP_BYTES / 4];
static uint8_t saved[A733_GPU_FWHEAP_BYTES];
#define CHECK(e) do { checks++; assert(e); } while (0)

void
bus_dmamap_sync(void *tag, void *map, size_t offset, size_t size, int op)
{
	CHECK(tag == storage && map == storage);
	CHECK(offset == (syncs % 2 == 0 ? 232U : 33040U));
	CHECK(size == (syncs % 2 == 0 ? 32U : 4U));
	CHECK(op == (syncs < 2 ? BUS_DMASYNC_POSTREAD : BUS_DMASYNC_PREREAD));
	if (delayed && op == BUS_DMASYNC_POSTREAD) {
		/* Distinct values appear only after the matching DMA acquisition. */
		if (offset == 232) {
			storage[232 / 4] = UINT32_C(0x12345678); /* write */
			storage[236 / 4] = UINT32_C(0x11223344); /* read */
		} else {
			storage[33040 / 4] = UINT32_C(0xabcdef01);
		}
		memcpy(saved + offset, (uint8_t *)storage + offset, size);
	}
	syncs++;
}

#include "observer.h"

int
main(void)
{
	struct sun60i_a733_gpu_fwheap heap = { 0 };
	uint32_t read, write, timeline;

	heap.dmat = storage;
	heap.heap.cpu = heap.heap.map = storage;
	for (unsigned mode = 0; mode < 2; mode++) {
		delayed = mode != 0;
		syncs = 0;
		memset(storage, 0xa5, sizeof(storage));
		storage[232 / 4] = UINT32_C(0x12345678);
		storage[236 / 4] = UINT32_C(0x11223344);
		storage[33040 / 4] = UINT32_C(0xabcdef01);
		if (delayed)
			storage[232 / 4] = storage[236 / 4] = storage[33040 / 4] = 0;
		memcpy(saved, storage, sizeof(saved));
		sun60i_a733_gpu_fwheap_observe(&heap, &read, &write, &timeline);
		CHECK(read == UINT32_C(0x11223344));
		CHECK(write == UINT32_C(0x12345678));
		CHECK(timeline == UINT32_C(0xabcdef01));
		CHECK(syncs == 4 && memcmp(saved, storage, sizeof(saved)) == 0);
	}
	syncs = 0;
	sun60i_a733_gpu_fwheap_observe(&heap, NULL, NULL, NULL);
	CHECK(syncs == 4);
	printf("PASS: %u real CCCB observer checks\n", checks);
	return 0;
}
