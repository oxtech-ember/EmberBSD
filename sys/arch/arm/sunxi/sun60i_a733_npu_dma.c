/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD, AI-assisted; native bus_dma ownership, no vendor code. */
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kmem.h>
#include <sys/systm.h>
#include <arm/sunxi/sun60i_a733_npu_mmu.h>
#include <arm/sunxi/sun60i_a733_npu_dma.h>

#define NPU_DMA_PAGE 4096
#define NPU_DMA_LIMIT (UINT64_C(1) << 32)
#define NPU_DMA_COMMAND_VA UINT32_C(0x01001000)
#define NPU_DMA_NBUFFERS 3

struct npu_dma_buffer {
	bus_dma_segment_t segment;
	bus_dmamap_t map;
	void *cpu;
	size_t bytes;
	bus_addr_t address;
	int nsegments;
	bool loaded;
};

struct sun60i_a733_npu_dma {
	bus_dma_tag_t tag;
	struct npu_dma_buffer buffers[NPU_DMA_NBUFFERS];
	size_t command_bytes;
	uint32_t pd_entry;
	bool retained;
};

static void
npu_dma_buffer_free(bus_dma_tag_t tag, struct npu_dma_buffer *b)
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
npu_dma_buffer_alloc(bus_dma_tag_t tag, size_t bytes, struct npu_dma_buffer *b)
{
	int error;

	b->bytes = bytes;
	error = bus_dmamem_alloc(tag, bytes, NPU_DMA_PAGE, 0, &b->segment,
	    1, &b->nsegments, BUS_DMA_WAITOK);
	if (error != 0) {
		b->nsegments = 0;
		return error;
	}
	if (b->nsegments != 1 || b->segment.ds_len != bytes) {
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
	error = bus_dmamap_load(tag, b->map, b->cpu, bytes, NULL, BUS_DMA_WAITOK);
	if (error != 0)
		goto fail;
	b->loaded = true;
	/* A bounce map must not replace the context's CPU/device page identity. */
	if ((b->map->_dm_flags & _BUS_DMAMAP_IS_BOUNCING) != 0) {
		error = EOPNOTSUPP;
		goto fail;
	}
	if (b->map->dm_nsegs != 1 || b->map->dm_mapsize != bytes ||
	    b->map->dm_segs[0].ds_len != bytes) {
		error = EINVAL;
		goto fail;
	}
	b->address = b->map->dm_segs[0].ds_addr;
	if ((b->address & (NPU_DMA_PAGE - 1)) != 0 ||
	    b->address >= NPU_DMA_LIMIT || bytes > NPU_DMA_LIMIT - b->address) {
		error = ERANGE;
		goto fail;
	}
	memset(b->cpu, 0, bytes);
	return 0;
fail:
	npu_dma_buffer_free(tag, b);
	return error;
}

int
sun60i_a733_npu_dma_destroy(struct sun60i_a733_npu_dma *m)
{
	if (m == NULL)
		return EINVAL;
	if (m->retained)
		return EBUSY;
	for (size_t i = NPU_DMA_NBUFFERS; i > 0; i--)
		npu_dma_buffer_free(m->tag, &m->buffers[i - 1]);
	bus_dmatag_destroy(m->tag);
	kmem_free(m, sizeof(*m));
	return 0;
}

int
sun60i_a733_npu_dma_prepare(bus_dma_tag_t parent, const void *commands,
    size_t bytes, struct sun60i_a733_npu_dma **out)
{
	static const size_t sizes[NPU_DMA_NBUFFERS] = {
		NPU_DMA_PAGE, A733_NPU_MMU_STLB_BYTES, NPU_DMA_PAGE
	};
	struct sun60i_a733_npu_dma *m;
	struct sun60i_a733_npu_mmu_page page;
	struct sun60i_a733_npu_mmu_plan plan;
	int error;

	if (parent == NULL || commands == NULL || out == NULL ||
	    bytes < 8 || bytes > NPU_DMA_PAGE || (bytes & 7) != 0 ||
	    bytes > UINTPTR_MAX - (uintptr_t)commands)
		return EINVAL;
	m = kmem_zalloc(sizeof(*m), KM_SLEEP);
	error = bus_dmatag_subregion(parent, 0, NPU_DMA_LIMIT - 1,
	    &m->tag, BUS_DMA_WAITOK);
	if (error != 0) {
		kmem_free(m, sizeof(*m));
		return error;
	}
	for (size_t i = 0; i < NPU_DMA_NBUFFERS; i++) {
		error = npu_dma_buffer_alloc(m->tag, sizes[i], &m->buffers[i]);
		if (error != 0)
			goto fail;
		for (size_t j = 0; j < i; j++) {
			if (m->buffers[i].address < m->buffers[j].address + sizes[j] &&
			    m->buffers[j].address < m->buffers[i].address + sizes[i]) {
				error = EEXIST;
				goto fail;
			}
		}
	}
	page = (struct sun60i_a733_npu_mmu_page){ m->buffers[2].address, 0 };
	plan = (struct sun60i_a733_npu_mmu_plan){
		.mtlb_physical = m->buffers[0].address,
		.stlb_physical = m->buffers[1].address,
		.virtual_base = NPU_DMA_COMMAND_VA, .pages = &page, .page_count = 1
	};
	error = sun60i_a733_npu_mmu_build(&plan, m->buffers[0].cpu, sizes[0],
	    m->buffers[1].cpu, sizes[1]);
	if (error != 0)
		goto fail;
	error = sun60i_a733_npu_mmu_pd_entry(m->buffers[0].address, &m->pd_entry);
	if (error != 0)
		goto fail;
	memcpy(m->buffers[2].cpu, commands, bytes);
	m->command_bytes = bytes;
	*out = m;
	return 0;
fail:
	(void)sun60i_a733_npu_dma_destroy(m);
	return error;
}

int
sun60i_a733_npu_dma_info(const struct sun60i_a733_npu_dma *m,
    struct sun60i_a733_npu_dma_info *out)
{
	if (m == NULL || out == NULL)
		return EINVAL;
	*out = (struct sun60i_a733_npu_dma_info){
		.mtlb_address = m->buffers[0].address,
		.stlb_address = m->buffers[1].address,
		.command_address = m->buffers[2].address, .pd_entry = m->pd_entry,
		.command_virtual = NPU_DMA_COMMAND_VA,
		.command_bytes = m->command_bytes, .retained = m->retained
	};
	return 0;
}

int
sun60i_a733_npu_dma_retain(struct sun60i_a733_npu_dma *m)
{
	if (m == NULL)
		return EINVAL;
	if (m->retained)
		return EBUSY;
	for (size_t i = 0; i < NPU_DMA_NBUFFERS; i++) {
		if (!m->buffers[i].loaded)
			return EINVAL;
	}
	/* Retain before the first sync and every later hardware write attempt. */
	m->retained = true;
	for (size_t i = 0; i < NPU_DMA_NBUFFERS; i++)
		bus_dmamap_sync(m->tag, m->buffers[i].map, 0, m->buffers[i].bytes,
		    BUS_DMASYNC_PREWRITE);
	return 0;
}
