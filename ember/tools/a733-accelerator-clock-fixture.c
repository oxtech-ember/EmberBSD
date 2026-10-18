/* Origin: EmberBSD; AI-assisted production A733 CCU regression fixture. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>

typedef unsigned int u_int;
typedef struct { bool held; } kmutex_t;
#define MUTEX_DEFAULT 0
#define IPL_NONE 0
static void mutex_init(kmutex_t *m, int type, int level) { m->held = false; }
static void mutex_enter(kmutex_t *m) { assert(!m->held); m->held = true; }
static void mutex_exit(kmutex_t *m) { assert(m->held); m->held = false; }

typedef void *device_t;
typedef void *cfdata_t;
typedef void *bus_space_tag_t;
typedef unsigned int bus_space_handle_t;
typedef size_t bus_size_t;
typedef uint64_t bus_addr_t;
#ifndef __BIT
#define __BIT(n) (UINT32_C(1) << (n))
#endif
#ifndef __BITS
#define __BITS(h, l) ((UINT32_MAX >> (31 - (h))) & (UINT32_MAX << (l)))
#endif
#ifndef __SHIFTOUT
#define __SHIFTOUT(v, m) (((v) & (m)) >> __builtin_ctz(m))
#endif
#ifndef __SHIFTIN
#define __SHIFTIN(v, m) (((v) << __builtin_ctz(m)) & (m))
#endif
#ifndef __SHIFTOUT_MASK
#define __SHIFTOUT_MASK(m) __SHIFTOUT(m, m)
#endif
#ifndef __arraycount
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#endif
#define howmany(x, y) (((x) + ((y) - 1)) / (y))
#define KASSERT(c) assert(c)
#define BUS_SPACE_BARRIER_READ 1
#define BUS_SPACE_BARRIER_WRITE 2
#define CLK_SET_RATE_PARENT 1
#define CFATTACH_DECL_NEW(n, s, m, a, d, r)
#define DEVICE_COMPAT_EOL { .compat = NULL }
#define device_private(d) (d)
#define aprint_error(...) ((void)0)
#define aprint_error_dev(...) ((void)0)
#define aprint_normal_dev(...) ((void)0)
#define aprint_normal(...) ((void)0)
#define aprint_naive(...) ((void)0)
static void delay(unsigned);
struct clk_domain { void *priv; };
struct clk { struct clk_domain *domain; const char *name; u_int flags; };
struct fdt_attach_args { int faa_phandle; bus_space_tag_t faa_bst; };
struct device_compatible_entry { const char *compat; };
#ifndef NSUNXI_RTC
#define NSUNXI_RTC 1
#endif
#include "sunxi_rtcvar.h"
#include "sunxi_ccu.h"
#include "sun60i_a733_ccu.h"
#include "sun60i_a733_display.h"
#include "bindings.h"

static uint32_t registers[0x2000 / 4], last_write;
static bus_size_t ccu_size, read_regs[64];
static int ccu_reg_error;
static unsigned reject_write_number;
static bus_size_t write_regs[64];
static uint32_t write_vals[64];
static unsigned writes, barriers, checks, reads, fail_peek;
static bool update_model, pll_model;
static unsigned pll_delay_us, pll_lock_after_us, pll_lock_reads;
static bool pll_glitch, pll_settle_loss, pll_output_loss, pll_gate_change;
static bool pll_factor_change, pll_settled;
static unsigned update_clear_us, update_delay_us, update_reads;
static unsigned unstable_read, hosc_reads, parent_queries;
static uint32_t unstable_mask;
static bus_size_t unstable_reg;
static u_int hosc_rate = 24000000;
static unsigned dcxo_queries;
static bool prepare_test;
static int dcxo_len = 4, dcxo_node = 7, dcxo_error, dcxo_fail_query;
static bool bad_dcxo, unstable_dcxo;
static u_int dcxo_rate = 24000000;
static uint32_t dcxo_raw;
static int
fdtbus_get_reg(int node, int index, bus_addr_t *addr, bus_size_t *size)
{
	assert(index == 0);
	*addr = 0x02002000;
	*size = ccu_size;
	return ccu_reg_error;
}
static int OF_getproplen(int node, const char *name) { return dcxo_len; }
static int fdtbus_get_phandle(int node, const char *name) { return dcxo_node; }
int
sunxi_rtc_dcxo_query(int node, struct sunxi_rtc_dcxo_state *out)
{
	assert(node == 7);
	if (!prepare_test)
		assert(dcxo_queries == 0 ? reads == 0 : reads == 26);
	dcxo_queries++;
	if (dcxo_error != 0 && (dcxo_fail_query == 0 ||
	    dcxo_queries == (unsigned)dcxo_fail_query))
		return dcxo_error;
	out->sample[0] = dcxo_raw ^ (unstable_dcxo && dcxo_queries == 2 ? 1 : 0);
	out->sample[1] = out->sample[0];
	out->rate_hz = dcxo_rate;
	return 0;
}

static bool unstable_hosc, unstable_all;
static bool reject_write, missing_hosc;
static struct sunxi_ccu_softc state;
static struct clk hosc = { .name = "hosc" };
static struct clk *clk_get_parent(struct clk *);
static u_int clk_get_rate(struct clk *);
static int clk_set_rate(struct clk *, u_int);
static int clk_enable(struct clk *);
static u_int clk_round_rate(struct clk *, u_int);
static struct clk *fdtbus_clock_get(int node, const char *name)
{
	return !missing_hosc && strcmp(name, "hosc") == 0 ? &hosc : NULL;
}
static uint32_t
bus_space_read_4(bus_space_tag_t tag, bus_space_handle_t handle, bus_size_t off)
{
	assert(off < sizeof(registers) && off % 4 == 0);
	assert(off + sizeof(uint32_t) <= ccu_size);
	if (reads < __arraycount(read_regs))
		read_regs[reads] = off;
	reads++;
	if (pll_model && off == 0xe0 &&
	    (registers[off / 4] & 0xe0000000) == 0xe0000000) {
		pll_lock_reads++;
		bool locked = pll_delay_us >= pll_lock_after_us;
		if ((pll_glitch && pll_delay_us == pll_lock_after_us + 3) ||
		    (pll_settle_loss && pll_settled) ||
		    (pll_output_loss && (registers[off / 4] & __BIT(27)) != 0))
			locked = false;
		if (locked)
			registers[off / 4] |= __BIT(28);
		else
			registers[off / 4] &= ~__BIT(28);
	}
	if (update_model && off == 0xb20 &&
	    (registers[off / 4] & __BIT(31)) != 0) {
		update_reads++;
		if (update_delay_us >= update_clear_us)
			registers[off / 4] &= ~__BIT(27);
	}
	if ((reads == unstable_read && off == unstable_reg) ||
	    (unstable_all && reads > 13 && reads <= 26))
		registers[off / 4] ^= unstable_mask;
	return registers[off / 4];
}
static int
bus_space_peek_4(bus_space_tag_t tag, bus_space_handle_t handle,
    bus_size_t off, uint32_t *value)
{
	*value = bus_space_read_4(tag, handle, off);
	return reads == fail_peek ? 1 : 0;
}
static void
bus_space_write_4(bus_space_tag_t tag, bus_space_handle_t handle,
    bus_size_t off, uint32_t val)
{
	assert(off < sizeof(registers) && off % 4 == 0);
	if (writes < __arraycount(write_regs)) {
		write_regs[writes] = off; write_vals[writes] = val;
	}
	writes++;
	last_write = val;
	if (!reject_write && writes != reject_write_number) {
		/* PLL LOCK is read-only. Production writes cannot manufacture it. */
		if (pll_model && off == 0xe0)
			val = (val & ~__BIT(28)) | (registers[off / 4] & __BIT(28));
		registers[off / 4] = off == 0xb20 && !update_model ?
		    val & ~__BIT(27) : val;
		if (pll_model && pll_factor_change && off == 0xb20 &&
		    (val & __BIT(31)) != 0)
			registers[0xe0 / 4] ^= __BIT(8);
	}
}
static void
delay(unsigned us)
{
	if (pll_model && (registers[0xb20 / 4] & __BIT(31)) == 0) {
		assert(us == 3 || us == 20);
		assert((registers[0xe0 / 4] & __BIT(27)) == 0);
		pll_delay_us += us;
		if (us == 20) {
			pll_settled = true;
			if (pll_gate_change)
				registers[0x1914 / 4] ^= __BIT(0);
		}
		return;
	}
	if (!update_model)
		return;
	/* UPDATE cannot finish until the last existing local write. */
	assert((pll_model || writes == 4) &&
	    (registers[0xb20 / 4] & __BIT(31)) != 0);
	assert((registers[0xb24 / 4] & 0x10001) == 0x10001);
	assert(us == 10 && update_delay_us < 10000);
	update_delay_us += us;
}
static void
bus_space_barrier(bus_space_tag_t tag, bus_space_handle_t handle,
    bus_size_t off, bus_size_t len, int flags)
{
	assert(len == 4 && flags == 3);
	barriers++;
}
static int
of_compatible_match(int node, const struct device_compatible_entry *compat)
{
	return strcmp(compat[0].compat, "allwinner,sun60i-a733-ccu") == 0 ||
	    (!bad_dcxo && strcmp(compat[0].compat, "allwinner,sun60i-a733-rtc") == 0);
}
int
sunxi_ccu_attach(struct sunxi_ccu_softc *sc)
{
	sc->sc_clkdom.priv = sc;
	for (u_int i = 0; i < sc->sc_nclks; i++)
		sc->sc_clks[i].base.domain = &sc->sc_clkdom;
	return 0;
}
void sunxi_ccu_print(struct sunxi_ccu_softc *sc) {}
struct sunxi_ccu_clk *
sunxi_ccu_clock_find(struct sunxi_ccu_softc *sc, const char *name)
{
	for (u_int i = 0; i < sc->sc_nclks; i++)
		if (sc->sc_clks[i].base.name != NULL &&
		    strcmp(sc->sc_clks[i].base.name, name) == 0)
			return &sc->sc_clks[i];
	return NULL;
}
#include "dispatch.h"
static struct clk *clk_get_parent(struct clk *clk)
{
	parent_queries++;
	return clk == &hosc ? NULL : sunxi_ccu_clock_get_parent(&state, clk);
}
static u_int clk_get_rate(struct clk *clk)
{
	if (clk == &hosc) {
		hosc_reads++;
		return unstable_hosc && hosc_reads > 1 ? 0 : hosc_rate;
	}
	return sunxi_ccu_clock_get_rate(&state, clk);
}
static int clk_set_rate(struct clk *clk, u_int rate)
{
	return sunxi_ccu_clock_set_rate(&state, clk, rate);
}
static int clk_enable(struct clk *clk)
{
	return clk == &hosc ? 0 : sunxi_ccu_clock_enable(&state, clk);
}
static u_int clk_round_rate(struct clk *clk, u_int rate)
{
	return sunxi_ccu_clock_round_rate(&state, clk, rate);
}
static int clk_set_parent(struct clk *clk, struct clk *parent)
{
	return sunxi_ccu_clock_set_parent(&state, clk, parent);
}
#include "sunxi_ccu_gate.c"
#include "sunxi_ccu_div.c"
/* Unrelated upstream NM code uses abs() on an unsigned subtraction. */
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wabsolute-value"
#elif defined(__GNUC__) && __GNUC__ >= 16
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wabsolute-value"
#endif
#include "sunxi_ccu_nm.c"
#ifdef __clang__
#pragma clang diagnostic pop
#elif defined(__GNUC__) && __GNUC__ >= 16
#pragma GCC diagnostic pop
#endif
#include "sunxi_ccu_nkmp.c"
#include "sunxi_ccu_fixed_factor.c"
#include "sun60i_a733_display.c"
#include "sun60i_a733_ccu.c"
#define CHECK(c) do { assert(c); checks++; } while (0)
#define REG(r) registers[(r) / 4]
#define CLOCK(id) (&state.sc_clks[(id)].base)

static void
reset(void)
{
	struct fdt_attach_args args = { 0 };
	memset(registers, 0, sizeof(registers));
	memset(&sun60i_gpu_lease, 0, sizeof(sun60i_gpu_lease));
	writes = barriers = reads = hosc_reads = dcxo_queries = fail_peek = 0;
	update_model = pll_model = false;
	pll_delay_us = pll_lock_after_us = pll_lock_reads = 0;
	pll_glitch = pll_settle_loss = pll_output_loss = pll_gate_change = false;
	pll_factor_change = pll_settled = false;
	update_clear_us = update_delay_us = update_reads = 0;
	reject_write_number = 0;
	prepare_test = false;
	ccu_size = sizeof(registers); ccu_reg_error = 0;
	unstable_read = 0;
	unstable_mask = 1;
	unstable_reg = SIZE_MAX;
	unstable_hosc = unstable_all = unstable_dcxo = bad_dcxo = false;
	dcxo_len = 4; dcxo_node = 7; dcxo_error = dcxo_fail_query = 0;
	dcxo_rate = 24000000; dcxo_raw = 0;
	hosc_rate = 24000000;
	reject_write = missing_hosc = false;
	sun60i_a733_ccu_attach(NULL, &state, &args);
	CHECK(writes == 0 && reads == 30);
	for (u_int r = 0; r < 30; r++)
		CHECK(read_regs[r] == sun60i_a733_display_clock_regs[r % 15].offset);
	reads = 0;
	/* Firmware peripheral VCO=2400 MHz, 2x=/2, 800M=/3, 480M=/5. */
	REG(PLL_PERIPH0_CTRL_REG) = __BIT(31) | (99 << 8) |
	    (1 << 20) | (2 << 16) | (4 << 2);
}

static void
ready_fixture(void)
{
	reset();
	REG(PLL_REF_CTRL_REG) = __BITS(31,27) | (99 << 8) | (99 << 16);
	REG(PLL_PERIPH0_CTRL_REG) |= __BITS(30,25);
	REG(PLL_GPU0_CTRL_REG) = __BITS(31,27) | (99 << 8) | (1 << 20);
	REG(GPU0_CLK_REG) = ACCEL_CLK_ENABLE;
	REG(GPU0_BGR_REG) = __BIT(0) | __BIT(16);
	REG(AHB_CFG_REG) = 3 << 24;
	REG(AHB_MASTER_GATE_REG) = __BIT(7);
}

static void
expect_ready(int error, u_int core_expected, u_int bus_expected)
{
	struct sun60i_a733_gpu_state observation, saved_observation;
	uint32_t saved[__arraycount(registers)];
	u_int core = 111, bus = 222;
	int inspect_error;

	memcpy(saved, registers, sizeof(saved));
	reads = hosc_reads = dcxo_queries = 0;
	CHECK(sun60i_a733_ccu_gpu_ready(CLOCK(A733_CLK_GPU0), &core, &bus) == error);
	CHECK(core == (error == 0 ? core_expected : 111));
	CHECK(bus == (error == 0 ? bus_expected : 222));
	CHECK(writes == 0 && barriers == 0 && reads <= 26);
	if (unstable_read == 0)
		CHECK(memcmp(saved, registers, sizeof(saved)) == 0);
	/* Inspect the same inputs, independently of the compatibility wrapper. */
	memcpy(registers, saved, sizeof(registers));
	memset(&observation, 0xa5, sizeof(observation));
	memcpy(&saved_observation, &observation, sizeof(saved_observation));
	reads = hosc_reads = dcxo_queries = 0;
	inspect_error = sun60i_a733_ccu_gpu_inspect(CLOCK(A733_CLK_GPU0),
	    &observation);
	if (inspect_error != 0) {
		CHECK(inspect_error == error);
		CHECK(memcmp(&observation, &saved_observation,
		    sizeof(observation)) == 0);
	} else {
		CHECK(inspect_error == 0 && observation.readiness_error == error);
		CHECK(reads == 26 && hosc_reads == 2);
		CHECK(observation.core_hz == (error == 0 ? core_expected : 0));
		CHECK(observation.bus_hz == (error == 0 ? bus_expected : 0));
		CHECK((observation.reason == A733_GPU_READY) == (error == 0));
	}
	CHECK(writes == 0 && barriers == 0);
	if (unstable_read == 0)
		CHECK(memcmp(saved, registers, sizeof(saved)) == 0);
}

static void
expect_inspect(enum sun60i_a733_gpu_reason reason, int error)
{
	static const bus_size_t regs[] = { 0, 0xa0, 0xa8, 0xac, 0xe0,
	    0xe8, 0xec, 0xb20, 0xb24, 0x500, 0x5c0, 0x1908, 0x1988 };
	struct sun60i_a733_gpu_state observation;
	uint32_t first[A733_GPU_NREGS], changed = 0;

	for (u_int i = 0; i < __arraycount(regs); i++)
		first[i] = REG(regs[i]);
	reads = hosc_reads = dcxo_queries = 0;
	CHECK(sun60i_a733_ccu_gpu_inspect(CLOCK(A733_CLK_GPU0),
	    &observation) == 0);
	CHECK(reads == 26 && hosc_reads == 2);
	CHECK(writes == 0 && barriers == 0);
	CHECK(observation.reason == reason);
	CHECK(observation.readiness_error == error);
	CHECK(observation.hosc_hz[0] == 24000000);
	CHECK(observation.hosc_hz[1] == (unstable_hosc ? 0 : 24000000));
	CHECK(error == 0 || (observation.core_hz == 0 && observation.bus_hz == 0));
	for (u_int i = 0; i < __arraycount(regs); i++) {
		CHECK(read_regs[i] == regs[i]);
		CHECK(read_regs[__arraycount(regs) + i] == regs[i]);
		CHECK(observation.sample[0][i] == first[i]);
		CHECK(observation.sample[1][i] == REG(regs[i]));
		if (first[i] != REG(regs[i]))
			changed |= __BIT(i);
	}
	CHECK(observation.changed == changed);
}

static void
test_gpu_inspect(void)
{
	static const bus_size_t regs[] = { 0, 0xa0, 0xa8, 0xac, 0xe0,
	    0xe8, 0xec, 0xb20, 0xb24, 0x500, 0x5c0, 0x1908, 0x1988 };
	static const struct {
		bus_size_t reg;
		uint32_t mask;
		enum sun60i_a733_gpu_reason reason;
	} busy[] = {
		{ GPU0_CLK_REG, __BIT(31), A733_GPU_MODULE_GATED },
		{ GPU0_CLK_REG, __BIT(27), A733_GPU_UPDATE_PENDING },
		{ GPU0_BGR_REG, __BIT(0), A733_GPU_BUS_GATED },
		{ GPU0_BGR_REG, __BIT(16), A733_GPU_RESET_ASSERTED },
		{ AHB_MASTER_GATE_REG, __BIT(7), A733_GPU_MASTER_GATED },
		{ PLL_REF_CTRL_REG, __BIT(31), A733_GPU_REF_FLAGS },
		{ PLL_GPU0_CTRL_REG, __BIT(31), A733_GPU_CORE_PLL },
		{ PLL_PERIPH0_CTRL_REG, __BIT(31), A733_GPU_AHB_PLL },
	};
	struct sun60i_a733_gpu_state result, saved;
	u_int start = checks;

	ready_fixture();
	memset(&result, 0xa5, sizeof(result));
	memcpy(&saved, &result, sizeof(saved));
	CHECK(sun60i_a733_ccu_gpu_inspect(NULL, NULL) == EINVAL);
	CHECK(sun60i_a733_ccu_gpu_inspect(CLOCK(A733_CLK_GPU0), NULL) == EINVAL);
	CHECK(sun60i_a733_ccu_gpu_inspect(NULL, &result) == ENXIO);
	CHECK(sun60i_a733_ccu_gpu_inspect(&hosc, &result) == EOPNOTSUPP);
	CHECK(memcmp(&result, &saved, sizeof(result)) == 0);
	CHECK(reads == 0 && hosc_reads == 0 && writes == 0);
	expect_inspect(A733_GPU_READY, 0);
	/* Multiple failed conditions retain the first cause in evaluation order. */
	for (u_int i = 0; i < __arraycount(busy); i++)
		REG(busy[i].reg) ^= busy[i].mask;
	for (u_int i = 0; i < __arraycount(busy); i++) {
		expect_inspect(busy[i].reason, EBUSY);
		REG(busy[i].reg) ^= busy[i].mask;
	}
	expect_inspect(A733_GPU_READY, 0);
	for (u_int i = 0; i < __arraycount(regs); i++) {
		ready_fixture();
		unstable_reg = regs[i];
		unstable_read = 14 + i;
		/* Snapshot instability takes priority over an already gated module. */
		REG(GPU0_CLK_REG) &= ~__BIT(31);
		unstable_hosc = true;
		expect_inspect(i <= A733_GPU_MASTER ?
		    A733_GPU_SNAPSHOT_CHANGED : A733_GPU_HOSC_CHANGED, EBUSY);
	}
	ready_fixture();
	unstable_all = true;
	expect_inspect(A733_GPU_SNAPSHOT_CHANGED, EBUSY);
	ready_fixture();
	REG(GPU0_CLK_REG) &= ~__BIT(31);
	unstable_hosc = true;
	expect_inspect(A733_GPU_HOSC_CHANGED, EBUSY);
	for (u_int pll = 0; pll < 3; pll++) {
		const bus_size_t reg = pll == 0 ? PLL_REF_CTRL_REG :
		    pll == 1 ? PLL_GPU0_CTRL_REG : PLL_PERIPH0_CTRL_REG;
		const enum sun60i_a733_gpu_reason reason = pll == 0 ?
		    A733_GPU_REF_FLAGS : pll == 1 ? A733_GPU_CORE_PLL : A733_GPU_AHB_PLL;

		for (u_int bit = 27; bit <= 31; bit++) {
			ready_fixture();
			REG(reg) &= ~__BIT(bit);
			expect_inspect(reason, EBUSY);
		}
	}
	ready_fixture();
	REG(PLL_REF_CTRL_REG) |= ACCEL_PLL_P;
	expect_inspect(A733_GPU_REF_RATE, EOPNOTSUPP);
	ready_fixture();
	REG(GPU0_CLK_REG) |= 6 << 24;
	expect_inspect(A733_GPU_CORE_PARENT, EOPNOTSUPP);
	ready_fixture();
	REG(GPU0_CLK_REG) |= 3;
	expect_inspect(A733_GPU_CORE_DIVIDER, EOPNOTSUPP);
	ready_fixture();
	REG(AHB_CFG_REG) = 1 << 24;
	expect_inspect(A733_GPU_AHB_PARENT, EOPNOTSUPP);
	ready_fixture();
	REG(AHB_CFG_REG) |= 6;
	expect_inspect(A733_GPU_AHB_DIVIDER, EOPNOTSUPP);
	for (u_int sel = 0; sel < 6; sel++) {
		const bus_size_t reg = sel == 0 ? PLL_GPU0_CTRL_REG : PLL_PERIPH0_CTRL_REG;

		ready_fixture();
		REG(GPU0_CLK_REG) |= sel << 24;
		REG(reg + 8) = __BIT(31);
		expect_inspect(A733_GPU_CORE_PLL, EOPNOTSUPP);
		REG(reg + 8) = 0;
		REG(reg + 12) = __BIT(27);
		expect_inspect(A733_GPU_CORE_PLL, EOPNOTSUPP);
		REG(reg + 12) = 0;
		REG(reg) &= ~(sel == 1 ? __BIT(26) : __BIT(27));
		expect_inspect(A733_GPU_CORE_PLL, EBUSY);
	}
	ready_fixture();
	REG(PLL_PERIPH0_CTRL_REG + 8) = __BIT(31);
	expect_inspect(A733_GPU_AHB_PLL, EOPNOTSUPP);
	ready_fixture();
	REG(PLL_GPU0_CTRL_REG) = (REG(PLL_GPU0_CTRL_REG) &
	    ~(ACCEL_PLL_N | ACCEL_PLL_M)) | (255 << 8);
	expect_inspect(A733_GPU_CORE_PLL, ERANGE);
	printf("A733 GPU inspection: %u production checks passed\n", checks - start);
}

static void
test_gpu_ready(void)
{
	const u_int parents[] = { 1200000000, 800000000, 600000000,
	    400000000, 300000000, 200000000 };
	const u_int divs[] = { 1, 2, 4, 8, 16 };
	const u_int raw[] = { 0, 8, 12, 14, 15 };
	const bus_size_t snapshots[] = { 0, 0xa0, 0xa8, 0xac, 0xe0,
	    0xe8, 0xec, 0xb20, 0xb24, 0x500, 0x5c0, 0x1908, 0x1988 };
	u_int core = 111, bus = 222, start = checks;

	ready_fixture();
	CHECK(sun60i_a733_ccu_gpu_ready(NULL, &core, &bus) == ENXIO);
	CHECK(sun60i_a733_ccu_gpu_ready(&hosc, &core, &bus) == EOPNOTSUPP);
	CHECK(sun60i_a733_ccu_gpu_ready(CLOCK(A733_CLK_GPU0), NULL, &bus) == EINVAL);
	CHECK(sun60i_a733_ccu_gpu_ready(CLOCK(A733_CLK_GPU0), &core, NULL) == EINVAL);
	CHECK(sun60i_a733_ccu_gpu_ready(CLOCK(A733_CLK_GPU0), &core, &core) == EINVAL);
	CHECK(core == 111 && bus == 222 && reads == 0 && writes == 0);
	CLOCK(A733_CLK_GPU0)->domain = NULL;
	expect_ready(ENXIO, 0, 0);
	CHECK(reads == 0);
	CLOCK(A733_CLK_GPU0)->domain = &state.sc_clkdom;
	state.sc_clkdom.priv = NULL;
	expect_ready(ENXIO, 0, 0);
	CHECK(reads == 0);
	state.sc_clkdom.priv = &state;
	missing_hosc = true;
	expect_ready(ENXIO, 0, 0);
	CHECK(reads == 0);
	missing_hosc = false;
	hosc_rate = 19200000;
	/* Fixed hosc is retained only as a diagnostic, never as DCXO input. */
	expect_ready(0, 1200000000, 600000000);
	CHECK(reads == 26);
	hosc_rate = 24000000;
	unstable_hosc = true;
	expect_ready(EBUSY, 0, 0);
	unstable_hosc = false;

	for (u_int sel = 0; sel < __arraycount(parents); sel++) {
		for (u_int div = 0; div < __arraycount(divs); div++) {
			for (u_int ahb = 1; ahb <= 32; ahb++) {
				REG(GPU0_CLK_REG) = ACCEL_CLK_ENABLE |
				    (sel << 24) | raw[div];
				REG(AHB_CFG_REG) = (3 << 24) | (ahb - 1);
				expect_ready(600000000 % ahb == 0 ? 0 : EOPNOTSUPP,
				    parents[sel] / divs[div], 600000000 / ahb);
			}
		}
	}
	for (u_int sel = 6; sel < 8; sel++) {
		REG(GPU0_CLK_REG) = ACCEL_CLK_ENABLE | (sel << 24);
		expect_ready(EOPNOTSUPP, 0, 0);
	}
	for (u_int div = 0; div < 16; div++) {
		REG(GPU0_CLK_REG) = ACCEL_CLK_ENABLE | div;
		REG(AHB_CFG_REG) = 0;
		expect_ready(div == 0 || div == 8 || div == 12 || div >= 14 ?
		    0 : EOPNOTSUPP, 1200000000 / 16 * (16 - div), 24000000);
	}
	for (u_int sel = 1; sel <= 2; sel++) {
		REG(GPU0_CLK_REG) = ACCEL_CLK_ENABLE;
		REG(AHB_CFG_REG) = sel << 24;
		expect_ready(EOPNOTSUPP, 0, 0);
	}
	ready_fixture();
	for (u_int bit = 27; bit <= 31; bit++) {
		REG(PLL_REF_CTRL_REG) &= ~__BIT(bit);
		expect_ready(EBUSY, 0, 0);
		REG(PLL_REF_CTRL_REG) |= __BIT(bit);
		REG(PLL_GPU0_CTRL_REG) &= ~__BIT(bit);
		expect_ready(EBUSY, 0, 0);
		REG(PLL_GPU0_CTRL_REG) |= __BIT(bit);
		REG(PLL_PERIPH0_CTRL_REG) &= ~__BIT(bit);
		expect_ready(EBUSY, 0, 0);
		REG(PLL_PERIPH0_CTRL_REG) |= __BIT(bit);
	}
	REG(PLL_REF_CTRL_REG) |= ACCEL_PLL_P;
	expect_ready(EOPNOTSUPP, 0, 0);
	REG(PLL_REF_CTRL_REG) = (REG(PLL_REF_CTRL_REG) & ~ACCEL_PLL_N) | (199 << 8);
	expect_ready(0, 1200000000, 600000000);
	REG(PLL_GPU0_CTRL_REG) |= ACCEL_PLL_P;
	REG(PLL_PERIPH0_CTRL_REG) |= ACCEL_PLL_P;
	expect_ready(0, 600000000, 300000000);
	/* Each peripheral output has its own gate and output divider. */
	REG(GPU0_CLK_REG) |= 1 << 24;
	expect_ready(0, 400000000, 300000000);
	REG(PLL_PERIPH0_CTRL_REG) &= ~__BIT(26);
	expect_ready(EBUSY, 0, 0);
	REG(PLL_PERIPH0_CTRL_REG) |= __BIT(26);
	for (u_int sel = 1; sel <= 5; sel++) {
		REG(GPU0_CLK_REG) = ACCEL_CLK_ENABLE | (sel << 24);
		expect_ready(0, parents[sel] / 2, 300000000);
	}
	ready_fixture();
	REG(PLL_GPU0_CTRL_REG) &= ~ACCEL_PLL_N;
	expect_ready(EOPNOTSUPP, 0, 0);
	ready_fixture();
	REG(PLL_GPU0_CTRL_REG) = (REG(PLL_GPU0_CTRL_REG) & ~ACCEL_PLL_M) | (6 << 20);
	expect_ready(EOPNOTSUPP, 0, 0);
	ready_fixture();
	REG(PLL_GPU0_CTRL_REG) = (REG(PLL_GPU0_CTRL_REG) &
	    ~(ACCEL_PLL_N | ACCEL_PLL_M)) | (255 << 8);
	expect_ready(ERANGE, 0, 0);
	for (u_int pll = 0; pll < 2; pll++) {
		const bus_size_t reg = pll == 0 ? PLL_GPU0_CTRL_REG : PLL_PERIPH0_CTRL_REG;

		ready_fixture();
		REG(reg + 8) = __BIT(31);
		expect_ready(EOPNOTSUPP, 0, 0);
		REG(reg + 8) = 0;
		REG(reg + 12) = __BIT(27);
		expect_ready(EOPNOTSUPP, 0, 0);
		REG(reg + 12) = 0;
		expect_ready(0, 1200000000, 600000000);
	}
	for (u_int i = 0; i < 5; i++) {
		const bus_size_t regs[] = { GPU0_CLK_REG, GPU0_CLK_REG,
		    GPU0_BGR_REG, GPU0_BGR_REG, AHB_MASTER_GATE_REG };
		const uint32_t masks[] = { __BIT(31), __BIT(27), __BIT(0),
		    __BIT(16), __BIT(7) };

		ready_fixture();
		REG(regs[i]) ^= masks[i];
		expect_ready(EBUSY, 0, 0);
	}
	for (u_int i = 0; i < __arraycount(snapshots); i++) {
		ready_fixture();
		unstable_reg = snapshots[i];
		unstable_read = 14 + i;
		expect_ready(i <= A733_GPU_MASTER ? EBUSY : 0,
		    1200000000, 600000000);
		CHECK(reads == 26);
	}
	ready_fixture();
	REG(AHB_CFG_REG) = 0;
	/* Unused peripheral PLL state is not required for a PLL_GPU path. */
	REG(PLL_PERIPH0_CTRL_REG) = 0;
	REG(PLL_PERIPH0_CTRL_REG + 8) = __BIT(31);
	expect_ready(0, 1200000000, 24000000);
	printf("A733 GPU readiness: %u production checks passed\n", checks - start);
}

static void
test_dcxo(void)
{
	struct sun60i_a733_gpu_state observation;
	const int errors[] = { ENXIO, EIO, EBUSY, EOPNOTSUPP };
	const u_int rates[] = { 19200000, 24000000, 26000000 };
	const u_int n[] = { 124, 99, 95 }, m[] = { 99, 99, 103 };

	for (u_int i = 0; i < __arraycount(rates); i++) {
		ready_fixture();
		dcxo_rate = rates[i];
		dcxo_raw = i << 14;
		REG(PLL_REF_CTRL_REG) = __BITS(31,27) | (n[i] << 8) | (m[i] << 16);
		REG(GPU0_CLK_REG) = ACCEL_CLK_ENABLE | (3 << 24);
		REG(AHB_CFG_REG) = (3 << 24) | 2;
		expect_ready(0, 400000000, 200000000);
		CHECK(dcxo_queries == 2 && hosc_rate == 24000000);
		reads = hosc_reads = dcxo_queries = 0;
		CHECK(sun60i_a733_ccu_gpu_inspect(CLOCK(A733_CLK_GPU0), &observation) == 0);
		CHECK(observation.dcxo_hz[0] == rates[i] && observation.dcxo_hz[1] == rates[i]);
		CHECK(observation.dcxo_sample[0][0] == dcxo_raw &&
		    observation.dcxo_sample[1][1] == dcxo_raw);
	}
	/* Captured REF f8675f00 requires hardware-classified 26 MHz. */
	ready_fixture(); REG(PLL_REF_CTRL_REG) = 0xf8675f00;
	dcxo_rate = 26000000; expect_ready(0, 1200000000, 600000000);
	dcxo_rate = 24000000; expect_ready(EOPNOTSUPP, 0, 0);
	ready_fixture(); unstable_dcxo = true;
	expect_ready(EBUSY, 0, 0);
	reads = hosc_reads = dcxo_queries = 0;
	CHECK(sun60i_a733_ccu_gpu_inspect(CLOCK(A733_CLK_GPU0), &observation) == 0);
	CHECK(observation.reason == A733_GPU_DCXO_CHANGED);
	for (u_int i = 0; i < __arraycount(errors); i++) {
		for (int query = 1; query <= 2; query++) {
			ready_fixture(); dcxo_error = errors[i]; dcxo_fail_query = query;
			expect_ready(errors[i], 0, 0);
			CHECK(reads == (query == 1 ? 0U : 26U));
			CHECK(dcxo_queries == (u_int)query);
		}
	}
	for (int len = -1; len <= 12; len++) {
		if (len == 4) continue;
		ready_fixture(); dcxo_len = len; expect_ready(EINVAL, 0, 0);
		CHECK(reads == 0 && dcxo_queries == 0);
	}
	ready_fixture(); dcxo_node = 0; expect_ready(EINVAL, 0, 0);
	ready_fixture(); bad_dcxo = true; expect_ready(EOPNOTSUPP, 0, 0);
	CHECK(reads == 0 && dcxo_queries == 0);
}

static void
test_periph_gates(void)
{
	const bus_size_t offsets[] = { 0x1908, 0x1988 };
	const bus_size_t short_sizes[] = { 0, 0x1500, 0x190c, 0x1988, 0x198b };
	struct sun60i_a733_gpu_state observed, saved;
	uint32_t before[__arraycount(registers)];
	u_int start = checks;

	/* Each control/status bit, including reserved and CPUS-only fields. */
	for (u_int reg = 0; reg < 2; reg++) {
		for (u_int bit = 0; bit < 32; bit++) {
			uint32_t gate = bit >= 16 && bit <= 27 ?
			    __BIT(bit - 16) : 0;

			ready_fixture();
			REG(offsets[reg]) = __BIT(bit);
			memcpy(before, registers, sizeof(before));
			CHECK(sun60i_a733_ccu_gpu_inspect(CLOCK(A733_CLK_GPU0),
			    &observed) == 0);
			for (u_int sample = 0; sample < 2; sample++) {
				CHECK(observed.periph_gates[sample].configured ==
				    (reg == 0 ? gate : 0));
				CHECK(observed.periph_gates[sample].no_auto ==
				    (reg == 0 && bit < 12 ? __BIT(bit) : 0));
				CHECK(observed.periph_gates[sample].effective ==
				    (reg == 1 ? gate : 0));
				CHECK(observed.sample[sample][reg == 0 ?
				    A733_GPU_PERIPH_GATE_EN : A733_GPU_PERIPH_GATE_STAT] ==
				    __BIT(bit));
			}
			CHECK(observed.changed == 0 && observed.readiness_error == 0);
			CHECK(observed.core_hz == 1200000000 && observed.bus_hz == 600000000);
			CHECK(reads == 26 && writes == 0 && barriers == 0);
			CHECK(memcmp(before, registers, sizeof(before)) == 0);
		}
	}
	/* A real status gate edge is reported, without changing old readiness. */
	ready_fixture();
	unstable_reg = 0x1988; unstable_read = 26; unstable_mask = __BIT(17);
	CHECK(sun60i_a733_ccu_gpu_inspect(CLOCK(A733_CLK_GPU0), &observed) == 0);
	CHECK(observed.periph_gates[0].effective == 0);
	CHECK(observed.periph_gates[1].effective == 2);
	CHECK(observed.changed == __BIT(A733_GPU_PERIPH_GATE_STAT));
	CHECK(observed.readiness_error == 0 && writes == 0);

	/* The generic provider maps FDT reg[0]; never read past that range. */
	for (u_int i = 0; i < __arraycount(short_sizes); i++) {
		ready_fixture(); ccu_size = short_sizes[i];
		memset(&observed, 0xa5, sizeof(observed)); saved = observed;
		CHECK(sun60i_a733_ccu_gpu_inspect(CLOCK(A733_CLK_GPU0), &observed) == ENXIO);
		CHECK(memcmp(&saved, &observed, sizeof(saved)) == 0);
		CHECK(reads == 0 && dcxo_queries == 0 && hosc_reads == 0 && writes == 0);
	}
	ready_fixture(); ccu_reg_error = ENXIO;
	expect_ready(ENXIO, 0, 0);
	CHECK(reads == 0 && dcxo_queries == 0);
	ready_fixture(); ccu_size = 0x198c;
	expect_ready(0, 1200000000, 600000000);
	printf("A733 PERI0 gate observations: %u checks passed\n", checks - start);
}

static void
prepare_fixture(void)
{
	ready_fixture();
	prepare_test = true;
	dcxo_rate = 26000000; dcxo_raw = 0x183fb0f7;
	REG(PLL_REF_CTRL_REG) = 0xf8675f00;
	REG(PLL_PERIPH0_CTRL_REG) = 0xff126310;
	REG(PLL_PERIPH0_CTRL_REG + 12) = 0x30000000;
	REG(PLL_GPU0_CTRL_REG) = 0x41104500;
	REG(PLL_GPU0_CTRL_REG + 12) = 0x30000000;
	REG(GPU0_CLK_REG) = 0;
	REG(GPU0_BGR_REG) = __BIT(7); /* Preserve an unrelated field. */
	REG(AHB_CFG_REG) = 0x03000002;
	REG(AHB_MASTER_GATE_REG) = 0x110103ff;
}

static void
test_gpu_prepare(void)
{
	const int owner = 1, other = 2;
	const u_int protected[] = { A733_CLK_GPU0, A733_CLK_BUS_GPU0,
	    A733_CLK_AHB_GPU0, A733_CLK_PLL_GPU0, A733_CLK_PLL_REF, A733_CLK_SYS_24M,
	    A733_CLK_PLL_PERIPH0_4X, A733_CLK_PLL_PERIPH0_2X,
	    A733_CLK_PLL_PERIPH0_800M, A733_CLK_PLL_PERIPH0_480M,
	    A733_CLK_PLL_PERIPH0_600M, A733_CLK_PLL_PERIPH0_400M,
	    A733_CLK_PLL_PERIPH0_300M, A733_CLK_PLL_PERIPH0_200M,
	    A733_CLK_PLL_PERIPH0_160M, A733_CLK_PLL_PERIPH0_150M, A733_CLK_AHB };
	bool retained, handled;
	unsigned start = checks;

	prepare_fixture();
	CHECK(sun60i_a733_ccu_gpu_reserve(CLOCK(A733_CLK_GPU0), &owner) == 0);
	CHECK(writes == 0);
	CHECK(sun60i_a733_ccu_gpu_reserve(CLOCK(A733_CLK_GPU0), &other) == EBUSY);
	for (u_int n = 0; n < __arraycount(protected); n++) {
		struct clk *c = CLOCK(protected[n]);
		parent_queries = 0;
		CHECK(clk_enable(c) == (n < 4 ? EBUSY : 0));
		CHECK(parent_queries == 0); /* Guard precedes parent recursion. */
		CHECK(sunxi_ccu_clock_disable(&state, c) == EBUSY);
		CHECK(clk_set_rate(c, 400000000) == EBUSY);
		CHECK(clk_set_parent(c, CLOCK(A733_CLK_PLL_REF)) == EBUSY);
		CHECK(writes == 0 && sun60i_gpu_lease.writers == 0);
	}
	CHECK(sunxi_ccu_reset_assert(&state,
	    &state.sc_resets[A733_RST_BUS_GPU0]) == EBUSY);
	CHECK(sunxi_ccu_reset_deassert(&state,
	    &state.sc_resets[A733_RST_BUS_GPU0]) == EBUSY);
	CHECK(writes == 0);
	CHECK(clk_enable(CLOCK(A733_CLK_BUS_UART0)) == 0);
	CHECK(writes == 1); /* Its borrowed ancestors caused no writes. */
	CHECK(sunxi_ccu_reset_deassert(&state,
	    &state.sc_resets[A733_RST_BUS_UART0]) == 0);
	CHECK(writes == 2 && sun60i_gpu_lease.writers == 0);
	CHECK(sun60i_a733_ccu_gpu_release(CLOCK(A733_CLK_GPU0), &other) == EINVAL);
	CHECK(sun60i_a733_ccu_gpu_release(CLOCK(A733_CLK_GPU0), &owner) == 0);
	/* New observations cannot strengthen or weaken the existing lease policy. */
	for (u_int reg = 0; reg < 2; reg++) {
		const bus_size_t offset = reg == 0 ? 0x1908 : 0x1988;

		prepare_fixture(); retained = false;
		CHECK(sun60i_a733_ccu_gpu_reserve(CLOCK(A733_CLK_GPU0), &owner) == 0);
		REG(offset) = UINT32_MAX;
		CHECK(sun60i_a733_ccu_gpu_prepare(CLOCK(A733_CLK_GPU0), &owner,
		    &retained) == 0);
		CHECK(retained && writes == 4 && REG(offset) == UINT32_MAX);
	}

	prepare_fixture(); handled = false;
	CHECK(sun60i_a733_gpu_guard_enter(&state, CLOCK(A733_CLK_BUS_UART0),
	    NULL, SUNXI_CCU_ENABLE, &handled) == 0 && !handled);
	CHECK(sun60i_a733_ccu_gpu_reserve(CLOCK(A733_CLK_GPU0), &owner) == EBUSY);
	sun60i_a733_gpu_guard_exit(&state);
	CHECK(sun60i_a733_ccu_gpu_reserve(CLOCK(A733_CLK_GPU0), &owner) == 0);
	CHECK(writes == 0);

	for (unsigned fail = 0; fail <= 4; fail++) {
		prepare_fixture(); retained = false;
		CHECK(sun60i_a733_ccu_gpu_reserve(CLOCK(A733_CLK_GPU0), &owner) == 0);
		reject_write_number = fail;
		CHECK(sun60i_a733_ccu_gpu_prepare(CLOCK(A733_CLK_GPU0),
		    &owner, &retained) == (fail == 0 ? 0 : EIO));
		CHECK(retained && writes == (fail == 0 ? 4 : fail));
		CHECK(write_regs[0] == 0xb20 && write_vals[0] == 0x0b000000);
		if (writes >= 2)
			CHECK(write_regs[1] == 0xb24 && write_vals[1] == 0x00010080);
		if (writes >= 3)
			CHECK(write_regs[2] == 0xb24 && write_vals[2] == 0x00010081);
		if (writes == 4)
			CHECK(write_regs[3] == 0xb20 && write_vals[3] == 0x8b000000);
		CHECK(REG(PLL_REF_CTRL_REG) == 0xf8675f00 &&
		    REG(PLL_PERIPH0_CTRL_REG) == 0xff126310);
		CHECK(sun60i_a733_ccu_gpu_release(CLOCK(A733_CLK_GPU0), &owner) == EBUSY);
		unsigned before = writes;
		CHECK(sun60i_a733_ccu_gpu_prepare(CLOCK(A733_CLK_GPU0),
		    &owner, &retained) == EBUSY && writes == before);
	}
	/* Changed sources between reservation and first write are not activated. */
	prepare_fixture(); retained = false;
	CHECK(sun60i_a733_ccu_gpu_reserve(CLOCK(A733_CLK_GPU0), &owner) == 0);
	REG(PLL_PERIPH0_CTRL_REG) ^= __BIT(8);
	CHECK(sun60i_a733_ccu_gpu_prepare(CLOCK(A733_CLK_GPU0), &owner,
	    &retained) == EBUSY && !retained && writes == 0);
	CHECK(sun60i_a733_ccu_gpu_release(CLOCK(A733_CLK_GPU0), &owner) == 0);
	for (unsigned test = 0; test < 8; test++) {
		prepare_fixture();
		switch (test) {
		case 0: REG(GPU0_CLK_REG) = __BIT(31); break;
		case 1: REG(GPU0_BGR_REG) |= __BIT(16); break;
		case 2: REG(GPU0_BGR_REG) |= __BIT(0); break;
		case 3: REG(PLL_PERIPH0_CTRL_REG) &= ~__BIT(28); break;
		case 4: REG(PLL_PERIPH0_CTRL_REG + 8) |= __BIT(31); break;
		case 5: dcxo_rate = 24000000; break;
		case 6: REG(AHB_MASTER_GATE_REG) &= ~__BIT(7); break;
		case 7: REG(AHB_CFG_REG) = 0; break;
		}
		CHECK(sun60i_a733_ccu_gpu_reserve(CLOCK(A733_CLK_GPU0), &owner) != 0);
		CHECK(sun60i_gpu_lease.owner == NULL && writes == 0);
	}
	prepare_test = false;
	printf("A733 experimental CCU ownership: %u checks passed\n", checks - start);
}

static void
test_gpu_update_completion(void)
{
	const unsigned clear_us[] = { 30, 0, 10000, 10010, UINT_MAX };
	const int owner = 1;
	unsigned start = checks;
	bool retained;

	for (u_int n = 0; n < __arraycount(clear_us); n++) {
		prepare_fixture(); retained = false;
		update_model = true;
		update_clear_us = clear_us[n];
		CHECK(sun60i_a733_ccu_gpu_reserve(CLOCK(A733_CLK_GPU0), &owner) == 0);
		fprintf(stderr, "UPDATE fixture: clear after %u us\n", clear_us[n]);
		CHECK(sun60i_a733_ccu_gpu_prepare(CLOCK(A733_CLK_GPU0),
		    &owner, &retained) == (n < 3 ? 0 : ETIMEDOUT));
		CHECK(update_delay_us == (n < 3 ? clear_us[n] : 10000));
		/* Final write readback plus the bounded completion observations. */
		CHECK(update_reads == 2 + update_delay_us / 10);
		CHECK((REG(GPU0_CLK_REG) & GPU_CLK_UPDATE) ==
		    (n < 3 ? 0 : GPU_CLK_UPDATE));
		CHECK(retained && writes == 4 && barriers == 4);
		CHECK(write_regs[0] == 0xb20 && write_vals[0] == 0x0b000000);
		CHECK(write_regs[1] == 0xb24 && write_vals[1] == 0x00010080);
		CHECK(write_regs[2] == 0xb24 && write_vals[2] == 0x00010081);
		CHECK(write_regs[3] == 0xb20 && write_vals[3] == 0x8b000000);
		CHECK(sun60i_a733_ccu_gpu_release(CLOCK(A733_CLK_GPU0), &owner) == EBUSY);
		CHECK(sun60i_a733_ccu_gpu_prepare(CLOCK(A733_CLK_GPU0), &owner,
		    &retained) == EBUSY && writes == 4);
	}
	prepare_test = false;
	printf("A733 GPU UPDATE completion: %u checks passed\n", checks - start);
}

static void
pll_fixture(void)
{
	prepare_fixture();
	pll_model = true;
	REG(GPUPLL_GATE_EN_REG) = 0xa5010001;
	/* The concurrent NPU uses PERIPH0-600M; preserve its live state. */
	REG(NPU_CLK_REG) = 0x82000000;
	REG(NPU_BGR_REG) = 0x00070001;
	REG(PLL_NPU_CTRL_REG) = 0x41104500;
}

static void
test_gpu_prepare_pll(void)
{
	const int owner = 1, other = 2;
	const unsigned locks[] = { 0, 9, 291, 294, UINT_MAX };
	uint32_t saved[__arraycount(registers)];
	bool retained;
	unsigned start = checks;
	u_int core, bus;

	pll_fixture(); retained = false;
	CHECK(sun60i_a733_ccu_gpu_prepare_pll(NULL, &owner, &retained) == EINVAL);
	CHECK(sun60i_a733_ccu_gpu_prepare_pll(CLOCK(A733_CLK_NPU), &owner,
	    &retained) == EINVAL);
	CHECK(sun60i_a733_ccu_gpu_prepare_pll(CLOCK(A733_CLK_GPU0), NULL,
	    &retained) == EINVAL);
	CHECK(sun60i_a733_ccu_gpu_prepare_pll(CLOCK(A733_CLK_GPU0), &owner,
	    NULL) == EINVAL);
	CHECK(sun60i_a733_ccu_gpu_prepare_pll(CLOCK(A733_CLK_GPU0), &owner,
	    &retained) == EBUSY);
	CHECK(sun60i_a733_ccu_gpu_reserve(CLOCK(A733_CLK_GPU0), &owner) == 0);
	CHECK(sun60i_a733_ccu_gpu_prepare_pll(CLOCK(A733_CLK_GPU0), &other,
	    &retained) == EBUSY);
	CHECK(!retained && writes == 0);
	CHECK(sun60i_a733_ccu_gpu_release(CLOCK(A733_CLK_GPU0), &owner) == 0);

	/* Unsupported initial states fail before the first hardware write. */
	for (unsigned n = 0; n < 9; n++) {
		pll_fixture(); retained = false;
		switch (n) {
		case 0: REG(PLL_GPU0_CTRL_REG) |= ACCEL_PLL_ENABLE; break;
		case 1: REG(PLL_GPU0_CTRL_REG) |= ACCEL_PLL_LOCK_ENABLE; break;
		case 2: REG(PLL_GPU0_CTRL_REG) |= ACCEL_PLL_LOCK; break;
		case 3: REG(PLL_GPU0_CTRL_REG) |= ACCEL_PLL_OUTPUT; break;
		case 4: REG(PLL_GPU0_CTRL_REG) |= ACCEL_PLL_P; break;
		case 5: REG(PLL_GPU0_CTRL_REG + 8) |= __BIT(31); break;
		case 6: REG(PLL_GPU0_CTRL_REG + 12) |= __BIT(27); break;
		case 7: REG(GPUPLL_GATE_EN_REG) &= ~__BIT(0); break;
		case 8: REG(GPUPLL_GATE_EN_REG) &= ~__BIT(16); break;
		}
		CHECK(sun60i_a733_ccu_gpu_reserve(CLOCK(A733_CLK_GPU0), &owner) == 0);
		CHECK(sun60i_a733_ccu_gpu_prepare_pll(CLOCK(A733_CLK_GPU0), &owner,
		    &retained) == (n < 4 ? EBUSY : EOPNOTSUPP));
		CHECK(!retained && writes == 0);
		CHECK(sun60i_a733_ccu_gpu_release(CLOCK(A733_CLK_GPU0), &owner) == 0);
	}
	for (unsigned n = 0; n < 5; n++) {
		pll_fixture(); retained = false;
		CHECK(sun60i_a733_ccu_gpu_reserve(CLOCK(A733_CLK_GPU0), &owner) == 0);
		switch (n) {
		case 0: REG(PLL_GPU0_CTRL_REG) ^= __BIT(8); break;
		case 1: REG(GPU0_CLK_REG) = ACCEL_CLK_ENABLE; break;
		case 2: REG(GPU0_BGR_REG) |= __BIT(16); break;
		case 3: dcxo_error = ENXIO; break;
		case 4:
			unstable_reg = GPUPLL_GATE_EN_REG;
			unstable_read = reads + 28; /* Second gate sample. */
			break;
		}
		CHECK(sun60i_a733_ccu_gpu_prepare_pll(CLOCK(A733_CLK_GPU0), &owner,
		    &retained) == (n == 3 ? ENXIO : EBUSY));
		CHECK(!retained && writes == 0);
		CHECK(sun60i_a733_ccu_gpu_release(CLOCK(A733_CLK_GPU0), &owner) == 0);
	}

	/* Both initial LDO states work; every actual write can fail. */
	for (unsigned ldo = 0; ldo < 2; ldo++) {
		const unsigned total = ldo == 0 ? 8 : 7;
		const unsigned output = total - 4;

		for (unsigned fail = 0; fail <= total; fail++) {
			pll_fixture(); retained = false;
			if (ldo == 0)
				REG(PLL_GPU0_CTRL_REG) &= ~ACCEL_PLL_LDO;
			memcpy(saved, registers, sizeof(saved));
			CHECK(sun60i_a733_ccu_gpu_reserve(CLOCK(A733_CLK_GPU0), &owner) == 0);
			reject_write_number = fail;
			CHECK(sun60i_a733_ccu_gpu_prepare_pll(CLOCK(A733_CLK_GPU0),
			    &owner, &retained) == (fail == 0 ? 0 : EIO));
			CHECK(retained && writes == (fail == 0 ? total : fail));
			CHECK(barriers == writes);
			/* No shared, automatic gate, SDM, or NPU register changes. */
			for (u_int r = 0; r < __arraycount(registers); r++) {
				if (r != PLL_GPU0_CTRL_REG / 4 && r != GPU0_CLK_REG / 4 &&
				    r != GPU0_BGR_REG / 4)
					CHECK(registers[r] == saved[r]);
			}
			CHECK((REG(PLL_GPU0_CTRL_REG) & __BIT(24)) != 0);
			CHECK((REG(GPU0_BGR_REG) & __BIT(7)) != 0);
			CHECK(write_regs[0] == PLL_GPU0_CTRL_REG);
			CHECK((write_vals[0] & (ACCEL_PLL_N | ACCEL_PLL_M)) ==
			    ((74 << 8) | (2 << 20)));
			for (unsigned w = 0; w < writes && w < output; w++) {
				CHECK(write_regs[w] == PLL_GPU0_CTRL_REG);
				CHECK((write_vals[w] & ACCEL_PLL_OUTPUT) == 0);
			}
			if (fail == 0) {
				CHECK(pll_settled && pll_delay_us == 29);
				CHECK(REG(PLL_GPU0_CTRL_REG) == 0xf9204a00);
				CHECK(REG(GPU0_CLK_REG) == ACCEL_CLK_ENABLE);
				CHECK(write_regs[output] == PLL_GPU0_CTRL_REG);
				CHECK((write_vals[output] & ACCEL_PLL_OUTPUT) != 0);
				CHECK(sun60i_a733_ccu_gpu_ready(CLOCK(A733_CLK_GPU0),
				    &core, &bus) == 0 && core == 600000000 && bus == 200000000);
			}
			CHECK(sun60i_a733_ccu_gpu_release(CLOCK(A733_CLK_GPU0), &owner) == EBUSY);
			unsigned count = writes;
			CHECK(sun60i_a733_ccu_gpu_prepare_pll(CLOCK(A733_CLK_GPU0),
			    &owner, &retained) == EBUSY && writes == count);
		}
	}

	for (u_int n = 0; n < __arraycount(locks); n++) {
		pll_fixture(); retained = false;
		pll_lock_after_us = locks[n];
		CHECK(sun60i_a733_ccu_gpu_reserve(CLOCK(A733_CLK_GPU0), &owner) == 0);
		CHECK(sun60i_a733_ccu_gpu_prepare_pll(CLOCK(A733_CLK_GPU0), &owner,
		    &retained) == (n < 3 ? 0 : ETIMEDOUT));
		CHECK(retained && pll_delay_us == (n < 3 ? locks[n] + 29 : 300));
		if (n >= 3) {
			CHECK((REG(PLL_GPU0_CTRL_REG) & ACCEL_PLL_OUTPUT) == 0);
			CHECK(REG(GPU0_CLK_REG) == 0 && writes == 3);
		}
	}
	/* A false lock sample resets the consecutive sample count. */
	pll_fixture(); retained = false; pll_glitch = true;
	CHECK(sun60i_a733_ccu_gpu_reserve(CLOCK(A733_CLK_GPU0), &owner) == 0);
	CHECK(sun60i_a733_ccu_gpu_prepare_pll(CLOCK(A733_CLK_GPU0), &owner,
	    &retained) == 0 && pll_delay_us == 35);

	for (unsigned n = 0; n < 5; n++) {
		pll_fixture(); retained = false;
		switch (n) {
		case 0: pll_settle_loss = true; break;
		case 1: pll_gate_change = true; break;
		case 2: pll_output_loss = true; break;
		case 3: pll_factor_change = true; break;
		case 4: dcxo_error = ENXIO; dcxo_fail_query = 7; break;
		}
		CHECK(sun60i_a733_ccu_gpu_reserve(CLOCK(A733_CLK_GPU0), &owner) == 0);
		CHECK(sun60i_a733_ccu_gpu_prepare_pll(CLOCK(A733_CLK_GPU0), &owner,
		    &retained) == (n == 2 ? EBUSY : n == 4 ? ENXIO : EIO));
		CHECK(retained);
		if (n < 2)
			CHECK(writes == 3 && (REG(PLL_GPU0_CTRL_REG) & ACCEL_PLL_OUTPUT) == 0);
		CHECK(sun60i_a733_ccu_gpu_release(CLOCK(A733_CLK_GPU0), &owner) == EBUSY);
	}
	for (unsigned n = 0; n < 2; n++) {
		pll_fixture(); retained = false; update_model = true;
		update_clear_us = n == 0 ? 30 : UINT_MAX;
		CHECK(sun60i_a733_ccu_gpu_reserve(CLOCK(A733_CLK_GPU0), &owner) == 0);
		CHECK(sun60i_a733_ccu_gpu_prepare_pll(CLOCK(A733_CLK_GPU0), &owner,
		    &retained) == (n == 0 ? 0 : ETIMEDOUT));
		CHECK(retained && update_delay_us == (n == 0 ? 30 : 10000));
	}
	prepare_test = false;
	printf("A733 opt-in GPU PLL600: %u checks passed\n", checks - start);
}

static void
test_display_observer(void)
{
	struct sun60i_a733_display_clocks observed, saved;

	memset(&saved, 0x5a, sizeof(saved));
	for (unsigned fault = 1; fault <= 30; fault++) {
		reset();
		fail_peek = fault;
		observed = saved;
		CHECK(sun60i_a733_display_read_clocks(sun60i_a733_display_ccu_read,
		    &state, ccu_size, &observed) == EFAULT);
		CHECK(reads == fault && writes == 0 && barriers == 0);
		CHECK(memcmp(&observed, &saved, sizeof(observed)) == 0);
	}
	for (unsigned failure = 0; failure < 2; failure++) {
		reset();
		if (failure == 0)
			ccu_size = 0x19ab;
		else
			ccu_reg_error = ENXIO;
		sun60i_a733_display_ccu_report(&state);
		CHECK(reads == 0 && writes == 0 && barriers == 0);
	}
	printf("PASS: 32 display CCU adapter fault/aperture scenarios\n");
}

int
main(void)
{
	const u_int gate_ids[] = { A733_CLK_AHB_NPU, A733_CLK_AHB_GPU0,
	    A733_CLK_MBUS_GPU0, A733_CLK_MBUS_NPU, A733_CLK_BUS_NPU,
	    A733_CLK_BUS_GPU0 };
	const u_int reset_ids[] = { A733_RST_BUS_NPU_CORE,
	    A733_RST_BUS_NPU_AXI, A733_RST_BUS_NPU_AHB,
	    A733_RST_BUS_NPU_SRAM, A733_RST_BUS_GPU0 };
	const u_int divs[] = { 1, 2, 4, 8, 16 };
	const u_int raw[] = { 0, 8, 12, 14, 15 };
	uint32_t val, old;
	u_int before;

	if (NSUNXI_RTC == 0) {
		ready_fixture();
		expect_ready(ENXIO, 0, 0);
		CHECK(reads == 0 && dcxo_queries == 0);
		puts("PASS: CCU without RTC rejects observation before MMIO");
		return 0;
	}
	test_display_observer();
	reset();
	CHECK(sun60i_a733_ccu_match(NULL, NULL,
	    &(struct fdt_attach_args){ 0 }) != 0);
	for (u_int i = 0; i < __arraycount(gate_ids); i++) {
		struct sunxi_ccu_clk *c = &state.sc_clks[gate_ids[i]];
		REG(c->u.gate.reg) = 0xa510551a & ~c->u.gate.mask;
		old = REG(c->u.gate.reg);
		CHECK(clk_enable(&c->base) == 0);
		CHECK(REG(c->u.gate.reg) == (old | c->u.gate.mask));
		CHECK(sunxi_ccu_clock_disable(&state, &c->base) == 0);
		CHECK(REG(c->u.gate.reg) == old);
		CHECK(c->base.flags == 0);
	}
	for (u_int i = 0; i < __arraycount(reset_ids); i++) {
		struct sunxi_ccu_reset *r = &state.sc_resets[reset_ids[i]];
		CHECK(r->reg == (i == 4 ? 0xb24 : 0xb04));
		CHECK(r->mask == __BIT(i == 4 ? 16 : 16 + i));
		REG(r->reg) = 0xa5a5a5a5;
		CHECK(sunxi_ccu_reset_assert(&state, r) == 0);
		CHECK(REG(r->reg) == (0xa5a5a5a5 & ~r->mask));
		CHECK(sunxi_ccu_reset_deassert(&state, r) == 0);
		CHECK(REG(r->reg) == (0xa5a5a5a5 | r->mask));
	}
	reset();
	for (u_int sel = 6; sel < 8; sel++) {
		REG(GPU0_CLK_REG) = sel << 24;
		CHECK(clk_get_parent(CLOCK(A733_CLK_GPU0)) == NULL);
		CHECK(clk_get_rate(CLOCK(A733_CLK_GPU0)) == 0);
		CHECK(clk_enable(CLOCK(A733_CLK_GPU0)) == ENXIO);
	}
	REG(NPU_CLK_REG) = 7 << 24;
	CHECK(clk_get_parent(CLOCK(A733_CLK_NPU)) == NULL);
	CHECK(clk_enable(CLOCK(A733_CLK_NPU)) == ENXIO);
	CHECK(writes == 0);
	for (u_int sel = 4; sel < 7; sel++) {
		REG(NPU_CLK_REG) = sel << 24;
		CHECK(clk_enable(CLOCK(A733_CLK_NPU)) == ENXIO);
		CHECK(clk_set_rate(CLOCK(A733_CLK_NPU), 100000000) == ENXIO);
	}
	CHECK(writes == 0);

	REG(GPU0_CLK_REG) = 0x00500000;
	CHECK(sunxi_ccu_clock_set_parent(&state, CLOCK(A733_CLK_GPU0),
	    CLOCK(A733_CLK_PLL_PERIPH0_800M)) == 0);
	CHECK((last_write & GPU_CLK_UPDATE) != 0);
	CHECK(REG(GPU0_CLK_REG) == 0x01500000);
	for (u_int i = 0; i < __arraycount(divs); i++) {
		CHECK(clk_set_rate(CLOCK(A733_CLK_GPU0), 800000000 / divs[i]) == 0);
		CHECK((REG(GPU0_CLK_REG) & 15) == raw[i]);
		CHECK(clk_enable(CLOCK(A733_CLK_GPU0)) == 0);
		CHECK(clk_get_rate(CLOCK(A733_CLK_GPU0)) == 800000000 / divs[i]);
		CHECK(clk_set_rate(CLOCK(A733_CLK_GPU0), 800000000 / divs[i]) == 0);
		CHECK(sunxi_ccu_clock_disable(&state, CLOCK(A733_CLK_GPU0)) == 0);
	}
	before = writes;
	CHECK(clk_set_rate(CLOCK(A733_CLK_GPU0), 0) == EINVAL);
	CHECK(clk_set_rate(CLOCK(A733_CLK_GPU0), 123456789) == ERANGE);
	CHECK(clk_set_rate(CLOCK(A733_CLK_GPU0), UINT_MAX) == ERANGE);
	CHECK(writes == before);
	CHECK(clk_enable(CLOCK(A733_CLK_GPU0)) == 0);
	before = writes;
	CHECK(clk_set_rate(CLOCK(A733_CLK_GPU0), 400000000) == EBUSY);
	CHECK(sunxi_ccu_clock_set_parent(&state, CLOCK(A733_CLK_GPU0),
	    CLOCK(A733_CLK_PLL_PERIPH0_600M)) == EBUSY);
	CHECK(writes == before);
	/* Firmware may use fractional GPU M: report it without integer rounding. */
	REG(GPU0_CLK_REG) = __BIT(31) | (1 << 24) | 3;
	CHECK(clk_get_rate(CLOCK(A733_CLK_GPU0)) == 650000000);

	REG(NPU_CLK_REG) = 0x00500000 | (2 << 24);
	for (u_int m = 1; m <= 32; m *= 2) {
		CHECK(clk_set_rate(CLOCK(A733_CLK_NPU), 600000000 / m) == 0);
		CHECK((REG(NPU_CLK_REG) & 31) == m - 1);
		CHECK((REG(NPU_CLK_REG) & ~31U) == 0x02500000);
		CHECK(clk_enable(CLOCK(A733_CLK_NPU)) == 0);
		CHECK(clk_get_rate(CLOCK(A733_CLK_NPU)) == 600000000 / m);
		CHECK(sunxi_ccu_clock_disable(&state, CLOCK(A733_CLK_NPU)) == 0);
	}
	reject_write = true;
	CHECK(clk_set_rate(CLOCK(A733_CLK_NPU), 300000000) == EIO);
	CHECK(clk_enable(CLOCK(A733_CLK_BUS_NPU)) == EIO);
	reject_write = false;

	reset();
	val = ACCEL_PLL_ENABLE | ACCEL_PLL_LDO | ACCEL_PLL_LOCK_ENABLE |
	    ACCEL_PLL_LOCK | ACCEL_PLL_OUTPUT | (99 << 8) | (1 << 20);
	for (u_int id = A733_CLK_PLL_GPU0; id <= A733_CLK_PLL_NPU;
	    id += A733_CLK_PLL_NPU - A733_CLK_PLL_GPU0) {
		bus_size_t r = state.sc_clks[id].u.nkmp.reg;
		REG(r) = val;
		CHECK(clk_get_rate(CLOCK(id)) == 1200000000);
		CHECK(clk_enable(CLOCK(id)) == 0);
		REG(r) |= ACCEL_PLL_P;
		CHECK(clk_get_rate(CLOCK(id)) == 600000000);
		CHECK(clk_set_rate(CLOCK(id), 300000000) == ENXIO);
		CHECK(sunxi_ccu_clock_disable(&state, CLOCK(id)) == EBUSY);
		for (u_int bit = 27; bit <= 31; bit++) {
			REG(r) = val & ~__BIT(bit);
			CHECK(clk_get_rate(CLOCK(id)) == 0);
			CHECK(clk_enable(CLOCK(id)) == ENXIO);
		}
		REG(r) = val;
		REG(r + 8) = __BIT(31);
		CHECK(clk_enable(CLOCK(id)) == ENXIO);
		REG(r + 8) = 0;
		REG(r + 12) = __BIT(27);
		CHECK(clk_enable(CLOCK(id)) == ENXIO);
		REG(r + 12) = 0;
		REG(r) = val & ~ACCEL_PLL_N;
		CHECK(clk_enable(CLOCK(id)) == ENXIO);
		REG(r) = val;
		missing_hosc = true;
		CHECK(clk_enable(CLOCK(id)) == ENXIO);
		missing_hosc = false;
	}
	CHECK(writes == 0 && barriers == 0);
	/* PWM follows raw DCXO; the old fixed 24 MHz provider is unrelated. */
	reset();
	for (u_int rate = 19200000; rate <= 26000000;
	    rate = rate == 19200000 ? 24000000 : rate + 2000000) {
		dcxo_rate = rate;
		dcxo_queries = 0;
		CHECK(clk_get_rate(CLOCK(A733_CLK_BUS_PWM0)) == rate);
		CHECK(hosc_rate == 24000000 && writes == 0);
	}
	dcxo_queries = 0; dcxo_error = EBUSY;
	CHECK(clk_get_rate(CLOCK(A733_CLK_BUS_PWM0)) == 0);
	dcxo_error = 0; dcxo_len = 8;
	CHECK(clk_get_rate(CLOCK(A733_CLK_BUS_PWM0)) == 0);
	dcxo_len = 4; bad_dcxo = true;
	CHECK(clk_get_rate(CLOCK(A733_CLK_BUS_PWM0)) == 0);
	bad_dcxo = false; dcxo_node = 0;
	CHECK(clk_get_rate(CLOCK(A733_CLK_BUS_PWM0)) == 0);
	reset();
	REG(PWM0_BGR_REG) = 0xa5a400a4;
	CHECK(clk_enable(CLOCK(A733_CLK_BUS_PWM0)) == 0);
	CHECK(REG(PWM0_BGR_REG) == 0xa5a400a5);
	CHECK(sunxi_ccu_reset_deassert(&state,
	    &state.sc_resets[A733_RST_BUS_PWM0]) == 0);
	CHECK(REG(PWM0_BGR_REG) == 0xa5a500a5);
	test_gpu_ready();

	test_gpu_inspect();
	test_dcxo();
	test_periph_gates();
	test_gpu_prepare();
	test_gpu_update_completion();
	test_gpu_prepare_pll();
	printf("A733 accelerator clocks: %u production checks passed\n", checks);
	return 0;
}
