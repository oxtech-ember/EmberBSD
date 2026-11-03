/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD; checks for the native A133 PLL lock-detector callback. */
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef unsigned int u_int;
#define __BIT(n) (UINT32_C(1) << (n))
#define __BITS(h, l) ((UINT32_MAX >> (31 - (h))) & (UINT32_MAX << (l)))
#define __SHIFTOUT(v, m) (((v) & (m)) / ((m) & (0U - (m))))

struct clk {
	struct clk *parent;
	u_int rate;
};
struct sunxi_ccu_nkmp {
	unsigned reg, n, m, p;
	const char *parent;
};
struct sunxi_ccu_div {
	unsigned reg, sel, nparents;
	const char **parents;
};
struct sunxi_ccu_clk {
	struct clk base;
	union {
		struct sunxi_ccu_nkmp nkmp;
		struct sunxi_ccu_div div;
	} u;
};
struct sunxi_ccu_softc {
	uint32_t value;
	unsigned reads, writes, lock_after, drift_at;
};
static unsigned elapsed_us;

static uint32_t
read_control(struct sunxi_ccu_softc *sc, unsigned reg)
{

	assert(reg == 0x30);
	sc->reads++;
	if (sc->lock_after != 0 && sc->reads >= sc->lock_after &&
	    (sc->value & __BIT(29)) != 0)
		sc->value |= __BIT(28);
	if (sc->drift_at != 0 && sc->reads == sc->drift_at)
		sc->value ^= __BIT(8);
	return sc->value;
}

static void
write_control(struct sunxi_ccu_softc *sc, unsigned reg, uint32_t value)
{

	assert(reg == 0x30);
	/* The detector is the only writable field owned by this operation. */
	assert(((value ^ sc->value) & ~(__BIT(29) | __BIT(28))) == 0);
	sc->value = (value & ~__BIT(28)) | (sc->value & __BIT(28));
	sc->writes++;
}

static void
delay(unsigned us)
{

	assert(us == 1 || us == 20);
	elapsed_us += us;
}

#define CCU_READ(sc, reg) read_control((sc), (reg))
#define CCU_WRITE(sc, reg, v) write_control((sc), (reg), (v))
static struct clk *clk_get_parent(struct clk *clk) { return clk->parent; }
static u_int clk_get_rate(struct clk *clk) { return clk->rate; }
/* Other callbacks in the production header are outside this fixture. */
u_int sunxi_ccu_div_get_rate(struct sunxi_ccu_softc *, struct sunxi_ccu_clk *);
int sunxi_ccu_div_enable(struct sunxi_ccu_softc *, struct sunxi_ccu_clk *, int);
int sunxi_ccu_div_set_rate(struct sunxi_ccu_softc *, struct sunxi_ccu_clk *, u_int);
int sunxi_ccu_div_set_parent(struct sunxi_ccu_softc *, struct sunxi_ccu_clk *, const char *);

#include "../sun50i_a100_ccu_gpu.h"

int
main(void)
{
	const uint32_t initial = 0x88002301;
	struct clk hosc = { .rate = 24000000 };
	struct sunxi_ccu_clk pll = {
		.base.parent = &hosc,
		.u.nkmp = { .reg = 0x30, .n = __BITS(15, 8),
		    .m = __BIT(1), .p = __BIT(0), .parent = "hosc" }
	};
	struct sunxi_ccu_softc sc = { .value = initial, .lock_after = 4 };

	assert(sun50i_a133_ccu_gpu_pll_enable(&sc, &pll, 1) == 0);
	assert(sc.writes == 1 && elapsed_us >= 20 && elapsed_us < 5020);
	assert((sc.value & ~(__BIT(29) | __BIT(28))) == initial);
	assert(sun50i_a100_ccu_gpu_pll_get_rate(&sc, &pll) == 432000000);
	assert(sun50i_a133_ccu_gpu_pll_enable(&sc, &pll, 1) == 0 && sc.writes == 1);
	assert(sun50i_a133_ccu_gpu_pll_enable(&sc, &pll, 0) == EOPNOTSUPP);
	assert(sc.writes == 1);

	/* Timeout restores only a detector acquired by this call. */
	sc = (struct sunxi_ccu_softc) { .value = initial };
	elapsed_us = 0;
	assert(sun50i_a133_ccu_gpu_pll_enable(&sc, &pll, 1) == ETIMEDOUT);
	assert(sc.value == initial && sc.writes == 2 && elapsed_us == 5000);
	sc = (struct sunxi_ccu_softc) { .value = initial | __BIT(29) };
	assert(sun50i_a133_ccu_gpu_pll_enable(&sc, &pll, 1) == ETIMEDOUT);
	assert(sc.value == (initial | __BIT(29)) && sc.writes == 0);

	/* Do not restore an old frequency after an external change. */
	sc = (struct sunxi_ccu_softc) { .value = initial, .drift_at = 4 };
	assert(sun50i_a133_ccu_gpu_pll_enable(&sc, &pll, 1) == EIO);
	assert((sc.value & __BITS(15, 8)) != (initial & __BITS(15, 8)));
	assert(sc.writes == 1);

	/* Disabled rails/output and unsupported factors are not bootstrapped. */
	for (unsigned i = 0; i < 4; i++) {
		sc = (struct sunxi_ccu_softc) { .value = initial };
		if (i == 0) sc.value &= ~__BIT(31);
		if (i == 1) sc.value &= ~__BIT(27);
		if (i == 2) sc.value = (initial & ~__BITS(15, 8)) | (10U << 8);
		if (i == 3) sc.value = (initial & ~__BITS(15, 8)) | (255U << 8);
		assert(sun50i_a133_ccu_gpu_pll_enable(&sc, &pll, 1) == EOPNOTSUPP);
		assert(sc.writes == 0);
	}
	/* A100 retains the earlier observation-only callback. */
	sc = (struct sunxi_ccu_softc) { .value = initial };
	assert(sun50i_a100_ccu_gpu_pll_enable(&sc, &pll, 1) == EOPNOTSUPP);
	assert(sc.writes == 0);
	puts("PASS: A133 PLL detector ownership, lock timeout and frequency preservation");
	return 0;
}
