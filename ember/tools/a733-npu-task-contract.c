/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD, AI-assisted; native API lifecycle fault controls. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

typedef uint64_t bus_addr_t;
typedef void *bus_space_tag_t;
typedef uintptr_t bus_space_handle_t;
typedef struct {
	bool held;
	int ipl;
} kmutex_t;
typedef size_t bus_size_t;
typedef struct fake_tag {
	unsigned refs;
	bool parent;
}       *bus_dma_tag_t;
typedef struct {
	uint64_t ds_addr, ds_len;
	unsigned id;
} bus_dma_segment_t;
typedef struct fake_map {
	int dm_nsegs, _dm_flags;
	size_t dm_mapsize;
	bus_dma_segment_t dm_segs[1];
	unsigned id;
}       *bus_dmamap_t;
#define BUS_DMA_WAITOK 1
#define BUS_DMASYNC_PREWRITE 2
#define BUS_DMASYNC_PREREAD 4
#define BUS_DMASYNC_POSTWRITE 8
#define BUS_DMASYNC_POSTREAD 16
#define KM_SLEEP 1
#define _BUS_DMAMAP_IS_BOUNCING 0x20000
static unsigned checks;
#define CHECK(c) do { checks++; if (!(c)) { \
	fprintf(stderr, "check %u failed at line %d: %s\n", \
	    checks, __LINE__, #c); exit(1); } } while (0)

static struct fake_tag parent_tag = {0, true};
static struct slot {
	void *cpu;
	size_t bytes;
	uint64_t address;
}    slots[6];
static unsigned stack[32], depth, calls, fail_call, allocated, current,
         syncs;
static bool borrowed, task_mode;


static unsigned bad_at, bad_kind;
static uint64_t addresses[6];

static void push(unsigned token){
	CHECK(depth < 32);
	stack[depth++] = token;
}
static void pop(unsigned token){
	CHECK(depth > 0 && stack[--depth] == token);
}
static bool
fails(void)
{
	return ++calls == fail_call;
}
static void *
kmem_zalloc(size_t n, int flags)
{
	CHECK(flags == KM_SLEEP);
	void *p = calloc(1, n);
	CHECK(p != NULL);
	push(1);
	return p;
}
static void
kmem_free(void *p, size_t n)
{
	(void)n;
	pop(1);
	free(p);
}
static int
bus_dmatag_subregion(bus_dma_tag_t parent, bus_addr_t lo,
    bus_addr_t hi, bus_dma_tag_t * out, int flags)
{
	CHECK(parent == &parent_tag && lo == 0 && hi == UINT32_MAX);
	CHECK(flags == BUS_DMA_WAITOK);
	if (fails())
		return EIO;
	if (borrowed)
		*out = parent;
	else {
		*out = calloc(1, sizeof(**out));
		CHECK(*out != NULL);
	}
	(*out)->refs++;
	push(2);
	return 0;
}
static void
bus_dmatag_destroy(bus_dma_tag_t t)
{
	pop(2);
	CHECK(t != NULL && t->refs == 1);
	t->refs--;
	if (!t->parent)
		free(t);
}
static int
bus_dmamem_alloc(bus_dma_tag_t tag, bus_size_t n,
    bus_size_t align, bus_size_t boundary, bus_dma_segment_t * seg,
    int maxseg, int *count, int flags)
{
	CHECK(tag->refs == 1 && align == 4096 && boundary == 0);
	CHECK(maxseg == 1 && flags == BUS_DMA_WAITOK);
	if (fails())
		return EIO;
	CHECK(allocated < (task_mode ? 6U : 3U));
	current = allocated++;
	slots[current].bytes = n;
	slots[current].address = addresses[current];
	*seg = (bus_dma_segment_t) {
		addresses[current], n, current
	};
	*count = 1;
	push(10 + current * 4);
	return 0;
}
static void
bus_dmamem_free(bus_dma_tag_t tag, bus_dma_segment_t * seg, int count)
{
	CHECK(tag->refs == 1 && count == 1);
	pop(10 + seg->id * 4);
}
static int
bus_dmamem_map(bus_dma_tag_t tag, bus_dma_segment_t * seg,
    int count, size_t n, void **out, int flags)
{
	CHECK(tag->refs == 1 && count == 1 && flags == BUS_DMA_WAITOK);
	CHECK(n == slots[seg->id].bytes);
	if (fails())
		return EIO;
	*out = malloc(n);
	CHECK(*out != NULL);
	memset(*out, 0xa5, n);
	slots[seg->id].cpu = *out;
	push(11 + seg->id * 4);
	return 0;
}
static void
bus_dmamem_unmap(bus_dma_tag_t tag, void *cpu, size_t n)
{
	CHECK(tag->refs == 1);
	unsigned id;
	for (id = 0; id < 6; id++)
		if (slots[id].cpu == cpu)
			break;
	CHECK(id < 6 && n == slots[id].bytes);
	pop(11 + id * 4);
	free(cpu);
}
static int
bus_dmamap_create(bus_dma_tag_t tag, bus_size_t n,
    int maxseg, bus_size_t maxsize, bus_size_t boundary, int flags,
    bus_dmamap_t * out)
{
	CHECK(tag->refs == 1 && maxseg == 1 && n == maxsize && boundary == 0);
	CHECK(flags == BUS_DMA_WAITOK);
	if (fails())
		return EIO;
	*out = calloc(1, sizeof(**out));
	CHECK(*out != NULL);
	(*out)->id = current;
	push(12 + current * 4);
	return 0;
}
static void
bus_dmamap_destroy(bus_dma_tag_t tag, bus_dmamap_t map)
{
	CHECK(tag->refs == 1);
	pop(12 + map->id * 4);
	free(map);
}
static int
bus_dmamap_load(bus_dma_tag_t tag, bus_dmamap_t map,
    void *cpu, bus_size_t n, void *proc, int flags)
{
	CHECK(tag->refs == 1 && proc == NULL && flags == BUS_DMA_WAITOK);
	CHECK(cpu == slots[map->id].cpu && n == slots[map->id].bytes);
	if (fails())
		return EIO;
	map->dm_nsegs = 1;
	map->dm_mapsize = n;
	map->dm_segs[0] = (bus_dma_segment_t) {
		slots[map->id].address, n, map->id
	};
	if (map->id == bad_at) {
		switch (bad_kind) {
		case 1:
			map->_dm_flags = _BUS_DMAMAP_IS_BOUNCING;
			break;
		case 2:
			map->dm_nsegs = 2;
			break;
		case 3:
			map->dm_mapsize--;
			break;
		case 4:
			map->dm_segs[0].ds_len--;
			break;
		case 5:
			map->dm_mapsize++;
			break;
		case 6:
			map->dm_segs[0].ds_len++;
			break;
		default:
			break;
		}
	}
	push(13 + map->id * 4);
	return 0;
}
static void
bus_dmamap_unload(bus_dma_tag_t tag, bus_dmamap_t map)
{
	CHECK(tag->refs == 1);
	pop(13 + map->id * 4);
}
static void task_sync(bus_dmamap_t, bus_size_t, int);
static void
bus_dmamap_sync(bus_dma_tag_t tag, bus_dmamap_t map,
    bus_addr_t offset, bus_size_t n, int flags)
{
	CHECK(tag->refs == 1 && offset == 0);
	task_sync(map, n, flags);
}
#define MUTEX_DEFAULT 0
#define IPL_VM 5
#define FDT_INTR_MPSAFE 1
#define BUS_SPACE_BARRIER_WRITE 2
#define BUS_SPACE_BARRIER_READ 1
struct clk {
	unsigned id;
};
#include "sun60i_a733_npu_mmu.h"
#include "sun60i_a733_npu_nn.h"
#include "sun60i_a733_npu_dma.h"
#include "sun60i_a733_npu_fe.h"
#include "sun60i_a733_npu_run.h"
#include "sun60i_a733_npu_task.h"
#include "sun60i_a733_npu_mmu.c"
#include "sun60i_a733_npu_nn.c"
#include "sun60i_a733_npu_dma.c"
static struct sun60i_a733_npu_run dummy, *rstate = &dummy;
static struct sun60i_a733_npu_dma *active;
static struct sun60i_a733_npu_task *current_task;
#define state (*rstate)
#define context (*active)

static struct clk clock1;
static const int cookie;
static int bus_token, irq_token;
static uint32_t regs[1024], write_offsets[64], write_values[64];
static unsigned maps, unmaps, established, removed, proofs, peeks, pokes;
static unsigned writes, ordinary_reads, barriers, delays, setters, retains;
static unsigned fail_proof, fail_peek, fail_poke;
static bool fail_map, fail_irq, fail_set, fail_retain, bad_mask, no_irq;
static bool in_irq, late_error, pending_irq, start_drained;
static int property_bytes;
static uint32_t irq_value;
static unsigned bad_route, unstable_peek, mismatch_step, bad_info, infos,
         fail_info;
static bool prestart_irq, clean_retain;
static uint32_t terminal_idle, terminal_address;
static bool mask_drained, address_drained, terminal_unstable;
static unsigned dma_reads;
static uint32_t job_status_xor, fresh_reset_unknown;
static uint32_t command_readback, command_requested;
static unsigned command_writes;
static bool lose_command_address;
static int (*callback) (void *);
static void *callback_arg;
static unsigned final_reads, admission_reads, post_ready_calls;
static unsigned admission_fail, admission_flip, final_fail, final_flip;
static uint32_t final_offsets[7];
static bool output_enabled, late_after_compare, fail_post_ready;
static unsigned corrupt_buffer, corrupt_byte, bad_capability;
static unsigned identity_override_field;
static uint32_t identity_override_bits;
static int task_peek(bus_size_t, uint32_t *);
static void task_unlock_probe(const kmutex_t *);
static int task_ready_probe(void);
static void task_device_work(void);
static void
mutex_init(kmutex_t * m, int type, int ipl)
{
	CHECK(type == MUTEX_DEFAULT && ipl == IPL_VM);
	m->held = false;
	m->ipl = ipl;
}
static void
mutex_destroy(kmutex_t * m)
{
	CHECK(!m->held && m->ipl == IPL_VM);
}
static void
mutex_enter(kmutex_t * m)
{
	CHECK(m->ipl == IPL_VM && !m->held);
	m->held = true;
}
static void
mutex_exit(kmutex_t * m)
{
	CHECK(m->held);
	task_unlock_probe(m);
	m->held = false;
	if (pending_irq && m == &state.irq_lock) {
		CHECK(start_drained);
		pending_irq = false;
		in_irq = true;
		CHECK(callback(callback_arg) == (irq_value == 0 ? 0 : 1));
		in_irq = false;
	}
}
static int
fdtbus_intr_parent(int phandle)
{
	CHECK(phandle == 7);
	return bad_route == 1 ? -1 : 8;
}
static bool
fdtbus_status_okay(int phandle)
{
	CHECK(phandle == 7 || phandle == 8);
	return bad_route != (phandle == 7 ? 2U : 3U);
}
static int
of_compatible(int phandle, const char *const *names)
{
	CHECK(phandle == 8 && !strcmp(names[0], "arm,gic-v3") && names[1] ==
	    NULL);
	return bad_route != 4;
}
static int
OF_getproplen(int phandle, const char *name)
{
	CHECK(phandle == 7 || phandle == 8);
	if (phandle == 7) {
		if (!strcmp(name, "interrupts"))
			return property_bytes;
		CHECK(!strcmp(name, "interrupts-extended"));
		return bad_route == 5 ? 12 : -1;
	}
	if (!strcmp(name, "interrupt-controller"))
		return bad_route == 6 ? -1 : 0;
	if (!strcmp(name, "#interrupt-cells"))
		return bad_route == 7 ? 8 : 4;
	CHECK(!strcmp(name, "reg"));
	return bad_route == 8 ? 24 : 16;
}
static int
of_getprop_uint32_array(int phandle, const char *name,
    uint32_t * out, int n)
{
	if (phandle == 8) {
		CHECK(!strcmp(name, "#interrupt-cells") && n == 1);
		*out = bad_route == 9 ? 2 : 3;
		return bad_route == 10 ? EIO : 0;
	}
	CHECK(phandle == 7 && !strcmp(name, "interrupts") && n == 3);
	out[0] = bad_route == 11 ? 1 : 0;
	out[1] = bad_route == 12 ? 64 : 65;
	out[2] = bad_route == 13 ? 1 : 4;
	return bad_route == 14 ? EIO : 0;
}
static int
fdtbus_get_reg(int phandle, unsigned slot, bus_addr_t * addr,
    bus_size_t * size)
{
	CHECK(phandle == 8 && slot < 2);
	*addr = slot == 0 ? 0x03400000 : 0x03460000;
	*size = slot == 0 ? 0x10000 : 0x100000;
	if (bad_route == 15 + slot * 3)
		return EIO;
	if (bad_route == 16 + slot * 3)
		(*addr)++;
	if (bad_route == 17 + slot * 3)
		(*size)++;
	return 0;
}
static int
sun60i_a733_ccu_npu_ready(struct clk *clock, const void *owner)
{
	CHECK(!in_irq && clock == &clock1 && owner == &cookie);
	proofs++;
	if (task_ready_probe() != 0)
		return EIO;
	if (prestart_irq && proofs == 9) {
		regs[4] = irq_value;
		in_irq = true;
		CHECK(callback(callback_arg) == 1);
		in_irq = false;
	}
	return proofs == fail_proof ? EIO : 0;
}
static int
sun60i_a733_pck_npu_ready(int power, const void *owner)
{
	CHECK(!in_irq && power == 9 && owner == &cookie);
	return ++proofs == fail_proof ? EIO : 0;
}
static int
bus_space_map(bus_space_tag_t tag, uint64_t addr, bus_size_t size,
    int flags, bus_space_handle_t * handle)
{
	CHECK(!in_irq && tag == &bus_token && addr == 0x03600000 && size ==
	    4096 && !flags);
	maps++;
	if (fail_map)
		return ENOMEM;
	*handle = 0x55;
	return 0;
}
static void
bus_space_unmap(bus_space_tag_t tag, bus_space_handle_t handle,
    bus_size_t bytes)
{
	CHECK(!in_irq && !context.retained && tag == &bus_token && handle ==
	    0x55 && bytes == 4096);
	unmaps++;
}
static bool
valid_offset(bus_size_t offset)
{
	static const unsigned allowed[] = {0, 4, 0x10, 0x14, 0x24, 0x30,
	    0x388, 0x3a4,
	0x20, 0x34, 0xa0, 0xa8, 0xe8, 0x3b4, 0x654, 0x664, 0x668, 0x66c, 0x90, 0x100,
	    0x104, 0x10c, 0x3a8, 0x414};
	for (size_t i = 0; i < sizeof(allowed) / sizeof(allowed[0]); i++)
		if (offset == allowed[i])
			return true;
	return false;
}
static int
bus_space_peek_4(bus_space_tag_t tag, bus_space_handle_t h,
    bus_size_t offset, uint32_t * out)
{
	CHECK(!in_irq && tag == &bus_token && h == 0x55 &&
	    valid_offset(offset));
	if (++peeks == fail_peek)
		return EFAULT;
	*out = regs[offset / 4];
	if (task_peek(offset, out) != 0)
		return EFAULT;
	if (peeks == unstable_peek)
		*out ^= 2;
	if (mismatch_step && state.status.step == mismatch_step && pokes !=
	    0 &&
	    offset == write_offsets[writes - 1]) {
		*out ^= offset == 0x10c ? 0x20000 : offset == 0x90 ? 0x40 : 1;
		mismatch_step = 0;
	}
	if (offset == 0x14 && pokes && write_values[writes - 1] == 0)
		CHECK(mask_drained);
	if (offset == 0x664 && state.status.started && terminal_unstable &&
	    ++dma_reads % 2 == 0)
		*out ^= 8;
	if (offset == 0x10) {
		CHECK(state.status.publication_attempted &&
		    state.irq_lock.held);
		regs[4] = 0;
	}
	return 0;
}
static void
record_write(bus_size_t offset, uint32_t value)
{
	CHECK(context.retained && state.status.publication_attempted &&
	    writes < 64);
	write_offsets[writes] = (uint32_t) offset;
	write_values[writes++] = value;
}
static void
bus_space_write_4(bus_space_tag_t tag, bus_space_handle_t h,
    bus_size_t offset, uint32_t value)
{
	CHECK(in_irq && state.irq_lock.held && tag == &bus_token && h == 0x55);
	CHECK(offset == 0x14 && value == 0);
	record_write(offset, value);
	regs[5] = value;
	mask_drained = false;
	if (late_error)
		regs[4] = 0x40000000;
}
static uint32_t bus_space_read_4(bus_space_tag_t tag, bus_space_handle_t h,
    bus_size_t offset) {
	CHECK(in_irq && state.irq_lock.held && tag == &bus_token && h == 0x55);
	CHECK(offset == 0x10 || offset == 0x14);
	ordinary_reads++;
	uint32_t value = regs[offset / 4];
	if (offset == 0x10)
		regs[4] = 0;
	if (offset == 0x14)
		CHECK(mask_drained);
	if (offset == 0x14 && bad_mask)
		value = 1;
	return value;
}
static void
bus_space_barrier(bus_space_tag_t tag, bus_space_handle_t h,
    bus_size_t offset, bus_size_t bytes, int flags)
{
	CHECK(tag == &bus_token && h == 0x55 && bytes == 4);
	CHECK((offset == 0x14 || offset == 0x654 || offset == 0x3a4) &&
	    flags == BUS_SPACE_BARRIER_WRITE);
	if (in_irq)
		CHECK(offset == 0x14 && state.irq_lock.held);
	if (offset == 0x14)
		mask_drained = true;
	if (offset == 0x654)
		address_drained = true;
	if (offset == 0x3a4) {
		CHECK(state.irq_lock.held);
		start_drained = true;
	}
	barriers++;
}
static int
bus_space_poke_4(bus_space_tag_t tag, bus_space_handle_t h,
    bus_size_t offset, uint32_t value)
{
	CHECK(!in_irq && tag == &bus_token && h == 0x55 &&
	    valid_offset(offset));
	CHECK(context.retained && state.status.publication_attempted);
	if (++pokes == fail_poke)
		return EFAULT;
	uint32_t before = regs[offset / 4];
	record_write(offset, value);
	regs[offset / 4] = value;
	if (offset == 0x3a8)
		regs[offset / 4] =
		    ((value & ~0x10U) | (before & 0x10U)) ^ job_status_xor;
	if (pokes == 2)
		regs[0x3a8 / 4] |= fresh_reset_unknown;
	if (offset == 0x14 && value == 0)
		mask_drained = false;
	if (offset == 0x654) {
		command_writes++;
		address_drained = false;
		if (!lose_command_address)
			command_requested = value;
		regs[offset / 4] = command_readback;
	}
	if (offset == 0x3a4) {
		CHECK(value == 0x00010200 && command_writes == 1);
		CHECK(command_requested == (lose_command_address ? 0 :
		    0x01001000));
		CHECK(state.status.started && state.irq_lock.held &&
		    address_drained);
		/*
		 * A zero echo and a synthetic event do not prove that FE
		 * fetched a command.
		 */
		regs[0x664 / 4] = lose_command_address ? 0 : terminal_address;
		regs[1] = terminal_idle;
		regs[4] = irq_value;
		task_device_work();
		if (!no_irq)
			pending_irq = true;
	}
	return 0;
}
static void *
fdtbus_intr_establish_xname(int phandle, unsigned slot, int ipl,
    int flags, int (*fn) (void *), void *arg, const char *name)
{
	CHECK(!in_irq && phandle == 7 && slot == 0 && ipl == IPL_VM);
	CHECK(flags == FDT_INTR_MPSAFE && name != NULL && context.retained);
	established++;
	if (fail_irq)
		return NULL;
	callback = fn;
	callback_arg = arg;
	unsigned reads = peeks + ordinary_reads;
	in_irq = true;
	CHECK(fn(arg) == 0);
	in_irq = false;
	CHECK(peeks + ordinary_reads == reads);
	return &irq_token;
}
static void
fdtbus_intr_disestablish(int phandle, void *ih)
{
	CHECK(!in_irq && !context.retained && phandle == 7 && ih ==
	    &irq_token);
	removed++;
}
static void
delay(unsigned us)
{
	CHECK(!in_irq && us == 1000);
	delays++;
	CHECK(delays <= 100);
}

#include "sun60i_a733_npu_fe.c"
#include "sun60i_a733_npu_run.c"
#include "sun60i_a733_npu_task.c"

static void
task_sync(bus_dmamap_t map, bus_size_t n, int flags)
{
	CHECK(current_task != NULL && active == current_task->dma && rstate == &current_task->run);
	CHECK(active->retained && state.task_retained);
	CHECK(active->task_run == rstate && active->task_reference ==
	    &current_task->reference);
	CHECK(map->id == syncs % 6 && n == slots[map->id].bytes);
	CHECK(flags == (syncs < 6 ?
	    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD :
	    BUS_DMASYNC_POSTWRITE | BUS_DMASYNC_POSTREAD));
	if (syncs >= 6)
		CHECK(sun60i_a733_npu_run_task_completed(rstate, active));
	CHECK(syncs < 12);
	syncs++;
}

static void
task_unlock_probe(const kmutex_t *m)
{
	if (current_task == NULL || m != &state.irq_lock || !state.status.final_valid ||
	    state.status.error != 0)
		return;
	/* Success must already be visible before the final critical section ends. */
	CHECK(state.status.model_pass == 1);
}

static int
task_peek(bus_size_t offset, uint32_t * value)
{
	static const bus_size_t features[] = {
		0x20, 0x20,
		0x20, 0x24, 0xa8, 0xe8, 0x30, 0x20,
		0x20, 0x24, 0xa8, 0xe8, 0x30, 0x20,
		0x34, 0x34, 0xa0, 0xa0
	};
	static const bus_size_t final[] = {0x14, 4, 0x664, 4, 0x664, 0x14,
	    0x10};

	if (state.status.snapshot_valid && !state.status.publication_attempted &&
	    (offset == 0x20 || offset == 0x24 || offset == 0x30 ||
	    offset == 0x34 || offset == 0xa0 || offset == 0xa8 || offset == 0xe8)) {
		CHECK(!syncs && !writes && !established);
		CHECK(admission_reads < 18 && offset == features[admission_reads]);
		admission_reads++;
		if (offset == 0xa8 || offset == 0xe8)
			CHECK(state.status.task_features[0][0] == 0x9000 &&
			    state.status.task_features[1][0] == 0x9000);
		if (offset == 0xa0)
			CHECK((regs[0x34 / 4] & 0x00200000) != 0);
		if (admission_reads == admission_fail)
			return EFAULT;
		if (admission_reads == admission_flip)
			*value ^= 1;
		if (admission_reads >= 3 && admission_reads <= 14 &&
		    (admission_reads - 3) % 6 == identity_override_field)
			*value ^= identity_override_bits;
	}
	if (current_task != NULL && current_task->status.compare.valid &&
	    current_task->status.compare.pass) {
		CHECK(state.irq_lock.held && !state.armed && syncs == 12);
		CHECK(final_reads < 7 && offset == final[final_reads]);
		final_offsets[final_reads++] = offset;
		if (final_reads == final_fail)
			return EFAULT;
		if (final_reads == final_flip)
			*value ^= 1;
	}
	return 0;
}

static int
task_ready_probe(void)
{
	if (current_task != NULL && state.status.task_profile_valid &&
	    !state.status.publication_attempted) {
		switch (bad_capability) {
		case 1:
			state.status.task_capability_source = 0;
			break;
		case 2:
			state.status.task_profile_valid = 0;
			break;
		case 3:
			state.status.task_int8_cores = 7;
			break;
		case 4:
			state.status.task_blt = 1;
			break;
		}
	}
	if (current_task == NULL || !current_task->status.compare.valid ||
	    !current_task->status.compare.pass)
		return 0;
	CHECK(!state.irq_lock.held && !state.armed && syncs == 12);
	post_ready_calls++;
	if (late_after_compare) {
		unsigned old = ordinary_reads;
		regs[4] = 0x40000000;
		in_irq = true;
		CHECK(callback(callback_arg) == 0);
		in_irq = false;
		CHECK(ordinary_reads == old && regs[4] == 0x40000000);
	}
	return fail_post_ready ? EIO : 0;
}

static void
task_device_work(void)
{
	CHECK(syncs == 6 && active->retained && state.task_retained);
	CHECK(memcmp(slots[2].cpu, current_task->reference.commands, 144) == 0);
	/* Only this simulated device double can write oracle bytes to output. */
	if (output_enabled)
		memcpy(slots[5].cpu, current_task->reference.image.expected, 512);
	if (corrupt_buffer < 6) {
		CHECK(corrupt_byte < slots[corrupt_buffer].bytes);
		((uint8_t *)slots[corrupt_buffer].cpu)[corrupt_byte] ^= 1;
	}
}

static void
reset_bus(void)
{
	memset(rstate, 0, sizeof(*rstate));
	memset(regs, 0, sizeof(regs));
	maps = unmaps = established = removed = proofs = peeks = pokes =
	    writes = ordinary_reads = barriers = delays = setters = retains =
	    0;
	fail_proof = fail_peek = fail_poke = 0;
	fail_map = fail_irq = fail_set = fail_retain = bad_mask = no_irq =
	    in_irq = late_error = false;
	pending_irq = start_drained = prestart_irq = clean_retain = false;
	unstable_peek = mismatch_step = bad_info = infos = fail_info = 0;
	mask_drained = address_drained = terminal_unstable = false;
	dma_reads = 0;
	job_status_xor = fresh_reset_unknown = 0;
	regs[0x3a8 / 4] = 0x10;
	command_readback = command_requested = command_writes = 0;
	lose_command_address = false;
	terminal_idle = 0x7fffffff;
	terminal_address = 0x01001028;
	callback = NULL;
	callback_arg = NULL;
	property_bytes = 12;
	irq_value = 0x10;
	bad_route = 0;
	regs[1] = 0x7fffffff;
	regs[0x24 / 4] = 0x9202;
	regs[0x30 / 4] = 0x1000003b;
	regs[0x10c / 4] = 0x1000000;
	regs[0x90 / 4] = 0x80000000;
	regs[0x414 / 4] = 3;
}


static void
reset_all(void)
{
	CHECK(depth == 0 && parent_tag.refs == 0);
	current_task = NULL;
	active = NULL;
	rstate = &dummy;
	reset_bus();
	memset(slots, 0, sizeof(slots));
	calls = fail_call = allocated = syncs = 0;
	bad_at = bad_kind = 0;
	borrowed = false;
	task_mode = true;
	for (unsigned i = 0; i < 6; i++)
		addresses[i] = 0x100000 + i * 0x10000;
	regs[0x20 / 4] = 0x9000;
	regs[0x34 / 4] = 0x00200000;
	regs[0xa0 / 4] = 0;
	regs[0xa8 / 4] = 0x05090009;
	regs[0xe8 / 4] = 0x08000000;
	final_reads = admission_reads = post_ready_calls = 0;
	admission_fail = admission_flip = final_fail = final_flip = 0;
	memset(final_offsets, 0, sizeof(final_offsets));
	output_enabled = true;
	late_after_compare = fail_post_ready = false;
	corrupt_buffer = 6;
	corrupt_byte = bad_capability = 0;
	identity_override_field = 6;
	identity_override_bits = 0;
}

static void
prepare(void)
{
	CHECK(sun60i_a733_npu_task_prepare(&parent_tag, &current_task) == 0);
	CHECK(current_task != NULL && current_task->status.prepared && calls == 25 && depth ==
	    27);
	active = current_task->dma;
	rstate = &current_task->run;
	CHECK(!syncs && !peeks && !writes && !maps && !established);
}

static int
execute(void)
{
	return sun60i_a733_npu_task_run(current_task, &bus_token, &clock1, 9, &cookie,
	    7, "task");
}

static void
cleanup(void)
{
	if (state.status.publication_attempted) {
		CHECK(sun60i_a733_npu_task_destroy(current_task) == EBUSY);
		/*
		 * End only this simulated boot. Native code has no such
		 * operation.
		 */
		state.status.publication_attempted = 0;
		state.task_retained = false;
		current_task->status.retained = 0;
		active->retained = false;
		state.mapped = false;
		state.ih = NULL;
	}
	CHECK(sun60i_a733_npu_task_destroy(current_task) == 0);
	current_task = NULL;
	active = NULL;
	rstate = &dummy;
	CHECK(!depth && !parent_tag.refs);
}

static void
cold_refusals(void)
{
	/* Successful read bits before each of the 18 injected read faults. */
	static const uint32_t masks[18][4] = {
		{0, 0, 0, 0}, {1, 0, 0, 0},
		{1, 1, 0, 0}, {1, 1, 1, 0}, {1, 1, 3, 0},
		{1, 1, 7, 0}, {1, 1, 15, 0}, {1, 1, 31, 0},
		{1, 1, 63, 0}, {1, 1, 63, 1}, {1, 1, 63, 3},
		{1, 1, 63, 7}, {1, 1, 63, 15}, {1, 1, 63, 31},
		{1, 1, 63, 63}, {3, 1, 63, 63},
		{3, 3, 63, 63}, {7, 3, 63, 63}
	};

	for (unsigned n = 0; n < 6; n++) {
		unsigned io;

		reset_all();
		prepare();
		if (n == 0)
			regs[0x20 / 4] ^= 1;
		if (n == 1)
			regs[0xa8 / 4] ^= 1;
		if (n == 2)
			regs[0xa0 / 4] = 0x80000000;
		if (n == 3)
			regs[0x24 / 4] ^= 1;
		if (n == 4)
			regs[0x30 / 4] ^= 1;
		if (n == 5)
			regs[0xe8 / 4] ^= 1;
		CHECK(execute() != 0);
		CHECK(!state.status.publication_attempted && !syncs &&
		    !established && !writes && !state.status.task_profile_valid);
		CHECK(!current_task->status.run.model_pass &&
		    !state.status.model_pass);
		if (n == 0)
			CHECK(admission_reads == 2);
		if (n == 1 || n == 5) {
			CHECK(admission_reads == 14);
			CHECK(state.status.task_identity_valid);
		}
		if (n == 2)
			CHECK(admission_reads == 18);
		if (n == 3 || n == 4)
			CHECK(admission_reads == 0);
		CHECK(unmaps == 1);
		io = peeks + ordinary_reads + proofs + maps + writes;
		CHECK(execute() == EALREADY);
		CHECK(io == peeks + ordinary_reads + proofs + maps + writes);
		cleanup();
	}
	/* Each fresh identity field must match every bit, not a masked revision. */
	for (unsigned field = 0; field < 6; field++) {
		for (unsigned bit = 0; bit < 32; bit++) {
			reset_all();
			prepare();
			identity_override_field = field;
			identity_override_bits = 1U << bit;
			CHECK(execute() != 0 && !syncs && !writes && !established);
			CHECK(!state.status.publication_attempted &&
			    !state.status.task_profile_valid);
			if (field == 0)
				CHECK(admission_reads == 3);
			else if (field == 5)
				CHECK(admission_reads == 8);
			else {
				CHECK(admission_reads == 14);
				CHECK(state.status.task_identity_valid);
			}
			cleanup();
		}
	}
	for (unsigned n = 1; n <= 18; n++) {
		reset_all();
		prepare();
		admission_fail = n;
		CHECK(execute() == EFAULT && admission_reads == n && !syncs &&
		    !writes && !state.status.publication_attempted);
		CHECK(!state.status.task_profile_valid && !established);
		CHECK(state.status.task_feature_read_mask[0] == masks[n - 1][0]);
		CHECK(state.status.task_feature_read_mask[1] == masks[n - 1][1]);
		CHECK(state.status.task_identity_read_mask[0] == masks[n - 1][2]);
		CHECK(state.status.task_identity_read_mask[1] == masks[n - 1][3]);
		CHECK(state.status.task_identity_valid == (unsigned)(n >= 15));
		CHECK(!state.status.task_features_valid);
		cleanup();
		reset_all();
		prepare();
		admission_flip = n;
		CHECK(execute() != 0 && !syncs && !writes && !established);
		CHECK(!state.status.task_profile_valid);
		cleanup();
	}
	for (unsigned n = 1; n <= 6; n++) {
		reset_all();
		prepare();
		fail_proof = n;
		CHECK(execute() == EIO && !writes && !syncs && !established);
		cleanup();
	}
	for (unsigned n = 1; n <= 4; n++) {
		reset_all();
		prepare();
		bad_capability = n;
		CHECK(execute() == EOPNOTSUPP && !writes && !syncs && !established);
		CHECK(!state.status.publication_attempted);
		cleanup();
	}
}

static void
hwdb_without_more(void)
{
	reset_all();
	prepare();
	regs[0x34 / 4] = 0;
	/* This poison must not be read without MORE. */
	regs[0xa0 / 4] = 0x80000000;
	CHECK(execute() == 0 && state.status.model_pass);
	CHECK(admission_reads == 16 && !state.status.task_features_valid);
	CHECK(state.status.task_identity_valid && state.status.task_profile_valid);
	CHECK(state.status.task_capability_source == A733_NPU_TASK_CAP_HWDB);
	CHECK(state.status.task_int8_cores == 8 && state.status.task_blt == 0);
	for (size_t row = 0; row < 2; row++) {
		CHECK(state.status.task_identity_read_mask[row] == 0x3f);
		CHECK(state.status.task_feature_read_mask[row] == 3);
		CHECK(state.status.task_features[row][1] == 0);
		CHECK(state.status.task_features[row][2] == 0);
		CHECK(state.status.task_identity[row][0] == 0x9000);
		CHECK(state.status.task_identity[row][1] == 0x9202);
		CHECK(state.status.task_identity[row][2] == 0x05090009);
		CHECK(state.status.task_identity[row][3] == 0x08000000);
		CHECK(state.status.task_identity[row][4] == 0x1000003b);
		CHECK(state.status.task_identity[row][5] == 0x9000);
	}
	cleanup();
}

int
main(void)
{
	struct sun60i_a733_npu_task_status status;
	struct sun60i_a733_npu_task *out = (void *)(uintptr_t) 1;
	const size_t sizes[] = {4096, 16384, 4096, 4096, 4096, 4096};

#ifdef NPU_TASK_IDENTITY_ONLY
	hwdb_without_more();
	cold_refusals();
	printf("A733 NPU TASK identity: %u checks passed (simulated device only)\n",
	    checks);
	return 0;
#endif
#ifdef NPU_TASK_LATE_ONLY
	reset_all();
	prepare();
#ifdef NPU_TASK_STALE_READY
	fail_post_ready = true;
#else
	late_after_compare = true;
#endif
	int late_result = execute();
	fprintf(stderr, "post-comparison-boundary: result=%d model_pass=%u compare=%u armed=%u ACK=%#x reads=%u\n",
	    late_result, state.status.model_pass, current_task->status.compare.pass,
	    state.armed, regs[4], final_reads);
	CHECK(late_result == EIO && current_task->status.compare.pass &&
	    state.status.model_pass == 0);
	cleanup();
	return 0;
#endif
	reset_all();
	CHECK(sun60i_a733_npu_task_prepare(NULL, &out) == EINVAL);
	CHECK(sun60i_a733_npu_task_prepare(&parent_tag, NULL) == EINVAL);
	CHECK(sun60i_a733_npu_task_prepare(&parent_tag,
	    (void *)(UINTPTR_MAX - 3)) == EINVAL);
	CHECK(out == (void *)(uintptr_t) 1 && !calls && !depth);
	for (unsigned n = 1; n <= 25; n++) {
		reset_all();
		fail_call = n;
		CHECK(sun60i_a733_npu_task_prepare(&parent_tag, &out) == EIO);
		CHECK(out == (void *)(uintptr_t) 1 && !depth && !syncs &&
		    !writes);
	}
	reset_all();
	prepare();
	uint8_t command_copy[144];
	memcpy(command_copy, current_task->reference.commands, sizeof(command_copy));
	sun60i_a733_npu_task_status(current_task, (void *)current_task);
	sun60i_a733_npu_task_status(current_task, (void *)(UINTPTR_MAX - 7));
	CHECK(memcmp(command_copy, current_task->reference.commands,
	    sizeof(command_copy)) == 0);
	active->retained = true;
	CHECK(sun60i_a733_npu_task_destroy(current_task) == EBUSY &&
	    current_task->status.release_error == EBUSY);
	CHECK(depth == 27 && current_task->status.prepared && !syncs && !writes);
	active->retained = false;
	cleanup();
	reset_all();
	prepare();
	CHECK(sun60i_a733_npu_run_task_once(rstate, &bus_token, &clock1, 9,
	    &cookie, 7, "task", active, &current_task->reference) == 0);
	unsigned before_compare = peeks + ordinary_reads + proofs + writes;
	CHECK(sun60i_a733_npu_run_task_finish(rstate, active, &clock1, 9,
	    &cookie) == EBUSY);
	CHECK(before_compare == peeks + ordinary_reads + proofs + writes);
	CHECK(syncs == 6 && !active->task_report.valid && !state.status.model_pass);
	cleanup();
	reset_all();
	prepare();
	CHECK(execute() == 0);
	CHECK(current_task->status.retained && state.task_retained &&
	    active->retained);
	CHECK(state.status.terminal && state.status.model_pass &&
	    state.status.final_valid);
	CHECK(state.status.final_attempted && final_reads == 7 &&
	    post_ready_calls == 1);
	CHECK(current_task->status.compare.valid && current_task->status.compare.pass &&
	    syncs == 12);
	CHECK(admission_reads == 18 && state.status.task_features_valid &&
	    state.status.command_bytes == 144);
	CHECK(state.status.task_identity_valid && state.status.task_profile_valid);
	CHECK(state.status.task_capability_source == A733_NPU_TASK_CAP_HWDB);
	CHECK(state.status.task_int8_cores == 8 && state.status.task_blt == 0);
	for (size_t row = 0; row < 2; row++) {
		CHECK(state.status.task_identity_read_mask[row] == 0x3f);
		CHECK(state.status.task_feature_read_mask[row] == 7);
	}
	CHECK(!unmaps && !removed && command_writes == 1 && address_drained);
	unsigned io = peeks + ordinary_reads + proofs + maps + writes;
	sun60i_a733_npu_task_status(current_task, &status);
	CHECK(status.run.model_pass && status.prepared && !status.result &&
	    status.compare.pass);
	CHECK(io == peeks + ordinary_reads + proofs + maps + writes);
	CHECK(execute() == EALREADY);
	CHECK(sun60i_a733_npu_run_task_finish(rstate, active, &clock1, 9,
	    &cookie) == EALREADY);
	CHECK(io == peeks + ordinary_reads + proofs + maps + writes);
	cleanup();
	cold_refusals();
	hwdb_without_more();
	reset_all();
	prepare();
	output_enabled = false;
	CHECK(execute() == EIO && state.status.terminal &&
	    !state.status.model_pass);
	CHECK(current_task->status.compare.valid && !current_task->status.compare.pass &&
	    !final_reads && syncs == 12);
	cleanup();
	for (unsigned b = 0; b < 6; b++) {
		for (unsigned edge = 0; edge < 2; edge++) {
			reset_all();
			prepare();
			corrupt_buffer = b;
			corrupt_byte = edge ? sizes[b] - 1 : 0;
			CHECK(execute() == EIO && !state.status.model_pass &&
			    !final_reads);
			CHECK(current_task->status.compare.valid &&
			    current_task->status.compare.mismatches[b] == 1);
			cleanup();
		}
	}
	for (unsigned byte = 0; byte < 512; byte++) {
		reset_all();
		prepare();
		corrupt_buffer = 5;
		corrupt_byte = byte;
		CHECK(execute() == EIO && !state.status.model_pass);
		CHECK(current_task->status.compare.mismatches[5] == 1 &&
		    current_task->status.compare.first[5] == byte);
		cleanup();
	}
	for (unsigned n = 1; n <= 7; n++) {
		reset_all();
		prepare();
		final_fail = n;
		CHECK(execute() == EFAULT && !state.status.model_pass &&
		    current_task->status.compare.pass);
		CHECK(final_reads == n && !state.status.final_valid);
		cleanup();
		reset_all();
		prepare();
		final_flip = n;
		CHECK(execute() == EIO && !state.status.model_pass &&
		    current_task->status.compare.pass);
		CHECK(state.status.final_valid && final_reads == 7);
		cleanup();
	}
	reset_all();
	prepare();
	late_after_compare = true;
	CHECK(execute() == EIO && !state.status.model_pass &&
	    current_task->status.compare.pass);
	CHECK(state.status.final_status == 0x40000000 && final_reads == 7 &&
	    !state.armed);
	cleanup();
	reset_all();
	prepare();
	fail_post_ready = true;
	CHECK(execute() == EIO && !state.status.model_pass &&
	    current_task->status.compare.pass && !final_reads);
	cleanup();
	reset_all();
	prepare();
	lose_command_address = true;
	CHECK(execute() == ETIMEDOUT && !state.status.terminal &&
	    !current_task->status.compare.valid && syncs == 6);
	cleanup();
	reset_all();
	printf("A733 NPU TASK: %u checks passed (simulated device only)\n",
	    checks);
	return 0;
}
