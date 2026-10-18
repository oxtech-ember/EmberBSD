/* Origin: EmberBSD; AI-assisted actual-source NPU acquire/write ordering. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
typedef unsigned int u_int;
typedef int bus_space_tag_t;
typedef int bus_space_handle_t;
typedef unsigned long bus_addr_t;
typedef unsigned long bus_size_t;
typedef struct {
	bool held;
} kmutex_t;
struct clk {
	int id;
};
struct sun60i_npu_softc {
	int sc_bst, sc_phandle;
	kmutex_t sc_run_lock;
	bool sc_mmio_ready;
	struct clk *sc_owned_clock;
	int sc_owned_power;
	uint64_t sc_pid, sc_rev, sc_date;
};
#define KASSERT(c) assert(c)
#define mutex_owned(m) ((m)->held)
#include "npu-register-defs.h"
static unsigned checks, maps, peeks, put_calls, releases;
static unsigned failure;
static bool power_reserved, power_retained, ccu_reserved, write_attempted;
static uint32_t cells[2];
static int binding_len;
static struct clk clock;
static struct sun60i_npu_softc sc;
static const int power_compat[1];
static char trace[64];
#define CHECK(c) do { assert(c); checks++; } while (0)
static void
record(char c)
{
	size_t n = strlen(trace);
	CHECK(n + 1 < sizeof(trace));
	trace[n] = c;
	trace[n + 1] = 0;
}

static uint32_t
be32toh(uint32_t v)
{
	const unsigned char *p = (const unsigned char *)&v;
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
	    ((uint32_t)p[2] << 8) | p[3];
}

static const void *
fdtbus_get_prop(int node, const char *name, int *len)
{
	CHECK(node == 5 && strcmp(name, "power-domains") == 0);
	*len = binding_len;
	return cells;
}

static int
fdtbus_get_phandle_from_native(int node)
{
	CHECK(node == 17);
	return node;
}

static int
of_compatible_match(int node, const void *compat)
{
	CHECK(node == 17 && compat == power_compat);
	return 1;
}

static struct clk *
fdtbus_clock_get(int node, const char *name)
{
	CHECK(node == 5 && strcmp(name, "clk_npu") == 0);
	record('g');
	return failure == 1 ? NULL : &clock;
}

static void
clk_put(struct clk *clk)
{
	CHECK(clk == &clock);
	record('x');
	put_calls++;
}

static int
sun60i_a733_pck_npu_reserve(int node, const void *owner)
{
	CHECK(node == 17 && owner == &sc && sc.sc_run_lock.held);
	record('p');
	if (failure == 2)
		return EBUSY;
	power_reserved = true;
	return 0;
}

static int
sun60i_a733_ccu_npu_reserve(struct clk *clk, const void *owner)
{
	CHECK(clk == &clock && owner == &sc && power_reserved);
	record('c');
	if (failure == 3)
		return EBUSY;
	ccu_reserved = true;
	return 0;
}

static int
sun60i_a733_pck_npu_retain(int node, const void *owner)
{
	CHECK(node == 17 && owner == &sc && power_reserved && ccu_reserved);
	record('r');
	if (failure == 4)
		return EBUSY;
	power_retained = true;
	return 0;
}

static int
sun60i_a733_ccu_npu_prepare(struct clk *clk, const void *owner, bool *attempted)
{
	CHECK(clk == &clock && owner == &sc && ccu_reserved && power_retained);
	record('w');
	*attempted = write_attempted = true;
	return failure == 5 ? EFAULT : 0;
}

static int
sun60i_a733_ccu_npu_ready(struct clk *clk, const void *owner)
{
	CHECK(clk == &clock && owner == &sc && power_retained && write_attempted);
	record('v');
	return failure == 6 ? EBUSY : 0;
}

static int
sun60i_a733_pck_npu_ready(int node, const void *owner)
{
	CHECK(node == 17 && owner == &sc && power_retained);
	record('q');
	return failure == 7 ? EBUSY : 0;
}

static int
sun60i_a733_ccu_npu_release(struct clk *clk, const void *owner)
{
	CHECK(clk == &clock && owner == &sc && ccu_reserved && !write_attempted);
	record('C');
	ccu_reserved = false;
	releases++;
	return 0;
}

static int
sun60i_a733_pck_npu_release(int node, const void *owner)
{
	CHECK(node == 17 && owner == &sc && power_reserved && !power_retained);
	record('P');
	power_reserved = false;
	releases++;
	return 0;
}

static int
bus_space_map(bus_space_tag_t tag, bus_addr_t addr, bus_size_t size,
	      int flags, bus_space_handle_t * handle)
{
	CHECK(sc.sc_run_lock.held && power_retained && write_attempted);
	CHECK(tag == 3 && addr == A733_NPU_BASE && size == A733_NPU_ID_SIZE && flags == 0);
	record('m');
	maps++;
	*handle = 41;
	return failure == 8 ? ENOMEM : 0;
}

static int
bus_space_peek_4(bus_space_tag_t tag, bus_space_handle_t handle,
		 bus_size_t off, uint32_t * value)
{
	CHECK(tag == 3 && handle == 41 && off + 4 <= A733_NPU_ID_SIZE);
	record('i');
	peeks++;
	if (failure == 9)
		return 1;
	*value = off == 0x30 ? (failure == 10 ? 1 : 0x1000003b) :
	    off == 0x24 ? (failure == 11 ? 1 : 0x9202) : 0x20210101;
	return 0;
}

static void
bus_space_unmap(bus_space_tag_t tag, bus_space_handle_t handle, bus_size_t size)
{
	CHECK(tag == 3 && handle == 41 && size == A733_NPU_ID_SIZE);
	record('u');
}
#include "npu-power.h"
int
main(void)
{
	static const char *const expected[] = {
		"gpcrwvqmiiiu", "g", "gpx", "gpcPx", "gpcrCPx", "gpcrw",
		"gpcrwv", "gpcrwvq", "gpcrwvqm", "gpcrwvqmiu",
		"gpcrwvqmiiiu", "gpcrwvqmiiiu"
	};
	const unsigned char binding[] = {0, 0, 0, 17, 0, 0, 0, 4};
	for (failure = 0; failure < sizeof(expected) / sizeof(expected[0]); failure++) {
		memset(&sc, 0, sizeof(sc));
		sc.sc_bst = 3;
		sc.sc_phandle = 5;
		sc.sc_run_lock.held = true;
		sc.sc_mmio_ready = true;
		memcpy(cells, binding, sizeof(cells));
		binding_len = sizeof(cells);
		trace[0] = 0;
		maps = peeks = put_calls = releases = 0;
		power_reserved = power_retained = ccu_reserved = write_attempted = false;
		int error = sun60i_npu_stage_power_id(&sc);
		CHECK(strcmp(trace, expected[failure]) == 0);
		CHECK((error == 0) == (failure == 0));
		CHECK(sc.sc_mmio_ready == (failure == 0));
		CHECK(failure < 5 || (releases == 0 && put_calls == 0 && power_retained));
		CHECK(failure >= 8 || maps == (failure == 0 ? 1 : 0));
		CHECK(failure > 4 || failure == 0 || (!power_reserved && !ccu_reserved));
	}
	printf("PASS: %u actual-source NPU acquire, retain, write and ID checks\n", checks);
	return 0;
}
