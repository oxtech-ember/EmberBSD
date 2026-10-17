/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: FreeBSD a143197801c680b958ab662c88cf086668a1958b rtw88. */

#include <sys/types.h>
#ifdef _KERNEL
#include <sys/errno.h>
#include <sys/systm.h>
#else
#include <errno.h>
#include <stdio.h>
#include <string.h>
#endif

#include <dev/ic/rtw88var.h>
#include "rtw8822c.h"
#include "rtw8822c_table.h"

#define RTW8822C_REG_SYS_CFG1		0x00f0
#define RTW8822C_REG_EFUSE_CTRL		0x0030
#define RTW8822C_REG_EFUSE_ACCESS	0x00cf
#define RTW8822C_EFUSE_ACCESS_ON	0x69
#define RTW8822C_BIT_EF_FLAG		UINT32_C(0x80000000)
#define RTW8822C_RFREG_MASK		0xfffff
#define RTW8822C_PWR_POLL_MAX		20000U
#define RTW8822C_PWR_PCI		0x04
#define RTW8822C_PWR_ALL		0x0f
#define RTW8822C_PWR_CUT_ALL		0xff
#define RTW8822C_PWR_CMD_READ		0
#define RTW8822C_PWR_CMD_WRITE		1
#define RTW8822C_PWR_CMD_POLL		2
#define RTW8822C_PWR_CMD_DELAY		3
#define RTW8822C_PWR_CMD_END		4
#define RTW8822C_PWR_ADDR_MAC		0
#define RTW8822C_BIT(n)			(1U << (n))

struct rtw8822c_pwr_cmd {
	uint16_t offset;
	uint8_t cut_mask;
	uint8_t intf_mask;
	uint8_t base;
	uint8_t cmd;
	uint8_t mask;
	uint8_t value;
};

static const uint32_t rtw8822c_rf_base[2] = { 0x3c00, 0x4c00 };
/* rtw8822c_hw_spec rf_sipi_addr: serial port for RF register 0x00. */
static const uint32_t rtw8822c_rf_sipi[2] = { 0x1808, 0x4108 };

static const struct rtw8822c_pwr_cmd trans_carddis_to_cardemu_8822c[] = {
	{ 0x002e, RTW8822C_PWR_CUT_ALL, RTW8822C_PWR_ALL,
	    RTW8822C_PWR_ADDR_MAC, RTW8822C_PWR_CMD_WRITE,
	    RTW8822C_BIT(2), RTW8822C_BIT(2) },
	{ 0x002d, RTW8822C_PWR_CUT_ALL, RTW8822C_PWR_ALL,
	    RTW8822C_PWR_ADDR_MAC, RTW8822C_PWR_CMD_WRITE, RTW8822C_BIT(0), 0 },
	{ 0x007f, RTW8822C_PWR_CUT_ALL, RTW8822C_PWR_ALL,
	    RTW8822C_PWR_ADDR_MAC, RTW8822C_PWR_CMD_WRITE, RTW8822C_BIT(7), 0 },
	{ 0x0005, RTW8822C_PWR_CUT_ALL, RTW8822C_PWR_ALL,
	    RTW8822C_PWR_ADDR_MAC, RTW8822C_PWR_CMD_WRITE,
	    RTW8822C_BIT(3) | RTW8822C_BIT(4) | RTW8822C_BIT(7), 0 },
	{ 0xffff, RTW8822C_PWR_CUT_ALL, RTW8822C_PWR_ALL, 0,
	    RTW8822C_PWR_CMD_END, 0, 0 }
};

static const struct rtw8822c_pwr_cmd trans_cardemu_to_act_8822c[] = {
	{ 0x0005, RTW8822C_PWR_CUT_ALL, RTW8822C_PWR_ALL,
	    RTW8822C_PWR_ADDR_MAC, RTW8822C_PWR_CMD_WRITE,
	    RTW8822C_BIT(4) | RTW8822C_BIT(3) | RTW8822C_BIT(2), 0 },
	{ 0x0075, RTW8822C_PWR_CUT_ALL, RTW8822C_PWR_PCI,
	    RTW8822C_PWR_ADDR_MAC, RTW8822C_PWR_CMD_WRITE,
	    RTW8822C_BIT(0), RTW8822C_BIT(0) },
	{ 0x0006, RTW8822C_PWR_CUT_ALL, RTW8822C_PWR_ALL,
	    RTW8822C_PWR_ADDR_MAC, RTW8822C_PWR_CMD_POLL,
	    RTW8822C_BIT(1), RTW8822C_BIT(1) },
	{ 0x0075, RTW8822C_PWR_CUT_ALL, RTW8822C_PWR_PCI,
	    RTW8822C_PWR_ADDR_MAC, RTW8822C_PWR_CMD_WRITE, RTW8822C_BIT(0), 0 },
	{ 0x002e, RTW8822C_PWR_CUT_ALL, RTW8822C_PWR_ALL,
	    RTW8822C_PWR_ADDR_MAC, RTW8822C_PWR_CMD_WRITE, RTW8822C_BIT(3), 0 },
	{ 0x0006, RTW8822C_PWR_CUT_ALL, RTW8822C_PWR_ALL,
	    RTW8822C_PWR_ADDR_MAC, RTW8822C_PWR_CMD_WRITE,
	    RTW8822C_BIT(0), RTW8822C_BIT(0) },
	{ 0x0005, RTW8822C_PWR_CUT_ALL, RTW8822C_PWR_ALL,
	    RTW8822C_PWR_ADDR_MAC, RTW8822C_PWR_CMD_WRITE,
	    RTW8822C_BIT(4) | RTW8822C_BIT(3), 0 },
	{ 0x1018, RTW8822C_PWR_CUT_ALL, RTW8822C_PWR_ALL,
	    RTW8822C_PWR_ADDR_MAC, RTW8822C_PWR_CMD_WRITE,
	    RTW8822C_BIT(2), RTW8822C_BIT(2) },
	{ 0x0005, RTW8822C_PWR_CUT_ALL, RTW8822C_PWR_ALL,
	    RTW8822C_PWR_ADDR_MAC, RTW8822C_PWR_CMD_WRITE,
	    RTW8822C_BIT(0), RTW8822C_BIT(0) },
	{ 0x0005, RTW8822C_PWR_CUT_ALL, RTW8822C_PWR_ALL,
	    RTW8822C_PWR_ADDR_MAC, RTW8822C_PWR_CMD_POLL, RTW8822C_BIT(0), 0 },
	{ 0x0074, RTW8822C_PWR_CUT_ALL, RTW8822C_PWR_PCI,
	    RTW8822C_PWR_ADDR_MAC, RTW8822C_PWR_CMD_WRITE,
	    RTW8822C_BIT(5), RTW8822C_BIT(5) },
	{ 0x0071, RTW8822C_PWR_CUT_ALL, RTW8822C_PWR_PCI,
	    RTW8822C_PWR_ADDR_MAC, RTW8822C_PWR_CMD_WRITE, RTW8822C_BIT(4), 0 },
	{ 0x0062, RTW8822C_PWR_CUT_ALL, RTW8822C_PWR_PCI,
	    RTW8822C_PWR_ADDR_MAC, RTW8822C_PWR_CMD_WRITE,
	    RTW8822C_BIT(7) | RTW8822C_BIT(6) | RTW8822C_BIT(5),
	    RTW8822C_BIT(7) | RTW8822C_BIT(6) | RTW8822C_BIT(5) },
	{ 0x0061, RTW8822C_PWR_CUT_ALL, RTW8822C_PWR_PCI,
	    RTW8822C_PWR_ADDR_MAC, RTW8822C_PWR_CMD_WRITE,
	    RTW8822C_BIT(7) | RTW8822C_BIT(6) | RTW8822C_BIT(5), 0 },
	{ 0x001f, RTW8822C_PWR_CUT_ALL, RTW8822C_PWR_ALL,
	    RTW8822C_PWR_ADDR_MAC, RTW8822C_PWR_CMD_WRITE,
	    RTW8822C_BIT(7) | RTW8822C_BIT(6), RTW8822C_BIT(7) },
	{ 0x00ef, RTW8822C_PWR_CUT_ALL, RTW8822C_PWR_ALL,
	    RTW8822C_PWR_ADDR_MAC, RTW8822C_PWR_CMD_WRITE,
	    RTW8822C_BIT(7) | RTW8822C_BIT(6), RTW8822C_BIT(7) },
	{ 0x1045, RTW8822C_PWR_CUT_ALL, RTW8822C_PWR_ALL,
	    RTW8822C_PWR_ADDR_MAC, RTW8822C_PWR_CMD_WRITE,
	    RTW8822C_BIT(4), RTW8822C_BIT(4) },
	{ 0x0010, RTW8822C_PWR_CUT_ALL, RTW8822C_PWR_ALL,
	    RTW8822C_PWR_ADDR_MAC, RTW8822C_PWR_CMD_WRITE,
	    RTW8822C_BIT(2), RTW8822C_BIT(2) },
	{ 0x1064, RTW8822C_PWR_CUT_ALL, RTW8822C_PWR_ALL,
	    RTW8822C_PWR_ADDR_MAC, RTW8822C_PWR_CMD_WRITE,
	    RTW8822C_BIT(1), RTW8822C_BIT(1) },
	{ 0xffff, RTW8822C_PWR_CUT_ALL, RTW8822C_PWR_ALL, 0,
	    RTW8822C_PWR_CMD_END, 0, 0 }
};

static const struct rtw8822c_pwr_cmd trans_act_to_cardemu_8822c[] = {
	{ 0x0093, RTW8822C_PWR_CUT_ALL, RTW8822C_PWR_ALL,
	    RTW8822C_PWR_ADDR_MAC, RTW8822C_PWR_CMD_WRITE, RTW8822C_BIT(3), 0 },
	{ 0x001f, RTW8822C_PWR_CUT_ALL, RTW8822C_PWR_ALL,
	    RTW8822C_PWR_ADDR_MAC, RTW8822C_PWR_CMD_WRITE, 0xff, 0 },
	{ 0x00ef, RTW8822C_PWR_CUT_ALL, RTW8822C_PWR_ALL,
	    RTW8822C_PWR_ADDR_MAC, RTW8822C_PWR_CMD_WRITE, 0xff, 0 },
	{ 0x1045, RTW8822C_PWR_CUT_ALL, RTW8822C_PWR_ALL,
	    RTW8822C_PWR_ADDR_MAC, RTW8822C_PWR_CMD_WRITE, RTW8822C_BIT(4), 0 },
	{ 0x0049, RTW8822C_PWR_CUT_ALL, RTW8822C_PWR_ALL,
	    RTW8822C_PWR_ADDR_MAC, RTW8822C_PWR_CMD_WRITE, RTW8822C_BIT(1), 0 },
	{ 0x0006, RTW8822C_PWR_CUT_ALL, RTW8822C_PWR_ALL,
	    RTW8822C_PWR_ADDR_MAC, RTW8822C_PWR_CMD_WRITE,
	    RTW8822C_BIT(0), RTW8822C_BIT(0) },
	{ 0x0002, RTW8822C_PWR_CUT_ALL, RTW8822C_PWR_ALL,
	    RTW8822C_PWR_ADDR_MAC, RTW8822C_PWR_CMD_WRITE, RTW8822C_BIT(1), 0 },
	{ 0x0005, RTW8822C_PWR_CUT_ALL, RTW8822C_PWR_ALL,
	    RTW8822C_PWR_ADDR_MAC, RTW8822C_PWR_CMD_WRITE,
	    RTW8822C_BIT(1), RTW8822C_BIT(1) },
	{ 0x0005, RTW8822C_PWR_CUT_ALL, RTW8822C_PWR_ALL,
	    RTW8822C_PWR_ADDR_MAC, RTW8822C_PWR_CMD_POLL, RTW8822C_BIT(1), 0 },
	{ 0xffff, RTW8822C_PWR_CUT_ALL, RTW8822C_PWR_ALL, 0,
	    RTW8822C_PWR_CMD_END, 0, 0 }
};

static const struct rtw8822c_pwr_cmd trans_cardemu_to_carddis_8822c[] = {
	{ 0x0067, RTW8822C_PWR_CUT_ALL, RTW8822C_PWR_ALL,
	    RTW8822C_PWR_ADDR_MAC, RTW8822C_PWR_CMD_WRITE, RTW8822C_BIT(5), 0 },
	{ 0x0081, RTW8822C_PWR_CUT_ALL, RTW8822C_PWR_ALL,
	    RTW8822C_PWR_ADDR_MAC, RTW8822C_PWR_CMD_WRITE,
	    RTW8822C_BIT(7) | RTW8822C_BIT(6), 0 },
	{ 0x0090, RTW8822C_PWR_CUT_ALL, RTW8822C_PWR_ALL,
	    RTW8822C_PWR_ADDR_MAC, RTW8822C_PWR_CMD_WRITE, RTW8822C_BIT(1), 0 },
	{ 0x0092, RTW8822C_PWR_CUT_ALL, RTW8822C_PWR_PCI,
	    RTW8822C_PWR_ADDR_MAC, RTW8822C_PWR_CMD_WRITE, 0xff, 0x20 },
	{ 0x0093, RTW8822C_PWR_CUT_ALL, RTW8822C_PWR_PCI,
	    RTW8822C_PWR_ADDR_MAC, RTW8822C_PWR_CMD_WRITE, 0xff, 0x04 },
	{ 0x0005, RTW8822C_PWR_CUT_ALL, RTW8822C_PWR_PCI,
	    RTW8822C_PWR_ADDR_MAC, RTW8822C_PWR_CMD_WRITE,
	    RTW8822C_BIT(2), RTW8822C_BIT(2) },
	{ 0xffff, RTW8822C_PWR_CUT_ALL, RTW8822C_PWR_ALL, 0,
	    RTW8822C_PWR_CMD_END, 0, 0 }
};

static const struct rtw8822c_pwr_cmd * const card_enable_flow_8822c[] = {
	trans_carddis_to_cardemu_8822c,
	trans_cardemu_to_act_8822c,
	NULL
};

static const struct rtw8822c_pwr_cmd * const card_disable_flow_8822c[] = {
	trans_act_to_cardemu_8822c,
	trans_cardemu_to_carddis_8822c,
	NULL
};

static int
rtw8822c_write32_mask(struct rtw88_softc *sc, uint32_t addr, uint32_t mask,
    uint32_t data)
{
	uint32_t old, val;
	unsigned shift;
	int error;

	if (mask != 0xffffffffU) {
		error = sc->sc_ops->read_4(sc->sc_bus, addr, &old);
		if (error != 0)
			return error;
		shift = 0;
		while (shift < 32 && (mask & (UINT32_C(1) << shift)) == 0)
			shift++;
		if (shift == 32)
			return EINVAL;
		val = (old & ~mask) | ((data << shift) & mask);
	} else
		val = data;
	return sc->sc_ops->write_4(sc->sc_bus, addr, val);
}

static int
rtw8822c_poll_bit(struct rtw88_softc *sc, uint32_t addr, uint8_t mask,
    uint8_t value)
{
	unsigned n;
	uint8_t v;
	int error;

	for (n = 0; n < RTW8822C_PWR_POLL_MAX; n++) {
		if (sc->sc_stop_requested)
			return EINTR;
		error = sc->sc_ops->read_1(sc->sc_bus, addr, &v);
		if (error != 0)
			return error;
		if ((v & mask) == value)
			return 0;
		sc->sc_ops->delay_us(sc->sc_bus, 50);
	}
	printf("rtwce: power poll timeout reg %#x mask %#x last %#x\n",
	    addr, mask, v);
	return ETIMEDOUT;
}

static int
rtw8822c_run_pwr(struct rtw88_softc *sc, const struct rtw8822c_pwr_cmd *cmd)
{
	uint8_t cut_mask = (uint8_t)(1U << (sc->sc_cut + 1));
	uint8_t v;
	int error;

	for (; cmd->cmd != RTW8822C_PWR_CMD_END; cmd++) {
		if ((cmd->intf_mask & RTW8822C_PWR_PCI) == 0 ||
		    (cmd->cut_mask & cut_mask) == 0)
			continue;
		switch (cmd->cmd) {
		case RTW8822C_PWR_CMD_READ:
			break;
		case RTW8822C_PWR_CMD_WRITE:
			error = sc->sc_ops->read_1(sc->sc_bus, cmd->offset, &v);
			if (error != 0)
				return error;
			v = (uint8_t)((v & ~cmd->mask) |
			    (cmd->value & cmd->mask));
			error = sc->sc_ops->write_1(sc->sc_bus, cmd->offset, v);
			if (error != 0)
				return error;
			break;
		case RTW8822C_PWR_CMD_POLL:
			error = rtw8822c_poll_bit(sc, cmd->offset, cmd->mask,
			    cmd->value);
			if (error != 0)
				return error;
			break;
		case RTW8822C_PWR_CMD_DELAY:
			sc->sc_ops->delay_us(sc->sc_bus,
			    cmd->value == 0 ? cmd->offset :
			    (unsigned)cmd->offset * 1000U);
			break;
		default:
			return EINVAL;
		}
	}
	return 0;
}

int
rtw8822c_power(struct rtw88_softc *sc, int on)
{
	const struct rtw8822c_pwr_cmd * const *flow;
	int error;

	flow = on ? card_enable_flow_8822c : card_disable_flow_8822c;
	for (; *flow != NULL; flow++) {
		error = rtw8822c_run_pwr(sc, *flow);
		if (error != 0)
			return error;
	}
	return 0;
}

static int
rtw8822c_efuse_byte(struct rtw88_softc *sc, uint32_t addr, uint8_t *out)
{
	uint32_t ctrl = 0;
	unsigned n;
	int error;

	error = sc->sc_ops->read_4(sc->sc_bus, RTW8822C_REG_EFUSE_CTRL, &ctrl);
	if (error != 0)
		return error;
	ctrl &= ~(UINT32_C(0xff) | (UINT32_C(0x3ff) << 8));
	ctrl |= (addr & 0x3ff) << 8;
	ctrl &= ~RTW8822C_BIT_EF_FLAG;
	error = sc->sc_ops->write_4(sc->sc_bus, RTW8822C_REG_EFUSE_CTRL, ctrl);
	if (error != 0)
		return error;
	for (n = 0; n < 1000; n++) {
		if (sc->sc_stop_requested)
			return EINTR;
		error = sc->sc_ops->read_4(sc->sc_bus, RTW8822C_REG_EFUSE_CTRL,
		    &ctrl);
		if (error != 0)
			return error;
		if (ctrl & RTW8822C_BIT_EF_FLAG) {
			*out = (uint8_t)ctrl;
			return 0;
		}
		sc->sc_ops->delay_us(sc->sc_bus, 1);
	}
	return ETIMEDOUT;
}

int
rtw8822c_identify(struct rtw88_softc *sc)
{
	struct rtw8822c_identity id;
	struct rtw8822c_efuse map;
	uint8_t phy[RTW8822C_EFUSE_PHYSICAL_SIZE];
	uint32_t i;
	int error;

	error = sc->sc_ops->read_4(sc->sc_bus, RTW8822C_REG_SYS_CFG1,
	    &sc->sc_sys_cfg1);
	if (error != 0)
		return error;
	error = sc->sc_ops->write_1(sc->sc_bus, RTW8822C_REG_EFUSE_ACCESS,
	    RTW8822C_EFUSE_ACCESS_ON);
	if (error != 0)
		return error;
	for (i = 0; i < RTW8822C_EFUSE_PHYSICAL_SIZE; i++) {
		error = rtw8822c_efuse_byte(sc, i, &phy[i]);
		if (error != 0) {
			(void)sc->sc_ops->write_1(sc->sc_bus,
			    RTW8822C_REG_EFUSE_ACCESS, 0);
			return error;
		}
	}
	(void)sc->sc_ops->write_1(sc->sc_bus, RTW8822C_REG_EFUSE_ACCESS, 0);
	error = rtw8822c_efuse_decode(phy, sizeof(phy), &map);
	if (error != 0)
		return error;
	sc->sc_rfe = map.rfe_option;
	sc->sc_cut = (uint8_t)((sc->sc_sys_cfg1 >> 12) & 0x0f);
	id.vendor = sc->sc_vendor;
	id.product = sc->sc_product;
	id.sys_cfg1 = sc->sc_sys_cfg1;
	id.rfe_option = sc->sc_rfe;
	return rtw8822c_validate_identity(&id);
}

static int
rtw8822c_phy_matched(struct rtw88_softc *sc, uint32_t cond)
{
	unsigned cut = (cond >> 24) & 0x0f;
	unsigned pkg = (cond >> 12) & 0x0f;
	unsigned intf = (cond >> 8) & 0x0f;
	unsigned rfe = cond & 0xff;
	unsigned drv_cut = sc->sc_cut != 0 ? sc->sc_cut : 15;

	if (cut != 0 && cut != drv_cut)
		return 0;
	if (pkg != 0 && pkg != 15)
		return 0;
	if (intf != 0 && intf != 1)
		return 0;
	if (rfe != sc->sc_rfe)
		return 0;
	return 1;
}

static int
rtw8822c_cfg_mac(struct rtw88_softc *sc, uint32_t addr, uint32_t data)
{

	return sc->sc_ops->write_1(sc->sc_bus, addr, (uint8_t)data);
}

static int
rtw8822c_cfg_agc(struct rtw88_softc *sc, uint32_t addr, uint32_t data)
{

	return sc->sc_ops->write_4(sc->sc_bus, addr, data);
}

static int
rtw8822c_cfg_bb(struct rtw88_softc *sc, uint32_t addr, uint32_t data)
{

	switch (addr) {
	case 0xfe:
		sc->sc_ops->delay_us(sc->sc_bus, 50000);
		return 0;
	case 0xfd:
		sc->sc_ops->delay_us(sc->sc_bus, 5000);
		return 0;
	case 0xfc:
		sc->sc_ops->delay_us(sc->sc_bus, 1000);
		return 0;
	case 0xfb:
		sc->sc_ops->delay_us(sc->sc_bus, 50);
		return 0;
	case 0xfa:
		sc->sc_ops->delay_us(sc->sc_bus, 5);
		return 0;
	case 0xf9:
		sc->sc_ops->delay_us(sc->sc_bus, 1);
		return 0;
	default:
		return sc->sc_ops->write_4(sc->sc_bus, addr, data);
	}
}

static int
rtw8822c_cfg_rf(struct rtw88_softc *sc, int path, uint32_t addr, uint32_t data)
{
	uint32_t direct;

	if (addr == 0xffe) {
		sc->sc_ops->delay_us(sc->sc_bus, 50000);
		return 0;
	}
	if (addr == 0xfe) {
		sc->sc_ops->delay_us(sc->sc_bus, 100);
		return 0;
	}
	if (path < 0 || path > 1)
		return EINVAL;
	addr &= 0xff;
	/* rtw_phy_write_rf_reg_mix: the mode register goes through SIPI. */
	if (addr == 0) {
		int error;

		error = sc->sc_ops->write_4(sc->sc_bus, rtw8822c_rf_sipi[path],
		    (data & RTW8822C_RFREG_MASK) & UINT32_C(0x0fffffff));
		sc->sc_ops->delay_us(sc->sc_bus, 13);
		return error;
	}
	direct = rtw8822c_rf_base[path] + (addr << 2);
	return rtw8822c_write32_mask(sc, direct, RTW8822C_RFREG_MASK, data);
}

static int
rtw8822c_apply_phy_cond(struct rtw88_softc *sc,
    const struct rtw8822c_table *tbl)
{
	const uint32_t *p = tbl->data;
	const uint32_t *end;
	uint32_t pos = 0;
	int matched = 1, skipped = 0, error = 0;

	if (tbl->count % 2 != 0)
		return EINVAL;
	end = p + tbl->count;
	for (; p + 1 < end; p += 2) {
		uint32_t word = p[0];

		if (sc->sc_stop_requested)
			return EINTR;
		if (word & UINT32_C(0x80000000)) {
			unsigned branch = (word >> 28) & 3;

			if (branch == 3) {
				matched = 1;
				skipped = 0;
			} else if (branch == 2)
				matched = skipped ? 0 : 1;
			else
				pos = word;
		} else if (word & UINT32_C(0x40000000)) {
			if (!skipped) {
				if (rtw8822c_phy_matched(sc, pos)) {
					matched = 1;
					skipped = 1;
				} else {
					matched = 0;
					skipped = 0;
				}
			} else
				matched = 0;
		} else if (matched) {
			switch (tbl->kind) {
			case RTW8822C_TBL_MAC:
				error = rtw8822c_cfg_mac(sc, p[0], p[1]);
				break;
			case RTW8822C_TBL_AGC:
				error = rtw8822c_cfg_agc(sc, p[0], p[1]);
				break;
			case RTW8822C_TBL_BB:
				error = rtw8822c_cfg_bb(sc, p[0], p[1]);
				break;
			case RTW8822C_TBL_RF:
				error = rtw8822c_cfg_rf(sc, tbl->rf_path,
				    p[0], p[1]);
				break;
			default:
				return EINVAL;
			}
			if (error != 0)
				return error;
			if (tbl->kind == RTW8822C_TBL_RF)
				sc->sc_ops->delay_us(sc->sc_bus, 1);
		}
	}
	return 0;
}

static int
rtw8822c_apply_pg(struct rtw88_softc *sc, const struct rtw8822c_table *tbl)
{
	const struct rtw_phy_pg_cfg_pair *p = tbl->data;
	uint32_t i;

	for (i = 0; i < tbl->count; i++, p++) {
		if (sc->sc_stop_requested)
			return EINTR;
		if (p->addr == 0xfe || p->addr == 0xffe) {
			sc->sc_ops->delay_us(sc->sc_bus, 50000);
			continue;
		}
		sc->sc_pg_rows++;
	}
	return 0;
}

static int
rtw8822c_apply_pwr(struct rtw88_softc *sc, const struct rtw8822c_table *tbl)
{
	const struct rtw_txpwr_lmt_cfg_pair *p = tbl->data;
	uint32_t i;

	for (i = 0; i < tbl->count; i++, p++) {
		if (sc->sc_stop_requested)
			return EINTR;
		(void)(p->regd | p->band | p->bw | p->rs | p->ch |
		    (uint8_t)p->txpwr_lmt);
		sc->sc_pwr_rows++;
	}
	return 0;
}

static int
rtw8822c_apply_dpk(struct rtw88_softc *sc, const struct rtw8822c_table *tbl)
{
	const uint32_t *p = tbl->data;
	uint32_t i;
	int error;

	if (tbl->count % 3 != 0)
		return EINVAL;
	for (i = 0; i < tbl->count; i += 3) {
		if (sc->sc_stop_requested)
			return EINTR;
		error = rtw8822c_write32_mask(sc, p[i], p[i + 1], p[i + 2]);
		if (error != 0)
			return error;
	}
	return 0;
}

int
rtw8822c_apply_table(struct rtw88_softc *sc, const struct rtw8822c_table *tbl)
{

	if (sc == NULL || tbl == NULL || tbl->data == NULL)
		return EINVAL;
	switch (tbl->kind) {
	case RTW8822C_TBL_MAC:
	case RTW8822C_TBL_AGC:
	case RTW8822C_TBL_BB:
	case RTW8822C_TBL_RF:
		return rtw8822c_apply_phy_cond(sc, tbl);
	case RTW8822C_TBL_BB_PG:
		return rtw8822c_apply_pg(sc, tbl);
	case RTW8822C_TBL_TXPWR_LMT:
		return rtw8822c_apply_pwr(sc, tbl);
	case RTW8822C_TBL_DPK:
		return rtw8822c_apply_dpk(sc, tbl);
	default:
		return EINVAL;
	}
}

int
rtw8822c_load_tables(struct rtw88_softc *sc)
{
	const struct rtw8822c_table *pwr;
	int error;

	error = rtw8822c_apply_table(sc, &rtw8822c_mac_tbl);
	if (error != 0)
		return error;
	error = rtw8822c_apply_table(sc, &rtw8822c_bb_tbl);
	if (error != 0)
		return error;
	error = rtw8822c_apply_table(sc, &rtw8822c_agc_tbl);
	if (error != 0)
		return error;
	error = rtw8822c_write32_mask(sc, 0x1e24, RTW8822C_BIT(17), 1);
	if (error != 0)
		return error;
	error = rtw8822c_write32_mask(sc, 0x1cd0, RTW8822C_BIT(28), 1);
	if (error != 0)
		return error;
	error = rtw8822c_write32_mask(sc, 0x1cd0, RTW8822C_BIT(29), 1);
	if (error != 0)
		return error;
	error = rtw8822c_write32_mask(sc, 0x1cd0, RTW8822C_BIT(30), 1);
	if (error != 0)
		return error;
	error = rtw8822c_write32_mask(sc, 0x1cd0, RTW8822C_BIT(31), 0);
	if (error != 0)
		return error;
	error = rtw8822c_apply_table(sc, &rtw8822c_array_mp_cal_init_tbl);
	if (error != 0)
		return error;
	error = rtw8822c_apply_table(sc, &rtw8822c_rf_b_tbl);
	if (error != 0)
		return error;
	error = rtw8822c_apply_table(sc, &rtw8822c_rf_a_tbl);
	if (error != 0)
		return error;
	error = rtw8822c_apply_table(sc, &rtw8822c_bb_pg_type0_tbl);
	if (error != 0)
		return error;
	pwr = sc->sc_rfe == 5 ? &rtw8822c_txpwr_lmt_type5_tbl :
	    &rtw8822c_txpwr_lmt_type0_tbl;
	error = rtw8822c_apply_table(sc, pwr);
	if (error != 0)
		return error;
	error = rtw8822c_apply_table(sc, &rtw8822c_dpk_mac_bb_tbl);
	if (error != 0)
		return error;
	error = rtw8822c_apply_table(sc, &rtw8822c_dpk_afe_no_dpk_tbl);
	if (error != 0)
		return error;
	return rtw8822c_apply_table(sc, &rtw8822c_dpk_afe_is_dpk_tbl);
}
