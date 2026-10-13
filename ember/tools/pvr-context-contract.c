/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. AI-assisted source contract. */
/*
 * Execute the real context and stream sources against the real firmware ABI.
 * Only allocation, DMA synchronization, firmware commands and time are models.
 * This is a host contract. It does not execute firmware or prove GPU rendering.
 */
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arm/sunxi/powervr/pvr_rogue_fwif.h>
#include <arm/sunxi/powervr/pvr_rogue_fwif_client.h>
#include <arm/sunxi/powervr/pvr_rogue_fwif_check.h>
#include <arm/sunxi/powervr/pvr_rogue_fwif_shared_check.h>
#include <arm/sunxi/powervr/pvr_rogue_fwif_client_check.h>
#include "pvr_backend.h"
#include "pvr_context.h"
#include "pvr_stream.h"

typedef uint16_t __u16;
typedef uint32_t __u32;
typedef uint64_t __u64;
typedef int32_t __s32;
#include "pvr-drm-body.h"

#define BUS_DMASYNC_PREREAD 1
#define BUS_DMASYNC_POSTREAD 2
#define BUS_DMASYNC_PREWRITE 4
#define BUS_DMASYNC_POSTWRITE 8
#define KM_SLEEP 0
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define roundup2(x, n) (((x) + (n) - 1) & ~((n) - 1))
#ifndef __arraycount
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#endif

static unsigned checks, heap_objects;
#define CHECK(x) do { checks++; if (!(x)) { \
	fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); abort(); \
} } while (0)

static void *
kmem_zalloc(size_t size, int flags)
{
	void *p;
	CHECK(flags == KM_SLEEP);
	p = calloc(1, size);
	CHECK(p != NULL);
	heap_objects++;
	return p;
}

static void
kmem_free(void *p, size_t size)
{
	CHECK(p != NULL && size > 0 && heap_objects > 0);
	heap_objects--;
	free(p);
}

static int hz = 100;
static int kpause(const char *, bool, int, void *);
#include "pvr-context-body.h"

struct allocation {
	struct pvr_fw_object *slot;
	unsigned pre;
	bool exposed;
};
struct model {
	struct allocation objects[16];
	struct pvr_backend backend;
	struct rogue_fwif_cccb_ctl *pending_control;
	uint32_t *pending_ufo;
	uint32_t pending_read, pending_value, last_sequence;
	uint32_t expected_id;
	struct rogue_fwif_cmd_compute expected_compute;
	unsigned allocs, frees, live, kicks, cleanups, pads, sleeps, health;
	unsigned alloc_fail, free_fail, check_fail, busy;
	unsigned ufo_delay, read_delay;
	int command_error;
	uint32_t command_result;
	bool failed, pending, reset;
};
static struct model *active;

static struct allocation *
find_slot(struct model *m, const struct pvr_fw_object *slot)
{
	unsigned i;
	for (i = 0; i < __arraycount(m->objects); i++)
		if (m->objects[i].slot == slot)
			return &m->objects[i];
	CHECK(false);
	return NULL;
}

static struct pvr_fw_object *
find_address(struct model *m, uint32_t address, size_t size)
{
	unsigned i;
	for (i = 0; i < __arraycount(m->objects); i++) {
		struct pvr_fw_object *obj = m->objects[i].slot;
		if (obj != NULL && address >= obj->address &&
		    address - obj->address <= obj->size &&
		    size <= obj->size - (address - obj->address))
			return obj;
	}
	CHECK(false);
	return NULL;
}

static void *
address_cpu(struct model *m, uint32_t address, size_t size)
{
	struct pvr_fw_object *obj = find_address(m, address, size);
	return (uint8_t *)obj->cpu + address - obj->address;
}

static int
model_alloc(void *cookie, size_t size, struct pvr_fw_object *obj)
{
	struct model *m = cookie;
	unsigned i;
	CHECK(!m->failed && obj->owner == NULL && size > 0);
	if (++m->allocs == m->alloc_fail)
		return ENOMEM;
	for (i = 0; i < __arraycount(m->objects); i++)
		if (m->objects[i].slot == NULL)
			break;
	CHECK(i < __arraycount(m->objects));
	obj->cpu = calloc(1, size);
	CHECK(obj->cpu != NULL);
	obj->owner = &m->objects[i];
	obj->size = size;
	obj->address = 0xc0200000U + i * 0x10000U;
	m->objects[i].slot = obj;
	m->live++;
	return 0;
}

static int
model_free(void *cookie, struct pvr_fw_object *obj)
{
	struct model *m = cookie;
	struct allocation *a = find_slot(m, obj);
	CHECK(!m->failed && !a->exposed && !m->pending && m->live > 0);
	if (++m->frees == m->free_fail)
		return EBUSY;
	/* The coherent DMA owner closes all outstanding pre-syncs on release. */
	free(obj->cpu);
	memset(obj, 0, sizeof(*obj));
	memset(a, 0, sizeof(*a));
	m->live--;
	return 0;
}

static void
model_sync(void *cookie, struct pvr_fw_object *obj, int operations)
{
	struct model *m = cookie;
	struct allocation *a = find_slot(m, obj);
	CHECK(!m->failed && operations != 0 && (operations & ~15) == 0);
	if (operations & BUS_DMASYNC_PREWRITE)
		a->pre |= BUS_DMASYNC_PREWRITE;
	if (operations & BUS_DMASYNC_PREREAD)
		a->pre |= BUS_DMASYNC_PREREAD;
	if (operations & BUS_DMASYNC_POSTWRITE) {
		CHECK(a->pre & BUS_DMASYNC_PREWRITE);
		a->pre &= ~BUS_DMASYNC_PREWRITE;
	}
	if (operations & BUS_DMASYNC_POSTREAD) {
		CHECK(a->pre & BUS_DMASYNC_PREREAD);
		a->pre &= ~BUS_DMASYNC_PREREAD;
	}
}

static int
model_check(void *cookie)
{
	struct model *m = cookie;
	m->health++;
	if (m->failed || m->health == m->check_fail)
		return EIO;
	return 0;
}

static void
model_fail(void *cookie)
{
	struct model *m = cookie;
	m->failed = true;
}

static void
parse_kick(struct model *m, const struct rogue_fwif_kccb_cmd *cmd)
{
	const struct rogue_fwif_kccb_cmd_kick_data *kick = &cmd->cmd_data.cmd_kick_data;
	struct rogue_fwif_fwcommoncontext *common;
	struct pvr_fw_object *ring, *ctl, *timeline, *firmware;
	struct rogue_fwif_cccb_ctl *control;
	struct rogue_fwif_ccb_cmd_header header;
	struct rogue_fwif_ufo ufo;
	uint32_t cursor;
	unsigned commands = 0, i;

	CHECK(!m->pending && kick->client_wrap_mask_update == 32767);
	CHECK(kick->num_cleanup_ctl == 0 && kick->work_est_cmd_header_offset == 0);
	firmware = find_address(m, kick->context_fw_addr, sizeof(*common));
	common = address_cpu(m, kick->context_fw_addr, sizeof(*common));
	CHECK(common->dm == PVR_FWIF_DM_CDM);
	ring = find_address(m, common->ccb_fw_addr, 32768);
	ctl = find_address(m, common->ccbctl_fw_addr, sizeof(*control));
	control = ctl->cpu;
	CHECK(control->wrap_mask == 32767 && kick->client_woff_update < 32768);
	CHECK(find_slot(m, ring)->pre & BUS_DMASYNC_PREWRITE);
	CHECK(find_slot(m, ctl)->pre & BUS_DMASYNC_PREREAD);
	CHECK(find_slot(m, firmware)->pre & BUS_DMASYNC_PREWRITE);
	cursor = control->read_offset;
	while (cursor != kick->client_woff_update) {
		CHECK(cursor + sizeof(header) <= ring->size);
		memcpy(&header, (uint8_t *)ring->cpu + cursor, sizeof(header));
		CHECK(header.cmd_size <= ring->size - cursor - sizeof(header));
		CHECK((header.cmd_size & 7) == 0);
		if (header.cmd_type == ROGUE_FWIF_CCB_CMD_TYPE_PADDING) {
			CHECK(commands == 0 && cursor != 0);
			CHECK(cursor + sizeof(header) + header.cmd_size == ring->size);
			m->pads++;
			cursor = 0;
			continue;
		}
		CHECK(header.ext_job_ref == m->expected_id && header.int_job_ref == m->expected_id);
		if (commands++ == 0) {
			CHECK(header.cmd_type == ROGUE_FWIF_CCB_CMD_TYPE_CDM);
			CHECK(header.cmd_size == sizeof(m->expected_compute));
			CHECK(memcmp((uint8_t *)ring->cpu + cursor + sizeof(header),
			    &m->expected_compute, sizeof(m->expected_compute)) == 0);
		} else {
			CHECK(commands == 2 && header.cmd_type == ROGUE_FWIF_CCB_CMD_TYPE_UPDATE);
			CHECK(header.cmd_size == sizeof(ufo));
			memcpy(&ufo, (uint8_t *)ring->cpu + cursor + sizeof(header), sizeof(ufo));
			timeline = find_address(m, ufo.addr, sizeof(uint32_t));
			CHECK(find_slot(m, timeline)->pre & BUS_DMASYNC_PREREAD);
			CHECK(ufo.value == m->last_sequence + 1);
			m->last_sequence = m->pending_value = ufo.value;
			m->pending_ufo = address_cpu(m, ufo.addr, sizeof(uint32_t));
		}
		cursor += sizeof(header) + header.cmd_size;
	}
	CHECK(commands == 2);
	m->pending_control = control;
	m->pending_read = kick->client_woff_update;
	m->pending = true;
	m->sleeps = 0;
	/* All allocations of this one test context are reachable from firmware. */
	for (i = 0; i < __arraycount(m->objects); i++)
		if (m->objects[i].slot != NULL)
			m->objects[i].exposed = true;
}

static int
model_command(void *cookie, const struct rogue_fwif_kccb_cmd *cmd, uint32_t *result)
{
	struct model *m = cookie;
	unsigned i;
	CHECK(!m->failed && result != NULL);
	if (cmd->cmd_type == ROGUE_FWIF_KCCB_CMD_KICK) {
		m->kicks++;
		parse_kick(m, cmd);
	} else {
		CHECK(cmd->cmd_type == ROGUE_FWIF_KCCB_CMD_CLEANUP);
		CHECK(cmd->cmd_data.cleanup_data.cleanup_type == ROGUE_FWIF_CLEANUP_FWCOMMONCONTEXT);
		(void)find_address(m, cmd->cmd_data.cleanup_data.cleanup_data.context_fw_addr,
		    sizeof(struct rogue_fwif_fwcommoncontext));
		CHECK(!m->pending);
		m->cleanups++;
		if (m->busy != 0) {
			m->busy--;
			*result = ROGUE_FWIF_KCCB_RTN_SLOT_CMD_EXECUTED | 2;
			return EBUSY;
		}
		if (m->command_error == 0 && m->command_result == ROGUE_FWIF_KCCB_RTN_SLOT_CMD_EXECUTED)
			for (i = 0; i < __arraycount(m->objects); i++)
				m->objects[i].exposed = false;
	}
	*result = m->command_result;
	return m->command_error;
}

static int
kpause(const char *name, bool interruptible, int ticks, void *lock)
{
	struct model *m = active;
	CHECK(strcmp(name, "pvrjob") == 0 && !interruptible && ticks > 0 && lock == NULL);
	CHECK(m != NULL && m->pending);
	m->sleeps++;
	if (m->sleeps == m->ufo_delay)
		*m->pending_ufo = m->pending_value;
	if (m->sleeps == m->read_delay)
		m->pending_control->read_offset = m->pending_read;
	if (*m->pending_ufo == m->pending_value && m->pending_control->read_offset == m->pending_read)
		m->pending = false;
	return 0;
}

static void
init_model(struct model *m)
{
	CHECK(heap_objects == 0);
	memset(m, 0, sizeof(*m));
	m->backend.cookie = m;
	m->backend.alloc = model_alloc;
	m->backend.free = model_free;
	m->backend.sync = model_sync;
	m->backend.command = model_command;
	m->backend.check = model_check;
	m->backend.fail = model_fail;
	m->command_result = ROGUE_FWIF_KCCB_RTN_SLOT_CMD_EXECUTED;
	m->ufo_delay = 1;
	m->read_delay = 3;
	active = m;
}

static void
put32(uint8_t *p, uint32_t value)
{
	unsigned i;
	for (i = 0; i < 4; i++)
		p[i] = (uint8_t)(value >> (8 * i));
}

static void
config_init(struct pvr_context_config *config, uint32_t type, uint8_t *bytes)
{
	unsigned i;
	memset(config, 0, sizeof(*config));
	config->type = type;
	config->priority = 1;
	config->id = 101;
	config->pid = 301;
	config->vm_address = 0xc0101000;
	if (type != DRM_PVR_CTX_TYPE_TRANSFER_FRAG) {
		config->static_state = bytes;
		config->static_size = type == DRM_PVR_CTX_TYPE_COMPUTE ? 64 : 192;
		memset(bytes, 0, config->static_size);
		put32(bytes, (uint32_t)config->static_size);
		for (i = 8; i < config->static_size; i++)
			bytes[i] = (uint8_t)i;
	}
	if (type == DRM_PVR_CTX_TYPE_RENDER)
		config->callstack = UINT64_C(0x810004000);
}

static struct pvr_context *
create_context(struct model *m, uint32_t type)
{
	struct pvr_context_config config;
	struct pvr_context *ctx = NULL;
	uint8_t bytes[192];
	config_init(&config, type, bytes);
	CHECK(pvr_context_create(&m->backend, &config, &ctx) == 0);
	CHECK(ctx != NULL && pvr_context_type(ctx) == type);
	return ctx;
}

static void
normal_contexts(void)
{
	struct model m;
	struct pvr_context *ctx;
	struct rogue_fwif_fwcommoncontext *common;
	struct rogue_fwif_geom_ctx_state *state;
	uint32_t type;
	unsigned i;
	for (type = 0; type < 3; type++) {
		init_model(&m);
		ctx = create_context(&m, type);
		CHECK(m.live == (type == DRM_PVR_CTX_TYPE_RENDER ? 9U : 5U));
		for (i = 0; i < ctx->count; i++) {
			common = (void *)((uint8_t *)ctx->firmware.cpu + ctx->queue[i].offset);
			CHECK(common->priority == 1 && common->server_common_context_id == 101 && common->pid == 301);
			CHECK(common->fw_mem_context_fw_addr == 0xc0101000);
			CHECK(common->ccb_fw_addr == ctx->queue[i].ring.address);
			CHECK(common->ccbctl_fw_addr == ctx->queue[i].control.address);
			CHECK(common->context_state_addr == ctx->queue[i].state.address);
			CHECK(common->dm == (type == DRM_PVR_CTX_TYPE_COMPUTE ? PVR_FWIF_DM_CDM :
			    type == DRM_PVR_CTX_TYPE_RENDER && i == 0 ? PVR_FWIF_DM_GEOM : PVR_FWIF_DM_FRAG));
		}
		if (type == DRM_PVR_CTX_TYPE_RENDER) {
			state = ctx->queue[0].state.cpu;
			CHECK(state->geom_core[0].geom_reg_vdm_call_stack_pointer_init == UINT64_C(0x810004000));
		}
		CHECK(pvr_context_destroy(&ctx) == 0);
		CHECK(ctx == NULL && m.live == 0 && m.cleanups == 0 && heap_objects == 0);
	}
}

static void
allocation_failures(void)
{
	struct model m;
	struct pvr_context_config config;
	struct pvr_context *ctx;
	uint8_t bytes[192];
	unsigned failure, type;
	for (type = 0; type < 3; type++) {
		for (failure = 1; failure <= (type == 0 ? 9U : 5U); failure++) {
			init_model(&m);
			m.alloc_fail = failure;
			config_init(&config, type, bytes);
			ctx = NULL;
			CHECK(pvr_context_create(&m.backend, &config, &ctx) == ENOMEM);
			CHECK(ctx != NULL && m.live == failure - 1);
			CHECK(pvr_context_destroy(&ctx) == 0);
			CHECK(ctx == NULL && m.live == 0 && m.cleanups == 0);
		}
	}
	for (failure = 1; failure <= 5; failure++) {
		init_model(&m);
		ctx = create_context(&m, DRM_PVR_CTX_TYPE_COMPUTE);
		m.free_fail = failure;
		CHECK(pvr_context_destroy(&ctx) == EBUSY);
		CHECK(ctx != NULL && m.live == 6 - failure);
		CHECK(pvr_context_destroy(&ctx) == 0);
		CHECK(ctx == NULL && m.live == 0);
	}
}

static void
run_job(struct model *m, struct pvr_context *ctx, uint32_t id)
{
	uint8_t bytes[44] = { 0 };
	put32(bytes, sizeof(bytes));
	put32(bytes + 8, 0x4000);
	put32(bytes + 16, 0x10000);
	put32(bytes + 24, 0x20000);
	CHECK(pvr_stream_compute(PVR_STREAM_BVNC, bytes, sizeof(bytes), 0, &m->expected_compute) == 0);
	m->expected_id = id;
	CHECK(pvr_context_compute(ctx, &m->expected_compute, id) == 0);
	CHECK(m->sleeps == MAX(m->ufo_delay, m->read_delay));
	CHECK(!m->pending && !m->failed);
}

static void
jobs_and_cleanup(void)
{
	struct model m;
	struct pvr_context *ctx;
	unsigned i;
	init_model(&m);
	ctx = create_context(&m, DRM_PVR_CTX_TYPE_COMPUTE);
	for (i = 1; i <= 600; i++) {
		if (i == 300) {
			m.ufo_delay = 4;
			m.read_delay = 1;
		}
		run_job(&m, ctx, i);
	}
	CHECK(m.pads >= 3 && m.kicks == 600);
	m.busy = 2;
	CHECK(pvr_context_destroy(&ctx) == EBUSY);
	CHECK(ctx != NULL && m.live == 5 && m.frees == 0 && !m.failed);
	CHECK(pvr_context_destroy(&ctx) == EBUSY);
	CHECK(ctx != NULL && m.live == 5 && m.frees == 0 && !m.failed);
	CHECK(pvr_context_destroy(&ctx) == 0);
	CHECK(ctx == NULL && m.live == 0 && m.cleanups == 3);
}

/* The model disconnects its simulated GPU before reclaiming retained memory.
 * No production reset or recovery path is implied by this test-only teardown.
 */
static void
reset_model(struct model *m, struct pvr_context **owner)
{
	unsigned i;
	m->reset = true;
	m->pending = false;
	for (i = 0; i < __arraycount(m->objects); i++) {
		if (m->objects[i].slot != NULL) {
			free(m->objects[i].slot->cpu);
			memset(m->objects[i].slot, 0, sizeof(*m->objects[i].slot));
		}
	}
	memset(m->objects, 0, sizeof(m->objects));
	m->live = 0;
	kmem_free(*owner, sizeof(**owner));
	*owner = NULL;
}

static void
retained_failures(void)
{
	struct model m;
	struct pvr_context *ctx;
	struct rogue_fwif_cmd_compute command = { 0 };
	unsigned scenario, saved;
	int expected;
	for (scenario = 0; scenario < 7; scenario++) {
		init_model(&m);
		ctx = create_context(&m, DRM_PVR_CTX_TYPE_COMPUTE);
		m.expected_id = 1;
		expected = EIO;
		switch (scenario) {
		case 0: m.ufo_delay = 0; expected = ETIMEDOUT; break;
		case 1: m.read_delay = 0; expected = ETIMEDOUT; break;
		case 2: m.command_error = ETIMEDOUT; expected = ETIMEDOUT; break;
		case 3: m.command_result = 0; break;
		case 4: m.check_fail = m.health + 2; break;
		case 5: m.check_fail = m.health + 3; break;
		case 6: ((struct rogue_fwif_cccb_ctl *)ctx->queue[0].control.cpu)->read_offset = 8; break;
		}
		CHECK(pvr_context_compute(ctx, &command, 1) == expected);
		CHECK(m.failed && ctx->failed && m.live == 5 && m.frees == 0);
		if (scenario < 2)
			CHECK(m.sleeps == 1000);
		saved = m.kicks;
		CHECK(pvr_context_compute(ctx, &command, 2) == EIO);
		CHECK(pvr_context_destroy(&ctx) == EIO);
		CHECK(ctx != NULL && m.live == 5 && m.frees == 0 && m.kicks == saved);
		reset_model(&m, &ctx);
	}
	init_model(&m);
	ctx = create_context(&m, DRM_PVR_CTX_TYPE_COMPUTE);
	run_job(&m, ctx, 1);
	m.command_result = 0;
	CHECK(pvr_context_destroy(&ctx) == EIO);
	CHECK(ctx != NULL && m.failed && m.live == 5 && m.frees == 0);
	CHECK(pvr_context_destroy(&ctx) == EIO);
	reset_model(&m, &ctx);
}

static void
invalid_inputs(void)
{
	struct model m;
	struct pvr_context_config config;
	struct pvr_context *ctx = NULL;
	struct rogue_fwif_cmd_compute command = { 0 };
	uint8_t bytes[192];
	init_model(&m);
	config_init(&config, DRM_PVR_CTX_TYPE_COMPUTE, bytes);
	CHECK(pvr_context_create(NULL, &config, &ctx) == EINVAL);
	CHECK(pvr_context_create(&m.backend, NULL, &ctx) == EINVAL);
	CHECK(pvr_context_create(&m.backend, &config, NULL) == EINVAL);
	config.priority = 3;
	CHECK(pvr_context_create(&m.backend, &config, &ctx) == EINVAL);
	config.priority = 1;
	config.id = 0;
	CHECK(pvr_context_create(&m.backend, &config, &ctx) == EINVAL);
	config.id = 101;
	config.vm_address++;
	CHECK(pvr_context_create(&m.backend, &config, &ctx) == EINVAL);
	config.vm_address--;
	config.callstack = 1;
	CHECK(pvr_context_create(&m.backend, &config, &ctx) == EINVAL);
	config.callstack = 0;
	config.static_size--;
	CHECK(pvr_context_create(&m.backend, &config, &ctx) == EINVAL);
	CHECK(ctx == NULL && m.allocs == 0);
	ctx = create_context(&m, DRM_PVR_CTX_TYPE_COMPUTE);
	CHECK(pvr_context_create(&m.backend, &config, &ctx) == EINVAL);
	CHECK(pvr_context_compute(ctx, NULL, 1) == EINVAL);
	CHECK(pvr_context_compute(ctx, &command, 0) == EINVAL);
	CHECK(pvr_context_compute(NULL, &command, 1) == EINVAL);
	ctx->queue[0].sequence = UINT32_MAX;
	CHECK(pvr_context_compute(ctx, &command, 1) == EOVERFLOW);
	CHECK(m.kicks == 0);
	CHECK(pvr_context_destroy(&ctx) == 0);
	CHECK(pvr_context_destroy(&ctx) == EINVAL);
	CHECK(pvr_context_destroy(NULL) == EINVAL);
	ctx = create_context(&m, DRM_PVR_CTX_TYPE_RENDER);
	CHECK(pvr_context_compute(ctx, &command, 1) == EOPNOTSUPP);
	CHECK(pvr_context_destroy(&ctx) == 0);
}

int
main(void)
{
	normal_contexts();
	allocation_failures();
	jobs_and_cleanup();
	retained_failures();
	invalid_inputs();
	CHECK(heap_objects == 0);
	printf("PowerVR native context contract: PASS (%u checks)\n", checks);
	return 0;
}
