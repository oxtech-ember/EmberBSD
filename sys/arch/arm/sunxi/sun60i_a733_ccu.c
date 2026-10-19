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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Allwinner A733 (sun60iw2) clock control unit.
 *
 * The register map was read on an Orange Pi Zero 4 and agrees with the
 * vendor kernel. The boot loader leaves the peripheral PLL at 2400 MHz;
 * this driver reports it and never retunes it.
 *
 * Origin: EmberBSD; AI-assisted accelerator clock support, using hardware
 * facts from the Orange Pi BSP and Junhui Liu's A733 CCU v5 submission.
 * Exact revisions and conservative PLL ownership are documented in
 * ember/boot/a733-accelerator-clocks.md.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(1, "$NetBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/mutex.h>
#include <sys/systm.h>

#include <dev/fdt/fdtvar.h>

#include <arm/sunxi/sunxi_ccu.h>
#include <arm/sunxi/sun60i_a733_ccu.h>
#include <arm/sunxi/sun60i_a733_display.h>
#include <arm/sunxi/sun60i_a733_display_owner.h>
#include <arm/sunxi/sun60i_a733_pck600.h>
#include <arm/sunxi/sunxi_rtcvar.h>

#include "sunxi_rtc.h"

static const struct device_compatible_entry sun60i_a733_dcxo_compat[] = {
	{ .compat = "allwinner,sun60i-a733-rtc" },
	DEVICE_COMPAT_EOL
};

static int
sun60i_a733_dcxo_query(int phandle, struct sunxi_rtc_dcxo_state *state)
{
#if NSUNXI_RTC > 0
	return sunxi_rtc_dcxo_query(phandle, state);
#else
	return ENXIO;
#endif
}

/* Serialize physical RMW only. Never hold this over parent recursion. */
static kmutex_t sun60i_a733_ccu_lock;

#define	PLL_REF_CTRL_REG		0x000
#define	PLL_PERIPH0_CTRL_REG	0x0a0
#define	PLL_GPU0_CTRL_REG	0x0e0
#define	PLL_NPU_CTRL_REG		0x2a0
#define	ACCEL_PLL_ENABLE	__BIT(31)
#define	ACCEL_PLL_LDO		__BIT(30)
#define	ACCEL_PLL_LOCK_ENABLE	__BIT(29)
#define	ACCEL_PLL_LOCK		__BIT(28)
#define	ACCEL_PLL_OUTPUT	__BIT(27)
#define	ACCEL_PLL_N		__BITS(15,8)
#define	ACCEL_PLL_M		__BITS(22,20)
#define	ACCEL_PLL_P		__BIT(1)
#define	AHB_CFG_REG		0x500
#define	APB0_CFG_REG		0x510
#define	APB1_CFG_REG		0x518
#define	APB_UART_CFG_REG	0x538
#define	AHB_MASTER_GATE_REG	0x5c0
#define	MBUS_MASTER_GATE_REG	0x5e0
#define	MBUS_GATE_REG		0x5e4
#define	PWM0_BGR_REG		0x784
#define	CE_CLK_REG		0xac0
#define	CE_BGR_REG		0xac4
#define	DE0_CLK_REG		0xa00
#define	DE0_BGR_REG		0xa04
#define	NPU_CLK_REG		0xb00
#define	NPU_BGR_REG		0xb04
#define	GPU0_CLK_REG		0xb20
#define	GPU0_BGR_REG		0xb24
#define	ACCEL_CLK_ENABLE	__BIT(31)
#define	ACCEL_CLK_SEL		__BITS(26,24)
#define	GPU_CLK_UPDATE		__BIT(27)
#define	GPU_UPDATE_POLL_US	10
#define	GPU_UPDATE_TIMEOUT_US	10000
#define	GPU_PLL_LOCK_POLL_US	3
#define	GPU_PLL_LOCK_POLLS	100
#define	GPU_PLL_SETTLE_US	20
#define	GPUPLL_GATE_EN_REG	0x1914
#define	GPUPLL_GATE_MANUAL	(__BIT(16) | __BIT(0))
/* A733 User Manual V1.00, sections 4.1.6.249 and 4.1.6.258. */
#define	PERI0PLL_GATE_EN_REG	0x1908
#define	PERI0PLL_GATE_STAT_REG	0x1988
#define	GPU_READINESS_REG_MASK	__BITS(A733_GPU_MASTER, A733_GPU_REF)
#define	SMHC0_CLK_REG		0xd00
#define	SMHC0_BGR_REG		0xd0c
#define	SMHC1_CLK_REG		0xd10
#define	SMHC1_BGR_REG		0xd1c
#define	SMHC2_CLK_REG		0xd20
#define	SMHC2_BGR_REG		0xd2c
#define	UART_BGR_REG(n)		(0xe00 + 4 * (n))
#define	TWI_BGR_REG(n)		(0xe80 + 4 * (n))
#define	GPADC0_24M_CLK_REG	0xfc0
#define	THS_BGR_REG		0xfe4
#define	USB0_CLK_REG		0x1300
#define	USB0_BGR_REG		0x1304
#define	USB1_CLK_REG		0x1308
#define	USB1_BGR_REG		0x130c
#define	USB2_U2_REF_CLK_REG	0x1348
#define	USB2_SUSPEND_CLK_REG	0x1350
#define	USB2_MF_CLK_REG		0x1354
#define	USB2_BGR_REG		0x135c
#define	USB2_U3_UTMI_CLK_REG	0x1360
#define	USB2_U2_PIPE_CLK_REG	0x1364
#define	SERDES_PHY_CLK_REG	0x13c0
#define	SERDES_BGR_REG		0x13c4
#define	GMAC0_PHY_CLK_REG	0x1410
#define	GMAC0_BGR_REG		0x141c

static int sun60i_a733_ccu_match(device_t, cfdata_t, void *);
static void sun60i_a733_ccu_attach(device_t, device_t, void *);

static const struct device_compatible_entry compat_data[] = {
	{ .compat = "allwinner,sun60i-a733-ccu" },
	DEVICE_COMPAT_EOL
};

CFATTACH_DECL_NEW(sunxi_a733_ccu, sizeof(struct sunxi_ccu_softc),
	sun60i_a733_ccu_match, sun60i_a733_ccu_attach, NULL, NULL);

static struct sunxi_ccu_reset sun60i_a733_ccu_resets[] = {
	SUNXI_CCU_RESET(A733_RST_BUS_PWM0, PWM0_BGR_REG, 16),
	SUNXI_CCU_RESET(A733_RST_BUS_CE, CE_BGR_REG, 16),
	SUNXI_CCU_RESET(A733_RST_BUS_CE_SYS, CE_BGR_REG, 17),
	SUNXI_CCU_RESET(A733_RST_BUS_NPU_CORE, NPU_BGR_REG, 16),
	SUNXI_CCU_RESET(A733_RST_BUS_NPU_AXI, NPU_BGR_REG, 17),
	SUNXI_CCU_RESET(A733_RST_BUS_NPU_AHB, NPU_BGR_REG, 18),
	SUNXI_CCU_RESET(A733_RST_BUS_NPU_SRAM, NPU_BGR_REG, 19),
	SUNXI_CCU_RESET(A733_RST_BUS_GPU0, GPU0_BGR_REG, 16),

	SUNXI_CCU_RESET(A733_RST_BUS_MMC0, SMHC0_BGR_REG, 16),
	SUNXI_CCU_RESET(A733_RST_BUS_MMC1, SMHC1_BGR_REG, 16),
	SUNXI_CCU_RESET(A733_RST_BUS_MMC2, SMHC2_BGR_REG, 16),

	SUNXI_CCU_RESET(A733_RST_BUS_UART0, UART_BGR_REG(0), 16),
	SUNXI_CCU_RESET(A733_RST_BUS_UART1, UART_BGR_REG(1), 16),
	SUNXI_CCU_RESET(A733_RST_BUS_UART2, UART_BGR_REG(2), 16),
	SUNXI_CCU_RESET(A733_RST_BUS_UART3, UART_BGR_REG(3), 16),
	SUNXI_CCU_RESET(A733_RST_BUS_UART4, UART_BGR_REG(4), 16),
	SUNXI_CCU_RESET(A733_RST_BUS_UART5, UART_BGR_REG(5), 16),
	SUNXI_CCU_RESET(A733_RST_BUS_UART6, UART_BGR_REG(6), 16),

	SUNXI_CCU_RESET(A733_RST_BUS_I2C0, TWI_BGR_REG(0), 16),
	SUNXI_CCU_RESET(A733_RST_BUS_I2C1, TWI_BGR_REG(1), 16),
	SUNXI_CCU_RESET(A733_RST_BUS_I2C2, TWI_BGR_REG(2), 16),
	SUNXI_CCU_RESET(A733_RST_BUS_I2C3, TWI_BGR_REG(3), 16),

	SUNXI_CCU_RESET(A733_RST_BUS_THS0, THS_BGR_REG, 16),

	SUNXI_CCU_RESET(A733_RST_USB_PHY0, USB0_CLK_REG, 30),
	SUNXI_CCU_RESET(A733_RST_BUS_OHCI0, USB0_BGR_REG, 16),
	SUNXI_CCU_RESET(A733_RST_BUS_EHCI0, USB0_BGR_REG, 20),
	SUNXI_CCU_RESET(A733_RST_BUS_OTG, USB0_BGR_REG, 24),
	SUNXI_CCU_RESET(A733_RST_USB_PHY1, USB1_CLK_REG, 30),
	SUNXI_CCU_RESET(A733_RST_BUS_OHCI1, USB1_BGR_REG, 16),
	SUNXI_CCU_RESET(A733_RST_BUS_EHCI1, USB1_BGR_REG, 20),
	SUNXI_CCU_RESET(A733_RST_BUS_USB2, USB2_BGR_REG, 16),
	SUNXI_CCU_RESET(A733_RST_BUS_SERDES, SERDES_BGR_REG, 16),

	SUNXI_CCU_RESET(A733_RST_BUS_GMAC0, GMAC0_BGR_REG, 16),
	SUNXI_CCU_RESET(A733_RST_BUS_GMAC0_AXI, GMAC0_BGR_REG, 17),
};

static const char *pll_periph0_4x_parent[] = { "pll-periph0-4x" };
static const char *pll_periph0_150M_parent[] = { "pll-periph0-150M" };
static const char *bus_parents[] = {
	"sys-24M", "losc", "iosc", "pll-periph0-600M"
};
static const char *apb_uart_parents[] = {
	"sys-24M", "losc", "iosc", "pll-periph0-600M", "pll-periph0-480M"
};
static const char *mmc_parents[] = {
	"sys-24M", "pll-periph0-400M", "pll-periph0-300M",
	"pll-periph1-400M", "pll-periph1-300M"
};
static const char *ce_parents[] = {
	"sys-24M", "pll-periph0-400M", "pll-periph0-600M"
};
static const char *gpu_parents[] = {
	"pll-gpu0", "pll-periph0-800M", "pll-periph0-600M",
	"pll-periph0-400M", "pll-periph0-300M", "pll-periph0-200M"
};
static const char *de0_parents[] = {
	NULL, NULL, NULL, "pll-periph0-400M"
};
static const char *npu_parents[] = {
	"pll-npu", "pll-periph0-800M", "pll-periph0-600M",
	"pll-periph0-480M", "pll-ve0", "pll-ve1", "pll-de-3x"
};
static const char *usb2_suspend_parents[] = { "losc", "sys-24M" };
static const char *usb2_300M_parents[] = {
	"sys-24M", "pll-periph0-300M", "hosc"
};
static const char *serdes_phy_parents[] = {
	"sys-24M", "pll-periph0-600M"
};
static const char *usb2_480M_parents[] = {
	"sys-24M", "pll-periph0-480M", "hosc"
};
static const char *gpadc_24m_parents[] = { "sys-24M", "hosc" };
static const char *emmc_parents[] = {
	"sys-24M", "pll-periph0-800M", "pll-periph0-600M",
	"pll-periph1-800M", "pll-periph1-600M"
};

/*
 * PLL_NPU can also feed DRAM, MBUS and video engines. Until ownership of
 * those consumers is implemented, neither accelerator PLL is retuned or
 * disabled here. Accept only a running integer PLL supplied by firmware.
 */
static u_int
sun60i_a733_accel_pll_rate(struct sunxi_ccu_softc *sc,
    struct sunxi_ccu_clk *clk)
{
	const bus_size_t reg = clk->u.nkmp.reg;
	const uint32_t running = ACCEL_PLL_ENABLE | ACCEL_PLL_LDO |
	    ACCEL_PLL_LOCK_ENABLE | ACCEL_PLL_LOCK | ACCEL_PLL_OUTPUT;
	struct clk *parent;
	uint32_t val;
	uint64_t rate;
	u_int n, m, p;

	val = CCU_READ(sc, reg);
	if ((val & running) != running)
		return 0;
	/* These PLLs have two SDM pattern registers, with separate enables. */
	if ((CCU_READ(sc, reg + 8) & __BIT(31)) != 0 ||
	    (CCU_READ(sc, reg + 12) & __BIT(27)) != 0)
		return 0;
	parent = clk_get_parent(&clk->base);
	if (parent == NULL)
		return 0;
	n = __SHIFTOUT(val, ACCEL_PLL_N) + 1;
	m = __SHIFTOUT(val, ACCEL_PLL_M) + 1;
	p = __SHIFTOUT(val, ACCEL_PLL_P) + 1;
	if (n < 11)
		return 0;
	rate = (uint64_t)clk_get_rate(parent) * n / m / p;
	return rate <= UINT_MAX ? (u_int)rate : 0;
}

static int
sun60i_a733_accel_pll_enable(struct sunxi_ccu_softc *sc,
    struct sunxi_ccu_clk *clk, int enable)
{
	if (!enable)
		return EBUSY;
	return sun60i_a733_accel_pll_rate(sc, clk) != 0 ? 0 : ENXIO;
}

/* Check only writable fields: the GPU update bit may clear itself. */
static int
sun60i_a733_accel_write_locked(struct sunxi_ccu_softc *sc, bus_size_t reg,
    uint32_t mask, uint32_t bits)
{
	uint32_t old, val;

	KASSERT(mutex_owned(&sun60i_a733_ccu_lock));
	old = CCU_READ(sc, reg);
	val = (old & ~mask) | bits;
	if (val == old)
		return 0;
	if (reg == GPU0_CLK_REG)
		val |= GPU_CLK_UPDATE;
	CCU_WRITE(sc, reg, val);
	bus_space_barrier(sc->sc_bst, sc->sc_bsh, reg, 4,
	    BUS_SPACE_BARRIER_WRITE | BUS_SPACE_BARRIER_READ);
	return (CCU_READ(sc, reg) & mask) == bits ? 0 : EIO;
}

/* Native callbacks acquire the same mutex as the typed preparation paths. */
static bool sun60i_a733_native_mask_busy(bus_size_t, uint32_t);

static int
sun60i_a733_accel_write(struct sunxi_ccu_softc *sc, bus_size_t reg,
    uint32_t mask, uint32_t bits)
{
	int error;

	mutex_enter(&sun60i_a733_ccu_lock);
	if (sun60i_a733_native_mask_busy(reg, mask))
		error = EBUSY;
	else
		error = sun60i_a733_accel_write_locked(sc, reg, mask, bits);
	mutex_exit(&sun60i_a733_ccu_lock);
	return error;
}

static int
sun60i_a733_accel_gate_enable(struct sunxi_ccu_softc *sc,
    struct sunxi_ccu_clk *clk, int enable)
{
	return sun60i_a733_accel_write(sc, clk->u.gate.reg,
	    clk->u.gate.mask, enable ? clk->u.gate.mask : 0);
}

static u_int
sun60i_a733_accel_gate_rate(struct sunxi_ccu_softc *sc,
    struct sunxi_ccu_clk *clk)
{
	struct clk *parent;

	if ((CCU_READ(sc, clk->u.gate.reg) & clk->u.gate.mask) == 0)
		return 0;
	parent = clk_get_parent(&clk->base);
	return parent != NULL ? clk_get_rate(parent) : 0;
}

static const char *
sun60i_a733_accel_parent(struct sunxi_ccu_softc *sc,
    struct sunxi_ccu_clk *clk)
{
	struct sunxi_ccu_div *div = &clk->u.div;
	u_int sel;

	sel = __SHIFTOUT(CCU_READ(sc, div->reg), ACCEL_CLK_SEL);
	return sel < div->nparents ? div->parents[sel] : NULL;
}

static int
sun60i_a733_accel_enable(struct sunxi_ccu_softc *sc,
    struct sunxi_ccu_clk *clk, int enable)
{
	struct clk *parent;

	if (enable) {
		parent = clk_get_parent(&clk->base);
		if (parent == NULL || clk_get_rate(parent) == 0)
			return ENXIO;
	}
	return sun60i_a733_accel_write(sc, clk->u.div.reg,
	    ACCEL_CLK_ENABLE, enable ? ACCEL_CLK_ENABLE : 0);
}

static u_int
sun60i_a733_accel_rate(struct sunxi_ccu_softc *sc,
    struct sunxi_ccu_clk *clk)
{
	struct sunxi_ccu_div *div = &clk->u.div;
	struct clk *parent;
	uint32_t val;
	uint64_t rate;
	u_int m;

	parent = clk_get_parent(&clk->base);
	if (parent == NULL)
		return 0;
	val = CCU_READ(sc, div->reg);
	if ((val & ACCEL_CLK_ENABLE) == 0)
		return 0;
	m = __SHIFTOUT(val, div->div);
	rate = clk_get_rate(parent);
	if (div->reg == GPU0_CLK_REG)
		return (u_int)(rate * (16 - m) / 16);
	return (u_int)(rate / (m + 1));
}

static int
sun60i_a733_accel_set_parent(struct sunxi_ccu_softc *sc,
    struct sunxi_ccu_clk *clk, const char *name)
{
	struct sunxi_ccu_div *div = &clk->u.div;
	struct sunxi_ccu_clk *parent;
	u_int sel;
	uint32_t val;

	for (sel = 0; sel < div->nparents; sel++)
		if (strcmp(name, div->parents[sel]) == 0)
			break;
	if (sel == div->nparents)
		return EINVAL;
	parent = sunxi_ccu_clock_find(sc, name);
	if (parent == NULL || clk_get_rate(&parent->base) == 0)
		return ENXIO;
	val = CCU_READ(sc, div->reg);
	if (__SHIFTOUT(val, ACCEL_CLK_SEL) == sel)
		return 0;
	if ((val & ACCEL_CLK_ENABLE) != 0)
		return EBUSY;
	return sun60i_a733_accel_write(sc, div->reg, ACCEL_CLK_SEL,
	    __SHIFTIN(sel, ACCEL_CLK_SEL));
}

static int
sun60i_a733_accel_set_rate(struct sunxi_ccu_softc *sc,
    struct sunxi_ccu_clk *clk, u_int rate)
{
	struct sunxi_ccu_div *div = &clk->u.div;
	struct clk *parent;
	uint64_t prate, candidate;
	uint32_t val;
	u_int m, max;

	if (rate == 0)
		return EINVAL;
	parent = clk_get_parent(&clk->base);
	if (parent == NULL || (prate = clk_get_rate(parent)) == 0)
		return ENXIO;
	max = __SHIFTOUT(div->div, div->div);
	for (m = 0; m <= max; m++) {
		if (div->reg == GPU0_CLK_REG) {
			/* Program only the documented integer divisors. */
			if (m != 0 && m != 8 && m != 12 && m != 14 && m != 15)
				continue;
			candidate = prate * (16 - m);
			if (candidate == (uint64_t)rate * 16)
				break;
		} else if (prate == (uint64_t)rate * (m + 1)) {
			break;
		}
	}
	if (m > max)
		return ERANGE;
	val = CCU_READ(sc, div->reg);
	if (__SHIFTOUT(val, div->div) == m)
		return 0;
	if ((val & ACCEL_CLK_ENABLE) != 0)
		return EBUSY;
	return sun60i_a733_accel_write(sc, div->reg, div->div,
	    __SHIFTIN(m, div->div));
}

#define	A733_ACCEL_PLL(_id, _name, _reg)			\
	[_id] = {						\
		.type = SUNXI_CCU_NKMP,				\
		.base.name = (_name),				\
		.u.nkmp.reg = (_reg),				\
		.u.nkmp.parent = "pll-ref",			\
		.enable = sun60i_a733_accel_pll_enable,		\
		.get_rate = sun60i_a733_accel_pll_rate,		\
		.get_parent = sunxi_ccu_nkmp_get_parent,	\
	}

#define	A733_ACCEL_GATE(_id, _name, _parent, _reg, _bit)	\
	[_id] = {						\
		.type = SUNXI_CCU_GATE,				\
		.base.name = (_name),				\
		.u.gate.reg = (_reg),				\
		.u.gate.mask = __BIT(_bit),			\
		.u.gate.parent = (_parent),			\
		.enable = sun60i_a733_accel_gate_enable,	\
		.get_rate = sun60i_a733_accel_gate_rate,	\
		.get_parent = sunxi_ccu_gate_get_parent,	\
	}

#define	A733_ACCEL_MOD(_id, _name, _parents, _reg, _div)	\
	[_id] = {						\
		.type = SUNXI_CCU_DIV,				\
		.base.name = (_name),				\
		.u.div.reg = (_reg),				\
		.u.div.div = (_div),				\
		.u.div.parents = (_parents),			\
		.u.div.nparents = __arraycount(_parents),	\
		.enable = sun60i_a733_accel_enable,		\
		.get_rate = sun60i_a733_accel_rate,		\
		.set_rate = sun60i_a733_accel_set_rate,		\
		.get_parent = sun60i_a733_accel_parent,		\
		.set_parent = sun60i_a733_accel_set_parent,	\
	}

/* All DE0 mutations use the typed retained profile, including enable. */
static int
sun60i_a733_de0_enable(struct sunxi_ccu_softc *sc,
    struct sunxi_ccu_clk *clk, int enable)
{
	return EOPNOTSUPP;
}

static int
sun60i_a733_de0_set_rate(struct sunxi_ccu_softc *sc,
    struct sunxi_ccu_clk *clk, u_int rate)
{
	return EOPNOTSUPP;
}

static int
sun60i_a733_de0_set_parent(struct sunxi_ccu_softc *sc,
    struct sunxi_ccu_clk *clk, const char *parent)
{
	return EOPNOTSUPP;
}

/*
 * The SD/MMC controllers halve their module clock, so a card clock of
 * 50 MHz is a 100 MHz module clock.
 */
#define	A733_MMC(_id, _name, _parents, _reg)				\
	SUNXI_CCU_NM(_id, _name, _parents, _reg,			\
	    __BITS(12,8),	/* n */					\
	    __BITS(4,0),	/* m */					\
	    __BITS(26,24),	/* sel */				\
	    __BIT(31),		/* enable */				\
	    SUNXI_CCU_NM_ROUND_DOWN | SUNXI_CCU_NM_DIVIDE_BY_TWO)

/* PWM0 uses DCXO directly, not the normalized 24 MHz PLL_REF output. */
static u_int
sun60i_a733_pwm_rate(struct sunxi_ccu_softc *sc, struct sunxi_ccu_clk *clk)
{
	struct sunxi_rtc_dcxo_state state;
	int node;

	if (OF_getproplen(sc->sc_phandle, "netbsd,dcxo-source") != 4)
		return 0;
	node = fdtbus_get_phandle(sc->sc_phandle, "netbsd,dcxo-source");
	if (node <= 0 || !of_compatible_match(node, sun60i_a733_dcxo_compat) ||
	    sun60i_a733_dcxo_query(node, &state) != 0)
		return 0;
	return state.rate_hz;
}

static struct sunxi_ccu_clk sun60i_a733_ccu_clks[] = {

	/*
	 * PLL_REF turns a 19.2, 24 or 26 MHz crystal into 24 MHz. Boards
	 * with a 24 MHz crystal run it one to one, which is all that is
	 * described here.
	 */
	SUNXI_CCU_FIXED_FACTOR(A733_CLK_PLL_REF, "pll-ref", "hosc", 1, 1),
	SUNXI_CCU_FIXED_FACTOR(A733_CLK_SYS_24M, "sys-24M", "pll-ref", 1, 1),
	[A733_CLK_DE0] = {
		.type = SUNXI_CCU_DIV,
		.base.name = "de0",
		.u.div.reg = DE0_CLK_REG,
		.u.div.div = __BITS(4,0),
		.u.div.parents = de0_parents,
		.u.div.nparents = __arraycount(de0_parents),
		.enable = sun60i_a733_de0_enable,
		.get_rate = sun60i_a733_accel_rate,
		.get_parent = sun60i_a733_accel_parent,
		.set_rate = sun60i_a733_de0_set_rate,
		.set_parent = sun60i_a733_de0_set_parent,
	},
	A733_ACCEL_PLL(A733_CLK_PLL_GPU0, "pll-gpu0", PLL_GPU0_CTRL_REG),
	A733_ACCEL_PLL(A733_CLK_PLL_NPU, "pll-npu", PLL_NPU_CTRL_REG),
	A733_ACCEL_GATE(A733_CLK_AHB_NPU, "ahb-npu", "ahb",
	    AHB_MASTER_GATE_REG, 6),
	A733_ACCEL_GATE(A733_CLK_AHB_GPU0, "ahb-gpu0", "ahb",
	    AHB_MASTER_GATE_REG, 7),
	/* MBUS rates and the undocumented NPU bus parent are not modeled. */
	A733_ACCEL_GATE(A733_CLK_MBUS_GPU0, "mbus-gpu0", NULL,
	    MBUS_MASTER_GATE_REG, 16),
	A733_ACCEL_GATE(A733_CLK_MBUS_NPU, "mbus-npu", NULL,
	    MBUS_MASTER_GATE_REG, 18),
	A733_ACCEL_GATE(A733_CLK_BUS_NPU, "bus-npu", NULL, NPU_BGR_REG, 0),
	A733_ACCEL_GATE(A733_CLK_BUS_GPU0, "bus-gpu0", "ahb", GPU0_BGR_REG, 0),
	A733_ACCEL_MOD(A733_CLK_NPU, "npu", npu_parents, NPU_CLK_REG,
	    __BITS(4,0)),
	A733_ACCEL_MOD(A733_CLK_GPU0, "gpu0", gpu_parents, GPU0_CLK_REG,
	    __BITS(3,0)),

	SUNXI_CCU_NKMP(A733_CLK_PLL_PERIPH0_4X, "pll-periph0-4x", "pll-ref",
	    PLL_PERIPH0_CTRL_REG,	/* reg */
	    __BITS(15,8),		/* n */
	    0,				/* k */
	    0,				/* m */
	    0,				/* p */
	    __BIT(31),			/* enable */
	    0),
	SUNXI_CCU_DIV(A733_CLK_PLL_PERIPH0_2X, "pll-periph0-2x",
	    pll_periph0_4x_parent, PLL_PERIPH0_CTRL_REG,
	    __BITS(22,20),		/* div */
	    0,				/* sel */
	    0),
	SUNXI_CCU_DIV(A733_CLK_PLL_PERIPH0_800M, "pll-periph0-800M",
	    pll_periph0_4x_parent, PLL_PERIPH0_CTRL_REG,
	    __BITS(18,16),		/* div */
	    0,				/* sel */
	    0),
	SUNXI_CCU_DIV(A733_CLK_PLL_PERIPH0_480M, "pll-periph0-480M",
	    pll_periph0_4x_parent, PLL_PERIPH0_CTRL_REG,
	    __BITS(4,2),		/* div */
	    0,				/* sel */
	    0),
	SUNXI_CCU_FIXED_FACTOR(A733_CLK_PLL_PERIPH0_600M, "pll-periph0-600M",
	    "pll-periph0-2x", 2, 1),
	SUNXI_CCU_FIXED_FACTOR(A733_CLK_PLL_PERIPH0_400M, "pll-periph0-400M",
	    "pll-periph0-2x", 3, 1),
	SUNXI_CCU_FIXED_FACTOR(A733_CLK_PLL_PERIPH0_300M, "pll-periph0-300M",
	    "pll-periph0-600M", 2, 1),
	SUNXI_CCU_FIXED_FACTOR(A733_CLK_PLL_PERIPH0_200M, "pll-periph0-200M",
	    "pll-periph0-400M", 2, 1),
	SUNXI_CCU_FIXED_FACTOR(A733_CLK_PLL_PERIPH0_160M, "pll-periph0-160M",
	    "pll-periph0-480M", 3, 1),
	SUNXI_CCU_FIXED_FACTOR(A733_CLK_PLL_PERIPH0_150M, "pll-periph0-150M",
	    "pll-periph0-300M", 2, 1),

	SUNXI_CCU_DIV(A733_CLK_AHB, "ahb", bus_parents, AHB_CFG_REG,
	    __BITS(4,0),		/* div */
	    __BITS(25,24),		/* sel */
	    0),
	SUNXI_CCU_DIV(A733_CLK_APB0, "apb0", bus_parents, APB0_CFG_REG,
	    __BITS(4,0),		/* div */
	    __BITS(25,24),		/* sel */
	    0),
	SUNXI_CCU_DIV(A733_CLK_APB1, "apb1", bus_parents, APB1_CFG_REG,
	    __BITS(4,0),		/* div */
	    __BITS(25,24),		/* sel */
	    0),
	SUNXI_CCU_DIV(A733_CLK_APB_UART, "apb-uart", apb_uart_parents,
	    APB_UART_CFG_REG,
	    __BITS(4,0),		/* div */
	    __BITS(26,24),		/* sel */
	    0),

	SUNXI_CCU_GATE(A733_CLK_MBUS_GMAC0, "mbus-gmac0", "ahb",
	    MBUS_GATE_REG, 11),

	[A733_CLK_BUS_PWM0] = {
		.type = SUNXI_CCU_GATE,
		.base.name = "bus-pwm0",
		.u.gate.reg = PWM0_BGR_REG,
		.u.gate.mask = __BIT(0),
		.enable = sunxi_ccu_gate_enable,
		.get_rate = sun60i_a733_pwm_rate,
	},

	SUNXI_CCU_GATE(A733_CLK_MBUS_CE, "mbus-ce", "ahb",
	    MBUS_GATE_REG, 2),
	SUNXI_CCU_DIV_GATE(A733_CLK_CE, "ce", ce_parents, CE_CLK_REG,
	    __BITS(4,0),		/* div */
	    __BITS(26,24),		/* sel */
	    __BIT(31),			/* enable */
	    0),
	SUNXI_CCU_GATE(A733_CLK_BUS_CE, "bus-ce", "ahb",
	    CE_BGR_REG, 0),
	SUNXI_CCU_GATE(A733_CLK_BUS_CE_SYS, "bus-ce-sys", "ahb",
	    CE_BGR_REG, 1),

	A733_MMC(A733_CLK_MMC0, "mmc0", mmc_parents, SMHC0_CLK_REG),
	A733_MMC(A733_CLK_MMC1, "mmc1", mmc_parents, SMHC1_CLK_REG),
	A733_MMC(A733_CLK_MMC2, "mmc2", emmc_parents, SMHC2_CLK_REG),
	SUNXI_CCU_GATE(A733_CLK_BUS_MMC0, "bus-mmc0", "ahb",
	    SMHC0_BGR_REG, 0),
	SUNXI_CCU_GATE(A733_CLK_BUS_MMC1, "bus-mmc1", "ahb",
	    SMHC1_BGR_REG, 0),
	SUNXI_CCU_GATE(A733_CLK_BUS_MMC2, "bus-mmc2", "ahb",
	    SMHC2_BGR_REG, 0),

	SUNXI_CCU_GATE(A733_CLK_BUS_UART0, "bus-uart0", "apb-uart",
	    UART_BGR_REG(0), 0),
	SUNXI_CCU_GATE(A733_CLK_BUS_UART1, "bus-uart1", "apb-uart",
	    UART_BGR_REG(1), 0),
	SUNXI_CCU_GATE(A733_CLK_BUS_UART2, "bus-uart2", "apb-uart",
	    UART_BGR_REG(2), 0),
	SUNXI_CCU_GATE(A733_CLK_BUS_UART3, "bus-uart3", "apb-uart",
	    UART_BGR_REG(3), 0),
	SUNXI_CCU_GATE(A733_CLK_BUS_UART4, "bus-uart4", "apb-uart",
	    UART_BGR_REG(4), 0),
	SUNXI_CCU_GATE(A733_CLK_BUS_UART5, "bus-uart5", "apb-uart",
	    UART_BGR_REG(5), 0),
	SUNXI_CCU_GATE(A733_CLK_BUS_UART6, "bus-uart6", "apb-uart",
	    UART_BGR_REG(6), 0),

	SUNXI_CCU_GATE(A733_CLK_BUS_I2C0, "bus-i2c0", "apb1",
	    TWI_BGR_REG(0), 0),
	SUNXI_CCU_GATE(A733_CLK_BUS_I2C1, "bus-i2c1", "apb1",
	    TWI_BGR_REG(1), 0),
	SUNXI_CCU_GATE(A733_CLK_BUS_I2C2, "bus-i2c2", "apb1",
	    TWI_BGR_REG(2), 0),
	SUNXI_CCU_GATE(A733_CLK_BUS_I2C3, "bus-i2c3", "apb1",
	    TWI_BGR_REG(3), 0),

	/* The thermal sensors convert on this clock. */
	SUNXI_CCU_DIV_GATE(A733_CLK_GPADC0_24M, "gpadc0-24m",
	    gpadc_24m_parents, GPADC0_24M_CLK_REG,
	    __BITS(4,0),		/* div */
	    __BITS(26,24),		/* sel */
	    __BIT(31),			/* enable */
	    0),
	SUNXI_CCU_GATE(A733_CLK_BUS_THS0, "bus-ths0", "apb0",
	    THS_BGR_REG, 0),

	/* The 24 MHz reference of the two USB 2.0 ports and their buses. */
	SUNXI_CCU_GATE(A733_CLK_USB_OHCI0, "usb-ohci0", "hosc",
	    USB0_CLK_REG, 31),
	SUNXI_CCU_GATE(A733_CLK_BUS_OHCI0, "bus-ohci0", "ahb",
	    USB0_BGR_REG, 0),
	SUNXI_CCU_GATE(A733_CLK_BUS_EHCI0, "bus-ehci0", "ahb",
	    USB0_BGR_REG, 4),
	SUNXI_CCU_GATE(A733_CLK_BUS_OTG, "bus-otg", "ahb",
	    USB0_BGR_REG, 8),
	SUNXI_CCU_GATE(A733_CLK_USB_OHCI1, "usb-ohci1", "hosc",
	    USB1_CLK_REG, 31),
	SUNXI_CCU_GATE(A733_CLK_BUS_OHCI1, "bus-ohci1", "ahb",
	    USB1_BGR_REG, 0),
	SUNXI_CCU_GATE(A733_CLK_BUS_EHCI1, "bus-ehci1", "ahb",
	    USB1_BGR_REG, 4),

	/* The USB 3 controller: the third USB port of the chip. */
	SUNXI_CCU_GATE(A733_CLK_USB2_U2_REF, "usb2-u2-ref", "hosc",
	    USB2_U2_REF_CLK_REG, 31),
	SUNXI_CCU_DIV_GATE(A733_CLK_USB2_SUSPEND, "usb2-suspend",
	    usb2_suspend_parents, USB2_SUSPEND_CLK_REG,
	    __BITS(4,0),		/* div */
	    __BIT(24),			/* sel */
	    __BIT(31),			/* enable */
	    0),
	SUNXI_CCU_DIV_GATE(A733_CLK_USB2_MF, "usb2-mf",
	    usb2_300M_parents, USB2_MF_CLK_REG,
	    __BITS(4,0),		/* div */
	    __BITS(26,24),		/* sel */
	    __BIT(31),			/* enable */
	    0),
	SUNXI_CCU_DIV_GATE(A733_CLK_USB2_U3_UTMI, "usb2-u3-utmi",
	    usb2_300M_parents, USB2_U3_UTMI_CLK_REG,
	    __BITS(4,0),		/* div */
	    __BITS(26,24),		/* sel */
	    __BIT(31),			/* enable */
	    0),
	SUNXI_CCU_DIV_GATE(A733_CLK_USB2_U2_PIPE, "usb2-u2-pipe",
	    usb2_480M_parents, USB2_U2_PIPE_CLK_REG,
	    __BITS(4,0),		/* div */
	    __BITS(26,24),		/* sel */
	    __BIT(31),			/* enable */
	    0),

	SUNXI_CCU_DIV_GATE(A733_CLK_SERDES_PHY, "serdes-phy-cfg",
	    serdes_phy_parents, SERDES_PHY_CLK_REG,
	    __BITS(4,0),		/* div */
	    __BITS(26,24),		/* sel */
	    __BIT(31),			/* enable */
	    0),

	SUNXI_CCU_DIV_GATE(A733_CLK_GMAC0_PHY, "gmac0-phy",
	    pll_periph0_150M_parent, GMAC0_PHY_CLK_REG,
	    __BITS(4,0),		/* div */
	    0,				/* sel */
	    __BIT(31),			/* enable */
	    0),
	SUNXI_CCU_GATE(A733_CLK_BUS_GMAC0, "bus-gmac0", "ahb",
	    GMAC0_BGR_REG, 0),
};

/* Decode an integer PLL output without the generic provider's assumptions. */
static int
sun60i_a733_gpu_pll_rate(uint32_t control, uint32_t pattern0,
    uint32_t pattern1, uint32_t divider_mask, uint32_t output_gate,
    u_int fixed_divider, u_int *result)
{
	const uint32_t running = ACCEL_PLL_ENABLE | ACCEL_PLL_LDO |
	    ACCEL_PLL_LOCK_ENABLE | ACCEL_PLL_LOCK | output_gate;
	uint64_t numerator;
	u_int n, denominator;

	if ((control & running) != running)
		return EBUSY;
	if ((pattern0 & __BIT(31)) != 0 || (pattern1 & __BIT(27)) != 0)
		return EOPNOTSUPP;
	n = __SHIFTOUT(control, ACCEL_PLL_N) + 1;
	if (n < 11)
		return EOPNOTSUPP;
	denominator = (__SHIFTOUT(control, ACCEL_PLL_P) + 1) *
	    (__SHIFTOUT(control, divider_mask) + 1) * fixed_divider;
	numerator = UINT64_C(24000000) * n;
	if (numerator % denominator != 0)
		return EOPNOTSUPP;
	if (numerator / denominator > UINT_MAX)
		return ERANGE;
	*result = numerator / denominator;
	return 0;
}

/* Evaluate a completed observation, keeping rates private until success. */
static int
sun60i_a733_gpu_evaluate(struct sun60i_a733_gpu_state *state)
{
	static const u_int periph_div[] = { 0, 1, 2, 3, 4, 6 };
	const uint32_t running = ACCEL_PLL_ENABLE | ACCEL_PLL_LDO |
	    ACCEL_PLL_LOCK_ENABLE | ACCEL_PLL_LOCK | ACCEL_PLL_OUTPUT;
	const uint32_t *val = state->sample[0];
	uint64_t reference;
	u_int core, bus, sel, raw, divisor, ref_divisor;
	int error;

	state->reason = A733_GPU_SNAPSHOT_CHANGED;
	if ((state->changed & GPU_READINESS_REG_MASK) != 0)
		return EBUSY;
	state->reason = A733_GPU_DCXO_CHANGED;
	if (state->dcxo_sample[0][0] != state->dcxo_sample[1][0] ||
	    state->dcxo_hz[0] != state->dcxo_hz[1])
		return EBUSY;
	state->reason = A733_GPU_HOSC_CHANGED;
	if (state->hosc_hz[0] != state->hosc_hz[1])
		return EBUSY;
	state->reason = A733_GPU_MODULE_GATED;
	if ((val[A733_GPU_MODULE] & ACCEL_CLK_ENABLE) == 0)
		return EBUSY;
	state->reason = A733_GPU_UPDATE_PENDING;
	if ((val[A733_GPU_MODULE] & GPU_CLK_UPDATE) != 0)
		return EBUSY;
	state->reason = A733_GPU_BUS_GATED;
	if ((val[A733_GPU_BUS] & __BIT(0)) == 0)
		return EBUSY;
	state->reason = A733_GPU_RESET_ASSERTED;
	if ((val[A733_GPU_BUS] & __BIT(16)) == 0)
		return EBUSY;
	state->reason = A733_GPU_MASTER_GATED;
	if ((val[A733_GPU_MASTER] & __BIT(7)) == 0)
		return EBUSY;
	state->reason = A733_GPU_REF_FLAGS;
	if ((val[A733_GPU_REF] & running) != running)
		return EBUSY;
	/* Verify PLL_REF's normalization instead of assuming a 1:1 clock. */
	state->reason = A733_GPU_REF_RATE;
	ref_divisor = (__SHIFTOUT(val[A733_GPU_REF], __BITS(22,16)) + 1) *
	    (__SHIFTOUT(val[A733_GPU_REF], ACCEL_PLL_P) + 1);
	reference = (uint64_t)state->dcxo_hz[0] *
	    (__SHIFTOUT(val[A733_GPU_REF], ACCEL_PLL_N) + 1);
	if (reference != UINT64_C(24000000) * ref_divisor)
		return EOPNOTSUPP;

	state->reason = A733_GPU_CORE_PLL;
	sel = __SHIFTOUT(val[A733_GPU_MODULE], ACCEL_CLK_SEL);
	if (sel == 0) {
		error = sun60i_a733_gpu_pll_rate(val[A733_GPU_PLL],
		    val[A733_GPU_PAT0], val[A733_GPU_PAT1], ACCEL_PLL_M,
		    ACCEL_PLL_OUTPUT, 1, &core);
	} else if (sel < __arraycount(periph_div)) {
		error = sun60i_a733_gpu_pll_rate(val[A733_GPU_PERIPH],
		    val[A733_GPU_PERIPH_PAT0], val[A733_GPU_PERIPH_PAT1],
		    sel == 1 ? __BITS(18,16) : ACCEL_PLL_M,
		    sel == 1 ? __BIT(26) : ACCEL_PLL_OUTPUT,
		    periph_div[sel], &core);
	} else {
		state->reason = A733_GPU_CORE_PARENT;
		return EOPNOTSUPP;
	}
	if (error != 0)
		return error;
	state->reason = A733_GPU_CORE_DIVIDER;
	raw = __SHIFTOUT(val[A733_GPU_MODULE], __BITS(3,0));
	/* Only the five dividers described by the pinned Linux table. */
	if (raw != 0 && raw != 8 && raw != 12 && raw != 14 && raw != 15)
		return EOPNOTSUPP;
	divisor = 16 / (16 - raw);
	if (core % divisor != 0)
		return EOPNOTSUPP;
	core /= divisor;

	state->reason = A733_GPU_AHB_PARENT;
	switch (__SHIFTOUT(val[A733_GPU_AHB], __BITS(25,24))) {
	case 0:
		bus = 24000000;
		break;
	case 3:
		state->reason = A733_GPU_AHB_PLL;
		error = sun60i_a733_gpu_pll_rate(val[A733_GPU_PERIPH],
		    val[A733_GPU_PERIPH_PAT0], val[A733_GPU_PERIPH_PAT1],
		    ACCEL_PLL_M, ACCEL_PLL_OUTPUT, 2, &bus);
		if (error != 0)
			return error;
		break;
	default:
		/* LOSC/IOSC readiness is outside this bounded observation. */
		return EOPNOTSUPP;
	}
	state->reason = A733_GPU_AHB_DIVIDER;
	divisor = __SHIFTOUT(val[A733_GPU_AHB], __BITS(4,0)) + 1;
	if (bus % divisor != 0)
		return EOPNOTSUPP;
	bus /= divisor;
	state->core_hz = core;
	state->bus_hz = bus;
	state->reason = A733_GPU_READY;
	return 0;
}

/*
 * Observe only the path needed to read the GPU register interface. This does
 * not enable clocks, change reset state, or claim MBUS/DMA readiness. Equal
 * snapshots detect some concurrent changes, but do not reserve the hardware.
 * Once acquisition starts, collect both complete samples even if they differ.
 */
int
sun60i_a733_ccu_gpu_inspect(struct clk *gpu, struct sun60i_a733_gpu_state *result)
{
	static const bus_size_t regs[A733_GPU_NREGS] = {
		[A733_GPU_REF] = PLL_REF_CTRL_REG,
		[A733_GPU_PERIPH] = PLL_PERIPH0_CTRL_REG,
		[A733_GPU_PERIPH_PAT0] = PLL_PERIPH0_CTRL_REG + 8,
		[A733_GPU_PERIPH_PAT1] = PLL_PERIPH0_CTRL_REG + 12,
		[A733_GPU_PLL] = PLL_GPU0_CTRL_REG,
		[A733_GPU_PAT0] = PLL_GPU0_CTRL_REG + 8,
		[A733_GPU_PAT1] = PLL_GPU0_CTRL_REG + 12,
		[A733_GPU_MODULE] = GPU0_CLK_REG,
		[A733_GPU_BUS] = GPU0_BGR_REG,
		[A733_GPU_AHB] = AHB_CFG_REG,
		[A733_GPU_MASTER] = AHB_MASTER_GATE_REG,
		[A733_GPU_PERIPH_GATE_EN] = PERI0PLL_GATE_EN_REG,
		[A733_GPU_PERIPH_GATE_STAT] = PERI0PLL_GATE_STAT_REG,
	};
	struct sun60i_a733_gpu_state state = { 0 };
	struct sunxi_rtc_dcxo_state dcxo[2];
	struct sunxi_ccu_softc *sc;
	struct clk *hosc;
	bus_addr_t addr;
	bus_size_t size;
	int node, error;

	if (result == NULL)
		return EINVAL;
	if (gpu == NULL)
		return ENXIO;
	if (gpu != &sun60i_a733_ccu_clks[A733_CLK_GPU0].base)
		return EOPNOTSUPP;
	if (gpu->domain == NULL || gpu->domain->priv == NULL)
		return ENXIO;
	sc = gpu->domain->priv;
	/* sunxi_ccu_attach maps this exact FDT range; reject truncated maps. */
	if (fdtbus_get_reg(sc->sc_phandle, 0, &addr, &size) != 0 ||
	    size < PERI0PLL_GATE_STAT_REG + sizeof(uint32_t))
		return ENXIO;
	if (OF_getproplen(sc->sc_phandle, "netbsd,dcxo-source") != 4)
		return EINVAL;
	node = fdtbus_get_phandle(sc->sc_phandle, "netbsd,dcxo-source");
	if (node <= 0)
		return EINVAL;
	if (!of_compatible_match(node, sun60i_a733_dcxo_compat))
		return EOPNOTSUPP;
	hosc = fdtbus_clock_get(sc->sc_phandle, "hosc");
	if (hosc == NULL)
		return ENXIO;
	state.hosc_hz[0] = clk_get_rate(hosc);
	error = sun60i_a733_dcxo_query(node, &dcxo[0]);
	if (error != 0)
		return error;
	for (u_int sample = 0; sample < 2; sample++) {
		for (u_int i = 0; i < __arraycount(regs); i++)
			state.sample[sample][i] = CCU_READ(sc, regs[i]);
	}
	state.hosc_hz[1] = clk_get_rate(hosc);
	error = sun60i_a733_dcxo_query(node, &dcxo[1]);
	if (error != 0)
		return error;
	for (u_int i = 0; i < 2; i++) {
		state.periph_gates[i].configured = __SHIFTOUT(
		    state.sample[i][A733_GPU_PERIPH_GATE_EN], __BITS(27,16));
		state.periph_gates[i].no_auto =
		    state.sample[i][A733_GPU_PERIPH_GATE_EN] & __BITS(11,0);
		state.periph_gates[i].effective = __SHIFTOUT(
		    state.sample[i][A733_GPU_PERIPH_GATE_STAT], __BITS(27,16));
		memcpy(state.dcxo_sample[i], dcxo[i].sample, sizeof(dcxo[i].sample));
		state.dcxo_hz[i] = dcxo[i].rate_hz;
	}
	for (u_int i = 0; i < __arraycount(regs); i++) {
		if (state.sample[0][i] != state.sample[1][i])
			state.changed |= __BIT(i);
	}
	state.readiness_error = sun60i_a733_gpu_evaluate(&state);
	*result = state;
	return 0;
}

int
sun60i_a733_ccu_gpu_ready(struct clk *gpu, u_int *core_rate, u_int *bus_rate)
{
	struct sun60i_a733_gpu_state state;
	int error;

	if (core_rate == NULL || bus_rate == NULL || core_rate == bus_rate)
		return EINVAL;
	error = sun60i_a733_ccu_gpu_inspect(gpu, &state);
	if (error != 0)
		return error;
	if (state.readiness_error != 0)
		return state.readiness_error;
	*core_rate = state.core_hz;
	*bus_rate = state.bus_hz;
	return 0;
}

/*
 * One boot-owned experiment. The guard counts dispatched writers even before
 * reservation, so a lease cannot overlap a writer that passed its entry check.
 * Recursive parent dispatch does not hold the mutex across the recursive call.
 */
static struct {
	struct sunxi_ccu_softc *sc;
	const void *owner;
	u_int writers;
	bool attempted;
	struct sun60i_a733_gpu_state before;
} sun60i_gpu_lease;

/* NPU owns local masks. Running common ancestors remain borrowed. */
enum sun60i_a733_npu_reg {
	A733_NPU_REF, A733_NPU_PERIPH, A733_NPU_PAT0, A733_NPU_PAT1,
	A733_NPU_AHB, A733_NPU_GATE, A733_NPU_STATUS,
	A733_NPU_MODULE, A733_NPU_BUS, A733_NPU_AHB_MASTER,
	A733_NPU_MBUS_MASTER, A733_NPU_NREGS
};
struct sun60i_a733_npu_state {
	uint32_t sample[2][A733_NPU_NREGS];
	uint32_t dcxo[2][2];
	u_int dcxo_hz[2], hosc_hz[2];
};
static struct {
	const void *owner;
	bool attempted, prepared;
	struct sun60i_a733_npu_state before, after;
} sun60i_npu_lease;

/* Fixed inactive IOMMU1 peers. A733 manual v1.00 sections 4.1.6.84,106-116. */
static const struct {
	bus_size_t reg;
	uint32_t mask;
} sun60i_a733_display_isolation_regs[A733_DISPLAY_NISOLATION_REGS] = {
	{ 0xa20, __BIT(31) }, { 0xa24, __BIT(16) | __BIT(0) },
	{ 0xa40, __BIT(31) }, { 0xa44, __BIT(16) | __BIT(0) },
	{ 0xa60, __BIT(31) }, { 0xa64, __BIT(31) },
	{ 0xa6c, __BIT(16) | __BIT(0) }, { 0xa88, __BIT(31) },
	{ 0xa8c, __BIT(18) | __BIT(2) }, { 0x5e4, __BIT(18) },
};
/* The pure DISPLAY core and native admission share this provider mutex. */
static struct {
	struct sun60i_a733_display_owner core;
	int power, fault;
	bool pck_retained, prepared, memory_claimed, isolation_claimed;
	bool hdmi_prepared, tv0_prepared;
	uint32_t memory[2][A733_DISPLAY_NMEMORY_REGS];
	struct sun60i_a733_display_inputs after;
} sun60i_display_lease;

static uint32_t
sun60i_a733_display_mask(bus_size_t reg)
{
	if (sun60i_display_lease.isolation_claimed)
		for (u_int i = 0; i < A733_DISPLAY_NISOLATION_REGS; i++)
			if (reg == sun60i_a733_display_isolation_regs[i].reg)
				return sun60i_a733_display_isolation_regs[i].mask;
	switch (reg) {
	case PLL_REF_CTRL_REG:
	case PLL_PERIPH0_CTRL_REG:
	case PLL_PERIPH0_CTRL_REG + 8:
	case PLL_PERIPH0_CTRL_REG + 12:
	case AHB_CFG_REG:
	case PERI0PLL_GATE_EN_REG:
	case PERI0PLL_GATE_STAT_REG:
	case 0x2e0: case 0x2e8: case 0x2ec:
	case 0x1928: case 0x19a8:
		return UINT32_MAX;
	case DE0_CLK_REG:
		return UINT32_C(0x8700001f);
	case DE0_BGR_REG:
	case 0x1604: case 0x16cc: case 0x16ec:
		return UINT32_C(0x00010001);
	case 0xa74:
		return __BIT(16);
	case 0x1684:
		return UINT32_C(0x87001f1f);
	case 0x168c:
		return UINT32_C(0x00070001);
	case 0x1690:
		return UINT32_C(0x87000000);
	case AHB_MASTER_GATE_REG:
		return __BIT(31) | __BIT(5) | __BIT(4);
	case MBUS_MASTER_GATE_REG:
		return __BIT(11) | (sun60i_display_lease.memory_claimed ? __BIT(1) : 0);
	case 0x5b4:
		return sun60i_display_lease.memory_claimed ? UINT32_C(0x00010007) : 0;
	case 0x584:
		return sun60i_display_lease.memory_claimed ? UINT32_C(0x00010001) : 0;
	case 0x588:
		return sun60i_display_lease.memory_claimed ? UINT32_C(0x9f00001f) : 0;
	case 0x580:
		return sun60i_display_lease.memory_claimed ? UINT32_C(0xdf00001f) : 0;
	default:
		return 0;
	}
}

/* Classify every current hardware descriptor, including PLL_DE providers. */
static enum sun60i_a733_display_resource
sun60i_a733_display_resource(const struct sunxi_ccu_clk *clk,
    const struct sunxi_ccu_reset *reset)
{
	bus_size_t reg;

	if (reset != NULL)
		reg = reset->reg;
	else if (clk != NULL) {
		switch (clk->type) {
		case SUNXI_CCU_GATE: reg = clk->u.gate.reg; break;
		case SUNXI_CCU_DIV: reg = clk->u.div.reg; break;
		case SUNXI_CCU_NM: reg = clk->u.nm.reg; break;
		case SUNXI_CCU_NKMP: reg = clk->u.nkmp.reg; break;
		default: return A733_DISPLAY_RES_OTHER;
		}
	} else
		return A733_DISPLAY_RES_OTHER;
	if (reg == DE0_CLK_REG || reg == DE0_BGR_REG)
		return A733_DISPLAY_RES_DE0;
	if (sun60i_a733_display_mask(reg) == 0)
		return A733_DISPLAY_RES_OTHER;
	/* Fixed masks below refine shared master registers before admission. */
	if (reset != NULL &&
	    (reset->mask & sun60i_a733_display_mask(reg)) == 0)
		return A733_DISPLAY_RES_OTHER;
	if (clk != NULL && clk->type == SUNXI_CCU_GATE &&
	    (clk->u.gate.mask & sun60i_a733_display_mask(reg)) == 0)
		return A733_DISPLAY_RES_OTHER;
	return A733_DISPLAY_RES_DE_PLL;
}

/* Recheck actual RMW masks under the same mutex, including direct callbacks. */
static bool
sun60i_a733_native_mask_busy(bus_size_t reg, uint32_t mask)
{
	bool gpu = sun60i_gpu_lease.owner != NULL;
	bool npu = sun60i_npu_lease.owner != NULL;

	KASSERT(mutex_owned(&sun60i_a733_ccu_lock));
	if (sun60i_display_lease.core.owner != NULL &&
	    (mask & sun60i_a733_display_mask(reg)) != 0)
		return true;
	if (!gpu && !npu)
		return false;
	switch (reg) {
	case PLL_REF_CTRL_REG:
	case PLL_PERIPH0_CTRL_REG:
	case PLL_PERIPH0_CTRL_REG + 8:
	case PLL_PERIPH0_CTRL_REG + 12:
	case AHB_CFG_REG:
	case PERI0PLL_GATE_EN_REG:
	case PERI0PLL_GATE_STAT_REG:
		return mask != 0;
	case PLL_GPU0_CTRL_REG:
	case PLL_GPU0_CTRL_REG + 8:
	case PLL_GPU0_CTRL_REG + 12:
	case GPUPLL_GATE_EN_REG:
	case GPU0_CLK_REG:
	case GPU0_BGR_REG:
		return gpu && mask != 0;
	case PLL_NPU_CTRL_REG:
	case PLL_NPU_CTRL_REG + 8:
	case PLL_NPU_CTRL_REG + 12:
	case NPU_CLK_REG:
	case NPU_BGR_REG:
		return npu && mask != 0;
	case AHB_MASTER_GATE_REG:
		return (mask & ((gpu ? __BIT(7) : 0) | (npu ? __BIT(6) : 0))) != 0;
	case MBUS_MASTER_GATE_REG:
		return (mask & ((gpu ? __BIT(16) : 0) | (npu ? __BIT(18) : 0))) != 0;
	default:
		return false;
	}
}

static int
sun60i_a733_gpu_guard_enter(struct sunxi_ccu_softc *sc, struct clk *clk,
    struct sunxi_ccu_reset *reset, enum sunxi_ccu_mutation op, bool *handled)
{
	bool shared = false, local = false;
	struct sunxi_ccu_clk *descriptor = NULL;
	enum sun60i_a733_display_resource resource;
	int error = 0;

	mutex_enter(&sun60i_a733_ccu_lock);
	for (u_int id = 0; clk != NULL && id < sc->sc_nclks; id++) {
		if (clk == &sc->sc_clks[id].base) {
			descriptor = &sc->sc_clks[id];
			break;
		}
	}
	resource = sun60i_a733_display_resource(descriptor, reset);
	/* A normal enable must not recurse into parents before failing locally. */
	if (descriptor == &sc->sc_clks[A733_CLK_DE0] &&
	    sun60i_display_lease.core.owner == NULL) {
		*handled = true;
		error = EOPNOTSUPP;
		goto out;
	}
	if (sun60i_gpu_lease.owner != NULL || sun60i_npu_lease.owner != NULL ||
	    sun60i_display_lease.core.owner != NULL) {
		if (reset != NULL)
			local = (sun60i_gpu_lease.owner != NULL &&
			    reset->reg == GPU0_BGR_REG) ||
			    (sun60i_npu_lease.owner != NULL && reset->reg == NPU_BGR_REG);
		for (u_int id = 0; clk != NULL && id < sc->sc_nclks; id++) {
			if (clk != &sc->sc_clks[id].base)
				continue;
			shared = id == A733_CLK_PLL_REF || id == A733_CLK_SYS_24M ||
			    (id >= A733_CLK_PLL_PERIPH0_4X &&
			    id <= A733_CLK_PLL_PERIPH0_150M) || id == A733_CLK_AHB;
			local = (sun60i_gpu_lease.owner != NULL &&
			    (id == A733_CLK_GPU0 || id == A733_CLK_BUS_GPU0 ||
			    id == A733_CLK_AHB_GPU0 || id == A733_CLK_PLL_GPU0 ||
			    id == A733_CLK_MBUS_GPU0)) ||
			    (sun60i_npu_lease.owner != NULL &&
			    (id == A733_CLK_NPU || id == A733_CLK_BUS_NPU ||
			    id == A733_CLK_AHB_NPU || id == A733_CLK_MBUS_NPU ||
			    id == A733_CLK_PLL_NPU));
			break;
		}
	}
	if (sun60i_display_lease.core.owner != NULL &&
	    resource != A733_DISPLAY_RES_OTHER && !shared)
		local = true;
	if (shared || local) {
		*handled = true;
		/* Running ancestors are borrowed, never recursively re-enabled. */
		if (!shared || op != SUNXI_CCU_ENABLE)
			error = EBUSY;
	} else {
		error = sun60i_a733_display_writer_enter(&sun60i_display_lease.core, resource);
		if (error == 0)
			sun60i_gpu_lease.writers++;
	}
out:
	mutex_exit(&sun60i_a733_ccu_lock);
	return error;
}

static void
sun60i_a733_gpu_guard_exit(struct sunxi_ccu_softc *sc)
{
	mutex_enter(&sun60i_a733_ccu_lock);
	KASSERT(sun60i_gpu_lease.writers != 0);
	sun60i_gpu_lease.writers--;
	(void)sun60i_a733_display_writer_exit(&sun60i_display_lease.core);
	mutex_exit(&sun60i_a733_ccu_lock);
}

/* Only compare actual observations; the trial decode below is not readiness. */
static bool
sun60i_a733_gpu_same(const struct sun60i_a733_gpu_state *a,
    const struct sun60i_a733_gpu_state *b)
{
	/* Autonomous diagnostic gate status is not part of the old policy. */
	for (u_int s = 0; s < 2; s++) {
		for (u_int i = A733_GPU_REF; i <= A733_GPU_MASTER; i++) {
			uint32_t mask = i == A733_GPU_MASTER ? __BIT(7) : UINT32_MAX;
			if (((a->sample[s][i] ^ b->sample[s][i]) & mask) != 0)
				return false;
		}
	}
	return (b->changed & GPU_READINESS_REG_MASK) == 0 &&
	    memcmp(a->dcxo_sample, b->dcxo_sample, sizeof(a->dcxo_sample)) == 0 &&
	    memcmp(a->dcxo_hz, b->dcxo_hz, sizeof(a->dcxo_hz)) == 0 &&
	    memcmp(a->hosc_hz, b->hosc_hz, sizeof(a->hosc_hz)) == 0;
}

int
sun60i_a733_ccu_gpu_reserve(struct clk *gpu, const void *owner)
{
	struct sun60i_a733_gpu_state state, trial;
	int error;

	if (owner == NULL)
		return EINVAL;
	/* Validate the actual provider handle before touching its lease mutex. */
	error = sun60i_a733_ccu_gpu_inspect(gpu, &state);
	if (error != 0)
		return error;
	if (gpu->domain->priv != sun60i_gpu_lease.sc)
		return ENXIO;
	mutex_enter(&sun60i_a733_ccu_lock);
	if (sun60i_gpu_lease.owner != NULL || sun60i_gpu_lease.writers != 0) {
		error = EBUSY;
		goto out;
	}
	/* Fresh acquisition under arbitration excludes all native CCU writers. */
	error = sun60i_a733_ccu_gpu_inspect(gpu, &state);
	if (error != 0)
		goto out;
	if (state.sample[0][A733_GPU_MODULE] != 0 ||
	    (state.sample[0][A733_GPU_BUS] & (__BIT(16) | __BIT(0))) != 0) {
		error = EBUSY;
		goto out;
	}
	trial = state;
	trial.sample[0][A733_GPU_MODULE] = ACCEL_CLK_ENABLE |
	    __SHIFTIN(3, ACCEL_CLK_SEL);
	trial.sample[0][A733_GPU_BUS] |= __BIT(16) | __BIT(0);
	error = sun60i_a733_gpu_evaluate(&trial);
	if (error != 0)
		goto out;
	if (trial.core_hz != 400000000 || trial.bus_hz != 200000000) {
		error = EOPNOTSUPP;
		goto out;
	}
	sun60i_gpu_lease.before = state;
	sun60i_gpu_lease.owner = owner;
	sun60i_gpu_lease.attempted = false;
out:
	mutex_exit(&sun60i_a733_ccu_lock);
	return error;
}

int
sun60i_a733_ccu_gpu_release(struct clk *gpu, const void *owner)
{
	int error = 0;

	if (gpu != &sun60i_a733_ccu_clks[A733_CLK_GPU0].base || owner == NULL ||
	    sun60i_gpu_lease.sc == NULL)
		return EINVAL;
	mutex_enter(&sun60i_a733_ccu_lock);
	if (sun60i_gpu_lease.owner != owner)
		error = EINVAL;
	else if (sun60i_gpu_lease.attempted)
		error = EBUSY;
	else
		sun60i_gpu_lease.owner = NULL;
	mutex_exit(&sun60i_a733_ccu_lock);
	return error;
}

int
sun60i_a733_ccu_gpu_prepare(struct clk *gpu, const void *owner, bool *retained)
{
	struct sun60i_a733_gpu_state state;
	struct sunxi_ccu_softc *sc = sun60i_gpu_lease.sc;
	int error;

	if (gpu != &sun60i_a733_ccu_clks[A733_CLK_GPU0].base || owner == NULL ||
	    retained == NULL || sc == NULL)
		return EINVAL;
	mutex_enter(&sun60i_a733_ccu_lock);
	if (sun60i_gpu_lease.owner != owner || sun60i_gpu_lease.attempted) {
		error = EBUSY;
		goto out;
	}
	error = sun60i_a733_ccu_gpu_inspect(gpu, &state);
	if (error != 0)
		goto out;
	if (!sun60i_a733_gpu_same(&sun60i_gpu_lease.before, &state)) {
		error = EBUSY;
		goto out;
	}
	/* Even an unconfirmed configuration write forbids release/retry. */
	sun60i_gpu_lease.attempted = true;
	*retained = true;
	error = sun60i_a733_accel_write_locked(sc, GPU0_CLK_REG,
	    ACCEL_CLK_SEL | __BITS(3,0), __SHIFTIN(3, ACCEL_CLK_SEL));
	if (error != 0)
		goto out;
	/* BSP local order: release bus reset, bus gate, then module gate. */
	error = sun60i_a733_accel_write_locked(sc, GPU0_BGR_REG, __BIT(16), __BIT(16));
	if (error != 0)
		goto out;
	error = sun60i_a733_accel_write_locked(sc, GPU0_BGR_REG, __BIT(0), __BIT(0));
	if (error != 0)
		goto out;
	error = sun60i_a733_accel_write_locked(sc, GPU0_CLK_REG,
	    ACCEL_CLK_ENABLE, ACCEL_CLK_ENABLE);
	if (error != 0)
		goto out;
	/* GPU0_UPD self-clears when the configuration becomes valid. */
	for (u_int elapsed = 0;; elapsed += GPU_UPDATE_POLL_US) {
		if ((CCU_READ(sc, GPU0_CLK_REG) & GPU_CLK_UPDATE) == 0)
			break;
		if (elapsed == GPU_UPDATE_TIMEOUT_US) {
			error = ETIMEDOUT;
			break;
		}
		delay(GPU_UPDATE_POLL_US);
	}
out:
	mutex_exit(&sun60i_a733_ccu_lock);
	return error;
}

/*
 * Opt-in cold start of the dedicated GPU PLL. The pinned A733 BSP permits
 * N=53..105: 24 MHz * 75 / 3 gives 600 MHz without changing PLL_REF.
 * The caller must establish the 600 MHz operating voltage before this call.
 * This does not change the existing PERIPH0-400M preparation policy.
 */
int
sun60i_a733_ccu_gpu_prepare_pll(struct clk *gpu, const void *owner,
    bool *retained)
{
	const uint32_t active = ACCEL_PLL_ENABLE | ACCEL_PLL_LOCK_ENABLE |
	    ACCEL_PLL_LOCK | ACCEL_PLL_OUTPUT;
	const uint32_t factors = __SHIFTIN(74, ACCEL_PLL_N) |
	    __SHIFTIN(2, ACCEL_PLL_M);
	const uint32_t stable = ACCEL_PLL_ENABLE | ACCEL_PLL_LDO |
	    ACCEL_PLL_LOCK_ENABLE | ACCEL_PLL_LOCK;
	const uint32_t verify = stable | ACCEL_PLL_OUTPUT | ACCEL_PLL_N |
	    ACCEL_PLL_M | ACCEL_PLL_P;
	struct sun60i_a733_gpu_state state;
	struct sunxi_ccu_softc *sc = sun60i_gpu_lease.sc;
	uint32_t gate;
	u_int locked = 0;
	int error;

	if (gpu != &sun60i_a733_ccu_clks[A733_CLK_GPU0].base || owner == NULL ||
	    retained == NULL || sc == NULL)
		return EINVAL;
	mutex_enter(&sun60i_a733_ccu_lock);
	if (sun60i_gpu_lease.owner != owner || sun60i_gpu_lease.attempted) {
		error = EBUSY;
		goto out;
	}
	gate = CCU_READ(sc, GPUPLL_GATE_EN_REG);
	error = sun60i_a733_ccu_gpu_inspect(gpu, &state);
	if (error != 0)
		goto out;
	/* GPU0 is the only functional consumer of this dedicated PLL. */
	if (!sun60i_a733_gpu_same(&sun60i_gpu_lease.before, &state) ||
	    state.sample[0][A733_GPU_MODULE] != 0 ||
	    (state.sample[0][A733_GPU_BUS] & (__BIT(16) | __BIT(0))) != 0 ||
	    (state.sample[0][A733_GPU_PLL] & active) != 0 ||
	    gate != CCU_READ(sc, GPUPLL_GATE_EN_REG)) {
		error = EBUSY;
		goto out;
	}
	/* Manual section 4.1.6.252: bit 0=1 disables automatic gating. */
	if ((gate & GPUPLL_GATE_MANUAL) != GPUPLL_GATE_MANUAL ||
	    (state.sample[0][A733_GPU_PLL] & ACCEL_PLL_P) != 0 ||
	    (state.sample[0][A733_GPU_PAT0] & __BIT(31)) != 0 ||
	    (state.sample[0][A733_GPU_PAT1] & __BIT(27)) != 0) {
		error = EOPNOTSUPP;
		goto out;
	}
	/* Preserve reserved fields, including the firmware's bit 24 value. */
	sun60i_gpu_lease.attempted = true;
	*retained = true;
	error = sun60i_a733_accel_write_locked(sc, PLL_GPU0_CTRL_REG,
	    ACCEL_PLL_N | ACCEL_PLL_M, factors);
	if (error != 0)
		goto out;
	error = sun60i_a733_accel_write_locked(sc, PLL_GPU0_CTRL_REG,
	    ACCEL_PLL_LDO, ACCEL_PLL_LDO);
	if (error != 0)
		goto out;
	error = sun60i_a733_accel_write_locked(sc, PLL_GPU0_CTRL_REG,
	    ACCEL_PLL_ENABLE, ACCEL_PLL_ENABLE);
	if (error != 0)
		goto out;
	error = sun60i_a733_accel_write_locked(sc, PLL_GPU0_CTRL_REG,
	    ACCEL_PLL_LOCK_ENABLE, ACCEL_PLL_LOCK_ENABLE);
	if (error != 0)
		goto out;
	/* The BSP requires three consecutive lock samples, then settling. */
	for (u_int n = 0; n < GPU_PLL_LOCK_POLLS; n++) {
		if ((CCU_READ(sc, PLL_GPU0_CTRL_REG) & ACCEL_PLL_LOCK) != 0)
			locked++;
		else
			locked = 0;
		delay(GPU_PLL_LOCK_POLL_US);
		if (locked == 3)
			break;
	}
	if (locked != 3) {
		error = ETIMEDOUT;
		goto out;
	}
	delay(GPU_PLL_SETTLE_US);
	if ((CCU_READ(sc, PLL_GPU0_CTRL_REG) & verify) != (stable | factors) ||
	    CCU_READ(sc, GPUPLL_GATE_EN_REG) != gate) {
		error = EIO;
		goto out;
	}
	error = sun60i_a733_accel_write_locked(sc, PLL_GPU0_CTRL_REG,
	    ACCEL_PLL_OUTPUT, ACCEL_PLL_OUTPUT);
	if (error != 0)
		goto out;
	error = sun60i_a733_accel_write_locked(sc, GPU0_CLK_REG,
	    ACCEL_CLK_SEL | __BITS(3,0), 0);
	if (error != 0)
		goto out;
	error = sun60i_a733_accel_write_locked(sc, GPU0_BGR_REG, __BIT(16), __BIT(16));
	if (error != 0)
		goto out;
	error = sun60i_a733_accel_write_locked(sc, GPU0_BGR_REG, __BIT(0), __BIT(0));
	if (error != 0)
		goto out;
	error = sun60i_a733_accel_write_locked(sc, GPU0_CLK_REG,
	    ACCEL_CLK_ENABLE, ACCEL_CLK_ENABLE);
	if (error != 0)
		goto out;
	for (u_int elapsed = 0;; elapsed += GPU_UPDATE_POLL_US) {
		if ((CCU_READ(sc, GPU0_CLK_REG) & GPU_CLK_UPDATE) == 0)
			break;
		if (elapsed == GPU_UPDATE_TIMEOUT_US) {
			error = ETIMEDOUT;
			goto out;
		}
		delay(GPU_UPDATE_POLL_US);
	}
	/* Only the actual post-write observation can establish readiness. */
	error = sun60i_a733_ccu_gpu_inspect(gpu, &state);
	if (error != 0)
		goto out;
	error = state.readiness_error;
	if (error == 0 && (state.core_hz != 600000000 ||
	    state.bus_hz != 200000000 || CCU_READ(sc, GPUPLL_GATE_EN_REG) != gate))
		error = EIO;
out:
	mutex_exit(&sun60i_a733_ccu_lock);
	return error;
}

/* Origin: EmberBSD; AI-assisted fixed NPU borrowed-parent policy. */
static int
sun60i_a733_npu_handle(const struct clk *npu, struct sunxi_ccu_softc **result)
{
	if (npu != &sun60i_a733_ccu_clks[A733_CLK_NPU].base)
		return EINVAL;
	if (sun60i_gpu_lease.sc == NULL || npu->domain == NULL ||
	    npu->domain->priv != sun60i_gpu_lease.sc)
		return ENXIO;
	*result = sun60i_gpu_lease.sc;
	return 0;
}

static int
sun60i_a733_npu_observe(struct sunxi_ccu_softc *sc,
    struct sun60i_a733_npu_state *result)
{
	static const bus_size_t regs[A733_NPU_NREGS] = {
		PLL_REF_CTRL_REG, PLL_PERIPH0_CTRL_REG,
		PLL_PERIPH0_CTRL_REG + 8, PLL_PERIPH0_CTRL_REG + 12,
		AHB_CFG_REG, PERI0PLL_GATE_EN_REG, PERI0PLL_GATE_STAT_REG,
		NPU_CLK_REG, NPU_BGR_REG, AHB_MASTER_GATE_REG, MBUS_MASTER_GATE_REG
	};
	struct sun60i_a733_npu_state state = { 0 };
	struct sunxi_rtc_dcxo_state dcxo;
	struct clk *hosc;
	bus_addr_t addr;
	bus_size_t size;
	int node, error;

	KASSERT(mutex_owned(&sun60i_a733_ccu_lock));
	if (fdtbus_get_reg(sc->sc_phandle, 0, &addr, &size) != 0 ||
	    size < PERI0PLL_GATE_STAT_REG + sizeof(uint32_t))
		return ENXIO;
	if (OF_getproplen(sc->sc_phandle, "netbsd,dcxo-source") != 4)
		return EINVAL;
	node = fdtbus_get_phandle(sc->sc_phandle, "netbsd,dcxo-source");
	if (node <= 0 || !of_compatible_match(node, sun60i_a733_dcxo_compat))
		return EOPNOTSUPP;
	hosc = fdtbus_clock_get(sc->sc_phandle, "hosc");
	if (hosc == NULL)
		return ENXIO;
	state.hosc_hz[0] = clk_get_rate(hosc);
	error = sun60i_a733_dcxo_query(node, &dcxo);
	if (error != 0)
		goto out;
	memcpy(state.dcxo[0], dcxo.sample, sizeof(dcxo.sample));
	state.dcxo_hz[0] = dcxo.rate_hz;
	for (u_int n = 0; n < 2; n++) {
		for (u_int i = 0; i < __arraycount(regs); i++) {
			if (bus_space_peek_4(sc->sc_bst, sc->sc_bsh, regs[i],
			    &state.sample[n][i]) != 0) {
				error = EFAULT;
				goto out;
			}
		}
		/* Other native owners can change disjoint master bits. */
		state.sample[n][A733_NPU_AHB_MASTER] &= __BIT(31) | __BIT(6);
		state.sample[n][A733_NPU_MBUS_MASTER] &= __BIT(18);
	}
	state.hosc_hz[1] = clk_get_rate(hosc);
	error = sun60i_a733_dcxo_query(node, &dcxo);
	if (error != 0)
		goto out;
	memcpy(state.dcxo[1], dcxo.sample, sizeof(dcxo.sample));
	state.dcxo_hz[1] = dcxo.rate_hz;
	*result = state;
out:
	clk_put(hosc);
	return error;
}

static int
sun60i_a733_npu_parents(const struct sun60i_a733_npu_state *state)
{
	const uint32_t *v = state->sample[0];
	const uint32_t running = ACCEL_PLL_ENABLE | ACCEL_PLL_LDO |
	    ACCEL_PLL_LOCK_ENABLE | ACCEL_PLL_LOCK | ACCEL_PLL_OUTPUT;
	const uint32_t gate = __BIT(9) | __BIT(25);
	uint64_t numerator;
	u_int divisor, core, bus;
	int error;

	if (memcmp(state->sample[0], state->sample[1],
	    sizeof(state->sample[0])) != 0 ||
	    memcmp(state->dcxo[0], state->dcxo[1], sizeof(state->dcxo[0])) != 0 ||
	    state->dcxo[0][0] != state->dcxo[0][1] ||
	    state->dcxo_hz[0] != state->dcxo_hz[1] ||
	    state->hosc_hz[0] != state->hosc_hz[1])
		return EBUSY;
	if (state->dcxo_hz[0] == 0 || state->hosc_hz[0] != 24000000 ||
	    (v[A733_NPU_REF] & running) != running)
		return EOPNOTSUPP;
	divisor = (__SHIFTOUT(v[A733_NPU_REF], __BITS(22,16)) + 1) *
	    (__SHIFTOUT(v[A733_NPU_REF], ACCEL_PLL_P) + 1);
	numerator = (uint64_t)state->dcxo_hz[0] *
	    (__SHIFTOUT(v[A733_NPU_REF], ACCEL_PLL_N) + 1);
	if (numerator != UINT64_C(24000000) * divisor)
		return EOPNOTSUPP;
	error = sun60i_a733_gpu_pll_rate(v[A733_NPU_PERIPH], v[A733_NPU_PAT0],
	    v[A733_NPU_PAT1], ACCEL_PLL_M, ACCEL_PLL_OUTPUT, 2, &core);
	if (error != 0)
		return error;
	if (core != 600000000 || (v[A733_NPU_GATE] & gate) != gate ||
	    (v[A733_NPU_STATUS] & __BIT(25)) == 0)
		return EOPNOTSUPP;
	/* First profile borrows the observed static 200 MHz AHB path. */
	if (__SHIFTOUT(v[A733_NPU_AHB], __BITS(25,24)) != 3 ||
	    (v[A733_NPU_AHB_MASTER] & __BIT(31)) != 0)
		return EOPNOTSUPP;
	divisor = __SHIFTOUT(v[A733_NPU_AHB], __BITS(4,0)) + 1;
	if (core % divisor != 0)
		return EOPNOTSUPP;
	bus = core / divisor;
	return bus == 200000000 ? 0 : EOPNOTSUPP;
}

static bool
sun60i_a733_npu_shared_same(const struct sun60i_a733_npu_state *a,
    const struct sun60i_a733_npu_state *b)
{
	for (u_int n = 0; n < 2; n++)
		for (u_int i = A733_NPU_REF; i <= A733_NPU_STATUS; i++)
			if (a->sample[n][i] != b->sample[n][i])
				return false;
	return memcmp(a->dcxo, b->dcxo, sizeof(a->dcxo)) == 0 &&
	    memcmp(a->dcxo_hz, b->dcxo_hz, sizeof(a->dcxo_hz)) == 0 &&
	    memcmp(a->hosc_hz, b->hosc_hz, sizeof(a->hosc_hz)) == 0;
}

int
sun60i_a733_ccu_npu_reserve(struct clk *npu, const void *owner)
{
	struct sun60i_a733_npu_state state;
	struct sunxi_ccu_softc *sc;
	int error;

	if (owner == NULL)
		return EINVAL;
	error = sun60i_a733_npu_handle(npu, &sc);
	if (error != 0)
		return error;
	mutex_enter(&sun60i_a733_ccu_lock);
	if (sun60i_npu_lease.owner != NULL || sun60i_gpu_lease.writers != 0) {
		error = EBUSY;
		goto out;
	}
	error = sun60i_a733_npu_observe(sc, &state);
	if (error != 0 || (error = sun60i_a733_npu_parents(&state)) != 0)
		goto out;
	if ((state.sample[0][A733_NPU_MODULE] & ACCEL_CLK_ENABLE) != 0 ||
	    (state.sample[0][A733_NPU_BUS] & (__BITS(18,16) | __BIT(0))) != 0) {
		error = EBUSY;
		goto out;
	}
	sun60i_npu_lease.before = state;
	sun60i_npu_lease.owner = owner;
	sun60i_npu_lease.attempted = sun60i_npu_lease.prepared = false;
out:
	mutex_exit(&sun60i_a733_ccu_lock);
	return error;
}

int
sun60i_a733_ccu_npu_release(struct clk *npu, const void *owner)
{
	struct sunxi_ccu_softc *sc;
	int error = sun60i_a733_npu_handle(npu, &sc);

	if (error != 0)
		return error;
	mutex_enter(&sun60i_a733_ccu_lock);
	if (owner == NULL || sun60i_npu_lease.owner != owner)
		error = EINVAL;
	else if (sun60i_npu_lease.attempted)
		error = EBUSY;
	else
		memset(&sun60i_npu_lease, 0, sizeof(sun60i_npu_lease));
	mutex_exit(&sun60i_a733_ccu_lock);
	return error;
}

/* Only the fixed NPU profile can call this internal masked write. */
static int
sun60i_a733_npu_write(struct sunxi_ccu_softc *sc, bus_size_t reg,
    uint32_t mask, uint32_t bits)
{
	uint32_t allowed, old, value;

	KASSERT(mutex_owned(&sun60i_a733_ccu_lock));
	switch (reg) {
	case NPU_CLK_REG:
		allowed = ACCEL_CLK_ENABLE | ACCEL_CLK_SEL | __BITS(4,0);
		break;
	case NPU_BGR_REG:
		allowed = __BITS(18,16) | __BIT(0);
		break;
	case AHB_MASTER_GATE_REG:
		allowed = __BIT(6);
		break;
	case MBUS_MASTER_GATE_REG:
		allowed = __BIT(18);
		break;
	default:
		return EINVAL;
	}
	if (mask == 0 || (mask & ~allowed) != 0 || (bits & ~mask) != 0)
		return EINVAL;
	if (bus_space_peek_4(sc->sc_bst, sc->sc_bsh, reg, &old) != 0)
		return EFAULT;
	value = (old & ~mask) | bits;
	if (value == old)
		return 0;
	if (bus_space_poke_4(sc->sc_bst, sc->sc_bsh, reg, value) != 0)
		return EFAULT;
	bus_space_barrier(sc->sc_bst, sc->sc_bsh, reg, sizeof(value),
	    BUS_SPACE_BARRIER_WRITE | BUS_SPACE_BARRIER_READ);
	if (bus_space_peek_4(sc->sc_bst, sc->sc_bsh, reg, &value) != 0)
		return EFAULT;
	return (value & mask) == bits ? 0 : EIO;
}

static int
sun60i_a733_npu_check_ready(struct sunxi_ccu_softc *sc,
    struct sun60i_a733_npu_state *state)
{
	const uint32_t module = ACCEL_CLK_ENABLE | __SHIFTIN(2, ACCEL_CLK_SEL);
	int error = sun60i_a733_npu_observe(sc, state);

	if (error != 0 || (error = sun60i_a733_npu_parents(state)) != 0)
		return error;
	if ((state->sample[0][A733_NPU_MODULE] &
	    (ACCEL_CLK_ENABLE | ACCEL_CLK_SEL | __BITS(4,0))) != module ||
	    (state->sample[0][A733_NPU_BUS] & (__BITS(18,16) | __BIT(0))) !=
	    (__BITS(18,16) | __BIT(0)) ||
	    (state->sample[0][A733_NPU_AHB_MASTER] & __BIT(6)) == 0 ||
	    (state->sample[0][A733_NPU_MBUS_MASTER] & __BIT(18)) == 0)
		return EBUSY;
	return 0;
}

int
sun60i_a733_ccu_npu_prepare(struct clk *npu, const void *owner, bool *retained)
{
	static const struct { bus_size_t reg; uint32_t mask, bits; } steps[] = {
		{ NPU_CLK_REG, ACCEL_CLK_SEL | __BITS(4,0),
		    __SHIFTIN(2, ACCEL_CLK_SEL) },
		{ NPU_BGR_REG, __BIT(18), __BIT(18) },
		{ NPU_BGR_REG, __BIT(17), __BIT(17) },
		{ NPU_BGR_REG, __BIT(16), __BIT(16) },
		{ AHB_MASTER_GATE_REG, __BIT(6), __BIT(6) },
		{ MBUS_MASTER_GATE_REG, __BIT(18), __BIT(18) },
		{ NPU_BGR_REG, __BIT(0), __BIT(0) },
		{ NPU_CLK_REG, ACCEL_CLK_ENABLE, ACCEL_CLK_ENABLE },
	};
	struct sun60i_a733_npu_state state;
	struct sunxi_ccu_softc *sc;
	int error;

	if (owner == NULL || retained == NULL)
		return EINVAL;
	error = sun60i_a733_npu_handle(npu, &sc);
	if (error != 0)
		return error;
	mutex_enter(&sun60i_a733_ccu_lock);
	if (sun60i_npu_lease.owner != owner || sun60i_npu_lease.attempted) {
		error = EBUSY;
		goto out;
	}
	error = sun60i_a733_npu_observe(sc, &state);
	if (error != 0 || (error = sun60i_a733_npu_parents(&state)) != 0)
		goto out;
	if (memcmp(&sun60i_npu_lease.before, &state, sizeof(state)) != 0) {
		error = EBUSY;
		goto out;
	}
	/* Caller must retain PCK4 first. Failed writes cannot release this slot. */
	sun60i_npu_lease.attempted = true;
	*retained = true;
	for (u_int i = 0; i < __arraycount(steps); i++) {
		error = sun60i_a733_npu_write(sc, steps[i].reg,
		    steps[i].mask, steps[i].bits);
		if (error != 0)
			goto out;
	}
	error = sun60i_a733_npu_check_ready(sc, &state);
	if (error == 0 &&
	    !sun60i_a733_npu_shared_same(&sun60i_npu_lease.before, &state))
		error = EBUSY;
	if (error == 0) {
		sun60i_npu_lease.after = state;
		sun60i_npu_lease.prepared = true;
	}
out:
	mutex_exit(&sun60i_a733_ccu_lock);
	return error;
}

int
sun60i_a733_ccu_npu_ready(struct clk *npu, const void *owner)
{
	struct sun60i_a733_npu_state state;
	struct sunxi_ccu_softc *sc;
	int error = sun60i_a733_npu_handle(npu, &sc);

	if (error != 0)
		return error;
	mutex_enter(&sun60i_a733_ccu_lock);
	if (owner == NULL || sun60i_npu_lease.owner != owner ||
	    !sun60i_npu_lease.attempted || !sun60i_npu_lease.prepared) {
		error = EBUSY;
		goto out;
	}
	error = sun60i_a733_npu_check_ready(sc, &state);
	if (error == 0 && memcmp(&state, &sun60i_npu_lease.after, sizeof(state)) != 0)
		error = EBUSY;
out:
	mutex_exit(&sun60i_a733_ccu_lock);
	return error;
}

static int sun60i_a733_display_ccu_read(void *, uint32_t, uint32_t *);

/* Origin: EmberBSD; AI-assisted fixed native DISPLAY provider adapter. */
struct sun60i_a733_display_read_arg {
	struct sunxi_ccu_softc *sc;
	int power;
	const void *owner;
};

static int
sun60i_a733_display_handle(const struct clk *de0, struct sunxi_ccu_softc **out)
{
	if (de0 != &sun60i_a733_ccu_clks[A733_CLK_DE0].base)
		return EINVAL;
	if (sun60i_gpu_lease.sc == NULL || de0->domain == NULL ||
	    de0->domain->priv != sun60i_gpu_lease.sc)
		return ENXIO;
	*out = sun60i_gpu_lease.sc;
	return 0;
}

static int
sun60i_a733_display_inputs_read(void *arg,
    struct sun60i_a733_display_inputs *out)
{
	static const bus_size_t regs[A733_DISPLAY_NANCESTORS] = {
		PLL_REF_CTRL_REG, PLL_PERIPH0_CTRL_REG,
		PLL_PERIPH0_CTRL_REG + 8, PLL_PERIPH0_CTRL_REG + 12,
		AHB_CFG_REG, PERI0PLL_GATE_EN_REG, PERI0PLL_GATE_STAT_REG
	};
	const struct sun60i_a733_display_read_arg *ctx = arg;
	struct sunxi_ccu_softc *sc = ctx->sc;
	struct sun60i_a733_display_inputs in = { 0 };
	struct sunxi_rtc_dcxo_state dcxo;
	bus_addr_t addr;
	bus_size_t size;
	int node, error;

	KASSERT(mutex_owned(&sun60i_a733_ccu_lock));
	if (fdtbus_get_reg(sc->sc_phandle, 0, &addr, &size) != 0 ||
	    size < 0x19ac)
		return ENXIO;
	if (OF_getproplen(sc->sc_phandle, "netbsd,dcxo-source") != 4)
		return EINVAL;
	node = fdtbus_get_phandle(sc->sc_phandle, "netbsd,dcxo-source");
	if (node <= 0 || !of_compatible_match(node, sun60i_a733_dcxo_compat))
		return EOPNOTSUPP;
	error = sun60i_a733_dcxo_query(node, &dcxo);
	if (error != 0)
		return error;
	memcpy(in.dcxo[0], dcxo.sample, sizeof(dcxo.sample));
	error = sun60i_a733_display_read_clocks(sun60i_a733_display_ccu_read,
	    sc, size, &in.clocks);
	if (error != 0)
		return error;
	for (u_int s = 0; s < 2; s++) {
		for (u_int i = 0; i < __arraycount(regs); i++) {
			if (bus_space_peek_4(sc->sc_bst, sc->sc_bsh, regs[i],
			    &in.ancestor[s][i]) != 0)
				return EFAULT;
		}
	}
	error = sun60i_a733_pck_display_snapshot(ctx->power, ctx->owner, &in.power);
	if (error != 0)
		return error;
	error = sun60i_a733_dcxo_query(node, &dcxo);
	if (error != 0)
		return error;
	memcpy(in.dcxo[1], dcxo.sample, sizeof(dcxo.sample));
	*out = in;
	return 0;
}

/* Compare only DISPLAY-owned fields. Other accelerator masks are disjoint. */
static bool
sun60i_a733_display_same(const struct sun60i_a733_display_inputs *a,
    const struct sun60i_a733_display_inputs *b)
{
	if (memcmp(a->power.sample, b->power.sample, sizeof(a->power.sample)) != 0 ||
	    memcmp(a->ancestor, b->ancestor, sizeof(a->ancestor)) != 0 ||
	    memcmp(a->dcxo, b->dcxo, sizeof(a->dcxo)) != 0)
		return false;
	for (u_int s = 0; s < 2; s++) {
		for (u_int i = 0; i < A733_DISPLAY_NCLOCK_REGS; i++) {
			const uint32_t mask = sun60i_a733_display_mask(
			    sun60i_a733_display_clock_regs[i].offset);
			if (((a->clocks.sample[s][i] ^ b->clocks.sample[s][i]) & mask) != 0)
				return false;
		}
	}
	return true;
}

static int
sun60i_a733_display_check(struct sun60i_a733_display_inputs *in)
{
	struct sun60i_a733_display_rates rates;
	const uint32_t *v = in->clocks.sample[0];
	int error;

	if (in->clocks.changed != 0 || in->power.changed != 0)
		return EBUSY;
	error = sun60i_a733_display_running_ancestors(in, &rates);
	if (error != 0)
		return error;
	if ((v[A733_DISPLAY_DE0] & UINT32_C(0x8700001f)) !=
	    UINT32_C(0x83000000) ||
	    (v[A733_DISPLAY_DE0_BUS] & UINT32_C(0x00010001)) !=
	    UINT32_C(0x00010001))
		return EBUSY;
	return 0;
}

static void
sun60i_a733_display_fault_locked(int error)
{
	KASSERT(mutex_owned(&sun60i_a733_ccu_lock));
	KASSERT(error > 0 && sun60i_display_lease.pck_retained);
	if (sun60i_display_lease.fault == 0)
		sun60i_display_lease.fault = error;
	if (sun60i_display_lease.core.retained)
		(void)sun60i_a733_display_fault(&sun60i_display_lease.core,
		    sun60i_display_lease.core.owner, error);
}

/* Raw memory-path observations. The DE consumer owns the finite policy. */
static const struct {
	bus_size_t reg;
	uint32_t mask;
} sun60i_a733_display_memory_regs[A733_DISPLAY_NMEMORY_REGS] = {
	{ 0x5b4, UINT32_C(0x00010007) },
	{ 0x584, UINT32_C(0x00010001) },
	{ 0x588, UINT32_C(0x9f00001f) },
	{ MBUS_MASTER_GATE_REG, __BIT(1) | __BIT(11) },
	{ PERI0PLL_GATE_EN_REG, __BIT(9) | __BIT(25) },
	{ PERI0PLL_GATE_STAT_REG, __BIT(25) },
	{ 0x580, UINT32_C(0xdf00001f) },
};

static int
sun60i_a733_display_memory_read(struct sunxi_ccu_softc *sc,
    uint32_t out[2][A733_DISPLAY_NMEMORY_REGS])
{
	struct sun60i_a733_display_power power;
	uint32_t words[2][A733_DISPLAY_NMEMORY_REGS];
	bus_addr_t addr;
	bus_size_t size;
	int error;

	KASSERT(mutex_owned(&sun60i_a733_ccu_lock));
	if (fdtbus_get_reg(sc->sc_phandle, 0, &addr, &size) != 0 ||
	    size < PERI0PLL_GATE_STAT_REG + sizeof(uint32_t))
		return ENXIO;
	error = sun60i_a733_pck_display_snapshot(sun60i_display_lease.power,
	    sun60i_display_lease.core.owner, &power);
	if (error != 0)
		return error;
	for (u_int s = 0; s < 2; s++) {
		for (u_int i = 0; i < A733_DISPLAY_NMEMORY_REGS; i++) {
			if (bus_space_peek_4(sc->sc_bst, sc->sc_bsh,
			    sun60i_a733_display_memory_regs[i].reg, &words[s][i]) != 0)
				return EFAULT;
		}
	}
	for (u_int i = 0; i < A733_DISPLAY_NMEMORY_REGS; i++) {
		if (((words[0][i] ^ words[1][i]) &
		    sun60i_a733_display_memory_regs[i].mask) != 0)
			return EBUSY;
	}
	memcpy(out, words, sizeof(words));
	return 0;
}

static int
sun60i_a733_display_memory_check(struct sunxi_ccu_softc *sc,
    uint32_t out[2][A733_DISPLAY_NMEMORY_REGS])
{
	uint32_t words[2][A733_DISPLAY_NMEMORY_REGS];
	int error = sun60i_a733_display_memory_read(sc, words);

	if (error != 0)
		return error;
	for (u_int s = 0; s < 2; s++) {
		for (u_int i = 0; i < A733_DISPLAY_NMEMORY_REGS; i++) {
			if (((words[s][i] ^ sun60i_display_lease.memory[s][i]) &
			    sun60i_a733_display_memory_regs[i].mask) != 0)
				return EBUSY;
		}
	}
	memcpy(out, words, sizeof(words));
	return 0;
}

int
sun60i_a733_ccu_display_memory_reserve(struct clk *de0, const void *owner)
{
	struct sunxi_ccu_softc *sc;
	uint32_t words[2][A733_DISPLAY_NMEMORY_REGS];
	int error = sun60i_a733_display_handle(de0, &sc);

	if (error != 0)
		return error;
	mutex_enter(&sun60i_a733_ccu_lock);
	if (owner == NULL || sun60i_display_lease.core.owner != owner) {
		error = EINVAL;
		goto out;
	}
	if (sun60i_display_lease.memory_claimed ||
	    sun60i_display_lease.pck_retained || sun60i_display_lease.fault != 0 ||
	    sun60i_display_lease.core.writers != 0) {
		error = EBUSY;
		goto out;
	}
	error = sun60i_a733_display_memory_read(sc, words);
	if (error == 0) {
		memcpy(sun60i_display_lease.memory, words, sizeof(words));
		sun60i_display_lease.memory_claimed = true;
	}
out:
	mutex_exit(&sun60i_a733_ccu_lock);
	return error;
}

int
sun60i_a733_ccu_display_memory_state(struct clk *de0, const void *owner,
    uint32_t out[2][A733_DISPLAY_NMEMORY_REGS])
{
	struct sunxi_ccu_softc *sc;
	uint32_t words[2][A733_DISPLAY_NMEMORY_REGS];
	int error = sun60i_a733_display_handle(de0, &sc);

	if (error != 0)
		return error;
	if (out == NULL)
		return EINVAL;
	mutex_enter(&sun60i_a733_ccu_lock);
	if (owner == NULL || sun60i_display_lease.core.owner != owner ||
	    !sun60i_display_lease.memory_claimed) {
		error = EINVAL;
		goto out;
	}
	if (sun60i_display_lease.fault != 0) {
		error = EBUSY;
		goto out;
	}
	error = sun60i_a733_display_memory_check(sc, words);
	if (error == 0)
		memcpy(out, words, sizeof(words));
	else if (sun60i_display_lease.pck_retained)
		sun60i_a733_display_fault_locked(error);
out:
	mutex_exit(&sun60i_a733_ccu_lock);
	return error;
}

/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD; AI-assisted fixed IOMMU1 peer isolation admission. */
/* Register facts: A733 manual v1.00. No peer MMIO or hardware writes. */
static int
sun60i_a733_display_isolation_read(struct sunxi_ccu_softc *sc,
    uint32_t out[2][A733_DISPLAY_NISOLATION_REGS])
{
	uint32_t words[2][A733_DISPLAY_NISOLATION_REGS], power[2][3], after[2][3];
	bus_addr_t addr;
	bus_size_t size;
	int error;

	KASSERT(mutex_owned(&sun60i_a733_ccu_lock));
	if (fdtbus_get_reg(sc->sc_phandle, 0, &addr, &size) != 0 || size < 0xa90)
		return ENXIO;
	error = sun60i_a733_pck_display_isolation_snapshot(
	    sun60i_display_lease.power, sun60i_display_lease.core.owner, power);
	if (error != 0)
		return error;
	for (u_int s = 0; s < 2; s++) {
		for (u_int i = 0; i < A733_DISPLAY_NISOLATION_REGS; i++) {
			if (bus_space_peek_4(sc->sc_bst, sc->sc_bsh,
			    sun60i_a733_display_isolation_regs[i].reg,
			    &words[s][i]) != 0)
				return EFAULT;
		}
	}
	for (u_int s = 0; s < 2; s++)
		for (u_int i = 0; i < A733_DISPLAY_NISOLATION_REGS; i++)
			if ((words[s][i] &
			    sun60i_a733_display_isolation_regs[i].mask) != 0)
				return EBUSY;
	error = sun60i_a733_pck_display_isolation_snapshot(
	    sun60i_display_lease.power, sun60i_display_lease.core.owner, after);
	if (error != 0)
		return error;
	if (memcmp(power, after, sizeof(power)) != 0)
		return EBUSY;
	memcpy(out, words, sizeof(words));
	return 0;
}

int
sun60i_a733_ccu_display_isolation_reserve(struct clk *de0, const void *owner)
{
	struct sunxi_ccu_softc *sc;
	uint32_t words[2][A733_DISPLAY_NISOLATION_REGS];
	int error = sun60i_a733_display_handle(de0, &sc), cleanup;

	if (error != 0)
		return error;
	mutex_enter(&sun60i_a733_ccu_lock);
	if (owner == NULL || sun60i_display_lease.core.owner != owner ||
	    !sun60i_display_lease.memory_claimed) {
		error = EINVAL;
		goto done;
	}
	if (sun60i_display_lease.isolation_claimed ||
	    sun60i_display_lease.pck_retained || sun60i_display_lease.fault != 0 ||
	    sun60i_display_lease.core.writers != 0) {
		error = EBUSY;
		goto done;
	}
	error = sun60i_a733_pck_display_isolation_reserve(
	    sun60i_display_lease.power, owner);
	if (error != 0)
		goto done;
	/* Keep the masks until a failed acquisition releases its PCK slot. */
	sun60i_display_lease.isolation_claimed = true;
	error = sun60i_a733_display_isolation_read(sc, words);
	if (error != 0) {
		cleanup = sun60i_a733_pck_display_isolation_release(
		    sun60i_display_lease.power, owner);
		if (cleanup == 0)
			sun60i_display_lease.isolation_claimed = false;
		else
			sun60i_display_lease.fault = error;
	}
done:
	mutex_exit(&sun60i_a733_ccu_lock);
	return error;
}

int
sun60i_a733_ccu_display_isolation_state(struct clk *de0, const void *owner,
    uint32_t out[2][A733_DISPLAY_NISOLATION_REGS])
{
	struct sunxi_ccu_softc *sc;
	uint32_t words[2][A733_DISPLAY_NISOLATION_REGS];
	int error = sun60i_a733_display_handle(de0, &sc);

	if (error != 0)
		return error;
	if (out == NULL)
		return EINVAL;
	mutex_enter(&sun60i_a733_ccu_lock);
	if (owner == NULL || sun60i_display_lease.core.owner != owner ||
	    !sun60i_display_lease.isolation_claimed) {
		error = EINVAL;
		goto done;
	}
	if (sun60i_display_lease.fault != 0) {
		error = EBUSY;
		goto done;
	}
	error = sun60i_a733_display_isolation_read(sc, words);
	if (error == 0)
		memcpy(out, words, sizeof(words));
	else if (sun60i_display_lease.pck_retained)
		sun60i_a733_display_fault_locked(error);
done:
	mutex_exit(&sun60i_a733_ccu_lock);
	return error;
}

int
sun60i_a733_ccu_display_reserve(struct clk *de0, int power, const void *owner)
{
	struct sun60i_a733_display_rates rates;
	struct sunxi_ccu_softc *sc;
	struct sun60i_a733_display_read_arg arg;
	int error;

	if (owner == NULL)
		return EINVAL;
	error = sun60i_a733_display_handle(de0, &sc);
	if (error != 0)
		return error;
	arg = (struct sun60i_a733_display_read_arg){ sc, power, owner };
	mutex_enter(&sun60i_a733_ccu_lock);
	error = sun60i_a733_display_reserve(&sun60i_display_lease.core,
	    owner, sun60i_a733_display_inputs_read, &arg);
	if (error == 0) {
		/* Borrowed enable admission must already refer to running parents. */
		error = sun60i_a733_display_running_ancestors(
		    &sun60i_display_lease.core.before, &rates);
		if (error != 0)
			(void)sun60i_a733_display_release(&sun60i_display_lease.core, owner);
		else
			sun60i_display_lease.power = power;
	}
	mutex_exit(&sun60i_a733_ccu_lock);
	return error;
}

int
sun60i_a733_ccu_display_release(struct clk *de0, const void *owner)
{
	struct sunxi_ccu_softc *sc;
	int error = sun60i_a733_display_handle(de0, &sc);

	if (error != 0)
		return error;
	mutex_enter(&sun60i_a733_ccu_lock);
	if (owner == NULL || sun60i_display_lease.core.owner != owner)
		error = EINVAL;
	else if (sun60i_display_lease.pck_retained || sun60i_display_lease.fault != 0)
		error = EBUSY;

	else if (sun60i_display_lease.core.retained ||
	    sun60i_display_lease.core.checking ||
	    sun60i_display_lease.core.fault != 0)
		error = EBUSY;
	else {
		if (sun60i_display_lease.isolation_claimed) {
			error = sun60i_a733_pck_display_isolation_release(
		    sun60i_display_lease.power, owner);
			if (error != 0) {
				mutex_exit(&sun60i_a733_ccu_lock);
				return error;
			}
			sun60i_display_lease.isolation_claimed = false;
		}
		error = sun60i_a733_display_release(&sun60i_display_lease.core, owner);
		if (error == 0) {
			sun60i_display_lease.power = 0;
			sun60i_display_lease.prepared = false;
			sun60i_display_lease.hdmi_prepared = false;
			sun60i_display_lease.tv0_prepared = false;
			sun60i_display_lease.memory_claimed = false;
			memset(sun60i_display_lease.memory, 0, sizeof(sun60i_display_lease.memory));
			memset(&sun60i_display_lease.after, 0, sizeof(sun60i_display_lease.after));
		}
	}
	mutex_exit(&sun60i_a733_ccu_lock);
	return error;
}

/* No caller can select another register or mask. */
static int
sun60i_a733_display_local_write(struct sunxi_ccu_softc *sc, bus_size_t reg,
    uint32_t mask, uint32_t bits)
{
	uint32_t old, value, allowed;

	KASSERT(mutex_owned(&sun60i_a733_ccu_lock));
	KASSERT(sun60i_display_lease.pck_retained && sun60i_display_lease.core.retained);
	allowed = reg == DE0_CLK_REG ? UINT32_C(0x8700001f) :
	    reg == DE0_BGR_REG || reg == 0x1604 ? UINT32_C(0x00010001) :
	    reg == 0x168c ? UINT32_C(0x00030001) :
	    reg == 0x1690 ? UINT32_C(0x87000000) : 0;
	if (mask == 0 || (mask & ~allowed) != 0 || (bits & ~mask) != 0)
		return EINVAL;
	if (bus_space_peek_4(sc->sc_bst, sc->sc_bsh, reg, &old) != 0)
		return EFAULT;
	value = (old & ~mask) | bits;
	if (value == old)
		return 0;
	if (bus_space_poke_4(sc->sc_bst, sc->sc_bsh, reg, value) != 0)
		return EFAULT;
	bus_space_barrier(sc->sc_bst, sc->sc_bsh, reg, sizeof(value),
	    BUS_SPACE_BARRIER_WRITE | BUS_SPACE_BARRIER_READ);
	if (bus_space_peek_4(sc->sc_bst, sc->sc_bsh, reg, &value) != 0)
		return EFAULT;
	return (value & mask) == bits ? 0 : EIO;
}

int
sun60i_a733_ccu_display_begin_de0(struct clk *de0, const void *owner,
    bool *retained)
{
	static const struct { bus_size_t reg; uint32_t mask, bits; } steps[] = {
		{ DE0_CLK_REG, UINT32_C(0x0700001f), UINT32_C(0x03000000) },
		{ DE0_BGR_REG, __BIT(16), __BIT(16) },
		{ DE0_BGR_REG, __BIT(0), __BIT(0) },
		{ DE0_CLK_REG, __BIT(31), __BIT(31) },
	};
	struct sun60i_a733_display_inputs in, expected;
	uint32_t memory[2][A733_DISPLAY_NMEMORY_REGS];
	struct sun60i_a733_display_read_arg arg;
	struct sunxi_ccu_softc *sc;
	int error;

	if (owner == NULL || retained == NULL)
		return EINVAL;
	*retained = false;
	error = sun60i_a733_display_handle(de0, &sc);
	if (error != 0)
		return error;
	mutex_enter(&sun60i_a733_ccu_lock);
	if (sun60i_display_lease.core.owner != owner) {
		error = EINVAL;
		goto out;
	}
	*retained = sun60i_display_lease.pck_retained;
	if (*retained || sun60i_display_lease.fault != 0) {
		error = EBUSY;
		goto out;
	}
	arg = (struct sun60i_a733_display_read_arg){ sc, sun60i_display_lease.power, owner };
	/* PCK retention precedes even the first faulting CCU write attempt. */
	error = sun60i_a733_pck_display_retain(arg.power, owner);
	if (error != 0)
		goto out;

	sun60i_display_lease.pck_retained = *retained = true;
	if (sun60i_display_lease.isolation_claimed) {
		error = sun60i_a733_pck_display_isolation_retain(arg.power, owner);
		if (error != 0)
			goto failed;
	}
	error = sun60i_a733_display_begin_de0(&sun60i_display_lease.core,
	    owner, sun60i_a733_display_inputs_read, &arg);
	if (error != 0)
		goto failed;
	if (sun60i_display_lease.memory_claimed) {
		error = sun60i_a733_display_memory_check(sc, memory);
		if (error != 0)
			goto failed;
	}

	if (sun60i_display_lease.isolation_claimed) {
		uint32_t isolation[2][A733_DISPLAY_NISOLATION_REGS];

		error = sun60i_a733_display_isolation_read(sc, isolation);
		if (error != 0)
			goto failed;
	}
	for (u_int i = 0; i < __arraycount(steps); i++) {
		error = sun60i_a733_display_local_write(sc, steps[i].reg,
		    steps[i].mask, steps[i].bits);
		if (error != 0)
			goto failed;
	}
	error = sun60i_a733_display_inputs_read(&arg, &in);
	if (error == 0)
		error = sun60i_a733_display_check(&in);
	if (error != 0)
		goto failed;
	expected = sun60i_display_lease.core.before;
	for (u_int s = 0; s < 2; s++) {
		expected.clocks.sample[s][A733_DISPLAY_DE0] =
		    (expected.clocks.sample[s][A733_DISPLAY_DE0] & ~UINT32_C(0x8700001f)) |
		    UINT32_C(0x83000000);
		expected.clocks.sample[s][A733_DISPLAY_DE0_BUS] |= UINT32_C(0x00010001);
	}
	if (!sun60i_a733_display_same(&expected, &in)) {
		error = EBUSY;
		goto failed;
	}
	sun60i_display_lease.after = in;
	sun60i_display_lease.prepared = true;
	goto out;
failed:
	sun60i_a733_display_fault_locked(error);
out:
	mutex_exit(&sun60i_a733_ccu_lock);
	return error;
}

/* The caller holds the provider lock and has checked the retained owner. */
static int
sun60i_a733_display_verify_locked(struct sunxi_ccu_softc *sc,
    const void *owner, const struct sun60i_a733_display_inputs *expected,
    struct sun60i_a733_display_inputs *out)
{
	struct sun60i_a733_display_inputs in;
	uint32_t memory[2][A733_DISPLAY_NMEMORY_REGS];
	struct sun60i_a733_display_read_arg arg = {
	    sc, sun60i_display_lease.power, owner
	};
	int error;

	KASSERT(mutex_owned(&sun60i_a733_ccu_lock));
	error = sun60i_a733_pck_display_ready(arg.power, owner);
	if (error == 0)
		error = sun60i_a733_display_inputs_read(&arg, &in);
	if (error == 0)
		error = sun60i_a733_display_check(&in);
	if (error == 0 && !sun60i_a733_display_same(expected, &in))
		error = EBUSY;
	if (error == 0 && sun60i_display_lease.memory_claimed)
		error = sun60i_a733_display_memory_check(sc, memory);

	if (error == 0 && sun60i_display_lease.isolation_claimed) {
		uint32_t isolation[2][A733_DISPLAY_NISOLATION_REGS];

		error = sun60i_a733_display_isolation_read(sc, isolation);
	}
	if (error == 0)
		*out = in;
	return error;
}

static int
sun60i_a733_display_ready_locked(struct sunxi_ccu_softc *sc,
    const void *owner)
{
	struct sun60i_a733_display_inputs in;
	int error;

	KASSERT(mutex_owned(&sun60i_a733_ccu_lock));
	if (owner == NULL || sun60i_display_lease.core.owner != owner ||
	    !sun60i_display_lease.pck_retained || !sun60i_display_lease.prepared ||
	    sun60i_display_lease.fault != 0)
		return EBUSY;
	error = sun60i_a733_display_verify_locked(sc, owner,
	    &sun60i_display_lease.after, &in);
	if (error != 0)
		sun60i_a733_display_fault_locked(error);
	return error;
}

int
sun60i_a733_ccu_display_ready(struct clk *de0, const void *owner)
{
	struct sunxi_ccu_softc *sc;
	int error = sun60i_a733_display_handle(de0, &sc);

	if (error != 0)
		return error;
	mutex_enter(&sun60i_a733_ccu_lock);
	error = sun60i_a733_display_ready_locked(sc, owner);
	mutex_exit(&sun60i_a733_ccu_lock);
	return error;
}

/* Fixed phases only. No caller supplies a register, mask, parent or rate. */
static int
sun60i_a733_display_output_begin(struct clk *de0, const void *owner,
    bool tv0)
{
	static const struct {
		bus_size_t reg;
		uint32_t mask, bits;
		enum sun60i_a733_display_clock_index index;
	} steps[] = {
		{ 0x168c, __BIT(16), __BIT(16), A733_DISPLAY_HDMI_BUS },
		{ 0x168c, __BIT(17), __BIT(17), A733_DISPLAY_HDMI_BUS },
		{ 0x1690, UINT32_C(0x07000000), 0, A733_DISPLAY_HDMI_SFR },
		{ 0x1690, __BIT(31), __BIT(31), A733_DISPLAY_HDMI_SFR },
		{ 0x168c, __BIT(0), __BIT(0), A733_DISPLAY_HDMI_BUS },
		{ 0x1604, __BIT(16), __BIT(16), A733_DISPLAY_TV0_BUS },
		{ 0x1604, __BIT(0), __BIT(0), A733_DISPLAY_TV0_BUS },
	};
	struct sun60i_a733_display_inputs expected, in;
	struct sunxi_ccu_softc *sc;
	int error = sun60i_a733_display_handle(de0, &sc);

	if (error != 0)
		return error;
	mutex_enter(&sun60i_a733_ccu_lock);
	/* Wrong owners and invalid phase order do not change the valid lease. */
	if (owner == NULL || sun60i_display_lease.core.owner != owner ||
	    !sun60i_display_lease.pck_retained || !sun60i_display_lease.prepared ||
	    sun60i_display_lease.fault != 0 ||
	    (tv0 ? (!sun60i_display_lease.hdmi_prepared ||
	    sun60i_display_lease.tv0_prepared) : sun60i_display_lease.hdmi_prepared)) {
		error = EBUSY;
		goto out;
	}
	error = sun60i_a733_display_ready_locked(sc, owner);
	if (error != 0)
		goto out;
	expected = sun60i_display_lease.after;
	if (!tv0 &&
	    (expected.clocks.sample[0][A733_DISPLAY_HDMI_SFR] & __BIT(31)) != 0 &&
	    (expected.clocks.sample[0][A733_DISPLAY_HDMI_SFR] &
	    UINT32_C(0x07000000)) != 0) {
		/* Never change the source of a running SFR clock. */
		error = EBUSY;
		goto failed;
	}
	for (u_int i = tv0 ? 5 : 0; i < (tv0 ? 7 : 5); i++) {
		error = sun60i_a733_display_local_write(sc, steps[i].reg,
		    steps[i].mask, steps[i].bits);
		if (error != 0)
			goto failed;
		for (u_int s = 0; s < 2; s++) {
			expected.clocks.sample[s][steps[i].index] =
			    (expected.clocks.sample[s][steps[i].index] & ~steps[i].mask) |
			    steps[i].bits;
		}
	}
	error = sun60i_a733_display_verify_locked(sc, owner, &expected, &in);
	if (error != 0)
		goto failed;
	/* Publish the new expected state only after the complete phase passes. */
	sun60i_display_lease.after = in;
	if (tv0)
		sun60i_display_lease.tv0_prepared = true;
	else
		sun60i_display_lease.hdmi_prepared = true;
	goto out;
failed:
	sun60i_a733_display_fault_locked(error);
out:
	mutex_exit(&sun60i_a733_ccu_lock);
	return error;
}

int
sun60i_a733_ccu_display_begin_hdmi(struct clk *de0, const void *owner)
{
	return sun60i_a733_display_output_begin(de0, owner, false);
}

int
sun60i_a733_ccu_display_begin_tv0(struct clk *de0, const void *owner)
{
	return sun60i_a733_display_output_begin(de0, owner, true);
}

int
sun60i_a733_ccu_display_hdmi_clock_state(struct clk *de0, const void *owner,
    struct sun60i_a733_display_hdmi_clock *out)
{
	struct sun60i_a733_display_hdmi_clock state;
	struct sun60i_a733_display_rates rates;
	struct sunxi_ccu_softc *sc;
	uint32_t sfr;
	int error;

	if (out == NULL)
		return EINVAL;
	error = sun60i_a733_display_handle(de0, &sc);
	if (error != 0)
		return error;
	mutex_enter(&sun60i_a733_ccu_lock);
	if (!sun60i_display_lease.hdmi_prepared) {
		error = EBUSY;
		goto out;
	}
	error = sun60i_a733_display_ready_locked(sc, owner);
	if (error != 0)
		goto out;
	/* ready_locked proved that these protected fields still match hardware. */
	sfr = sun60i_display_lease.after.clocks.sample[0][A733_DISPLAY_HDMI_SFR];
	error = sun60i_a733_display_running_ancestors(&sun60i_display_lease.after,
	    &rates);
	if (error == 0 && (sfr & UINT32_C(0x87000000)) != UINT32_C(0x80000000))
		error = EBUSY;
	if (error != 0) {
		sun60i_a733_display_fault_locked(error);
		goto out;
	}
	state.source = (sfr >> 24) & 7;
	state.divisor = 1; /* HDMI_SFR has no divider field. */
	state.hz = rates.reference_hz;
	*out = state;
out:
	mutex_exit(&sun60i_a733_ccu_lock);
	return error;
}

int
sun60i_a733_ccu_display_status(struct clk *de0, const void *owner,
    struct sun60i_a733_display_owner_status *out)
{
	struct sunxi_ccu_softc *sc;
	struct sun60i_a733_display_owner_status status;
	int error = sun60i_a733_display_handle(de0, &sc);

	if (error != 0)
		return error;
	if (out == NULL)
		return EINVAL;
	mutex_enter(&sun60i_a733_ccu_lock);
	if (owner == NULL || sun60i_display_lease.core.owner != owner)
		error = EINVAL;
	else {
		error = sun60i_a733_display_owner_status(&sun60i_display_lease.core, &status);
		if (error == 0) {
			status.retained |= sun60i_display_lease.pck_retained;
			if (sun60i_display_lease.fault != 0)
				status.fault = sun60i_display_lease.fault;
			*out = status;
		}
	}
	mutex_exit(&sun60i_a733_ccu_lock);
	return error;
}

int
sun60i_a733_ccu_display_fault(struct clk *de0, const void *owner, int fault)
{
	struct sunxi_ccu_softc *sc;
	int error = sun60i_a733_display_handle(de0, &sc);

	if (error != 0)
		return error;
	mutex_enter(&sun60i_a733_ccu_lock);
	if (owner == NULL || sun60i_display_lease.core.owner != owner ||
	    !sun60i_display_lease.pck_retained || fault <= 0)
		error = EINVAL;
	else
		sun60i_a733_display_fault_locked(fault);
	mutex_exit(&sun60i_a733_ccu_lock);
	return error;
}

static int
sun60i_a733_display_ccu_read(void *arg, uint32_t reg, uint32_t *value)
{
	struct sunxi_ccu_softc *sc = arg;

	return bus_space_peek_4(sc->sc_bst, sc->sc_bsh, reg, value) == 0 ?
	    0 : EFAULT;
}

static void
sun60i_a733_display_ccu_report(struct sunxi_ccu_softc *sc)
{
	struct sun60i_a733_display_clocks state;
	struct sun60i_a733_display_clock_facts facts;
	bus_addr_t addr;
	bus_size_t size;
	int error;

	error = fdtbus_get_reg(sc->sc_phandle, 0, &addr, &size);
	if (error == 0)
		error = sun60i_a733_display_read_clocks(sun60i_a733_display_ccu_read,
		    sc, size, &state);
	if (error != 0) {
		aprint_error_dev(sc->sc_dev, "display CCU observation unavailable: %d\n",
		    error);
		return;
	}
	aprint_normal_dev(sc->sc_dev, "display CCU observe-only: changed 0x%04x; "
	    "no display MMIO access authorized\n", state.changed);
	for (u_int r = 0; r < A733_DISPLAY_NCLOCK_REGS; r++)
		aprint_normal_dev(sc->sc_dev, "display %s [0x%04x]: 0x%08x 0x%08x\n",
		    sun60i_a733_display_clock_regs[r].name,
		    sun60i_a733_display_clock_regs[r].offset,
		    state.sample[0][r], state.sample[1][r]);
	error = sun60i_a733_display_decode_clocks(&state, &facts);
	if (error != 0) {
		aprint_normal_dev(sc->sc_dev, "display clock decode unavailable: %d\n",
		    error);
		return;
	}
	aprint_normal_dev(sc->sc_dev, "display mux selection: DE3X 0x%x "
	    "DE4X 0x%x; gates 0x%x unknown 0x%x update 0x%x dynamic 0x%x; "
	    "not ownership or readiness\n", facts.de3x_selected,
	    facts.de4x_selected, facts.gates_enabled, facts.unknown_selectors,
	    facts.update_pending, facts.dynamic_mode);
}

static int
sun60i_a733_ccu_match(device_t parent, cfdata_t cf, void *aux)
{
	struct fdt_attach_args * const faa = aux;

	return of_compatible_match(faa->faa_phandle, compat_data);
}

static void
sun60i_a733_ccu_attach(device_t parent, device_t self, void *aux)
{
	struct sunxi_ccu_softc * const sc = device_private(self);
	struct fdt_attach_args * const faa = aux;

	sc->sc_dev = self;
	sc->sc_phandle = faa->faa_phandle;
	sc->sc_bst = faa->faa_bst;

	sc->sc_resets = sun60i_a733_ccu_resets;
	sc->sc_nresets = __arraycount(sun60i_a733_ccu_resets);

	sc->sc_clks = sun60i_a733_ccu_clks;
	sc->sc_nclks = __arraycount(sun60i_a733_ccu_clks);

	mutex_init(&sun60i_a733_ccu_lock, MUTEX_DEFAULT, IPL_NONE);
	sc->sc_guard_enter = sun60i_a733_gpu_guard_enter;
	sc->sc_guard_exit = sun60i_a733_gpu_guard_exit;
	sc->sc_update_bits = sun60i_a733_accel_write;
	if (sunxi_ccu_attach(sc) != 0) {
		mutex_destroy(&sun60i_a733_ccu_lock);
		return;
	}

	sun60i_gpu_lease.sc = sc;

	aprint_naive("\n");
	aprint_normal(": A733 CCU\n");

	sunxi_ccu_print(sc);
	sun60i_a733_display_ccu_report(sc);
}
