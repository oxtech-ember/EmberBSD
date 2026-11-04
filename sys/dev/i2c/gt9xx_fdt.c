/* Origin: EmberBSD - retain YS-M33 Goodix reset integration. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <sys/param.h>
#include <sys/device.h>
#include <sys/bus.h>
#include <sys/kmem.h>
#include <dev/i2c/i2cvar.h>
#include <dev/fdt/fdtvar.h>
#include "gt9xx_fdt.h"
int gt9xx_ys_m33_reset(bus_space_tag_t);
static const struct device_compatible_entry gt9xx_compat[] = {
	{ .compat = "goodix,gt9xx" },
	{ .compat = "goodix,gt9271" },
	DEVICE_COMPAT_EOL
};
static const struct device_compatible_entry a133_compat[] = {
	{ .compat = "allwinner,a133" },
	DEVICE_COMPAT_EOL
};
int
gt9xx_fdt_match(struct i2c_attach_args *ia, cfdata_t cf)
{
	int result;

	if (ia->ia_cookietype == I2C_COOKIE_OF && ia->ia_addr == 0x5d &&
	    iic_use_direct_match(ia, cf, gt9xx_compat, &result))
		return (result);
	return (0);
}
int
gt9xx_fdt_prepare(struct i2c_attach_args *ia)
{
	int phandle = ia->ia_cookie, error;
	bus_space_tag_t bst;

	if (!of_hasprop(phandle, "ember,ys-m33-reset") ||
	    !of_compatible_match(OF_finddevice("/"), a133_compat))
		return (EINVAL);
	bst = fdtbus_bus_tag_create(phandle, 0);
	error = gt9xx_ys_m33_reset(bst);
	kmem_free(bst, sizeof(*bst));
	return (error);
}
