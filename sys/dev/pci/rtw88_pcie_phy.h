/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: FreeBSD a143197801c680b958ab662c88cf086668a1958b rtw88/pci.c. */
#ifndef _DEV_PCI_RTW88_PCIE_PHY_H_
#define _DEV_PCI_RTW88_PCIE_PHY_H_

/* PCIe MDIO and DBI are shared transport, not RF-register accesses.
 * Caller serializes against detach and supplies the captured generation. */
static int
rtw88_pcie_current(struct rtw88_softc *sc, uint32_t generation)
{
	return sc->sc_stop_requested || sc->sc_generation != generation ? EINTR : 0;
}

#define RTW88_PCIE_STEP(expr) do { \
	if ((error = rtw88_pcie_current(sc, generation)) != 0) return error; \
	error = (expr); \
	if (error != 0) return error; \
	if ((error = rtw88_pcie_current(sc, generation)) != 0) return error; \
} while (0)

static int
rtw88_pcie_wait8(struct rtw88_softc *sc, uint32_t generation,
    uint32_t reg, uint8_t mask)
{
	uint8_t value;
	unsigned i;
	int error;

	for (i = 0; i < 20; i++) {
		RTW88_PCIE_STEP(sc->sc_ops->read_1(sc->sc_bus, reg, &value));
		if ((value & mask) == 0)
			return 0;
		if ((error = rtw88_pcie_current(sc, generation)) != 0)
			return error;
		sc->sc_ops->delay_us(sc->sc_bus, 10);
	}
	return ETIMEDOUT;
}

static int
rtw88_pcie_8821_setup(struct rtw88_softc *sc, uint32_t generation,
    int clkreq_enabled)
{
	uint32_t mix;
	uint8_t value;
	int error;

	if (sc == NULL || sc->sc_ops == NULL || sc->sc_ops->read_1 == NULL ||
	    sc->sc_ops->read_4 == NULL || sc->sc_ops->write_1 == NULL ||
	    sc->sc_ops->write_2 == NULL || sc->sc_ops->write_4 == NULL ||
	    sc->sc_ops->delay_us == NULL)
		return EINVAL;
	/* Donor Gen1 table: MDIO register 0x09 = 0x6380 for all cuts. */
	RTW88_PCIE_STEP(sc->sc_ops->write_2(sc->sc_bus, 0x03f4, 0x6380));
	RTW88_PCIE_STEP(sc->sc_ops->write_1(sc->sc_bus, 0x03f8, 0x09));
	RTW88_PCIE_STEP(sc->sc_ops->write_1(sc->sc_bus, 0x03fb, 0));
	RTW88_PCIE_STEP(sc->sc_ops->read_4(sc->sc_bus, 0x03f8, &mix));
	RTW88_PCIE_STEP(sc->sc_ops->write_4(sc->sc_bus, 0x03f8, mix | 0x20));
	error = rtw88_pcie_wait8(sc, generation, 0x03f8, 0x20);
	if (error != 0 || !clkreq_enabled)
		return error;

	/* DBI read byte 0x719; then enable CLKREQ software gate bit 4. */
	RTW88_PCIE_STEP(sc->sc_ops->write_2(sc->sc_bus, 0x03f0, 0x0718));
	RTW88_PCIE_STEP(sc->sc_ops->write_1(sc->sc_bus, 0x03f2, 0x02));
	error = rtw88_pcie_wait8(sc, generation, 0x03f2, 0x03);
	if (error != 0)
		return error;
	RTW88_PCIE_STEP(sc->sc_ops->read_1(sc->sc_bus, 0x03ed, &value));
	RTW88_PCIE_STEP(sc->sc_ops->write_1(sc->sc_bus, 0x03e9, value | 0x10));
	RTW88_PCIE_STEP(sc->sc_ops->write_2(sc->sc_bus, 0x03f0, 0x2718));
	RTW88_PCIE_STEP(sc->sc_ops->write_1(sc->sc_bus, 0x03f2, 0x01));
	return rtw88_pcie_wait8(sc, generation, 0x03f2, 0x03);
}
#undef RTW88_PCIE_STEP
#endif
