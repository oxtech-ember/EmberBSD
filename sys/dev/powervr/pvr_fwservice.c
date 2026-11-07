/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. AI-assisted native implementation. */
/* Origin: EmberBSD A733 firmware service, factored to the common owner. */
/* Native firmware-object lifetime over the existing MIT Rogue encoders. */
#include <sys/param.h>
#include <sys/endian.h>
#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/systm.h>

#include <dev/powervr/pvr_fwservice.h>
#include <dev/powervr/pvr_fwheap.h>
#include <dev/powervr/pvr_mips.h>

/* Boot code/data and FWIF work precede this allocation arena. */
#define FWSERVICE_FIRST	UINT32_C(0xc0200000)
/* Connection, OS-init and system-init pages start here. */
#define FWSERVICE_END	PVR_FWINIT_CONNECTION_VA
#define FWSERVICE_MAX	(256 * 1024)
#define FWSERVICE_PAGES	(FWSERVICE_MAX / PVR_MIPS_PAGE_SIZE)
#define FWSERVICE_FLUSH_FLAGS (ROGUE_FWIF_MMUCACHEDATA_FLAGS_PT | \
    ROGUE_FWIF_MMUCACHEDATA_FLAGS_PD | ROGUE_FWIF_MMUCACHEDATA_FLAGS_PC | \
    ROGUE_FWIF_MMUCACHEDATA_FLAGS_TLB | ROGUE_FWIF_MMUCACHEDATA_FLAGS_INTERRUPT)
/* Bounded busy wait for a reported in-progress recovery, under the gates. */
#define FWSERVICE_HWR_POLL_US	1000
#define FWSERVICE_HWR_POLLS	250

static bool
fwservice_ready(const struct pvr_fwservice *service)
{

	return service != NULL && service->queue.attached &&
	    !service->queue.failed;
}

int
pvr_fwservice_attach(struct pvr_fwservice *service,
    bus_space_tag_t bst, bus_space_handle_t bsh,
    struct pvr_fwmem *memory,
    const struct pvr_fwboot_result *boot,
    const struct pvr_fwprobe_result *previous)
{
	int error;

	if (service == NULL || bst == NULL)
		return EINVAL;
	if (service->queue.attached || service->objects != NULL)
		return EBUSY;
	error = pvr_fwqueue_attach(memory, boot, previous,
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
pvr_fwservice_set_events(struct pvr_fwservice *service,
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

int
pvr_fwservice_enable_resets(struct pvr_fwservice *service)
{

	if (!fwservice_ready(service))
		return EIO;
	if (service->dispatching)
		return EBUSY;
	service->queue.reset_events = true;
	return 0;
}

bool
pvr_fwservice_context_lost(const struct pvr_fwservice *service, uint32_t id)
{
	unsigned int i;

	if (id == 0)
		return false;
	for (i = 0; i < PVR_FWSERVICE_LOST; i++)
		if (service->lost[i] == id)
			return true;
	return false;
}

static int
fwservice_stuck(struct pvr_fwservice *service)
{

	printf("PVR_HWR_STUCK: recovery still in progress after %u us\n",
	    FWSERVICE_HWR_POLLS * FWSERVICE_HWR_POLL_US);
	service->queue.failed = true;
	return ETIMEDOUT;
}

/* Wait out a transient recovery; persistent recovery freezes the service. */
static int
fwservice_observe(struct pvr_fwservice *service,
    struct pvr_fwprobe_observation *observation)
{
	unsigned int polls;
	int error;

	for (polls = 0; polls < FWSERVICE_HWR_POLLS; polls++) {
		error = pvr_fwqueue_observe(service->memory, service->boot,
		    &service->queue, observation);
		if (error != EAGAIN)
			return error;
		delay(FWSERVICE_HWR_POLL_US);
	}
	return fwservice_stuck(service);
}

static int
fwservice_next(struct pvr_fwservice *service, struct pvr_fwqueue_event *event)
{
	unsigned int polls;
	int error;

	for (polls = 0; polls < FWSERVICE_HWR_POLLS; polls++) {
		error = pvr_fwqueue_next(service->memory, service->boot,
		    &service->queue, event);
		if (error != EAGAIN)
			return error;
		delay(FWSERVICE_HWR_POLL_US);
	}
	return fwservice_stuck(service);
}

/*
 * Firmware has already recovered the hardware and dropped this context's
 * running work. Record the context; its owner refuses further work on it.
 */
static void
fwservice_context_reset(struct pvr_fwservice *service,
    const struct pvr_fwqueue_event *event)
{

	printf("PVR_CONTEXT_RESET: ctx=%u job=%u reason=%u dm=%u flags=%#x\n",
	    event->id, event->job, event->reason, event->dm, event->flags);
	if ((event->flags & ROGUE_FWIF_FWCCB_CMD_CONTEXT_RESET_FLAG_PF) != 0 &&
	    service->queue.page_fault != NULL)
		service->queue.page_fault(service->queue.fault_cookie,
		    event->id, event->job, event->pc, event->fault);
	if (pvr_fwservice_context_lost(service, event->id))
		return;
	service->lost[service->lost_next] = event->id;
	service->lost_next = (service->lost_next + 1) % PVR_FWSERVICE_LOST;
}

/* No callback may recursively enter the transport while it owns an event. */
static int
fwservice_drain(struct pvr_fwservice *service,
    struct pvr_fwprobe_observation *observation)
{
	struct pvr_fwprobe_result result;
	struct rogue_fwif_kccb_cmd command;
	struct pvr_fwqueue_event event;
	uint32_t id;
	unsigned int count;
	int error, completion;

	if (service->dispatching) {
		service->queue.failed = true;
		return EDEADLK;
	}
	service->dispatching = true;
	for (count = 0; count < PVR_FWINIT_FWCCB_COUNT; count++) {
		error = fwservice_next(service, &event);
		if (error == ENOENT) {
			error = 0;
			break;
		}
		if (error != 0)
			break;
		if (event.type ==
		    ROGUE_FWIF_FWCCB_CMD_CONTEXT_RESET_NOTIFICATION) {
			fwservice_context_reset(service, &event);
			continue;
		}
		id = event.id;
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
		error = pvr_fwqueue_cmd(service->bst, service->bsh,
		    service->memory, service->boot, &service->queue, &command,
		    service->queue.expected_log_type, &result);
		if (error == 0)
			error = fwservice_observe(service, observation);
		completion = service->grow_complete(service->event_cookie, id, error);
		if (error == 0)
			error = completion;
		if (error == 0 && service->queue.failed)
			error = EIO;
		if (error != 0)
			break;
	}
	if (error == 0) {
		error = fwservice_observe(service, observation);
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
pvr_fwservice_command(struct pvr_fwservice *service,
    const struct rogue_fwif_kccb_cmd *command,
    struct pvr_fwprobe_result *result)
{
	struct pvr_fwprobe_observation observation;
	int error, dispatch_error;

	if (!fwservice_ready(service))
		return EIO;
	if (service->dispatching) {
		service->queue.failed = true;
		return EDEADLK;
	}
	error = pvr_fwqueue_cmd(service->bst, service->bsh,
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
pvr_fwservice_observe(struct pvr_fwservice *service,
    struct pvr_fwprobe_observation *result)
{
	int error;

	if (!fwservice_ready(service))
		return EIO;
	if (service->dispatching) {
		service->queue.failed = true;
		return EDEADLK;
	}
	error = fwservice_observe(service, result);
	if (error != 0)
		return error;
	error = fwservice_drain(service, result);
	result->error = error;
	return error;
}

int
pvr_fwservice_flush(struct pvr_fwservice *service)
{
	struct rogue_fwif_kccb_cmd command = { 0 };
	struct pvr_fwprobe_result result = { 0 };
	int error;

	if (!fwservice_ready(service))
		return EIO;
	command.cmd_type = ROGUE_FWIF_KCCB_CMD_MMUCACHE;
	command.cmd_data.mmu_cache_data.cache_flags = FWSERVICE_FLUSH_FLAGS;
	command.cmd_data.mmu_cache_data.mmu_cache_sync_fw_addr =
	    service->memory->interface_layout.object[
	    PVR_FWINIT_MMUCACHE_SYNC].fw_addr;
	error = pvr_fwservice_command(service, &command, &result);
	/* Callers may already have published or removed page-table entries. */
	if (error != 0)
		service->queue.failed = true;
	return error;
}

/* Output slots belong to the caller, never to this private object chain. */
static bool
fwservice_slot_valid(const struct pvr_fwservice *service,
    struct pvr_fwobject *const *slot)
{
	const struct pvr_fwobject *obj;

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
fwservice_find(struct pvr_fwservice *service, size_t size,
    uint32_t *address)
{
	const uint8_t *table = service->memory->table.cpu;
	const struct pvr_fwobject *obj;
	uint32_t candidate, end;
	size_t first, count, i;
	bool busy;

	count = size / PVR_MIPS_PAGE_SIZE;
	for (candidate = FWSERVICE_FIRST; candidate <= FWSERVICE_END - size;
	    candidate += PVR_MIPS_PAGE_SIZE) {
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
		first = (candidate - PVR_MIPS_HEAP_BASE) /
		    PVR_MIPS_PAGE_SIZE;
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

/* The GE allocator returns untouched RAM; reject sibling aliases first. */
static bool
fwservice_dma_alias(const struct pvr_dma *a, const struct pvr_dma *b)
{
	uintptr_t acpu = (uintptr_t)a->cpu, bcpu = (uintptr_t)b->cpu;

	return (a->address < b->address + b->size &&
	    b->address < a->address + a->size) ||
	    (acpu <= bcpu ? bcpu - acpu < a->size : acpu - bcpu < b->size);
}

int
pvr_fwservice_alloc(struct pvr_fwservice *service,
    size_t size, struct pvr_fwobject **out)
{
	struct pvr_fwobject *obj, *other;
	struct pvr_fwmem *mem;
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
	aligned = roundup(size, PVR_MIPS_PAGE_SIZE);
	error = fwservice_find(service, aligned, &address);
	if (error != 0)
		return error;
	obj = kmem_zalloc(sizeof(*obj), KM_SLEEP);
	if (mem->profile == PVR_FW_PROFILE_GE8300) {
		error = pvr_fwmem_compute_alloc(mem, aligned, &obj->dma);
		if (error != 0)
			goto discard;
		for (other = service->objects; other != NULL; other = other->next) {
			if (fwservice_dma_alias(&obj->dma, &other->dma)) {
				error = EEXIST;
				goto discard;
			}
		}
		memset(obj->dma.cpu, 0, aligned);
	} else {
		error = pvr_heap_dma_alloc_coherent(mem->dmat, aligned, &obj->dma);
		if (error != 0)
			goto discard;
	}
	obj->fw_addr = address;
	obj->size = size;
	for (i = 0; i < aligned / PVR_MIPS_PAGE_SIZE; i++)
		pages[i] = obj->dma.address + i * PVR_MIPS_PAGE_SIZE;
	bus_dmamap_sync(mem->dmat, obj->dma.map, 0, obj->dma.size,
	    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);
	bus_dmamap_sync(mem->dmat, mem->table.map, 0, mem->table.size,
	    BUS_DMASYNC_POSTWRITE);
	/* Coherent page-table writes can be visible before the following sync. */
	if (mem->profile == PVR_FW_PROFILE_GE8300) {
		obj->dma.device_exposed = true;
		obj->mapped = true;
		obj->next = service->objects;
		service->objects = obj;
	}
	error = pvr_mips_map(mem->table.cpu, mem->table.size,
	    address, pages, aligned / PVR_MIPS_PAGE_SIZE);
	if (error != 0) {
		if (mem->profile == PVR_FW_PROFILE_GE8300) {
			service->queue.failed = true;
			return error;
		}
		bus_dmamap_sync(mem->dmat, obj->dma.map, 0, obj->dma.size,
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
		goto discard;
	}
	if (mem->profile != PVR_FW_PROFILE_GE8300) {
		/* A733 keeps its existing ownership and publication order. */
		obj->mapped = true;
		obj->next = service->objects;
		service->objects = obj;
	}
	bus_dmamap_sync(mem->dmat, mem->table.map, 0, mem->table.size,
	    BUS_DMASYNC_PREWRITE);
	error = pvr_fwservice_flush(service);
	if (error != 0)
		return error;
	*out = obj;
	return 0;
discard:
	if (mem->profile == PVR_FW_PROFILE_GE8300) {
		if (obj->dma.compute_owner == mem)
			(void)pvr_fwmem_compute_free(mem, &obj->dma);
	} else
		pvr_heap_dma_free(mem->dmat, &obj->dma);
	kmem_free(obj, sizeof(*obj));
	return error;
}

void
pvr_fwservice_sync(struct pvr_fwservice *service,
    struct pvr_fwobject *obj, int operations)
{

	bus_dmamap_sync(service->memory->dmat, obj->dma.map, 0,
	    obj->dma.size, operations);
}

int
pvr_fwservice_free(struct pvr_fwservice *service,
    struct pvr_fwobject **slot)
{
	struct pvr_fwobject *obj, **link;
	struct pvr_fwmem *mem;
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
	count = obj->dma.size / PVR_MIPS_PAGE_SIZE;
	if (count == 0 || count > FWSERVICE_PAGES ||
	    obj->dma.size % PVR_MIPS_PAGE_SIZE != 0) {
		service->queue.failed = true;
		return EINVAL;
	}
	for (i = 0; i < count; i++)
		pages[i] = obj->dma.address + i * PVR_MIPS_PAGE_SIZE;
	bus_dmamap_sync(mem->dmat, mem->table.map, 0, mem->table.size,
	    BUS_DMASYNC_POSTWRITE);
	error = pvr_mips_unmap(mem->table.cpu, mem->table.size,
	    obj->fw_addr, pages, count);
	if (error != 0) {
		service->queue.failed = true;
		return error;
	}
	obj->mapped = false;
	bus_dmamap_sync(mem->dmat, mem->table.map, 0, mem->table.size,
	    BUS_DMASYNC_PREWRITE);
	error = pvr_fwservice_flush(service);
	if (error != 0)
		return error;
	/* Caller has retired firmware references; unmap and flush are complete. */
	bus_dmamap_sync(mem->dmat, obj->dma.map, 0, obj->dma.size,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	if (mem->profile == PVR_FW_PROFILE_GE8300) {
		obj->dma.device_exposed = false;
		error = pvr_fwmem_compute_free(mem, &obj->dma);
		if (error != 0) {
			obj->dma.device_exposed = true;
			service->queue.failed = true;
			return error;
		}
	} else
		pvr_heap_dma_free(mem->dmat, &obj->dma);
	*link = obj->next;
	kmem_free(obj, sizeof(*obj));
	*slot = NULL;
	return 0;
}
