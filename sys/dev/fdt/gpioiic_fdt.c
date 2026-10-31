/* $NetBSD$ */
/*
 * Copyright (c) 2006 Alexander Yurchenko <grange@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
/* FDT adaptation of dev/gpio/gpioiic.c; requires open-drain GPIO pins. */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");
#include <sys/param.h>
#include <sys/device.h>
#include <sys/gpio.h>
#include <dev/fdt/fdtvar.h>
#include <dev/i2c/i2cvar.h>
#include <dev/i2c/i2c_bitbang.h>

#define SDA __BIT(0)
#define SCL __BIT(1)
struct gpioiic_fdt_softc {
	struct fdtbus_gpio_pin *sda, *scl;
	struct i2c_controller tag;
	bool sda_input;
};
static void
bb_bits(void *cookie, uint32_t bits)
{
	struct gpioiic_fdt_softc *sc = cookie;
	fdtbus_gpio_write_raw(sc->sda, sc->sda_input || (bits & SDA));
	fdtbus_gpio_write_raw(sc->scl, !!(bits & SCL));
}
static void
bb_dir(void *cookie, uint32_t bits)
{
	struct gpioiic_fdt_softc *sc = cookie;
	/* With open drain, high releases SDA for input. */
	sc->sda_input = !(bits & SDA);
	if (sc->sda_input)
		fdtbus_gpio_write_raw(sc->sda, 1);
}
static uint32_t
bb_read(void *cookie)
{
	struct gpioiic_fdt_softc *sc = cookie;
	return (fdtbus_gpio_read_raw(sc->sda) ? SDA : 0) |
	       (fdtbus_gpio_read_raw(sc->scl) ? SCL : 0);
}
static const struct i2c_bitbang_ops bbops = {
    bb_bits, bb_dir, bb_read, {SDA, SCL, SDA, 0}};
static int
bb_start(void *c, int f)
{
	return i2c_bitbang_send_start(c, f, &bbops);
}
static int
bb_stop(void *c, int f)
{
	return i2c_bitbang_send_stop(c, f, &bbops);
}
static int
bb_initiate(void *c, i2c_addr_t a, int f)
{
	return i2c_bitbang_initiate_xfer(c, a, f, &bbops);
}
static int
bb_read_byte(void *c, uint8_t *p, int f)
{
	return i2c_bitbang_read_byte(c, p, f, &bbops);
}
static int
bb_write_byte(void *c, uint8_t v, int f)
{
	return i2c_bitbang_write_byte(c, v, f, &bbops);
}
static const struct device_compatible_entry compat[] = {{.compat = "i2c-gpio"},
                                                        DEVICE_COMPAT_EOL};
static int
gpioiic_fdt_match(device_t parent, cfdata_t match, void *aux)
{
	struct fdt_attach_args *faa = aux;
	return of_compatible_match(faa->faa_phandle, compat);
}
static void
gpioiic_fdt_attach(device_t parent, device_t self, void *aux)
{
	struct gpioiic_fdt_softc *sc = device_private(self);
	struct fdt_attach_args *faa = aux;
	int flags = GPIO_PIN_INPUT | GPIO_PIN_OUTPUT | GPIO_PIN_OPENDRAIN;

	if (of_hasprop(faa->faa_phandle, "i2c-gpio,delay-us") ||
	    of_hasprop(faa->faa_phandle, "i2c-gpio,scl-output-only") ||
	    of_hasprop(faa->faa_phandle, "i2c-gpio,sda-output-only")) {
		aprint_error(": unsupported GPIO I2C timing or direction\n");
		return;
	}

	sc->sda = fdtbus_gpio_acquire(faa->faa_phandle, "sda-gpios", flags);
	if (sc->sda == NULL) {
		aprint_error(": cannot acquire SDA open-drain GPIO\n");
		return;
	}
	sc->scl = fdtbus_gpio_acquire(faa->faa_phandle, "scl-gpios", flags);
	if (sc->scl == NULL) {
		fdtbus_gpio_release(sc->sda);
		aprint_error(": cannot acquire SCL open-drain GPIO\n");
		return;
	}
	fdtbus_gpio_write_raw(sc->sda, 1);
	fdtbus_gpio_write_raw(sc->scl, 1);
	iic_tag_init(&sc->tag);
	sc->tag.ic_cookie = sc;
	sc->tag.ic_send_start = bb_start;
	sc->tag.ic_send_stop = bb_stop;
	sc->tag.ic_initiate_xfer = bb_initiate;
	sc->tag.ic_read_byte = bb_read_byte;
	sc->tag.ic_write_byte = bb_write_byte;
	fdtbus_register_i2c_controller(&sc->tag, faa->faa_phandle);
	aprint_naive("\n");
	aprint_normal(": GPIO bit-banged I2C\n");
	fdtbus_attach_i2cbus(self, faa->faa_phandle, &sc->tag, iicbus_print);
}
CFATTACH_DECL_NEW(gpioiic_fdt, sizeof(struct gpioiic_fdt_softc),
                  gpioiic_fdt_match, gpioiic_fdt_attach, NULL, NULL);
