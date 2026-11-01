/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 EmberBSD contributors.
 *
 * FDT GPIO bit-banged SPI controller. The transfer path follows the SPI
 * controller contract in spivar.h; it intentionally supports only ordinary
 * single-line SPI at low speed.
 */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$EmberBSD$");

#include <sys/param.h>
#include <sys/device.h>
#include <sys/gpio.h>
#include <sys/systm.h>

#include <dev/fdt/fdtvar.h>
#include <dev/spi/spivar.h>

#define SPIGPIO_MAX_FREQUENCY	100000

struct spigpio_softc {
	device_t sc_dev;
	struct spi_controller sc_controller;
	struct fdtbus_gpio_pin *sc_sck;
	struct fdtbus_gpio_pin *sc_mosi;
	struct fdtbus_gpio_pin *sc_miso;
	struct fdtbus_gpio_pin *sc_cs;
	unsigned int sc_mode;
	unsigned int sc_speed;
	unsigned int sc_half_period;
	bool sc_configured;
};

static int spigpio_match(device_t, cfdata_t, void *);
static void spigpio_attach(device_t, device_t, void *);
static struct spi_controller *spigpio_get_controller(device_t);
static int spigpio_configure(void *, int, int, int);
static int spigpio_validate(const struct spigpio_softc *,
	const struct spi_transfer *);
static uint8_t spigpio_shift_byte(struct spigpio_softc *, uint8_t);
static int spigpio_transfer(void *, struct spi_transfer *);

CFATTACH_DECL_NEW(spigpio, sizeof(struct spigpio_softc),
	spigpio_match, spigpio_attach, NULL, NULL);

static const struct device_compatible_entry spigpio_compat[] = {
	{ .compat = "spi-gpio" },
	DEVICE_COMPAT_EOL
};

static const struct fdtbus_spi_controller_func spigpio_funcs = {
	.get_controller = spigpio_get_controller,
};

static int
spigpio_match(device_t parent, cfdata_t cf, void *aux)
{
	const struct fdt_attach_args *faa = aux;

	return of_compatible_match(faa->faa_phandle, spigpio_compat);
}

static void
spigpio_release_pins(struct spigpio_softc *sc)
{
	if (sc->sc_cs != NULL) {
		fdtbus_gpio_release(sc->sc_cs);
		sc->sc_cs = NULL;
	}
	if (sc->sc_miso != NULL) {
		fdtbus_gpio_release(sc->sc_miso);
		sc->sc_miso = NULL;
	}
	if (sc->sc_mosi != NULL) {
		fdtbus_gpio_release(sc->sc_mosi);
		sc->sc_mosi = NULL;
	}
	if (sc->sc_sck != NULL) {
		fdtbus_gpio_release(sc->sc_sck);
		sc->sc_sck = NULL;
	}
}

static void
spigpio_attach(device_t parent, device_t self, void *aux)
{
	struct spigpio_softc *sc = device_private(self);
	const struct fdt_attach_args *faa = aux;
	int phandle = faa->faa_phandle;

	sc->sc_dev = self;
	if (fdtbus_gpio_count(phandle, "sck-gpios") != 1 ||
	    fdtbus_gpio_count(phandle, "mosi-gpios") != 1 ||
	    fdtbus_gpio_count(phandle, "cs-gpios") != 1 ||
	    (of_hasprop(phandle, "miso-gpios") &&
	    fdtbus_gpio_count(phandle, "miso-gpios") != 1)) {
		aprint_error(": expected one SCK, MOSI, and CS GPIO; MISO is optional\n");
		return;
	}

	sc->sc_sck = fdtbus_gpio_acquire(phandle, "sck-gpios",
	    GPIO_PIN_OUTPUT);
	if (sc->sc_sck == NULL)
		goto fail;
	sc->sc_mosi = fdtbus_gpio_acquire(phandle, "mosi-gpios",
	    GPIO_PIN_OUTPUT);
	if (sc->sc_mosi == NULL)
		goto fail;
	if (of_hasprop(phandle, "miso-gpios")) {
		sc->sc_miso = fdtbus_gpio_acquire(phandle, "miso-gpios",
		    GPIO_PIN_INPUT);
		if (sc->sc_miso == NULL)
			goto fail;
	}
	sc->sc_cs = fdtbus_gpio_acquire(phandle, "cs-gpios",
	    GPIO_PIN_OUTPUT);
	if (sc->sc_cs == NULL)
		goto fail;

	/* GPIO writes use the logical active state from each FDT specifier. */
	fdtbus_gpio_write(sc->sc_cs, 0);
	fdtbus_gpio_write(sc->sc_sck, 0);
	fdtbus_gpio_write(sc->sc_mosi, 0);

	sc->sc_controller.sct_cookie = sc;
	sc->sc_controller.sct_nslaves = 1;
	sc->sc_controller.sct_configure = spigpio_configure;
	sc->sc_controller.sct_transfer = spigpio_transfer;
	sc->sc_controller.sct_caps = 0;
	fdtbus_register_spi_controller(self, phandle, &spigpio_funcs);
	aprint_naive("\n");
	aprint_normal(": GPIO bit-banged SPI, maximum %u Hz%s\n",
	    SPIGPIO_MAX_FREQUENCY, sc->sc_miso == NULL ? ", transmit only" : "");
	fdtbus_attach_spibus(self, phandle, spibus_print);
	return;

fail:
	spigpio_release_pins(sc);
	aprint_error(": cannot acquire GPIO pins\n");
}

static struct spi_controller *
spigpio_get_controller(device_t dev)
{
	struct spigpio_softc *sc = device_private(dev);

	return &sc->sc_controller;
}

static int
spigpio_configure(void *cookie, int slave, int mode, int speed)
{
	struct spigpio_softc *sc = cookie;

	if (slave != 0 || mode < SPI_MODE_0 || mode > SPI_MODE_3 ||
	    speed <= 0 || speed > SPIGPIO_MAX_FREQUENCY)
		return EINVAL;

	sc->sc_mode = mode;
	sc->sc_speed = speed;
	sc->sc_half_period = (500000U + speed - 1) / speed;
	sc->sc_configured = true;
	return 0;
}

static int
spigpio_validate(const struct spigpio_softc *sc, const struct spi_transfer *st)
{
	const struct spi_chunk *chunk;

	if (!sc->sc_configured || st->st_flags != 0 || st->st_chunks == NULL)
		return EOPNOTSUPP;
	for (chunk = st->st_chunks; chunk != NULL; chunk = chunk->chunk_next) {
		if (chunk->chunk_count <= 0 ||
		    (chunk->chunk_write == NULL && chunk->chunk_read == NULL))
			return EINVAL;
		if (chunk->chunk_bus_width > 1)
			return EOPNOTSUPP;
		if (chunk->chunk_read != NULL && sc->sc_miso == NULL)
			return EOPNOTSUPP;
	}
	return 0;
}

static uint8_t
spigpio_shift_byte(struct spigpio_softc *sc, uint8_t tx)
{
	const bool cpol = (sc->sc_mode & SPI_MODE_CPOL) != 0;
	const bool cpha = (sc->sc_mode & SPI_MODE_CPHA) != 0;
	uint8_t rx = 0;

	for (uint8_t mask = 0x80; mask != 0; mask >>= 1) {
		if (cpha) {
			fdtbus_gpio_write(sc->sc_sck, !cpol);
			fdtbus_gpio_write(sc->sc_mosi, (tx & mask) != 0);
			delay(sc->sc_half_period);
			fdtbus_gpio_write(sc->sc_sck, cpol);
			if (sc->sc_miso != NULL)
				rx |= fdtbus_gpio_read(sc->sc_miso) ? mask : 0;
			delay(sc->sc_half_period);
		} else {
			fdtbus_gpio_write(sc->sc_mosi, (tx & mask) != 0);
			delay(sc->sc_half_period);
			fdtbus_gpio_write(sc->sc_sck, !cpol);
			if (sc->sc_miso != NULL)
				rx |= fdtbus_gpio_read(sc->sc_miso) ? mask : 0;
			delay(sc->sc_half_period);
			fdtbus_gpio_write(sc->sc_sck, cpol);
		}
	}
	return rx;
}

static int
spigpio_transfer(void *cookie, struct spi_transfer *st)
{
	struct spigpio_softc *sc = cookie;
	struct spi_chunk *chunk;
	int error;

	error = spigpio_validate(sc, st);
	if (error != 0) {
		spi_done(st, error);
		return 0;
	}

	const bool cpol = (sc->sc_mode & SPI_MODE_CPOL) != 0;
	fdtbus_gpio_write(sc->sc_sck, cpol);
	fdtbus_gpio_write(sc->sc_cs, 1);
	delay(sc->sc_half_period);
	for (chunk = st->st_chunks; chunk != NULL; chunk = chunk->chunk_next) {
		while (chunk->chunk_wresid > 0) {
			uint8_t tx = 0xff, rx;

			if (chunk->chunk_wptr != NULL)
				tx = *chunk->chunk_wptr;
			rx = spigpio_shift_byte(sc, tx);
			if (chunk->chunk_rptr != NULL)
				*chunk->chunk_rptr = rx;
			if (chunk->chunk_wptr != NULL)
				chunk->chunk_wptr++;
			if (chunk->chunk_rptr != NULL)
				chunk->chunk_rptr++;
			chunk->chunk_wresid--;
			chunk->chunk_rresid--;
		}
	}
	delay(sc->sc_half_period);
	fdtbus_gpio_write(sc->sc_cs, 0);
	spi_done(st, 0);
	return 0;
}
