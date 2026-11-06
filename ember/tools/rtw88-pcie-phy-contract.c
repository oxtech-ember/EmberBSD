/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual PCIe PHY helper with deterministic fake MMIO. */
#include "rtw88-test-kernel.h"
#include <dev/ic/rtw88var.h>
#include <dev/pci/rtw88_pcie_phy.h>
#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "check failed: %s:%d: %s\n", __FILE__, __LINE__, #x); \
    abort(); \
} } while (0)

static struct {
	struct rtw88_softc *sc;
	uint8_t regs[0x400];
	unsigned ops, fail_at, cancel_at, delays, commands;
	int stuck_mdio, stuck_dbi;
} bus;

static int
access_event(void)
{
	bus.ops++;
	if (bus.ops == bus.fail_at)
		return EIO;
	if (bus.ops == bus.cancel_at)
		bus.sc->sc_generation++;
	return 0;
}

static int
read1(void *cookie, uint32_t reg, uint8_t *value)
{
	int error = access_event();
	(void)cookie;
	if (error != 0)
		return error;
	*value = bus.regs[reg];
	if (reg == 0x3f8 && !bus.stuck_mdio)
		*value &= (uint8_t)~0x20;
	if (reg == 0x3f2 && !bus.stuck_dbi)
		*value = 0;
	return 0;
}

static int
read4(void *cookie, uint32_t reg, uint32_t *value)
{
	int error = access_event();
	(void)cookie;
	if (error != 0)
		return error;
	*value = (uint32_t)bus.regs[reg] | (uint32_t)bus.regs[reg + 1] << 8 |
	    (uint32_t)bus.regs[reg + 2] << 16 | (uint32_t)bus.regs[reg + 3] << 24;
	return 0;
}

static int
write1(void *cookie, uint32_t reg, uint8_t value)
{
	int error = access_event();
	(void)cookie;
	if (error != 0)
		return error;
	bus.regs[reg] = value;
	if (reg == 0x3f2)
		bus.commands++;
	return 0;
}

static int
write2(void *cookie, uint32_t reg, uint16_t value)
{
	int error = access_event();
	(void)cookie;
	if (error != 0)
		return error;
	bus.regs[reg] = (uint8_t)value;
	bus.regs[reg + 1] = (uint8_t)(value >> 8);
	return 0;
}

static int
write4(void *cookie, uint32_t reg, uint32_t value)
{
	int error = access_event();
	unsigned i;
	(void)cookie;
	if (error != 0)
		return error;
	for (i = 0; i < 4; i++)
		bus.regs[reg + i] = (uint8_t)(value >> (i * 8));
	return 0;
}

static void
pcie_delay(void *cookie, unsigned us)
{
	(void)cookie;
	CHECK(us == 10);
	bus.delays++;
}

static const struct rtw88_bus_ops ops = {
	.read_1 = read1, .read_4 = read4, .write_1 = write1,
	.write_2 = write2, .write_4 = write4, .delay_us = pcie_delay
};

static void
reset(struct rtw88_softc *sc)
{
	memset(&bus, 0, sizeof(bus));
	memset(sc, 0, sizeof(*sc));
	sc->sc_ops = &ops;
	sc->sc_generation = 7;
	bus.sc = sc;
	bus.regs[0x3ed] = 0x85;
}

int
main(void)
{
	struct rtw88_softc sc;
	unsigned count, i;

	reset(&sc);
	CHECK(rtw88_pcie_8821_setup(&sc, 7, 0) == 0);
	CHECK(bus.regs[0x3f4] == 0x80 && bus.regs[0x3f5] == 0x63);
	CHECK(bus.regs[0x3fb] == 0 && bus.commands == 0);
	reset(&sc);
	CHECK(rtw88_pcie_8821_setup(&sc, 7, 1) == 0);
	CHECK(bus.commands == 2 && bus.regs[0x3e9] == 0x95);
	CHECK(bus.regs[0x3f0] == 0x18 && bus.regs[0x3f1] == 0x27);
	count = bus.ops;
	for (i = 1; i <= count; i++) {
		reset(&sc); bus.fail_at = i;
		CHECK(rtw88_pcie_8821_setup(&sc, 7, 1) == EIO);
		CHECK(bus.ops == i);
		reset(&sc); bus.cancel_at = i;
		CHECK(rtw88_pcie_8821_setup(&sc, 7, 1) == EINTR);
		CHECK(bus.ops == i);
	}
	reset(&sc); bus.stuck_mdio = 1;
	CHECK(rtw88_pcie_8821_setup(&sc, 7, 1) == ETIMEDOUT);
	CHECK(bus.delays == 20 && bus.commands == 0);
	reset(&sc); bus.stuck_dbi = 1;
	CHECK(rtw88_pcie_8821_setup(&sc, 7, 1) == ETIMEDOUT);
	CHECK(bus.delays == 20 && bus.commands == 1);
	bus.sc = NULL;
	puts("PASS: 8821 PCIe MDIO, conditional CLKREQ, faults and cancellation");
	return 0;
}
