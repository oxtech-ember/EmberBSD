/* Origin: EmberBSD; execute production A100/A133 GPU CCU callbacks. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned int u_int;
typedef size_t bus_size_t;
struct clk { const char *name; u_int rate; };
struct sunxi_ccu_softc;
typedef struct sunxi_ccu_softc *device_t;
struct sunxi_ccu_gate { bus_size_t reg; uint32_t mask; const char *parent; };
struct sunxi_ccu_reset { bus_size_t reg; uint32_t mask; };
struct sunxi_ccu_nkmp {
	bus_size_t reg;
	const char *parent;
	uint32_t n, k, m, p, lock, enable, flags;
};
struct sunxi_ccu_div {
	bus_size_t reg;
	const char **parents;
	u_int nparents;
	uint32_t div, sel, enable, flags;
};
struct sunxi_ccu_clk {
	struct clk base;
	int type;
	union {
		struct sunxi_ccu_gate gate;
		struct sunxi_ccu_div div;
		struct sunxi_ccu_nkmp nkmp;
	} u;
};
struct sunxi_ccu_softc {
	uint32_t regs[0x800 / 4];
	unsigned writes;
	int (*sc_update_bits)(struct sunxi_ccu_softc *, bus_size_t, uint32_t,
	    uint32_t);
};

#define SUNXI_CCU_GATE 1
#define SUNXI_CCU_DIV 2
#define SUNXI_CCU_DIV_POWER_OF_TWO __BIT(0)
#define SUNXI_CCU_DIV_ZERO_IS_ONE __BIT(1)
#define SUNXI_CCU_DIV_TIMES_TWO __BIT(2)
#define SUNXI_CCU_DIV_SET_RATE_PARENT __BIT(3)
#define KASSERT(c) assert(c)
#define device_private(dev) (dev)
#define __BIT(n) (UINT32_C(1) << (n))
#define __BITS(h, l) ((UINT32_MAX >> (31 - (h))) & (UINT32_MAX << (l)))
#define __SHIFTOUT(v, mask) (((v) & (mask)) >> __builtin_ctz(mask))
#define __SHIFTIN(v, mask) (((v) << __builtin_ctz(mask)) & (mask))
#define __SHIFTOUT_MASK(mask) __SHIFTOUT(mask, mask)
#define howmany(x, y) (((x) + ((y) - 1)) / (y))
#define CCU_READ(sc, reg) ((sc)->regs[(reg) / sizeof(uint32_t)])
#define CCU_WRITE(sc, reg, val) do { \
	(sc)->regs[(reg) / sizeof(uint32_t)] = (val); (sc)->writes++; \
} while (0)

static struct clk *
clk_get_parent(struct clk *clk)
{
	return clk->rate == 0 ? NULL : clk;
}

static u_int
clk_get_rate(struct clk *clk)
{
	return clk->rate;
}

static int
clk_set_parent(struct clk *clk, struct clk *parent)
{
	return EOPNOTSUPP;
}

static int
clk_set_rate(struct clk *clk, u_int rate)
{
	return EOPNOTSUPP;
}

static struct sunxi_ccu_clk *
sunxi_ccu_clock_find(struct sunxi_ccu_softc *sc, const char *name)
{
	return NULL;
}

#include "ccu-rmw.h"
#include "ccu-div.h"
#include "ccu-gate.h"
#include <arm/sunxi/sun50i_a100_ccu_gpu.h>
#include <arm/sunxi/sun50i_a100_ccu.h>
#include <dt-bindings/clock/sun50i-a100-ccu.h>
#include <dt-bindings/reset/sun50i-a100-ccu.h>

_Static_assert(A100_CLK_PLL_GPU == 7, "internal GPU PLL index drift");
_Static_assert(A100_CLK_GPU == CLK_GPU, "GPU clock binding drift");
_Static_assert(A100_CLK_BUS_GPU == CLK_BUS_GPU, "GPU bus binding drift");
_Static_assert(A100_RST_BUS_GPU == RST_BUS_GPU, "GPU reset binding drift");

static void
check(bool condition, const char *name)
{
	if (!condition) {
		fprintf(stderr, "FAIL: %s\n", name);
		exit(1);
	}
}

static void
test_pll_observation(void)
{
	struct sunxi_ccu_softc sc = {0};
	struct sunxi_ccu_clk pll = { .base = { .rate = 24000000 } };
	const uint32_t ready = __BIT(31) | __BIT(29) | __BIT(28) | __BIT(27);
	pll.u.nkmp.reg = 0x30;
	pll.u.nkmp.parent = "hosc";
	pll.u.nkmp.n = __BITS(15,8);
	pll.u.nkmp.m = __BIT(1);
	pll.u.nkmp.p = __BIT(0);

	sc.regs[0x30 / 4] = ready | (17U << 8);
	check(sun50i_a100_ccu_gpu_pll_get_rate(&sc, &pll) == 432000000,
	    "PLL field 17 gives 432 MHz");
	check(sun50i_a100_ccu_gpu_pll_get_parent(&sc, &pll) ==
	    pll.u.nkmp.parent, "PLL reports its hosc parent");
	check(sun50i_a100_ccu_gpu_pll_enable(&sc, &pll, 1) == 0,
	    "ready firmware-owned PLL enable is observational");
	check(sun50i_a100_ccu_gpu_pll_enable(&sc, &pll, 0) == EOPNOTSUPP,
	    "PLL disable is unsupported");
	check(sun50i_a100_ccu_gpu_pll_set_rate(&sc, &pll, 500000000) ==
	    EOPNOTSUPP, "PLL rate mutation is unsupported");
	check(sc.writes == 0, "PLL callbacks never write MMIO");

	for (unsigned bit = 0; bit < 4; bit++) {
		const unsigned ready_bit[] = {31, 29, 28, 27};
		sc.regs[0x30 / 4] = (ready | (17U << 8)) & ~__BIT(ready_bit[bit]);
		check(sun50i_a100_ccu_gpu_pll_get_rate(&sc, &pll) == 0,
		    "each readiness bit is required");
		check(sun50i_a100_ccu_gpu_pll_enable(&sc, &pll, 1) == EOPNOTSUPP,
		    "incomplete PLL readiness refuses enable");
	}

	/* Manual: PLL_FACTOR_N is 0..254, and application value must be >=11. */
	sc.regs[0x30 / 4] = ready | (10U << 8);
	check(sun50i_a100_ccu_gpu_pll_get_rate(&sc, &pll) == 0,
	    "PLL_FACTOR_N 10 is below the application minimum");
	sc.regs[0x30 / 4] = ready | (11U << 8);
	check(sun50i_a100_ccu_gpu_pll_get_rate(&sc, &pll) == 288000000,
	    "PLL_FACTOR_N 11 is the inclusive minimum");
	sc.regs[0x30 / 4] = ready | (255U << 8);
	check(sun50i_a100_ccu_gpu_pll_get_rate(&sc, &pll) == 0,
	    "PLL_FACTOR_N 255 is outside the manual range");

	/* Wide numerator must be divided before narrowing to the public u_int. */
	sc.regs[0x30 / 4] = ready | (254U << 8) | __BIT(1) | __BIT(0);
	check(sun50i_a100_ccu_gpu_pll_get_rate(&sc, &pll) == 1530000000U,
	    "64-bit intermediate computes the high-N divided rate");
	check(sc.writes == 0, "all PLL paths remain read-only");
}

static void
test_gpu_divider(void)
{
	struct sunxi_ccu_softc sc = {0};
	struct sunxi_ccu_clk core = { .base = { .rate = 800000000 },
	    .type = SUNXI_CCU_DIV };
	static const char *parents[] = { "pll_gpu" };
	struct sunxi_ccu_div *div = &core.u.div;
	div->reg = 0x670;
	div->div = __BITS(2,0);
	div->sel = __BIT(24);
	div->enable = __BIT(31);
	div->parents = parents;
	div->nparents = 1;

	sc.regs[0x670 / 4] = __BIT(31) | 2;
	check(sun50i_a133_ccu_gpu_core_get_rate(&sc, &core) == 266666666,
	    "A133 3-bit divider reads divisor 3");
	check(strcmp(sun50i_a133_ccu_gpu_core_get_parent(&sc, &core), "pll_gpu") == 0,
	    "valid selector resolves the PLL parent");

	/* Existing DIV provider code must constrain mutations to its divider field. */
	sc.regs[0x670 / 4] = __BIT(31) | __BIT(20) | 7;
	check(sun50i_a133_ccu_gpu_core_set_rate(&sc, &core, 100000000) == 0,
	    "A133 divider set-rate succeeds");
	check(sun50i_a133_ccu_gpu_core_get_rate(&sc, &core) == 100000000,
	    "A133 3-bit divider sets /8");
	check((sc.regs[0x670 / 4] & (__BIT(31) | __BIT(20) | div->div)) ==
	    (__BIT(31) | __BIT(20) | 7), "divider update preserves neighboring bits");
	check(sun50i_a133_ccu_gpu_core_set_parent(&sc, &core, "bad-parent") ==
	    EINVAL, "unsupported parent name is rejected");
	check(sun50i_a133_ccu_gpu_core_set_parent(&sc, &core, "pll_gpu") == 0,
	    "valid parent selection dispatches to the production DIV helper");

	const unsigned before_invalid_rates = sc.writes;
	check(sun50i_a133_ccu_gpu_core_set_rate(&sc, &core, 0) == EINVAL,
	    "zero requested rate is rejected before generic DIV math");
	check(sun50i_a133_ccu_gpu_core_set_rate(&sc, &core, UINT_MAX) == ERANGE,
	    "rate above parent maximum is rejected");
	check(sun50i_a133_ccu_gpu_core_set_rate(&sc, &core, 1) == ERANGE,
	    "unrepresentable low rate is rejected by production DIV helper");
	check(sc.writes == before_invalid_rates,
	    "invalid rates leave divider MMIO unchanged");

	core.base.rate = (u_int)INT_MAX + 1U;
	check(sun50i_a133_ccu_gpu_core_set_rate(&sc, &core, 100000000) == ERANGE,
	    "parent rate outside generic signed arithmetic is rejected");
	core.base.rate = 0;
	check(sun50i_a133_ccu_gpu_core_set_rate(&sc, &core, 100000000) == ENXIO,
	    "missing parent rate is reported");
	check(sc.writes == before_invalid_rates,
	    "unsupported parent ranges leave divider MMIO unchanged");
	core.base.rate = 800000000;

	const unsigned before_bad_parent = sc.writes;
	sc.regs[0x670 / 4] |= __BIT(24);
	check(sun50i_a133_ccu_gpu_core_get_parent(&sc, &core) == NULL,
	    "reserved selector is rejected without indexing the parent table");
	check(sun50i_a133_ccu_gpu_core_get_rate(&sc, &core) == 0,
	    "reserved selector has no reported rate");
	check(sun50i_a133_ccu_gpu_core_enable(&sc, &core, 1) == EINVAL,
	    "reserved selector refuses gate mutation");
	check(sun50i_a133_ccu_gpu_core_set_rate(&sc, &core, 100000000) == EINVAL,
	    "reserved selector refuses divider mutation");
	check(sun50i_a133_ccu_gpu_core_set_parent(&sc, &core, "pll_gpu") == EINVAL,
	    "reserved selector refuses parent repair writes");
	check(sc.writes == before_bad_parent,
	    "all reserved-selector paths leave MMIO unchanged");

	/* A100 has the same provider but keeps its 2-bit divider mask. */
	div->div = __BITS(1,0);
	sc.regs[0x670 / 4] = __BIT(31) | 3;
	check(sun50i_a133_ccu_gpu_core_get_rate(&sc, &core) == 200000000,
	    "A100 descriptor retains the /4 limit");
	check(sun50i_a133_ccu_gpu_core_enable(&sc, &core, 0) == 0,
	    "production DIV enable callback disables the functional gate");
	check((sc.regs[0x670 / 4] & __BIT(31)) == 0 &&
	    (sc.regs[0x670 / 4] & 3) == 3,
	    "functional gate update preserves divider bits");
}

static void
test_gpu_bus_gate_and_reset(void)
{
	struct sunxi_ccu_softc sc = {0};
	struct sunxi_ccu_clk gate = { .type = SUNXI_CCU_GATE };
	struct sunxi_ccu_reset reset = { .reg = 0x67c, .mask = __BIT(16) };
	const uint32_t initial = UINT32_C(0xa55a1234);

	gate.u.gate.reg = 0x67c;
	gate.u.gate.mask = __BIT(0);
	sc.regs[0x67c / 4] = initial;
	check(sunxi_ccu_gate_enable(&sc, &gate, 1) == 0,
	    "production GPU bus gate enables");
	check(sc.regs[0x67c / 4] == (initial | __BIT(0)),
	    "GPU bus gate preserves neighboring bits");
	check(sunxi_ccu_reset_deassert_unguarded(&sc, &reset) == 0,
	    "production GPU reset deasserts");
	check(sc.regs[0x67c / 4] == (initial | __BIT(0) | __BIT(16)),
	    "GPU reset deassert preserves neighboring bits");
	check(sunxi_ccu_reset_assert_unguarded(&sc, &reset) == 0,
	    "production GPU reset asserts");
	check(sc.regs[0x67c / 4] == (initial | __BIT(0)),
	    "GPU reset assert preserves neighboring bits");
}

int
main(void)
{
	test_pll_observation();
	test_gpu_divider();
	test_gpu_bus_gate_and_reset();
	puts("A100/A133 GPU CCU production contract: PASS");
	return 0;
}
