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
#define	CE_CLK_REG		0xac0
#define	CE_BGR_REG		0xac4
#define	NPU_CLK_REG		0xb00
#define	NPU_BGR_REG		0xb04
#define	GPU0_CLK_REG		0xb20
#define	GPU0_BGR_REG		0xb24
#define	ACCEL_CLK_ENABLE	__BIT(31)
#define	ACCEL_CLK_SEL		__BITS(26,24)
#define	GPU_CLK_UPDATE		__BIT(27)
#define	GPU_UPDATE_POLL_US	10
#define	GPU_UPDATE_TIMEOUT_US	10000
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
sun60i_a733_accel_write(struct sunxi_ccu_softc *sc, bus_size_t reg,
    uint32_t mask, uint32_t bits)
{
	uint32_t old, val;

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

static struct sunxi_ccu_clk sun60i_a733_ccu_clks[] = {
	/*
	 * PLL_REF turns a 19.2, 24 or 26 MHz crystal into 24 MHz. Boards
	 * with a 24 MHz crystal run it one to one, which is all that is
	 * described here.
	 */
	SUNXI_CCU_FIXED_FACTOR(A733_CLK_PLL_REF, "pll-ref", "hosc", 1, 1),
	SUNXI_CCU_FIXED_FACTOR(A733_CLK_SYS_24M, "sys-24M", "pll-ref", 1, 1),
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
	kmutex_t lock;
	struct sunxi_ccu_softc *sc;
	const void *owner;
	u_int writers;
	bool attempted;
	struct sun60i_a733_gpu_state before;
} sun60i_gpu_lease;

static int
sun60i_a733_gpu_guard_enter(struct sunxi_ccu_softc *sc, struct clk *clk,
    struct sunxi_ccu_reset *reset, enum sunxi_ccu_mutation op, bool *handled)
{
	bool shared = false, local = false;
	int error = 0;

	mutex_enter(&sun60i_gpu_lease.lock);
	if (sun60i_gpu_lease.owner != NULL) {
		if (reset != NULL)
			local = reset->reg == GPU0_BGR_REG;
		for (u_int id = 0; clk != NULL && id < sc->sc_nclks; id++) {
			if (clk != &sc->sc_clks[id].base)
				continue;
			shared = id == A733_CLK_PLL_REF || id == A733_CLK_SYS_24M ||
			    (id >= A733_CLK_PLL_PERIPH0_4X &&
			    id <= A733_CLK_PLL_PERIPH0_150M) || id == A733_CLK_AHB;
			local = id == A733_CLK_GPU0 || id == A733_CLK_BUS_GPU0 ||
			    id == A733_CLK_AHB_GPU0;
			break;
		}
	}
	if (shared || local) {
		*handled = true;
		/* Running ancestors are borrowed, never recursively re-enabled. */
		if (!shared || op != SUNXI_CCU_ENABLE)
			error = EBUSY;
	} else {
		sun60i_gpu_lease.writers++;
	}
	mutex_exit(&sun60i_gpu_lease.lock);
	return error;
}

static void
sun60i_a733_gpu_guard_exit(struct sunxi_ccu_softc *sc)
{
	mutex_enter(&sun60i_gpu_lease.lock);
	KASSERT(sun60i_gpu_lease.writers != 0);
	sun60i_gpu_lease.writers--;
	mutex_exit(&sun60i_gpu_lease.lock);
}

/* Only compare actual observations; the trial decode below is not readiness. */
static bool
sun60i_a733_gpu_same(const struct sun60i_a733_gpu_state *a,
    const struct sun60i_a733_gpu_state *b)
{
	/* Autonomous diagnostic gate status is not part of the old policy. */
	for (u_int s = 0; s < 2; s++) {
		for (u_int i = A733_GPU_REF; i <= A733_GPU_MASTER; i++) {
			if (a->sample[s][i] != b->sample[s][i])
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
	mutex_enter(&sun60i_gpu_lease.lock);
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
	mutex_exit(&sun60i_gpu_lease.lock);
	return error;
}

int
sun60i_a733_ccu_gpu_release(struct clk *gpu, const void *owner)
{
	int error = 0;

	if (gpu != &sun60i_a733_ccu_clks[A733_CLK_GPU0].base || owner == NULL ||
	    sun60i_gpu_lease.sc == NULL)
		return EINVAL;
	mutex_enter(&sun60i_gpu_lease.lock);
	if (sun60i_gpu_lease.owner != owner)
		error = EINVAL;
	else if (sun60i_gpu_lease.attempted)
		error = EBUSY;
	else
		sun60i_gpu_lease.owner = NULL;
	mutex_exit(&sun60i_gpu_lease.lock);
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
	mutex_enter(&sun60i_gpu_lease.lock);
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
	error = sun60i_a733_accel_write(sc, GPU0_CLK_REG,
	    ACCEL_CLK_SEL | __BITS(3,0), __SHIFTIN(3, ACCEL_CLK_SEL));
	if (error != 0)
		goto out;
	/* BSP local order: release bus reset, bus gate, then module gate. */
	error = sun60i_a733_accel_write(sc, GPU0_BGR_REG, __BIT(16), __BIT(16));
	if (error != 0)
		goto out;
	error = sun60i_a733_accel_write(sc, GPU0_BGR_REG, __BIT(0), __BIT(0));
	if (error != 0)
		goto out;
	error = sun60i_a733_accel_write(sc, GPU0_CLK_REG,
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
	mutex_exit(&sun60i_gpu_lease.lock);
	return error;
}

/*
 * Select a running parent for the NPU module clock before any enable:
 * after reset the mux points at pll-npu, which firmware left off and the
 * accel PLL protection refuses to turn on. PERIPH0-600M runs at boot.
 */
int
sun60i_a733_ccu_npu_prepare(struct clk *npu, const void *owner, bool *retained)
{
	struct sunxi_ccu_softc *sc = sun60i_gpu_lease.sc;

	if (npu != &sun60i_a733_ccu_clks[A733_CLK_NPU].base || owner == NULL ||
	    retained == NULL || sc == NULL)
		return EINVAL;
	mutex_enter(&sun60i_gpu_lease.lock);
	if (sun60i_gpu_lease.owner != NULL && sun60i_gpu_lease.owner != owner) {
		mutex_exit(&sun60i_gpu_lease.lock);
		return EBUSY;
	}
	sun60i_gpu_lease.attempted = true;
	*retained = true;
	/* npu_parents[2] = pll-periph0-600M; GPU uses index 3 = 400M. */
	int error = sun60i_a733_accel_write(sc, NPU_CLK_REG,
	    ACCEL_CLK_SEL | __BITS(3,0), __SHIFTIN(2, ACCEL_CLK_SEL));
	mutex_exit(&sun60i_gpu_lease.lock);
	return error;
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

	if (sunxi_ccu_attach(sc) != 0)
		return;

	sun60i_gpu_lease.sc = sc;
	mutex_init(&sun60i_gpu_lease.lock, MUTEX_DEFAULT, IPL_NONE);
	sc->sc_guard_enter = sun60i_a733_gpu_guard_enter;
	sc->sc_guard_exit = sun60i_a733_gpu_guard_exit;

	aprint_naive("\n");
	aprint_normal(": A733 CCU\n");

	sunxi_ccu_print(sc);
}
