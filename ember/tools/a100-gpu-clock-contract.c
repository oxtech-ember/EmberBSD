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
struct clk { const char *name; u_int rate; uint32_t flags; };
struct sunxi_ccu_softc;
struct sunxi_ccu_clk;
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
	int (*enable)(struct sunxi_ccu_softc *, struct sunxi_ccu_clk *, int);
	const char *(*get_parent)(struct sunxi_ccu_softc *, struct sunxi_ccu_clk *);
	u_int (*get_rate)(struct sunxi_ccu_softc *, struct sunxi_ccu_clk *);
	int (*set_rate)(struct sunxi_ccu_softc *, struct sunxi_ccu_clk *, u_int);
	int (*set_parent)(struct sunxi_ccu_softc *, struct sunxi_ccu_clk *,
	    const char *);
};
struct sunxi_ccu_softc {
	uint32_t regs[0x800 / 4];
	unsigned writes;
	int (*sc_update_bits)(struct sunxi_ccu_softc *, bus_size_t, uint32_t,
	    uint32_t);
	void *sc_dev;
	int sc_phandle, sc_bst;
	struct sunxi_ccu_reset *sc_resets;
	u_int sc_nresets;
	struct sunxi_ccu_clk *sc_clks;
	u_int sc_nclks;
};

enum {
	SUNXI_CCU_GATE = 1,
	SUNXI_CCU_DIV = 2,
};
#define CLK_SET_RATE_PARENT __BIT(0)
#define SUNXI_CCU_DIV_POWER_OF_TWO __BIT(0)
#define SUNXI_CCU_DIV_ZERO_IS_ONE __BIT(1)
#define SUNXI_CCU_DIV_TIMES_TWO __BIT(2)
#define SUNXI_CCU_DIV_SET_RATE_PARENT __BIT(3)
#define KASSERT(c) assert(c)
#define device_private(dev) (dev)
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#define KM_NOSLEEP 0
#define CFATTACH_DECL_NEW(n, s, m, a, d, r)
#define DEVICE_COMPAT_EOL { .compat = NULL }
#define aprint_error_dev(...) ((void)0)
#define aprint_naive(...) ((void)0)
#define aprint_normal(...) ((void)0)
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

typedef void *cfdata_t;
struct device_compatible_entry { const char *compat; };
struct fdt_attach_args { int faa_phandle, faa_bst; };

static void *
kmem_alloc(size_t size, int flags)
{
	return calloc(1, size);
}

static void
kmem_free(void *ptr, size_t size)
{
	free(ptr);
}

static int
of_compatible_match(int phandle, const struct device_compatible_entry *entries)
{
	const char *wanted = phandle == 133 ? "allwinner,sun50i-a133-ccu" :
	    phandle == 100 ? "allwinner,sun50i-a100-ccu" : NULL;

	if (wanted == NULL)
		return 0;
	for (; entries->compat != NULL; entries++) {
		if (strcmp(entries->compat, wanted) == 0)
			return 1;
	}
	return 0;
}

static int sunxi_ccu_attach(struct sunxi_ccu_softc *sc) { return 0; }
static void sunxi_ccu_print(struct sunxi_ccu_softc *sc) { }

/* Non-G2D clocks are retained in the production arrays but not modeled. */
#define SUNXI_CCU_FIXED_FACTOR(_id, _name, _parent, _div, _mult) [_id] = {0}
#define SUNXI_CCU_NKMP(_id, _name, _parent, _reg, _n, _k, _m, _p, \
	_enable, _flags) [_id] = {0}
#define SUNXI_CCU_NM(_id, _name, _parents, _reg, _n, _m, _sel, _enable, \
	_flags) [_id] = {0}
#define SUNXI_CCU_DIV_GATE(_id, _name, _parents, _reg, _div, _sel, _gate, \
	_flags) [_id] = {0}

#include "ccu-rmw.h"
#include "ccu-div.h"
#include "ccu-gate.h"
#include <arm/sunxi/sun50i_a100_ccu_gpu.h>
#include <arm/sunxi/sun50i_a100_ccu.h>
#include <dt-bindings/clock/sun50i-a100-ccu.h>
#include <dt-bindings/reset/sun50i-a100-ccu.h>
#include "ccu-g2d-provider.h"
#include "sun50i_a100_ccu.c"

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

static unsigned int g2d_failures;

static void
check_g2d(bool condition, const char *name)
{
	if (!condition) {
		fprintf(stderr, "FAIL: %s\n", name);
		g2d_failures++;
	}
}

static void
test_g2d_bus_clock_and_reset(void)
{
	struct sunxi_ccu_softc a100 = {0}, a133 = {0};
	struct fdt_attach_args faa = { .faa_phandle = 100, .faa_bst = 1 };
	struct sunxi_ccu_clk *gate;
	const uint32_t initial = UINT32_C(0xa55a1234);

#ifdef A100_CLK_BUS_G2D
	check_g2d(A100_CLK_BUS_G2D == CLK_BUS_G2D,
	    "A100 G2D bus clock ID matches DT binding");
#else
	check_g2d(false, "A100 G2D bus clock ID is published");
#endif
#ifdef A100_RST_BUS_G2D
	check_g2d(A100_RST_BUS_G2D == RST_BUS_G2D,
	    "A100 G2D reset ID matches DT binding");
#else
	check_g2d(false, "A100 G2D reset ID is published");
#endif
	check_g2d(sun50i_a100_ccu_match(NULL, NULL, &faa) != 0,
	    "production A100 provider matches its compatible");
	sun50i_a100_ccu_attach(NULL, &a100, &faa);
	check_g2d(a100.sc_clks != NULL,
	    "production A100 provider initializes its clock array");
	if (a100.sc_clks == NULL)
		return;
	gate = &a100.sc_clks[CLK_BUS_G2D];

	check_g2d(gate->type == SUNXI_CCU_GATE && gate->enable != NULL,
	    "production G2D bus gate descriptor exists");
	if (gate->type == SUNXI_CCU_GATE && gate->enable != NULL) {
		check_g2d(gate->u.gate.reg == 0x63c &&
		    gate->u.gate.mask == __BIT(0),
		    "production G2D bus gate uses BGR bit 0");
		check_g2d(gate->u.gate.parent != NULL &&
		    strcmp(gate->u.gate.parent, "psi") == 0,
		    "production G2D bus gate uses the existing psi parent");
		a100.regs[0x63c / 4] = initial | __BIT(16);
		check_g2d(gate->enable(&a100, gate, 1) == 0,
		    "production gate callback enables G2D bus gate");
		check_g2d(a100.regs[0x63c / 4] ==
		    (initial | __BIT(0) | __BIT(16)),
		    "G2D bus gate enable preserves reset and neighboring bits");
		check_g2d(gate->enable(&a100, gate, 1) == 0 &&
		    a100.regs[0x63c / 4] ==
		    (initial | __BIT(0) | __BIT(16)),
		    "repeated G2D bus gate enable keeps the same reset state");
		check_g2d(gate->enable(&a100, gate, 0) == 0 &&
		    a100.regs[0x63c / 4] == (initial | __BIT(16)),
		    "G2D bus gate disable preserves reset state and neighbors");
		check_g2d(gate->enable(&a100, gate, 0) == 0 &&
		    a100.regs[0x63c / 4] == (initial | __BIT(16)),
		    "repeated G2D bus gate disable keeps the same reset state");
	} else {
		check_g2d(false, "production G2D bus gate callback is present");
	}

	check_g2d(a100.sc_resets[RST_BUS_G2D].reg == 0x63c &&
	    a100.sc_resets[RST_BUS_G2D].mask == __BIT(16),
	    "production G2D reset descriptor uses BGR bit 16");
	a100.regs[0x63c / 4] = initial | __BIT(0);
	check_g2d(sunxi_ccu_reset_deassert_unguarded(&a100,
	    &a100.sc_resets[RST_BUS_G2D]) == 0,
	    "production reset callback deasserts G2D reset");
	check_g2d(a100.regs[0x63c / 4] == (initial | __BIT(0) | __BIT(16)),
	    "G2D reset deassert preserves neighboring BGR bits");
	check_g2d(sunxi_ccu_reset_assert_unguarded(&a100,
	    &a100.sc_resets[RST_BUS_G2D]) == 0,
	    "production reset callback asserts G2D reset");
	check_g2d(a100.regs[0x63c / 4] == (initial | __BIT(0)),
	    "G2D reset assert preserves neighboring BGR bits");

	/* Exercise the actual A133 attach path and cloned production array. */
	faa.faa_phandle = 133;
	check_g2d(sun50i_a100_ccu_match(NULL, NULL, &faa) != 0,
	    "production A100 provider matches the A133 compatible");
	sun50i_a100_ccu_attach(NULL, &a133, &faa);
	check_g2d(a133.sc_clks != NULL,
	    "production A133 provider initializes its cloned clock array");
	if (a133.sc_clks != NULL) {
		gate = &a133.sc_clks[CLK_BUS_G2D];
		check_g2d(a133.sc_clks != sun50i_a100_ccu_clks &&
		    gate->type == SUNXI_CCU_GATE && gate->u.gate.reg == 0x63c &&
		    gate->u.gate.mask == __BIT(0) && gate->u.gate.parent != NULL &&
		    strcmp(gate->u.gate.parent, "psi") == 0,
		    "A133 descriptor clone retains the G2D bus gate");
		if (a133.sc_clks != sun50i_a100_ccu_clks)
			free(a133.sc_clks);
	}
	check_g2d(a133.sc_resets == a100.sc_resets &&
	    a133.sc_resets[RST_BUS_G2D].reg == 0x63c &&
	    a133.sc_resets[RST_BUS_G2D].mask == __BIT(16),
	    "A133 attach shares the production G2D reset descriptor");
}

int
main(void)
{
	test_pll_observation();
	test_gpu_divider();
	test_gpu_bus_gate_and_reset();
	test_g2d_bus_clock_and_reset();
	if (g2d_failures != 0)
		return 1;
	puts("A100/A133 GPU CCU production contract: PASS");
	return 0;
}
