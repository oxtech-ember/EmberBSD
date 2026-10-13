/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2023 Imagination Technologies Ltd. */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * AI-assisted native adaptation of the MIT alternative in Linux
 * 7b63ef2d55f24519e7e9e5f4d15dbea03f126e40: pvr_free_list.c.
 * Native DMA ownership and retained cleanup failures are EmberBSD changes.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
#include <sys/param.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/systm.h>
#include <arm/sunxi/powervr/pvr_rogue_fwif.h>
#include <arm/sunxi/powervr/pvr_rogue_fwif_client.h>
#include "pvr_backend.h"
#include "pvr_freelist.h"

#define PVR_FREELIST_PAGE 4096U
#define PVR_FREELIST_PA_END (UINT64_C(1) << 36)
#define PVR_FREELIST_VA_END (UINT64_C(1) << 40)
/*
 * The pinned FWIF client limit is 2 GiB of PM pages. Four PM pipes must not
 * wrap the 16 GiB PM address spaces. This is not an allocation budget.
 */
#define PVR_FREELIST_MAX_PAGES \
	(ROGUE_PM_MAX_FREELIST_SIZE / PVR_FREELIST_PAGE)

struct pvr_freelist_page {
	bus_dma_segment_t segment;
	bus_dmamap_t map;
	void *cpu;
	uint64_t pa;
	int nsegments;
	bool loaded, synced;
};

struct pvr_freelist {
	bus_dma_tag_t dmat;
	const struct pvr_backend *backend;
	struct pvr_freelist_config config;
	struct pvr_fw_object firmware;
	struct pvr_freelist_page *pages;
	uint32_t allocated, page_capacity, current_pages, ready_pages;
	bool initialized, ready, cleaned, failed, grow_prepared;
};

static void
pvr_freelist_page_free(struct pvr_freelist *fl, struct pvr_freelist_page *page)
{

	if (page->loaded && page->synced)
		bus_dmamap_sync(fl->dmat, page->map, 0, PVR_FREELIST_PAGE,
		    BUS_DMASYNC_POSTWRITE | BUS_DMASYNC_POSTREAD);
	if (page->loaded)
		bus_dmamap_unload(fl->dmat, page->map);
	if (page->map != NULL)
		bus_dmamap_destroy(fl->dmat, page->map);
	if (page->cpu != NULL)
		bus_dmamem_unmap(fl->dmat, page->cpu, PVR_FREELIST_PAGE);
	if (page->nsegments != 0)
		bus_dmamem_free(fl->dmat, &page->segment, page->nsegments);
	memset(page, 0, sizeof(*page));
}

static int
pvr_freelist_page_alloc(struct pvr_freelist *fl, struct pvr_freelist_page *page)
{
	int error;

	error = bus_dmamem_alloc(fl->dmat, PVR_FREELIST_PAGE,
	    PVR_FREELIST_PAGE, 0, &page->segment, 1, &page->nsegments,
	    BUS_DMA_WAITOK);
	if (error != 0) {
		page->nsegments = 0;
		return error;
	}
	if (page->nsegments != 1) {
		error = EINVAL;
		goto fail;
	}
	error = bus_dmamem_map(fl->dmat, &page->segment, 1, PVR_FREELIST_PAGE,
	    &page->cpu, BUS_DMA_WAITOK | BUS_DMA_COHERENT);
	if (error != 0) {
		page->cpu = NULL;
		goto fail;
	}
	error = bus_dmamap_create(fl->dmat, PVR_FREELIST_PAGE, 1,
	    PVR_FREELIST_PAGE, 0, BUS_DMA_WAITOK, &page->map);
	if (error != 0) {
		page->map = NULL;
		goto fail;
	}
	error = bus_dmamap_load(fl->dmat, page->map, page->cpu,
	    PVR_FREELIST_PAGE, NULL, BUS_DMA_WAITOK);
	if (error != 0)
		goto fail;
	page->loaded = true;
	if ((page->map->_dm_flags & (_BUS_DMAMAP_COHERENT |
	    _BUS_DMAMAP_IS_BOUNCING)) != _BUS_DMAMAP_COHERENT) {
		error = EOPNOTSUPP;
		goto fail;
	}
	if (page->map->dm_nsegs != 1 ||
	    page->map->dm_mapsize != PVR_FREELIST_PAGE ||
	    page->map->dm_segs[0].ds_len != PVR_FREELIST_PAGE) {
		error = EINVAL;
		goto fail;
	}
	page->pa = page->map->dm_segs[0].ds_addr;
	if ((page->pa & (PVR_FREELIST_PAGE - 1)) != 0 ||
	    page->pa > PVR_FREELIST_PA_END - PVR_FREELIST_PAGE) {
		error = EINVAL;
		goto fail;
	}
	memset(page->cpu, 0, PVR_FREELIST_PAGE);
	bus_dmamap_sync(fl->dmat, page->map, 0, PVR_FREELIST_PAGE,
	    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);
	page->synced = true;
	return 0;
fail:
	pvr_freelist_page_free(fl, page);
	return error;
}

static void
pvr_freelist_release(struct pvr_freelist *fl)
{

	KASSERT(fl->firmware.owner == NULL);
	while (fl->allocated != 0) {
		fl->allocated--;
		pvr_freelist_page_free(fl, &fl->pages[fl->allocated]);
	}
	if (fl->pages != NULL)
		kmem_free(fl->pages, fl->page_capacity * sizeof(*fl->pages));
	kmem_free(fl, sizeof(*fl));
}

int
pvr_freelist_create(bus_dma_tag_t dmat, const struct pvr_backend *be,
    const struct pvr_freelist_config *config, struct pvr_freelist **owner)
{
	struct pvr_freelist *fl;
	struct rogue_fwif_freelist *firmware;
	uint8_t *table;
	uint64_t bytes;
	uint32_t ready, entry, i;
	int error;

	if (dmat == NULL || be == NULL || config == NULL || owner == NULL ||
	    *owner != NULL || be->alloc == NULL || be->free == NULL ||
	    be->sync == NULL || be->command == NULL || be->flush == NULL ||
	    be->check == NULL || be->fail == NULL || config->backing_cpu == NULL ||
	    config->backing_sync == NULL || config->id == 0)
		return EINVAL;
	if (config->max_pages == 0 ||
	    config->max_pages > PVR_FREELIST_MAX_PAGES ||
	    config->initial_pages > config->max_pages ||
	    config->grow_pages > config->max_pages || config->grow_threshold > 100 ||
	    ((config->initial_pages | config->max_pages | config->grow_pages) & 3) != 0 ||
	    (config->initial_pages < config->max_pages && config->grow_pages == 0) ||
	    (config->initial_pages == config->max_pages && config->grow_pages != 0))
		return EINVAL;
	bytes = (uint64_t)config->max_pages * sizeof(uint32_t);
	if (bytes > config->backing_size ||
	    ((config->gpu_va | config->backing_pa) & 15) != 0 ||
	    config->gpu_va >= PVR_FREELIST_VA_END ||
	    bytes > PVR_FREELIST_VA_END - config->gpu_va ||
	    config->backing_pa >= PVR_FREELIST_PA_END ||
	    bytes > PVR_FREELIST_PA_END - config->backing_pa)
		return EINVAL;
	error = be->check(be->cookie);
	if (error != 0)
		return error;
	fl = kmem_zalloc(sizeof(*fl), KM_SLEEP);
	fl->dmat = dmat;
	fl->backend = be;
	fl->config = *config;
	fl->page_capacity = config->initial_pages;
	if (config->initial_pages != 0)
		fl->pages = kmem_zalloc(config->initial_pages * sizeof(*fl->pages),
		    KM_SLEEP);
	while (fl->allocated < config->initial_pages) {
		error = pvr_freelist_page_alloc(fl, &fl->pages[fl->allocated]);
		if (error != 0) {
			pvr_freelist_release(fl);
			return error;
		}
		fl->allocated++;
	}
	error = be->alloc(be->cookie, sizeof(*firmware), &fl->firmware);
	if (error != 0) {
		if (fl->firmware.owner == NULL)
			pvr_freelist_release(fl);
		else
			*owner = fl;
		return error;
	}
	*owner = fl;
	if (fl->firmware.owner == NULL || fl->firmware.cpu == NULL ||
	    fl->firmware.size < sizeof(*firmware) || fl->firmware.address == 0 ||
	    (fl->firmware.address & 7) != 0) {
		fl->failed = true;
		be->fail(be->cookie);
		return EPROTO;
	}
	/* Every physical page exists before the table or FW structure is written. */
	table = config->backing_cpu;
	table += (config->max_pages - config->initial_pages) * sizeof(uint32_t);
	for (i = 0; i < config->initial_pages; i++) {
		entry = htole32((uint32_t)(fl->pages[i].pa >> 12));
		memcpy(table + i * sizeof(entry), &entry, sizeof(entry));
	}
	config->backing_sync(config->sync_cookie,
	    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);
	ready = MIN(config->initial_pages * config->grow_threshold / 100,
	    config->grow_pages) & ~3U;
	fl->current_pages = config->initial_pages - ready;
	fl->ready_pages = ready;
	firmware = fl->firmware.cpu;
	memset(firmware, 0, sizeof(*firmware));
	firmware->freelist_dev_addr = config->gpu_va;
	firmware->current_pages = config->initial_pages - ready;
	firmware->current_dev_addr = (config->gpu_va +
	    (config->max_pages - firmware->current_pages) * sizeof(uint32_t)) &
	    ~UINT64_C(15);
	firmware->current_stack_top = firmware->current_pages - 1;
	firmware->max_pages = config->max_pages;
	firmware->grow_pages = config->grow_pages;
	firmware->ready_pages = ready;
	firmware->freelist_id = config->id;
	fl->initialized = true;
	be->sync(be->cookie, &fl->firmware,
	    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);
	error = be->flush(be->cookie);
	if (error == 0)
		error = be->check(be->cookie);
	if (error == 0)
		fl->ready = true;
	return error;
}

/* The event owner has consumed one validated FWCCB grow request. */
int
pvr_freelist_grow_prepare(struct pvr_freelist *fl,
    struct rogue_fwif_freelist_gs_data *response)
{
	struct rogue_fwif_freelist_gs_data result;
	struct pvr_freelist_page *pages;
	uint8_t *table;
	uint32_t delta = 0, total, count, i, entry;
	int error;

	if (fl == NULL || response == NULL)
		return EINVAL;
	if (fl->failed || !fl->ready || fl->cleaned)
		return EIO;
	if (fl->grow_prepared)
		return EBUSY;
	if (fl->allocated > fl->config.max_pages ||
	    fl->allocated > fl->page_capacity ||
	    fl->ready_pages > fl->allocated ||
	    fl->current_pages != fl->allocated - fl->ready_pages) {
		fl->failed = true;
		fl->backend->fail(fl->backend->cookie);
		return EPROTO;
	}
	/* Firmware has consumed the reserve before it requests host growth. */
	fl->current_pages += fl->ready_pages;
	fl->ready_pages = 0;
	if (fl->config.grow_pages == 0 ||
	    fl->config.grow_pages > fl->config.max_pages - fl->allocated)
		goto respond;
	total = fl->allocated + fl->config.grow_pages;
	pages = kmem_zalloc(total * sizeof(*pages), KM_SLEEP);
	if (fl->allocated != 0)
		memcpy(pages, fl->pages, fl->allocated * sizeof(*pages));
	count = fl->allocated;
	while (count < total) {
		error = pvr_freelist_page_alloc(fl, &pages[count]);
		if (error != 0) {
			/* No new PFN is visible. Existing page ownership is unchanged. */
			while (count > fl->allocated)
				pvr_freelist_page_free(fl, &pages[--count]);
			kmem_free(pages, total * sizeof(*pages));
			goto respond;
		}
		count++;
	}
	/* Adopt every new DMA page before publishing any table entry. */
	if (fl->pages != NULL)
		kmem_free(fl->pages, fl->page_capacity * sizeof(*fl->pages));
	fl->pages = pages;
	fl->page_capacity = total;
	count = fl->allocated;
	fl->allocated = total;
	fl->config.backing_sync(fl->config.sync_cookie,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	table = fl->config.backing_cpu;
	table += (fl->config.max_pages - total) * sizeof(uint32_t);
	for (i = count; i < total; i++) {
		entry = htole32((uint32_t)(pages[i].pa >> 12));
		memcpy(table + (i - count) * sizeof(entry), &entry, sizeof(entry));
	}
	fl->config.backing_sync(fl->config.sync_cookie,
	    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);
	delta = fl->config.grow_pages;
	fl->ready_pages = MIN(total * fl->config.grow_threshold / 100,
	    fl->config.grow_pages) & ~3U;
	fl->current_pages = total - fl->ready_pages;
respond:
	/* Allocation failure is a valid zero-delta response, not a fake grow. */
	memset(&result, 0, sizeof(result));
	result.freelist_fw_addr = fl->firmware.address;
	result.delta_pages = delta;
	result.new_pages = fl->current_pages + fl->ready_pages;
	result.ready_pages = fl->ready_pages;
	fl->grow_prepared = true;
	*response = result;
	return 0;
}

int
pvr_freelist_grow_complete(struct pvr_freelist *fl, int error)
{

	if (fl == NULL || !fl->grow_prepared)
		return EINVAL;
	if (fl->failed)
		return EIO;
	if (error != 0) {
		/* Publication or acknowledgement is ambiguous. Keep all DMA pages. */
		fl->failed = true;
		fl->backend->fail(fl->backend->cookie);
		return error > 0 ? error : EPROTO;
	}
	fl->grow_prepared = false;
	return 0;
}

uint32_t
pvr_freelist_fw_address(const struct pvr_freelist *fl)
{

	return fl != NULL && fl->ready && !fl->cleaned && !fl->failed ?
	    fl->firmware.address : 0;
}

int
pvr_freelist_destroy(struct pvr_freelist **owner)
{
	struct pvr_freelist *fl;
	const struct pvr_backend *be;
	struct rogue_fwif_kccb_cmd command;
	uint8_t *table;
	uint32_t result;
	int error;

	if (owner == NULL || *owner == NULL)
		return EINVAL;
	fl = *owner;
	be = fl->backend;
	if (fl->failed)
		return EIO;
	if (fl->grow_prepared)
		return EBUSY;
	error = be->check(be->cookie);
	if (error != 0)
		return error;
	if (fl->initialized && !fl->cleaned) {
		memset(&command, 0, sizeof(command));
		command.cmd_type = ROGUE_FWIF_KCCB_CMD_CLEANUP;
		command.cmd_data.cleanup_data.cleanup_type = ROGUE_FWIF_CLEANUP_FREELIST;
		command.cmd_data.cleanup_data.cleanup_data.freelist_fw_addr =
		    fl->firmware.address;
		result = 0;
		error = be->command(be->cookie, &command, &result);
		if (error != 0)
			return error;
		if (result == (ROGUE_FWIF_KCCB_RTN_SLOT_CMD_EXECUTED |
		    ROGUE_FWIF_KCCB_RTN_SLOT_CLEANUP_BUSY))
			return EBUSY;
		if (result != ROGUE_FWIF_KCCB_RTN_SLOT_CMD_EXECUTED) {
			fl->failed = true;
			be->fail(be->cookie);
			return EPROTO;
		}
		fl->cleaned = true;
	}
	if (fl->firmware.owner != NULL) {
		error = be->free(be->cookie, &fl->firmware);
		if (error != 0)
			return error;
		memset(&fl->firmware, 0, sizeof(fl->firmware));
	}
	if (fl->initialized) {
		fl->config.backing_sync(fl->config.sync_cookie,
		    BUS_DMASYNC_POSTWRITE | BUS_DMASYNC_POSTREAD);
		table = fl->config.backing_cpu;
		table += (fl->config.max_pages - fl->allocated) * sizeof(uint32_t);
		memset(table, 0, fl->allocated * sizeof(uint32_t));
		fl->config.backing_sync(fl->config.sync_cookie, BUS_DMASYNC_PREWRITE);
	}
	pvr_freelist_release(fl);
	*owner = NULL;
	return 0;
}
