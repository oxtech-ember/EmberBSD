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
		return BUS_DMASYNC_PREWRITE;
	case CIXVPU_FW_PLAN_PRIVATE:
	case CIXVPU_FW_PLAN_SHARED:
	case CIXVPU_FW_PLAN_MSG_MVE:
	case CIXVPU_FW_PLAN_BUF_IN_MVE:
	case CIXVPU_FW_PLAN_BUF_OUT_MVE:
	case CIXVPU_FW_PLAN_RPC:
	case CIXVPU_FW_PLAN_PRINT:
	case CIXVPU_FW_PLAN_RPC_DATA:
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
cixvpu_dma_boot_snapshot(struct cixvpu_dma *dma,
    const struct cixvpu_prepared *owner, const struct cixvpu_fw_plan *plan,
    struct cixvpu_dma_boot_snapshot *snapshot)
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
	if (state == 1 && le32dec(rpc_header + 4) == 2 &&
	    le32dec(rpc_header + 8) == sizeof(snapshot->rpc_alloc)) {
		uint8_t rpc_after[12];

		bus_dmamap_sync(dma->tag, rpc->map, 12,
		    sizeof(snapshot->rpc_alloc), BUS_DMASYNC_POSTREAD);
		memcpy(snapshot->rpc_alloc, (const uint8_t *)rpc->kva + 12,
		    sizeof(snapshot->rpc_alloc));
		bus_dmamap_sync(dma->tag, rpc->map, 12,
		    sizeof(snapshot->rpc_alloc), BUS_DMASYNC_PREREAD);
		bus_dmamap_sync(dma->tag, rpc->map, 0, sizeof(rpc_after),
		    BUS_DMASYNC_POSTREAD);
		memcpy(rpc_after, rpc->kva, sizeof(rpc_after));
		bus_dmamap_sync(dma->tag, rpc->map, 0, sizeof(rpc_after),
		    BUS_DMASYNC_PREREAD);
		snapshot->rpc_alloc_valid =
		    memcmp(rpc_header, rpc_after, sizeof(rpc_header)) == 0;
	}

	if (state != 0)
		return ENOTSUP;
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
