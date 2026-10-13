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
 * Clocks and resets of the Allwinner A733 always-on domain: the buses
 * of the devices that stay powered, among them the I2C controller the
 * power management chips sit on.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(1, "$NetBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/systm.h>

#include <dev/fdt/fdtvar.h>

#include <arm/sunxi/sunxi_ccu.h>
#include <arm/sunxi/sun60i_a733_r_ccu.h>

#define	R_AHB_CFG_REG		0x000
#define	R_APB0_CFG_REG		0x00c
#define	R_APB1_CFG_REG		0x010
#define	R_UART_BGR_REG		0x18c
#define	R_I2C_BGR_REG		0x19c
#define	R_PPU_BGR_REG		0x1ac
#define	R_RTC_BGR_REG		0x20c

static int sun60i_a733_r_ccu_match(device_t, cfdata_t, void *);
static void sun60i_a733_r_ccu_attach(device_t, device_t, void *);

static const struct device_compatible_entry compat_data[] = {
	{ .compat = "allwinner,sun60i-a733-r-ccu" },
	DEVICE_COMPAT_EOL
};

CFATTACH_DECL_NEW(sunxi_a733_r_ccu, sizeof(struct sunxi_ccu_softc),
	sun60i_a733_r_ccu_match, sun60i_a733_r_ccu_attach, NULL, NULL);

static struct sunxi_ccu_reset sun60i_a733_r_ccu_resets[] = {
	SUNXI_CCU_RESET(A733_R_RST_BUS_UART0, R_UART_BGR_REG, 16),
	SUNXI_CCU_RESET(A733_R_RST_BUS_UART1, R_UART_BGR_REG, 17),
	SUNXI_CCU_RESET(A733_R_RST_BUS_I2C0, R_I2C_BGR_REG, 16),
	SUNXI_CCU_RESET(A733_R_RST_BUS_I2C1, R_I2C_BGR_REG, 17),
	SUNXI_CCU_RESET(A733_R_RST_BUS_I2C2, R_I2C_BGR_REG, 18),
	SUNXI_CCU_RESET(A733_R_RST_BUS_RTC, R_RTC_BGR_REG, 16),
};

static const char *r_ahb_parents[] = {
	"hosc", "losc", "iosc", "pll-periph0-200M", "pll-periph0-300M"
};
static const char *r_apb_parents[] = {
	"hosc", "losc", "iosc", "pll-periph0-200M", "pll-ref"
};

static struct sunxi_ccu_clk sun60i_a733_r_ccu_clks[] = {
	SUNXI_CCU_DIV(A733_R_CLK_AHB, "r-ahb", r_ahb_parents,
	    R_AHB_CFG_REG,
	    __BITS(4,0),		/* div */
	    __BITS(26,24),		/* sel */
	    0),
	SUNXI_CCU_DIV(A733_R_CLK_APB0, "r-apb0", r_apb_parents,
	    R_APB0_CFG_REG,
	    __BITS(4,0),		/* div */
	    __BITS(26,24),		/* sel */
	    0),
	SUNXI_CCU_DIV(A733_R_CLK_APB1, "r-apb1", r_apb_parents,
	    R_APB1_CFG_REG,
	    __BITS(4,0),		/* div */
	    __BITS(26,24),		/* sel */
	    0),

	SUNXI_CCU_GATE(A733_R_CLK_BUS_UART0, "bus-r-uart0", "r-apb1",
	    R_UART_BGR_REG, 0),
	SUNXI_CCU_GATE(A733_R_CLK_BUS_UART1, "bus-r-uart1", "r-apb1",
	    R_UART_BGR_REG, 1),
	SUNXI_CCU_GATE(A733_R_CLK_BUS_I2C0, "bus-r-i2c0", "r-apb1",
	    R_I2C_BGR_REG, 0),
	SUNXI_CCU_GATE(A733_R_CLK_BUS_I2C1, "bus-r-i2c1", "r-apb1",
	    R_I2C_BGR_REG, 1),
	SUNXI_CCU_GATE(A733_R_CLK_BUS_I2C2, "bus-r-i2c2", "r-apb1",
	    R_I2C_BGR_REG, 2),
	/* A733 has a PPU gate, but no PPU reset in this register. */
	SUNXI_CCU_GATE(A733_R_CLK_BUS_PPU, "bus-r-ppu", "hosc",
	    R_PPU_BGR_REG, 0),
	SUNXI_CCU_GATE(A733_R_CLK_BUS_RTC, "bus-r-rtc", "r-ahb",
	    R_RTC_BGR_REG, 0),
};

static int
sun60i_a733_r_ccu_match(device_t parent, cfdata_t cf, void *aux)
{
	struct fdt_attach_args * const faa = aux;

	return of_compatible_match(faa->faa_phandle, compat_data);
}

static void
sun60i_a733_r_ccu_attach(device_t parent, device_t self, void *aux)
{
	struct sunxi_ccu_softc * const sc = device_private(self);
	struct fdt_attach_args * const faa = aux;

	sc->sc_dev = self;
	sc->sc_phandle = faa->faa_phandle;
	sc->sc_bst = faa->faa_bst;

	sc->sc_resets = sun60i_a733_r_ccu_resets;
	sc->sc_nresets = __arraycount(sun60i_a733_r_ccu_resets);

	sc->sc_clks = sun60i_a733_r_ccu_clks;
	sc->sc_nclks = __arraycount(sun60i_a733_r_ccu_clks);

	if (sunxi_ccu_attach(sc) != 0)
		return;

	aprint_naive("\n");
	aprint_normal(": A733 always-on domain CCU\n");

	sunxi_ccu_print(sc);
}
