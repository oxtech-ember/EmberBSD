/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: original stopped-only native DMA allocator, AI-assisted. */
#include <sys/param.h>
#include <sys/bus.h>
#include <sys/cpu.h>
#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/systm.h>
#include <dev/ic/cixvpu_dma.h>

#define CIXVPU_DMA_LIMIT UINT64_C(0xffffffffff)

struct cixvpu_dma_owner {
	struct cixvpu_fw_plan_page page;
	bus_dma_segment_t allocation;
	bus_dmamap_t map;
	void *kva;
	bus_addr_t bus_address;
	bool allocated, mapped, created, loaded;
};

struct cixvpu_dma {
	bus_dma_tag_t tag;
	bool tag_acquired;
	uint32_t count;
	size_t bytes;
	struct cixvpu_dma_owner owners[];
};

static int
cixvpu_dma_context(void)
{

	return cpu_intr_p() || cpu_softintr_p() ? EWOULDBLOCK : 0;
}

static int
cixvpu_dma_record(const struct cixvpu_fw_plan_page *page, uint32_t index)
{

	if (page->backing_index != index ||
	    page->role < CIXVPU_FW_PLAN_TEXT ||
	    page->role > CIXVPU_FW_PLAN_LEAF ||
	    page->attribute > CIXVPU_MMU_SHARED_RW ||
	    page->permission < CIXVPU_MMU_READ ||
	    page->permission > CIXVPU_MMU_WRITE ||
	    page->image_bytes > CIXVPU_FW_PAGE_SIZE)
		return EINVAL;
	return 0;
}

static void
cixvpu_dma_release(struct cixvpu_dma *dma)
{
	struct cixvpu_dma_owner *owner;
	uint32_t i;

	for (i = dma->count; i != 0; i--) {
		owner = &dma->owners[i - 1];
		if (owner->loaded)
			bus_dmamap_unload(dma->tag, owner->map);
		if (owner->created)
			bus_dmamap_destroy(dma->tag, owner->map);
		if (owner->mapped)
			bus_dmamem_unmap(dma->tag, owner->kva,
			    CIXVPU_FW_PAGE_SIZE);
		if (owner->allocated)
			bus_dmamem_free(dma->tag, &owner->allocation, 1);
	}
	if (dma->tag_acquired)
		bus_dmatag_destroy(dma->tag);
	kmem_free(dma, dma->bytes);
}

static int
cixvpu_dma_load(struct cixvpu_dma *dma, struct cixvpu_dma_owner *owner)
{
	const bus_dma_segment_t *segment;
	int error, nalloc, flags;

	error = bus_dmamem_alloc(dma->tag, CIXVPU_FW_PAGE_SIZE,
	    CIXVPU_FW_PAGE_SIZE, 0, &owner->allocation, 1, &nalloc,
	    BUS_DMA_WAITOK);
	if (error != 0)
		return error;
	owner->allocated = true;
	/*
	 * A successful native call with capacity one owns this one-entry list.
	 * A different reported count violates that API contract. Reject it, but
	 * keep cleanup bounded to the original capacity, never the bad count.
	 */
	if (nalloc != 1)
		return EIO;
	flags = owner->page.role >= CIXVPU_FW_PLAN_MSG_HOST &&
	    owner->page.role <= CIXVPU_FW_PLAN_PRINT ? BUS_DMA_COHERENT : 0;
	error = bus_dmamem_map(dma->tag, &owner->allocation, 1,
	    CIXVPU_FW_PAGE_SIZE, &owner->kva, flags);
	if (error != 0)
		return error;
	owner->mapped = true;
	error = bus_dmamap_create(dma->tag, CIXVPU_FW_PAGE_SIZE, 1,
	    CIXVPU_FW_PAGE_SIZE, CIXVPU_FW_PAGE_SIZE, BUS_DMA_WAITOK,
	    &owner->map);
	if (error != 0)
		return error;
	owner->created = true;
	/* Linear load preserves the CPU VA for noncoherent sync and bounce. */
	error = bus_dmamap_load(dma->tag, owner->map, owner->kva,
	    CIXVPU_FW_PAGE_SIZE, NULL, BUS_DMA_WAITOK);
	if (error != 0)
		return error;
	owner->loaded = true;
	/*
	 * Each owner must fit one PTE. Multiple owners may be discontiguous.
	 * Reject bad counts before touching the variable-sized segment array.
	 * The child tag can limit PA rather than translated bus addresses, so
	 * validate every accepted loaded segment independently of allocation PA.
	 */
	if (owner->map->dm_nsegs != 1 ||
	    owner->map->dm_mapsize != CIXVPU_FW_PAGE_SIZE)
		return EIO;
	segment = &owner->map->dm_segs[0];
	if (segment->ds_len != CIXVPU_FW_PAGE_SIZE ||
	    segment->ds_addr % CIXVPU_FW_PAGE_SIZE != 0 ||
	    segment->ds_addr > CIXVPU_DMA_LIMIT ||
	    segment->ds_len - 1 > CIXVPU_DMA_LIMIT - segment->ds_addr)
		return ERANGE;
	owner->bus_address = segment->ds_addr;
	/* Still CPU-owned. No PRE sync or device handoff has occurred. */
	memset(owner->kva, 0, CIXVPU_FW_PAGE_SIZE);
	return 0;
}

int
cixvpu_dma_alloc(bus_dma_tag_t dmat, bus_dma_tag_t dmat64,
    const struct cixvpu_fw_plan *plan, struct cixvpu_dma **out)
{
	struct cixvpu_fw_plan snapshot;
	struct cixvpu_fw_plan_page page;
	struct cixvpu_dma *dma;
	bus_dma_tag_t parent;
	size_t bytes, count;
	uint32_t i, j;
	int error;

	error = cixvpu_dma_context();
	if (error != 0)
		return error;
	if (plan == NULL || out == NULL)
		return EINVAL;
	if (*out != NULL)
		return EBUSY;
	if (PAGE_SIZE != CIXVPU_FW_PAGE_SIZE)
		return EOPNOTSUPP;
	parent = dmat64 != NULL ? dmat64 : dmat;
	if (parent == NULL)
		return EINVAL;
	snapshot = *plan;
	/* Validate the copied snapshot and all records before any allocation. */
	error = cixvpu_fw_plan_page(&snapshot, 0, &page);
	if (error != 0)
		return error;
	count = snapshot.backing_pages;
	if (count == 0 ||
	    snapshot.backing_pages > CIXVPU_FW_PLAN_MAX_PAGES ||
	    count >
	    (SIZE_MAX - sizeof(*dma)) / sizeof(dma->owners[0]))
		return E2BIG;
	for (i = 0; i < snapshot.backing_pages; i++) {
		error = cixvpu_fw_plan_page(&snapshot, i, &page);
		if (error == 0)
			error = cixvpu_dma_record(&page, i);
		if (error != 0)
			return error;
	}
	bytes = sizeof(*dma) + snapshot.backing_pages * sizeof(dma->owners[0]);
	dma = kmem_zalloc(bytes, KM_SLEEP);
	if (dma == NULL)
		return ENOMEM;
	dma->bytes = bytes;
	dma->count = snapshot.backing_pages;
	for (i = 0; i < dma->count; i++) {
		error = cixvpu_fw_plan_page(&snapshot, i, &dma->owners[i].page);
		if (error != 0)
			goto fail;
	}
	error = bus_dmatag_subregion(parent, 0, CIXVPU_DMA_LIMIT,
	    &dma->tag, BUS_DMA_WAITOK);
	if (error != 0)
		goto fail;
	dma->tag_acquired = true;
	for (i = 0; i < dma->count; i++) {
		error = cixvpu_dma_load(dma, &dma->owners[i]);
		if (error != 0)
			goto fail;
		/* Distinct owners must not alias through translated DMA ranges. */
		for (j = 0; j < i; j++) {
			if (dma->owners[j].bus_address ==
			    dma->owners[i].bus_address) {
				error = EADDRINUSE;
				goto fail;
			}
		}
	}
	*out = dma;
	return 0;
fail:
	cixvpu_dma_release(dma);
	return error;
}

int
cixvpu_dma_page(struct cixvpu_dma *dma, unsigned int index,
    struct cixvpu_dma_page *out)
{
	struct cixvpu_dma_page page = { 0 };
	int error;

	error = cixvpu_dma_context();
	if (error != 0)
		return error;
	if (dma == NULL || out == NULL)
		return EINVAL;
	if (index >= dma->count)
		return ERANGE;
	page.kva = dma->owners[index].kva;
	page.bus_address = dma->owners[index].bus_address;
	page.plan_page = dma->owners[index].page;
	*out = page;
	return 0;
}

int
cixvpu_dma_free(struct cixvpu_dma **allocation)
{
	int error;

	error = cixvpu_dma_context();
	if (error != 0)
		return error;
	if (allocation == NULL || *allocation == NULL)
		return EINVAL;
	cixvpu_dma_release(*allocation);
	*allocation = NULL;
	return 0;
}
