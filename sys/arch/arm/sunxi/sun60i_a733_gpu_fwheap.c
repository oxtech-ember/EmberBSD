/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD; A733 firmware heap compatibility adapter. */
/* Copyright (c) 2026 EmberBSD contributors. */
#include "sun60i_a733_gpu_fwheap.h"

int
sun60i_a733_gpu_dma_alloc_coherent(bus_dma_tag_t t, size_t n,
    struct sun60i_a733_gpu_dma *d)
{
	return pvr_heap_dma_alloc_coherent(t, n, d);
}

void
sun60i_a733_gpu_dma_free(bus_dma_tag_t t, struct sun60i_a733_gpu_dma *d)
{
	pvr_heap_dma_free(t, d);
}

int
sun60i_a733_gpu_fwheap_prepare(bus_dma_tag_t t, struct sun60i_a733_gpu_fwmem *m,
    const struct sun60i_a733_gpu_fwcompute_image *i,
    struct sun60i_a733_gpu_fwheap **h)
{
	return pvr_fwheap_prepare(t, m, i, h);
}

int
sun60i_a733_gpu_fwheap_map(struct sun60i_a733_gpu_fwheap *h,
    struct sun60i_a733_gpu_fwmem *m)
{
	return pvr_fwheap_map(h, m);
}

int
sun60i_a733_gpu_fwheap_set_vm_context(struct sun60i_a733_gpu_fwheap *h,
    const uint8_t c[32])
{
	return pvr_fwheap_set_vm_context(h, c);
}

void
sun60i_a733_gpu_fwheap_observe(const struct sun60i_a733_gpu_fwheap *h,
    uint32_t *r, uint32_t *w, uint32_t *t)
{
	pvr_fwheap_observe(h, r, w, t);
}

void
sun60i_a733_gpu_fwheap_destroy(struct sun60i_a733_gpu_fwheap **h)
{
	pvr_fwheap_destroy(h);
}

int
sun60i_a733_gpu_fwvm_prepare(bus_dma_tag_t t,
    const struct a733_gpu_fwcompute_file *f, const uint8_t *b,
    struct sun60i_a733_gpu_fwheap *h, struct sun60i_a733_gpu_fwvm **v)
{
	return pvr_fwvm_prepare(t, f, b, h, v);
}

void
sun60i_a733_gpu_fwvm_destroy(struct sun60i_a733_gpu_fwvm **v)
{
	pvr_fwvm_destroy(v);
}
