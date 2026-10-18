/* Origin: EmberBSD; AI-assisted actual NPU register range controls. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdbool.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int bus_space_tag_t;
typedef int bus_space_handle_t;
typedef unsigned long bus_addr_t;
typedef unsigned long bus_size_t;
typedef struct { bool held; } kmutex_t;
struct clk { int id; };
struct sun60i_npu_softc {
	int sc_bst;
	uint64_t sc_pid, sc_rev, sc_date;
	kmutex_t sc_run_lock;
	bool sc_mmio_ready, sc_run_pass;
	unsigned sc_run_stage, sc_run_result;
	struct clk *sc_owned_clock;
	int sc_owned_power;
};
#define KASSERT(c) assert(c)
static void mutex_enter(kmutex_t *m) { assert(!m->held); m->held = true; }
static void mutex_exit(kmutex_t *m) { assert(m->held); m->held = false; }
#define mutex_owned(m) ((m)->held)
#include "vip-abi.h"
#include "npu-register-defs.h"
#define CHECK(c) do { if (!(c)) { \
	fprintf(stderr, "FAIL: register contract line %d: %s\n", __LINE__, #c); \
	exit(1); } checks++; } while (0)
static unsigned checks, maps, unmaps, peeks, wide_reads, range_errors;
static unsigned fail_peek;
static int map_error;
static bus_size_t mapped_size;
static unsigned ccu_calls, power_calls;
static int ccu_error, power_error;
static bool require_lock;
static struct sun60i_npu_softc *current;
static struct clk clock;
static int sun60i_a733_ccu_npu_ready(struct clk *clk, const void *owner) {
	CHECK(current->sc_run_lock.held && clk == &clock && owner == current);
	ccu_calls++; return ccu_error;
}
static int sun60i_a733_pck_npu_ready(int node, const void *owner) {
	CHECK(current->sc_run_lock.held && node == 17 && owner == current);
	power_calls++; return power_error;
}

static int
bus_space_map(bus_space_tag_t tag, bus_addr_t addr, bus_size_t size,
    int flags, bus_space_handle_t *handle)
{
	CHECK(tag == 3 && addr == A733_NPU_BASE && size == 0x34 && flags == 0);
	CHECK(!require_lock || current->sc_run_lock.held);
	maps++;
	if (map_error)
		return map_error;
	mapped_size = size;
	*handle = 41;
	return 0;
}
static void
bus_space_unmap(bus_space_tag_t tag, bus_space_handle_t handle,
    bus_size_t size)
{
	CHECK(tag == 3 && handle == 41 && size == mapped_size);
	unmaps++;
	mapped_size = 0;
}
static int
bus_space_peek_4(bus_space_tag_t tag, bus_space_handle_t handle,
    bus_size_t offset, uint32_t *value)
{
	CHECK(tag == 3 && handle == 41 && offset + 4 <= mapped_size);
	peeks++;
	if (fail_peek == peeks)
		return 1;
	*value = offset == 0x30 ? 0x1000003b :
	    offset == 0x24 ? 0x9202 : offset == 0x28 ? 0x20210101 : 0x12345678;
	return 0;
}
/* Retain the 64-bit API double so a future over-wide read fails causally. */
int
bus_space_peek_8(bus_space_tag_t tag, bus_space_handle_t handle,
    bus_size_t offset, uint64_t *value)
{
	CHECK(tag == 3 && handle == 41);
	wide_reads++;
	if (offset + 8 > mapped_size) {
		range_errors++;
		return 1;
	}
	*value = 0x1000003b;
	return 0;
}
#include "npu-registers.h"

static void
reset(void)
{
	maps = unmaps = peeks = wide_reads = range_errors = fail_peek = 0;
	mapped_size = 0;
	map_error = 0;
	ccu_calls = power_calls = 0;
	ccu_error = power_error = 0;
}
static void
ready(struct sun60i_npu_softc *sc)
{
	memset(sc, 0, sizeof(*sc));
	sc->sc_bst = 3; sc->sc_pid = A733_NPU_EXPECTED_PID;
	sc->sc_rev = A733_NPU_EXPECTED_REV; sc->sc_mmio_ready = true;
	sc->sc_run_pass = true; sc->sc_run_stage = A733_NPU_STAGE_POWER_ID;
	sc->sc_owned_clock = &clock; sc->sc_owned_power = 17;
	current = sc; require_lock = true;
}
int
main(void)
{
	(void)mutex_enter; (void)mutex_exit;
	(void)sun60i_a733_ccu_npu_ready; (void)sun60i_a733_pck_npu_ready;
	struct sun60i_npu_softc sc = { .sc_bst = 3 };
	struct vip_drv_reg reg;
	const uint32_t invalid[] = { 0, 1, 2, 3, 0x31, 0x34, UINT32_MAX };
	int error;

	reset();
	error = sun60i_npu_read_ids(&sc);
	CHECK(error == 0 && range_errors == 0 && wide_reads == 0);
	CHECK(maps == 1 && peeks == 3 && unmaps == 1);
	CHECK(sc.sc_pid == 0x1000003b && sc.sc_rev == 0x9202 &&
	    sc.sc_date == 0x20210101);
	for (unsigned n = 1; n <= 3; n++) {
		reset();
		fail_peek = n;
		sc.sc_pid = 1; sc.sc_rev = 2; sc.sc_date = 3;
		error = sun60i_npu_read_ids(&sc);
		CHECK(error == EFAULT && unmaps == 1);
		CHECK(sc.sc_pid == 1 && sc.sc_rev == 2 && sc.sc_date == 3);
	}
	reset(); map_error = ENOMEM;
	error = sun60i_npu_read_ids(&sc);
	CHECK(error == ENOMEM && maps == 1 && unmaps == 0 && peeks == 0);
	/* Cold, failed and stale ownership must stop before map or NPU peek. */
	for (unsigned test = 0; test < 12; test++) {
		reset(); ready(&sc);
		switch (test) {
		case 0: case 1: case 2: case 3: sc.sc_run_stage = test; break;
		case 4: sc.sc_mmio_ready = false; break;
		case 5: sc.sc_run_pass = false; break;
		case 6: sc.sc_run_result = EBUSY; break;
		case 7: sc.sc_pid ^= 1; break;
		case 8: sc.sc_rev ^= 1; break;
		case 9: ccu_error = EBUSY; break;
		case 10: power_error = EBUSY; break;
		case 11: ccu_error = EFAULT; break;
		}
		reg.offset = 0x30; reg.value = 7;
		error = sun60i_npu_vip_read_reg(&sc, &reg);
		CHECK(error == VIP_STATUS_IO && maps == 0 && peeks == 0 &&
		    unmaps == 0 && reg.value == 7 && !sc.sc_run_lock.held);
		CHECK(test >= 9 || (ccu_calls == 0 && power_calls == 0));
		CHECK(test != 9 || power_calls == 0);
	}
	ready(&sc);
	for (unsigned offset = 4; offset <= 0x30; offset += 4) {
		reset();
		memset(&reg, 0, sizeof(reg)); reg.offset = offset;
		error = sun60i_npu_vip_read_reg(&sc, &reg);
		CHECK(error == VIP_STATUS_SUCCESS && maps == 1 && peeks == 1 &&
		    unmaps == 1 && wide_reads == 0 && range_errors == 0);
	}
	for (unsigned n = 0; n < sizeof(invalid) / sizeof(invalid[0]); n++) {
		reset();
		memset(&reg, 0, sizeof(reg)); reg.offset = invalid[n];
		error = sun60i_npu_vip_read_reg(&sc, &reg);
		CHECK(error == VIP_STATUS_INVALID && maps == 0 && peeks == 0);
	}
	reset(); fail_peek = 1; reg.offset = 0x30; reg.value = 7;
	error = sun60i_npu_vip_read_reg(&sc, &reg);
	CHECK(error == VIP_STATUS_IO && unmaps == 1 && reg.value == 7);
	reset(); map_error = ENOMEM;
	error = sun60i_npu_vip_read_reg(&sc, &reg);
	CHECK(error == VIP_STATUS_IO && unmaps == 0 && peeks == 0);
	printf("PASS: %u actual-source NPU readiness, range and failure checks\n", checks);
	return 0;
}
