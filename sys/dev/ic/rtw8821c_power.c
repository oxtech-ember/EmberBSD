/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: FreeBSD a143197801c680b958ab662c88cf086668a1958b rtw88. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <dev/ic/rtw88var.h>
#include <dev/ic/rtw8821c_boot.h>
#define BIT(n) (1U << (n))
#define RTW_PWR_CUT_ALL_MSK 0xff
#define RTW_PWR_INTF_SDIO_MSK BIT(0)
#define RTW_PWR_INTF_USB_MSK BIT(1)
#define RTW_PWR_INTF_PCI_MSK BIT(2)
#define RTW_PWR_INTF_ALL_MSK 0x0f
#define RTW_PWR_ADDR_MAC 0
#define RTW_PWR_ADDR_SDIO 3
#define RTW_PWR_CMD_WRITE 1
#define RTW_PWR_CMD_POLLING 2
#define RTW_PWR_CMD_DELAY 3
#define RTW_PWR_CMD_END 4
#define RTW_PWR_DELAY_US 0
#define RTW_PWR_DELAY_MS 1
struct power_step { uint16_t offset; uint8_t cut_mask, intf_mask, base, cmd, mask, value; };
static const struct power_step trans_carddis_to_cardemu_8821c[] = {
	{0x0086,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_SDIO,
	 RTW_PWR_CMD_WRITE, BIT(0), 0},
	{0x0086,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_SDIO,
	 RTW_PWR_CMD_POLLING, BIT(1), BIT(1)},
	{0x004A,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), 0},
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(3) | BIT(4) | BIT(7), 0},
	{0x0300,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_PCI_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0},
	{0x0301,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_PCI_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0},
	{0xFFFF,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 0,
	 RTW_PWR_CMD_END, 0, 0},
};

static const struct power_step trans_cardemu_to_act_8821c[] = {
	{0x0020,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK | RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), BIT(0)},
	{0x0001,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK | RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_DELAY, 1, RTW_PWR_DELAY_MS},
	{0x0000,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK | RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(5), 0},
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, (BIT(4) | BIT(3) | BIT(2)), 0},
	{0x0075,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_PCI_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), BIT(0)},
	{0x0006,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_POLLING, BIT(1), BIT(1)},
	{0x0075,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_PCI_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), 0},
	{0x0006,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), BIT(0)},
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(7), 0},
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, (BIT(4) | BIT(3)), 0},
	{0x10C3,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), BIT(0)},
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), BIT(0)},
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_POLLING, BIT(0), 0},
	{0x0020,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(3), BIT(3)},
	{0x0074,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_PCI_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(5), BIT(5)},
	{0x0022,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_PCI_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(1), 0},
	{0x0062,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_PCI_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, (BIT(7) | BIT(6) | BIT(5)),
	 (BIT(7) | BIT(6) | BIT(5))},
	{0x0061,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_PCI_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, (BIT(7) | BIT(6) | BIT(5)), 0},
	{0x007C,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(1), 0},
	{0xFFFF,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 0,
	 RTW_PWR_CMD_END, 0, 0},
};

static const struct power_step trans_act_to_cardemu_8821c[] = {
	{0x0093,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(3), 0},
	{0x001F,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0},
	{0x0049,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(1), 0},
	{0x0006,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), BIT(0)},
	{0x0002,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(1), 0},
	{0x10C3,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), 0},
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(1), BIT(1)},
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_POLLING, BIT(1), 0},
	{0x0020,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(3), 0},
	{0x0000,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK | RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(5), BIT(5)},
	{0xFFFF,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 0,
	 RTW_PWR_CMD_END, 0, 0},
};

static const struct power_step trans_cardemu_to_carddis_8821c[] = {
	{0x0007,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK | RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0x20},
	{0x0067,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(5), 0},
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_PCI_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(2), BIT(2)},
	{0x004A,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), 0},
	{0x0067,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(5), 0},
	{0x0067,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(4), 0},
	{0x004F,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), 0},
	{0x0067,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(1), 0},
	{0x0046,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(6), BIT(6)},
	{0x0067,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(2), 0},
	{0x0046,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(7), BIT(7)},
	{0x0062,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(4), BIT(4)},
	{0x0081,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(7) | BIT(6), 0},
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK | RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(3) | BIT(4), BIT(3)},
	{0x0086,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_SDIO,
	 RTW_PWR_CMD_WRITE, BIT(0), BIT(0)},
	{0x0086,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_SDIO,
	 RTW_PWR_CMD_POLLING, BIT(1), 0},
	{0x0090,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK | RTW_PWR_INTF_PCI_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(1), 0},
	{0x0044,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_SDIO,
	 RTW_PWR_CMD_WRITE, 0xFF, 0},
	{0x0040,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_SDIO,
	 RTW_PWR_CMD_WRITE, 0xFF, 0x90},
	{0x0041,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_SDIO,
	 RTW_PWR_CMD_WRITE, 0xFF, 0x00},
	{0x0042,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_SDIO,
	 RTW_PWR_CMD_WRITE, 0xFF, 0x04},
	{0xFFFF,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 0,
	 RTW_PWR_CMD_END, 0, 0},
};

static const struct power_step * const card_enable_flow_8821c[] = {
	trans_carddis_to_cardemu_8821c,
	trans_cardemu_to_act_8821c,
	NULL
};

static const struct power_step * const card_disable_flow_8821c[] = {
	trans_act_to_cardemu_8821c,
	trans_cardemu_to_carddis_8821c,
	NULL
};


static int
power_step_run(struct rtw88_softc *sc, const struct power_step *step,
    uint32_t generation, int shutdown)
{
	unsigned n;
	uint8_t value;

	if (!shutdown && (sc->sc_stop_requested || sc->sc_generation != generation))
		return EINTR;
	if (step->cmd == RTW_PWR_CMD_DELAY) {
		sc->sc_ops->delay_us(sc->sc_bus, step->value == RTW_PWR_DELAY_MS ?
		    (unsigned)step->offset * 1000U : step->offset);
		return 0;
	}
	if (step->base != RTW_PWR_ADDR_MAC)
		return EINVAL;
	for (n = 0; n < 20000; n++) {
		int error;

		if (!shutdown && (sc->sc_stop_requested || sc->sc_generation != generation))
			return EINTR;
		error = sc->sc_ops->read_1(sc->sc_bus, step->offset, &value);
		if (error != 0)
			return error;
		if (!shutdown && (sc->sc_stop_requested || sc->sc_generation != generation))
			return EINTR;
		if (step->cmd == RTW_PWR_CMD_WRITE)
			return sc->sc_ops->write_1(sc->sc_bus, step->offset,
			    (uint8_t)((value & ~step->mask) | (step->value & step->mask)));
		if (step->cmd != RTW_PWR_CMD_POLLING)
			return EINVAL;
		if ((value & step->mask) == (step->value & step->mask))
			return 0;
		sc->sc_ops->delay_us(sc->sc_bus, 50);
	}
	return ETIMEDOUT;
}

int
rtw8821c_power(struct rtw88_softc *sc, int on)
{
	const struct power_step * const *flow;
	const struct power_step *step;
	uint32_t generation;
	int error;

	if (sc == NULL || sc->sc_ops == NULL || sc->sc_ops->read_1 == NULL ||
	    sc->sc_ops->write_1 == NULL || sc->sc_ops->delay_us == NULL ||
	    (on != 0 && on != 1) || sc->sc_cut > 6)
		return EINVAL;
	generation = sc->sc_generation;
	flow = on ? card_enable_flow_8821c : card_disable_flow_8821c;
	for (; *flow != NULL; flow++) {
		for (step = *flow; step->cmd != RTW_PWR_CMD_END; step++) {
			if ((step->intf_mask & RTW_PWR_INTF_PCI_MSK) == 0 ||
			    (step->cut_mask & (1U << (sc->sc_cut + 1))) == 0)
				continue;
			error = power_step_run(sc, step, generation, !on);
			if (error != 0)
				return error;
		}
	}
	return !on || (!sc->sc_stop_requested && sc->sc_generation == generation) ? 0 : EINTR;
}

/* Shared WCPU3081 PCIe system configuration, donor mac.c. */
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
rtw8821c_power_pre(struct rtw88_softc *sc)
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
rtw8821c_power_post(struct rtw88_softc *sc)
{
	struct power_io io;
	uint32_t value;
	int error;

	if ((error = validate(sc)) != 0)
		return error;
	io = (struct power_io){ sc, sc->sc_generation, 0 };
	/* __rtw_mac_init_system_cfg; rtw8821c_hw_spec.sys_func_en=0xd8. */
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
