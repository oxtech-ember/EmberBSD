/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual BO/backing/helper/table TUs with native service bridge. */
#include "range-bridge-services.h"

static struct drm_device range_drm;
static struct bcmv3d_device range_device;
static struct drm_file range_file;
static struct bcmv3d_mmu4_backing *range_backing;
static struct bcm2712_mmu4_config range_cfg;
static struct bcm2712_mmu4_pages range_pages;
static struct bcm2712_mmu4_image range_image;
static struct bcmv3d_mmu4_backing_status range_status;
static void *range_frame, *range_initial_control[3];
static uint32_t range_expected[2048];
static unsigned range_slot, range_op, range_sync_mask, range_store_count;
static bool range_first_link, range_phase;
static unsigned int consumer_pre, consumer_post;
static struct mmu4_allocation range_saved_control[3][2];

int range_icac_owned(void);
int range_native_owned(void);
int range_exposure_latched(void);

int
native_backing_create(uint32_t debug, void **out, unsigned int failure)
{
	unsigned int i;
	int error;

	CHECK(!range_icac_owned() && range_native_owned());
	range_device.drm = &range_drm;
	range_device.dmat = &range_device;
	range_device.bo_total = BCMV3D_BO_TOTAL_MIN;
	range_drm.dev_private = &range_device;
	fail_service = failure;
	error = bcmv3d_mmu4_backing_create(&range_device, debug, &range_backing);
	if (error != 0)
		return error;
	range_frame = malloc(range_backing->status.frame_bytes);
	CHECK(range_frame != NULL);
	for (i = 0; i < 3; i++) {
		range_initial_control[i] = malloc(range_backing->status.control_bytes);
		CHECK(range_initial_control[i] != NULL);
	}
	*out = range_backing;
	return 0;
}

int
native_backing_prepare(void *arg, const struct bcm2712_mmu4_config *cfg,
    bool failure)
{
	unsigned int i, before = service_calls;
	int error;

	CHECK(arg == range_backing && range_icac_owned());
	if (failure)
		fail_pmap = pmap_calls + 1;
	error = bcmv3d_mmu4_backing_prepare(arg, cfg);
	CHECK(before == service_calls);
	if (error != 0)
		return error;
	range_cfg = range_backing->cfg;
	range_pages = range_backing->pages;
	range_image = range_backing->image;
	range_status = range_backing->status;
	memcpy(range_frame, range_backing->frame->vaddr,
	    range_backing->status.frame_bytes);
	for (i = 0; i < 3; i++)
		memcpy(range_initial_control[i], range_backing->control[i + 1].kva,
		    range_backing->status.control_bytes);
	return 0;
}

void
native_backing_check(void *arg)
{

	CHECK(arg == range_backing && range_backing->status.sealed);
	CHECK(bcmv3d_mmu4_backing_destroy_unexposed(arg) == EBUSY);
}

void *
range_bridge_bo(unsigned int fragments)
{
	struct drm_v3d_create_bo request = { .size = 8294400 };

	CHECK(!range_icac_owned());
	fragment_pages = fragments;
	CHECK(bcmv3d_create_bo(&range_drm, &request, &range_file) == 0);
	return range_file.handles[request.handle];
}

unsigned int
range_bridge_refs(void *obj)
{

	return ((struct drm_gem_object *)obj)->refs;
}

void
range_bridge_close(void *obj)
{
	unsigned int i;

	CHECK(!range_icac_owned());
	for (i = 1; i < 16; i++) {
		if (range_file.handles[i] == obj) {
			drm_gem_object_put_unlocked(obj);
			range_file.handles[i] = NULL;
			return;
		}
	}
	CHECK(false);
}

void
range_bridge_begin(unsigned int slot, unsigned int op)
{
	struct mmu4_gem_slot *g = range_backing->staged[slot];
	unsigned int i, seg = 0;
	uint64_t offset = 0;

	CHECK(!range_icac_owned() && g != NULL && g->bo != NULL);
	range_slot = slot;
	range_op = op;
	for (i = 0; i < 2; i++) {
		if (!g->linked) {
			range_saved_control[slot][i] = g->control[i];
		} else {
			CHECK(g->control[i].kva == range_saved_control[slot][i].kva);
			CHECK(g->control[i].map == range_saved_control[slot][i].map);
			CHECK(g->control[i].pa == range_saved_control[slot][i].pa);
		}
	}
	range_first_link = !g->linked;
	range_sync_mask = range_store_count = 0;
	range_phase = true;
	for (i = 0; i < 2048; i++) {
		range_expected[i] = 0;
		if (op != BCMV3D_MMU4_RANGE_PUBLISH || i >= 2025)
			continue;
		range_expected[i] = 0x10000000 | (uint32_t)
		    ((g->bo->cma.dmamap->dm_segs[seg].ds_addr + offset) >> 12);
		offset += 4096;
		if (offset == g->bo->cma.dmamap->dm_segs[seg].ds_len) {
			seg++;
			offset = 0;
		}
	}
}

static void
range_atomic_store(uint32_t *p, uint32_t value)
{
	struct mmu4_gem_slot *g = range_backing->staged[range_slot];
	uintptr_t address = (uintptr_t)p;
	unsigned int i;
	size_t offset;

	CHECK(range_phase && range_icac_owned() && range_native_owned());
	for (i = 0; i < 2; i++) {
		if (address < (uintptr_t)g->control[i].kva ||
		    address >= (uintptr_t)g->control[i].kva + 4096)
			continue;
		offset = (address - (uintptr_t)g->control[i].kva) / 4;
		CHECK(!g->linked || range_exposure_latched());
		CHECK(le32toh(value) == range_expected[i * 1024 + offset]);
		__atomic_store_n(p, value, __ATOMIC_RELAXED);
		range_store_count++;
		return;
	}
	CHECK(p == (uint32_t *)range_backing->control[0].kva +
	    (range_slot + 1) * 2 ||
	    p == (uint32_t *)range_backing->control[0].kva +
	    (range_slot + 1) * 2 + 1);
	CHECK(g->linked && range_exposure_latched());
	__atomic_store_n(p, value, __ATOMIC_RELAXED);
	range_store_count++;
}

static void
range_dma_sync(bus_dmamap_t map, size_t size, int flags)
{
	struct mmu4_gem_slot *g;
	unsigned int i, j;
	uint32_t expected;

	CHECK(range_native_owned());
	CHECK(size == map->dm_mapsize);
	if (!range_icac_owned()) {
		for (i = 0; i < 3; i++) {
			g = range_backing->staged[i];
			if (g == NULL || g->map != map)
				continue;
			CHECK(g->bo != NULL && g->loaded && !g->exposed);
			CHECK(map != g->bo->cma.dmamap);
			CHECK(size == g->bo->cma.dmasize);
			if (flags == BUS_DMASYNC_PREWRITE) {
				CHECK(!g->read_synced && !g->published);
				consumer_pre++;
			} else {
				CHECK(flags == BUS_DMASYNC_POSTWRITE && g->read_synced);
				consumer_post++;
			}
			return;
		}
		CHECK(false);
	}

	if (!range_phase) {
		CHECK(flags == BUS_DMASYNC_PREWRITE);
		syncs++;
		return;
	}
	CHECK(flags == BUS_DMASYNC_PREWRITE || flags == BUS_DMASYNC_POSTWRITE);
	if (flags == BUS_DMASYNC_POSTWRITE)
		return;
	g = range_backing->staged[range_slot];
	for (i = 0; i < 2; i++) {
		if (map != g->control[i].map)
			continue;
		for (j = 0; j < size / 4; j++) {
			expected = j < 1024 ? range_expected[i * 1024 + j] : 0;
			CHECK(le32toh(((uint32_t *)g->control[i].kva)[j]) == expected);
		}
		range_sync_mask |= 1U << i;
		return;
	}
	CHECK(map == range_backing->control[0].map && range_sync_mask == 3);
	CHECK(g->linked && range_exposure_latched());
	range_sync_mask |= 4;
}

void
range_bridge_flushed(void)
{

	CHECK(range_phase && range_icac_owned());
	CHECK(range_sync_mask == (range_first_link ? 7U : 3U));
	CHECK(range_store_count == (range_first_link ? 2050U : 2048U));
}

void
range_bridge_verify(unsigned int slot, bool mapped)
{
	struct mmu4_gem_slot *g = range_backing->staged[slot], *other;
	uint32_t expected;
	unsigned int i, j;

	CHECK(!range_icac_owned() && g != NULL && g->linked);
	CHECK(g->published == mapped);
	CHECK(mapped ? g->bo != NULL && g->loaded : g->bo == NULL && !g->loaded);
	CHECK(memcmp(&range_cfg, &range_backing->cfg, sizeof(range_cfg)) == 0);
	CHECK(memcmp(&range_pages, &range_backing->pages, sizeof(range_pages)) == 0);
	CHECK(memcmp(&range_image, &range_backing->image, sizeof(range_image)) == 0);
	CHECK(memcmp(&range_status, &range_backing->status,
	    sizeof(range_status)) == 0);
	CHECK(memcmp(range_frame, range_backing->frame->vaddr,
	    range_backing->status.frame_bytes) == 0);
	for (i = 0; i < 3; i++)
		CHECK(memcmp(range_initial_control[i], range_backing->control[i + 1].kva,
		    range_backing->status.control_bytes) == 0);
	for (i = 0; i < range_backing->status.control_bytes / 4; i++) {
		expected = i < 1024 ? mmu4_range_word(range_image.top + i * 4) : 0;
		if (i >= 2 && i < 8) {
			other = range_backing->staged[i / 2 - 1];
			if (other != NULL && other->linked)
				expected = 0x10000000 | (uint32_t)(other->control[i % 2].pa >> 12);
		}
		CHECK(le32toh(((uint32_t *)range_backing->control[0].kva)[i]) == expected);
	}
	for (i = 0; i < 2; i++) {
		for (j = 0; j < range_backing->status.control_bytes / 4; j++) {
			expected = mapped && j < 1024 ? range_expected[i * 1024 + j] : 0;
			CHECK(le32toh(((uint32_t *)g->control[i].kva)[j]) == expected);
		}
	}
	range_phase = false;
}

unsigned int
range_bridge_allocated(void)
{

	return allocated;
}

void
range_bridge_retained(unsigned int slot, void *obj)
{
	struct mmu4_gem_slot *g = range_backing->staged[slot];

	CHECK(g != NULL && g->linked && g->bo != NULL && g->loaded);
	CHECK(&g->bo->cma.base == obj && g->bo->cma.base.refs >= 2);
	CHECK(range_backing->range_fault != 0);
	CHECK(bcmv3d_mmu4_backing_discard_staged_gem(range_backing, slot) == EBUSY);
}

/* Link uncalled retained-reference fixture functions. */
void *
icac_backing_create(struct bcm2712_mmu4_config *cfg, bool failure)
{
	void *b = NULL;

	CHECK(native_backing_create(0x20006664, &b, 0) == 0);
	CHECK(bcmv3d_mmu4_backing_get_config(b, cfg) == 0);
	CHECK(bcmv3d_mmu4_backing_seal(b) == 0);
	if (failure)
		fail_pmap = pmap_calls + 1;
	return b;
}

int
icac_backing_prepare(void *arg, const struct bcm2712_mmu4_config *cfg)
{

	return native_backing_prepare(arg, cfg, false);
}

void
icac_backing_retained(void *arg)
{

	native_backing_check(arg);
}

void
range_bridge_consumer_counts(unsigned int pre, unsigned int post)
{

	CHECK(consumer_pre == pre && consumer_post == post);
}
