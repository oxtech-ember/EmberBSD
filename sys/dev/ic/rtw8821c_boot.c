/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: FreeBSD a143197801c680b958ab662c88cf086668a1958b rtw88/efuse.c. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <dev/ic/rtw88var.h>
#include <dev/ic/rtw8821c.h>
#include <dev/ic/rtw8821c_boot.h>

#define SYS_CFG1 0x00f0
#define EFUSE_CTRL 0x0030
#define EFUSE_ACCESS 0x00cf
#define EFUSE_FLAG UINT32_C(0x80000000)

static int
rtw8821c_cancelled(const struct rtw88_softc *sc, uint32_t generation)
{
	return sc->sc_stop_requested || sc->sc_generation != generation;
}

int
rtw8821c_identify(struct rtw88_softc *sc)
{
	struct rtw8821c_identity id;
	uint32_t generation, cfg;
	int error;

	if (sc == NULL || sc->sc_ops == NULL || sc->sc_ops->read_4 == NULL)
		return EINVAL;
	generation = sc->sc_generation;
	if (rtw8821c_cancelled(sc, generation))
		return EINTR;
	error = sc->sc_ops->read_4(sc->sc_bus, SYS_CFG1, &cfg);
	if (error != 0)
		return error;
	if (rtw8821c_cancelled(sc, generation))
		return EINTR;
	id = (struct rtw8821c_identity){ sc->sc_vendor, sc->sc_product, cfg };
	error = rtw8821c_validate_identity(&id);
	if (error != 0)
		return error;
	/* The 8821C runtime implements only the donor's 1T1R path. */
	if ((cfg & UINT32_C(0x08000000)) != 0)
		return ENOTSUP;
	sc->sc_sys_cfg1 = cfg;
	sc->sc_cut = (uint8_t)((cfg >> 12) & 0x0f);
	return 0;
}

int
rtw8821c_read_efuse(struct rtw88_softc *sc)
{
	struct rtw8821c_efuse map;
	uint8_t physical[RTW8821C_EFUSE_PHYSICAL_SIZE];
	uint32_t generation, ctrl, i;
	unsigned poll;
	int error, close_error;

	if (sc == NULL || sc->sc_ops == NULL || sc->sc_ops->read_4 == NULL ||
	    sc->sc_ops->write_4 == NULL || sc->sc_ops->write_1 == NULL ||
	    sc->sc_ops->delay_us == NULL)
		return EINVAL;
	generation = sc->sc_generation;
	if (rtw8821c_cancelled(sc, generation))
		return EINTR;
	error = sc->sc_ops->write_1(sc->sc_bus, EFUSE_ACCESS, 0x69);
	if (error != 0) {
		(void)sc->sc_ops->write_1(sc->sc_bus, EFUSE_ACCESS, 0);
		return error;
	}
	for (i = 0; i < sizeof(physical); i++) {
		if (rtw8821c_cancelled(sc, generation)) {
			error = EINTR;
			break;
		}
		error = sc->sc_ops->read_4(sc->sc_bus, EFUSE_CTRL, &ctrl);
		if (error != 0)
			break;
		ctrl = (ctrl & ~(UINT32_C(0x3ffff) | EFUSE_FLAG)) | (i << 8);
		error = sc->sc_ops->write_4(sc->sc_bus, EFUSE_CTRL, ctrl);
		if (error != 0)
			break;
		for (poll = 0; poll < 1000; poll++) {
			if (rtw8821c_cancelled(sc, generation)) {
				error = EINTR;
				break;
			}
			error = sc->sc_ops->read_4(sc->sc_bus, EFUSE_CTRL, &ctrl);
			if (error != 0 || (ctrl & EFUSE_FLAG) != 0)
				break;
			sc->sc_ops->delay_us(sc->sc_bus, 1);
		}
		if (error != 0)
			break;
		if (poll == 1000) {
			error = ETIMEDOUT;
			break;
		}
		physical[i] = (uint8_t)ctrl;
	}
	close_error = sc->sc_ops->write_1(sc->sc_bus, EFUSE_ACCESS, 0);
	if (error != 0)
		return error;
	if (close_error != 0)
		return close_error;
	if (rtw8821c_cancelled(sc, generation))
		return EINTR;
	error = rtw8821c_efuse_decode(physical, sizeof(physical), &map);
	if (error != 0)
		return error;
	memcpy(sc->sc_efuse_phys, physical, sizeof(physical));
	memcpy(sc->sc_mac, map.mac, sizeof(map.mac));
	sc->sc_rfe = map.rfe_option;
	/* Donor chip setup uses zero for an unprogrammed crystal cap. */
	sc->sc_xtal = map.xtal == 0xff ? 0 : map.xtal & 0x7f;
	return 0;
}
