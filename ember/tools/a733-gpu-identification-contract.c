/* Origin: EmberBSD; causal checks of the production GPU identity consumer. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <sys/queue.h>

typedef unsigned int u_int;
typedef struct device *device_t;
typedef void *cfdata_t;
typedef void *bus_space_tag_t;
typedef uintptr_t bus_space_handle_t;
typedef uintptr_t bus_addr_t;
typedef size_t bus_size_t;
#ifndef __arraycount
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#endif
#define CFATTACH_DECL_NEW(n, s, m, a, d, r)
#define DEVICE_COMPAT_EOL { .compat = NULL }
#define device_private(d) ((d)->dv_private)
#define aprint_naive(...) ((void)0)
static void aprint_normal(const char *fmt, ...) {}
static void aprint_debug(const char *fmt, ...) {}
#define printf_nolog(...) ((void)0)
#define KASSERT(c) assert(c)
#define KERNEL_LOCKED_P() true
#define KERNEL_LOCK(n, l) ((void)0)
#define KERNEL_UNLOCK_ONE(l) ((void)0)
#define KM_SLEEP 0
#define AB_QUIET 1
#define AB_SILENT 2
#define AB_VERBOSE 4
#define mstohz(n) (n)
#define device_xname(d) "fixture"
#ifndef __printflike
#define __printflike(a, b)
#endif
struct device {
	device_t dv_parent;
	void *dv_private;
	unsigned int dv_pending;
	TAILQ_ENTRY(device) dv_pending_list;
};
#include "autoconf-structs.h"
TAILQ_HEAD(deferred_config_head, deferred_config);
static struct deferred_config_head deferred_config_queue =
    TAILQ_HEAD_INITIALIZER(deferred_config_queue);
static TAILQ_HEAD(, finalize_hook) config_finalize_list =
    TAILQ_HEAD_INITIALIZER(config_finalize_list);
static TAILQ_HEAD(, device) config_pending =
    TAILQ_HEAD_INITIALIZER(config_pending);
static int config_finalize_done, config_misc_lock, config_misc_cv;
static int config_do_twiddle, boothowto;
static const unsigned int hz = 100;
struct pdevinit { void (*pdev_attach)(int); int pdev_count; };
struct pdevinit pdevinit[] = { { NULL, 0 } };
static struct device root_device, soc_device, gpu_device;

static unsigned getticks(void) { return 0; }
static int aprint_get_error_count(void) { return 0; }
static void *kmem_alloc(size_t size, int flags) { return calloc(1, size); }
static void kmem_free(void *ptr, size_t size) { free(ptr); }
static void panic(const char *msg) { assert(!msg); }
static void mutex_enter(int *lock) { assert(!*lock); *lock = 1; }
static void mutex_exit(int *lock) { assert(*lock); *lock = 0; }
static int
cv_timedwait(int *cv, int *lock, int ticks)
{
	/* Turn a stranded pending device into a bounded, causal failure. */
	assert(!"configuration would wait for an orphaned callback");
	return 0;
}
static void
config_pending_incr(device_t dev)
{
	if (dev->dv_pending++ == 0)
		TAILQ_INSERT_TAIL(&config_pending, dev, dv_pending_list);
}
static void
config_pending_decr(device_t dev)
{
	assert(dev->dv_pending > 0);
	if (--dev->dv_pending == 0)
		TAILQ_REMOVE(&config_pending, dev, dv_pending_list);
}
struct fdt_attach_args { int faa_phandle; bus_space_tag_t faa_bst; };
struct device_compatible_entry { const char *compat; };
struct fdtbus_regulator { int acquired; };
struct clk { int acquired; };
#include "sun60i_a733_ccu.h"
#include "sun60i_a733_pck600.h"

static uint32_t pd[2], clocks[2], power_cells, clock_cells;
static int prepare_len = -1, request_len = -1;
static int pd_len, clock_len, managed_len, supply_len, observe_len;
static bool bad_power_compat, bad_pmic, bad_supply;
static bool missing_supply, missing_clock;
static int reg_error, state_error, voltage_error, power_error, ready_error;
static int map_error, peek_error, match_result;
static unsigned inspect_calls;
static int terminal_error, initial_error;
static bool preparation_timed_out;
static bus_addr_t binding_addr;
static bus_size_t binding_size;
static bool supply_on, power_on;
static unsigned supply_calls, fail_supply_call;
static u_int voltage, core_hz, bus_hz;
static uint64_t pbvnc;
static uint32_t core_id;
static int coreid_peek_error;
static struct fdtbus_regulator regulator;
static struct clk clock_fixture;
static struct sun60i_a733_gpu_state clock_observation;
static struct sun60i_a733_gpu_state terminal_observation;
static struct sun60i_a733_gpu_state initial_observation;
static unsigned int maps, peeks, unmaps, releases, clock_puts, checks;
static char events[128], normal_output[4096], error_output[256];
static size_t nevents;

static void
record(char event)
{
	assert(nevents + 1 < sizeof(events));
	events[nevents++] = event;
	events[nevents] = '\0';
}

static uint32_t
swap32(uint32_t value)
{
	const uint32_t one = 1;

	return *(const unsigned char *)&one == 1 ? __builtin_bswap32(value) : value;
}
#ifndef be32toh
#define be32toh(v) swap32(v)
#endif

static int
OF_getproplen(int phandle, const char *name)
{
	if (strcmp(name, "netbsd,experimental-clock-prepare") == 0)
		return prepare_len;
	if (strcmp(name, "netbsd,experimental-domain-request") == 0)
		return request_len;
	if (strcmp(name, "netbsd,observe-only") == 0)
		return observe_len;
	if (strcmp(name, "netbsd,consumer-managed-power") == 0)
		return managed_len;
	assert(strcmp(name, "gpu-supply") == 0);
	return supply_len;
}

static int
fdtbus_get_reg(int phandle, u_int index, bus_addr_t *addr, bus_size_t *size)
{
	assert(index == 0);
	*addr = binding_addr;
	*size = binding_size;
	return reg_error;
}

static const void *
fdtbus_get_prop(int phandle, const char *name, int *len)
{
	if (strcmp(name, "power-domains") == 0) {
		*len = pd_len;
		return pd_len < 0 ? NULL : pd;
	}
	assert(strcmp(name, "clocks") == 0);
	*len = clock_len;
	return clock_len < 0 ? NULL : clocks;
}

static int fdtbus_get_phandle_from_native(int phandle) { return phandle; }
static int
of_getprop_uint32(int phandle, const char *name, uint32_t *value)
{
	if (strcmp(name, "#power-domain-cells") == 0) {
		*value = power_cells;
		return phandle == 2 ? 0 : -1;
	}
	assert(strcmp(name, "#clock-cells") == 0);
	*value = clock_cells;
	return phandle == 3 ? 0 : -1;
}

static int
of_compatible_match(int phandle, const struct device_compatible_entry *compat)
{
	if (strcmp(compat[0].compat, "allwinner,sun60i-a733-gpu") == 0)
		return match_result;
	if (strcmp(compat[0].compat, "allwinner,sun60i-a733-pck-600") == 0)
		return phandle == 2 && !bad_power_compat;
	assert(strcmp(compat[0].compat, "x-powers,axp8191") == 0);
	return phandle == 6 && !bad_pmic;
}

static int
fdtbus_get_phandle(int phandle, const char *name)
{
	assert(strcmp(name, "gpu-supply") == 0);
	return 4;
}
static const char *
fdtbus_get_string(int phandle, const char *name)
{
	assert(phandle == 4 && strcmp(name, "name") == 0);
	return bad_supply ? "dcdc2" : "dcdc4";
}
static int OF_parent(int phandle) { return phandle + 1; }

static struct fdtbus_regulator *
fdtbus_regulator_acquire(int phandle, const char *name)
{
	record('A');
	assert(!regulator.acquired);
	if (missing_supply)
		return NULL;
	regulator.acquired = 1;
	return &regulator;
}
static int
fdtbus_regulator_is_enabled(struct fdtbus_regulator *reg, bool *enabled)
{
	assert(reg->acquired);
	record('S');
	if (++supply_calls == fail_supply_call)
		return EIO;
	*enabled = supply_on;
	return state_error;
}
static int
fdtbus_regulator_get_voltage(struct fdtbus_regulator *reg, u_int *uvol)
{
	assert(reg->acquired);
	record('V');
	*uvol = voltage;
	return voltage_error;
}
static int
fdtbus_powerdomain_is_enabled_index(int phandle, int index, bool *enabled)
{
	assert(index == 0);
	record('P');
	*enabled = power_on;
	return power_error;
}
static struct clk *
fdtbus_clock_get_index(int phandle, u_int index)
{
	assert(index == 0 && !clock_fixture.acquired);
	record('C');
	if (missing_clock)
		return NULL;
	clock_fixture.acquired = 1;
	return &clock_fixture;
}
int
sun60i_a733_ccu_gpu_inspect(struct clk *clock,
    struct sun60i_a733_gpu_state *state)
{
	assert(clock->acquired);
	record('Q');
	inspect_calls++;
	if (prepare_len == 0 && inspect_calls == 1) {
		if (initial_error != 0)
			return initial_error;
		*state = initial_observation;
		return 0;
	}
	if (prepare_len == 0 &&
	    (inspect_calls > 2 || preparation_timed_out)) {
		if (terminal_error != 0)
			return terminal_error;
		*state = terminal_observation;
		return 0;
	}
	if (ready_error == 0) {
		*state = clock_observation;
		state->core_hz = core_hz;
		state->bus_hz = bus_hz;
	}
	return ready_error;
}
static int
bus_space_map(bus_space_tag_t tag, bus_addr_t addr, bus_size_t size,
    int flags, bus_space_handle_t *handle)
{
	assert(addr == 0x01800000 && size == 0x28 && flags == 0);
	record('M');
	maps++;
	*handle = 0x1234;
	return map_error;
}
static int
bus_space_peek_4(bus_space_tag_t tag, bus_space_handle_t handle,
    bus_size_t offset, uint32_t *value)
{
	assert(handle == 0x1234 && offset == 0x18);
	record('i');
	*value = core_id;
	return coreid_peek_error;
}
static int
bus_space_peek_8(bus_space_tag_t tag, bus_space_handle_t handle,
    bus_size_t offset, uint64_t *value)
{
	assert(handle == 0x1234 && offset == 0x20);
	record('I');
	peeks++;
	*value = pbvnc;
	return peek_error;
}
static void
bus_space_unmap(bus_space_tag_t tag, bus_space_handle_t handle, bus_size_t size)
{
	assert(handle == 0x1234 && size == 0x28);
	record('U');
	unmaps++;
}
static void
clk_put(struct clk *clock)
{
	assert(clock->acquired);
	clock->acquired = 0;
	clock_puts++;
	record('c');
}
static void
fdtbus_regulator_release(struct fdtbus_regulator *reg)
{
	assert(reg->acquired);
	reg->acquired = 0;
	releases++;
	record('s');
}
static void
aprint_normal_dev(device_t dev, const char *fmt, ...)
{
	va_list ap;
	size_t len = strlen(normal_output);
	int written;

	va_start(ap, fmt);
	written = vsnprintf(normal_output + len, sizeof(normal_output) - len,
	    fmt, ap);
	va_end(ap);
	assert(written >= 0 && (size_t)written < sizeof(normal_output) - len);
}
static void
aprint_error_dev(device_t dev, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(error_output, sizeof(error_output), fmt, ap);
	va_end(ap);
}
/* Runtime staging stubs: the sysctl tree itself is not under test. */
struct sysctllog;
struct sysctlnode { void *sysctl_data; };
#define SYSCTLFN_ARGS struct sysctllog *log, void *sname, void *nam, \
	struct sysctlnode *rnode, const void *newp, size_t newlen
#define SYSCTLFN_CALL(rn) (log, sname, nam, (rn), newp, newlen)
#define sysctl_lookup(...) (0)
static int sysctl_createv_stub(void *first, ...) { (void)first; return 0; }
#define sysctl_createv(...) sysctl_createv_stub(__VA_ARGS__)
#define SYSCTL_DESCR(s) (s)
#define CTLTYPE_NODE 1
#define CTLTYPE_INT 2
#define CTLTYPE_QUAD 3
#define CTLFLAG_READWRITE 4
#define CTLFLAG_READONLY 8
#define CTL_HW 16
#define CTL_CREATE 32
#define CTL_EOL -1
typedef int kmutex_t;
#define MUTEX_DEFAULT 0
#define IPL_NONE 0
static void mutex_init(kmutex_t *m, int type, int ipl) { *m = 1; }
#define mutex_destroy(m) ((void)0)
#include "autoconf.h"
static int prepare_error, reserve_error, wait_error, retain_error;
static int request_error;
static int power_reserve_error, second_wait_error, final_ready_error;static unsigned prepares, reserves, waits, retains, unreserves, requests;
int sun60i_a733_ccu_gpu_reserve(struct clk *c, const void *owner)
{ reserves++; record('C'); return reserve_error; }
int sun60i_a733_pck_gpu_reserve(int node, const void *owner)
{ reserves++; record('P'); return power_reserve_error; }
int sun60i_a733_ccu_gpu_release(struct clk *c, const void *owner)
{ unreserves++; record('c'); return 0; }
int sun60i_a733_pck_gpu_release(int node, const void *owner)
{ unreserves++; record('p'); return 0; }
int sun60i_a733_pck_gpu_retain(int node, const void *owner)
{ retains++; record('H'); return retain_error; }
int sun60i_a733_pck_gpu_request_on(int node, const void *owner, bool quiet)
{ requests++; record('R'); return request_error; }
int sun60i_a733_ccu_gpu_prepare(struct clk *c, const void *owner, bool *held)
{
	prepares++; record('W'); *held = true;
	preparation_timed_out = prepare_error == ETIMEDOUT;
	return prepare_error;
}
int sun60i_a733_pck_gpu_wait(int node, const void *owner, bool quiet)
{ waits++; record('K'); return waits == 2 ? second_wait_error : wait_error; }
int sun60i_a733_ccu_gpu_ready(struct clk *c, u_int *core, u_int *bus)
{ record('F'); *core = core_hz; *bus = bus_hz; return final_ready_error; }
#include "driver.h"
#define CHECK(c) do { assert(c); checks++; } while (0)
static struct sun60i_gpu_softc sc;

static void
reset(void)
{
	assert(!regulator.acquired && !clock_fixture.acquired);
	assert(TAILQ_EMPTY(&config_pending));
	assert(TAILQ_EMPTY(&deferred_config_queue));
	assert(TAILQ_EMPTY(&config_finalize_list));
	memset(&sc, 0, sizeof(sc));
	root_device = (struct device){ 0 };
	soc_device = (struct device){ .dv_parent = &root_device };
	gpu_device = (struct device){ .dv_parent = &soc_device, .dv_private = &sc };
	config_finalize_done = 0;
	sc.sc_dev = &gpu_device;
	sc.sc_phandle = 1;
	sc.sc_run_pass = true;
	pd[0] = swap32(2);
	pd[1] = swap32(5);
	clocks[0] = swap32(3);
	clocks[1] = swap32(A733_CLK_GPU0);
	power_cells = clock_cells = 1;
	pd_len = clock_len = 8;
	managed_len = 0;
	observe_len = prepare_len = request_len = -1;
	prepare_error = reserve_error = wait_error = retain_error = 0;
	request_error = 0;
	power_reserve_error = second_wait_error = final_ready_error = 0;
	inspect_calls = 0; terminal_error = initial_error = 0;
	preparation_timed_out = false;
	prepares = reserves = waits = retains = unreserves = requests = 0;
	supply_calls = fail_supply_call = 0;
	supply_len = 4;
	bad_power_compat = bad_pmic = bad_supply = false;
	missing_supply = missing_clock = false;
	reg_error = state_error = voltage_error = power_error = ready_error = 0;
	map_error = peek_error = 0;
	match_result = 1;
	binding_addr = 0x01800000;
	binding_size = 0x8ffff;
	supply_on = power_on = true;
	voltage = 800000;
	core_hz = 400000000;
	bus_hz = 200000000;
	memset(&clock_observation, 0, sizeof(clock_observation));
	memset(&terminal_observation, 0, sizeof(terminal_observation));
	memset(&initial_observation, 0, sizeof(initial_observation));
	clock_observation.hosc_hz[0] = clock_observation.hosc_hz[1] = 24000000;
	pbvnc = UINT64_C(0x00240038006800b7);
	core_id = 0;
	coreid_peek_error = 0;
	maps = peeks = unmaps = releases = clock_puts = 0;
	nevents = 0;
	events[0] = normal_output[0] = error_output[0] = '\0';
}

static unsigned legacy_callbacks, extra_passes;
static void legacy_probe(device_t dev) { legacy_callbacks++; }
static int request_extra_pass(device_t dev) { return extra_passes++ == 0; }

static void
unavailable(int error)
{
	CHECK(sun60i_gpu_identify(&sc) == error);
	CHECK(maps == 0 && peeks == 0 && unmaps == 0);
	CHECK(!sc.sc_have_id);
	CHECK(!regulator.acquired && !clock_fixture.acquired);
}

static void
test_clock_observation(void)
{
	static const struct {
		enum sun60i_a733_gpu_reason reason;
		int error;
		const char *text;
	} failures[] = {
		{ A733_GPU_SNAPSHOT_CHANGED, EBUSY, "CCU snapshot changed" },
		{ A733_GPU_HOSC_CHANGED, EBUSY, "oscillator rate changed" },
		{ A733_GPU_DCXO_CHANGED, EBUSY, "DCXO status changed" },
		{ A733_GPU_MODULE_GATED, EBUSY, "GPU module gated" },
		{ A733_GPU_UPDATE_PENDING, EBUSY, "GPU update pending" },
		{ A733_GPU_BUS_GATED, EBUSY, "GPU bus gated" },
		{ A733_GPU_RESET_ASSERTED, EBUSY, "GPU reset asserted" },
		{ A733_GPU_MASTER_GATED, EBUSY, "GPU AHB master gated" },
		{ A733_GPU_REF_FLAGS, EBUSY, "PLL_REF flags" },
		{ A733_GPU_REF_RATE, EOPNOTSUPP, "PLL_REF rate" },
		{ A733_GPU_CORE_PARENT, EOPNOTSUPP, "GPU parent unsupported" },
		{ A733_GPU_CORE_PLL, EBUSY, "GPU parent PLL" },
		{ A733_GPU_CORE_DIVIDER, EOPNOTSUPP, "GPU divider" },
		{ A733_GPU_AHB_PARENT, EOPNOTSUPP, "AHB parent unsupported" },
		{ A733_GPU_AHB_PLL, EBUSY, "AHB parent PLL" },
		{ A733_GPU_AHB_DIVIDER, EOPNOTSUPP, "AHB divider" },
	};
	static const char * const registers[] = {
		"PLL_REF[0x000]", "PLL_PERIPH0[0x0a0]",
		"PLL_PERIPH0_PAT0[0x0a8]", "PLL_PERIPH0_PAT1[0x0ac]",
		"PLL_GPU0[0x0e0]", "PLL_GPU0_PAT0[0x0e8]",
		"PLL_GPU0_PAT1[0x0ec]", "GPU_CLK[0xb20]", "GPU_BGR[0xb24]",
		"AHB[0x500]", "AHB_MASTER[0x5c0]",
		"PERI0_GATE_EN[0x1908]", "PERI0_GATE_STAT[0x1988]",
	};
	char expected[128];

	for (u_int i = 0; i < __arraycount(failures); i++) {
		reset();
		/* Readiness fails before the OPP check; report the actual query. */
		voltage = 735000;
		sc.sc_core_hz = 111;
		sc.sc_bus_hz = 222;
		clock_observation.reason = failures[i].reason;
		clock_observation.readiness_error = failures[i].error;
		for (u_int reg = 0; reg < A733_GPU_NREGS; reg++) {
			clock_observation.sample[0][reg] = 0x10000000 + reg;
			clock_observation.sample[1][reg] = 0x10000000 + reg;
		}
		CHECK(sun60i_gpu_finalize(&gpu_device) == 0);
		CHECK(maps == 0 && peeks == 0 && unmaps == 0 && !sc.sc_have_id);
		CHECK(releases == 1 && clock_puts == 1);
		CHECK(!regulator.acquired && !clock_fixture.acquired);
		CHECK(strcmp(events, "ASVPCQcs") == 0);
		CHECK(sc.sc_core_hz == 111 && sc.sc_bus_hz == 222);
		snprintf(expected, sizeof(expected), "CCU observation: %s (error %d)",
		    failures[i].text, failures[i].error);
		CHECK(strstr(normal_output, expected) != NULL);
		CHECK(strstr(normal_output, "DCDC4 735000 uV, changed 0x000") != NULL);
		CHECK(strstr(normal_output, "hosc 24000000 / 24000000 Hz") != NULL);
		for (u_int reg = 0; reg < A733_GPU_NREGS; reg++) {
			snprintf(expected, sizeof(expected), "%s 0x%08x\n",
			    registers[reg], 0x10000000 + reg);
			CHECK(strstr(normal_output, expected) != NULL);
		}
		CHECK(strstr(normal_output, " -> ") == NULL);
		snprintf(expected, sizeof(expected), "clock/reset state: %d;",
		    failures[i].error);
		CHECK(strstr(error_output, expected) != NULL);
		CHECK(sun60i_gpu_finalize(&gpu_device) == 0);
		CHECK(strcmp(events, "ASVPCQcs") == 0);
	}
	reset();
	clock_observation.reason = A733_GPU_SNAPSHOT_CHANGED;
	clock_observation.readiness_error = EBUSY;
	clock_observation.changed = (1U << A733_GPU_NREGS) - 1;
	clock_observation.hosc_hz[1] = 19200000;
	for (u_int reg = 0; reg < A733_GPU_NREGS; reg++) {
		clock_observation.sample[0][reg] = 0xa5000000 + reg;
		clock_observation.sample[1][reg] = 0x5a000000 + reg;
	}
	unavailable(EBUSY);
	CHECK(strstr(normal_output, "changed 0x1fff") != NULL);
	CHECK(strstr(normal_output, "hosc 24000000 / 19200000 Hz") != NULL);
	for (u_int reg = 0; reg < A733_GPU_NREGS; reg++) {
		snprintf(expected, sizeof(expected), "%s 0x%08x -> 0x%08x\n",
		    registers[reg], 0xa5000000 + reg, 0x5a000000 + reg);
		CHECK(strstr(normal_output, expected) != NULL);
	}
	/* An acquisition error must not expose a nonexistent snapshot. */
	reset();
	ready_error = ENXIO;
	unavailable(ENXIO);
	CHECK(normal_output[0] == '\0' && strcmp(events, "ASVPCQcs") == 0);
	reset();
	clock_observation.reason = A733_GPU_CORE_PLL;
	clock_observation.readiness_error = ERANGE;
	unavailable(ERANGE);
}

static void
test_experimental_prepare(void)
{
	unsigned start = checks;
	const int errors[] = { EIO, EBUSY, EFAULT, ENXIO, ETIMEDOUT };

	reset(); prepare_len = 0; observe_len = 0;
	unavailable(EINVAL); CHECK(nevents == 0 && prepares == 0);
	for (int len = 1; len <= 4; len++) {
		reset(); prepare_len = len;
		unavailable(EINVAL); CHECK(nevents == 0);
	}
	reset(); prepare_len = 0;
	CHECK(sun60i_gpu_identify(&sc) == 0);
	CHECK(prepares == 1 && reserves == 2 && retains == 1 && waits == 2);
	CHECK(sc.sc_retained && peeks == 1 && unreserves == 0);
	CHECK(strcmp(events, "ASVPCQPCSVHWQKSVKFMIUcs") == 0);
	CHECK(strstr(normal_output, "initial CCU observation before preparation") != NULL);
	reset(); prepare_len = 0; initial_error = ENXIO;
	unavailable(ENXIO);
	CHECK(prepares == 0 && reserves == 0 && retains == 0 && waits == 0);
	CHECK(inspect_calls == 1 && normal_output[0] == '\0');
	for (u_int i = 0; i < __arraycount(errors); i++) {
		for (u_int stage = 0; stage < 5; stage++) {
			reset(); prepare_len = 0;
			switch (stage) {
			case 0: power_reserve_error = errors[i]; break;
			case 1: reserve_error = errors[i]; break;
			case 2: retain_error = errors[i]; break;
			case 3: prepare_error = errors[i]; break;
			case 4: wait_error = errors[i]; break;
			}
			unavailable(errors[i]);
			CHECK(sc.sc_retained == (stage >= 3));
			CHECK(prepares == (stage >= 3 ? 1 : 0));
			CHECK(unreserves == (stage == 1 ? 1 : stage == 2 ? 2 : 0));
		}
	}
	for (unsigned call = 2; call <= 3; call++) {
		reset(); prepare_len = 0; fail_supply_call = call;
		unavailable(EIO);
		CHECK(prepares == (call == 3 ? 1 : 0));
		CHECK(sc.sc_retained == (call == 3));
		CHECK(unreserves == (call == 2 ? 2 : 0));
	}
	reset(); prepare_len = 0; final_ready_error = EBUSY;
	unavailable(EBUSY); CHECK(sc.sc_retained && unreserves == 0);
	reset(); prepare_len = 0; second_wait_error = EIO;
	unavailable(EIO); CHECK(sc.sc_retained && unreserves == 0);
	/* Complete clock readiness must precede the first CORE observation. */
	reset(); prepare_len = 0;
	clock_observation.reason = A733_GPU_UPDATE_PENDING;
	clock_observation.readiness_error = EBUSY;
	unavailable(EBUSY);
	CHECK(waits == 0 && inspect_calls == 2 && sc.sc_retained && unreserves == 0);
	reset(); prepare_len = 0; ready_error = ENXIO;
	unavailable(ENXIO);
	CHECK(waits == 0 && sc.sc_retained && unreserves == 0);
	for (unsigned rate = 0; rate < 2; rate++) {
		reset(); prepare_len = 0;
		if (rate == 0) core_hz = 600000000; else bus_hz = 100000000;
		unavailable(EBUSY);
		CHECK(waits == 0 && sc.sc_retained && unreserves == 0);
	}
	/* A terminal snapshot is fresh, read-only, and preserves the timeout. */
	for (unsigned stage = 0; stage < 3; stage++) {
		reset(); prepare_len = 0;
		if (stage == 0) prepare_error = ETIMEDOUT;
		if (stage == 1) wait_error = ETIMEDOUT;
		if (stage == 2) second_wait_error = ETIMEDOUT;
		clock_observation.sample[0][A733_GPU_MODULE] = 0x83000000;
		clock_observation.sample[1][A733_GPU_MODULE] = 0x83000000;
		terminal_observation.sample[0][A733_GPU_MODULE] = 0x8b000000;
		terminal_observation.sample[1][A733_GPU_MODULE] = 0x8b000001;
		terminal_observation.changed = 1U << A733_GPU_MODULE;
		terminal_observation.reason = A733_GPU_SNAPSHOT_CHANGED;
		terminal_observation.readiness_error = EBUSY;
		initial_observation.sample[0][A733_GPU_PERIPH_GATE_EN] = 0x80020002;
		initial_observation.sample[1][A733_GPU_PERIPH_GATE_EN] = 0x80020002;
		initial_observation.periph_gates[0].configured = 2;
		initial_observation.periph_gates[0].no_auto = 2;
		initial_observation.periph_gates[1] = initial_observation.periph_gates[0];
		terminal_observation.sample[0][A733_GPU_PERIPH_GATE_STAT] = 0x00060000;
		terminal_observation.sample[1][A733_GPU_PERIPH_GATE_STAT] = 0x00020000;
		terminal_observation.periph_gates[0].effective = 6;
		terminal_observation.periph_gates[1].effective = 2;
		if (stage == 0) clock_observation = terminal_observation;
		unavailable(ETIMEDOUT);
		CHECK(waits == stage && inspect_calls == (stage == 0 ? 2 : 3));
		CHECK(sc.sc_retained && unreserves == 0 && prepares == 1);
		CHECK(strstr(normal_output, "terminal CCU observation after ") != NULL);
		CHECK(strstr(normal_output, "GPU_CLK[0xb20] 0x8b000000 -> 0x8b000001") != NULL);
		CHECK(strstr(normal_output, "PERI0_GATE_EN[0x1908] 0x80020002") != NULL);
		CHECK(strstr(normal_output, "PERI0_GATE_STAT[0x1988] 0x00060000 -> 0x00020000") != NULL);
		CHECK(strstr(normal_output, "configured 0x002, no-auto 0x002, effective 0x000") != NULL);
		CHECK(strstr(normal_output, "configured 0x000, no-auto 0x000, effective 0x006") != NULL);
		CHECK(strstr(normal_output, "diagnostic only") != NULL);
		CHECK(strcmp(sc.sc_stage, stage == 0 ? "experimental clock UPDATE completion" :
		    stage == 1 ? "experimental CORE ON/Q acceptance" :
		    "experimental final CORE check") == 0);
	}
	reset(); prepare_len = 0; wait_error = ETIMEDOUT; terminal_error = EIO;
	unavailable(ETIMEDOUT);
	CHECK(inspect_calls == 3 && waits == 1 && sc.sc_retained && unreserves == 0);
	CHECK(strstr(normal_output, "terminal CCU observation unavailable:") != NULL);
	CHECK(strstr(strstr(normal_output, "terminal CCU observation unavailable:"), "GPU_CLK[") == NULL);
	CHECK(strcmp(sc.sc_stage, "experimental CORE ON/Q acceptance") == 0);
	reset(); prepare_len = 0; map_error = ENOMEM;
	CHECK(sun60i_gpu_identify(&sc) == ENOMEM && sc.sc_retained && unreserves == 0);
	reset(); prepare_len = 0; peek_error = 1;
	CHECK(sun60i_gpu_identify(&sc) == EFAULT && sc.sc_retained && unreserves == 0);
	reset(); prepare_len = 0; pbvnc = 0;
	CHECK(sun60i_gpu_identify(&sc) == ENODEV && sc.sc_retained && unreserves == 0);
	reset(); prepare_len = 0; prepare_error = EIO;
	CHECK(sun60i_gpu_finalize(&gpu_device) == 0);
	CHECK(strstr(error_output, "retained until reboot") != NULL);
	CHECK(strstr(error_output, "firmware state left unchanged") == NULL);
	CHECK(sun60i_gpu_finalize(&gpu_device) == 0 && prepares == 1);
	reset(); observe_len = 0;
	CHECK(sun60i_gpu_identify(&sc) == 0);
	CHECK(prepares == 0 && reserves == 0 && retains == 0 && waits == 0 && peeks == 0);
	printf("PASS: %u experimental GPU consumer checks\n", checks - start);
}

static void
test_domain_request(void)
{
	unsigned start = checks;
	const int errors[] = { EIO, EBUSY, EFAULT, ENXIO, ETIMEDOUT };
	char expected[192];

	/* The fresh request requires the prepared-clock opt-in. */
	reset(); request_len = 0;
	unavailable(EINVAL); CHECK(nevents == 0 && requests == 0);
	reset(); request_len = 0; observe_len = 0;
	unavailable(EINVAL); CHECK(nevents == 0 && requests == 0);
	for (int len = 1; len <= 4; len++) {
		reset(); request_len = len; prepare_len = 0;
		unavailable(EINVAL); CHECK(nevents == 0);
	}

	/* Full success keeps the request between clock preparation and wait. */
	reset(); prepare_len = request_len = 0;
	CHECK(sun60i_gpu_identify(&sc) == 0);
	CHECK(requests == 1 && prepares == 1 && waits == 2 && retains == 1);
	CHECK(strcmp(events, "ASVPCQPCSVHWQRKSVKFMIUcs") == 0);
	CHECK(strstr(normal_output, "TOP-only") == NULL);

	/* A failed request stops before the waiter and reads identification. */
	for (u_int i = 0; i < __arraycount(errors); i++) {
		reset(); prepare_len = request_len = 0; request_error = errors[i];
		CHECK(sun60i_gpu_identify(&sc) == errors[i]);
		CHECK(requests == 1 && waits == 0 && sc.sc_retained);
		CHECK(sc.sc_have_id && sc.sc_bvnc == pbvnc);
		CHECK(maps == 1 && peeks == 1 && unmaps == 1);
		CHECK(!regulator.acquired && !clock_fixture.acquired);
		snprintf(expected, sizeof(expected),
		    "TOP-only identification: CORE_ID 0x00000000, PBVNC "
		    "0x00240038006800b7: 36.56.104.183");
		CHECK(strstr(normal_output, expected) != NULL);
		CHECK(strstr(normal_output,
		    "(expected A733 GPU; GPU_CORE not confirmed ON)") != NULL);
		CHECK(strcmp(sc.sc_stage, "experimental domain request") == 0);
	}

	/* A failed wait still reaches the same bounded identification. */
	reset(); prepare_len = request_len = 0; wait_error = ETIMEDOUT;
	CHECK(sun60i_gpu_identify(&sc) == ETIMEDOUT);
	CHECK(requests == 1 && waits == 1 && sc.sc_have_id);
	CHECK(strstr(events, "RKQ") != NULL && strstr(events, "SVPFMiIUcs") != NULL);
	CHECK(strstr(normal_output, "terminal CCU observation") != NULL);
	reset(); prepare_len = request_len = 0; second_wait_error = EIO;
	CHECK(sun60i_gpu_identify(&sc) == EIO);
	CHECK(requests == 1 && waits == 2 && sc.sc_have_id);
	CHECK(strcmp(sc.sc_stage, "experimental final CORE check") == 0);

	/* Readiness changes skip the TOP-only access instead of forcing it. */
	reset(); prepare_len = request_len = 0; request_error = EIO;
	final_ready_error = ENXIO;
	CHECK(sun60i_gpu_identify(&sc) == EIO);
	CHECK(maps == 0 && peeks == 0 && !sc.sc_have_id);
	CHECK(strstr(normal_output, "TOP-only identification skipped:") != NULL);
	reset(); prepare_len = request_len = 0; request_error = EIO;
	supply_on = false;
	CHECK(sun60i_gpu_identify(&sc) == EBUSY);
	CHECK(maps == 0 && requests == 0);
	reset(); prepare_len = request_len = 0; request_error = EIO; voltage = 735000;
	CHECK(sun60i_gpu_identify(&sc) == EOPNOTSUPP);
	CHECK(maps == 0 && requests == 0 && !sc.sc_have_id);
	reset(); prepare_len = request_len = 0; request_error = EIO;
	power_on = false;
	CHECK(sun60i_gpu_identify(&sc) == EBUSY);
	CHECK(maps == 0 && requests == 0 && !sc.sc_have_id);
	reset(); prepare_len = request_len = 0; request_error = EIO;
	core_hz = 600000000;
	CHECK(sun60i_gpu_identify(&sc) == EBUSY);
	CHECK(maps == 0 && requests == 0 && !sc.sc_have_id);

	/* Mapping and read faults stay bounded diagnostics. */
	reset(); prepare_len = request_len = 0; request_error = EIO;
	map_error = ENOMEM;
	CHECK(sun60i_gpu_identify(&sc) == EIO);
	CHECK(maps == 1 && peeks == 0 && !sc.sc_have_id);
	CHECK(strstr(normal_output, "TOP-only identification unavailable:") != NULL);
	reset(); prepare_len = request_len = 0; request_error = EIO;
	peek_error = 1;
	CHECK(sun60i_gpu_identify(&sc) == EIO);
	CHECK(maps == 1 && peeks == 1 && unmaps == 1 && !sc.sc_have_id);
	CHECK(strstr(normal_output, "TOP-only PBVNC read failed") != NULL);
	reset(); prepare_len = request_len = 0; request_error = EIO;
	coreid_peek_error = 1; core_id = 0x10300;
	CHECK(sun60i_gpu_identify(&sc) == EIO);
	CHECK(sc.sc_have_id && sc.sc_bvnc == pbvnc);
	CHECK(strstr(normal_output, "TOP-only CORE_ID read failed") != NULL);
	CHECK(strstr(normal_output, "CORE_ID 0x00000000") == NULL);
	reset(); prepare_len = request_len = 0; request_error = EIO;
	pbvnc = UINT64_C(0x00240037006800b7);
	CHECK(sun60i_gpu_identify(&sc) == EIO);
	CHECK(sc.sc_have_id && sc.sc_bvnc == pbvnc);
	CHECK(strstr(normal_output, "36.55.104.183 (unexpected") != NULL);
	/* Without the opt-in no fresh request and no TOP-only access exist. */
	reset(); prepare_len = 0; wait_error = ETIMEDOUT;
	CHECK(sun60i_gpu_identify(&sc) == ETIMEDOUT);
	CHECK(requests == 0 && maps == 0 && !sc.sc_have_id);
	printf("PASS: %u experimental CORE request consumer checks\n",
	    checks - start);
}

static void
test_runtime_stages(void)
{
	unsigned start = checks;
	const char * const seq[] = {
	    NULL,
	    "ASVs",		/* 1 supply */
	    "P",		/* 2 GPU_TOP */
	    "P",		/* 3 PCK reserve */
	    "CCc",		/* 4 CCU reserve */
	    "H",		/* 5 retain */
	    "CWc",		/* 6 prepare */
	    "CQc",		/* 7 inspect */
	    "ASVsPR",		/* 8 request */
	    "K",		/* 9 wait */
	    "ASVsPACSVPFMiIUcs",	/* 10 TOP-only identify */
	    "ASVsPMIU",		/* 11 strict identify */
	};
	u_int stage;

	/* The dry run passes without touching a single provider. */
	reset();
	sun60i_gpu_run_stage(&sc, 0);
	CHECK(sc.sc_run_result == 0 && sc.sc_run_stage == 0);
	sun60i_gpu_run_stage(&sc, 2);
	CHECK(sc.sc_run_result == EINVAL && sc.sc_run_stage == 0);
	CHECK(nevents == 0 && prepares == 0 && requests == 0);

	/* Ordered single-call stages with the exact provider sequences. */
	reset();
	sun60i_gpu_run_stage(&sc, 1);
	CHECK(sc.sc_run_result == 0);
	CHECK(strcmp(events, seq[1]) == 0);
	for (stage = 2; stage <= 8; stage++) {
		nevents = 0;
		sun60i_gpu_run_stage(&sc, stage);
		CHECK(sc.sc_run_stage == stage);
		if (stage == 9 - 1)
			continue;
		CHECK(sc.sc_run_result == 0);
		CHECK(strcmp(events, seq[stage]) == 0);
	}
	CHECK(prepares == 1 && reserves == 2 && retains == 1 && requests == 1);
	nevents = 0;
	sun60i_gpu_run_stage(&sc, 9);
	CHECK(sc.sc_run_result == 0 && waits == 1 && sc.sc_run_core_on);
	CHECK(strcmp(events, seq[9]) == 0);
	nevents = 0;
	sun60i_gpu_run_stage(&sc, 10);
	CHECK(sc.sc_run_result == 0 && sc.sc_have_id && sc.sc_bvnc == pbvnc);
	CHECK(strcmp(events, seq[10]) == 0);
	nevents = 0;
	sun60i_gpu_run_stage(&sc, 11);
	CHECK(sc.sc_run_result == 0);
	CHECK(strcmp(events, seq[11]) == 0);
	nevents = 0;
	sun60i_gpu_run_stage(&sc, 12);
	CHECK(sc.sc_run_result == EINVAL && nevents == 0);

	/* A wait timeout keeps the sequence; identification still runs. */
	reset();
	for (stage = 1; stage <= 8; stage++)
		sun60i_gpu_run_stage(&sc, stage);
	wait_error = ETIMEDOUT;
	sun60i_gpu_run_stage(&sc, 9);
	CHECK(sc.sc_run_result == ETIMEDOUT && !sc.sc_run_core_on);
	sun60i_gpu_run_stage(&sc, 10);
	CHECK(sc.sc_run_result == 0 && sc.sc_have_id);
	sun60i_gpu_run_stage(&sc, 11);
	CHECK(sc.sc_run_result == EBUSY && maps == 1);

	/* A supply failure at stage 1 blocks the rest until reboot. */
	reset(); supply_on = false;
	sun60i_gpu_run_stage(&sc, 1);
	CHECK(sc.sc_run_result == EBUSY && sc.sc_run_stage == 1);
	sun60i_gpu_run_stage(&sc, 2);
	CHECK(sc.sc_run_result == EINVAL);
	printf("PASS: %u runtime GPU stage checks\n", checks - start);
}

int
main(void)
{
	const struct fdt_attach_args faa = { .faa_phandle = 1 };
	const int failures[] = { ENXIO, EIO, ETIMEDOUT, EOPNOTSUPP, EINVAL };
	const int bad_lengths[] = { -1, 0, 3, 4, 7, 12 };
	const u_int bad_rates[] = { 0, 300000000, 401000000, 800000000 };
	const u_int bad_volts[] = { 0, 500000, 799999, 810000 };
	const uint64_t ids[] = { 0, UINT64_MAX, UINT64_C(0x00240037006800b7),
	    UINT64_C(0x00230038006800b7), UINT64_C(0x00240038006700b7),
	    UINT64_C(0x00240038006800b6) };

	reset();
	/* /soc finished before a later root FDT pass attaches its GPU child. */
	config_process_deferred(&deferred_config_queue, &soc_device);
	config_defer(&gpu_device, legacy_probe);
	config_process_deferred(&deferred_config_queue, &gpu_device);
	config_process_deferred(&deferred_config_queue, &root_device);
	CHECK(legacy_callbacks == 0 && gpu_device.dv_pending == 1);
	CHECK(!TAILQ_EMPTY(&config_pending));
	/* An artificial second /soc drain is needed to clean the old queue. */
	config_process_deferred(&deferred_config_queue, &soc_device);
	CHECK(legacy_callbacks == 1 && TAILQ_EMPTY(&config_pending));
	reset();
	CHECK(sun60i_gpu_match(NULL, NULL, (void *)&faa) == 1);
	match_result = 0;
	CHECK(sun60i_gpu_match(NULL, NULL, (void *)&faa) == 0);
	CHECK(nevents == 0);
	missing_supply = true;
	config_process_deferred(&deferred_config_queue, &soc_device);
	sun60i_gpu_attach(&soc_device, &gpu_device, (void *)&faa);
	config_process_deferred(&deferred_config_queue, &gpu_device);
	config_process_deferred(&deferred_config_queue, &root_device);
	CHECK(!TAILQ_EMPTY(&config_finalize_list) && nevents == 0);
	CHECK(TAILQ_EMPTY(&config_pending));
	/* Remaining FDT passes attach the providers before finalization. */
	missing_supply = false;
	CHECK(config_finalize_register(&root_device, request_extra_pass) == 0);
	config_finalize();
	CHECK(config_finalize_done && extra_passes == 2);
	CHECK(TAILQ_EMPTY(&config_finalize_list) && TAILQ_EMPTY(&config_pending));
	CHECK(strcmp(events, "ASVPCQMIUcs") == 0);
	CHECK(maps == 1 && peeks == 1 && unmaps == 1 &&
	    clock_puts == 1 && releases == 1);
	CHECK(strstr(normal_output, "0x00240038006800b7: 36.56.104.183") != NULL);
	CHECK(strstr(normal_output, "expected A733 GPU") != NULL);
	CHECK(error_output[0] == '\0');
	/* Late registration invokes the real hook immediately, still only once. */
	CHECK(config_finalize_register(&gpu_device, sun60i_gpu_finalize) == 0);
	CHECK(peeks == 1);
	reset();
	missing_supply = true;
	sun60i_gpu_attach(&soc_device, &gpu_device, (void *)&faa);
	config_finalize();
	CHECK(config_finalize_done && TAILQ_EMPTY(&config_pending));
	CHECK(TAILQ_EMPTY(&config_finalize_list));
	CHECK(maps == 0 && peeks == 0);
	CHECK(strstr(error_output, "supply provider") != NULL);
	reset();
	core_hz = 600000000;
	CHECK(sun60i_gpu_identify(&sc) == 0 && sc.sc_have_id && sc.sc_bvnc == pbvnc);

	for (u_int i = 0; i < __arraycount(bad_lengths); i++) {
		reset();
		pd_len = bad_lengths[i];
		unavailable(EINVAL);
		CHECK(nevents == 0);
		reset();
		clock_len = bad_lengths[i];
		unavailable(EINVAL);
		CHECK(nevents == 0);
	}
	for (int i = 0; i < 13; i++) {
		reset();
		switch (i) {
		case 0: managed_len = -1; break;
		case 1: managed_len = 4; break;
		case 2: binding_addr += 0x1000; break;
		case 3: binding_size = 0x27; break;
		case 4: pd[1] = swap32(6); break;
		case 5: power_cells = 0; break;
		case 6: bad_power_compat = true; break;
		case 7: clocks[1] = swap32(A733_CLK_NPU); break;
		case 8: clock_cells = 0; break;
		case 9: supply_len = 8; break;
		case 10: bad_supply = true; break;
		case 11: bad_pmic = true; break;
		case 12: clocks[0] = swap32(99); break;
		}
		unavailable(EINVAL);
		CHECK(nevents == 0);
	}
	reset(); reg_error = ENOENT; unavailable(ENOENT);
	reset(); missing_supply = true; unavailable(ENXIO);
	CHECK(releases == 0 && clock_puts == 0);
	reset(); missing_clock = true; unavailable(ENXIO);
	CHECK(releases == 1 && clock_puts == 0);
	reset(); supply_on = false; unavailable(EBUSY);
	CHECK(strcmp(events, "ASs") == 0);
	reset(); power_on = false; unavailable(EBUSY);
	CHECK(strcmp(events, "ASVPs") == 0);

	for (u_int i = 0; i < __arraycount(failures); i++) {
		for (u_int stage = 0; stage < 4; stage++) {
			reset();
			int *errors[] = { &state_error, &voltage_error,
			    &power_error, &ready_error };
			*errors[stage] = failures[i];
			unavailable(failures[i]);
			CHECK(releases == 1 && clock_puts == (stage == 3 ? 1U : 0U));
			*errors[stage] = 0;
			CHECK(sun60i_gpu_identify(&sc) == 0);
			CHECK(peeks == 1 && unmaps == 1 && releases == 2);
		}
	}
	for (u_int i = 0; i < __arraycount(bad_rates); i++) {
		reset(); core_hz = bad_rates[i]; unavailable(EOPNOTSUPP);
		CHECK(clock_puts == 1 && releases == 1);
		reset(); voltage = bad_volts[i]; unavailable(EOPNOTSUPP);
		CHECK(clock_puts == 1 && releases == 1);
	}
	reset(); map_error = ENOMEM;
	CHECK(sun60i_gpu_identify(&sc) == ENOMEM);
	CHECK(maps == 1 && peeks == 0 && unmaps == 0 &&
	    clock_puts == 1 && releases == 1);
	reset(); peek_error = -1;
	CHECK(sun60i_gpu_finalize(&gpu_device) == 0);
	CHECK(!sc.sc_have_id && normal_output[0] == '\0');
	CHECK(maps == 1 && peeks == 1 && unmaps == 1 &&
	    clock_puts == 1 && releases == 1);
	CHECK(strstr(error_output, "PBVNC read") != NULL);
	for (u_int i = 0; i < __arraycount(ids); i++) {
		reset(); pbvnc = ids[i];
		CHECK(sun60i_gpu_identify(&sc) == ENODEV);
		CHECK(sc.sc_have_id && sc.sc_bvnc == pbvnc);
		CHECK(maps == 1 && peeks == 1 && unmaps == 1 &&
		    clock_puts == 1 && releases == 1);
		reset(); pbvnc = ids[i];
		CHECK(sun60i_gpu_finalize(&gpu_device) == 0);
		CHECK(strstr(normal_output, "(unexpected)") != NULL);
		CHECK(strstr(normal_output, "(expected A733 GPU)") == NULL);
		CHECK(strstr(error_output, "PBVNC value") != NULL);
	}
	/* Diagnostic binding must block MMIO even with completely ready inputs. */
	for (u_int ready = 0; ready < 2; ready++) {
		reset(); observe_len = 0;
		clock_observation.dcxo_hz[0] = clock_observation.dcxo_hz[1] = 26000000;
		clock_observation.dcxo_sample[0][0] = clock_observation.dcxo_sample[0][1] = 0x8003;
		clock_observation.dcxo_sample[1][0] = clock_observation.dcxo_sample[1][1] = 0x8003;
		if (!ready) {
			clock_observation.reason = A733_GPU_MODULE_GATED;
			clock_observation.readiness_error = EBUSY;
		}
		sun60i_gpu_attach(&soc_device, &gpu_device, (void *)&faa);
		config_finalize();
		CHECK(maps == 0 && peeks == 0 && unmaps == 0 && !sc.sc_have_id);
		CHECK(strcmp(events, "ASVPCQcs") == 0);
		CHECK(strstr(normal_output, "observation only; GPU registers not mapped or read") != NULL);
		CHECK(strstr(normal_output, "DCXO before CCU: 0x00008003 / 0x00008003, 26000000 Hz") != NULL);
		CHECK(strstr(normal_output, "DCXO after CCU: 0x00008003 / 0x00008003, 26000000 Hz") != NULL);
		CHECK(strstr(normal_output, "PBVNC") == NULL);
		CHECK(sun60i_gpu_finalize(&gpu_device) == 0 && maps == 0);
	}
	for (int len = 1; len < 9; len++) {
		reset(); observe_len = len; unavailable(EINVAL); CHECK(nevents == 0);
	}
	test_clock_observation();

	test_experimental_prepare();
	test_domain_request();
	test_runtime_stages();
	printf("PASS: %u A733 GPU identification checks\n", checks);
	return 0;
}
