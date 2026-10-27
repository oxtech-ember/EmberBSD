/* SPDX-License-Identifier: BSD-2-Clause */
/* Test-only MMIO and kernel boundaries. The runner inserts actual functions. */
/* PRODUCTION LICENSE */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifndef __packed
#define __packed __attribute__((__packed__))
#endif
#ifndef __aligned
#define __aligned(n) __attribute__((__aligned__(n)))
#endif
#ifndef __CTASSERT
#define __CTASSERT(e) _Static_assert(e, #e)
#endif
#include "sunxi_emac.h"

/* PRODUCTION LIMIT */
#define IFF_RUNNING 1
#define EMAC_ASSERT_LOCKED(sc) ((void)(sc))
#define aprint_debug_dev(...) ((void)0)

struct ifnet { unsigned int if_flags; };
struct mii_data { int changes; };
struct sunxi_emac_softc {
	struct { struct ifnet ec_if; } ec;
	struct mii_data mii;
	struct { uint32_t desc_ring_paddr; } tx, rx;
	int stat_ch;
};

static struct {
	uint32_t regs[128];
	unsigned int reset_writes, other_writes, reads, delays;
	unsigned int filters, interrupts, media, schedules;
	unsigned int clear_after;
	bool stuck;
} bus;
static int sunxi_emac_burst_len = 8;
static int sunxi_emac_rx_tx_pri = 0;
static int hz = 100;

static uint32_t
read_reg(unsigned int reg)
{
	assert(reg / 4 < 128 && reg % 4 == 0);
	if (reg == EMAC_BASIC_CTL_1 &&
	    (bus.regs[reg / 4] & BASIC_CTL_SOFT_RST) != 0) {
		bus.reads++;
		if (!bus.stuck && bus.reads >= bus.clear_after)
			bus.regs[reg / 4] &= ~BASIC_CTL_SOFT_RST;
	}
	return bus.regs[reg / 4];
}

static void
write_reg(unsigned int reg, uint32_t value)
{
	assert(reg / 4 < 128 && reg % 4 == 0);
	if (reg == EMAC_BASIC_CTL_1 && value == BASIC_CTL_SOFT_RST) {
		bus.reset_writes++;
		bus.reads = 0;
	} else {
		bus.other_writes++;
	}
	bus.regs[reg / 4] = value;
}

#define RD4(sc, reg) ((void)(sc), read_reg(reg))
#define WR4(sc, reg, value) ((void)(sc), write_reg(reg, value))
static void delay(unsigned int us) { assert(us == 10); bus.delays++; }
static void sunxi_emac_setup_rxfilter(struct sunxi_emac_softc *sc)
{ (void)sc; bus.filters++; }
static void sunxi_emac_enable_intr(struct sunxi_emac_softc *sc)
{ (void)sc; bus.interrupts++; }
static void mii_mediachg(struct mii_data *mii) { mii->changes++; bus.media++; }
static void callout_schedule(int *ch, int ticks)
{ (void)ch; assert(ticks == hz); bus.schedules++; }

/* PRODUCTION FUNCTIONS */

static void
check_started(struct sunxi_emac_softc *sc)
{
	assert(sc->ec.ec_if.if_flags & IFF_RUNNING);
	assert(bus.regs[EMAC_TX_DMA_LIST / 4] == 0x1000);
	assert(bus.regs[EMAC_RX_DMA_LIST / 4] == 0x2000);
	assert(bus.regs[EMAC_TX_CTL_1 / 4] & TX_DMA_EN);
	assert(bus.regs[EMAC_RX_CTL_1 / 4] & RX_DMA_EN);
	assert(bus.regs[EMAC_TX_CTL_0 / 4] & 0x80000000U);
	assert(bus.regs[EMAC_RX_CTL_0 / 4] & 0x80000000U);
	assert(bus.filters == 1 && bus.interrupts == 1);
	assert(bus.media == 1 && bus.schedules == 1);
}

int
main(void)
{
	struct sunxi_emac_softc sc = { 0 };
	int error;

	sc.tx.desc_ring_paddr = 0x1000;
	sc.rx.desc_ring_paddr = 0x2000;
	bus.stuck = true;
	error = sunxi_emac_init_locked(&sc);
	assert(error == ETIMEDOUT);
	assert((sc.ec.ec_if.if_flags & IFF_RUNNING) == 0);
	assert(bus.reset_writes == 1 && bus.other_writes == 0);
	assert(bus.filters == 0 && bus.interrupts == 0);
	assert(bus.media == 0 && bus.schedules == 0);
	assert(bus.reads == SOFT_RST_RETRY && bus.delays == SOFT_RST_RETRY);

	/* A later attempt can succeed after the hardware recovers. */
	bus.stuck = false;
	bus.clear_after = 3;
	error = sunxi_emac_init_locked(&sc);
	assert(error == 0);
	check_started(&sc);
	assert(bus.reset_writes == 2);
	assert(bus.reads == 3);

	/* Reinitialization of a running interface must not reset live DMA. */
	unsigned int writes = bus.other_writes;
	error = sunxi_emac_init_locked(&sc);
	assert(error == 0);
	check_started(&sc);
	assert(bus.reset_writes == 2 && bus.other_writes == writes);

	memset(&bus, 0, sizeof(bus));
	sc.ec.ec_if.if_flags = 0;
	bus.clear_after = 1;
	error = sunxi_emac_init_locked(&sc);
	assert(error == 0);
	check_started(&sc);
	assert(bus.reset_writes == 1 && bus.delays == 0);
	puts("sunxi EMAC reset/init contract: PASS");
	return 0;
}
