/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: FreeBSD a143197801c680b958ab662c88cf086668a1958b rtw88 mac.c. */
/*
 * PCIe/3081 system setup around the RTL8822C power table. The caller runs
 * pre, the existing chip power-on table, then post before firmware download.
 * These functions do not implement RPWM wakeup, power-state detection or
 * the reference's already-powered off/on retry. The caller serializes
 * startup and stop, and must power off after any error. No failed read
 * can feed a computed write. Stop and generation changes cancel the slice.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <dev/ic/rtw88var.h>
#include <dev/ic/rtw8822c.h>

struct power_io {
	struct rtw88_softc *sc;
	uint32_t generation;
	int error;
};

static int
check(struct power_io *io)
{

	if (io->error == 0 && (io->sc->sc_stop_requested ||
	    io->sc->sc_generation != io->generation))
		io->error = EINTR;
	return io->error;
}

static uint32_t
read_reg(struct power_io *io, uint32_t address, unsigned width)
{
	uint32_t v32 = 0;
	uint8_t v8 = 0;

	if (check(io) != 0)
		return 0;
	if (width == 1)
		io->error = io->sc->sc_ops->read_1(io->sc->sc_bus, address, &v8);
	else
		io->error = io->sc->sc_ops->read_4(io->sc->sc_bus, address, &v32);
	(void)check(io);
	return width == 1 ? v8 : v32;
}

static void
write_reg(struct power_io *io, uint32_t address, unsigned width, uint32_t value)
{

	if (check(io) != 0)
		return;
	if (width == 1)
		io->error = io->sc->sc_ops->write_1(io->sc->sc_bus, address,
		    (uint8_t)value);
	else
		io->error = io->sc->sc_ops->write_4(io->sc->sc_bus, address, value);
	(void)check(io);
}

static void
modify(struct power_io *io, uint32_t address, unsigned width,
    uint32_t clear, uint32_t set)
{
	uint32_t value = read_reg(io, address, width);

	write_reg(io, address, width, (value & ~clear) | set);
}

static int
validate(const struct rtw88_softc *sc)
{

	if (sc == NULL || sc->sc_ops == NULL ||
	    sc->sc_ops->read_1 == NULL || sc->sc_ops->read_4 == NULL ||
	    sc->sc_ops->write_1 == NULL || sc->sc_ops->write_4 == NULL)
		return EINVAL;
	if (sc->sc_started)
		return EBUSY;
	if (!sc->sc_resources_ready || !sc->sc_bootstrapping)
		return ENXIO;
	return 0;
}

int
rtw8822c_power_pre(struct rtw88_softc *sc)
{
	struct power_io io;
	int error;

	if ((error = validate(sc)) != 0)
		return error;
	io = (struct power_io){ sc, sc->sc_generation, 0 };
	/* rtw_mac_pre_system_cfg: PCIe, WCPU3081. */
	write_reg(&io, 0x001c, 1, 0);
	modify(&io, 0x0074, 4, 0, 0x100); /* HCI_OPT_CTRL: USB_SUS_DIS */
	modify(&io, 0x0064, 4, 0, 0x30000000); /* PAD_CTRL1: WLBT PA/LNA */
	modify(&io, 0x004c, 4, 0x06000000, 0); /* LED_CFG: PA/LNA selection */
	modify(&io, 0x0040, 4, 0, 0x04); /* GPIO_MUXCFG: WLRFE4/5 */
	modify(&io, 0x0002, 1, 0x03, 0); /* SYS_FUNC_EN: BB reset */
	modify(&io, 0x001f, 1, 0x07, 0); /* RF_CTRL: RF reset/enable */
	modify(&io, 0x00ec, 4, 0x07000000, 0); /* WLRF1: BBRF_EN */
	return io.error;
}

int
rtw8822c_power_post(struct rtw88_softc *sc)
{
	struct power_io io;
	uint32_t value;
	int error;

	if ((error = validate(sc)) != 0)
		return error;
	io = (struct power_io){ sc, sc->sc_generation, 0 };
	/* __rtw_mac_init_system_cfg; rtw8822c_hw_spec.sys_func_en=0xd8. */
	modify(&io, 0x1080, 4, 0, 0x00010100); /* platform reset/DDMA enable */
	modify(&io, 0x0003, 1, 0, 0xd8);
	modify(&io, 0x1103, 1, 0x0f, 0x0c); /* CR_EXT + 3 */
	value = read_reg(&io, 0x0080, 4);
	if ((value & 0x00100000) != 0) { /* MCUFW_CTRL: BOOT_FSPI_EN */
		write_reg(&io, 0x0080, 4, value & ~UINT32_C(0x00100000));
		modify(&io, 0x0040, 4, 0x00080000, 0); /* GPIO_MUXCFG: FSPI_EN */
	}
	return io.error;
}
