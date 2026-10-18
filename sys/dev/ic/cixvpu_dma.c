/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: original stopped-only native DMA allocator, AI-assisted. */
#include <sys/param.h>
#include <sys/atomic.h>
#include <sys/bus.h>
#include <sys/cpu.h>
#include <sys/endian.h>
#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/systm.h>
#include <dev/ic/cixvpu_dma.h>
#include <dev/ic/cixvpu_dma_private.h>

#define CIXVPU_DMA_LIMIT UINT64_C(0xffffffffff)

struct cixvpu_dma_owner {
	struct cixvpu_fw_plan_page page;
	bus_dma_segment_t allocation;
	bus_dmamap_t map;
	void *kva;
	bus_addr_t bus_address;
	bool allocated, mapped, created, loaded;
	int pre_ops;
};

enum cixvpu_dma_phase {
	CIXVPU_DMA_ALLOC,
	CIXVPU_DMA_SEALED,
	CIXVPU_DMA_SYNCED,
	CIXVPU_DMA_EXPOSED
};

struct cixvpu_dma {
	bus_dma_tag_t tag;
	bool tag_acquired;
	bool ever_visible;
	bool rpc_alloc_used, rpc_resize_used, rpc_resize_committed;
	bool rpc_second_alloc_used;
	unsigned int rpc_debug_count;
	enum cixvpu_dma_phase phase;
	const struct cixvpu_prepared *prepared_owner;
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

	switch (page->role) {
	case CIXVPU_FW_PLAN_TEXT:
	case CIXVPU_FW_PLAN_PRIVATE:
	case CIXVPU_FW_PLAN_SHARED:
	case CIXVPU_FW_PLAN_MSG_HOST:
	case CIXVPU_FW_PLAN_MSG_MVE:
	case CIXVPU_FW_PLAN_BUF_IN_HOST:
	case CIXVPU_FW_PLAN_BUF_IN_MVE:
	case CIXVPU_FW_PLAN_BUF_OUT_HOST:
	case CIXVPU_FW_PLAN_BUF_OUT_MVE:
	case CIXVPU_FW_PLAN_RPC:
	case CIXVPU_FW_PLAN_PRINT:
	case CIXVPU_FW_PLAN_RPC_DATA:
	case CIXVPU_FW_PLAN_ROOT:
	case CIXVPU_FW_PLAN_LEAF:
	case CIXVPU_FW_PLAN_RPC_SECOND_DATA:
	case CIXVPU_FW_PLAN_FRAME_Y:
	case CIXVPU_FW_PLAN_FRAME_UV:
	case CIXVPU_FW_PLAN_BITSTREAM:
		break;
	default:
		return EINVAL;
	}
	if (page->backing_index != index ||
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
	if (dma->ever_visible || dma->phase != CIXVPU_DMA_ALLOC)
		return EBUSY;
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
	if ((*allocation)->ever_visible ||
	    (*allocation)->phase != CIXVPU_DMA_ALLOC)
		return EBUSY;
	cixvpu_dma_release(*allocation);
	*allocation = NULL;
	return 0;
}

static int
cixvpu_dma_owned(const struct cixvpu_dma *dma,
    const struct cixvpu_prepared *owner, enum cixvpu_dma_phase phase)
{

	if (dma == NULL || owner == NULL)
		return EINVAL;
	if (dma->ever_visible || dma->phase != phase ||
	    dma->prepared_owner != owner)
		return EBUSY;
	return 0;
}

int
cixvpu_dma_seal(struct cixvpu_dma *dma,
    const struct cixvpu_prepared *owner)
{
	int error;

	error = cixvpu_dma_context();
	if (error != 0)
		return error;
	if (dma == NULL || owner == NULL)
		return EINVAL;
	if (dma->ever_visible || dma->phase != CIXVPU_DMA_ALLOC ||
	    dma->prepared_owner != NULL)
		return EBUSY;
	dma->prepared_owner = owner;
	dma->phase = CIXVPU_DMA_SEALED;
	return 0;
}

static int
cixvpu_dma_initial_ops(enum cixvpu_fw_plan_role role)
{

	switch (role) {
	case CIXVPU_FW_PLAN_TEXT:
	case CIXVPU_FW_PLAN_MSG_HOST:
	case CIXVPU_FW_PLAN_BUF_IN_HOST:
	case CIXVPU_FW_PLAN_BUF_OUT_HOST:
	case CIXVPU_FW_PLAN_LEAF:
	case CIXVPU_FW_PLAN_ROOT:
	case CIXVPU_FW_PLAN_FRAME_Y:
	case CIXVPU_FW_PLAN_FRAME_UV:
		return BUS_DMASYNC_PREWRITE;
	case CIXVPU_FW_PLAN_PRIVATE:
	case CIXVPU_FW_PLAN_SHARED:
	case CIXVPU_FW_PLAN_MSG_MVE:
	case CIXVPU_FW_PLAN_BUF_IN_MVE:
	case CIXVPU_FW_PLAN_BUF_OUT_MVE:
	case CIXVPU_FW_PLAN_RPC:
	case CIXVPU_FW_PLAN_PRINT:
	case CIXVPU_FW_PLAN_RPC_DATA:
	case CIXVPU_FW_PLAN_RPC_SECOND_DATA:
	case CIXVPU_FW_PLAN_BITSTREAM:
		return BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD;
	default:
		return 0;
	}
}

static bool
cixvpu_dma_page_equal(const struct cixvpu_fw_plan_page *a,
    const struct cixvpu_fw_plan_page *b)
{

	return a->backing_index == b->backing_index && a->role == b->role &&
	    a->core == b->core && a->ordinal == b->ordinal &&
	    a->image_offset == b->image_offset && a->image_bytes == b->image_bytes &&
	    a->leaf_address == b->leaf_address && a->attribute == b->attribute &&
	    a->permission == b->permission;
}

/* All recoverable checks precede the first native void sync or cleanup. */
static int
cixvpu_dma_sync_check(struct cixvpu_dma *dma,
    const struct cixvpu_fw_plan *plan, bool synced)
{
	struct cixvpu_fw_plan snapshot;
	struct cixvpu_fw_plan_page expected;
	const struct cixvpu_dma_owner *p;
	uint32_t i, roots = 0;
	int error;

	if (dma->tag == NULL || !dma->tag_acquired || dma->count == 0 ||
	    dma->count > CIXVPU_FW_PLAN_MAX_PAGES)
		return EINVAL;
	if (dma->bytes != sizeof(*dma) +
	    (size_t)dma->count * sizeof(dma->owners[0]))
		return EINVAL;
	if (PAGE_SIZE != CIXVPU_FW_PAGE_SIZE)
		return EOPNOTSUPP;
	if (plan != NULL) {
		snapshot = *plan;
		error = cixvpu_fw_plan_page(&snapshot, 0, &expected);
		if (error != 0)
			return error;
		if (snapshot.backing_pages != dma->count)
			return EINVAL;
	}
	for (i = 0; i < dma->count; i++) {
		const bus_dma_segment_t *segment;
		int ops;

		p = &dma->owners[i];
		error = cixvpu_dma_record(&p->page, i);
		if (error != 0)
			return error;
		if (plan != NULL) {
			error = cixvpu_fw_plan_page(&snapshot, i, &expected);
			if (error != 0)
				return error;
			if (!cixvpu_dma_page_equal(&p->page, &expected))
				return EINVAL;
		}
		ops = cixvpu_dma_initial_ops(p->page.role);
		if (ops == 0 || p->pre_ops != (synced ? ops : 0) ||
		    !p->allocated || !p->mapped || !p->created || !p->loaded ||
		    p->kva == NULL || p->map == NULL)
			return EINVAL;
		if (p->map->dm_nsegs != 1 ||
		    p->map->dm_mapsize != CIXVPU_FW_PAGE_SIZE)
			return EIO;
		segment = &p->map->dm_segs[0];
		if (segment->ds_len != CIXVPU_FW_PAGE_SIZE ||
		    segment->ds_addr != p->bus_address ||
		    segment->ds_addr % CIXVPU_FW_PAGE_SIZE != 0 ||
		    segment->ds_addr > CIXVPU_DMA_LIMIT ||
		    segment->ds_len - 1 > CIXVPU_DMA_LIMIT - segment->ds_addr)
			return ERANGE;
		if (p->page.role == CIXVPU_FW_PLAN_ROOT)
			roots++;
	}
	return roots == 1 ? 0 : EINVAL;
}

int
cixvpu_dma_sync_initial(struct cixvpu_dma *dma,
    const struct cixvpu_prepared *owner, const struct cixvpu_fw_plan *plan)
{
	struct cixvpu_dma_owner *p;
	uint32_t i;
	unsigned int pass;
	int error, ops;

	error = cixvpu_dma_context();
	if (error != 0)
		return error;
	if (plan == NULL)
		return EINVAL;
	error = cixvpu_dma_owned(dma, owner, CIXVPU_DMA_SEALED);
	if (error != 0)
		return error;
	error = cixvpu_dma_sync_check(dma, plan, false);
	if (error != 0)
		return error;
	/* No fallible work remains. Each unique map is synchronized once. */
	for (pass = 0; pass < 3; pass++) {
		for (i = 0; i < dma->count; i++) {
			int group;

			p = &dma->owners[i];
			group = p->page.role == CIXVPU_FW_PLAN_ROOT ? 2 :
			    p->page.role == CIXVPU_FW_PLAN_LEAF ? 1 : 0;
			if ((unsigned int)group != pass)
				continue;
			ops = cixvpu_dma_initial_ops(p->page.role);
			bus_dmamap_sync(dma->tag, p->map, 0,
			    CIXVPU_FW_PAGE_SIZE, ops);
			p->pre_ops = ops;
		}
	}
	dma->phase = CIXVPU_DMA_SYNCED;
	return 0;
}

int
cixvpu_dma_free_prepared(struct cixvpu_dma **allocation,
    const struct cixvpu_prepared *owner)
{
	struct cixvpu_dma *dma;
	struct cixvpu_dma_owner *p;
	int error;

	error = cixvpu_dma_context();
	if (error != 0)
		return error;
	if (allocation == NULL || *allocation == NULL || owner == NULL)
		return EINVAL;
	dma = *allocation;
	if (dma->ever_visible || dma->prepared_owner != owner ||
	    (dma->phase != CIXVPU_DMA_SEALED && dma->phase != CIXVPU_DMA_SYNCED))
		return EBUSY;
	error = cixvpu_dma_sync_check(dma, NULL,
	    dma->phase == CIXVPU_DMA_SYNCED);
	if (error != 0)
		return error;
	if (dma->phase == CIXVPU_DMA_SYNCED) {
		uint32_t i;

		/* No address was disclosed. Cancel PRE ownership before unloading. */
		for (i = dma->count; i != 0; i--) {
			int ops;

			p = &dma->owners[i - 1];
			ops = BUS_DMASYNC_POSTWRITE;
			if (p->pre_ops & BUS_DMASYNC_PREREAD)
				ops |= BUS_DMASYNC_POSTREAD;
			bus_dmamap_sync(dma->tag, p->map, 0,
			    CIXVPU_FW_PAGE_SIZE, ops);
			p->pre_ops = 0;
		}
	}
	cixvpu_dma_release(dma);
	*allocation = NULL;
	return 0;
}

int
cixvpu_dma_mark_exposed(struct cixvpu_dma *dma,
    const struct cixvpu_prepared *owner)
{
	int error;

	error = cixvpu_dma_context();
	if (error != 0)
		return error;
	error = cixvpu_dma_owned(dma, owner, CIXVPU_DMA_SYNCED);
	if (error != 0)
		return error;
	/* Mark before root capability publication. Never undo this transition. */
	dma->ever_visible = true;
	dma->phase = CIXVPU_DMA_EXPOSED;
	return 0;
}

static bool
cixvpu_dma_overlap(const void *a, size_t an, const void *b, size_t bn)
{
	uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;

	return x <= y ? y - x < an : x - y < bn;
}

/* Reject known retained storage, not arbitrary private native-cookie memory. */
int
cixvpu_dma_boot_output(struct cixvpu_dma *dma,
    const struct cixvpu_prepared *owner, const void *out, size_t size, bool *fault)
{
	uint32_t i;
	int error;

	error = cixvpu_dma_context();
	if (error != 0)
		return error;
	if (fault == NULL)
		return EINVAL;
	*fault = false;
	if (dma == NULL || owner == NULL || out == NULL || size == 0 ||
	    size > UINTPTR_MAX - (uintptr_t)out)
		return EINVAL;
	if (dma->prepared_owner != owner ||
	    (dma->phase != CIXVPU_DMA_SYNCED && dma->phase != CIXVPU_DMA_EXPOSED))
		return EBUSY;
	error = cixvpu_dma_sync_check(dma, NULL, true);
	if (error != 0) {
		*fault = true;
		return error;
	}
	if (dma->bytes > UINTPTR_MAX - (uintptr_t)dma ||
	    cixvpu_dma_overlap(out, size, dma, dma->bytes))
		return EINVAL;
	for (i = 0; i < dma->count; i++) {
		const struct cixvpu_dma_owner *p = &dma->owners[i];

		/* Maps were created with exactly one segment of retained capacity. */
		if (CIXVPU_FW_PAGE_SIZE > UINTPTR_MAX - (uintptr_t)p->kva ||
		    sizeof(*p->map) > UINTPTR_MAX - (uintptr_t)p->map ||
		    cixvpu_dma_overlap(out, size, p->kva, CIXVPU_FW_PAGE_SIZE) ||
		    cixvpu_dma_overlap(out, size, p->map, sizeof(*p->map)))
			return EINVAL;
	}
	return 0;
}

static int
cixvpu_dma_boot_pages(struct cixvpu_dma *dma,
    const struct cixvpu_prepared *owner, const struct cixvpu_fw_plan *plan,
    struct cixvpu_dma_owner **host, struct cixvpu_dma_owner **mve,
    struct cixvpu_dma_owner **rpc)
{
	uint32_t i;
	int error;

	error = cixvpu_dma_context();
	if (error != 0)
		return error;
	if (dma == NULL || owner == NULL || plan == NULL)
		return EINVAL;
	if (!dma->ever_visible || dma->phase != CIXVPU_DMA_EXPOSED ||
	    dma->prepared_owner != owner)
		return EBUSY;
	error = cixvpu_dma_sync_check(dma, plan, true);
	if (error != 0)
		return error;
	*host = *mve = *rpc = NULL;
	for (i = 0; i < dma->count; i++) {
		switch (dma->owners[i].page.role) {
		case CIXVPU_FW_PLAN_MSG_HOST:
			*host = &dma->owners[i];
			break;
		case CIXVPU_FW_PLAN_MSG_MVE:
			*mve = &dma->owners[i];
			break;
		case CIXVPU_FW_PLAN_RPC:
			*rpc = &dma->owners[i];
			break;
		default: break;
		}
	}
	return *host != NULL && *mve != NULL && *rpc != NULL ? 0 : EINVAL;
}

int
cixvpu_dma_boot_debugparam(struct cixvpu_dma *dma,
    const struct cixvpu_prepared *owner, const struct cixvpu_fw_plan *plan,
    struct cixvpu_dma_boot_snapshot *snapshot)
{
	struct cixvpu_dma_owner *host, *mve, *rpc;
	uint8_t before[12];
	uint32_t state, call, size;
	unsigned int attempt;
	int error;
	bool fault;

	error = cixvpu_dma_boot_pages(dma, owner, plan, &host, &mve, &rpc);
	if (error != 0)
		return error;
	error = cixvpu_dma_boot_output(dma, owner, snapshot,
	    sizeof(*snapshot), &fault);
	if (error != 0)
		return error;
	if ((uintptr_t)rpc->kva % _Alignof(uint32_t) != 0)
		return EINVAL;
	for (attempt = 0; attempt < 4; attempt++) {
		bus_dmamap_sync(dma->tag, rpc->map, 0, sizeof(before),
		    BUS_DMASYNC_POSTREAD);
		memcpy(before, rpc->kva, sizeof(before));
		bus_dmamap_sync(dma->tag, rpc->map, 0, sizeof(before),
		    BUS_DMASYNC_PREREAD);
		bus_dmamap_sync(dma->tag, rpc->map, 0, sizeof(before),
		    BUS_DMASYNC_POSTREAD);
		memcpy(snapshot->rpc_header, rpc->kva, sizeof(before));
		snapshot->rpc_valid = true;
		if (memcmp(before, snapshot->rpc_header, sizeof(before)) == 0)
			break;
		bus_dmamap_sync(dma->tag, rpc->map, 0, sizeof(before),
		    BUS_DMASYNC_PREREAD);
	}
	if (attempt == 4)
		return EAGAIN;
	state = le32dec(snapshot->rpc_header);
	call = le32dec(snapshot->rpc_header + 4);
	size = le32dec(snapshot->rpc_header + 8);
	if (state == 0 || state == 2)
		error = 0;
	else if (state != 1 || (call == 1 && size > 1012))
		error = EPROTO;
	else if (call != 1)
		error = ENOTSUP;
	else {
		/* Committed PARAM grants ownership until the RETURN store. */
		bus_dmamap_sync(dma->tag, rpc->map, 0, 4,
		    BUS_DMASYNC_POSTWRITE);
		/* Re-arm unchanged words before state becomes CPU-dirty. */
		bus_dmamap_sync(dma->tag, rpc->map, 4, 8,
		    BUS_DMASYNC_PREREAD);
		atomic_store_release((uint32_t *)rpc->kva, htole32(2));
		bus_dmamap_sync(dma->tag, rpc->map, 0, 4,
		    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);
		return 0;
	}
	bus_dmamap_sync(dma->tag, rpc->map, 0, sizeof(before),
	    BUS_DMASYNC_PREREAD);
	return error;
}

int
cixvpu_dma_boot_rpc_step(struct cixvpu_dma *dma,
    const struct cixvpu_prepared *owner, const struct cixvpu_fw_plan *plan,
    struct cixvpu_dma_boot_snapshot *snapshot, enum cixvpu_dma_rpc_result *out)
{
	struct cixvpu_dma_owner *host, *mve, *rpc;
	enum cixvpu_dma_rpc_result result;
	uint8_t before[12], observed[24];
	uint32_t state, call, size;
	size_t bytes;
	unsigned int attempt;
	int error;
	bool fault;

	if (plan == NULL || out == NULL)
		return EINVAL;
	if (plan->boot_rpc != 1)
		return ENOTSUP;
	error = cixvpu_dma_boot_pages(dma, owner, plan, &host, &mve, &rpc);
	if (error != 0)
		return error;
	error = cixvpu_dma_boot_output(dma, owner, snapshot,
	    sizeof(*snapshot), &fault);
	if (error != 0)
		return error;
	if ((uintptr_t)rpc->kva % _Alignof(uint32_t) != 0)
		return EINVAL;
	for (attempt = 0; attempt < 4; attempt++) {
		bus_dmamap_sync(dma->tag, rpc->map, 0, sizeof(before),
		    BUS_DMASYNC_POSTREAD);
		memcpy(before, rpc->kva, sizeof(before));
		bus_dmamap_sync(dma->tag, rpc->map, 0, sizeof(before),
		    BUS_DMASYNC_PREREAD);
		bytes = sizeof(before);
		if (le32dec(before) == 1) {
			if (le32dec(before + 4) == 2 && le32dec(before + 8) == 12)
				bytes = sizeof(observed);
			else if (le32dec(before + 4) == 3 && le32dec(before + 8) == 8)
				bytes = 20;
		}
		bus_dmamap_sync(dma->tag, rpc->map, 0, bytes,
		    BUS_DMASYNC_POSTREAD);
		memcpy(observed, rpc->kva, bytes);
		memcpy(snapshot->rpc_header, observed, sizeof(before));
		snapshot->rpc_valid = true;
		snapshot->rpc_alloc_valid = false;
		snapshot->rpc_resize_valid = false;
		if (memcmp(before, observed, sizeof(before)) == 0)
			break;
		bus_dmamap_sync(dma->tag, rpc->map, 0, bytes,
		    BUS_DMASYNC_PREREAD);
	}
	if (attempt == 4)
		return EAGAIN;
	state = le32dec(observed);
	call = le32dec(observed + 4);
	size = le32dec(observed + 8);
	if (bytes == sizeof(observed)) {
		memcpy(snapshot->rpc_alloc, observed + 12, 12);
		snapshot->rpc_alloc_valid = true;
	}
	if (state == 1 && call == 3 && size == 8 && bytes == 20) {
		uint8_t after[12];

		memcpy(snapshot->rpc_resize, observed + 12, 8);
		/* Recheck the committed header before a possible fixed response. */
		bus_dmamap_sync(dma->tag, rpc->map, 0, sizeof(after),
		    BUS_DMASYNC_PREREAD);
		bus_dmamap_sync(dma->tag, rpc->map, 0, sizeof(after),
		    BUS_DMASYNC_POSTREAD);
		memcpy(after, rpc->kva, sizeof(after));
		snapshot->rpc_resize_valid =
		    memcmp(observed, after, sizeof(after)) == 0;
	}
	if (state == 0 || state == 2) {
		result = state == 0 ? CIXVPU_DMA_RPC_IDLE : CIXVPU_DMA_RPC_PENDING;
		bus_dmamap_sync(dma->tag, rpc->map, 0, bytes,
		    BUS_DMASYNC_PREREAD);
		*out = result;
		return 0;
	}
	if (state != 1 || (call == 1 && size > 1012) ||
	    (call == 2 && size != 12) || (call == 3 && size != 8)) {
		error = EPROTO;
		goto rearm;
	}
	if (call == 2) {
		uint32_t *words;
		uint32_t address;
		uint64_t pages, capacity;

		if (bytes != sizeof(observed)) {
			error = EPROTO;
			goto rearm;
		}
		pages = ((uint64_t)le32dec(observed + 12) + 4095) / 4096;
		capacity = ((uint64_t)le32dec(observed + 16) + 4095) / 4096;
		if (le32dec(observed + 12) == CIXVPU_FW_PLAN_BOOT_RPC_INITIAL_SIZE &&
		    le32dec(observed + 16) == CIXVPU_FW_PLAN_BOOT_RPC_CAPACITY &&
		    observed[20] == CIXVPU_FW_PLAN_BOOT_RPC_REGION &&
		    observed[21] == CIXVPU_FW_PLAN_BOOT_RPC_LOG2_ALIGNMENT &&
		    pages == CIXVPU_FW_PLAN_BOOT_RPC_PAGES && capacity == 16384) {
			if (dma->rpc_alloc_used) {
				error = EBUSY;
				goto rearm;
			}
			dma->rpc_alloc_used = true;
			address = CIXVPU_FW_PLAN_BOOT_RPC_ADDRESS;
			result = CIXVPU_DMA_RPC_ALLOC_REPLIED;
		} else if (le32dec(observed + 12) ==
		    CIXVPU_FW_PLAN_BOOT_RPC_SECOND_SIZE &&
		    le32dec(observed + 16) == CIXVPU_FW_PLAN_BOOT_RPC_SECOND_SIZE &&
		    observed[20] == CIXVPU_FW_PLAN_BOOT_RPC_REGION &&
		    observed[21] == CIXVPU_FW_PLAN_BOOT_RPC_SECOND_LOG2_ALIGNMENT) {
			if (dma->rpc_second_alloc_used) {
				error = EBUSY;
				goto rearm;
			}
			if (!dma->rpc_alloc_used || !dma->rpc_resize_committed) {
				error = ENOTSUP;
				goto rearm;
			}
			dma->rpc_second_alloc_used = true;
			address = CIXVPU_FW_PLAN_BOOT_RPC_SECOND_ADDRESS;
			result = CIXVPU_DMA_RPC_SECOND_ALLOC_REPLIED;
		} else {
			error = ENOTSUP;
			goto rearm;
		}
		/* Retire host writes, then rearm clean neighbors before dirtying. */
		bus_dmamap_sync(dma->tag, rpc->map, 0, 4, BUS_DMASYNC_POSTWRITE);
		bus_dmamap_sync(dma->tag, rpc->map, 8, 8, BUS_DMASYNC_POSTWRITE);
		bus_dmamap_sync(dma->tag, rpc->map, 4, 4, BUS_DMASYNC_PREREAD);
		bus_dmamap_sync(dma->tag, rpc->map, 16, 8, BUS_DMASYNC_PREREAD);
		words = rpc->kva;
		words[3] = htole32(address);
		words[2] = htole32(4);
		bus_dmamap_sync(dma->tag, rpc->map, 8, 8,
		    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);
	} else if (call == 3) {
		uint32_t *words;

		if (!snapshot->rpc_resize_valid) {
			error = EPROTO;
			goto rearm;
		}
		if (!dma->rpc_alloc_used ||
		    le32dec(observed + 12) != CIXVPU_FW_PLAN_BOOT_RPC_ADDRESS ||
		    le32dec(observed + 16) != CIXVPU_FW_PLAN_BOOT_RPC_RESIZE_SIZE) {
			error = ENOTSUP;
			goto rearm;
		}
		if (dma->rpc_resize_used) {
			error = EBUSY;
			goto rearm;
		}
		dma->rpc_resize_used = true;
		/* The entire fixed backing was mapped and synced before exposure. */
		bus_dmamap_sync(dma->tag, rpc->map, 0, 4, BUS_DMASYNC_POSTWRITE);
		bus_dmamap_sync(dma->tag, rpc->map, 8, 8, BUS_DMASYNC_POSTWRITE);
		bus_dmamap_sync(dma->tag, rpc->map, 4, 4, BUS_DMASYNC_PREREAD);
		bus_dmamap_sync(dma->tag, rpc->map, 16, 4, BUS_DMASYNC_PREREAD);
		words = rpc->kva;
		words[3] = htole32(CIXVPU_FW_PLAN_BOOT_RPC_ADDRESS);
		words[2] = htole32(4);
		bus_dmamap_sync(dma->tag, rpc->map, 8, 8,
		    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);
		result = CIXVPU_DMA_RPC_RESIZE_REPLIED;
	} else if (call == 1) {
		if (dma->rpc_debug_count >= 16) {
			error = E2BIG;
			goto rearm;
		}
		dma->rpc_debug_count++;
		bus_dmamap_sync(dma->tag, rpc->map, 0, 4, BUS_DMASYNC_POSTWRITE);
		bus_dmamap_sync(dma->tag, rpc->map, 4, 8, BUS_DMASYNC_PREREAD);
		result = CIXVPU_DMA_RPC_DEBUG_REPLIED;
	} else {
		error = ENOTSUP;
		goto rearm;
	}
	atomic_store_release((uint32_t *)rpc->kva, htole32(2));
	bus_dmamap_sync(dma->tag, rpc->map, 0, 4,
	    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);
	if (result == CIXVPU_DMA_RPC_RESIZE_REPLIED)
		dma->rpc_resize_committed = true;
	*out = result;
	return 0;
rearm:
	bus_dmamap_sync(dma->tag, rpc->map, 0, bytes, BUS_DMASYNC_PREREAD);
	return error;
}

static int
cixvpu_dma_boot_snapshot_common(struct cixvpu_dma *dma,
    const struct cixvpu_prepared *owner, const struct cixvpu_fw_plan *plan,
    struct cixvpu_dma_boot_snapshot *snapshot, bool rpc_enabled)
{
	struct cixvpu_dma_owner *host, *mve, *rpc;
	uint8_t before[16], control[16], rpc_header[12];
	uint32_t state;
	unsigned int attempt;
	int error;
	bool fault;

	error = cixvpu_dma_boot_pages(dma, owner, plan, &host, &mve, &rpc);
	if (error != 0)
		return error;
	error = cixvpu_dma_boot_output(dma, owner, snapshot,
	    sizeof(*snapshot), &fault);
	if (error != 0)
		return error;
	bus_dmamap_sync(dma->tag, rpc->map, 0, sizeof(rpc_header),
	    BUS_DMASYNC_POSTREAD);
	memcpy(rpc_header, rpc->kva, sizeof(rpc_header));
	bus_dmamap_sync(dma->tag, rpc->map, 0, sizeof(rpc_header),
	    BUS_DMASYNC_PREREAD);
	memcpy(snapshot->rpc_header, rpc_header, sizeof(rpc_header));
	snapshot->rpc_valid = true;
	state = (uint32_t)rpc_header[0] | (uint32_t)rpc_header[1] << 8 |
	    (uint32_t)rpc_header[2] << 16 | (uint32_t)rpc_header[3] << 24;
	/* Closed diagnostics only. Keep the original header and rejection. */
	snapshot->rpc_alloc_valid = false;
	snapshot->rpc_resize_valid = false;
	if (rpc_enabled && state == 1)
		return EAGAIN;
	if (state == 1 &&
	    ((le32dec(rpc_header + 4) == 2 && le32dec(rpc_header + 8) == 12) ||
	    (le32dec(rpc_header + 4) == 3 && le32dec(rpc_header + 8) == 8))) {
		uint8_t rpc_after[12];
		bool alloc = le32dec(rpc_header + 4) == 2;
		size_t length = alloc ? 12 : 8;
		uint8_t *parameters = alloc ? snapshot->rpc_alloc :
		    snapshot->rpc_resize;

		bus_dmamap_sync(dma->tag, rpc->map, 12,
		    length, BUS_DMASYNC_POSTREAD);
		memcpy(parameters, (const uint8_t *)rpc->kva + 12, length);
		bus_dmamap_sync(dma->tag, rpc->map, 12,
		    length, BUS_DMASYNC_PREREAD);
		bus_dmamap_sync(dma->tag, rpc->map, 0, sizeof(rpc_after),
		    BUS_DMASYNC_POSTREAD);
		memcpy(rpc_after, rpc->kva, sizeof(rpc_after));
		bus_dmamap_sync(dma->tag, rpc->map, 0, sizeof(rpc_after),
		    BUS_DMASYNC_PREREAD);
		if (alloc)
			snapshot->rpc_alloc_valid =
			    memcmp(rpc_header, rpc_after, sizeof(rpc_header)) == 0;
		else
			snapshot->rpc_resize_valid =
			    memcmp(rpc_header, rpc_after, sizeof(rpc_header)) == 0;
	}

	if (state != 0 && !(rpc_enabled && state == 2))
		return rpc_enabled ? EPROTO : ENOTSUP;
	for (attempt = 0; attempt < 4; attempt++) {
		/* A bounce copy need not fetch control before payload. Bracket it. */
		bus_dmamap_sync(dma->tag, mve->map, 0, sizeof(before),
		    BUS_DMASYNC_POSTREAD);
		memcpy(before, mve->kva, sizeof(before));
		bus_dmamap_sync(dma->tag, mve->map, 0, sizeof(before),
		    BUS_DMASYNC_PREREAD);

		bus_dmamap_sync(dma->tag, mve->map, 0, CIXVPU_FW_PAGE_SIZE,
		    BUS_DMASYNC_POSTREAD);
		memcpy(snapshot->message, mve->kva, sizeof(snapshot->message));
		bus_dmamap_sync(dma->tag, mve->map, 0, CIXVPU_FW_PAGE_SIZE,
		    BUS_DMASYNC_PREREAD);
		bus_dmamap_sync(dma->tag, mve->map, 0, sizeof(control),
		    BUS_DMASYNC_POSTREAD);
		memcpy(control, mve->kva, sizeof(control));
		bus_dmamap_sync(dma->tag, mve->map, 0, sizeof(control),
		    BUS_DMASYNC_PREREAD);
		if (memcmp(before, control, sizeof(control)) == 0 &&
		    memcmp(control, snapshot->message, sizeof(control)) == 0)
			return 0;
	}
	return EAGAIN;
}

int
cixvpu_dma_boot_snapshot(struct cixvpu_dma *dma,
    const struct cixvpu_prepared *owner, const struct cixvpu_fw_plan *plan,
    struct cixvpu_dma_boot_snapshot *snapshot)
{

	return cixvpu_dma_boot_snapshot_common(dma, owner, plan, snapshot, false);
}

int
cixvpu_dma_boot_snapshot_rpc(struct cixvpu_dma *dma,
    const struct cixvpu_prepared *owner, const struct cixvpu_fw_plan *plan,
    struct cixvpu_dma_boot_snapshot *snapshot)
{

	if (plan == NULL)
		return EINVAL;
	if (plan->boot_rpc != 1)
		return ENOTSUP;
	return cixvpu_dma_boot_snapshot_common(dma, owner, plan, snapshot, true);
}

int
cixvpu_dma_boot_ack(struct cixvpu_dma *dma,
    const struct cixvpu_prepared *owner, const struct cixvpu_fw_plan *plan,
    uint16_t cursor)
{
	struct cixvpu_dma_owner *host, *mve, *rpc;
	uint8_t *p;
	int error;

	error = cixvpu_dma_boot_pages(dma, owner, plan, &host, &mve, &rpc);
	if (error != 0)
		return error;
	if (cursor >= 1020)
		return EINVAL;
	bus_dmamap_sync(dma->tag, host->map, 0, 2, BUS_DMASYNC_POSTWRITE);
	p = host->kva;
	p[0] = (uint8_t)cursor;
	p[1] = (uint8_t)(cursor >> 8);
	bus_dmamap_sync(dma->tag, host->map, 0, 2, BUS_DMASYNC_PREWRITE);
	return 0;
}

static int
cixvpu_dma_queue_pages(struct cixvpu_dma *dma,
    const struct cixvpu_prepared *owner, const struct cixvpu_fw_plan *plan,
    enum cixvpu_dma_queue queue, struct cixvpu_dma_owner **host,
    struct cixvpu_dma_owner **mve, struct cixvpu_dma_owner **rpc)
{
	struct cixvpu_dma_owner *msg, *reply;
	enum cixvpu_fw_plan_role hrole, mrole;
	uint32_t i;
	int error;

	switch (queue) {
	case CIXVPU_DMA_MESSAGE:
		hrole = CIXVPU_FW_PLAN_MSG_HOST;
		mrole = CIXVPU_FW_PLAN_MSG_MVE;
		break;
	case CIXVPU_DMA_INPUT:
		hrole = CIXVPU_FW_PLAN_BUF_IN_HOST;
		mrole = CIXVPU_FW_PLAN_BUF_IN_MVE;
		break;
	case CIXVPU_DMA_OUTPUT:
		hrole = CIXVPU_FW_PLAN_BUF_OUT_HOST;
		mrole = CIXVPU_FW_PLAN_BUF_OUT_MVE;
		break;
	default:
		return EINVAL;
	}
	error = cixvpu_dma_boot_pages(dma, owner, plan, &msg, &reply, rpc);
	if (error != 0)
		return error;
	if (plan->slots_enabled != 1 || plan->boot_rpc != 1 || plan->cores != 1)
		return ENOTSUP;
	*host = *mve = NULL;
	for (i = 0; i < dma->count; i++) {
		if (dma->owners[i].page.role == hrole)
			*host = &dma->owners[i];
		if (dma->owners[i].page.role == mrole)
			*mve = &dma->owners[i];
	}
	if (*host == NULL || *mve == NULL ||
	    (uintptr_t)(*host)->kva % _Alignof(uint16_t) != 0 ||
	    (uintptr_t)(*mve)->kva % _Alignof(uint16_t) != 0)
		return EINVAL;
	return 0;
}

static int
cixvpu_dma_queue_rpc(struct cixvpu_dma *dma, struct cixvpu_dma_owner *rpc,
    struct cixvpu_dma_boot_snapshot *snapshot)
{
	uint32_t state;

	bus_dmamap_sync(dma->tag, rpc->map, 0, 12, BUS_DMASYNC_POSTREAD);
	__atomic_thread_fence(__ATOMIC_ACQUIRE);
	memcpy(snapshot->rpc_header, rpc->kva, 12);
	bus_dmamap_sync(dma->tag, rpc->map, 0, 12, BUS_DMASYNC_PREREAD);
	snapshot->rpc_valid = true;
	snapshot->rpc_alloc_valid = snapshot->rpc_resize_valid = false;
	state = le32dec(snapshot->rpc_header);
	return state == 1 ? EAGAIN : state == 0 || state == 2 ? 0 : EPROTO;
}

int
cixvpu_dma_queue_cursor(struct cixvpu_dma *dma,
    const struct cixvpu_prepared *owner, const struct cixvpu_fw_plan *plan,
    enum cixvpu_dma_queue queue, struct cixvpu_dma_boot_snapshot *snapshot,
    uint16_t *out)
{
	struct cixvpu_dma_owner *host, *mve, *rpc;
	const uint8_t *p;
	uint16_t value;
	int error;
	bool fault;

	error = cixvpu_dma_queue_pages(dma, owner, plan, queue, &host, &mve, &rpc);
	if (error == 0)
		error = cixvpu_dma_boot_output(dma, owner, snapshot, sizeof(*snapshot), &fault);
	if (error == 0)
		error = cixvpu_dma_boot_output(dma, owner, out, sizeof(*out), &fault);
	if (error != 0)
		return error;
	error = cixvpu_dma_queue_rpc(dma, rpc, snapshot);
	if (error != 0)
		return error;
	bus_dmamap_sync(dma->tag, mve->map, 2, 2, BUS_DMASYNC_POSTREAD);
	__atomic_thread_fence(__ATOMIC_ACQUIRE);
	p = (const uint8_t *)mve->kva + 2;
	value = (uint16_t)p[0] | (uint16_t)p[1] << 8;
	bus_dmamap_sync(dma->tag, mve->map, 2, 2, BUS_DMASYNC_PREREAD);
	if (value >= 1020)
		return EPROTO;
	*out = value;
	return 0;
}

int
cixvpu_dma_queue_snapshot(struct cixvpu_dma *dma,
    const struct cixvpu_prepared *owner, const struct cixvpu_fw_plan *plan,
    enum cixvpu_dma_queue queue, struct cixvpu_dma_boot_snapshot *snapshot)
{
	struct cixvpu_dma_owner *host, *mve, *rpc;
	uint8_t before[16], after[16];
	unsigned int attempt;
	int error;
	bool fault;

	error = cixvpu_dma_queue_pages(dma, owner, plan, queue, &host, &mve, &rpc);
	if (error == 0)
		error = cixvpu_dma_boot_output(dma, owner, snapshot, sizeof(*snapshot), &fault);
	if (error != 0)
		return error;
	error = cixvpu_dma_queue_rpc(dma, rpc, snapshot);
	if (error != 0)
		return error;
	for (attempt = 0; attempt < 4; attempt++) {
		bus_dmamap_sync(dma->tag, mve->map, 0, 16, BUS_DMASYNC_POSTREAD);
		__atomic_thread_fence(__ATOMIC_ACQUIRE);
		memcpy(before, mve->kva, sizeof(before));
		bus_dmamap_sync(dma->tag, mve->map, 0, 16, BUS_DMASYNC_PREREAD);
		bus_dmamap_sync(dma->tag, mve->map, 0, CIXVPU_FW_PAGE_SIZE, BUS_DMASYNC_POSTREAD);
		memcpy(snapshot->message, mve->kva, sizeof(snapshot->message));
		bus_dmamap_sync(dma->tag, mve->map, 0, CIXVPU_FW_PAGE_SIZE, BUS_DMASYNC_PREREAD);
		bus_dmamap_sync(dma->tag, mve->map, 0, 16, BUS_DMASYNC_POSTREAD);
		__atomic_thread_fence(__ATOMIC_ACQUIRE);
		memcpy(after, mve->kva, sizeof(after));
		bus_dmamap_sync(dma->tag, mve->map, 0, 16, BUS_DMASYNC_PREREAD);
		if (memcmp(before, after, 2) != 0 ||
		    memcmp(after, snapshot->message, 2) != 0)
			continue;
		if (queue == CIXVPU_DMA_MESSAGE &&
		    (memcmp(before + 4, after + 4, 12) != 0 ||
		    memcmp(after + 4, snapshot->message + 4, 12) != 0))
			continue;
		return 0;
	}
	return EAGAIN;
}

/* The caller has validated every map/span before reaching this void boundary. */
static void
cixvpu_dma_queue_store(struct cixvpu_dma *dma, struct cixvpu_dma_owner *host,
    unsigned int offset, uint16_t cursor)
{
	const uint8_t bytes[2] = { (uint8_t)cursor, (uint8_t)(cursor >> 8) };
	uint16_t wire;

	memcpy(&wire, bytes, sizeof(wire));
	bus_dmamap_sync(dma->tag, host->map, offset, 2, BUS_DMASYNC_POSTWRITE);
	atomic_store_release((uint16_t *)((uint8_t *)host->kva + offset), wire);
	bus_dmamap_sync(dma->tag, host->map, offset, 2, BUS_DMASYNC_PREWRITE);
}

int
cixvpu_dma_queue_publish(struct cixvpu_dma *dma,
    const struct cixvpu_prepared *owner, const struct cixvpu_fw_plan *plan,
    enum cixvpu_dma_queue queue, uint16_t previous, uint16_t next,
    const void *data, const uint32_t history[3], bool refill)
{
	struct cixvpu_dma_owner *host, *mve, *rpc;
	struct cixvpu_fw_slot_span output;
	const uint8_t *bytes = data;
	uint8_t *p;
	size_t start, first, length;
	unsigned int i;
	int error;
	bool fault;

	if (previous >= 1020 || next >= 1020 || previous == next ||
	    (refill && queue != CIXVPU_DMA_OUTPUT))
		return EINVAL;
	length = (size_t)(next >= previous ? next - previous :
	    1020 - previous + next) * 4;
	if (length > 80)
		return EINVAL;
	error = cixvpu_dma_queue_pages(dma, owner, plan, queue, &host, &mve, &rpc);
	if (error == 0)
		error = cixvpu_dma_boot_output(dma, owner, data, 4080, &fault);
	if (error == 0)
		error = cixvpu_dma_boot_output(dma, owner, history, 12, &fault);
	if (error == 0 && refill)
		error = cixvpu_fw_plan_slot(plan, CIXVPU_FW_SLOT_BITSTREAM, &output);
	if (error != 0)
		return error;
	/* No fallible work remains, including output refill and cursor publication. */
	if (refill)
		for (i = 0; i < output.pages; i++)
			bus_dmamap_sync(dma->tag, dma->owners[output.first_backing + i].map,
			    0, CIXVPU_FW_PAGE_SIZE, BUS_DMASYNC_PREREAD);
	start = (size_t)previous * 4;
	first = length < 4080 - start ? length : 4080 - start;
	p = host->kva;
	bus_dmamap_sync(dma->tag, host->map, 16 + start, first, BUS_DMASYNC_POSTWRITE);
	memcpy(p + 16 + start, bytes + start, first);
	bus_dmamap_sync(dma->tag, host->map, 16 + start, first, BUS_DMASYNC_PREWRITE);
	if (first != length) {
		bus_dmamap_sync(dma->tag, host->map, 16, length - first, BUS_DMASYNC_POSTWRITE);
		memcpy(p + 16, bytes, length - first);
		bus_dmamap_sync(dma->tag, host->map, 16, length - first, BUS_DMASYNC_PREWRITE);
	}
	bus_dmamap_sync(dma->tag, host->map, 4, 12, BUS_DMASYNC_POSTWRITE);
	for (i = 0; i < 3; i++) {
		p[4 + 4 * i] = (uint8_t)history[i];
		p[5 + 4 * i] = (uint8_t)(history[i] >> 8);
		p[6 + 4 * i] = (uint8_t)(history[i] >> 16);
		p[7 + 4 * i] = (uint8_t)(history[i] >> 24);
	}
	bus_dmamap_sync(dma->tag, host->map, 4, 12, BUS_DMASYNC_PREWRITE);
	cixvpu_dma_queue_store(dma, host, 2, next);
	return 0;
}

int
cixvpu_dma_queue_ack(struct cixvpu_dma *dma,
    const struct cixvpu_prepared *owner, const struct cixvpu_fw_plan *plan,
    enum cixvpu_dma_queue queue, uint16_t cursor)
{
	struct cixvpu_dma_owner *host, *mve, *rpc;
	int error;

	if (cursor >= 1020)
		return EINVAL;
	error = cixvpu_dma_queue_pages(dma, owner, plan, queue, &host, &mve, &rpc);
	if (error != 0)
		return error;
	cixvpu_dma_queue_store(dma, host, 0, cursor);
	return 0;
}

int
cixvpu_dma_queue_return(struct cixvpu_dma *dma,
    const struct cixvpu_prepared *owner, const struct cixvpu_fw_plan *plan,
    enum cixvpu_dma_queue queue, uint16_t cursor, uint32_t offset,
    uint32_t length, void *destination, size_t capacity)
{
	struct cixvpu_dma_owner *host, *mve, *rpc;
	struct cixvpu_fw_slot_span first, second;
	unsigned int i;
	int error;
	bool fault;

	if (cursor >= 1020 || (queue != CIXVPU_DMA_INPUT && queue != CIXVPU_DMA_OUTPUT))
		return EINVAL;
	error = cixvpu_dma_queue_pages(dma, owner, plan, queue, &host, &mve, &rpc);
	if (error == 0)
		error = cixvpu_fw_plan_slot(plan, queue == CIXVPU_DMA_INPUT ?
		    CIXVPU_FW_SLOT_Y : CIXVPU_FW_SLOT_BITSTREAM, &first);
	if (error == 0 && queue == CIXVPU_DMA_INPUT)
		error = cixvpu_fw_plan_slot(plan, CIXVPU_FW_SLOT_UV, &second);
	if (error != 0)
		return error;
	if (queue == CIXVPU_DMA_OUTPUT) {
		if (offset > first.bytes || length > first.bytes - offset)
			return EPROTO;
		if (capacity < length)
			return EMSGSIZE;
		if (capacity != 0) {
			error = cixvpu_dma_boot_output(dma, owner, destination, capacity, &fault);
			if (error != 0)
				return error;
		} else if (length != 0 || destination != NULL) {
			return EINVAL;
		}
	} else if (offset != 0 || length != 0 || capacity != 0 || destination != NULL) {
		return EINVAL;
	}
	/* Preflight is complete. Copy precedes ACK and cannot fail halfway. */
	if (queue == CIXVPU_DMA_INPUT) {
		for (i = 0; i < first.pages; i++)
			bus_dmamap_sync(dma->tag, dma->owners[first.first_backing + i].map,
			    0, CIXVPU_FW_PAGE_SIZE, BUS_DMASYNC_POSTWRITE);
		for (i = 0; i < second.pages; i++)
			bus_dmamap_sync(dma->tag, dma->owners[second.first_backing + i].map,
			    0, CIXVPU_FW_PAGE_SIZE, BUS_DMASYNC_POSTWRITE);
	} else {
		uint8_t *out = destination;
		uint32_t page, local, remaining = length;

		for (i = 0; i < first.pages; i++)
			bus_dmamap_sync(dma->tag, dma->owners[first.first_backing + i].map,
			    0, CIXVPU_FW_PAGE_SIZE, BUS_DMASYNC_POSTREAD);
		page = offset / CIXVPU_FW_PAGE_SIZE;
		local = offset % CIXVPU_FW_PAGE_SIZE;
		while (remaining != 0) {
			uint32_t bytes = CIXVPU_FW_PAGE_SIZE - local;

			if (bytes > remaining)
				bytes = remaining;
			memcpy(out, (uint8_t *)dma->owners[
			    first.first_backing + page].kva + local, bytes);
			out += bytes;
			remaining -= bytes;
			page++;
			local = 0;
		}
	}
	cixvpu_dma_queue_store(dma, host, 0, cursor);
	return 0;
}
