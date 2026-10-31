/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-FileCopyrightText: 2026 EmberBSD contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * ESP32-S31 GPSPI PIO controller. Peripheral sequencing follows ESP-IDF
 * v6.1 spi_ll.h (fff9895c82d744c7237be8847347bdd1b07c6643).
 * The FDT/SPI integration is specific to EmberBSD, not an IDF application.
 */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$EmberBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/systm.h>
#include <dev/clk/clk_backend.h>
#include <dev/fdt/fdtvar.h>
#include <dev/fdt/syscon.h>
#include <dev/spi/spivar.h>
#include <riscv/espressif/esp_spireg.h>

struct esp_spi_softc {
	device_t sc_dev;
	bus_space_tag_t sc_bst;
	bus_space_handle_t sc_bsh;
	struct syscon *sc_syscon;
	struct spi_controller sc_spi;
	bus_size_t sc_clkrst;
	u_int sc_source_hz;
	u_int sc_speed;
	uint32_t sc_clock;
	uint32_t sc_user;
	uint32_t sc_misc;
};

#define ESPSPI_READ(sc, reg) \
	bus_space_read_4((sc)->sc_bst, (sc)->sc_bsh, (reg))
#define ESPSPI_WRITE(sc, reg, value) \
	bus_space_write_4((sc)->sc_bst, (sc)->sc_bsh, (reg), (value))

static int esp_spi_match(device_t, cfdata_t, void *);
static void esp_spi_attach(device_t, device_t, void *);
static int esp_spi_configure(void *, int, int, int);
static int esp_spi_transfer(void *, struct spi_transfer *);
static struct spi_controller *esp_spi_get_controller(device_t);

CFATTACH_DECL_NEW(espspi, sizeof(struct esp_spi_softc),
    esp_spi_match, esp_spi_attach, NULL, NULL);

static const struct device_compatible_entry compat_data[] = {
	{ .compat = "espressif,esp32s31-spi2", .value = ESPSPI2_CLKRST },
	{ .compat = "espressif,esp32s31-spi3", .value = ESPSPI3_CLKRST },
	DEVICE_COMPAT_EOL
};

static const struct fdtbus_spi_controller_func esp_spi_funcs = {
	.get_controller = esp_spi_get_controller,
};

static struct spi_controller *
esp_spi_get_controller(device_t dev)
{
	struct esp_spi_softc *sc = device_private(dev);

	return &sc->sc_spi;
}

static int
esp_spi_match(device_t parent, cfdata_t cf, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return of_compatible_match(faa->faa_phandle, compat_data);
}

/* Reset only this GPSPI instance; shared clock registers use syscon locking. */
static void
esp_spi_reset(struct esp_spi_softc *sc)
{
	uint32_t control;

	syscon_lock(sc->sc_syscon);
	control = syscon_read_4(sc->sc_syscon, sc->sc_clkrst);
	control &= ~(ESPSPI_CLKRST_DIVSEL | ESPSPI_CLKRST_RESET);
	control |= ESPSPI_CLKRST_ENABLE;
	syscon_write_4(sc->sc_syscon, sc->sc_clkrst,
	    control | ESPSPI_CLKRST_RESET);
	(void)syscon_read_4(sc->sc_syscon, sc->sc_clkrst);
	syscon_write_4(sc->sc_syscon, sc->sc_clkrst, control);
	syscon_unlock(sc->sc_syscon);

	/* XTAL source, both upstream dividers one, no DMA or interrupts. */
	ESPSPI_WRITE(sc, ESPSPI_CLK_GATE, ESPSPI_CLK_ENABLE);
	ESPSPI_WRITE(sc, ESPSPI_SLAVE, 0);
	ESPSPI_WRITE(sc, ESPSPI_CTRL, 0);
	ESPSPI_WRITE(sc, ESPSPI_USER1, 0);
	ESPSPI_WRITE(sc, ESPSPI_USER2, 0);
	ESPSPI_WRITE(sc, ESPSPI_DMA_CONF, ESPSPI_DMA_BASE);
	ESPSPI_WRITE(sc, ESPSPI_INT_ENA, 0);
	ESPSPI_WRITE(sc, ESPSPI_INT_CLR, UINT32_MAX);
	ESPSPI_WRITE(sc, ESPSPI_CLOCK, sc->sc_clock);
	ESPSPI_WRITE(sc, ESPSPI_USER, sc->sc_user);
	ESPSPI_WRITE(sc, ESPSPI_MISC, sc->sc_misc);
}

static void
esp_spi_attach(device_t parent, device_t self, void *aux)
{
	struct esp_spi_softc *sc = device_private(self);
	struct fdt_attach_args *faa = aux;
	const struct device_compatible_entry *dce;
	struct clk *xtal;
	bus_addr_t addr;
	bus_size_t size;
	int phandle = faa->faa_phandle;

	sc->sc_dev = self;
	sc->sc_bst = faa->faa_bst;
	dce = of_compatible_lookup(phandle, compat_data);
	sc->sc_clkrst = dce->value;
	sc->sc_syscon = fdtbus_syscon_acquire(phandle, "espressif,syscon");
	xtal = fdtbus_clock_get(phandle, "xtal");
	if (sc->sc_syscon == NULL || xtal == NULL ||
	    (sc->sc_source_hz = clk_get_rate(xtal)) == 0) {
		aprint_error(": missing clock/reset controller or XTAL clock\n");
		return;
	}
	if (fdtbus_get_reg(phandle, 0, &addr, &size) != 0 ||
	    size < ESPSPI_REG_SIZE ||
	    bus_space_map(sc->sc_bst, addr, size, 0, &sc->sc_bsh) != 0) {
		aprint_error(": cannot map registers\n");
		return;
	}
	sc->sc_clock = ESPSPI_CLOCK_DIRECT;
	sc->sc_misc = ESPSPI_CS_DISABLE;
	esp_spi_reset(sc);
	if (fdtbus_pinctrl_set_config_index(phandle, 0) != 0) {
		aprint_error(": cannot configure pins\n");
		bus_space_unmap(sc->sc_bst, sc->sc_bsh, size);
		return;
	}
	sc->sc_spi.sct_cookie = sc;
	sc->sc_spi.sct_nslaves = sc->sc_clkrst == ESPSPI2_CLKRST ? 6 : 3;
	sc->sc_spi.sct_configure = esp_spi_configure;
	sc->sc_spi.sct_transfer = esp_spi_transfer;
	sc->sc_spi.sct_caps = SPI_CAP_DUAL_TX | SPI_CAP_QUAD_TX;
	aprint_normal(": GPSPI PIO, XTAL %u Hz\n", sc->sc_source_hz);
	fdtbus_register_spi_controller(self, phandle, &esp_spi_funcs);
	fdtbus_attach_spibus(self, phandle, spibus_print);
}

/* Select the fastest divider that does not exceed the peripheral's limit. */
static int
esp_spi_clock(u_int source, u_int limit, uint32_t *value, u_int *actual)
{
	u_int best = 0;
	uint32_t selected = 0;

	if (source == 0 || limit == 0)
		return EINVAL;
	if (limit >= source) {
		*value = ESPSPI_CLOCK_DIRECT;
		*actual = source;
		return 0;
	}
	for (u_int n = 2; n <= 64; n++) {
		for (u_int pre = 1; pre <= 16; pre++) {
			u_int candidate_hz = source / (pre * n);
			if (source > (uint64_t)limit * pre * n ||
			    candidate_hz <= best)
				continue;
			best = candidate_hz;
			selected = ((pre - 1) << 18) | ((n - 1) << 12) |
			    ((n / 2 - 1) << 6) | (n - 1);
		}
	}
	if (best == 0)
		return EINVAL;
	*value = selected;
	*actual = best;
	return 0;
}

static int
esp_spi_configure(void *cookie, int slave, int mode, int speed)
{
	struct esp_spi_softc *sc = cookie;
	int error;

	if (slave < 0 || slave >= sc->sc_spi.sct_nslaves ||
	    mode < SPI_MODE_0 || mode > SPI_MODE_3 || speed <= 0)
		return EINVAL;
	error = esp_spi_clock(sc->sc_source_hz, speed,
	    &sc->sc_clock, &sc->sc_speed);
	if (error != 0)
		return error;
	sc->sc_misc = ESPSPI_CS_DISABLE & ~__BIT(slave);
	if (mode & SPI_MODE_CPOL)
		sc->sc_misc |= ESPSPI_CK_IDLE_EDGE;
	sc->sc_user = ((mode == SPI_MODE_1 || mode == SPI_MODE_2) ?
	    ESPSPI_CK_OUT_EDGE : 0);
	ESPSPI_WRITE(sc, ESPSPI_CLOCK, sc->sc_clock);
	ESPSPI_WRITE(sc, ESPSPI_MISC, sc->sc_misc);
	return 0;
}

static int
esp_spi_wait(struct esp_spi_softc *sc, bus_size_t reg, uint32_t mask,
    bool set, u_int timeout)
{
	for (u_int i = 0; i < timeout; i++) {
		if (((ESPSPI_READ(sc, reg) & mask) != 0) == set)
			return 0;
		delay(1);
	}
	return ETIMEDOUT;
}

static int
esp_spi_block(struct esp_spi_softc *sc, const uint8_t *tx, uint8_t *rx,
    u_int count, u_int width, bool more)
{
	uint32_t user = sc->sc_user, word;
	int error;

	KASSERT(count > 0 && count <= ESPSPI_FIFO_SIZE);
	if (tx != NULL || rx == NULL)
		user |= ESPSPI_USR_MOSI;
	if (rx != NULL)
		user |= ESPSPI_USR_MISO;
	if (tx != NULL && rx != NULL)
		user |= ESPSPI_DOUTDIN;
	if (width == 2)
		user |= ESPSPI_FWRITE_DUAL;
	if (width == 4)
		user |= ESPSPI_FWRITE_QUAD;

	ESPSPI_WRITE(sc, ESPSPI_DMA_CONF, ESPSPI_DMA_BASE |
	    ESPSPI_TX_FIFO_RESET | ESPSPI_RX_FIFO_RESET);
	ESPSPI_WRITE(sc, ESPSPI_DMA_CONF, ESPSPI_DMA_BASE);
	for (u_int n = 0; n < count; n += 4) {
		word = UINT32_MAX;
		if (tx != NULL)
			memcpy(&word, tx + n, MIN(4, count - n));
		ESPSPI_WRITE(sc, ESPSPI_W0 + n, word);
	}
	ESPSPI_WRITE(sc, ESPSPI_USER, user);
	ESPSPI_WRITE(sc, ESPSPI_DLEN, count * 8 - 1);
	ESPSPI_WRITE(sc, ESPSPI_MISC, sc->sc_misc |
	    (more ? ESPSPI_CS_KEEP_ACTIVE : 0));
	ESPSPI_WRITE(sc, ESPSPI_INT_CLR, UINT32_MAX);
	ESPSPI_WRITE(sc, ESPSPI_CMD, ESPSPI_UPDATE);
	error = esp_spi_wait(sc, ESPSPI_CMD, ESPSPI_UPDATE, false, 1000);
	if (error != 0)
		return error;
	ESPSPI_WRITE(sc, ESPSPI_CMD, ESPSPI_USR);
	error = esp_spi_wait(sc, ESPSPI_INT_RAW, ESPSPI_TRANS_DONE, true,
	    1000 + 2 * (ESPSPI_FIFO_SIZE * 8000000U / sc->sc_speed));
	if (error == 0 && rx != NULL) {
		for (u_int n = 0; n < count; n += 4) {
			word = ESPSPI_READ(sc, ESPSPI_W0 + n);
			memcpy(rx + n, &word, MIN(4, count - n));
		}
	}
	return error;
}

static int
esp_spi_transfer(void *cookie, struct spi_transfer *st)
{
	struct esp_spi_softc *sc = cookie;
	struct spi_chunk *chunk, *last = NULL;
	int error = 0;

	/* Conservative GPIO-matrix receive timing; multi-line RX is not exposed. */
	for (chunk = st->st_chunks; chunk; chunk = chunk->chunk_next) {
		if (chunk->chunk_count < 0 || (chunk->chunk_read != NULL &&
		    sc->sc_speed > 10000000)) {
			spi_done(st, EINVAL);
			return 0;
		}
		if (chunk->chunk_count > 0)
			last = chunk;
	}
	for (chunk = st->st_chunks; chunk && error == 0;
	    chunk = chunk->chunk_next) {
		while (chunk->chunk_wresid > 0) {
			u_int count = MIN(chunk->chunk_wresid, ESPSPI_FIFO_SIZE);
			bool more = chunk != last || chunk->chunk_wresid > count;
			error = esp_spi_block(sc, chunk->chunk_wptr,
			    chunk->chunk_rptr, count, chunk->chunk_bus_width, more);
			if (error != 0)
				break;
			chunk->chunk_wresid -= count;
			chunk->chunk_rresid -= count;
			if (chunk->chunk_wptr != NULL)
				chunk->chunk_wptr += count;
			if (chunk->chunk_rptr != NULL)
				chunk->chunk_rptr += count;
		}
	}
	if (error != 0)
		esp_spi_reset(sc); /* Release CS and restore the selected configuration. */
	spi_done(st, error);
	return 0;
}
