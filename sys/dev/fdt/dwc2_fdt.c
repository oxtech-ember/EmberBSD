/*	$NetBSD: dwc2_fdt.c,v 1.13 2023/07/31 04:59:47 rin Exp $	*/

/*-
 * Copyright (c) 2013 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Nick Hudson
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: dwc2_fdt.c,v 1.13 2023/07/31 04:59:47 rin Exp $");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/mutex.h>
#include <sys/bus.h>
#include <sys/workqueue.h>

#include <dev/fdt/fdtvar.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdivar.h>
#include <dev/usb/usb_mem.h>

#include <dwc2/dwc2var.h>

#include <dwc2/dwc2.h>
#include "dwc2_core.h"

struct dwc2_fdt_softc {
	struct dwc2_softc	sc_dwc2;

	struct dwc2_core_params	sc_params;

	void			*sc_ih;
	int			sc_phandle;
	struct clk		*sc_clk;
	struct fdtbus_phy	*sc_phy;
	bus_size_t		sc_size;
};

static int dwc2_fdt_match(device_t, struct cfdata *, void *);
static void dwc2_fdt_attach(device_t, device_t, void *);
static void dwc2_fdt_deferred(device_t);
static void dwc2_fdt_release(struct dwc2_fdt_softc *);
static int dwc2_fdt_intr(void *);

static void dwc2_fdt_amlogic_params(struct dwc2_fdt_softc *, struct dwc2_core_params *);
static void dwc2_fdt_rockchip_params(struct dwc2_fdt_softc *, struct dwc2_core_params *);

struct dwc2_fdt_config {
	void	(*params)(struct dwc2_fdt_softc *, struct dwc2_core_params *);
};

static const struct dwc2_fdt_config dwc2_fdt_rk3066_config = {
	.params = dwc2_fdt_rockchip_params,
};

static const struct dwc2_fdt_config dwc2_fdt_meson8b_config = {
	.params = dwc2_fdt_amlogic_params,
};

static const struct dwc2_fdt_config dwc2_fdt_generic_config = {
};

static const struct device_compatible_entry compat_data[] = {
	{ .compat = "amlogic,meson8b-usb",
	  .data = &dwc2_fdt_meson8b_config },
	{ .compat = "amlogic,meson-gxbb-usb",
	  .data = &dwc2_fdt_meson8b_config },
	{ .compat = "rockchip,rk3066-usb",
	  .data = &dwc2_fdt_rk3066_config },
	{ .compat = "snps,dwc2",
	  .data = &dwc2_fdt_generic_config },

	DEVICE_COMPAT_EOL
};

CFATTACH_DECL_NEW(dwc2_fdt, sizeof(struct dwc2_fdt_softc),
    dwc2_fdt_match, dwc2_fdt_attach, NULL, NULL);

/* ARGSUSED */
static int
dwc2_fdt_match(device_t parent, struct cfdata *match, void *aux)
{
	struct fdt_attach_args * const faa = aux;

	return of_compatible_match(faa->faa_phandle, compat_data);
}

/* ARGSUSED */
static void
dwc2_fdt_attach(device_t parent, device_t self, void *aux)
{
	struct dwc2_fdt_softc *sc = device_private(self);
	struct fdt_attach_args * const faa = aux;
	const int phandle = faa->faa_phandle;
	const struct dwc2_fdt_config *conf =
	    of_compatible_lookup(phandle, compat_data)->data;
	char intrstr[128];
	struct fdtbus_phy *phy;
	bus_addr_t addr;
	bus_size_t size;
	uint32_t ahbcfg;
	u_int index;
	int error;

	const char *dr_mode = fdtbus_get_string(phandle, "dr_mode");
	if (dr_mode == NULL || strcmp(dr_mode, "host") != 0) {
		aprint_error(": mode '%s' not supported\n", dr_mode);
		return;
	}

	if (fdtbus_get_reg(phandle, 0, &addr, &size) != 0 ||
	    size < GAHBCFG + sizeof(ahbcfg)) {
		aprint_error(": couldn't get registers\n");
		return;
	}

	sc->sc_clk = fdtbus_clock_get(phandle, "otg");
	if (sc->sc_clk == NULL || clk_enable(sc->sc_clk) != 0) {
		aprint_error(": couldn't enable otg clock\n");
		if (sc->sc_clk)
			clk_put(sc->sc_clk);
		return;
	}

	if (fdtbus_get_index(phandle, "phy-names", "usb2-phy", &index) == 0) {
		phy = fdtbus_phy_get_index(phandle, index);
		if (phy == NULL) {
			aprint_error(": couldn't acquire usb2-phy\n");
			goto fail;
		}
		if (fdtbus_phy_enable(phy, true) != 0) {
			aprint_error(": couldn't enable phy\n");
			fdtbus_phy_put(phy);
			goto fail;
		}
		sc->sc_phy = phy;
	}

	sc->sc_phandle = phandle;
	sc->sc_dwc2.sc_dev = self;
	sc->sc_dwc2.sc_iot = faa->faa_bst;
	sc->sc_dwc2.sc_bus.ub_dmatag = faa->faa_dmat;

	error = bus_space_map(faa->faa_bst, addr, size, 0, &sc->sc_dwc2.sc_ioh);
	if (error) {
		aprint_error(": couldn't map device\n");
		goto fail;
	}
	sc->sc_size = size;

	if (conf->params) {
		conf->params(sc, &sc->sc_params);
		sc->sc_dwc2.sc_params = &sc->sc_params;
	}

	if (!fdtbus_intr_str(phandle, 0, intrstr, sizeof(intrstr))) {
		aprint_error(": failed to decode interrupt\n");
		goto fail;
	}

	aprint_naive("\n");
	aprint_normal(": DesignWare USB2 OTG\n");

	/* Mask firmware interrupts until dwc2_hcd_init sets up the host. */
	ahbcfg = bus_space_read_4(faa->faa_bst, sc->sc_dwc2.sc_ioh, GAHBCFG);
	bus_space_write_4(faa->faa_bst, sc->sc_dwc2.sc_ioh, GAHBCFG,
	    ahbcfg & ~GAHBCFG_GLBL_INTR_EN);
	bus_space_barrier(faa->faa_bst, sc->sc_dwc2.sc_ioh, GAHBCFG,
	    sizeof(ahbcfg), BUS_SPACE_BARRIER_WRITE);
	sc->sc_ih = fdtbus_intr_establish_xname(phandle, 0, IPL_VM,
	    FDT_INTR_MPSAFE, dwc2_fdt_intr, sc, device_xname(self));
	if (sc->sc_ih == NULL) {
		aprint_error_dev(self, "failed to establish interrupt %s\n",
		    intrstr);
		goto fail;
	}
	aprint_normal_dev(self, "interrupting on %s\n", intrstr);
	config_interrupts(self, dwc2_fdt_deferred);

	return;

fail:
	dwc2_fdt_release(sc);
}

static int
dwc2_fdt_intr(void *arg)
{
	struct dwc2_fdt_softc * const sc = arg;
	struct dwc2_softc * const dwc2 = &sc->sc_dwc2;

	/* A shared or pending IRQ can arrive before deferred setup. */
	if ((bus_space_read_4(dwc2->sc_iot, dwc2->sc_ioh, GAHBCFG) &
	    GAHBCFG_GLBL_INTR_EN) == 0)
		return 0;
	return dwc2_intr(dwc2);
}

static void
dwc2_fdt_release(struct dwc2_fdt_softc *sc)
{
	if (sc->sc_ih) {
		fdtbus_intr_disestablish(sc->sc_phandle, sc->sc_ih);
		sc->sc_ih = NULL;
	}
	if (sc->sc_size)
		bus_space_unmap(sc->sc_dwc2.sc_iot, sc->sc_dwc2.sc_ioh,
		    sc->sc_size);
	if (sc->sc_phy) {
		fdtbus_phy_enable(sc->sc_phy, false);
		fdtbus_phy_put(sc->sc_phy);
	}
	clk_disable(sc->sc_clk);
	clk_put(sc->sc_clk);
}

static void
dwc2_fdt_deferred(device_t self)
{
	struct dwc2_fdt_softc *sc = device_private(self);
	int error;

	error = dwc2_init(&sc->sc_dwc2);
	if (error != 0) {
		aprint_error_dev(self, "couldn't initialize host, error=%d\n",
		    error);
		dwc2_fdt_release(sc);
		return;
	}
	sc->sc_dwc2.sc_child = config_found(sc->sc_dwc2.sc_dev,
	    &sc->sc_dwc2.sc_bus, usbctlprint, CFARGS_NONE);
}

static void
dwc2_fdt_amlogic_params(struct dwc2_fdt_softc *sc, struct dwc2_core_params *params)
{
	dwc2_set_all_params(params, -1);

	params->otg_cap = DWC2_CAP_PARAM_NO_HNP_SRP_CAPABLE;
	params->speed = DWC2_SPEED_PARAM_HIGH;
	params->dma_enable = 1;
	params->enable_dynamic_fifo = 1;
	params->host_rx_fifo_size = 512;
	params->host_nperio_tx_fifo_size = 500;
	params->host_perio_tx_fifo_size = 500;
	params->host_channels = 16;
	params->phy_type = DWC2_PHY_TYPE_PARAM_UTMI;
	params->reload_ctl = 1;
	params->ahbcfg = GAHBCFG_HBSTLEN_INCR8 << GAHBCFG_HBSTLEN_SHIFT;
#ifdef DWC2_POWER_DOWN_PARAM_NONE
	params->power_down = DWC2_POWER_DOWN_PARAM_NONE;
#endif
}

static void
dwc2_fdt_rockchip_params(struct dwc2_fdt_softc *sc, struct dwc2_core_params *params)
{
	dwc2_set_all_params(params, -1);

	params->otg_cap = DWC2_CAP_PARAM_NO_HNP_SRP_CAPABLE;
	params->host_rx_fifo_size = 525;
	params->host_nperio_tx_fifo_size = 128;
	params->host_perio_tx_fifo_size = 256;
	params->ahbcfg = GAHBCFG_HBSTLEN_INCR16 << GAHBCFG_HBSTLEN_SHIFT;
}
