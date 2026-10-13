/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. AI-assisted native implementation. */
/* Native firmware-object lifetime over the existing MIT Rogue encoders. */
#include <sys/param.h>
#include <sys/endian.h>
#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/systm.h>

#include <arm/sunxi/sun60i_a733_gpu_fwservice.h>
#include <arm/sunxi/sun60i_a733_gpu_mips.h>

/* Boot code/data and FWIF work precede this allocation arena. */
#define FWSERVICE_FIRST	UINT32_C(0xc0200000)
/* Connection, OS-init and system-init pages start here. */
#define FWSERVICE_END	A733_GPU_FWINIT_CONNECTION_VA
#define FWSERVICE_MAX	(256 * 1024)
#define FWSERVICE_PAGES	(FWSERVICE_MAX / A733_GPU_MIPS_PAGE_SIZE)
#define FWSERVICE_FLUSH_FLAGS (ROGUE_FWIF_MMUCACHEDATA_FLAGS_PT | \
    ROGUE_FWIF_MMUCACHEDATA_FLAGS_PD | ROGUE_FWIF_MMUCACHEDATA_FLAGS_PC | \
    ROGUE_FWIF_MMUCACHEDATA_FLAGS_TLB | ROGUE_FWIF_MMUCACHEDATA_FLAGS_INTERRUPT)

static bool
fwservice_ready(const struct sun60i_a733_gpu_fwservice *service)
{

	return service != NULL && service->queue.attached &&
	    !service->queue.failed;
}

int
sun60i_a733_gpu_fwservice_attach(struct sun60i_a733_gpu_fwservice *service,
    bus_space_tag_t bst, bus_space_handle_t bsh,
    struct sun60i_a733_gpu_fwmem *memory,
    const struct sun60i_a733_gpu_fwboot_result *boot,
    const struct sun60i_a733_gpu_fwprobe_result *previous)
{
	int error;

	if (service == NULL || bst == NULL)
		return EINVAL;
	if (service->queue.attached || service->objects != NULL)
		return EBUSY;
	error = sun60i_a733_gpu_fwqueue_attach(memory, boot, previous,
	    &service->queue);
	if (error != 0)
		return error;
	service->memory = memory;
	service->boot = boot;
	service->bst = bst;
	service->bsh = bsh;
	return 0;
}

int
sun60i_a733_gpu_fwservice_set_events(struct sun60i_a733_gpu_fwservice *service,
    void *cookie,
    int (*prepare)(void *, uint32_t, struct rogue_fwif_freelist_gs_data *),
    int (*complete)(void *, uint32_t, int))
{

	if (cookie == NULL || prepare == NULL || complete == NULL)
		return EINVAL;
	if (!fwservice_ready(service))
		return EIO;
	if (service->events_set || service->dispatching)
		return EBUSY;
	service->event_cookie = cookie;
	service->grow_prepare = prepare;
	service->grow_complete = complete;
	service->events_set = true;
	return 0;
}

/* No callback may recursively enter the transport while it owns an event. */
static int
fwservice_drain(struct sun60i_a733_gpu_fwservice *service,
    struct sun60i_a733_gpu_fwprobe_observation *observation)
{
	struct sun60i_a733_gpu_fwprobe_result result;
	struct rogue_fwif_kccb_cmd command;
	uint32_t id;
	unsigned int count;
	int error, completion;

	if (service->dispatching) {
		service->queue.failed = true;
		return EDEADLK;
	}
	service->dispatching = true;
	for (count = 0; count < A733_GPU_FWINIT_FWCCB_COUNT; count++) {
		error = sun60i_a733_gpu_fwqueue_event(service->memory, service->boot,
		    &service->queue, &id);
		if (error == ENOENT) {
			error = 0;
			break;
		}
		if (error != 0)
			break;
		if (!service->events_set) {
			error = EOPNOTSUPP;
			break;
		}
		memset(&command, 0, sizeof(command));
		command.cmd_type = ROGUE_FWIF_KCCB_CMD_FREELIST_GROW_UPDATE;
		error = service->grow_prepare(service->event_cookie, id,
		    &command.cmd_data.free_list_gs_data);
		if (error != 0)
			break;
		memset(&result, 0, sizeof(result));
		error = sun60i_a733_gpu_fwqueue_cmd(service->bst, service->bsh,
		    service->memory, service->boot, &service->queue, &command,
		    service->queue.expected_log_type, &result);
		if (error == 0)
			error = sun60i_a733_gpu_fwqueue_observe(service->memory,
			    service->boot, &service->queue, observation);
		completion = service->grow_complete(service->event_cookie, id, error);
		if (error == 0)
			error = completion;
		if (error == 0 && service->queue.failed)
			error = EIO;
		if (error != 0)
			break;
	}
	if (error == 0) {
		error = sun60i_a733_gpu_fwqueue_observe(service->memory,
		    service->boot, &service->queue, observation);
		if (error == 0 && observation->state.fw_read_offset !=
		    observation->state.fw_write_offset)
			error = ELOOP;
	}
	service->dispatching = false;
	if (error != 0)
		service->queue.failed = true;
	return error > 0 ? error : error == 0 ? 0 : EPROTO;
}

int
sun60i_a733_gpu_fwservice_command(struct sun60i_a733_gpu_fwservice *service,
    const struct rogue_fwif_kccb_cmd *command,
    struct sun60i_a733_gpu_fwprobe_result *result)
{
	struct sun60i_a733_gpu_fwprobe_observation observation;
	int error, dispatch_error;

	if (!fwservice_ready(service))
		return EIO;
	if (service->dispatching) {
		service->queue.failed = true;
		return EDEADLK;
	}
	error = sun60i_a733_gpu_fwqueue_cmd(service->bst, service->bsh,
	    service->memory, service->boot, &service->queue, command,
	    service->queue.expected_log_type, result);
	if (error != 0 && (error != EBUSY || service->queue.failed))
		return error;
	dispatch_error = fwservice_drain(service, &observation);
	if (dispatch_error != 0) {
		result->error = dispatch_error;
		return dispatch_error;
	}
	return error;
}

int
sun60i_a733_gpu_fwservice_observe(struct sun60i_a733_gpu_fwservice *service,
    struct sun60i_a733_gpu_fwprobe_observation *result)
{
	int error;

	if (!fwservice_ready(service))
		return EIO;
	if (service->dispatching) {
		service->queue.failed = true;
		return EDEADLK;
	}
	error = sun60i_a733_gpu_fwqueue_observe(service->memory, service->boot,
	    &service->queue, result);
	if (error != 0)
		return error;
	error = fwservice_drain(service, result);
	result->error = error;
	return error;
}

int
sun60i_a733_gpu_fwservice_flush(struct sun60i_a733_gpu_fwservice *service)
{
	struct rogue_fwif_kccb_cmd command = { 0 };
	struct sun60i_a733_gpu_fwprobe_result result = { 0 };
	int error;

	if (!fwservice_ready(service))
		return EIO;
	command.cmd_type = ROGUE_FWIF_KCCB_CMD_MMUCACHE;
	command.cmd_data.mmu_cache_data.cache_flags = FWSERVICE_FLUSH_FLAGS;
	command.cmd_data.mmu_cache_data.mmu_cache_sync_fw_addr =
	    service->memory->interface_layout.object[
	    A733_GPU_FWINIT_MMUCACHE_SYNC].fw_addr;
	error = sun60i_a733_gpu_fwservice_command(service, &command, &result);
	/* Callers may already have published or removed page-table entries. */
	if (error != 0)
		service->queue.failed = true;
	return error;
}

/* Output slots belong to the caller, never to this private object chain. */
static bool
fwservice_slot_valid(const struct sun60i_a733_gpu_fwservice *service,
    struct sun60i_a733_gpu_fwobject *const *slot)
{
	const struct sun60i_a733_gpu_fwobject *obj;

	if (slot == &service->objects)
		return false;
	for (obj = service->objects; obj != NULL; obj = obj->next) {
		if (slot == &obj->next)
			return false;
	}
	return true;
}

/* Preserve pre-existing firmware and diagnostic mappings in this arena. */

static int
fwservice_find(struct sun60i_a733_gpu_fwservice *service, size_t size,
    uint32_t *address)
{
	const uint8_t *table = service->memory->table.cpu;
	const struct sun60i_a733_gpu_fwobject *obj;
	uint32_t candidate, end;
	size_t first, count, i;
	bool busy;

	count = size / A733_GPU_MIPS_PAGE_SIZE;
	for (candidate = FWSERVICE_FIRST; candidate <= FWSERVICE_END - size;
	    candidate += A733_GPU_MIPS_PAGE_SIZE) {
		end = candidate + size;
		busy = false;
		/* A removed PTE can still belong to a failed retirement. */
		for (obj = service->objects; obj != NULL; obj = obj->next) {
			if (candidate < obj->fw_addr + obj->dma.size &&
			    obj->fw_addr < end) {
				busy = true;
				break;
			}
		}
		if (busy)
			continue;
		first = (candidate - A733_GPU_MIPS_HEAP_BASE) /
		    A733_GPU_MIPS_PAGE_SIZE;
		for (i = 0; i < count; i++) {
			if (le32dec(table + (first + i) * 4) != 0)
				break;
		}
		if (i == count) {
			*address = candidate;
			return 0;
		}
	}
	return ENOSPC;
}

int
sun60i_a733_gpu_fwservice_alloc(struct sun60i_a733_gpu_fwservice *service,
    size_t size, struct sun60i_a733_gpu_fwobject **out)
{
	struct sun60i_a733_gpu_fwobject *obj;
	struct sun60i_a733_gpu_fwmem *mem;
	uint64_t pages[FWSERVICE_PAGES];
	size_t aligned, i;
	uint32_t address;
	int error;

	if (out == NULL || *out != NULL || size == 0 || size > FWSERVICE_MAX)
		return EINVAL;
	if (!fwservice_ready(service))
		return EIO;
	if (!fwservice_slot_valid(service, out))
		return EINVAL;
	mem = service->memory;
	aligned = roundup(size, A733_GPU_MIPS_PAGE_SIZE);
	error = fwservice_find(service, aligned, &address);
	if (error != 0)
		return error;
	obj = kmem_zalloc(sizeof(*obj), KM_SLEEP);
	error = sun60i_a733_gpu_dma_alloc_coherent(mem->dmat, aligned, &obj->dma);
	if (error != 0)
		goto discard;
	obj->fw_addr = address;
	obj->size = size;
	for (i = 0; i < aligned / A733_GPU_MIPS_PAGE_SIZE; i++)
		pages[i] = obj->dma.address + i * A733_GPU_MIPS_PAGE_SIZE;
	bus_dmamap_sync(mem->dmat, obj->dma.map, 0, obj->dma.size,
	    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);
	bus_dmamap_sync(mem->dmat, mem->table.map, 0, mem->table.size,
	    BUS_DMASYNC_POSTWRITE);
	error = sun60i_a733_gpu_mips_map(mem->table.cpu, mem->table.size,
	    address, pages, aligned / A733_GPU_MIPS_PAGE_SIZE);
	if (error != 0) {
		bus_dmamap_sync(mem->dmat, obj->dma.map, 0, obj->dma.size,
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
		goto discard;
	}
	/* Ownership is retained before the first table publication. */
	obj->mapped = true;
	obj->next = service->objects;
	service->objects = obj;
	bus_dmamap_sync(mem->dmat, mem->table.map, 0, mem->table.size,
	    BUS_DMASYNC_PREWRITE);
	error = sun60i_a733_gpu_fwservice_flush(service);
	if (error != 0)
		return error;
	*out = obj;
	return 0;
discard:
	sun60i_a733_gpu_dma_free(mem->dmat, &obj->dma);
	kmem_free(obj, sizeof(*obj));
	return error;
}

void
sun60i_a733_gpu_fwservice_sync(struct sun60i_a733_gpu_fwservice *service,
    struct sun60i_a733_gpu_fwobject *obj, int operations)
{

	bus_dmamap_sync(service->memory->dmat, obj->dma.map, 0,
	    obj->dma.size, operations);
}

int
sun60i_a733_gpu_fwservice_free(struct sun60i_a733_gpu_fwservice *service,
    struct sun60i_a733_gpu_fwobject **slot)
{
	struct sun60i_a733_gpu_fwobject *obj, **link;
	struct sun60i_a733_gpu_fwmem *mem;
	uint64_t pages[FWSERVICE_PAGES];
	size_t count, i;
	int error;

	if (slot == NULL || *slot == NULL)
		return EINVAL;
	if (!fwservice_ready(service))
		return EIO;
	obj = *slot;
	for (link = &service->objects; *link != obj; link = &(*link)->next) {
		if (*link == NULL)
			return ENOENT;
	}
	if (slot == link)
		return EINVAL;
	mem = service->memory;
	KASSERT(obj->mapped);
	count = obj->dma.size / A733_GPU_MIPS_PAGE_SIZE;
	if (count == 0 || count > FWSERVICE_PAGES ||
	    obj->dma.size % A733_GPU_MIPS_PAGE_SIZE != 0) {
		service->queue.failed = true;
		return EINVAL;
	}
	for (i = 0; i < count; i++)
		pages[i] = obj->dma.address + i * A733_GPU_MIPS_PAGE_SIZE;
	bus_dmamap_sync(mem->dmat, mem->table.map, 0, mem->table.size,
	    BUS_DMASYNC_POSTWRITE);
	error = sun60i_a733_gpu_mips_unmap(mem->table.cpu, mem->table.size,
	    obj->fw_addr, pages, count);
	if (error != 0) {
		service->queue.failed = true;
		return error;
	}
	obj->mapped = false;
	bus_dmamap_sync(mem->dmat, mem->table.map, 0, mem->table.size,
	    BUS_DMASYNC_PREWRITE);
	error = sun60i_a733_gpu_fwservice_flush(service);
	if (error != 0)
		return error;
	/* A completed invalidation is the first point at which RAM is reusable. */
	*link = obj->next;
	bus_dmamap_sync(mem->dmat, obj->dma.map, 0, obj->dma.size,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	sun60i_a733_gpu_dma_free(mem->dmat, &obj->dma);
	kmem_free(obj, sizeof(*obj));
	*slot = NULL;
	return 0;
}
