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
 * Clock controller of the Allwinner A100 and A133.
 *
 * The register layout follows the H6. The peripheral PLLs differ: their
 * register gives the doubled rate and the plain one is half of it. The
 * layout was checked against the registers of a running A133 board.
 */

#include <sys/cdefs.h>

__KERNEL_RCSID(1, "$NetBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/kmem.h>
#include <sys/systm.h>

#include <dev/fdt/fdtvar.h>

#include <arm/sunxi/sunxi_ccu.h>
#include <arm/sunxi/sun50i_a100_ccu.h>
#include <arm/sunxi/sun50i_a100_ccu_gpu.h>

#define	PLL_PERI0_CTRL_REG	0x020
#define	PLL_PERI1_CTRL_REG	0x028
#define	PSI_AHB1_AHB2_CFG_REG	0x510
#define	AHB3_CFG_REG		0x51c
#define	APB1_CFG_REG		0x520
#define	APB2_CFG_REG		0x524
#define	MBUS_CFG_REG		0x540
#define	DMA_BGR_REG		0x70c
#define	HSTIMER_BGR_REG		0x73c
#define	PWM_BGR_REG		0x7ac
#define	SMHC0_CLK_REG		0x830
#define	SMHC1_CLK_REG		0x834
#define	SMHC2_CLK_REG		0x838
#define	SMHC_BGR_REG		0x84c
#define	UART_BGR_REG		0x90c
#define	TWI_BGR_REG		0x91c
#define	SPI_BGR_REG		0x96c
#define	EMAC_25M_CLK_REG	0x970
#define	EMAC_BGR_REG		0x97c
#define	THS_BGR_REG		0x9fc
#define	USB0_CLK_REG		0xa70
#define	USB1_CLK_REG		0xa74
#define	USB_BGR_REG		0xa8c
#define	GPU_PLL_CTRL_REG	0x030
#define	GPU_CLK_REG		0x670
#define	GPU_BGR_REG		0x67c

static int sun50i_a100_ccu_match(device_t, cfdata_t, void *);
static void sun50i_a100_ccu_attach(device_t, device_t, void *);

static const struct device_compatible_entry compat_data[] = {
	{ .compat = "allwinner,sun50i-a133-ccu" },
	{ .compat = "allwinner,sun50i-a100-ccu" },
	DEVICE_COMPAT_EOL
};

static const struct device_compatible_entry a133_compat_data[] = {
	{ .compat = "allwinner,sun50i-a133-ccu" },
	DEVICE_COMPAT_EOL
};

CFATTACH_DECL_NEW(sunxi_a100_ccu, sizeof(struct sunxi_ccu_softc),
	sun50i_a100_ccu_match, sun50i_a100_ccu_attach, NULL, NULL);

static struct sunxi_ccu_reset sun50i_a100_ccu_resets[] = {
	SUNXI_CCU_RESET(A100_RST_MBUS, MBUS_CFG_REG, 30),
	SUNXI_CCU_RESET(A100_RST_BUS_GPU, GPU_BGR_REG, 16),
	SUNXI_CCU_RESET(A100_RST_BUS_DMA, DMA_BGR_REG, 16),
	SUNXI_CCU_RESET(A100_RST_BUS_HSTIMER, HSTIMER_BGR_REG, 16),
	SUNXI_CCU_RESET(A100_RST_BUS_PWM, PWM_BGR_REG, 16),

	SUNXI_CCU_RESET(A100_RST_BUS_MMC0, SMHC_BGR_REG, 16),
	SUNXI_CCU_RESET(A100_RST_BUS_MMC1, SMHC_BGR_REG, 17),
	SUNXI_CCU_RESET(A100_RST_BUS_MMC2, SMHC_BGR_REG, 18),

	SUNXI_CCU_RESET(A100_RST_BUS_UART0, UART_BGR_REG, 16),
	SUNXI_CCU_RESET(A100_RST_BUS_UART1, UART_BGR_REG, 17),
	SUNXI_CCU_RESET(A100_RST_BUS_UART2, UART_BGR_REG, 18),
	SUNXI_CCU_RESET(A100_RST_BUS_UART3, UART_BGR_REG, 19),
	SUNXI_CCU_RESET(A100_RST_BUS_UART4, UART_BGR_REG, 20),

	SUNXI_CCU_RESET(A100_RST_BUS_I2C0, TWI_BGR_REG, 16),
	SUNXI_CCU_RESET(A100_RST_BUS_I2C1, TWI_BGR_REG, 17),
	SUNXI_CCU_RESET(A100_RST_BUS_I2C2, TWI_BGR_REG, 18),
	SUNXI_CCU_RESET(A100_RST_BUS_I2C3, TWI_BGR_REG, 19),

	SUNXI_CCU_RESET(A100_RST_BUS_SPI0, SPI_BGR_REG, 16),
	SUNXI_CCU_RESET(A100_RST_BUS_SPI1, SPI_BGR_REG, 17),
	SUNXI_CCU_RESET(A100_RST_BUS_SPI2, SPI_BGR_REG, 18),

	SUNXI_CCU_RESET(A100_RST_BUS_EMAC, EMAC_BGR_REG, 16),
	SUNXI_CCU_RESET(A100_RST_BUS_THS, THS_BGR_REG, 16),

	SUNXI_CCU_RESET(A100_RST_USB_PHY0, USB0_CLK_REG, 30),
	SUNXI_CCU_RESET(A100_RST_USB_PHY1, USB1_CLK_REG, 30),
	SUNXI_CCU_RESET(A100_RST_BUS_OHCI0, USB_BGR_REG, 16),
	SUNXI_CCU_RESET(A100_RST_BUS_OHCI1, USB_BGR_REG, 17),
	SUNXI_CCU_RESET(A100_RST_BUS_EHCI0, USB_BGR_REG, 20),
	SUNXI_CCU_RESET(A100_RST_BUS_EHCI1, USB_BGR_REG, 21),
	SUNXI_CCU_RESET(A100_RST_BUS_OTG, USB_BGR_REG, 24),
};

static const char *bus_parents[] = { "hosc", "losc", "psi", "pll_periph0" };
static const char *psi_parents[] = { "hosc", "losc", "iosc", "pll_periph0" };
static const char *mod_parents[] = { "hosc", "pll_periph0_2x", "pll_periph1_2x" };
static const char *gpu_parents[] = { "pll_gpu" };

#define	A100_PLL_PERIPH(_id, _name, _reg)				\
	SUNXI_CCU_NKMP(_id, _name, "hosc",				\
	    _reg,			/* reg */			\
	    __BITS(15,8),		/* n */				\
	    0,				/* k */				\
	    __BIT(1),			/* m */				\
	    __BIT(0),			/* p */				\
	    __BIT(31),			/* enable */			\
	    0)

#define	A100_BUS(_id, _name, _parents, _reg)				\
	SUNXI_CCU_NM(_id, _name, _parents,				\
	    _reg,			/* reg */			\
	    __BITS(9,8),		/* n */				\
	    __BITS(1,0),		/* m */				\
	    __BITS(25,24),		/* sel */			\
	    0,				/* enable */			\
	    SUNXI_CCU_NM_POWER_OF_TWO)

#define	A100_MMC(_id, _name, _reg)					\
	SUNXI_CCU_NM(_id, _name, mod_parents,				\
	    _reg,			/* reg */			\
	    __BITS(9,8),		/* n */				\
	    __BITS(3,0),		/* m */				\
	    __BITS(25,24),		/* sel */			\
	    __BIT(31),			/* enable */			\
	    SUNXI_CCU_NM_POWER_OF_TWO|SUNXI_CCU_NM_ROUND_DOWN)

#define	A100_PLL_GPU(_id, _name)					\
	SUNXI_CCU_NKMP(_id, _name, "hosc",				\
	    GPU_PLL_CTRL_REG,		/* reg */			\
	    __BITS(15,8),		/* n */				\
	    0,				/* k */				\
	    __BIT(1),			/* m */				\
	    __BIT(0),			/* p */				\
	    0,				/* firmware-owned enable */	\
	    0)

#define	A100_GPU_CORE(_m)					\
	SUNXI_CCU_DIV_GATE(A100_CLK_GPU, "gpu", gpu_parents,	\
	    GPU_CLK_REG,			/* reg */			\
	    (_m),			/* divider */			\
	    __BIT(24),			/* sel */			\
	    __BIT(31),			/* gate */			\
	    0)

static struct sunxi_ccu_clk sun50i_a100_ccu_clks[] = {
	SUNXI_CCU_FIXED_FACTOR(A100_CLK_OSC12M, "osc12m", "hosc", 2, 1),
	A100_PLL_GPU(A100_CLK_PLL_GPU, "pll_gpu"),

	A100_PLL_PERIPH(A100_CLK_PLL_PERIPH0_2X, "pll_periph0_2x",
	    PLL_PERI0_CTRL_REG),
	SUNXI_CCU_FIXED_FACTOR(A100_CLK_PLL_PERIPH0, "pll_periph0",
	    "pll_periph0_2x", 2, 1),
	A100_PLL_PERIPH(A100_CLK_PLL_PERIPH1_2X, "pll_periph1_2x",
	    PLL_PERI1_CTRL_REG),
	SUNXI_CCU_FIXED_FACTOR(A100_CLK_PLL_PERIPH1, "pll_periph1",
	    "pll_periph1_2x", 2, 1),

	A100_BUS(A100_CLK_PSI_AHB1_AHB2, "psi", psi_parents,
	    PSI_AHB1_AHB2_CFG_REG),
	A100_BUS(A100_CLK_AHB3, "ahb3", bus_parents, AHB3_CFG_REG),
	A100_BUS(A100_CLK_APB1, "apb1", bus_parents, APB1_CFG_REG),
	A100_BUS(A100_CLK_APB2, "apb2", bus_parents, APB2_CFG_REG),
	A100_GPU_CORE(__BITS(1,0)),
	SUNXI_CCU_GATE(A100_CLK_BUS_GPU, "bus-gpu", "psi", GPU_BGR_REG, 0),

	SUNXI_CCU_GATE(A100_CLK_BUS_DMA, "bus-dma", "psi", DMA_BGR_REG, 0),
	SUNXI_CCU_GATE(A100_CLK_BUS_HSTIMER, "bus-hstimer", "psi",
	    HSTIMER_BGR_REG, 0),
	SUNXI_CCU_GATE(A100_CLK_BUS_PWM, "bus-pwm", "apb1", PWM_BGR_REG, 0),

	A100_MMC(A100_CLK_MMC0, "mmc0", SMHC0_CLK_REG),
	A100_MMC(A100_CLK_MMC1, "mmc1", SMHC1_CLK_REG),
	A100_MMC(A100_CLK_MMC2, "mmc2", SMHC2_CLK_REG),
	SUNXI_CCU_GATE(A100_CLK_BUS_MMC0, "bus-mmc0", "ahb3", SMHC_BGR_REG, 0),
	SUNXI_CCU_GATE(A100_CLK_BUS_MMC1, "bus-mmc1", "ahb3", SMHC_BGR_REG, 1),
	SUNXI_CCU_GATE(A100_CLK_BUS_MMC2, "bus-mmc2", "ahb3", SMHC_BGR_REG, 2),

	SUNXI_CCU_GATE(A100_CLK_BUS_UART0, "bus-uart0", "apb2", UART_BGR_REG, 0),
	SUNXI_CCU_GATE(A100_CLK_BUS_UART1, "bus-uart1", "apb2", UART_BGR_REG, 1),
	SUNXI_CCU_GATE(A100_CLK_BUS_UART2, "bus-uart2", "apb2", UART_BGR_REG, 2),
	SUNXI_CCU_GATE(A100_CLK_BUS_UART3, "bus-uart3", "apb2", UART_BGR_REG, 3),
	SUNXI_CCU_GATE(A100_CLK_BUS_UART4, "bus-uart4", "apb2", UART_BGR_REG, 4),

	SUNXI_CCU_GATE(A100_CLK_BUS_I2C0, "bus-i2c0", "apb2", TWI_BGR_REG, 0),
	SUNXI_CCU_GATE(A100_CLK_BUS_I2C1, "bus-i2c1", "apb2", TWI_BGR_REG, 1),
	SUNXI_CCU_GATE(A100_CLK_BUS_I2C2, "bus-i2c2", "apb2", TWI_BGR_REG, 2),
	SUNXI_CCU_GATE(A100_CLK_BUS_I2C3, "bus-i2c3", "apb2", TWI_BGR_REG, 3),

	SUNXI_CCU_GATE(A100_CLK_BUS_SPI0, "bus-spi0", "ahb3", SPI_BGR_REG, 0),
	SUNXI_CCU_GATE(A100_CLK_BUS_SPI1, "bus-spi1", "ahb3", SPI_BGR_REG, 1),
	SUNXI_CCU_GATE(A100_CLK_BUS_SPI2, "bus-spi2", "ahb3", SPI_BGR_REG, 2),

	SUNXI_CCU_FIXED_FACTOR(A100_CLK_EMAC_25M, "emac-25m", "pll_periph0",
	    24, 1),
	SUNXI_CCU_GATE(A100_CLK_BUS_EMAC, "bus-emac", "ahb3", EMAC_BGR_REG, 0),

	SUNXI_CCU_GATE(A100_CLK_BUS_THS, "bus-ths", "apb1", THS_BGR_REG, 0),

	SUNXI_CCU_GATE(A100_CLK_USB_OHCI0, "usb-ohci0", "osc12m",
	    USB0_CLK_REG, 31),
	SUNXI_CCU_GATE(A100_CLK_USB_PHY0, "usb-phy0", "hosc",
	    USB0_CLK_REG, 29),
	SUNXI_CCU_GATE(A100_CLK_USB_OHCI1, "usb-ohci1", "osc12m",
	    USB1_CLK_REG, 31),
	SUNXI_CCU_GATE(A100_CLK_USB_PHY1, "usb-phy1", "hosc",
	    USB1_CLK_REG, 29),
	SUNXI_CCU_GATE(A100_CLK_BUS_OHCI0, "bus-ohci0", "ahb3", USB_BGR_REG, 0),
	SUNXI_CCU_GATE(A100_CLK_BUS_OHCI1, "bus-ohci1", "ahb3", USB_BGR_REG, 1),
	SUNXI_CCU_GATE(A100_CLK_BUS_EHCI0, "bus-ehci0", "ahb3", USB_BGR_REG, 4),
	SUNXI_CCU_GATE(A100_CLK_BUS_EHCI1, "bus-ehci1", "ahb3", USB_BGR_REG, 5),
	SUNXI_CCU_GATE(A100_CLK_BUS_OTG, "bus-otg", "ahb3", USB_BGR_REG, 8),
};

static int
sun50i_a100_ccu_match(device_t parent, cfdata_t cf, void *aux)
{
	struct fdt_attach_args * const faa = aux;

	return of_compatible_match(faa->faa_phandle, compat_data);
}

static void
sun50i_a100_ccu_attach(device_t parent, device_t self, void *aux)
{
	struct sunxi_ccu_softc * const sc = device_private(self);
	struct fdt_attach_args * const faa = aux;
	struct sunxi_ccu_clk *clks = NULL;

	sc->sc_dev = self;
	sc->sc_phandle = faa->faa_phandle;
	sc->sc_bst = faa->faa_bst;

	sc->sc_resets = sun50i_a100_ccu_resets;
	sc->sc_nresets = __arraycount(sun50i_a100_ccu_resets);

	if (of_compatible_match(faa->faa_phandle, a133_compat_data)) {
		clks = kmem_alloc(sizeof(sun50i_a100_ccu_clks), KM_NOSLEEP);
		if (clks == NULL) {
			aprint_error_dev(self,
			    "couldn't allocate A133 CCU clock descriptors\n");
			return;
		}
		memcpy(clks, sun50i_a100_ccu_clks,
		    sizeof(sun50i_a100_ccu_clks));
		clks[A100_CLK_GPU].u.div.div = __BITS(2,0);
	}
	sc->sc_clks = clks != NULL ? clks : sun50i_a100_ccu_clks;
	sc->sc_nclks = __arraycount(sun50i_a100_ccu_clks);

	sc->sc_clks[A100_CLK_PLL_GPU].enable = sun50i_a100_ccu_gpu_pll_enable;
	sc->sc_clks[A100_CLK_PLL_GPU].get_rate = sun50i_a100_ccu_gpu_pll_get_rate;
	sc->sc_clks[A100_CLK_PLL_GPU].set_rate = sun50i_a100_ccu_gpu_pll_set_rate;
	sc->sc_clks[A100_CLK_PLL_GPU].get_parent =
	    sun50i_a100_ccu_gpu_pll_get_parent;
	sc->sc_clks[A100_CLK_GPU].enable = sun50i_a133_ccu_gpu_core_enable;
	sc->sc_clks[A100_CLK_GPU].get_rate = sun50i_a133_ccu_gpu_core_get_rate;
	sc->sc_clks[A100_CLK_GPU].set_rate = sun50i_a133_ccu_gpu_core_set_rate;
	sc->sc_clks[A100_CLK_GPU].set_parent =
	    sun50i_a133_ccu_gpu_core_set_parent;
	sc->sc_clks[A100_CLK_GPU].get_parent =
	    sun50i_a133_ccu_gpu_core_get_parent;

	if (sunxi_ccu_attach(sc) != 0) {
		if (clks != NULL)
			kmem_free(clks, sizeof(sun50i_a100_ccu_clks));
		return;
	}

	aprint_naive("\n");
	aprint_normal(": A100 CCU\n");

	sunxi_ccu_print(sc);
}
