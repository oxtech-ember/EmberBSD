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
#include "sun60i_a733_npu_dma.h"
#include "sun60i_a733_npu_run.h"
struct sun60i_a733_npu_dma { int id; };
struct sun60i_npu_softc {
	struct sun60i_a733_npu_run sc_engine_run;
	struct sun60i_a733_npu_run_status sc_engine_run_cache;
	void *sc_dev;
	int sc_phandle;
	int sc_run_lock;
	bool sc_mmio_ready, sc_run_pass;
	u_int sc_run_stage, sc_run_result;
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

#define CTLTYPE_INT 2
#define CTLTYPE_QUAD 4
#define SYSCTL_TYPE(x) ((x) & 15)
struct sysctlnode { unsigned sysctl_flags; void *sysctl_data; };
#define SYSCTLFN_ARGS const struct sysctlnode *rnode, const void *newp
#define SYSCTLFN_CALL(node) node, newp
static struct sun60i_npu_softc *sun60i_npu_instance = &state;
static unsigned cache_locks, cache_copies, cache_lookups;
static uint64_t cache_result;
static void mutex_enter(int *lock) { CHECK(*lock == 0); *lock = 1; cache_locks++; }
static void mutex_exit(int *lock) { CHECK(*lock == 1); *lock = 0; }
static void *cache_copy(void *to, const void *from, size_t bytes)
{
	CHECK(state.sc_run_lock == 1); cache_copies++; return memcpy(to, from, bytes);
}
static int sysctl_lookup(const struct sysctlnode *node, const void *newp)
{
	CHECK(state.sc_run_lock == 0 && newp == NULL); cache_lookups++;
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
	cache(); runs();
	printf("A733 NPU native consumer: %u checks passed\n", checks); return 0;
}
