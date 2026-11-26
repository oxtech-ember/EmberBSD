/*-
 * Copyright (c) 2018 Jared McNeill <jmcneill@invisible.ca>
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
 * Attachment based on the NetBSD Rockchip IOMUX drivers.
 * Register layout: Rockchip RK3506 TRM, Part 1, GPIO1_IOC and GPIO234_IOC.
 * Support direct mux and weak bias on the regular pads of banks 1, 2 and 3.
 */
#include <sys/param.h>
#include <sys/device.h>
#include <sys/endian.h>
#include <sys/errno.h>
#include <sys/gpio.h>
#include <sys/systm.h>

#include <dev/fdt/fdtvar.h>
#include <dev/fdt/syscon.h>

#define	RK3506_MUX_REG(bank, pin)	(0x20 * (bank) + 4 * ((pin) / 4))
#define	RK3506_PULL_REG(bank, pin)	(0x200 + 0x10 * (bank) + 4 * ((pin) / 8))

struct rk3506_iomux_softc {
	struct syscon *sc_ioc1;
	struct syscon *sc_ioc234;
};

static const struct device_compatible_entry compat_data[] = {
	{ .compat = "rockchip,rk3506-pinctrl" },
	DEVICE_COMPAT_EOL
};

static int	rk3506_iomux_match(device_t, cfdata_t, void *);
static void	rk3506_iomux_attach(device_t, device_t, void *);

CFATTACH_DECL_NEW(rk3506_iomux, sizeof(struct rk3506_iomux_softc),
    rk3506_iomux_match, rk3506_iomux_attach, NULL, NULL);

static int
rk3506_iomux_pin(struct rk3506_iomux_softc *sc, const uint32_t *cells,
    bool apply)
{
	static const u_int npins[] = { [1] = 28, [2] = 17, [3] = 15 };
	const u_int bank = be32toh(cells[0]);
	const u_int pin = be32toh(cells[1]);
	const u_int mux = be32toh(cells[2]);
	const int cfg = fdtbus_get_phandle_from_native(be32toh(cells[3]));
	struct syscon *ioc;
	uint32_t mask, val;
	int bias;

	if (bank >= __arraycount(npins) || pin >= npins[bank] ||
	    mux > 15 || cfg < 0)
		return EINVAL;
	ioc = bank == 1 ? sc->sc_ioc1 : sc->sc_ioc234;
	if (ioc == NULL)
		return ENXIO;
	if (fdtbus_pinctrl_parse_drive(cfg) >= 0 ||
	    of_hasprop(cfg, "drive-strength") ||
	    of_hasprop(cfg, "drive-strength-microamp") ||
	    of_hasprop(cfg, "bias-high-impedance") ||
	    of_hasprop(cfg, "bias-bus-hold") ||
	    of_hasprop(cfg, "bias-pull-pin-default") ||
	    OF_getproplen(cfg, "bias-pull-up") > 0 ||
	    OF_getproplen(cfg, "bias-pull-down") > 0 ||
	    of_hasprop(cfg, "input-schmitt-enable") ||
	    of_hasprop(cfg, "input-schmitt-disable") ||
	    of_hasprop(cfg, "input-disable") ||
	    fdtbus_pinctrl_parse_input_output(cfg, NULL) >= 0)
		return EOPNOTSUPP;
	bias = fdtbus_pinctrl_parse_bias(cfg, NULL);
	if (!apply)
		return 0;

	syscon_lock(ioc);
	if (bias != -1) {
		mask = 0x3U << ((pin % 8) * 2);
		val = bias == GPIO_PIN_PULLUP ? 1 :
		    bias == GPIO_PIN_PULLDOWN ? 2 : 0;
		syscon_write_4(ioc, RK3506_PULL_REG(bank, pin),
		    (mask << 16) | __SHIFTIN(val, mask));
	}
	mask = 0xfU << ((pin % 4) * 4);
	syscon_write_4(ioc, RK3506_MUX_REG(bank, pin),
	    (mask << 16) | __SHIFTIN(mux, mask));
	syscon_unlock(ioc);

	return 0;
}

static int
rk3506_iomux_set_config(device_t dev, const void *data, size_t len)
{
	struct rk3506_iomux_softc * const sc = device_private(dev);
	const uint32_t *pins;
	int phandle, bytes, error;

	if (len != sizeof(uint32_t))
		return EINVAL;
	phandle = fdtbus_get_phandle_from_native(be32dec(data));
	if (phandle < 0)
		return EINVAL;
	pins = fdtbus_get_prop(phandle, "rockchip,pins", &bytes);
	if (pins == NULL || bytes <= 0 || bytes % (4 * sizeof(*pins)) != 0)
		return EINVAL;

	/* Check the complete pin group before changing any pad. */
	for (u_int pass = 0; pass < 2; pass++) {
		for (int i = 0; i < bytes / (int)sizeof(*pins); i += 4) {
			error = rk3506_iomux_pin(sc, &pins[i], pass != 0);
			if (error)
				return error;
		}
	}
	return 0;
}

static const struct fdtbus_pinctrl_controller_func rk3506_iomux_funcs = {
	.set_config = rk3506_iomux_set_config,
};

static int
rk3506_iomux_match(device_t parent, cfdata_t cf, void *aux)
{
	static const char * const ioc234[] = { "rockchip,rk3506-ioc234", NULL };
	struct fdt_attach_args * const faa = aux;
	const int phandle = faa->faa_phandle;
	const int match = of_compatible_match(phandle, compat_data);
	int grf;

	if (!match)
		return 0;
	/* The vendor tree uses a different binding for the IOC banks. */
	grf = fdtbus_get_phandle(phandle, "rockchip,grf");
	if (grf < 0 || !of_compatible(grf, ioc234))
		return 0;
	return match;
}

static void
rk3506_iomux_attach(device_t parent, device_t self, void *aux)
{
	static const char * const gpio_bank[] = { "rockchip,gpio-bank", NULL };
	static const char * const ioc1[] = { "rockchip,rk3506-ioc1", NULL };
	struct rk3506_iomux_softc * const sc = device_private(self);
	struct fdt_attach_args * const faa = aux;
	const int phandle = faa->faa_phandle;
	int child, group, grf;

	sc->sc_ioc234 = fdtbus_syscon_acquire(phandle, "rockchip,grf");
	if (sc->sc_ioc234 == NULL) {
		aprint_error(": missing GPIO234 IOC\n");
		return;
	}
	aprint_naive("\n");
	aprint_normal(": RK3506 IOMUX control\n");

	for (child = OF_child(phandle); child; child = OF_peer(child)) {
		if (!fdtbus_status_okay(child))
			continue;
		if (of_compatible(child, gpio_bank)) {
			grf = fdtbus_get_phandle(child, "rockchip,grf");
			if (grf >= 0 && of_compatible(grf, ioc1))
				sc->sc_ioc1 = fdtbus_syscon_lookup(grf);
			continue;
		}
		for (group = OF_child(child); group; group = OF_peer(group)) {
			if (of_hasprop(group, "rockchip,pins"))
				fdtbus_register_pinctrl_config(self, group,
				    &rk3506_iomux_funcs);
		}
	}

	for (child = OF_child(phandle); child; child = OF_peer(child)) {
		struct fdt_attach_args cfaa = *faa;

		if (!fdtbus_status_okay(child) || !of_compatible(child, gpio_bank))
			continue;
		cfaa.faa_phandle = child;
		cfaa.faa_name = fdtbus_get_string(child, "name");
		cfaa.faa_quiet = false;
		config_found(self, &cfaa, NULL, CFARGS_NONE);
	}
}
