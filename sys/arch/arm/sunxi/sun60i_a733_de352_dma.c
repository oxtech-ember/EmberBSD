/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD, AI-assisted. Native DMA allocation and retention.
 * Uses the existing EmberBSD bus_dma lifecycle. No vendor code is copied.
 */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");
#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kmem.h>
#include <sys/systm.h>
#include <arm/sunxi/sun60i_a733_de352_dma.h>

#define DE352_DMA_PAGE 4096
#define DE352_DMA_LIMIT (UINT64_C(1) << 32)
#define DE352_DMA_QUEUE_BYTES 4096
#define DE352_DMA_LAYER_OFFSET 32
#define DE352_DMA_BLEND_OFFSET 64

static void
de352_buffer_free(bus_dma_tag_t tag, struct sun60i_a733_de352_buffer *b)
{
	if (b->loaded)
		bus_dmamap_unload(tag, b->map);
	if (b->map != NULL)
		bus_dmamap_destroy(tag, b->map);
	if (b->cpu != NULL)
		bus_dmamem_unmap(tag, b->cpu, b->bytes);
	if (b->nsegments != 0)
		bus_dmamem_free(tag, &b->segment, b->nsegments);
	memset(b, 0, sizeof(*b));
}

static int
de352_buffer_alloc(bus_dma_tag_t tag, size_t bytes,
    struct sun60i_a733_de352_buffer *b)
{
	int error;

	b->bytes = bytes;
	error = bus_dmamem_alloc(tag, bytes, DE352_DMA_PAGE, 0, &b->segment,
	    1, &b->nsegments, BUS_DMA_WAITOK);
	if (error != 0) {
		b->nsegments = 0;
		return error;
	}
	if (b->nsegments != 1) {
		error = EINVAL;
		goto fail;
	}
	error = bus_dmamem_map(tag, &b->segment, 1, bytes, &b->cpu,
	    BUS_DMA_WAITOK);
	if (error != 0) {
		b->cpu = NULL;
		goto fail;
	}
	error = bus_dmamap_create(tag, bytes, 1, bytes, 0, BUS_DMA_WAITOK,
	    &b->map);
	if (error != 0) {
		b->map = NULL;
		goto fail;
	}
	error = bus_dmamap_load(tag, b->map, b->cpu, bytes, NULL,
	    BUS_DMA_WAITOK);
	if (error != 0)
		goto fail;
	b->loaded = true;
	if (b->map->dm_nsegs != 1 || b->map->dm_segs[0].ds_len != bytes) {
		error = EINVAL;
		goto fail;
	}
	b->address = b->map->dm_segs[0].ds_addr;
	if ((b->address & (DE352_DMA_PAGE - 1)) != 0 ||
	    b->address >= DE352_DMA_LIMIT || bytes > DE352_DMA_LIMIT - b->address) {
		error = ERANGE;
		goto fail;
	}
	memset(b->cpu, 0, bytes);
	return 0;
fail:
	de352_buffer_free(tag, b);
	return error;
}

static void
de352_put32(uint8_t *p, uint32_t value)
{
	for (u_int i = 0; i < 4; i++)
		p[i] = (uint8_t)(value >> (8 * i));
}

int
sun60i_a733_de352_dma_destroy(struct sun60i_a733_de352_dma *m)
{
	if (m == NULL)
		return EINVAL;
	if (m->published)
		return EBUSY;
	de352_buffer_free(m->tag, &m->queue);
	de352_buffer_free(m->tag, &m->pixels);
	bus_dmatag_destroy(m->tag);
	kmem_free(m, sizeof(*m));
	return 0;
}

int
sun60i_a733_de352_dma_prepare(bus_dma_tag_t parent, uint32_t width,
    uint32_t height, struct sun60i_a733_de352_dma **out)
{
	static const uint32_t colors[] = {
		0x00ffffff, 0x00ffff00, 0x0000ffff, 0x0000ff00,
		0x00ff00ff, 0x00ff0000, 0x000000ff, 0x00000000
	};
	struct sun60i_a733_de352_dma *m;
	struct sun60i_a733_de352_rgb rgb;
	struct sun60i_a733_de352_rcq_block block;
	uint32_t words[7];
	uint8_t *p;
	size_t bytes;
	int error;

	if (parent == NULL || out == NULL || width == 0 || width > 1920 ||
	    height == 0 || height > 1080)
		return EINVAL;
	m = kmem_zalloc(sizeof(*m), KM_SLEEP);
	m->width = width;
	m->height = height;
	m->pitch = (width * 4 + 63) & ~UINT32_C(63);
	bytes = ((size_t)m->pitch * height + DE352_DMA_PAGE - 1) &
	    ~(size_t)(DE352_DMA_PAGE - 1);
	error = bus_dmatag_subregion(parent, 0, DE352_DMA_LIMIT - 1,
	    &m->tag, BUS_DMA_WAITOK);
	if (error != 0) {
		kmem_free(m, sizeof(*m));
		return error;
	}
	error = de352_buffer_alloc(m->tag, bytes, &m->pixels);
	if (error != 0)
		goto fail;
	error = de352_buffer_alloc(m->tag, DE352_DMA_QUEUE_BYTES, &m->queue);
	if (error != 0)
		goto fail;
	if (m->pixels.address < m->queue.address + m->queue.bytes &&
	    m->queue.address < m->pixels.address + m->pixels.bytes) {
		error = EEXIST;
		goto fail;
	}
	rgb = (struct sun60i_a733_de352_rgb) {
		.width = width, .height = height, .pitch = m->pitch,
		.dma = m->pixels.address, .mapped_bytes = m->pixels.bytes
	};
	error = sun60i_a733_de352_rgb_encode(&rgb, &m->layer);
	if (error != 0)
		goto fail;
	for (uint32_t y = 0; y < height; y++) {
		p = (uint8_t *)m->pixels.cpu + (size_t)y * m->pitch;
		for (uint32_t x = 0; x < width; x++)
			de352_put32(p + x * 4, colors[(x * 8) / width]);
	}
	words[0] = m->layer.attribute;
	words[1] = m->layer.size;
	words[2] = m->layer.coordinate;
	words[3] = m->layer.pitch;
	words[4] = m->layer.address_low;
	words[5] = m->layer.bottom_low;
	words[6] = m->layer.fill_color;
	p = (uint8_t *)m->queue.cpu + DE352_DMA_LAYER_OFFSET;
	for (u_int i = 0; i < __arraycount(words); i++)
		de352_put32(p + i * 4, words[i]);
	/* The native submitter must first establish exclusive pipe ownership. */
	de352_put32((uint8_t *)m->queue.cpu + DE352_DMA_BLEND_OFFSET, 0x100);
	block = (struct sun60i_a733_de352_rcq_block) {
		.dma = m->queue.address + DE352_DMA_LAYER_OFFSET,
		.bytes = sizeof(words), .destination = 0x1c1000, .dirty = true
	};
	error = sun60i_a733_de352_rcq_encode(&block, m->queue.cpu);
	if (error != 0)
		goto fail;
	block.dma = m->queue.address + DE352_DMA_BLEND_OFFSET;
	block.bytes = 4;
	block.destination = 0x281000;
	error = sun60i_a733_de352_rcq_encode(&block,
	    (uint8_t *)m->queue.cpu + A733_DE352_RCQ_DESCRIPTOR_BYTES);
	if (error != 0)
		goto fail;
	*out = m;
	return 0;
fail:
	(void)sun60i_a733_de352_dma_destroy(m);
	return error;
}

int
sun60i_a733_de352_dma_publish(struct sun60i_a733_de352_dma *m)
{
	if (m == NULL || !m->pixels.loaded || !m->queue.loaded)
		return EINVAL;
	if (m->published)
		return EBUSY;
	bus_dmamap_sync(m->tag, m->pixels.map, 0, m->pixels.bytes,
	    BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(m->tag, m->queue.map, 0, m->queue.bytes,
	    BUS_DMASYNC_PREWRITE);
	m->synchronized = true;
	/* No later error authorizes freeing either map. */
	m->published = true;
	return 0;
}
