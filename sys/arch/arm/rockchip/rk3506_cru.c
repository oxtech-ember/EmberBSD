/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2026 The EmberBSD Project.
 *
 * RK3506 clocks for the SD card, card-detect GPIO and USB host.
 */

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/device.h>

#include <dev/fdt/fdtvar.h>

#include <arm/rockchip/rk_cru.h>
#include <external/gpl2/dts/dist/include/dt-bindings/clock/rockchip,rk3506-cru.h>
#include <external/gpl2/dts/dist/include/dt-bindings/reset/rockchip,rk3506-cru.h>

#define	RK3506_CLKSEL(n)	(0x300 + 4 * (n))
#define	RK3506_CLKGATE(n)	(0x800 + 4 * (n))
#define	RK3506_SOFTRST_BASE	0xa00

static int rk3506_cru_match(device_t, cfdata_t, void *);
static void rk3506_cru_attach(device_t, device_t, void *);

CFATTACH_DECL_NEW(rk3506_cru, sizeof(struct rk_cru_softc),
    rk3506_cru_match, rk3506_cru_attach, NULL, NULL);

static const struct device_compatible_entry compat_data[] = {
	{ .compat = "rockchip,rk3506-cru" },
	DEVICE_COMPAT_EOL
};

/*
 * Keep the firmware's peripheral bus roots running. Only the crystal
 * path has a rate provider here. The MMC driver can set the CIU rate
 * from this source without retuning a system PLL.
 */
static const char *sdmmc_parents[] = {
	"xin24m_gate", "gpll", "clk_v0pll_gate", "clk_v1pll_gate"
};

static struct rk_cru_clk rk3506_clks[] = {
	RK_GATE(XIN24M_GATE, "xin24m_gate", "xin24m", RK3506_CLKGATE(0), 1),
	RK_GATE(CLK_REF_USBPHY_TOP, "clk_ref_usbphy_top", "xin24m",
	    RK3506_CLKGATE(1), 4),
	RK_GATE(PCLK_GPIO1, "pclk_gpio1", "aclk_core_root", RK3506_CLKGATE(3), 8),
	RK_GATE(DBCLK_GPIO1, "dbclk_gpio1", "xin24m_gate", RK3506_CLKGATE(3), 9),
	RK_GATE(HCLK_USBOTG1, "hclk_usbotg1", "hclk_bus_root",
	    RK3506_CLKGATE(7), 8),
	RK_GATE(PCLK_USBPHY, "pclk_usbphy", "pclk_bus_root",
	    RK3506_CLKGATE(7), 11),
	RK_GATE(HCLK_SDMMC, "hclk_sdmmc", "hclk_hsperi_root", RK3506_CLKGATE(17), 7),
	RK_COMPOSITE(CCLK_SRC_SDMMC, "cclk_src_sdmmc", sdmmc_parents,
	    RK3506_CLKSEL(49), __BITS(14,13), __BITS(12,7),
	    RK3506_CLKGATE(17), __BIT(6), RK_COMPOSITE_ROUND_DOWN),
};

static int
rk3506_cru_reset_index(u_int id)
{
	if (id == SRST_H_SDMMC)
		return 17 * 16 + 7;
	return -1;
}

static int
rk3506_cru_match(device_t parent, cfdata_t cf, void *aux)
{
	const struct fdt_attach_args * const faa = aux;
	const int match = of_compatible_match(faa->faa_phandle, compat_data);
	u_int index;

	if (match == 0)
		return 0;
	/* The vendor binding omits xin and has different clock/reset IDs. */
	if (fdtbus_get_index(faa->faa_phandle, "clock-names", "xin", &index) != 0 ||
	    index != 0 || !of_hasprop(faa->faa_phandle, "clocks"))
		return 0;
	return match;
}

static void
rk3506_cru_attach(device_t parent, device_t self, void *aux)
{
	struct rk_cru_softc * const sc = device_private(self);
	const struct fdt_attach_args * const faa = aux;

	sc->sc_dev = self;
	sc->sc_phandle = faa->faa_phandle;
	sc->sc_bst = faa->faa_bst;
	sc->sc_clks = rk3506_clks;
	sc->sc_nclks = __arraycount(rk3506_clks);
	sc->sc_softrst_base = RK3506_SOFTRST_BASE;
	sc->sc_reset_index = rk3506_cru_reset_index;

	if (rk_cru_attach(sc) != 0)
		return;

	aprint_naive("\n");
	aprint_normal(": RK3506 CRU\n");
}
