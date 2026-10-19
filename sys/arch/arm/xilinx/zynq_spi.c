/*-
 * Copyright (c) 2018 Thomas Skibo <thomasskibo@yahoo.com>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* Origin: EmberBSD SPI framework adaptation; registers from FreeBSD bd369bc7c2368b19789cfabcea99e355e1e959da. */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/systm.h>

#include <dev/clk/clk_backend.h>
#include <dev/fdt/fdtvar.h>
#include <dev/spi/spivar.h>
#include <arm/xilinx/zynq_spireg.h>

/*
 * PIO control transfers with bounded polling, including during cold boot.
 * The SPI framework serializes configure/transfer until spi_wait releases it.
 * No interrupt handler or DMA engine shares these registers.
 */
#define ZSPI_MAX_TRANSFER	65536
#define ZSPI_POLL_BUDGET_US	100000

struct zynq_spi_softc {
	device_t sc_dev;
	bus_space_tag_t sc_bst;
	bus_space_handle_t sc_bsh;
	struct spi_controller sc_spi;
	uint32_t sc_config;
	u_int sc_ref_rate;
};

static int zynq_spi_match(device_t, cfdata_t, void *);
static void zynq_spi_attach(device_t, device_t, void *);
static int zynq_spi_configure(void *, int, int, int);
static int zynq_spi_transfer(void *, struct spi_transfer *);
static struct spi_controller *zynq_spi_get_controller(device_t);

CFATTACH_DECL_NEW(zynqspi, sizeof(struct zynq_spi_softc),
    zynq_spi_match, zynq_spi_attach, NULL, NULL);

static const struct device_compatible_entry compat_data[] = {
	{ .compat = "xlnx,zynq-spi-r1p6" },
	{ .compat = "xlnx,zynq-spi-1.0" },
	{ .compat = "cdns,spi-r1p6" },
	DEVICE_COMPAT_EOL
};

static const struct fdtbus_spi_controller_func zynq_spi_fdt_funcs = {
	.get_controller = zynq_spi_get_controller
};

static uint32_t
zynq_spi_read(struct zynq_spi_softc *sc, bus_size_t reg)
{

	return bus_space_read_4(sc->sc_bst, sc->sc_bsh, reg);
}

static void
zynq_spi_write(struct zynq_spi_softc *sc, bus_size_t reg, uint32_t value)
{

	bus_space_write_4(sc->sc_bst, sc->sc_bsh, reg, value);
	bus_space_barrier(sc->sc_bst, sc->sc_bsh, 0, 0x30,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
}

static int
zynq_spi_drain(struct zynq_spi_softc *sc)
{
	u_int n;

	for (n = 0; n < ZSPI_FIFO_DEPTH; n++) {
		if ((zynq_spi_read(sc, ZSPI_STATUS) & ZSPI_RX_NOT_EMPTY) == 0)
			return 0;
		(void)zynq_spi_read(sc, ZSPI_RX_DATA);
	}
	return (zynq_spi_read(sc, ZSPI_STATUS) & ZSPI_RX_NOT_EMPTY) ?
	    EIO : 0;
}

static int
zynq_spi_wait(struct zynq_spi_softc *sc, uint32_t mask, u_int *budget)
{
	uint32_t status;

	for (;;) {
		status = zynq_spi_read(sc, ZSPI_STATUS);
		if (status & (ZSPI_RX_OVERFLOW | ZSPI_MODE_FAULT))
			return EIO;
		if (status & mask)
			return 0;
		if (*budget == 0)
			return ETIMEDOUT;
		--*budget;
		delay(1);
	}
}

static int
zynq_spi_match(device_t parent, cfdata_t cf, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return of_compatible_match(faa->faa_phandle, compat_data);
}

static void
zynq_spi_attach(device_t parent, device_t self, void *aux)
{
	struct zynq_spi_softc *sc = device_private(self);
	struct fdt_attach_args *faa = aux;
	const int phandle = faa->faa_phandle;
	struct clk *pclk, *refclk;
	bus_addr_t addr;
	bus_size_t size;

	sc->sc_dev = self;
	sc->sc_bst = faa->faa_bst;
	pclk = fdtbus_clock_get(phandle, "pclk");
	refclk = fdtbus_clock_get(phandle, "ref_clk");
	if (pclk == NULL || refclk == NULL) {
		aprint_error(": cannot get SPI clocks\n");
		return;
	}
	if (clk_enable(pclk) != 0) {
		aprint_error(": cannot enable peripheral clock\n");
		return;
	}
	if (clk_enable(refclk) != 0) {
		aprint_error(": cannot enable reference clock\n");
		goto disable_pclk;
	}
	sc->sc_ref_rate = clk_get_rate(refclk);
	if (sc->sc_ref_rate == 0 ||
	    fdtbus_get_reg(phandle, 0, &addr, &size) != 0 ||
	    size < 0x30 ||
	    bus_space_map(sc->sc_bst, addr, size, 0, &sc->sc_bsh) != 0) {
		aprint_error(": invalid clock or register mapping\n");
		goto disable_refclk;
	}

	sc->sc_config = ZSPI_CFG_MASTER | ZSPI_CFG_MAN_CS | ZSPI_CFG_CS;
	zynq_spi_write(sc, ZSPI_INT_DISABLE, ~0U);
	zynq_spi_write(sc, ZSPI_ENABLE, 0);
	zynq_spi_write(sc, ZSPI_CONFIG, sc->sc_config);
	zynq_spi_write(sc, ZSPI_TX_THRESHOLD, 1);
	zynq_spi_write(sc, ZSPI_RX_THRESHOLD, 1);
	if (zynq_spi_drain(sc) != 0) {
		aprint_error(": receive FIFO did not drain\n");
		bus_space_unmap(sc->sc_bst, sc->sc_bsh, size);
		goto disable_refclk;
	}
	zynq_spi_write(sc, ZSPI_STATUS, ~0U);
	sc->sc_spi.sct_cookie = sc;
	sc->sc_spi.sct_nslaves = ZSPI_NSLAVES;
	sc->sc_spi.sct_configure = zynq_spi_configure;
	sc->sc_spi.sct_transfer = zynq_spi_transfer;

	aprint_naive("\n");
	aprint_normal(": Zynq PS SPI, reference %u Hz, bounded PIO\n",
	    sc->sc_ref_rate);
	fdtbus_register_spi_controller(self, phandle, &zynq_spi_fdt_funcs);
	fdtbus_attach_spibus(self, phandle, spibus_print);
	return;

disable_refclk:
	clk_disable(refclk);
disable_pclk:
	clk_disable(pclk);
}

static struct spi_controller *
zynq_spi_get_controller(device_t dev)
{
	struct zynq_spi_softc *sc = device_private(dev);

	return &sc->sc_spi;
}

static int
zynq_spi_configure(void *cookie, int slave, int mode, int speed)
{
	struct zynq_spi_softc *sc = cookie;
	uint32_t config;
	u_int div;

	if (slave < 0 || slave >= ZSPI_NSLAVES || speed <= 0 ||
	    mode < SPI_MODE_0 || mode > SPI_MODE_3)
		return EINVAL;

	/* The controller does not support the encoded divide-by-two setting. */
	for (div = 1; div < 8; div++)
		if ((sc->sc_ref_rate >> (div + 1)) <= (u_int)speed)
			break;
	if (div == 8)
		return EINVAL;
	config = ZSPI_CFG_MASTER | ZSPI_CFG_MAN_CS | ZSPI_CFG_CS |
	    __SHIFTIN(div, ZSPI_CFG_BAUD);
	if (mode & SPI_MODE_CPHA)
		config |= ZSPI_CFG_CPHA;
	if (mode & SPI_MODE_CPOL)
		config |= ZSPI_CFG_CPOL;
	sc->sc_config = config;
	return 0;
}

static int
zynq_spi_transfer(void *cookie, struct spi_transfer *st)
{
	struct zynq_spi_softc *sc = cookie;
	struct spi_chunk *chunk;
	size_t total = 0;
	u_int budget = ZSPI_POLL_BUDGET_US;
	uint32_t config;
	uint8_t value;
	int error = 0;

	for (chunk = st->st_chunks; chunk != NULL; chunk = chunk->chunk_next) {
		if (chunk->chunk_count < 0 ||
		    (size_t)chunk->chunk_count > ZSPI_MAX_TRANSFER - total) {
			error = EINVAL;
			goto done;
		}
		total += chunk->chunk_count;
	}
	if (st->st_slave < 0 || st->st_slave >= ZSPI_NSLAVES) {
		error = EINVAL;
		goto done;
	}
	if (total == 0)
		goto done;
	zynq_spi_write(sc, ZSPI_ENABLE, 0);
	error = zynq_spi_drain(sc);
	if (error != 0)
		goto stop;
	zynq_spi_write(sc, ZSPI_STATUS, ~0U);
	config = sc->sc_config & ~ZSPI_CFG_CS;
	config |= __SHIFTIN(0xf ^ __BIT(st->st_slave), ZSPI_CFG_CS);
	zynq_spi_write(sc, ZSPI_CONFIG, config);
	zynq_spi_write(sc, ZSPI_ENABLE, 1);

	/* Keep chip select asserted across command and response chunks. */
	for (chunk = st->st_chunks; chunk != NULL; chunk = chunk->chunk_next) {
		while (chunk->chunk_wresid > 0) {
			error = zynq_spi_wait(sc, ZSPI_TX_NOT_FULL, &budget);
			if (error != 0)
				goto stop;
			value = chunk->chunk_wptr ? *chunk->chunk_wptr++ : 0;
			zynq_spi_write(sc, ZSPI_TX_DATA, value);
			chunk->chunk_wresid--;
			error = zynq_spi_wait(sc, ZSPI_RX_NOT_EMPTY, &budget);
			if (error != 0)
				goto stop;
			value = zynq_spi_read(sc, ZSPI_RX_DATA);
			if (chunk->chunk_rptr != NULL)
				*chunk->chunk_rptr++ = value;
			chunk->chunk_rresid--;
		}
	}
stop:
	zynq_spi_write(sc, ZSPI_ENABLE, 0);
	zynq_spi_write(sc, ZSPI_CONFIG, sc->sc_config | ZSPI_CFG_CS);
	zynq_spi_write(sc, ZSPI_INT_DISABLE, ~0U);
	if (zynq_spi_drain(sc) != 0 && error == 0)
		error = EIO;
	zynq_spi_write(sc, ZSPI_STATUS, ~0U);
done:
	spi_done(st, error);
	return 0;
}
