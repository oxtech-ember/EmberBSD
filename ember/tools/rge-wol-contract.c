/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. AI-assisted actual-source tests. */
/*
 * Actual-source Wake-on-LAN contract.  The chip registers, PCI configuration
 * space, sysctl tree and device messages are explicit service boundaries;
 * the driver functions under test are extracted from if_rge.c.
 */
#include <sys/types.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned int checks;
#define CHECK(c) do { checks++; if (!(c)) { \
	fprintf(stderr, "FAIL line %u: %s\n", (unsigned)__LINE__, #c); exit(1); \
} } while (0)
#ifndef __unused
#define __unused __attribute__((unused))
#endif
#define CLR(v,b) ((v)&=~(b))
#define DELAY(n) ((void)(n))
#define ETHER_ADDR_LEN 6
#define BUS_DMA_NOWAIT 1

typedef uint64_t bus_addr_t;
typedef size_t bus_size_t;
typedef unsigned int u_int;
typedef unsigned long u_long;
typedef unsigned int pcireg_t;
typedef void *device_t;
typedef void *bus_space_tag_t;
typedef uintptr_t bus_space_handle_t;
typedef void *pci_chipset_tag_t;
typedef unsigned int pcitag_t;
typedef unsigned int pci_intr_handle_t;
typedef unsigned int callout_t;
struct lwp { int dummy; };
struct ifnet { void *if_softc, *if_sadl, *if_percpuq; unsigned int if_mtu, if_flags, if_capabilities; };
struct ethercom { struct ifnet ec_if; unsigned int ec_capabilities, ec_rx_mowner; };
struct ifmedia { unsigned int dummy; };
struct mbuf { struct mbuf *m_next; };
typedef struct { bus_addr_t ds_addr; bus_size_t ds_len; } bus_dma_segment_t;
struct test_tag { unsigned int dummy; };
typedef struct test_tag *bus_dma_tag_t;
struct test_map { unsigned int dummy; };
typedef struct test_map *bus_dmamap_t;

/* Native sysctl, reboot and PCI constants, extracted from the tree. */
struct sysctlnode {
	uint32_t sysctl_flags;
	void *sysctl_data;
	const char *sysctl_name;
	int (*sysctl_func)(const int *, u_int, void *, size_t *, const void *,
	    size_t, const int *, struct lwp *, const struct sysctlnode *);
};
struct sysctllog { unsigned int live; };
#define SYSCTL_DESCR(s) s
#include "wol-native.inc"
#include "rge-header.inc"

static int tag_dev;

/* ---- chip model ---- */
#define MMIO_SIZE 0x100
static uint8_t mmio[MMIO_SIZE];
static uint16_t ocp[0x10000 / 2];
static unsigned int mmio_writes, locked_writes, ocp_writes;
static unsigned int stop_calls, stops_unprotected, spl_depth, spl_errors;
static unsigned int event_seq, last_mmio_seq, last_ocp_seq, pci_write_seq;
static unsigned int stop_seq, ppsw_seq, eecmd_seq, rxcfg_seq;
static bool written[MMIO_SIZE];
static struct rge_softc *active_sc;

static bool
cfg_locked(void)
{
	return (mmio[RGE_EECMD] & 0xc0) != 0xc0;
}

static void
bus_space_write_1(bus_space_tag_t t, bus_space_handle_t h, bus_size_t r,
    uint8_t v)
{
	(void)t; (void)h;
	CHECK(r < MMIO_SIZE);
	mmio_writes++; last_mmio_seq = ++event_seq; written[r] = true;
	if (r == RGE_PPSW)
		ppsw_seq = event_seq;
	if (r == RGE_EECMD && eecmd_seq == 0)
		eecmd_seq = event_seq;
	/* The configuration registers only latch while unlocked. */
	if (r >= RGE_CFG0 && r <= RGE_CFG5 && cfg_locked()) {
		locked_writes++;
		return;
	}
	mmio[r] = v;
}
static uint8_t
bus_space_read_1(bus_space_tag_t t, bus_space_handle_t h, bus_size_t r)
{
	(void)t; (void)h;
	CHECK(r < MMIO_SIZE);
	return mmio[r];
}
static void __unused
bus_space_write_2(bus_space_tag_t t, bus_space_handle_t h, bus_size_t r,
    uint16_t v)
{
	(void)t; (void)h; (void)r; (void)v;
	CHECK(false);
}
static uint16_t __unused
bus_space_read_2(bus_space_tag_t t, bus_space_handle_t h, bus_size_t r)
{
	(void)t; (void)h; (void)r;
	CHECK(false);
	return 0;
}
static void __unused
bus_space_write_4(bus_space_tag_t t, bus_space_handle_t h, bus_size_t r,
    uint32_t v)
{
	(void)t; (void)h;
	CHECK(r + 4 <= MMIO_SIZE);
	mmio_writes++; last_mmio_seq = ++event_seq;
	if (r == RGE_RXCFG)
		rxcfg_seq = event_seq;
	for (unsigned int i = 0; i < 4; i++)
		written[r + i] = true;
	memcpy(&mmio[r], &v, 4);
}
static uint32_t __unused
bus_space_read_4(bus_space_tag_t t, bus_space_handle_t h, bus_size_t r)
{
	uint32_t v;

	(void)t; (void)h;
	CHECK(r + 4 <= MMIO_SIZE);
	memcpy(&v, &mmio[r], 4);
	return v;
}

static void
rge_write_mac_ocp(struct rge_softc *sc, uint16_t reg, uint16_t val)
{
	(void)sc;
	ocp_writes++; last_ocp_seq = ++event_seq;
	ocp[reg / 2] = val;
}
static uint16_t
rge_read_mac_ocp(struct rge_softc *sc, uint16_t reg)
{
	(void)sc;
	return ocp[reg / 2];
}
/*
 * Stopping the interface is its own service; model its visible effect:
 * the interface leaves the running state, the receive filter closes and
 * the chip reset closes the RXDV gate.  The wake settings survive.
 */
static int
splnet(void)
{
	return (int)spl_depth++;
}
static void
splx(int s)
{
	if (spl_depth == 0 || (unsigned int)s != spl_depth - 1)
		spl_errors++;
	else
		spl_depth--;
}
static void
rge_stop(struct ifnet *ifp, int disable)
{
	CHECK(ifp == &active_sc->sc_ec.ec_if);
	CHECK(disable == 1);
	stop_calls++; stop_seq = ++event_seq;
	if (spl_depth == 0)
		stops_unprotected++;
	ifp->if_flags &= ~(unsigned int)IFF_RUNNING;
	{
		uint32_t v;

		memcpy(&v, &mmio[RGE_RXCFG], 4);
		v &= ~(uint32_t)(RGE_RXCFG_ALLPHYS | RGE_RXCFG_INDIV |
		    RGE_RXCFG_MULTI | RGE_RXCFG_BROAD);
		memcpy(&mmio[RGE_RXCFG], &v, 4);
	}
	mmio[RGE_PPSW] |= 0x08;
}

/* ---- PCI configuration, messages, sysctl ---- */
static uint32_t pci_cfg[64];
static bool have_pm;
static int pm_off = 0x40;
static unsigned int pci_writes, messages, error_messages;
static char last_message[160];

static int
pci_get_capability(pci_chipset_tag_t pc, pcitag_t tag, int id, int *offp,
    pcireg_t *valp)
{
	(void)pc; (void)tag; (void)valp;
	CHECK(id == PCI_CAP_PWRMGMT);
	if (!have_pm)
		return 0;
	*offp = pm_off;
	return 1;
}
static pcireg_t
pci_conf_read(pci_chipset_tag_t pc, pcitag_t tag, int reg)
{
	(void)pc; (void)tag;
	CHECK(reg >= 0 && reg < 256 && (reg & 3) == 0);
	return pci_cfg[reg / 4];
}
static void
pci_conf_write(pci_chipset_tag_t pc, pcitag_t tag, int reg, pcireg_t v)
{
	(void)pc; (void)tag;
	CHECK(reg >= 0 && reg < 256 && (reg & 3) == 0);
	pci_writes++; pci_write_seq = ++event_seq;
	if (reg == pm_off + PCI_PMCSR) {
		/* PME_STS is write-one-to-clear; the rest is plain storage. */
		pcireg_t keep = pci_cfg[reg / 4] & ~v & PCI_PMCSR_PME_STS;

		pci_cfg[reg / 4] = (v & ~PCI_PMCSR_PME_STS) | keep;
	} else
		pci_cfg[reg / 4] = v;
}

static void
record_message(const char *fmt, va_list ap)
{
	CHECK(vsnprintf(last_message, sizeof(last_message), fmt, ap) > 0);
}
static void
device_printf(device_t d, const char *fmt, ...)
{
	va_list ap;

	(void)d;
	va_start(ap, fmt);
	record_message(fmt, ap);
	va_end(ap);
	messages++;
}
static void
aprint_error_dev(device_t d, const char *fmt, ...)
{
	va_list ap;

	(void)d;
	va_start(ap, fmt);
	record_message(fmt, ap);
	va_end(ap);
	error_messages++;
}
static void *
device_private(device_t d)
{
	CHECK(d == (device_t)&tag_dev);
	return active_sc;
}
static const char *
device_xname(device_t d)
{
	(void)d;
	return "rge0";
}

/* Stand-in for sysctl_lookup(9): copy an int out and, if asked, in. */
static int
sysctl_lookup(SYSCTLFN_ARGS)
{
	(void)name; (void)namelen; (void)oname; (void)l;
	if (oldp != NULL) {
		CHECK(*oldlenp >= sizeof(int));
		memcpy(oldp, rnode->sysctl_data, sizeof(int));
		*oldlenp = sizeof(int);
	}
	if (newp != NULL) {
		CHECK(newlen == sizeof(int));
		memcpy(rnode->sysctl_data, newp, sizeof(int));
	}
	return 0;
}

struct create_call {
	int flags, type, parent_valid;
	const char *name;
	void *func, *data;
	int first_mib;
};
static struct create_call creates[4];
static unsigned int ncreates, fail_create_at, teardowns;
static struct sysctlnode node_store[4];

static int
sysctl_createv(struct sysctllog **log, int cflags, const struct sysctlnode **rn,
    const struct sysctlnode **cn, int flags, int type, const char *name,
    const char *desc, int (*func)(SYSCTLFN_PROTO), uint64_t qv, void *data,
    size_t newlen, ...)
{
	struct create_call *c = &creates[ncreates];
	struct sysctlnode *n = &node_store[ncreates];
	va_list ap;

	(void)cflags; (void)desc; (void)newlen; (void)qv;
	CHECK(ncreates < 4);
	ncreates++;
	if (fail_create_at == ncreates)
		return ENOMEM;
	c->flags = flags; c->type = type; c->name = name;
	c->func = (void *)(uintptr_t)func; c->data = data;
	c->parent_valid = rn != NULL && *rn != NULL;
	va_start(ap, newlen);
	c->first_mib = va_arg(ap, int);
	va_end(ap);
	n->sysctl_name = name;
	if (*log == NULL) {
		*log = calloc(1, sizeof(**log));
		CHECK(*log != NULL);
		(*log)->live = 1;
	}
	*cn = n;
	return 0;
}
static void
sysctl_teardown(struct sysctllog **log)
{
	if (log == NULL || *log == NULL)
		return;
	teardowns++;
	free(*log);
	*log = NULL;
}


#include "wol-source.inc"

static struct rge_softc softc;

static void
reset_model(int wol, bool pm_cap, bool pm_en)
{
	memset(mmio, 0, sizeof(mmio));
	memset(ocp, 0, sizeof(ocp));
	memset(written, 0, sizeof(written));
	memset(pci_cfg, 0, sizeof(pci_cfg));
	mmio_writes = locked_writes = ocp_writes = 0;
	stop_calls = stops_unprotected = spl_depth = spl_errors = 0;
	event_seq = last_mmio_seq = last_ocp_seq = pci_write_seq = 0;
	stop_seq = ppsw_seq = eecmd_seq = rxcfg_seq = 0;
	pci_writes = messages = error_messages = 0;
	last_message[0] = '\0';
	have_pm = pm_cap;
	memset(&softc, 0, sizeof(softc));
	softc.sc_dev = &tag_dev;
	softc.sc_wol = wol;
	active_sc = &softc;
	/*
	 * State of a stopped interface: RXDV gate closed, receive filter
	 * closed, firmware left stale wake bits behind.
	 */
	mmio[RGE_PPSW] = 0x08 | 0x40;
	mmio[RGE_CFG1] = (pm_en ? RGE_CFG1_PM_EN : 0) | RGE_CFG1_SPEED_DOWN;
	mmio[RGE_CFG2] = RGE_CFG2_CLKREQ_EN;
	mmio[RGE_CFG3] = RGE_CFG3_RDY_TO_L23 | RGE_CFG3_WOL_LINK |
	    RGE_CFG3_WOL_MAGIC;
	mmio[RGE_CFG5] = RGE_CFG5_WOL_UCAST | RGE_CFG5_WOL_MCAST |
	    RGE_CFG5_WOL_BCAST | RGE_CFG5_PME_STS;
	mmio[RGE_CMD] = 0;
	mmio[RGE_RXCFG] = 0;
	pci_cfg[(pm_off + PCI_PMCSR) / 4] = PCI_PMCSR_STATE_D0 |
	    PCI_PMCSR_PME_STS;
}

static void
snapshot_unchanged_outside(const uint8_t *before, const unsigned int *allowed,
    unsigned int n)
{
	for (unsigned int r = 0; r < MMIO_SIZE; r++) {
		bool ok = false;

		for (unsigned int i = 0; i < n; i++)
			if (allowed[i] <= r && r < allowed[i] + 1)
				ok = true;
		if (!ok)
			CHECK(mmio[r] == before[r] && !written[r]);
	}
}

static void
no_wol_activity(void)
{
	CHECK(mmio_writes == 0 && ocp_writes == 0 && stop_calls == 0);
	CHECK(pci_writes == 0);
	CHECK(!written[RGE_CFG1] && !written[RGE_CFG2] && !written[RGE_CFG3] &&
	    !written[RGE_CFG5] && !written[RGE_EECMD] && !written[RGE_PPSW]);
	CHECK(ocp[0xc0b6 / 2] == 0);
}

static void
test_off(void)
{
	uint8_t before[MMIO_SIZE];

	/* wol=0: behaviour is unchanged, whatever the shutdown flags. */
	reset_model(0, true, true);
	softc.sc_ec.ec_if.if_flags = IFF_RUNNING;
	memcpy(before, mmio, sizeof(before));
	CHECK(rge_shutdown(&tag_dev, RB_POWERDOWN) == true);
	no_wol_activity();
	CHECK(softc.sc_ec.ec_if.if_flags & IFF_RUNNING);
	CHECK(messages == 0 && error_messages == 0);
	CHECK(memcmp(before, mmio, sizeof(before)) == 0);
	CHECK(pci_cfg[(pm_off + PCI_PMCSR) / 4] ==
	    (PCI_PMCSR_STATE_D0 | PCI_PMCSR_PME_STS));
	CHECK(rge_shutdown(&tag_dev, 0) == true);
	no_wol_activity();

	/* wol=0 without PM capability stays silent too. */
	reset_model(0, false, true);
	CHECK(rge_shutdown(&tag_dev, RB_POWERDOWN) == true);
	no_wol_activity();
	CHECK(messages == 0 && error_messages == 0);

	/* wol=1 but a plain reboot or halt: nothing is armed. */
	reset_model(1, true, true);
	CHECK(rge_shutdown(&tag_dev, 0) == true);
	no_wol_activity();
	CHECK(rge_shutdown(&tag_dev, RB_HALT) == true);
	no_wol_activity();
	CHECK(messages == 0 && error_messages == 0);
}

static void
test_armed(int type, bool running)
{
	static const unsigned int allowed[] = {
	    RGE_EECMD, RGE_CFG1, RGE_CFG2, RGE_CFG3, RGE_CFG5, RGE_PPSW,
	    RGE_RXCFG, RGE_RXCFG + 1, RGE_RXCFG + 2, RGE_RXCFG + 3
	};
	const uint32_t accept = RGE_RXCFG_INDIV | RGE_RXCFG_MULTI |
	    RGE_RXCFG_BROAD;
	const uint32_t keep = 0x00a00000;	/* unrelated RXCFG bits */
	uint8_t before[MMIO_SIZE];

	reset_model(1, true, true);
	softc.rge_type = type;
	bus_space_write_4(NULL, 0, RGE_RXCFG, keep);
	if (running) {
		softc.sc_ec.ec_if.if_flags = IFF_RUNNING;
		/* A running interface: gate open, receive filter open. */
		mmio[RGE_PPSW] &= (uint8_t)~0x08;
		bus_space_write_4(NULL, 0, RGE_RXCFG, keep | accept |
		    RGE_RXCFG_ALLPHYS);
	}
	memset(written, 0, sizeof(written));
	mmio_writes = 0; rxcfg_seq = 0;
	memcpy(before, mmio, sizeof(before));
	CHECK(rge_shutdown(&tag_dev, RB_POWERDOWN) == true);

	/* Magic packet wakeup is on and the unicast/multicast/broadcast
	   wake filters are off. */
	CHECK(ocp[0xc0b6 / 2] & 0x0001);
	CHECK(ocp_writes == 1);
	CHECK(!(mmio[RGE_CFG5] & (RGE_CFG5_WOL_UCAST | RGE_CFG5_WOL_MCAST |
	    RGE_CFG5_WOL_BCAST)));
	CHECK(mmio[RGE_CFG5] & RGE_CFG5_WOL_LANWAKE);
	CHECK(!(mmio[RGE_CFG3] & (RGE_CFG3_WOL_LINK | RGE_CFG3_WOL_MAGIC)));
	/* Unrelated bits survive. */
	CHECK(mmio[RGE_CFG3] & RGE_CFG3_RDY_TO_L23);
	CHECK(mmio[RGE_CFG5] & RGE_CFG5_PME_STS);
	CHECK(mmio[RGE_CFG1] & RGE_CFG1_SPEED_DOWN);
	CHECK(mmio[RGE_CFG2] & RGE_CFG2_CLKREQ_EN);
	CHECK(mmio[RGE_PPSW] & 0x40);

	/* The configuration lock was opened for the writes and is released. */
	CHECK(locked_writes == 0);
	CHECK((mmio[RGE_EECMD] & 0xc0) == 0);

	/* Power management enabled and the RXDV gate open at the end. */
	CHECK(mmio[RGE_CFG1] & RGE_CFG1_PM_EN);
	CHECK(mmio[RGE_CFG2] & RGE_CFG2_PMSTS_EN);
	CHECK(!(mmio[RGE_PPSW] & 0x08));
	/*
	 * A running interface is stopped once, at raised priority, after the
	 * wake settings are written and before the gate is opened; an
	 * interface that is not running is left alone.
	 */
	CHECK(stop_calls == (running ? 1u : 0u));
	CHECK(stops_unprotected == 0 && spl_depth == 0 && spl_errors == 0);
	CHECK(!(softc.sc_ec.ec_if.if_flags & IFF_RUNNING));
	if (running) {
		CHECK(stop_seq > last_ocp_seq);
		CHECK(stop_seq > eecmd_seq);
		CHECK(ppsw_seq > stop_seq);
	}
	/*
	 * After the stop the receive filter accepts frames to the station,
	 * broadcast and multicast, not every frame, and keeps its other bits.
	 */
	CHECK(bus_space_read_4(NULL, 0, RGE_RXCFG) == (keep | accept));
	if (running)
		CHECK(rxcfg_seq > stop_seq);
	/* The extracted code itself writes no reset or command register. */
	CHECK(!written[RGE_CMD] && !written[RGE_IMR] && !written[RGE_ISR]);

	/* PME enabled, stale PME status cleared, power state preserved. */
	CHECK(pci_writes == 1);
	CHECK(pci_cfg[(pm_off + PCI_PMCSR) / 4] == PCI_PMCSR_PME_EN);
	CHECK(messages == 0 && error_messages == 0);

	/* PCI write is last, after the lock was released. */
	CHECK(pci_write_seq > last_mmio_seq && pci_write_seq > last_ocp_seq);

	snapshot_unchanged_outside(before, allowed,
	    sizeof(allowed) / sizeof(allowed[0]));
}

static void
test_disarm(void)
{
	static const unsigned int allowed[] = { RGE_EECMD, RGE_CFG3, RGE_CFG5 };
	uint8_t before[MMIO_SIZE];

	/* Attach: wake left armed by an earlier power-down is cleared. */
	reset_model(0, true, true);
	ocp[0xc0b6 / 2] = 0x8001;
	mmio[RGE_CFG5] |= RGE_CFG5_WOL_LANWAKE;
	memcpy(before, mmio, sizeof(before));
	rge_wol_disable(&softc);
	CHECK(ocp[0xc0b6 / 2] == 0x8000);
	CHECK(ocp_writes == 1);
	CHECK(!(mmio[RGE_CFG5] & (RGE_CFG5_WOL_LANWAKE | RGE_CFG5_WOL_UCAST |
	    RGE_CFG5_WOL_MCAST | RGE_CFG5_WOL_BCAST)));
	CHECK(!(mmio[RGE_CFG3] & (RGE_CFG3_WOL_LINK | RGE_CFG3_WOL_MAGIC)));
	/* Unrelated bits survive. */
	CHECK(mmio[RGE_CFG3] & RGE_CFG3_RDY_TO_L23);
	CHECK(mmio[RGE_CFG5] & RGE_CFG5_PME_STS);
	CHECK(locked_writes == 0);
	CHECK((mmio[RGE_EECMD] & 0xc0) == 0);
	/* No stop, no PCI write, no message; power management is untouched. */
	CHECK(stop_calls == 0 && pci_writes == 0);
	CHECK(messages == 0 && error_messages == 0);
	CHECK(!written[RGE_CFG1] && !written[RGE_CFG2] && !written[RGE_PPSW]);
	snapshot_unchanged_outside(before, allowed,
	    sizeof(allowed) / sizeof(allowed[0]));

	/* Nothing armed: the same call leaves the wake settings clear. */
	reset_model(0, true, false);
	mmio[RGE_CFG3] = RGE_CFG3_RDY_TO_L23;
	mmio[RGE_CFG5] = RGE_CFG5_PME_STS;
	rge_wol_disable(&softc);
	CHECK(ocp[0xc0b6 / 2] == 0);
	CHECK(mmio[RGE_CFG3] == RGE_CFG3_RDY_TO_L23);
	CHECK(mmio[RGE_CFG5] == RGE_CFG5_PME_STS);
	CHECK(locked_writes == 0 && (mmio[RGE_EECMD] & 0xc0) == 0);

	/* Armed again after a disarm: the full sequence still applies. */
	reset_model(1, true, true);
	rge_wol_disable(&softc);
	CHECK(rge_shutdown(&tag_dev, RB_POWERDOWN) == true);
	CHECK(ocp[0xc0b6 / 2] & 0x0001);
	CHECK(mmio[RGE_CFG5] & RGE_CFG5_WOL_LANWAKE);
	CHECK(pci_cfg[(pm_off + PCI_PMCSR) / 4] == PCI_PMCSR_PME_EN);
}

static void
test_no_pm_capability(void)
{
	uint8_t before[MMIO_SIZE];

	reset_model(1, false, true);
	softc.sc_ec.ec_if.if_flags = IFF_RUNNING;
	memcpy(before, mmio, sizeof(before));
	CHECK(rge_shutdown(&tag_dev, RB_POWERDOWN) == true);
	no_wol_activity();
	CHECK(softc.sc_ec.ec_if.if_flags & IFF_RUNNING);
	CHECK(memcmp(before, mmio, sizeof(before)) == 0);
	CHECK(messages == 1 && error_messages == 0);
	CHECK(strstr(last_message, "cannot do WOL") != NULL);
	CHECK(pci_cfg[(pm_off + PCI_PMCSR) / 4] ==
	    (PCI_PMCSR_STATE_D0 | PCI_PMCSR_PME_STS));
}

static void
test_pm_disabled_in_chip(void)
{
	uint8_t before[MMIO_SIZE];

	/* RGE_CFG1_PM_EN clear: refuse before any wake register is touched. */
	reset_model(1, true, false);
	softc.sc_ec.ec_if.if_flags = IFF_RUNNING;
	memcpy(before, mmio, sizeof(before));
	CHECK(rge_shutdown(&tag_dev, RB_POWERDOWN) == true);
	no_wol_activity();
	CHECK(softc.sc_ec.ec_if.if_flags & IFF_RUNNING);
	CHECK(memcmp(before, mmio, sizeof(before)) == 0);
	CHECK(messages == 1 && error_messages == 0);
	CHECK(strstr(last_message, "cannot do WOL") != NULL);
	CHECK(pci_cfg[(pm_off + PCI_PMCSR) / 4] ==
	    (PCI_PMCSR_STATE_D0 | PCI_PMCSR_PME_STS));
}

static int
sysctl_write(struct sysctlnode *node, int *value, size_t len)
{
	int cur;
	size_t oldlen = sizeof(cur);

	return rge_sysctl_wol(NULL, 0, &cur, &oldlen, value, len, NULL, NULL,
	    node);
}

static void
test_sysctl_handler(void)
{
	struct sysctlnode node;
	int cur, v;
	size_t oldlen;

	reset_model(0, true, true);
	memset(&node, 0, sizeof(node));
	node.sysctl_data = &softc;

	/* Read returns the current value and changes nothing. */
	oldlen = sizeof(cur);
	CHECK(rge_sysctl_wol(NULL, 0, &cur, &oldlen, NULL, 0, NULL, NULL,
	    &node) == 0);
	CHECK(cur == 0 && softc.sc_wol == 0);

	v = 1;
	CHECK(sysctl_write(&node, &v, sizeof(v)) == 0 && softc.sc_wol == 1);
	oldlen = sizeof(cur);
	CHECK(rge_sysctl_wol(NULL, 0, &cur, &oldlen, NULL, 0, NULL, NULL,
	    &node) == 0 && cur == 1);
	v = 0;
	CHECK(sysctl_write(&node, &v, sizeof(v)) == 0 && softc.sc_wol == 0);

	/* Anything but 0 and 1 is refused and leaves the value alone. */
	static const int bad[] = { 2, -1, 256, 0x7fffffff, -0x7fffffff - 1 };
	for (unsigned int i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		for (int start = 0; start <= 1; start++) {
			softc.sc_wol = start;
			v = bad[i];
			CHECK(sysctl_write(&node, &v, sizeof(v)) == EINVAL);
			CHECK(softc.sc_wol == start);
		}
	}
	/* No register or PCI access from the handler. */
	no_wol_activity();
}

static void
test_sysctl_attach(void)
{
	struct sysctlnode node;
	int v;

	/* Success: hw.<xname>.wol, read-write int, handler bound to sc. */
	reset_model(0, true, true);
	ncreates = teardowns = 0; fail_create_at = 0;
	rge_sysctl_attach(&softc);
	CHECK(ncreates == 2 && teardowns == 0 && error_messages == 0);
	CHECK(softc.sc_sysctllog != NULL);
	CHECK(creates[0].type == CTLTYPE_NODE && creates[0].first_mib == CTL_HW);
	CHECK(strcmp(creates[0].name, "rge0") == 0);
	CHECK(creates[1].parent_valid);
	CHECK(strcmp(creates[1].name, "wol") == 0);
	CHECK(creates[1].type == CTLTYPE_INT);
	CHECK(creates[1].flags == CTLFLAG_READWRITE);
	CHECK(creates[1].func == (void *)(uintptr_t)rge_sysctl_wol);
	CHECK(creates[1].data == &softc);
	CHECK(creates[1].first_mib == CTL_CREATE);
	/* The default stays off: attach does not enable anything. */
	CHECK(softc.sc_wol == 0);
	/* The registered handler works through the registered data. */
	memset(&node, 0, sizeof(node));
	node.sysctl_data = creates[1].data;
	v = 1;
	CHECK(sysctl_write(&node, &v, sizeof(v)) == 0 && softc.sc_wol == 1);
	/* Detach path: teardown releases the log and tolerates a second call. */
	sysctl_teardown(&softc.sc_sysctllog);
	CHECK(softc.sc_sysctllog == NULL && teardowns == 1);
	sysctl_teardown(&softc.sc_sysctllog);
	CHECK(teardowns == 1);

	/* Failure of either createv: reported once, log released. */
	for (unsigned int at = 1; at <= 2; at++) {
		reset_model(0, true, true);
		ncreates = teardowns = 0; fail_create_at = at;
		rge_sysctl_attach(&softc);
		CHECK(ncreates == at);
		CHECK(error_messages == 1 && teardowns == (at == 2 ? 1 : 0));
		CHECK(softc.sc_sysctllog == NULL);
		CHECK(softc.sc_wol == 0);
	}
	fail_create_at = 0;
}

int
main(int argc, char **argv)
{
	(void)argc; (void)argv;
	test_off();
	/* Every chip type of the table takes the same path. */
	for (int t = MAC_R25; t <= MAC_R27; t++) {
		test_armed(t, true);
		test_armed(t, false);
	}
	test_disarm();
	test_no_pm_capability();
	test_pm_disabled_in_chip();
	test_sysctl_handler();
	test_sysctl_attach();
	printf("rge-wol-contract: %u checks, 0 failures\n", checks);
	return 0;
}
