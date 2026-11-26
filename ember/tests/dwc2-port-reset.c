/* SPDX-License-Identifier: BSD-2-Clause */
/* Exercise both reset edges in the production DWC2 port interrupt handler. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef uint32_t u32;
#include "dwc2_hw.h"

struct core_params {
	unsigned dma_desc_fs_enable;
	unsigned dma_desc_enable;
};

struct dwc2_hsotg {
	struct {
		struct {
			bool port_reset_change;
			bool port_enable_change;
			bool port_connect_status_change;
			bool port_over_current_change;
		} b;
	} flags;
	struct core_params *core_params;
	struct dwc2_hsotg *hsotg_sc;
	u32 input;
	u32 cleared;
	unsigned notifications;
	bool new_connection;
};

#define dev_vdbg(...) ((void)0)
#define DWC2_READ_4(sc, reg) ((reg) == HPRT0 ? (sc)->input : 0U)
#define DWC2_WRITE_4(sc, reg, val) do {		\
	if ((reg) == HPRT0)			\
		(sc)->cleared |= (val);		\
} while (0)

static void
dwc2_hcd_connect(struct dwc2_hsotg *sc)
{
	sc->flags.b.port_connect_status_change = true;
}

static void
dwc2_hprt0_enable(struct dwc2_hsotg *sc, u32 status, u32 *update)
{
	(void)status;
	(void)update;
	sc->flags.b.port_reset_change = true;
}

static void
dwc2_root_intr(struct dwc2_hsotg *sc)
{
	sc->notifications++;
}

#include "port-intr.inc"

int
main(void)
{
	struct core_params params = { .dma_desc_fs_enable = 1, .dma_desc_enable = 1 };
	struct dwc2_hsotg sc = { .core_params = &params };

	sc.hsotg_sc = &sc;
	sc.input = HPRT0_RST | HPRT0_ENACHG;
	dwc2_port_intr(&sc);
	assert(!sc.flags.b.port_enable_change);
	assert((sc.cleared & HPRT0_ENACHG) != 0 && sc.notifications == 0);
	assert(params.dma_desc_enable == 0);

	sc.cleared = 0;
	sc.input = HPRT0_ENACHG | HPRT0_ENA;
	dwc2_port_intr(&sc);
	assert(sc.flags.b.port_reset_change && sc.new_connection);
	assert((sc.cleared & HPRT0_ENACHG) != 0);
	assert(!sc.flags.b.port_enable_change);

	sc.input = HPRT0_ENACHG;
	dwc2_port_intr(&sc);
	assert(sc.flags.b.port_enable_change && sc.notifications == 1);

	/* A later reset must preserve pending errors and other change bits. */
	sc.input = HPRT0_RST | HPRT0_ENACHG | HPRT0_CONNDET | HPRT0_OVRCURRCHG;
	dwc2_port_intr(&sc);
	assert(sc.flags.b.port_enable_change && sc.flags.b.port_connect_status_change);
	assert(sc.flags.b.port_over_current_change && sc.notifications == 2);

	puts("PASS: DWC2 port reset edges, real disable and simultaneous changes");
	return 0;
}
