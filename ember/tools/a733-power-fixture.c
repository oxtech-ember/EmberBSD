/* Origin: EmberBSD; AI-assisted tests executing the production FDT and PCK600 code. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <sys/queue.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

typedef unsigned int u_int;
typedef void *device_t;
typedef void *cfdata_t;
typedef void *bus_space_tag_t;
typedef unsigned int bus_space_handle_t;
typedef size_t bus_size_t;
typedef uintptr_t bus_addr_t;
typedef struct { bool initialized, held; } kmutex_t;
#ifndef __BIT
#define __BIT(n) (UINT32_C(1) << (n))
#endif
#ifndef __BITS
#define __BITS(hi, lo) ((__BIT((hi) + 1) - 1) & ~(__BIT(lo) - 1))
#endif
#ifndef __arraycount
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#endif
#define KASSERT(c) assert(c)
#define BUS_SPACE_BARRIER_READ 1
#define BUS_SPACE_BARRIER_WRITE 2
#define MUTEX_DEFAULT 0
#define IPL_NONE 0
#define KM_SLEEP 0
#define CFATTACH_DECL_NEW(n, s, m, a, d, r)
#define DEVICE_COMPAT_EOL { .compat = NULL }
#define device_private(d) (d)
#define aprint_debug_dev(...) ((void)0)
#define aprint_error_dev aprint_normal_dev
#define aprint_error(...) ((void)0)
static char core_output[8192];
static void
aprint_normal_dev(device_t dev, const char *fmt, ...)
{
	va_list ap;
	size_t len = strlen(core_output);
	int n;
	va_start(ap, fmt);
	n = vsnprintf(core_output + len, sizeof(core_output) - len, fmt, ap);
	va_end(ap);
	assert(n >= 0 && (size_t)n < sizeof(core_output) - len);
}

#define aprint_normal(...) ((void)0)
#define aprint_naive(...) ((void)0)
static uint32_t
swap32(uint32_t v)
{
	const uint8_t probe[] = { 1, 0, 0, 0 };
	uint32_t native;
	memcpy(&native, probe, sizeof(native));
	return native == 1 ? __builtin_bswap32(v) : v;
}
#ifndef be32toh
#define be32toh(v) swap32(v)
#endif

struct clk { int enabled; };
struct fdt_attach_args { int faa_phandle; bus_space_tag_t faa_bst; };
struct device_compatible_entry { const char *compat; };
#include "power-interface.h"

static uint32_t registers[0xb000 / 4], specifier[8];
static uint32_t binding_cells;
static int prop_len, getprop_error, reg_error, map_error, clock_error;
static int register_error, missing_clock;
static bus_size_t binding_size;
static unsigned int reads, writes, barriers, mapped, unmapped, delay_us;
static unsigned int poll_reads, complete_after, deny_after, policy_written;
static bus_size_t fail_write;
static int legacy_calls, checked_calls, checked_error;
static int checked_get_calls;
static bool checked_state;
static kmutex_t *required_read_lock;
static bus_size_t unstable_reg;
static unsigned int unstable_read, state_checks;
static bool lease_test;
static unsigned lease_complete_peek;
static unsigned int peeks, fail_peek, change_peek;
static bus_size_t peek_trace[92];
static bus_size_t read_trace[6];
static struct clk clock_fixture;

static void *kmem_alloc(size_t n, int flags) { return calloc(1, n); }
static int
of_getprop_uint32(int node, const char *name, uint32_t *v)
{
	*v = binding_cells;
	return getprop_error;
}
static const void *
fdtbus_get_prop(int node, const char *name, int *len)
{
	*len = prop_len;
	return prop_len == -1 ? NULL : specifier;
}
static int fdtbus_get_phandle_from_native(int v) { return v; }
#include "power-fdt.h"

static int
fixture_register(device_t dev, int node,
    const struct fdtbus_powerdomain_controller_func *funcs)
{
	return register_error ? register_error :
	    fdtbus_register_powerdomain_controller(dev, node, funcs);
}
#define fdtbus_register_powerdomain_controller fixture_register
static int
fdtbus_get_reg(int node, u_int index, bus_addr_t *addr, bus_size_t *size)
{
	*addr = 0x07060000;
	*size = binding_size;
	return reg_error;
}
static struct clk *
fdtbus_clock_get_index(int node, u_int index)
{
	return missing_clock ? NULL : &clock_fixture;
}
static int clk_enable(struct clk *c)
{
	if (!clock_error) c->enabled++;
	return clock_error;
}
static int clk_disable(struct clk *c) { return --c->enabled; }
static int
bus_space_map(bus_space_tag_t tag, bus_addr_t addr, bus_size_t size,
    int flags, bus_space_handle_t *handle)
{
	if (!map_error) mapped++;
	*handle = 0;
	return map_error;
}
static void
bus_space_unmap(bus_space_tag_t tag, bus_space_handle_t handle, bus_size_t size)
{
	unmapped++;
}
static uint32_t
bus_space_read_4(bus_space_tag_t tag, bus_space_handle_t handle, bus_size_t off)
{
	assert(off < sizeof(registers) && off % 4 == 0);
	if (required_read_lock != NULL)
		assert(required_read_lock->held);
	reads++;
	if (reads <= __arraycount(read_trace))
		read_trace[reads - 1] = off;
	if (reads == unstable_read && off == unstable_reg)
		registers[off / 4] ^= __BIT(3);
	if (policy_written && off % 0x1000 == 8) {
		poll_reads++;
		if (complete_after && poll_reads >= complete_after)
			registers[off / 4] = registers[(off - 8) / 4] & 0xf;
		if (deny_after && poll_reads >= deny_after)
			registers[(off - 8) / 4] = registers[off / 4];
	}
	return registers[off / 4];
}
static int
bus_space_peek_4(bus_space_tag_t tag, bus_space_handle_t handle,
    bus_size_t off, uint32_t *value)
{
	assert(off < sizeof(registers) && off % 4 == 0);
	assert(required_read_lock != NULL && required_read_lock->held);
	assert(lease_test || peeks < __arraycount(peek_trace));
	peek_trace[peeks % __arraycount(peek_trace)] = off;
	if (lease_test && lease_complete_peek != 0 && peeks == lease_complete_peek) {
		registers[0x6008 / 4] = 8;
		registers[0x6014 / 4] = 0x100;
	}
	peeks++;
	if (peeks == fail_peek)
		return 1;
	if (peeks == change_peek)
		registers[off / 4] ^= __BIT(3);
	*value = registers[off / 4];
	return 0;
}
static void
bus_space_write_4(bus_space_tag_t tag, bus_space_handle_t handle,
    bus_size_t off, uint32_t value)
{
	assert(off < sizeof(registers) && off % 4 == 0);
	writes++;
	if (off == fail_write) return;
	registers[off / 4] = value;
	if (off % 0x1000 == 0) policy_written++;
}
static void
bus_space_barrier(bus_space_tag_t tag, bus_space_handle_t handle,
    bus_size_t off, bus_size_t size, int flags)
{
	assert(flags == (BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE));
	barriers++;
}
static void delay(unsigned int us) { delay_us += us; }
static void mutex_init(kmutex_t *m, int type, int ipl) { m->initialized = true; }
static void mutex_destroy(kmutex_t *m) { assert(!m->held); m->initialized = false; }
static void mutex_enter(kmutex_t *m) { assert(m->initialized && !m->held); m->held = true; }
static void mutex_exit(kmutex_t *m) { assert(m->held); m->held = false; }
static bool mutex_owned(kmutex_t *m) { return m->held; }
static int
of_compatible_match(int node, const struct device_compatible_entry *data)
{
	return strcmp(data[0].compat, "allwinner,sun60i-a733-pck-600") == 0;
}
#include "sun60i_a733_display.h"
#include "sun60i_a733_display.c"
#include "power-driver.h"
#undef fdtbus_register_powerdomain_controller

static struct sun60i_pck600_softc sc;
static struct fdt_attach_args attach_args = { .faa_phandle = 1 };
static unsigned int cases;
static void
reset_fixture(void)
{
	struct fdtbus_powerdomain_controller *pdc;
	while ((pdc = LIST_FIRST(&fdtbus_powerdomain_controllers)) != NULL) {
		LIST_REMOVE(pdc, pdc_next);
		free(pdc);
	}
	LIST_INIT(&sun60i_pck600_providers);
	memset(&sc, 0, sizeof(sc));
	memset(registers, 0, sizeof(registers));
	memset(specifier, 0, sizeof(specifier));
	binding_cells = 1;
	binding_size = sizeof(registers);
	getprop_error = reg_error = map_error = clock_error = 0;
	register_error = missing_clock = 0;
	reads = writes = barriers = mapped = unmapped = delay_us = 0;
	poll_reads = complete_after = deny_after = policy_written = 0;
	clock_fixture.enabled = 0;
	legacy_calls = checked_calls = checked_error = 0;
	checked_get_calls = 0;
	checked_state = false;
	required_read_lock = NULL;
	unstable_reg = SIZE_MAX;
	unstable_read = 0;
	peeks = fail_peek = change_peek = 0;
	lease_complete_peek = 0;
	memset(peek_trace, 0, sizeof(peek_trace));
	memset(read_trace, 0, sizeof(read_trace));
	fail_write = SIZE_MAX;
	specifier[0] = swap32(1);
	specifier[1] = swap32(4);
	prop_len = 8;
	core_output[0] = 0;
	cases++;
}
static void
attach_controller(void)
{
	required_read_lock = &sc.sc_lock;
	sun60i_pck600_attach(NULL, &sc, &attach_args);
	assert(mapped == 1 && unmapped == 0 && clock_fixture.enabled == 1);
	assert(LIST_FIRST(&fdtbus_powerdomain_controllers) != NULL);
	assert(reads == 0 && peeks == 92 && writes == 0);
	for (u_int r = 0; r < 12; r++)
		assert(peek_trace[80 + r] ==
		    ((r / 3) % 2 == 0 ? 0x1000 : 0xa000) + (r % 3) * 4);
	peeks = 0;
	reads = 0;
}
static void
legacy_set(device_t dev, const uint32_t *data, bool enable)
{
	legacy_calls++;
}
static int
checked_set(device_t dev, const uint32_t *data, bool enable)
{
	checked_calls++;
	return checked_error;
}

static int
checked_get(device_t dev, const uint32_t *data, bool *enabled)
{
	checked_get_calls++;
	*enabled = checked_state;
	return checked_error;
}

static void
expect_state(int index, int error, bool state)
{
	bool enabled;
	unsigned int i;

	for (i = 0; i < 2; i++) {
		enabled = i != 0;
		assert(fdtbus_powerdomain_is_enabled_index(2, index,
		    &enabled) == error);
		assert(enabled == (error == 0 ? state : i != 0));
		assert(writes == 0 && barriers == 0 && delay_us == 0);
		assert(peeks == 0);
		assert(!sc.sc_lock.held);
		state_checks++;
	}
}

static void
test_read_state(void)
{
	static const struct fdtbus_powerdomain_controller_func legacy = {
		.pdc_enable = legacy_set,
	}, checked = { .pdc_set = checked_set, .pdc_get = checked_get };
	const int errors[] = { ENXIO, EIO, ETIMEDOUT, EOPNOTSUPP };
	u_int id, mode, lock, i;
	bool enabled = true;

	reset_fixture();
	expect_state(0, ENXIO, false);
	assert(fdtbus_powerdomain_is_enabled_index(2, 0, NULL) == EINVAL);
	expect_state(-1, EINVAL, false);
	for (i = 0; i < 4; i++) {
		prop_len = (int[]){ -1, 0, 3, 4 }[i];
		expect_state(0, EINVAL, false);
	}
	prop_len = 8;
	binding_cells = UINT_MAX;
	expect_state(0, EINVAL, false);
	binding_cells = 1;
	assert(fdtbus_register_powerdomain_controller(NULL, 1, &legacy) == 0);
	expect_state(0, EOPNOTSUPP, false);
	expect_state(1, ENOENT, false);
	assert(legacy_calls == 0 && checked_calls == 0 && reads == 0);

	reset_fixture();
	assert(fdtbus_register_powerdomain_controller(NULL, 1, &checked) == 0);
	for (i = 0; i < __arraycount(errors); i++) {
		checked_error = errors[i];
		checked_state = i % 2 != 0;
		expect_state(0, errors[i], false);
	}
	checked_error = 0;
	for (i = 0; i < 2; i++) {
		checked_state = i != 0;
		expect_state(0, 0, checked_state);
	}
	assert(legacy_calls == 0 && checked_calls == 0 && reads == 0);
	/* Skip an unregistered earlier domain using its binding metadata. */
	specifier[0] = swap32(3);
	specifier[2] = swap32(1);
	specifier[3] = swap32(5);
	prop_len = 16;
	i = checked_get_calls;
	expect_state(0, ENXIO, false);
	assert(checked_get_calls == (int)i);
	expect_state(1, 0, true);
	/* A provider registered later makes the same strict query usable. */
	assert(fdtbus_register_powerdomain_controller(NULL, 3, &checked) == 0);
	expect_state(0, 0, true);
	prop_len = 12;
	expect_state(1, EINVAL, false);

	reset_fixture();
	binding_cells = 0;
	prop_len = 4;
	assert(fdtbus_register_powerdomain_controller(NULL, 1, &checked) == 0);
	expect_state(0, 0, false);

	for (id = 0; id < PCK600_NDOMAINS; id++) {
		const bus_size_t base = id * PCK600_DOMAIN_SIZE;

		reset_fixture();
		attach_controller();
		required_read_lock = &sc.sc_lock;
		specifier[1] = swap32(id);
		for (mode = 0; mode < 16; mode++) {
			for (lock = 0; lock < 4; lock++) {
				registers[base / 4] = mode |
				    (lock & 1 ? PCK600_LOCK : 0);
				registers[(base + 8) / 4] = mode |
				    (lock & 2 ? PCK600_LOCK : 0);
				expect_state(0, mode == PCK600_ON ||
				    mode == PCK600_OFF ? 0 : EOPNOTSUPP,
				    mode == PCK600_ON);
			}
		}
		registers[base / 4] = PCK600_ON;
		registers[(base + 8) / 4] = PCK600_OFF;
		expect_state(0, EBUSY, false);
		registers[(base + 8) / 4] = PCK600_ON;
		for (i = 0; i < 4; i++) {
			const bus_size_t reg = base + (i < 2 ? 0 : 8);
			const uint32_t bit = __BIT(i % 2 ? 24 : 8);

			registers[reg / 4] |= bit;
			expect_state(0, EOPNOTSUPP, false);
			registers[reg / 4] &= ~bit;
		}
		registers[(base + 4) / 4] = PCK600_EMULATION;
		expect_state(0, EOPNOTSUPP, false);
		registers[(base + 4) / 4] = 0;
		for (i = 0; i < 3; i++) {
			unstable_reg = base + i * 4;
			unstable_read = i + 4;
			reads = 0;
			enabled = true;
			assert(fdtbus_powerdomain_is_enabled_index(2, 0,
			    &enabled) == EBUSY && enabled);
			assert(reads == 6 && writes == 0);
			registers[unstable_reg / 4] ^= __BIT(3);
			unstable_read = 0;
			state_checks++;
		}
		assert(!sc.sc_failed[id]);
		expect_state(0, 0, true);
		sc.sc_failed[id] = true;
		reads = 0;
		expect_state(0, EIO, false);
		assert(reads == 0 && sc.sc_failed[id]);
	}
	reset_fixture();
	attach_controller();
	specifier[1] = swap32(PCK600_NDOMAINS);
	expect_state(0, EINVAL, false);
	specifier[1] = swap32(UINT_MAX);
	expect_state(0, EINVAL, false);
	assert(reads == 0);
	printf("PASS: %u PCK600/FDT read-only state checks\n", state_checks);
}

static void
test_core_observation(void)
{
	struct sun60i_pck600_observation observation, saved;
	const bus_size_t base = 6 * PCK600_DOMAIN_SIZE;
	int error;

	for (u_int scenario = 0; scenario < 6; scenario++) {
		reset_fixture();
		registers[base / 4] = registers[(base + 8) / 4] = PCK600_ON;
		error = 0;
		if (scenario == 1)
			registers[base / 4] = registers[(base + 8) / 4] = 0;
		if (scenario == 2) registers[base / 4] |= PCK600_DYNAMIC;
		if (scenario == 3) registers[(base + 4) / 4] = PCK600_EMULATION;
		if (scenario == 4) registers[(base + 8) / 4] = 0;
		if (scenario == 5) { unstable_reg = base; unstable_read = 4; }
		if (scenario >= 2) error = scenario < 4 ? EOPNOTSUPP : EBUSY;
		required_read_lock = &sc.sc_lock;
		mutex_init(&sc.sc_lock, MUTEX_DEFAULT, IPL_NONE);
		mutex_enter(&sc.sc_lock);
		assert(sun60i_pck600_observe(&sc, 6, &observation) == 0);
		mutex_exit(&sc.sc_lock);
		assert(observation.state_error == error);
		if (error == 0)
			assert(observation.enabled == (scenario == 0));
		assert(reads == 6 && writes == 0 && barriers == 0 && delay_us == 0);
		assert(peeks == 0);
		for (u_int i = 0; i < 6; i++)
			assert(read_trace[i] == base + i % 3 * 4);
		assert(!sc.sc_lock.held && !sc.sc_failed[6]);
	}
	memset(&observation, 0xa5, sizeof(observation)); saved = observation;
	reads = 0; sc.sc_failed[6] = true;
	mutex_enter(&sc.sc_lock);
	assert(sun60i_pck600_observe(&sc, 6, &observation) == EIO);
	assert(memcmp(&observation, &saved, sizeof(saved)) == 0 && reads == 0);
	mutex_exit(&sc.sc_lock);
	sun60i_pck600_report(&sc);
	assert(reads == 0 && peeks == 0 && writes == 0);
	reset_fixture();
	printf("PASS: 6 strict CORE6 observations and quarantined-state control\n");
}

/* Independent register order and domain bases, not the production tables. */
static const bus_size_t diagnostic_offsets[] = {
	0x000, 0x004, 0x008, 0x010, 0x014, 0x018, 0x020, 0x024,
	0x030, 0x034, 0x038, 0x03c, 0x160, 0x164, 0x170, 0x174,
	0xfb0, 0xfb4, 0xfc8, 0xfcc,
};

static void
diagnostic_fixture(void)
{
	reset_fixture();
	mutex_init(&sc.sc_lock, MUTEX_DEFAULT, IPL_NONE);
	required_read_lock = &sc.sc_lock;
	for (u_int d = 0; d < 2; d++) {
		const bus_size_t base = d == 0 ? 0x5000 : 0x6000;

		for (u_int r = 0; r < 20; r++)
			registers[(base + diagnostic_offsets[r]) / 4] =
			    0x10000000 | base | r;
		registers[(base + 0xfb0) / 4] = 0x00030100;
		registers[(base + 0xfb4) / 4] = 0;
		registers[(base + 0xfc8) / 4] = 0x0b61443b;
		registers[(base + 0xfcc) / 4] = 0x11;
	}
}

static int
diagnostic_inspect(struct sun60i_pck600_diagnostic *state)
{
	int error;

	mutex_enter(&sc.sc_lock);
	error = sun60i_pck600_inspect(&sc, state);
	mutex_exit(&sc.sc_lock);
	assert(reads == 0 && writes == 0 && barriers == 0 && delay_us == 0);
	assert(mapped == 0 && unmapped == 0 && clock_fixture.enabled == 0);
	for (u_int i = 0; i < peeks; i++)
		assert(peek_trace[i] == (i < 40 ? 0x5000 : 0x6000) +
		    diagnostic_offsets[i % 20]);
	return error;
}

static void
test_diagnostic(void)
{
	struct sun60i_pck600_diagnostic state, saved;
	unsigned int checks = 0;

	diagnostic_fixture();
	assert(diagnostic_inspect(&state) == 0 && peeks == 80);
	for (u_int d = 0; d < 2; d++) {
		const bus_size_t base = d == 0 ? 0x5000 : 0x6000;

		assert(state.changed[d] == 0);
		for (u_int s = 0; s < 2; s++)
			for (u_int r = 0; r < 20; r++)
				assert(state.sample[d][s][r] ==
				    registers[(base + diagnostic_offsets[r]) / 4]);
	}
	checks++;
	/* Every read can fail, even after a complete earlier domain/sample. */
	for (u_int i = 1; i <= 80; i++) {
		diagnostic_fixture();
		memset(&state, 0xa5, sizeof(state));
		saved = state;
		fail_peek = i;
		assert(diagnostic_inspect(&state) == EFAULT && peeks == i);
		assert(memcmp(&state, &saved, sizeof(state)) == 0);
		assert(!sc.sc_failed[5] && !sc.sc_failed[6]);
		/* A read fault is not a transition failure or a permanent quarantine. */
		peeks = fail_peek = 0;
		assert(diagnostic_inspect(&state) == 0 && peeks == 80);
		checks++;
	}
	/* Each changed field is reported, including policy and identity words. */
	for (u_int d = 0; d < 2; d++) {
		for (u_int r = 0; r < 20; r++) {
			diagnostic_fixture();
			change_peek = d * 40 + 20 + r + 1;
			assert(diagnostic_inspect(&state) == 0 && peeks == 80);
			assert(state.changed[d] == __BIT(r));
			assert(state.changed[1 - d] == 0);
			assert((state.sample[d][0][r] ^ state.sample[d][1][r]) == __BIT(3));
			assert(sun60i_pck600_channels(&state, d) == (r >= 16 ? -1 : 0));
			checks++;
		}
	}
	/* P and every supported Q-channel count, with two recognized revisions. */
	for (u_int c = 0; c <= 8; c++) {
		diagnostic_fixture();
		registers[(0x5000 + 0xfb0) / 4] |= c;
		registers[(0x6000 + 0xfb0) / 4] |= c;
		registers[(0x6000 + 0xfc8) / 4] = 0x0b60043b;
		assert(diagnostic_inspect(&state) == 0);
		assert(sun60i_pck600_channels(&state, 0) == (int)c);
		assert(sun60i_pck600_channels(&state, 1) == (int)c);
		checks++;
	}
	for (u_int d = 0; d < 2; d++) {
		const bus_size_t base = d == 0 ? 0x5000 : 0x6000;
		static const struct {
			bus_size_t reg;
			uint32_t value;
		} unknown[] = {
			{ 0xfc8, 0 }, { 0xfc8, UINT32_MAX },
			{ 0xfc8, 0x0b61443a }, { 0xfc8, 0x0b71443b },
			{ 0xfcc, 0 }, { 0xfcc, 0x10 }, { 0xfcc, 0x12 },
			{ 0xfcc, 0x10000011 }, { 0xfb0, 0 },
			{ 0xfb0, 0x00030109 }, { 0xfb0, 0x00030111 },
		};

		for (u_int i = 0; i < __arraycount(unknown); i++) {
			diagnostic_fixture();
			registers[(base + unknown[i].reg) / 4] = unknown[i].value;
			assert(diagnostic_inspect(&state) == 0 && peeks == 80);
			assert(state.changed[d] == 0);
			assert(sun60i_pck600_channels(&state, d) == -1);
			assert(sun60i_pck600_channels(&state, 1 - d) == 0);
			checks++;
		}
		diagnostic_fixture();
		sc.sc_failed[d == 0 ? 5 : 6] = true;
		memset(&state, 0x5a, sizeof(state));
		saved = state;
		assert(diagnostic_inspect(&state) == EIO && peeks == 0);
		assert(memcmp(&state, &saved, sizeof(state)) == 0);
		checks++;
	}
	/* Provider attachment publishes only a complete pair of raw samples. */
	for (u_int i = 0; i <= 80; i++) {
		diagnostic_fixture();
		fail_peek = i;
		registers[(0x5000 + 0xfb0) / 4] |= 2;
		sun60i_pck600_attach(NULL, &sc, &attach_args);
		assert(mapped == 1 && unmapped == 0 && clock_fixture.enabled == 1);
		assert(LIST_FIRST(&fdtbus_powerdomain_controllers) != NULL);
		assert(reads == 0 && writes == 0 && barriers == 0 && delay_us == 0);
		assert(!sc.sc_lock.held && !sc.sc_failed[5] && !sc.sc_failed[6]);
		if (i == 0) {
			assert(peeks == 92);
			assert(strstr(core_output, "GPU_TOP PPU metadata: Q-Channel, 2") != NULL);
			assert(strstr(core_output, "GPU_CORE PPU metadata: P-Channel, 1") != NULL);
			assert(strstr(core_output,
			    "not a power-state or GPU readiness guarantee") != NULL);
			assert(strstr(core_output, "GPU_TOP PWPR [0x000]:") != NULL);
			assert(strstr(core_output, "GPU_CORE PWCR [0x020]:") != NULL);
			assert(strstr(core_output,
			    "GPU_CORE AIDR [0xfcc]: 0x00000011 0x00000011") != NULL);
		} else {
			assert(peeks == i + 12);
			assert(strstr(core_output, "diagnostic unavailable") != NULL);
			assert(strstr(core_output, "GPU_TOP") == NULL);
			assert(strstr(core_output, "GPU_CORE") == NULL);
		}
		checks++;
	}
	diagnostic_fixture();
	registers[(0x6000 + 0xfc8) / 4] = 0;
	sun60i_pck600_report(&sc);
	assert(strstr(core_output, "GPU_CORE PPU metadata: "
	    "unrecognized or unstable, raw values only") != NULL);
	assert(strstr(core_output, "GPU_CORE PPU metadata: P-Channel") == NULL);
	assert(strstr(core_output,
	    "GPU_CORE IIDR [0xfc8]: 0x00000000 0x00000000") != NULL);
	checks++;
	reset_fixture();
	printf("PASS: %u bounded TOP5/CORE6 diagnostic scenarios\n", checks);
}

static void
test_display_observer(void)
{
	struct sun60i_a733_display_power observed, saved;

	memset(&saved, 0x5a, sizeof(saved));
	for (unsigned fault = 1; fault <= 12; fault++) {
		diagnostic_fixture();
		fail_peek = fault;
		observed = saved;
		assert(sun60i_a733_display_read_power(sun60i_a733_display_pck_read,
		    &sc, &observed) == EFAULT);
		assert(peeks == fault && reads == 0 && writes == 0);
		assert(!sc.sc_lock.held && barriers == 0 && delay_us == 0);
		assert(memcmp(&observed, &saved, sizeof(observed)) == 0);
		assert(!sc.sc_failed[1] && !sc.sc_failed[10]);
	}
	for (unsigned d = 0; d < 2; d++) {
		diagnostic_fixture();
		sc.sc_failed[d == 0 ? 1 : 10] = true;
		observed = saved;
		assert(sun60i_a733_display_read_power(sun60i_a733_display_pck_read,
		    &sc, &observed) == EIO);
		assert(peeks == d * 3 && reads == 0 && writes == 0);
		assert(!sc.sc_lock.held && barriers == 0 && delay_us == 0);
		assert(memcmp(&observed, &saved, sizeof(observed)) == 0);
	}
	for (unsigned fault = 81; fault <= 92; fault++) {
		diagnostic_fixture();
		fail_peek = fault;
		sun60i_pck600_attach(NULL, &sc, &attach_args);
		assert(peeks == fault && reads == 0 && writes == 0);
		assert(strstr(core_output, "display PCK observation unavailable") != NULL);
		assert(strstr(core_output, "display DE_SYS sample") == NULL);
		assert(strstr(core_output, "display VO1 sample") == NULL);
		assert(!sc.sc_lock.held && !sc.sc_failed[1] && !sc.sc_failed[10]);
		assert(LIST_FIRST(&fdtbus_powerdomain_controllers) != NULL);
	}
	reset_fixture();
	puts("PASS: 26 display PCK adapter fault/quarantine/publication scenarios");
}

static void
lease_fixture(void)

{
	reset_fixture();
	lease_test = true;
	for (u_int d = 5; d <= 6; d++) {
		const bus_size_t b = d * 0x1000;
		registers[b / 4] = 8;
		registers[(b + 8) / 4] = d == 5 ? 8 : 0;
		registers[(b + 0x14) / 4] = d == 5 ? 0x100 : 0;
		registers[(b + 0x20) / 4] = 0x101;
		registers[(b + 0x170) / 4] = 0x1f1f1f;
		registers[(b + 0x174) / 4] = 0x1f1f;
		registers[(b + 0xfb0) / 4] = 0x10130101;
		registers[(b + 0xfb4) / 4] = 2;
		registers[(b + 0xfc8) / 4] = 0x0b61143b;
		registers[(b + 0xfcc) / 4] = 0x11;
	}
	attach_controller();
}

static void
borrow_fixture(void)
{
	lease_fixture();
	for (u_int id = 0; id < PCK600_NDOMAINS; id++) {
		if (id != 1 && id != 4 && id != 10)
			continue;
		registers[id * PCK600_DOMAIN_SIZE / 4] = PCK600_ON;
		registers[(id * PCK600_DOMAIN_SIZE + 4) / 4] = 0;
		registers[(id * PCK600_DOMAIN_SIZE + 8) / 4] = PCK600_ON;
	}
	peeks = 0;
}

static void
test_borrowed_domains(void)
{
	const int gpu = 1, npu = 2, display = 3;
	uint32_t id[2] = { 0, 0 };
	unsigned cases = 0;

	borrow_fixture();
	assert(sun60i_a733_pck_gpu_reserve(1, &gpu) == 0);
	assert(sun60i_a733_pck_npu_retain(1, &gpu) == EINVAL);
	assert(sun60i_a733_pck_npu_reserve(1, &npu) == 0);
	assert(sun60i_a733_pck_display_reserve(1, &display) == 0);
	for (u_int domain = 0; domain < PCK600_NDOMAINS; domain++) {
		bool owned = domain == 1 || domain == 4 || domain == 5 || domain == 6 || domain == 10;
		id[1] = swap32(domain);
		if (owned)
			assert(sun60i_pck600_set(&sc, id, true) == EBUSY);
	}
	id[1] = swap32(3);
	assert(sun60i_pck600_set(&sc, id, false) == 0);
	assert(sun60i_a733_pck_npu_ready(1, &npu) == EBUSY);
	assert(sun60i_a733_pck_npu_retain(1, &npu) == 0);
	assert(sun60i_a733_pck_display_retain(1, &display) == 0);
	assert(sun60i_a733_pck_npu_ready(1, &npu) == 0);
	assert(sun60i_a733_pck_display_ready(1, &display) == 0);
	assert(sun60i_a733_pck_gpu_release(1, &gpu) == 0);
	assert(sc.sc_lease[5].owner == NULL && sc.sc_lease[6].owner == NULL);
	assert(sun60i_a733_pck_npu_release(1, &npu) == EBUSY);
	assert(sun60i_a733_pck_display_release(1, &display) == EBUSY);
	assert(sun60i_a733_pck_npu_ready(1, &display) == EINVAL);
	assert(writes == 0); cases++;

	for (unsigned fault = 1; fault <= 12; fault++) {
		borrow_fixture(); fail_peek = fault;
		assert(sun60i_a733_pck_display_reserve(1, &display) == EFAULT);
		assert(sc.sc_lease[1].owner == NULL && sc.sc_lease[10].owner == NULL);
		assert(writes == 0 && peeks == fault); cases++;
	}
	for (unsigned fault = 1; fault <= 6; fault++) {
		borrow_fixture(); fail_peek = fault;
		assert(sun60i_a733_pck_npu_reserve(1, &npu) == EFAULT);
		assert(sc.sc_lease[4].owner == NULL && writes == 0); cases++;
	}
	for (unsigned scenario = 0; scenario < 7; scenario++) {
		borrow_fixture();
		switch (scenario) {
		case 0: registers[0xa008 / 4] = 0; break;
		case 1: registers[0xa004 / 4] = 1; break;
		case 2: registers[0xa000 / 4] |= PCK600_DYNAMIC; break;
		case 3: sc.sc_failed[10] = true; break;
		case 4: change_peek = 12; break;
		case 5: registers[0x1000 / 4] |= PCK600_DYNAMIC; break;
		case 6: registers[0x1008 / 4] = 0; break;
		}
		assert(sun60i_a733_pck_display_reserve(1, &display) != 0);
		assert(sc.sc_lease[1].owner == NULL && sc.sc_lease[10].owner == NULL);
		assert(writes == 0); cases++;
	}
	for (unsigned scenario = 0; scenario < 3; scenario++) {
		borrow_fixture();
		assert(sun60i_a733_pck_display_reserve(1, &display) == 0);
		if (scenario == 0) registers[0xa008 / 4] = 0;
		if (scenario == 1) sc.sc_failed[10] = true;
		if (scenario == 2) registers[0xa000 / 4] |= PCK600_LOCK;
		assert(sun60i_a733_pck_display_retain(1, &display) != 0);
		assert(!sc.sc_lease[1].retained && !sc.sc_lease[10].retained);
		assert(sun60i_a733_pck_display_release(1, &display) == 0);
		assert(sc.sc_lease[1].owner == NULL && sc.sc_lease[10].owner == NULL);
		cases++;
	}
	borrow_fixture();
	registers[0x4000 / 4] |= PCK600_LOCK;
	registers[0x4008 / 4] |= PCK600_LOCK;
	assert(sun60i_a733_pck_npu_reserve(1, &npu) == 0);
	assert(sun60i_a733_pck_npu_retain(1, &npu) == 0);
	assert(sun60i_a733_pck_npu_ready(1, &npu) == 0);
	registers[0x4008 / 4] = 0;
	assert(sun60i_a733_pck_npu_ready(1, &npu) == EBUSY);
	assert(sun60i_a733_pck_npu_release(1, &npu) == EBUSY);
	assert(writes == 0); cases++;
	lease_test = false;
	reset_fixture();
	printf("PASS: %u independent NPU/display PCK lease scenarios\n", cases);
}

#include "a733-display-power-fixture.h"

static void
test_gpu_lease(void)
{
	const int owner = 1, other = 2;
	uint32_t id[2] = { 0, 0 };
	unsigned checks = 0;

	lease_fixture();
	assert(sun60i_a733_pck_gpu_reserve(99, &owner) == ENXIO);
	assert(sun60i_a733_pck_gpu_reserve(1, &owner) == 0);
	assert(sun60i_a733_pck_gpu_reserve(1, &other) == EBUSY);
	assert(writes == 0);
	for (u_int d = 5; d <= 6; d++) {
		id[1] = swap32(d);
		assert(sun60i_pck600_set(&sc, id, true) == EBUSY);
		assert(sun60i_pck600_set(&sc, id, false) == EBUSY);
	}
	id[1] = swap32(4);
	assert(sun60i_pck600_set(&sc, id, false) == 0 && writes == 0);
	assert(sun60i_a733_pck_gpu_release(1, &other) == EINVAL);
	assert(sun60i_a733_pck_gpu_release(1, &owner) == 0);
	id[1] = swap32(5);
	assert(sun60i_pck600_set(&sc, id, true) == 0);
	checks += 10;

	for (unsigned test = 0; test < 16; test++) {
		static const bus_size_t bad_regs[] = { 0x5000, 0x5004, 0x5008,
		    0x5014, 0x5020, 0x5024, 0x5160, 0x5170, 0x5fb0,
		    0x5fb4, 0x5fc8, 0x5fcc, 0x6000, 0x6008, 0x6014, 0x6020 };
		lease_fixture();
		registers[bad_regs[test] / 4] ^= 8;
		assert(sun60i_a733_pck_gpu_reserve(1, &owner) != 0);
		assert(sc.sc_lease[PCK600_GPU_TOP].owner == NULL && writes == 0);
		checks++;
	}
	for (unsigned fault = 1; fault <= 80; fault++) {
		lease_fixture(); fail_peek = fault;
		assert(sun60i_a733_pck_gpu_reserve(1, &owner) == EFAULT);
		assert(sc.sc_lease[PCK600_GPU_TOP].owner == NULL && writes == 0 && peeks == fault);
		checks++;
	}
	for (unsigned change = 21; change <= 40; change++) {
		lease_fixture(); change_peek = change;
		assert(sun60i_a733_pck_gpu_reserve(1, &owner) != 0);
		assert(sc.sc_lease[PCK600_GPU_TOP].owner == NULL && writes == 0);
		checks++;
	}
	for (unsigned test = 0; test < 7; test++) {
		lease_fixture();
		assert(sun60i_a733_pck_gpu_reserve(1, &owner) == 0);
		assert(sun60i_a733_pck_gpu_retain(1, &owner) == 0);
		assert(sun60i_a733_pck_gpu_release(1, &owner) == EBUSY);
		peeks = 0;
		switch (test) {
		case 0: lease_complete_peek = 80; break;
		case 1: break; /* Never completes. */
		case 2: fail_peek = 37; break;
		case 3: registers[0x6000 / 4] = 0; break;
		case 4: registers[0x6038 / 4] = 4; break;
		case 5: registers[0x603c / 4] = 1; break;
		case 6: registers[0x6008 / 4] = 8; break; /* No Q acceptance. */
		}
		int error = sun60i_a733_pck_gpu_wait(1, &owner, false);
		assert(error == (test == 0 ? 0 : test == 2 ? EFAULT :
		    test == 3 ? EOPNOTSUPP : test == 4 || test == 5 ? EIO : ETIMEDOUT));
		assert(writes == 0 && sc.sc_lease[PCK600_GPU_TOP].retained && sc.sc_lease[PCK600_GPU_TOP].owner == &owner);
		if (test == 0) assert(delay_us == 10 && peeks == 160);
		if (test == 1 || test == 6) assert(delay_us == 10000 && peeks == 80080);
		checks++;
	}
	lease_test = false;
	reset_fixture();
	printf("PASS: %u experimental PCK ownership/wait scenarios\n", checks);
}

static void
test_gpu_request_on(void)
{
	const int owner = 1, other = 2;
	uint32_t id[2] = { 0, swap32(6) };
	unsigned checks = 0;

	lease_test = true;
	/* Ownership and provider errors. */
	lease_fixture();
	assert(sun60i_a733_pck_gpu_request_on(99, &owner, false) == ENXIO);
	assert(sun60i_a733_pck_gpu_request_on(1, &owner, false) == EINVAL);
	assert(writes == 0);
	assert(sun60i_a733_pck_gpu_reserve(1, &owner) == 0);
	assert(sun60i_a733_pck_gpu_request_on(1, &owner, false) == EINVAL);
	assert(sun60i_a733_pck_gpu_request_on(1, NULL, false) == EINVAL);
	assert(sun60i_a733_pck_gpu_retain(1, &other) == EBUSY);
	assert(writes == 0);
	checks += 7;

	/* A domain that completed on its own needs no fresh request. */
	lease_fixture();
	assert(sun60i_a733_pck_gpu_reserve(1, &owner) == 0);
	assert(sun60i_a733_pck_gpu_retain(1, &owner) == 0);
	registers[0x6008 / 4] = 8;
	registers[0x6014 / 4] = 0x100;
	assert(sun60i_a733_pck_gpu_request_on(1, &owner, false) == 0);
	assert(writes == 0);
	checks += 4;

	for (unsigned test = 0; test < 7; test++) {
		lease_fixture();
		assert(sun60i_a733_pck_gpu_reserve(1, &owner) == 0);
		assert(sun60i_a733_pck_gpu_retain(1, &owner) == 0);
		writes = peeks = 0;
		switch (test) {
		case 0:
			/* PWSR reaches ON after three policy-status polls. */
			complete_after = 3;
			break;
		case 1: break; /* Never completes; bounded timeout. */
		case 2: deny_after = 2; break; /* Reverted policy quarantine. */
		case 3: fail_write = 0x6c00; break; /* Ignored delay write. */
		case 4: fail_peek = 41; break; /* Fault inside precheck. */
		case 5: registers[0x6014 / 4] = 0x100; break; /* Not stalled. */
		case 6: registers[0x5020 / 4] = 0; break; /* TOP unhealthy. */
		}
		int error = sun60i_a733_pck_gpu_request_on(1, &owner, false);
		assert(error == (test == 0 ? 0 : test == 1 ? ETIMEDOUT :
		    test == 2 || test == 3 ? EIO :
		    test == 4 ? EFAULT : test == 5 ? EBUSY : EOPNOTSUPP));
		if (test == 0) {
			assert(registers[0x6008 / 4] == 8);
			assert(writes == 6); /* Five delays and the policy. */
			/* Q acceptance arrives after; the waiter sees it. */
			registers[0x6014 / 4] = 0x100;
			assert(sun60i_a733_pck_gpu_wait(1, &owner, false) == 0);
		}
		if (test == 1) {
			/* Timeout keeps the domain available to the waiter. */
			assert(writes == 6 && !sc.sc_failed[6]);
			assert(sun60i_a733_pck_gpu_wait(1, &owner, false) == ETIMEDOUT);
			assert(writes == 6);
		}
		if (error == EIO)
			assert(sc.sc_failed[6]);
		/* The reservation survives every outcome until release. */
		assert(sc.sc_lease[PCK600_GPU_TOP].owner == &owner && sc.sc_lease[PCK600_GPU_TOP].retained);
		assert(sun60i_a733_pck_gpu_release(1, &owner) == EBUSY);
		checks++;
	}
	/* Ordinary power requests stay refused for the reserved domains. */
	lease_fixture();
	assert(sun60i_a733_pck_gpu_reserve(1, &owner) == 0);
	assert(sun60i_a733_pck_gpu_retain(1, &owner) == 0);
	assert(sun60i_pck600_set(&sc, id, true) == EBUSY);
	checks += 3;

	lease_test = false;
	reset_fixture();
	printf("PASS: %u experimental CORE static request scenarios\n", checks);
}

int
main(void)

{
	static const struct fdtbus_powerdomain_controller_func legacy = {
		.pdc_enable = legacy_set,
	}, checked = { .pdc_enable = legacy_set, .pdc_set = checked_set }, empty = {0};
	const bus_size_t base = 4 * 0x1000;
	unsigned int old_writes, old_reads;

	test_gpu_lease();
	test_borrowed_domains();
	test_display_reserved_snapshot();
	test_gpu_request_on();
	test_read_state();
	cases = 0;

	reset_fixture();
	assert(sun60i_pck600_match(NULL, NULL, &attach_args));
	attach_controller();
	complete_after = 3;
	assert(fdtbus_powerdomain_enable(2) == 0);
	assert(registers[(base + 8) / 4] == 8 && delay_us == 20);
	assert(writes == 6 && barriers == writes);
	assert(registers[(base + 0x170) / 4] == 0x1f1f1f);
	assert(registers[(base + 0x174) / 4] == 0x1f1f);
	assert(registers[(base + 0xc00) / 4] == 0x08080808);
	assert(registers[(base + 0xc04) / 4] == 0x0808);
	assert(registers[(base + 0xc10) / 4] == 8);
	old_writes = writes;
	assert(fdtbus_powerdomain_enable(2) == 0 && writes == old_writes);
	assert(fdtbus_powerdomain_disable(2) == 0);
	assert(registers[(base + 8) / 4] == 0);

	reset_fixture(); attach_controller();
	registers[base / 4] = registers[(base + 8) / 4] = 0x30000;
	complete_after = 1;
	assert(fdtbus_powerdomain_enable(2) == 0);
	assert(registers[base / 4] == 0x30008); /* preserve operating mode */

	reset_fixture(); attach_controller();
	assert(fdtbus_powerdomain_enable(2) == ETIMEDOUT);
	assert(delay_us == 10000 && poll_reads == 1001 && sc.sc_failed[4]);
	old_reads = reads; old_writes = writes;
	assert(fdtbus_powerdomain_enable(2) == EIO);
	assert(reads == old_reads && writes == old_writes && !sc.sc_lock.held);

	reset_fixture(); attach_controller(); deny_after = 2;
	assert(fdtbus_powerdomain_enable(2) == EIO);
	assert(delay_us == 20 && sc.sc_failed[4]);

	for (unsigned int n = 0; n < 6; n++) {
		static const bus_size_t offsets[] = { 0x170, 0x174, 0xc00, 0xc04, 0xc10, 0 };
		reset_fixture(); attach_controller(); fail_write = base + offsets[n];
		assert(fdtbus_powerdomain_enable(2) == EIO);
		assert(writes == n + 1 && delay_us == 0 && sc.sc_failed[4]);
	}
	for (unsigned int n = 0; n < 5; n++) {
		reset_fixture(); attach_controller();
		if (n == 0) registers[base / 4] = __BIT(8);
		if (n == 1) registers[(base + 8) / 4] = __BIT(24);
		if (n == 2) registers[(base + 8) / 4] = __BIT(12);
		if (n == 3) registers[(base + 4) / 4] = 1;
		if (n == 4) registers[base / 4] = 8;
		assert(fdtbus_powerdomain_enable(2) == (n == 4 ? EBUSY : EOPNOTSUPP));
		assert(writes == 0 && !sc.sc_failed[4]);
	}
	reset_fixture(); attach_controller(); specifier[1] = swap32(11);
	assert(fdtbus_powerdomain_enable(2) == EINVAL && reads == 0);
	reset_fixture(); attach_controller(); specifier[1] = swap32(UINT32_MAX);
	assert(fdtbus_powerdomain_enable(2) == EINVAL && reads == 0);
	reset_fixture(); attach_controller(); specifier[1] = swap32(6);
	assert(fdtbus_powerdomain_disable(2) == EOPNOTSUPP && reads == 0);
	complete_after = 1;
	assert(fdtbus_powerdomain_enable(2) == 0);

	for (unsigned int n = 0; n < 8; n++) {
		reset_fixture();
		if (n == 0) binding_cells = 0;
		if (n == 1) binding_size--;
		if (n == 2) getprop_error = EINVAL;
		if (n == 3) reg_error = EINVAL;
		if (n == 4) missing_clock = 1;
		if (n == 5) map_error = EIO;
		if (n == 6) clock_error = EIO;
		if (n == 7) register_error = EIO;
		sun60i_pck600_attach(NULL, &sc, &attach_args);
		assert(LIST_FIRST(&fdtbus_powerdomain_controllers) == NULL);
		assert(clock_fixture.enabled == 0 && mapped == unmapped);
		assert(writes == 0 && reads == 0 && !sc.sc_lock.initialized);
	}

	reset_fixture();
	assert(fdtbus_register_powerdomain_controller(&sc, 1, &legacy) == 0);
	assert(fdtbus_powerdomain_enable_index(2, 0) == 0 && legacy_calls == 1);
	assert(fdtbus_powerdomain_disable(2) == 0 && legacy_calls == 2);
	reset_fixture(); checked_error = ETIMEDOUT;
	assert(fdtbus_register_powerdomain_controller(&sc, 1, &checked) == 0);
	assert(fdtbus_powerdomain_enable(2) == ETIMEDOUT);
	assert(checked_calls == 1 && legacy_calls == 0);
	specifier[2] = swap32(1); specifier[3] = swap32(5); prop_len = 16;
	assert(fdtbus_powerdomain_enable(2) == ETIMEDOUT && checked_calls == 2);
	checked_error = 0;
	assert(fdtbus_powerdomain_enable(2) == 0 && checked_calls == 4);
	assert(fdtbus_powerdomain_enable_index(2, 1) == 0 && checked_calls == 5);

	for (int n = -1; n <= 7; n++) {
		if (n == 4) continue; /* one phandle without its cell is tested too */
		reset_fixture();
		assert(fdtbus_register_powerdomain_controller(&sc, 1, &checked) == 0);
		prop_len = n;
		assert(fdtbus_powerdomain_enable(2) == EINVAL && checked_calls == 0);
	}
	reset_fixture();
	assert(fdtbus_register_powerdomain_controller(&sc, 1, &checked) == 0);
	prop_len = 4;
	assert(fdtbus_powerdomain_enable(2) == EINVAL && checked_calls == 0);
	reset_fixture();
	assert(fdtbus_powerdomain_enable(2) == ENXIO);
	assert(fdtbus_register_powerdomain_controller(&sc, 1, &empty) == EINVAL);
	binding_cells = UINT32_MAX;
	assert(fdtbus_register_powerdomain_controller(&sc, 1, &checked) == EINVAL);
	test_core_observation();
	printf("PASS: %u A733 PCK600/FDT production regression scenarios\n", cases);
	test_diagnostic();
	test_display_observer();
	reset_fixture();
	return 0;
}
