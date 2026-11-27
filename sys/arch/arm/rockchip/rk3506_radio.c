/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Luckfox Lyra Zero W U10 WL_DIS#. The Rockchip BSP holds the power key
 * inactive for 20 ms and waits another 20 ms after it releases the radio.
 */

#include <sys/param.h>
#include <sys/device.h>
#include <sys/endian.h>
#include <sys/errno.h>
#include <sys/systm.h>

#include <dev/fdt/fdtvar.h>

#include <arm/rockchip/rk_gpiovar.h>

#include "rk_gpio.h"

static int	rk3506_radio_match(device_t, cfdata_t, void *);
static void	rk3506_radio_attach(device_t, device_t, void *);

CFATTACH_DECL_NEW(rk3506radio, 0,
    rk3506_radio_match, rk3506_radio_attach, NULL, NULL);

static int
rk3506_radio_match(device_t parent, cfdata_t cf, void *aux)
{
	static const char * const boards[] = { "luckfox,lyra-zero-w", NULL };
	static const struct device_compatible_entry compat[] = {
		{ .compat = "rfkill-gpio" }, DEVICE_COMPAT_EOL
	};
	const struct fdt_attach_args *faa = aux;
	const char *type = fdtbus_get_string(faa->faa_phandle, "radio-type");

	if (!of_compatible(OF_finddevice("/"), boards) ||
	    type == NULL || strcmp(type, "wlan") != 0)
		return 0;
	return of_compatible_match(faa->faa_phandle, compat);
}

#if NRK_GPIO > 0
static int
rk3506_radio_enable(device_t bank, u_int pin, bool *preserved)
{
	bool driving_high, level;
	int error, hold_error;

	error = rk_gpio_v2_output_state(bank, pin, &driving_high, &level);
	if (error)
		return error;
	/* R77 pulls up an input. Only a driven high line can be retained. */
	if (driving_high) {
		*preserved = true;
		return level ? 0 : EIO;
	}
	*preserved = false;
	error = rk_gpio_config_output(bank, pin, 0, true);
	if (error)
		return error;
	delay(20000);
	/* Release the radio even if the inactive readback fails. */
	hold_error = rk_gpio_v2_output_state(bank, pin, &driving_high, &level);
	if (!hold_error && (driving_high || level))
		hold_error = EIO;
	error = rk_gpio_config_output(bank, pin, 1, true);
	if (error)
		return error;
	delay(20000);
	error = rk_gpio_v2_output_state(bank, pin, &driving_high, &level);
	if (error)
		return error;
	return driving_high && level ? hold_error : EIO;
}
#endif

static void
rk3506_radio_attach(device_t parent, device_t self, void *aux)
{
#if NRK_GPIO > 0
	const struct fdt_attach_args *faa = aux;
	const int node = faa->faa_phandle;
	const uint32_t *gpios;
	device_t bank;
	int len, error;
	bool preserved;
#endif

	aprint_naive("\n");
	aprint_normal(": onboard WLAN enable\n");
#if NRK_GPIO > 0
	gpios = fdtbus_get_prop(node, "shutdown-gpios", &len);
	if (gpios == NULL || len != 3 * sizeof(*gpios) ||
	    be32toh(gpios[2]) != 0) {
		aprint_error_dev(self, "expected one active-high GPIO\n");
		return;
	}
	bank = of_device_from_phandle(
	    fdtbus_get_phandle_from_native(be32toh(gpios[0])));
	if (bank == NULL || !device_is_a(bank, "rkgpio")) {
		aprint_error_dev(self, "GPIO bank unavailable\n");
		return;
	}
	error = fdtbus_pinctrl_set_config(node, "default");
	if (!error)
		error = rk3506_radio_enable(bank, be32toh(gpios[1]), &preserved);
	if (error)
		aprint_error_dev(self, "cannot enable WLAN: %d\n", error);
	else
		aprint_normal_dev(self, "WL_DIS# output high (%s)\n",
		    preserved ? "retained" : "sequenced");
#else
	aprint_error_dev(self, "GPIO driver unavailable\n");
#endif
}
