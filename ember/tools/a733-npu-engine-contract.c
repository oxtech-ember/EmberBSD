/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD, AI-assisted; actual consumer admission and cleanup controls. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
typedef unsigned int u_int;
typedef void *bus_space_tag_t;
typedef void *bus_dma_tag_t;
typedef uintptr_t bus_space_handle_t;
typedef int kmutex_t;
struct clk { int unused; };
#include "sun60i_a733_npu_fe.h"
#include "sun60i_a733_npu_nn.h"
#include "sun60i_a733_npu_dma.h"
#include "sun60i_a733_npu_run.h"
#include "sun60i_a733_npu_task.h"
struct sun60i_a733_npu_dma { int id; };
struct sun60i_npu_softc {
	struct sun60i_a733_npu_run sc_engine_run;
	struct sun60i_a733_npu_run_status sc_engine_run_cache;
	void *sc_dev;
	int sc_phandle;
	int sc_run_lock;
	bool sc_mmio_ready, sc_run_pass;
	u_int sc_run_stage, sc_run_result;
	u_int sc_native_profile, sc_task_attempted, sc_task_stage, sc_task_result;
	uint32_t sc_task_command_bytes, sc_task_constants_bytes;
	uint32_t sc_task_input_bytes, sc_task_output_bytes;
	struct sun60i_a733_npu_task *sc_task;
	struct sun60i_a733_npu_task_status sc_task_cache;
	uint64_t sc_pid, sc_rev;
	struct clk *sc_owned_clock;
	int sc_owned_power;
	bus_space_tag_t sc_bst;
	bus_dma_tag_t sc_dmat;
	u_int sc_engine_attempted, sc_engine_prepared, sc_engine_stage;
	u_int sc_engine_snapshot_valid, sc_engine_release_error;
	u_int sc_engine_command_bytes;
	struct sun60i_a733_npu_fe_snapshot sc_engine_snapshot;
	struct sun60i_a733_npu_dma_info sc_engine_dma_info;
	struct sun60i_a733_npu_dma *sc_engine_dma;
};
#define A733_NPU_STAGE_POWER_ID 4
#define A733_NPU_EXPECTED_PID UINT64_C(0x1000003b)
#define A733_NPU_EXPECTED_REV UINT64_C(0x9202)
static unsigned checks;
#define CHECK(c) do { checks++; if (!(c)) { \
	fprintf(stderr, "check %u failed at line %d: %s\n", checks, __LINE__, #c); exit(1); \
} } while (0)
#define KASSERT(c) CHECK(c)
#define mutex_owned(m) (*(m) == 1)
static struct clk clock1;
static struct sun60i_npu_softc state;
static struct sun60i_a733_npu_dma context;
static struct sun60i_a733_npu_fe_snapshot raw;
static unsigned proofs, snapshots, allocations, infos, releases;
static int proof_error, snapshot_error, allocation_error, info_error, release_error;
static bool identity;
static int sun60i_a733_ccu_npu_ready(struct clk *clock, const void *owner)
{
	CHECK(clock == &clock1 && owner == &state); proofs++;
	return proof_error;
}
static int sun60i_a733_pck_npu_ready(int power, const void *owner)
{ CHECK(power == 7 && owner == &state); proofs++; return 0; }
int sun60i_a733_npu_fe_snapshot(bus_space_tag_t tag, struct clk *clock,
    int power, const void *owner, struct sun60i_a733_npu_fe_snapshot *out)
{
	CHECK(tag == (void *)(uintptr_t)1 && clock == &clock1 && power == 7 && owner == &state);
	snapshots++;
	if (snapshot_error)
		return snapshot_error;
	*out = raw;
	return 0;
}
#include "npu-fe-pure.h"
int sun60i_a733_npu_dma_prepare(bus_dma_tag_t tag, const void *image, size_t bytes,
    struct sun60i_a733_npu_dma **out)
{
	static const uint8_t expected[16] = { 1,14,1,8,68,0,0,0,0,0,0,16,0,0,0,0 };
	CHECK(tag == (void *)(uintptr_t)2 && bytes == 16);
	CHECK(memcmp(image, expected, 16) == 0); allocations++;
	if (allocation_error)
		return allocation_error;
	*out = &context;
	return 0;
}
int sun60i_a733_npu_dma_info(const struct sun60i_a733_npu_dma *m,
    struct sun60i_a733_npu_dma_info *out)
{
	CHECK(m == &context); infos++;
	if (info_error)
		return info_error;
	*out = (struct sun60i_a733_npu_dma_info){ .mtlb_address = 0x40000000,
	    .stlb_address = 0x40010000, .command_address = identity ? 0x01001000 : 0x40020000,
	    .profile = A733_NPU_DMA_PROFILE_FE, .pd_entry = 0x400001, .command_virtual = 0x01001000, .command_bytes = 16 };
	return 0;
}
int sun60i_a733_npu_dma_destroy(struct sun60i_a733_npu_dma *m)
{ CHECK(m == &context); releases++; return release_error; }
#include "sun60i_npu_mmio_ready.h"
#include "sun60i_npu_engine_prepare.h"
static unsigned run_calls, run_copies;
static int run_error;
static bool run_attempt;
static const char *device_xname(void *dev)
{ CHECK(dev == &context); return "npu0"; }
int sun60i_a733_npu_run_once(struct sun60i_a733_npu_run *run, bus_space_tag_t tag,
    struct clk *clock, int power, const void *owner, int phandle,
    const char *name, struct sun60i_a733_npu_dma *m)
{
	CHECK(state.sc_run_lock == 1 && !state.sc_mmio_ready);
	CHECK(run == &state.sc_engine_run && tag == state.sc_bst);
	CHECK(clock == &clock1 && power == 7 && owner == &state);
	CHECK(phandle == 13 && !strcmp(name, "npu0") && m == &context);
	run_calls++;
	if (run_attempt) {
		run->status.attempted = 1;
		run->status.publication_attempted = 1;
		run->status.retained = 1;
		run->status.command_bytes = 40;
		run->status.error = run_error;
		run->status.terminal = run_error == 0;
	}
	return run_error;
}
void sun60i_a733_npu_run_status(struct sun60i_a733_npu_run *run,
    struct sun60i_a733_npu_run_status *out)
{
	CHECK(state.sc_run_lock == 1 && run == &state.sc_engine_run);
	run_copies++; *out = run->status;
}
#include "sun60i_npu_engine_run.h"

struct sun60i_a733_npu_task {
	struct sun60i_a733_npu_task_status status;
};
static struct sun60i_a733_npu_task task_context;
static unsigned task_allocations, task_runs, task_copies;
static int task_prepare_error, task_release_error, task_run_error;
static bool task_keep, task_publish, task_compare, task_model, task_valid;

int
sun60i_a733_npu_task_prepare(bus_dma_tag_t tag,
    struct sun60i_a733_npu_task **out)
{

	CHECK(tag == state.sc_dmat && state.sc_run_lock == 1);
	CHECK(state.sc_native_profile == A733_NPU_DMA_PROFILE_TASK);
	CHECK(state.sc_task_attempted == 1 && !state.sc_mmio_ready);
	task_allocations++;
	if (task_prepare_error != 0 && !task_keep)
		return task_prepare_error;
	task_context.status.prepared = task_prepare_error == 0;
	task_context.status.result = task_prepare_error;
	task_context.status.release_error = task_release_error;
	task_context.status.memory = (struct sun60i_a733_npu_dma_task_info){
		.memory = {
			.mtlb_address = 0x40000000, .stlb_address = 0x40001000,
			.command_address = 0x40005000, .pd_entry = 0x400001,
			.command_virtual = 0x01001000, .command_bytes = 144,
			.profile = A733_NPU_DMA_PROFILE_TASK
		},
		.constants_address = 0x40006000, .input_address = 0x40007000,
		.output_address = 0x40008000, .constants_virtual = 0x01002000,
		.input_virtual = 0x01003000, .output_virtual = 0x01004000,
		.constants_bytes = 864, .input_bytes = 256, .output_bytes = 4096
	};
	*out = &task_context;
	return task_prepare_error;
}

int
sun60i_a733_npu_task_run(struct sun60i_a733_npu_task *t,
    bus_space_tag_t tag, struct clk *clock, int power, const void *owner,
    int phandle, const char *name)
{

	CHECK(t == &task_context && state.sc_run_lock == 1);
	CHECK(tag == state.sc_bst && clock == &clock1 && power == 7);
	CHECK(owner == &state && phandle == 13 && !strcmp(name, "npu0"));
	CHECK(state.sc_task_stage == 2 && !state.sc_mmio_ready);
	task_runs++;
	t->status.attempted = 1;
	t->status.result = task_run_error;
	t->status.retained = task_publish;
	t->status.run.attempted = 1;
	t->status.run.publication_attempted = task_publish;
	t->status.run.retained = task_publish;
	t->status.run.model_pass = task_model;
	t->status.compare.valid = task_valid;
	t->status.compare.pass = task_compare;
	return task_run_error;
}

void
sun60i_a733_npu_task_status(struct sun60i_a733_npu_task *t,
    struct sun60i_a733_npu_task_status *out)
{

	CHECK(t == &task_context && state.sc_run_lock == 1);
	task_copies++;
	*out = t->status;
}
#include "sun60i_npu_task_cache.h"
#include "sun60i_npu_task_prepare.h"
#include "sun60i_npu_task_run.h"

#define CTLTYPE_INT 2
#define CTLTYPE_QUAD 4
#define SYSCTL_TYPE(x) ((x) & 15)
struct sysctlnode { unsigned sysctl_flags; void *sysctl_data; };
#define SYSCTLFN_ARGS const struct sysctlnode *rnode, const void *newp
#define SYSCTLFN_CALL(node) node, newp
static struct sun60i_npu_softc *sun60i_npu_instance = &state;
static unsigned cache_locks, cache_copies, cache_lookups;
static uint64_t cache_result;
static int lookup_error;
static void mutex_enter(int *lock) { CHECK(*lock == 0); *lock = 1; cache_locks++; }
static void mutex_exit(int *lock) { CHECK(*lock == 1); *lock = 0; }
static void *cache_copy(void *to, const void *from, size_t bytes)
{
	CHECK(state.sc_run_lock == 1); cache_copies++; return memcpy(to, from, bytes);
}
static int sysctl_lookup(const struct sysctlnode *node, const void *newp)
{
	CHECK(state.sc_run_lock == 0); cache_lookups++;
	if (lookup_error != 0)
		return lookup_error;
	if (newp != NULL) {
		memcpy(node->sysctl_data, newp, sizeof(uint32_t));
		return 0;
	}
	cache_result = 0;
	memcpy(&cache_result, node->sysctl_data,
	    SYSCTL_TYPE(node->sysctl_flags) == CTLTYPE_INT ? 4 : 8);
	return 0;
}
#pragma push_macro("memcpy")
#undef memcpy
#define memcpy cache_copy
#include "sun60i_npu_sysctl_engine_value.h"
#undef memcpy
#pragma pop_macro("memcpy")
#include "sun60i_npu_sysctl_task_stage.h"

#define CTLFLAG_READONLY 0x100
#define CTLFLAG_READWRITE 0x200
#define CTLFLAG_ANYWRITE 0x400
#define SYSCTL_DESCR(x) (x)
#define CTL_CREATE (-2)
#define CTL_EOL (-1)
#ifndef __arraycount
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#endif
struct cached_node {
	char name[64];
	int flags, type;
	void *data;
	int (*callback)(const struct sysctlnode *, const void *);
};
static struct cached_node nodes[160];
static unsigned node_count, setup_errors;

static int
sysctl_createv(void *log, int flags, const struct sysctlnode **root,
    const struct sysctlnode **out, int node_flags, int type,
    const char *name, const char *description,
    int (*callback)(const struct sysctlnode *, const void *),
    int value, void *data, size_t bytes, ...)
{

	CHECK(log == NULL && flags == 0 && root != NULL && out == NULL);
	CHECK(name != NULL && description != NULL && callback != NULL);
	CHECK(value == 0 && bytes == 0 && node_count < __arraycount(nodes));
	CHECK(strlen(name) < sizeof(nodes[0].name));
	for (size_t i = 0; i < node_count; i++)
		CHECK(strcmp(nodes[i].name, name) != 0);
	strcpy(nodes[node_count].name, name);
	nodes[node_count].flags = node_flags;
	nodes[node_count].type = type;
	nodes[node_count].data = data;
	nodes[node_count].callback = callback;
	node_count++;
	return 0;
}

static void
aprint_error_dev(void *dev, const char *message)
{

	CHECK(dev == &context && message != NULL);
	setup_errors++;
}
#include "sun60i_npu_sysctl_task_setup.h"

static void cache(void)
{
	uint32_t word = 0x89abcdef;
	uint64_t quad = UINT64_C(0x1234567889abcdef);
	struct sysctlnode node = { CTLTYPE_INT, &word };
	state.sc_run_lock = 0; cache_locks = cache_copies = cache_lookups = 0;
	unsigned before_proofs = proofs, before_snapshots = snapshots;
	CHECK(sun60i_npu_sysctl_engine_value(&node, NULL) == 0);
	CHECK(cache_result == word && cache_locks == 1 && cache_copies == 1);
	node = (struct sysctlnode){ CTLTYPE_QUAD, &quad };
	CHECK(sun60i_npu_sysctl_engine_value(&node, NULL) == 0);
	CHECK(cache_result == quad && cache_locks == 2 && cache_copies == 2);
	CHECK(sun60i_npu_sysctl_engine_value(&node, &quad) == EPERM);
	node.sysctl_flags = 15;
	CHECK(sun60i_npu_sysctl_engine_value(&node, NULL) == EINVAL);
	sun60i_npu_instance = NULL;
	CHECK(sun60i_npu_sysctl_engine_value(&node, NULL) == ENXIO);
	sun60i_npu_instance = &state;
	CHECK(cache_locks == 2 && cache_copies == 2 && cache_lookups == 2);
	CHECK(proofs == before_proofs && snapshots == before_snapshots);
}
static void reset(void)
{
	memset(&state, 0, sizeof(state)); memset(&raw, 0, sizeof(raw));
	state.sc_run_lock = 1; state.sc_mmio_ready = state.sc_run_pass = true;
	state.sc_run_stage = 4; state.sc_pid = A733_NPU_EXPECTED_PID;
	state.sc_rev = A733_NPU_EXPECTED_REV; state.sc_owned_clock = &clock1;
	state.sc_owned_power = 7; state.sc_bst = (void *)(uintptr_t)1;
	state.sc_dmat = (void *)(uintptr_t)2;
	raw.valid = true; raw.words[0][1] = 0x7fffffff;
	raw.words[0][3] = 0x9202; raw.words[0][4] = 0x1000003b;
	memcpy(raw.words[1], raw.words[0], sizeof(raw.words[0]));
	proofs = snapshots = allocations = infos = releases = 0;
	proof_error = snapshot_error = allocation_error = info_error = release_error = 0;
	identity = false;
	run_calls = run_copies = 0; run_error = 0; run_attempt = true;
	state.sc_dev = &context; state.sc_phandle = 13;
	memset(&task_context, 0, sizeof(task_context));
	task_allocations = task_runs = task_copies = 0;
	task_prepare_error = task_release_error = task_run_error = 0;
	task_keep = false;
	task_publish = task_compare = task_model = task_valid = true;
	lookup_error = 0;
	node_count = setup_errors = 0;
}
static void cold(void)
{
	for (unsigned stage = 0; stage < 6; stage++) {
		if (stage == 4) continue;
		reset(); state.sc_run_stage = stage;
		CHECK(sun60i_npu_engine_prepare(&state) == EBUSY);
		CHECK(!state.sc_engine_attempted && proofs == 0 && snapshots == 0 && allocations == 0);
	}
	for (unsigned reason = 0; reason < 5; reason++) {
		reset();
		switch (reason) {
		case 0: state.sc_mmio_ready = false; break;
		case 1: state.sc_run_pass = false; break;
		case 2: state.sc_run_result = EIO; break;
		case 3: state.sc_pid = 0; break;
		case 4: state.sc_rev = 0; break;
		}
		CHECK(sun60i_npu_engine_prepare(&state) == EBUSY);
		CHECK(proofs == 0 && snapshots == 0 && allocations == 0);
	}
	reset(); proof_error = EIO;
	CHECK(sun60i_npu_engine_prepare(&state) == EIO);
	CHECK(!state.sc_engine_attempted && snapshots == 0 && allocations == 0);
}
static void failures(void)
{
	reset(); snapshot_error = EFAULT;
	CHECK(sun60i_npu_engine_prepare(&state) == EFAULT);
	CHECK(state.sc_engine_attempted && !state.sc_engine_snapshot_valid && allocations == 0);
	CHECK(!state.sc_mmio_ready);
	CHECK(sun60i_npu_engine_prepare(&state) == EALREADY && snapshots == 1);
	reset(); raw.words[1][1] ^= 2;
	CHECK(sun60i_npu_engine_prepare(&state) == EBUSY);
	CHECK(state.sc_engine_snapshot_valid && allocations == 0 && !state.sc_engine_prepared);
	CHECK(!state.sc_mmio_ready);
	for (unsigned kind = 0; kind < 2; kind++) {
		reset();
		if (kind == 0)
			raw.words[0][1] = raw.words[1][1] = 0x7ffffffd;
		else
			raw.words[0][2] = raw.words[1][2] = 0x10;
		CHECK(sun60i_npu_engine_prepare(&state) == EBUSY);
		CHECK(state.sc_engine_snapshot_valid && !state.sc_engine_prepared);
		CHECK(!state.sc_mmio_ready && allocations == 0);
	}
	reset(); allocation_error = ENOMEM;
	CHECK(sun60i_npu_engine_prepare(&state) == ENOMEM);
	CHECK(state.sc_engine_dma == NULL && releases == 0 && !state.sc_engine_prepared);
	CHECK(!state.sc_mmio_ready);
	reset(); info_error = EIO;
	CHECK(sun60i_npu_engine_prepare(&state) == EIO);
	CHECK(releases == 1 && state.sc_engine_dma == NULL && !state.sc_engine_prepared);
	CHECK(!state.sc_mmio_ready);
	reset(); identity = true;
	CHECK(sun60i_npu_engine_prepare(&state) == EADDRINUSE);
	CHECK(releases == 1 && state.sc_engine_dma == NULL && !state.sc_engine_prepared);
	CHECK(!state.sc_mmio_ready);
	reset(); identity = true; release_error = EBUSY;
	CHECK(sun60i_npu_engine_prepare(&state) == EADDRINUSE);
	CHECK(state.sc_engine_release_error == EBUSY && state.sc_engine_dma == &context);
	CHECK(!state.sc_engine_prepared && releases == 1 && !state.sc_mmio_ready);
}
static void runs(void)
{
	for (unsigned why = 0; why < 7; why++) {
		reset(); state.sc_engine_dma = &context; state.sc_engine_prepared = 1;
		state.sc_native_profile = A733_NPU_DMA_PROFILE_FE;
		switch (why) {
		case 0: state.sc_run_stage = 0; break;
		case 1: state.sc_run_pass = false; break;
		case 2: state.sc_pid = 0; break;
		case 3: state.sc_engine_prepared = 0; break;
		case 4: state.sc_engine_dma = NULL; break;
		case 5: state.sc_engine_release_error = EBUSY; break;
		case 6: proof_error = EIO; break;
		}
		CHECK(sun60i_npu_engine_run(&state) != 0);
		CHECK(!state.sc_mmio_ready && !run_calls && !run_copies);
	}
	for (unsigned kind = 0; kind < 3; kind++) {
		reset(); state.sc_engine_dma = &context; state.sc_engine_prepared = 1;
		state.sc_native_profile = A733_NPU_DMA_PROFILE_FE;
		run_error = kind == 0 ? 0 : EIO; run_attempt = kind != 2;
		CHECK(sun60i_npu_engine_run(&state) == run_error);
		CHECK(!state.sc_mmio_ready && run_calls == 1 && run_copies == 1);
		CHECK(state.sc_engine_stage == (run_attempt ? 2U : 0U));
		CHECK(state.sc_engine_command_bytes == (run_attempt ? 40U : 0U));
		CHECK(state.sc_engine_run_cache.retained == (unsigned)run_attempt);
		unsigned old_proofs = proofs;
		if (run_attempt) {
			CHECK(sun60i_npu_engine_run(&state) == EALREADY);
			/* A later legacy ID stage cannot reopen user register access. */
			state.sc_mmio_ready = true;
			CHECK(sun60i_npu_mmio_ready(&state) == EBUSY);
		}
		CHECK(proofs == old_proofs && run_calls == 1 && run_copies == 1);
	}
}
static void
task_legacy_gate(void)
{
	reset();
	state.sc_task_attempted = 1;
	CHECK(sun60i_npu_mmio_ready(&state) == EBUSY);
	CHECK(proofs == 0 && snapshots == 0 && allocations == 0);
	/* An old caller cannot restore access by setting its readiness bit. */
	state.sc_mmio_ready = true;
	CHECK(sun60i_npu_mmio_ready(&state) == EBUSY);
	CHECK(proofs == 0);
}

static void
task_profiles(void)
{
	unsigned before;

	reset();
	CHECK(sun60i_npu_engine_prepare(&state) == 0);
	CHECK(state.sc_native_profile == A733_NPU_DMA_PROFILE_FE);
	before = proofs;
	CHECK(sun60i_npu_task_prepare(&state) == EBUSY);
	CHECK(sun60i_npu_task_run(&state) == EBUSY);
	CHECK(task_allocations == 0 && task_runs == 0 && proofs == before);
	CHECK(!state.sc_task_attempted && state.sc_mmio_ready);
	reset();
	proof_error = EIO;
	CHECK(sun60i_npu_engine_prepare(&state) == EIO);
	CHECK(state.sc_native_profile == A733_NPU_DMA_PROFILE_FE);
	before = proofs;
	CHECK(sun60i_npu_task_prepare(&state) == EBUSY);
	CHECK(proofs == before && task_allocations == 0);
	reset();
	CHECK(sun60i_npu_task_prepare(&state) == 0);
	CHECK(state.sc_native_profile == A733_NPU_DMA_PROFILE_TASK);
	CHECK(state.sc_task_attempted == 1 && state.sc_task_stage == 1);
	CHECK(!state.sc_mmio_ready && state.sc_task == &task_context);
	CHECK(task_allocations == 1 && task_copies == 1 && task_runs == 0);
	CHECK(state.sc_task_cache.prepared == 1);
	CHECK(state.sc_task_command_bytes == 144);
	CHECK(state.sc_task_constants_bytes == 864);
	CHECK(state.sc_task_input_bytes == 256 && state.sc_task_output_bytes == 4096);
	before = proofs;
	CHECK(sun60i_npu_engine_prepare(&state) == EBUSY);
	CHECK(sun60i_npu_engine_run(&state) == EBUSY);
	CHECK(state.sc_native_profile == A733_NPU_DMA_PROFILE_TASK);
	CHECK(sun60i_npu_task_prepare(&state) == EALREADY);
	CHECK(proofs == before && snapshots == 0 && allocations == 0);
	CHECK(task_allocations == 1 && task_runs == 0 && !state.sc_mmio_ready);
}

static void
task_failures(void)
{

	for (unsigned why = 0; why < 6; why++) {
		unsigned before;

		reset();
		switch (why) {
		case 0: state.sc_run_stage = 0; break;
		case 1: state.sc_mmio_ready = false; break;
		case 2: state.sc_run_pass = false; break;
		case 3: state.sc_pid = 0; break;
		case 4: state.sc_run_result = EIO; break;
		case 5: proof_error = EIO; break;
		}
		CHECK(sun60i_npu_task_prepare(&state) != 0);
		CHECK(state.sc_task_attempted == 1 && state.sc_task_stage == 1);
		CHECK(state.sc_native_profile == A733_NPU_DMA_PROFILE_TASK);
		CHECK(!state.sc_mmio_ready && task_allocations == 0);
		before = proofs;
		state.sc_mmio_ready = true;
		CHECK(sun60i_npu_mmio_ready(&state) == EBUSY);
		CHECK(sun60i_npu_task_prepare(&state) == EALREADY);
		CHECK(sun60i_npu_task_run(&state) == EINVAL);
		CHECK(proofs == before && task_allocations == 0 && task_runs == 0);
	}
	for (unsigned keep = 0; keep < 2; keep++) {
		reset();
		task_prepare_error = ENOMEM;
		task_keep = keep != 0;
		task_release_error = EBUSY;
		CHECK(sun60i_npu_task_prepare(&state) == ENOMEM);
		CHECK(state.sc_task_attempted == 1 && !state.sc_mmio_ready);
		CHECK(state.sc_task == (keep ? &task_context : NULL));
		CHECK(state.sc_task_cache.prepared == 0);
		CHECK(state.sc_task_cache.release_error == (keep ? EBUSY : 0U));
		CHECK(sun60i_npu_task_run(&state) == EINVAL);
		CHECK(sun60i_npu_task_prepare(&state) == EALREADY);
		CHECK(task_allocations == 1 && task_runs == 0);
	}
}

static void
task_execution(void)
{

	for (unsigned kind = 0; kind < 5; kind++) {
		unsigned before;

		reset();
		CHECK(sun60i_npu_task_run(&state) == EINVAL);
		CHECK(proofs == 0 && task_runs == 0);
		CHECK(sun60i_npu_task_prepare(&state) == 0);
		task_run_error = kind == 0 ? 0 : EIO;
		task_publish = kind != 1;
		task_valid = kind == 0 || kind >= 3;
		task_compare = kind == 0 || kind == 4;
		task_model = kind == 0;
		CHECK(sun60i_npu_task_run(&state) == task_run_error);
		CHECK(state.sc_task_stage == 2 && task_runs == 1);
		CHECK(state.sc_task_cache.retained == (unsigned)task_publish);
		CHECK(state.sc_task_cache.compare.valid == (unsigned)task_valid);
		CHECK(state.sc_task_cache.compare.pass == (unsigned)task_compare);
		CHECK(state.sc_task_cache.run.model_pass == (unsigned)task_model);
		CHECK(!state.sc_mmio_ready && task_copies == 2);
		before = proofs;
		state.sc_mmio_ready = true;
		CHECK(sun60i_npu_mmio_ready(&state) == EBUSY);
		CHECK(sun60i_npu_task_run(&state) == EALREADY);
		CHECK(sun60i_npu_task_prepare(&state) == EALREADY);
		CHECK(sun60i_npu_engine_prepare(&state) == EBUSY);
		CHECK(sun60i_npu_engine_run(&state) == EBUSY);
		CHECK(task_runs == 1 && task_copies == 2 && proofs == before);
	}
}

static int
stage_request(uint32_t value)
{
	struct sysctlnode node = { CTLTYPE_INT, NULL };
	int error;

	state.sc_run_lock = 0;
	error = sun60i_npu_sysctl_task_stage(&node, &value);
	CHECK(state.sc_run_lock == 0);
	state.sc_run_lock = 1;
	return error;
}

static void
task_stage_controls(void)
{
	struct sysctlnode node = { CTLTYPE_INT, NULL };
	unsigned before;

	reset();
	CHECK(stage_request(0) == 0 && state.sc_native_profile == 0);
	CHECK(stage_request(3) == EINVAL && !state.sc_task_attempted);
	CHECK(stage_request(2) == EINVAL && task_runs == 0);
	lookup_error = EFAULT;
	CHECK(stage_request(1) == EFAULT && !state.sc_task_attempted);
	lookup_error = 0;
	CHECK(stage_request(1) == 0 && state.sc_task_result == 0);
	CHECK(stage_request(1) == EALREADY && task_allocations == 1);
	CHECK(stage_request(2) == 0 && state.sc_task_stage == 2);
	CHECK(state.sc_task_cache.run.model_pass == 1);
	before = task_runs;
	CHECK(stage_request(2) == EALREADY && task_runs == before);
	CHECK(stage_request(0) == 0 && state.sc_task_stage == 2);
	CHECK(state.sc_native_profile == A733_NPU_DMA_PROFILE_TASK);
	CHECK(stage_request(1) == EALREADY && task_allocations == 1);
	state.sc_run_lock = 0;
	CHECK(sun60i_npu_sysctl_task_stage(&node, NULL) == 0);
	CHECK(cache_result == 2);
	sun60i_npu_instance = NULL;
	CHECK(sun60i_npu_sysctl_task_stage(&node, NULL) == ENXIO);
	sun60i_npu_instance = &state;
	state.sc_run_lock = 1;
}

static void
task_cached_nodes(void)
{
	struct sysctlnode root = { 0, NULL };
	unsigned before_proofs, before_snapshots;
	bool count_seen = false, baseline_seen = false;

	reset();
	sun60i_npu_sysctl_task_setup(&state, &root);
	CHECK(node_count == 133 && setup_errors == 0);
	before_proofs = proofs;
	before_snapshots = snapshots;
	state.sc_run_lock = 0;
	for (size_t i = 0; i < node_count; i++) {
		struct cached_node *saved = &nodes[i];
		struct sysctlnode node = { saved->type, saved->data };
		uint64_t expected;

		CHECK((saved->flags & CTLFLAG_ANYWRITE) == 0);
		if (!strcmp(saved->name, "task_stage")) {
			CHECK(saved->flags == CTLFLAG_READWRITE);
			CHECK(saved->type == CTLTYPE_INT && saved->data == NULL);
			CHECK(saved->callback == sun60i_npu_sysctl_task_stage);
			CHECK(saved->callback(&node, NULL) == 0);
			continue;
		}
		CHECK(saved->flags == CTLFLAG_READONLY && saved->data != NULL);
		CHECK(saved->callback == sun60i_npu_sysctl_engine_value);
		if (saved->type == CTLTYPE_INT) {
			expected = UINT32_MAX;
			memcpy(saved->data, &expected, sizeof(uint32_t));
		} else {
			CHECK(saved->type == CTLTYPE_QUAD);
			expected = UINT64_C(0x1234567889abcdef);
			memcpy(saved->data, &expected, sizeof(expected));
		}
		CHECK(saved->callback(&node, NULL) == 0);
		CHECK(cache_result == expected);
		CHECK(saved->callback(&node, &expected) == EPERM);
		if (!strcmp(saved->name, "task_run_irq_count")) {
			CHECK(saved->type == CTLTYPE_QUAD);
			count_seen = true;
		}
		if (!strcmp(saved->name, "task_run_irq_baseline")) {
			CHECK(saved->type == CTLTYPE_QUAD);
			baseline_seen = true;
		}
	}
	CHECK(count_seen && baseline_seen);
	CHECK(proofs == before_proofs && snapshots == before_snapshots);
	CHECK(task_allocations == 0 && task_runs == 0 && task_copies == 0);
	state.sc_run_lock = 1;
}

int main(void)
{
	cold(); failures(); reset();
	CHECK(sun60i_npu_engine_prepare(&state) == 0);
	CHECK(state.sc_engine_stage == 1 && state.sc_engine_prepared && state.sc_engine_snapshot_valid);
	CHECK(state.sc_engine_dma == &context && snapshots == 1 && allocations == 1 && infos == 1 && releases == 0);
	CHECK(!state.sc_engine_dma_info.retained);
	CHECK(state.sc_mmio_ready && sun60i_npu_mmio_ready(&state) == 0);
	CHECK(sun60i_npu_engine_prepare(&state) == EALREADY);
	CHECK(snapshots == 1 && allocations == 1 && releases == 0);
	cache(); runs(); task_legacy_gate();
	task_profiles(); task_failures(); task_execution();
	task_stage_controls(); task_cached_nodes();
	printf("A733 NPU native consumer: %u checks passed\n", checks); return 0;
}
