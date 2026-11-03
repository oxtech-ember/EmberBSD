/*-
 * Copyright (c) 2026 Anton and EmberBSD contributors
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _SUN50I_A100_CCU_GPU_H
#define _SUN50I_A100_CCU_GPU_H

/* Origin: EmberBSD; native GPU clock checks and A133 lock detection. */

/* A133 User Manual V1.1, PLL_GPU_CTRL. The PLL is firmware-owned here. */
#define A100_GPU_PLL_READY_MASK \
	(__BIT(31) | __BIT(29) | __BIT(28) | __BIT(27))
#define A100_GPU_PLL_N_FIELD_MIN	11
#define A100_GPU_PLL_N_FIELD_MAX	254

static bool
sun50i_a100_ccu_gpu_pll_ready(uint32_t val)
{
	const u_int n = __SHIFTOUT(val, __BITS(15,8));

	return (val & A100_GPU_PLL_READY_MASK) == A100_GPU_PLL_READY_MASK &&
	    n >= A100_GPU_PLL_N_FIELD_MIN && n <= A100_GPU_PLL_N_FIELD_MAX;
}

static int
sun50i_a100_ccu_gpu_pll_enable(struct sunxi_ccu_softc *sc,
    struct sunxi_ccu_clk *clk, int enable)
{
	const uint32_t val = CCU_READ(sc, clk->u.nkmp.reg);

	/* We may observe firmware setup; we do not invent PLL sequencing. */
	if (enable && sun50i_a100_ccu_gpu_pll_ready(val))
		return 0;
	return EOPNOTSUPP;
}

/*
 * A133 keeps PLL power, output and frequency under the boot setup.
 * Enable only the lock detector. Do not start or retune a PLL here.
 */
static int
sun50i_a133_ccu_gpu_pll_enable(struct sunxi_ccu_softc *sc,
    struct sunxi_ccu_clk *clk, int enable)
{
	const uint32_t detector = __BIT(29), status = __BIT(28);
	const uint32_t original = CCU_READ(sc, clk->u.nkmp.reg);
	uint32_t value;
	unsigned retry;

	if (!enable)
		return EOPNOTSUPP;
	if (sun50i_a100_ccu_gpu_pll_ready(original))
		return 0;
	if (!sun50i_a100_ccu_gpu_pll_ready(original | detector | status))
		return EOPNOTSUPP;
	if ((original & detector) == 0)
		CCU_WRITE(sc, clk->u.nkmp.reg, original | detector);
	for (retry = 0; retry < 5000; retry++) {
		value = CCU_READ(sc, clk->u.nkmp.reg);
		if (((value ^ original) & ~(detector | status)) != 0)
			return EIO;
		if (sun50i_a100_ccu_gpu_pll_ready(value)) {
			delay(20);
			value = CCU_READ(sc, clk->u.nkmp.reg);
			if (((value ^ original) & ~(detector | status)) != 0 ||
			    !sun50i_a100_ccu_gpu_pll_ready(value))
				return EIO;
			return 0;
		}
		delay(1);
	}
	/*
	 * Restore only our detector bit, and only while other fields match.
	 * An external change is not permission to restore an old frequency.
	 */
	if ((original & detector) == 0) {
		value = CCU_READ(sc, clk->u.nkmp.reg);
		if (((value ^ original) & ~(detector | status)) != 0)
			return EIO;
		if ((value & detector) != 0)
			CCU_WRITE(sc, clk->u.nkmp.reg, value & ~detector);
	}
	return ETIMEDOUT;
}

static u_int
sun50i_a100_ccu_gpu_pll_get_rate(struct sunxi_ccu_softc *sc,
    struct sunxi_ccu_clk *clk)
{
	const struct sunxi_ccu_nkmp *nkmp = &clk->u.nkmp;
	struct clk *parent = clk_get_parent(&clk->base);
	uint32_t val;
	uint64_t rate;
	u_int n, m, p;

	if (parent == NULL)
		return 0;
	val = CCU_READ(sc, nkmp->reg);
	if (!sun50i_a100_ccu_gpu_pll_ready(val))
		return 0;

	n = __SHIFTOUT(val, nkmp->n) + 1;
	m = __SHIFTOUT(val, nkmp->m) + 1;
	p = __SHIFTOUT(val, nkmp->p) + 1;
	if (m == 0 || p == 0)
		return 0;

	/* Divide before narrowing: intermediate products can exceed u_int. */
	rate = (uint64_t)clk_get_rate(parent) * n;
	rate /= m;
	rate /= p;
	return rate > UINT_MAX ? 0 : (u_int)rate;
}

static int
sun50i_a100_ccu_gpu_pll_set_rate(struct sunxi_ccu_softc *sc,
    struct sunxi_ccu_clk *clk, u_int rate)
{
	return EOPNOTSUPP;
}

/* A133 fixed 432-to-504 MHz PLL transition, with power/output preserved. */
static int
sun50i_a133_ccu_gpu_pll_set_rate(struct sunxi_ccu_softc *sc,
    struct sunxi_ccu_clk *clk, u_int rate)
{
	const struct sunxi_ccu_nkmp *nkmp = &clk->u.nkmp;
	const uint32_t detector = __BIT(29), lock = __BIT(28);
	const uint32_t factors = __BITS(15,8) | __BIT(1) | __BIT(0);
	struct clk *parent = clk_get_parent(&clk->base);
	uint32_t original, value, target;

	if (rate != 504000000 || parent == NULL || clk_get_rate(parent) != 24000000)
		return EOPNOTSUPP;
	original = CCU_READ(sc, nkmp->reg);
	if (!sun50i_a100_ccu_gpu_pll_ready(original))
		return EOPNOTSUPP;
	if ((original & factors) == ((41U << 8) | 1U))
		return 0;
	if ((original & factors) != ((35U << 8) | 1U))
		return EOPNOTSUPP;
	/* Do not change PLL power/output; no rollback after an uncertain write. */
	CCU_WRITE(sc, nkmp->reg, original & ~detector);
	value = CCU_READ(sc, nkmp->reg);
	if (((value ^ original) & ~(detector | lock)) != 0 ||
	    (value & detector) != 0)
		return EIO;
	target = (value & ~__BITS(15,8)) | (41U << 8);
	CCU_WRITE(sc, nkmp->reg, target);
	value = CCU_READ(sc, nkmp->reg);
	if (((value ^ target) & ~lock) != 0)
		return EIO;
	CCU_WRITE(sc, nkmp->reg, value | detector);
	for (unsigned i = 0; i < 5000; i++) {
		value = CCU_READ(sc, nkmp->reg);
		if (((value ^ (target | detector)) & ~lock) != 0)
			return EIO;
		if ((value & lock) != 0) {
			delay(20);
			value = CCU_READ(sc, nkmp->reg);
			if (((value ^ (target | detector)) & ~lock) != 0 ||
			    (value & lock) == 0)
				return EIO;
			return 0;
		}
		delay(1);
	}
	return ETIMEDOUT;
}

static const char *
sun50i_a100_ccu_gpu_pll_get_parent(struct sunxi_ccu_softc *sc,
    struct sunxi_ccu_clk *clk)
{
	return clk->u.nkmp.parent;
}

static const char *
sun50i_a133_ccu_gpu_core_get_parent(struct sunxi_ccu_softc *sc,
    struct sunxi_ccu_clk *clk)
{
	const struct sunxi_ccu_div *div = &clk->u.div;
	u_int index;

	if (div->sel == 0)
		return NULL;
	index = __SHIFTOUT(CCU_READ(sc, div->reg), div->sel);
	if (index >= div->nparents)
		return NULL;
	return div->parents[index];
}

static u_int
sun50i_a133_ccu_gpu_core_get_rate(struct sunxi_ccu_softc *sc,
    struct sunxi_ccu_clk *clk)
{
	/* Only PLL_GPU0 is wired on this provider; reject reserved selector 1. */
	if (sun50i_a133_ccu_gpu_core_get_parent(sc, clk) == NULL)
		return 0;
	return sunxi_ccu_div_get_rate(sc, clk);
}

static int
sun50i_a133_ccu_gpu_core_enable(struct sunxi_ccu_softc *sc,
    struct sunxi_ccu_clk *clk, int enable)
{
	if (sun50i_a133_ccu_gpu_core_get_parent(sc, clk) == NULL)
		return EINVAL;
	return sunxi_ccu_div_enable(sc, clk, enable);
}

static int
sun50i_a133_ccu_gpu_core_set_rate(struct sunxi_ccu_softc *sc,
    struct sunxi_ccu_clk *clk, u_int rate)
{
	struct clk *parent;
	u_int parent_rate;

	if (sun50i_a133_ccu_gpu_core_get_parent(sc, clk) == NULL)
		return EINVAL;
	if (rate == 0)
		return EINVAL;
	parent = clk_get_parent(&clk->base);
	if (parent == NULL)
		return ENXIO;
	parent_rate = clk_get_rate(parent);
	/* The generic DIV helper uses signed int and howmany arithmetic. */
	if (parent_rate == 0)
		return ENXIO;
	if (parent_rate > INT_MAX || rate > parent_rate)
		return ERANGE;
	return sunxi_ccu_div_set_rate(sc, clk, rate);
}

static int
sun50i_a133_ccu_gpu_core_set_parent(struct sunxi_ccu_softc *sc,
    struct sunxi_ccu_clk *clk, const char *name)
{
	if (sun50i_a133_ccu_gpu_core_get_parent(sc, clk) == NULL)
		return EINVAL;
	return sunxi_ccu_div_set_parent(sc, clk, name);
}

#endif /* !_SUN50I_A100_CCU_GPU_H */
