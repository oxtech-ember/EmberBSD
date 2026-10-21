/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: FreeBSD a143197801c680b958ab662c88cf086668a1958b rtw88 PCI. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/mutex.h>
#include <sys/once.h>
#include <sys/bus.h>
#include <sys/intr.h>
#include <sys/xcall.h>
#include <dev/firmload.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>
#include <dev/ic/rtw88var.h>
#include <dev/ic/rtw8822c.h>
#include <dev/ic/rtw88_net.h>

struct rtw88_pci_softc {
	struct rtw88_softc sc_core;
	struct rtw88_net *sc_net;
	device_t sc_dev;
	struct pci_attach_args sc_pa;
	bus_space_tag_t sc_st;
	bus_space_handle_t sc_sh;
	bus_size_t sc_mapsize;
	bus_dma_tag_t sc_dmat;
	pci_intr_handle_t *sc_pih;
	void *sc_ih;
	kmutex_t sc_lock;
	pcireg_t sc_command;
	uint8_t *sc_fw;
	size_t sc_fwlen;
	int sc_mapped;
	int sc_lock_initialized;
	int sc_command_saved;
	int sc_stopping;
	int sc_detaching;	/* The net wrapper is closed; only cleanup may run. */
};

/* pci.h at the pinned origin: BAR2 and RTL8822C WCPU interrupt masks. */
#define RTW88_PCI_BAR PCI_BAR(2)
#define RTW88_PCI_HIMR0 0x00b0
#define RTW88_PCI_HIMR1 0x00b8
#define RTW88_PCI_HIMR3 0x10b8
#define RTW88_PCI_MAP_MIN 0x10000
#define RTW88_PCI_RING_ALIGN 256

struct rtw88_pci_dma {
	bus_dmamap_t map;
	bus_dma_segment_t seg;
};

static int rtw88_pci_detach(device_t, int);
/*
 * Diagnostics run in process context without Giant. Keep this gate for the
 * whole operation so detach cannot destroy the instance lock or resources.
 * Lock order is the adaptive diagnostic gate, then the instance spin lock.
 * Interrupt callbacks never acquire the diagnostic gate.
 */
static struct rtw88_pci_softc *rtwce_sc;
static kmutex_t rtwce_diag_lock;
static ONCE_DECL(rtwce_diag_once);

static int
rtwce_diag_init(void)
{

	mutex_init(&rtwce_diag_lock, MUTEX_DEFAULT, IPL_NONE);
	return 0;
}

/* Return a stable instance with the diagnostic gate held. */
static struct rtw88_pci_softc *
rtwce_diag_enter(void)
{
	struct rtw88_pci_softc *psc;

	(void)RUN_ONCE(&rtwce_diag_once, rtwce_diag_init);
	mutex_enter(&rtwce_diag_lock);
	psc = rtwce_sc;
	if (psc == NULL)
		mutex_exit(&rtwce_diag_lock);
	return psc;
}

int
rtwce_started(void)
{
	struct rtw88_pci_softc *psc;
	int started;

	psc = rtwce_diag_enter();
	if (psc == NULL)
		return 0;
	mutex_enter(&psc->sc_lock);
	started = psc->sc_core.sc_started;
	mutex_exit(&psc->sc_lock);
	mutex_exit(&rtwce_diag_lock);
	return started;
}

int
rtwce_bootstrap(int on)
{
	struct rtw88_pci_softc *psc;
	struct rtw88_softc *sc;
	int error;

	psc = rtwce_diag_enter();
	if (psc == NULL)
		return ENXIO;
	sc = &psc->sc_core;
	mutex_enter(&psc->sc_lock);
	error = !sc->sc_resources_ready ? ENXIO :
	    (psc->sc_net != NULL || (on && psc->sc_detaching) ? EBUSY : 0);
	mutex_exit(&psc->sc_lock);
	/* Firmware loading and interrupt draining cannot hold a spin lock. */
	if (error == 0) {
		if (on)
			error = rtw88_start(sc);
		else
			error = rtw88_stop(sc);
	}
	mutex_exit(&rtwce_diag_lock);
	return error;
}

/* Diagnostic counters for the operator sysctl. */
int
rtwce_stats(char *buf, size_t len)
{
	struct rtw88_pci_softc *psc;
	struct rtw88_softc *sc;
	const struct rtw88_stats *st;
	struct rtw88_net_stats ns;
	uint32_t r[RTW88_DIAG_REGS] = { 0 };
	int n;

	psc = rtwce_diag_enter();
	if (psc == NULL)
		return ENXIO;
	sc = &psc->sc_core;
	mutex_enter(&psc->sc_lock);
	st = &sc->sc_stats;
	n = snprintf(buf, len, "running %d detaching %d "
	    "power_dirty %d power_error %d dma_error %d "
	    "intr %llu rx %llu c2h %llu "
	    "err %llu short %llu tag %llu dma %llu last_c2h %#x/%u "
	    "txq %llu txdone %llu txfull %llu txdrop %llu txdma %llu",
	    sc->sc_running, psc->sc_detaching, sc->sc_power_dirty,
	    sc->sc_power_error, sc->sc_dma_error,
	    (unsigned long long)st->intr,
	    (unsigned long long)st->rx_frames, (unsigned long long)st->rx_c2h,
	    (unsigned long long)st->rx_errors, (unsigned long long)st->rx_short,
	    (unsigned long long)st->rx_tag_errors,
	    (unsigned long long)st->rx_dma_errors, st->last_c2h_id,
	    st->last_c2h_seq, (unsigned long long)st->tx_queued,
	    (unsigned long long)st->tx_done, (unsigned long long)st->tx_full,
	    (unsigned long long)st->tx_dropped,
	    (unsigned long long)st->tx_dma_errors);
	if (n > 0 && (size_t)n < len)
		n += snprintf(buf + n, len - (size_t)n,
		    " ch %u bcn %llu", sc->sc_channel,
		    (unsigned long long)st->rx_beacons);
	if (n > 0 && (size_t)n < len && rtw88_diag(sc, r) == 0)
		n += snprintf(buf + n, len - (size_t)n,
		    " hisr0 %#x himr0 %#x hisr3 %#x himr3 %#x rxidx %#x pci %#x"
		    " rf18a %#x rf18b %#x",
		    r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7]);
	if (n > 0 && (size_t)n < len)
		n += snprintf(buf + n, len - (size_t)n,
		    " basic %#x txdata %llu txrate %u txbasic %#x txother %llu",
		    sc->sc_basic_rates, (unsigned long long)st->tx_data_encoded,
		    st->tx_data_rate, st->tx_data_basic,
		    (unsigned long long)st->tx_data_other);
	for (unsigned i = 0; i < __arraycount(st->tx_data_rates) &&
	    n > 0 && (size_t)n < len; i++)
		n += snprintf(buf + n, len - (size_t)n, " txr%u %llu", i,
		    (unsigned long long)st->tx_data_rates[i]);
	for (unsigned i = 0; i < __arraycount(st->tx_posted) &&
	    n > 0 && (size_t)n < len; i++)
		n += snprintf(buf + n, len - (size_t)n, " txpost%u %llu", i,
		    (unsigned long long)st->tx_posted[i]);
	/* Descriptor seeds and firmware reports are separate observations. */
	if (n > 0 && (size_t)n < len)
		n += snprintf(buf + n, len - (size_t)n,
		    " fwra_tx %llu protection_tx %llu ra_commands %llu ra_errors %llu"
		    " ra_reports %llu unattributed %llu malformed %llu foreign %llu"
		    " unsupported %llu inactive %llu trailing %llu outside_mask %llu"
		    " ra_observed_gen %u ra_last %u/%u/%u/%u"
		    " ra_config %d/%d/%d ra_error %d",
		    (unsigned long long)st->tx_ra_encoded,
		    (unsigned long long)st->tx_ra_protection_limited,
		    (unsigned long long)st->ra_commands,
		    (unsigned long long)st->ra_command_errors,
		    (unsigned long long)st->ra_reports,
		    (unsigned long long)st->ra_unattributed,
		    (unsigned long long)st->ra_malformed,
		    (unsigned long long)st->ra_foreign,
		    (unsigned long long)st->ra_unsupported,
		    (unsigned long long)st->ra_inactive,
		    (unsigned long long)st->ra_trailing,
		    (unsigned long long)st->ra_outside_mask,
		    st->ra_observed_generation, st->ra_report_valid,
		    st->ra_report_rate, st->ra_report_macid, st->ra_report_bw,
		    sc->sc_ra.configured, sc->sc_ra.active,
		    sc->sc_ra.protection_limited, sc->sc_ra.error);
	/* Firmware boot claim, not negotiated PHY or measured radio capacity. */
	if (n > 0 && (size_t)n < len)
		n += snprintf(buf + n, len - (size_t)n,
		    " hw_requests %llu hw_reports %llu hw_missing %llu"
		    " hw_foreign %llu hw_unstable %llu hw_io_errors %llu"
		    " hw_requested %u/%u hw_observed %u/%u hw_known %#x"
		    " hw_raw %u/%u/%u/%u/%u hw_bandwidths %#x",
		    (unsigned long long)st->hw_requests,
		    (unsigned long long)st->hw_reports,
		    (unsigned long long)st->hw_missing,
		    (unsigned long long)st->hw_foreign,
		    (unsigned long long)st->hw_unstable,
		    (unsigned long long)st->hw_io_errors,
		    sc->sc_hw_feature.requested,
		    sc->sc_hw_feature.requested_generation,
		    sc->sc_hw_feature.observed,
		    sc->sc_hw_feature.observed_generation,
		    sc->sc_hw_feature.report.known,
		    sc->sc_hw_feature.report.hci, sc->sc_hw_feature.report.bw,
		    sc->sc_hw_feature.report.nss, sc->sc_hw_feature.report.antennas,
		    sc->sc_hw_feature.report.protocol,
		    sc->sc_hw_feature.report.bandwidths);
	if (n > 0 && (size_t)n < len && psc->sc_net != NULL) {
		rtw88_net_get_stats(psc->sc_net, &ns);
		n += snprintf(buf + n, len - (size_t)n,
		    " nrx %llu ninput %llu ninactive %llu nepoch %llu"
		    " ninvalid %llu nsize %llu ncorrupt %llu nformat %llu"
		    " ncrypto %llu nfull %llu nmem %llu ndinactive %llu"
		    " ndepoch %llu nchained %llu nfirstshort %llu"
		    " nbeacon %u chanmismatch %u badchan %u"
		    " elem_missing %u elem_toobig %u elem_toosmall %u"
		    " nodealloc %u mgtdiscard %u",
		    (unsigned long long)ns.rx.accepted,
		    (unsigned long long)ns.rx.delivered,
		    (unsigned long long)ns.rx.inactive,
		    (unsigned long long)ns.rx.epoch,
		    (unsigned long long)ns.rx.invalid,
		    (unsigned long long)ns.rx.size,
		    (unsigned long long)ns.rx.corrupt,
		    (unsigned long long)ns.rx.format,
		    (unsigned long long)ns.rx.crypto,
		    (unsigned long long)ns.rx.full,
		    (unsigned long long)ns.nomem,
		    (unsigned long long)ns.rx.delivery_inactive,
		    (unsigned long long)ns.rx.delivery_epoch,
		    (unsigned long long)ns.chained,
		    (unsigned long long)ns.first_short,
		    ns.beacon, ns.chanmismatch, ns.badchan, ns.elem_missing,
		    ns.elem_toobig, ns.elem_toosmall, ns.nodealloc, ns.mgtdiscard);
	}
	mutex_exit(&psc->sc_lock);
	mutex_exit(&rtwce_diag_lock);
	return n < 0 || (size_t)n >= len ? ENOSPC : 0;
}

/* Operator channel switch (20 MHz); 0 reads nothing back. */
int
rtwce_channel(int *ch, int set)
{
	struct rtw88_pci_softc *psc;
	struct rtw88_softc *sc;
	int error = 0;

	psc = rtwce_diag_enter();
	if (psc == NULL)
		return ENXIO;
	sc = &psc->sc_core;
	mutex_enter(&psc->sc_lock);
	if (!set) {
		*ch = (int)sc->sc_channel;
		mutex_exit(&psc->sc_lock);
		mutex_exit(&rtwce_diag_lock);
		return 0;
	}
	if (psc->sc_net != NULL || psc->sc_detaching)
		error = EBUSY;
	else if (*ch <= 0)
		error = EINVAL;
	else
		error = rtw88_channel(sc, (unsigned)*ch);
	mutex_exit(&psc->sc_lock);
	mutex_exit(&rtwce_diag_lock);
	return error;
}

/* Diagnostic loopback mode; see rtw88_loopback. */
int
rtwce_lbmode(int mode)
{
	struct rtw88_pci_softc *psc;
	struct rtw88_softc *sc;
	int error;

	psc = rtwce_diag_enter();
	if (psc == NULL)
		return ENXIO;
	sc = &psc->sc_core;
	mutex_enter(&psc->sc_lock);
	error = psc->sc_net != NULL || psc->sc_detaching ? EBUSY :
	    rtw88_loopback(sc, mode);
	mutex_exit(&psc->sc_lock);
	mutex_exit(&rtwce_diag_lock);
	return error;
}

/* Diagnostic TX: post n broadcast action frames on the management queue. */
int
rtwce_loopback(int n)
{
	static const uint8_t frame[24] = {
		0xd0, 0x00, 0x00, 0x00,
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		0x02, 0x00, 0x00, 0x00, 0x00, 0x01,
		0x02, 0x00, 0x00, 0x00, 0x00, 0x01,
		0x00, 0x00
	};
	struct rtw88_pci_softc *psc;
	struct rtw88_softc *sc;
	int error, i;

	psc = rtwce_diag_enter();
	if (psc == NULL)
		return ENXIO;
	sc = &psc->sc_core;
	mutex_enter(&psc->sc_lock);
	if (psc->sc_net != NULL || psc->sc_detaching) {
		mutex_exit(&psc->sc_lock);
		mutex_exit(&rtwce_diag_lock);
		return EBUSY;
	}
	if (n < 0 || n > (int)RTW88_TX_SLOTS) {
		mutex_exit(&psc->sc_lock);
		mutex_exit(&rtwce_diag_lock);
		return EINVAL;
	}
	error = 0;
	for (i = 0; i < n && error == 0; i++)
		error = rtw88_tx_raw(sc, RTW88_TXQ_MGMT, frame,
		    sizeof(frame));
	mutex_exit(&psc->sc_lock);
	mutex_exit(&rtwce_diag_lock);
	return error;
}

/* Diagnostic H2C: low byte is the command, the next three its payload. */
int
rtwce_h2c(uint32_t cmd)
{
	struct rtw88_pci_softc *psc;
	struct rtw88_softc *sc;
	uint8_t payload[3];
	int error;

	psc = rtwce_diag_enter();
	if (psc == NULL)
		return ENXIO;
	sc = &psc->sc_core;
	payload[0] = (uint8_t)(cmd >> 8);
	payload[1] = (uint8_t)(cmd >> 16);
	payload[2] = (uint8_t)(cmd >> 24);
	mutex_enter(&psc->sc_lock);
	error = psc->sc_net != NULL || psc->sc_detaching ? EBUSY :
	    rtw88_fw_command(sc, (uint8_t)cmd, payload, sizeof(payload));
	mutex_exit(&psc->sc_lock);
	mutex_exit(&rtwce_diag_lock);
	return error;
}

static int
rtw88_pci_present(struct rtw88_pci_softc *psc)
{
	return pci_conf_read(psc->sc_pa.pa_pc, psc->sc_pa.pa_tag,
	    PCI_ID_REG) == psc->sc_pa.pa_id;
}

static int
rtw88_pci_access(struct rtw88_pci_softc *psc, uint32_t reg, size_t width)
{
	if ((reg & (width - 1)) != 0 || reg > psc->sc_mapsize ||
	    width > psc->sc_mapsize - reg)
		return EINVAL;
	if (!psc->sc_mapped || !rtw88_pci_present(psc))
		return ENXIO;
	return 0;
}

/*
 * bus_space has no status result. Check PCI presence around each access.
 * All-ones register values alone are not proof of removal. These methods
 * cannot recover a synchronous CPU bus fault; see the provenance limits.
 * The caller must serialize hardware commands against detach.
 */
#define RTW88_PCI_IO(width, type) \
static int \
rtw88_pci_read_##width(void *cookie, uint32_t reg, type *out) \
{ \
	struct rtw88_pci_softc *psc = cookie; \
	type value; \
	int error; \
	if (out == NULL) \
		return EINVAL; \
	error = rtw88_pci_access(psc, reg, width); \
	if (error != 0) \
		return error; \
	value = bus_space_read_##width(psc->sc_st, psc->sc_sh, reg); \
	if (!rtw88_pci_present(psc)) \
		return ENXIO; \
	*out = value; \
	return 0; \
} \
static int \
rtw88_pci_write_##width(void *cookie, uint32_t reg, type value) \
{ \
	struct rtw88_pci_softc *psc = cookie; \
	int error = rtw88_pci_access(psc, reg, width); \
	if (error != 0) \
		return error; \
	bus_space_write_##width(psc->sc_st, psc->sc_sh, reg, value); \
	bus_space_barrier(psc->sc_st, psc->sc_sh, reg, width, \
	    BUS_SPACE_BARRIER_WRITE); \
	return rtw88_pci_present(psc) ? 0 : ENXIO; \
}
RTW88_PCI_IO(1, uint8_t)
RTW88_PCI_IO(2, uint16_t)
RTW88_PCI_IO(4, uint32_t)
#undef RTW88_PCI_IO

static void
rtw88_pci_delay(void *cookie, unsigned us)
{
	(void)cookie;
	delay(us);
}

static int
rtw88_pci_mask(struct rtw88_pci_softc *psc)
{
	static const uint32_t regs[] = {
		RTW88_PCI_HIMR0, RTW88_PCI_HIMR1, RTW88_PCI_HIMR3
	};
	uint32_t value;

	for (unsigned i = 0; i < __arraycount(regs); i++) {
		int error = rtw88_pci_write_4(psc, regs[i], 0);
		if (error != 0)
			return error;
		/* Readback flushes posted writes and verifies the critical mask. */
		error = rtw88_pci_read_4(psc, regs[i], &value);
		if (error != 0)
			return error;
		if (value != 0)
			return EIO;
	}
	return 0;
}

static int
rtw88_pci_dma_alloc(void *cookie, struct rtw88_dma *ring, size_t size)
{
	struct rtw88_pci_softc *psc = cookie;
	struct rtw88_pci_dma *dma;
	void *kva;
	int error, nsegs;

	dma = kmem_zalloc(sizeof(*dma), KM_NOSLEEP);
	if (dma == NULL)
		return ENOMEM;
	error = bus_dmamap_create(psc->sc_dmat, size, 1, size, 0,
	    BUS_DMA_NOWAIT, &dma->map);
	if (error != 0)
		goto free_cookie;
	error = bus_dmamem_alloc(psc->sc_dmat, size, RTW88_PCI_RING_ALIGN,
	    0, &dma->seg, 1, &nsegs, BUS_DMA_NOWAIT);
	if (error != 0)
		goto destroy_map;
	error = bus_dmamem_map(psc->sc_dmat, &dma->seg, nsegs, size,
	    &kva, BUS_DMA_NOWAIT | BUS_DMA_COHERENT);
	if (error != 0)
		goto free_mem;
	error = bus_dmamap_load(psc->sc_dmat, dma->map, kva, size,
	    NULL, BUS_DMA_NOWAIT);
	if (error != 0)
		goto unmap_mem;
	if (dma->map->dm_nsegs != 1 || dma->map->dm_mapsize != size ||
	    dma->map->dm_segs[0].ds_len != size ||
	    (dma->map->dm_segs[0].ds_addr & (RTW88_PCI_RING_ALIGN - 1))) {
		error = EINVAL;
		goto unload_map;
	}
	error = rtw88_dma_validate(dma->map->dm_segs[0].ds_addr, size,
	    RTW8822C_DMA_ADDR_BITS);
	if (error != 0)
		goto unload_map;
	memset(kva, 0, size);
	ring->cookie = dma;
	ring->vaddr = kva;
	ring->addr = dma->map->dm_segs[0].ds_addr;
	ring->size = size;
	return 0;
unload_map:
	bus_dmamap_unload(psc->sc_dmat, dma->map);
unmap_mem:
	bus_dmamem_unmap(psc->sc_dmat, kva, size);
free_mem:
	bus_dmamem_free(psc->sc_dmat, &dma->seg, nsegs);
destroy_map:
	bus_dmamap_destroy(psc->sc_dmat, dma->map);
free_cookie:
	kmem_free(dma, sizeof(*dma));
	return error;
}

static void
rtw88_pci_dma_free(void *cookie, struct rtw88_dma *ring)
{
	struct rtw88_pci_softc *psc = cookie;
	struct rtw88_pci_dma *dma = ring->cookie;

	bus_dmamap_unload(psc->sc_dmat, dma->map);
	bus_dmamem_unmap(psc->sc_dmat, ring->vaddr, ring->size);
	bus_dmamem_free(psc->sc_dmat, &dma->seg, 1);
	bus_dmamap_destroy(psc->sc_dmat, dma->map);
	kmem_free(dma, sizeof(*dma));
	memset(ring, 0, sizeof(*ring));
}

static void
rtw88_pci_dma_sync(void *cookie, struct rtw88_dma *ring, int ops)
{
	struct rtw88_pci_softc *psc = cookie;
	struct rtw88_pci_dma *dma = ring->cookie;

	bus_dmamap_sync(psc->sc_dmat, dma->map, 0, ring->size, ops);
}

static int
rtw88_pci_intr(void *arg)
{
	struct rtw88_pci_softc *psc = arg;

	int handled = 0;

	mutex_enter(&psc->sc_lock);
	/*
	 * rtw88_intr touches nothing unless the core unmasked interrupts
	 * and one of its causes is pending, so a shared INTx is left alone.
	 */
	if (!psc->sc_stopping)
		handled = rtw88_intr(&psc->sc_core);
	mutex_exit(&psc->sc_lock);
	return handled;
}

/*
 * rtw88_stop cleared sc_running and the masks before calling this. A
 * handler holding sc_lock finishes first; one already dispatched but
 * not yet locked sees sc_running == 0. xc_barrier covers the latter.
 */
static void
rtw88_pci_intr_barrier(void *cookie)
{
	struct rtw88_pci_softc *psc = cookie;

	mutex_enter(&psc->sc_lock);
	mutex_exit(&psc->sc_lock);
	xc_barrier(0);
}

static int
rtw88_pci_intr_establish(void *cookie)
{
	struct rtw88_pci_softc *psc = cookie;
	int counts[PCI_INTR_TYPE_SIZE] = { 0 };
	int error;

	/* Pinned PCI code requests one MSI or legacy INTx, never MSI-X. */
	counts[PCI_INTR_TYPE_INTX] = 1;
	counts[PCI_INTR_TYPE_MSI] = 1;
	error = pci_intr_alloc(&psc->sc_pa, &psc->sc_pih, counts,
	    PCI_INTR_TYPE_MSI);
	if (error != 0)
		return error;
	psc->sc_ih = pci_intr_establish_xname(psc->sc_pa.pa_pc,
	    psc->sc_pih[0], IPL_NET, rtw88_pci_intr, psc,
	    device_xname(psc->sc_dev));
	if (psc->sc_ih == NULL) {
		pci_intr_release(psc->sc_pa.pa_pc, psc->sc_pih, 1);
		psc->sc_pih = NULL;
		return EIO;
	}
	return 0;
}

static int
rtw88_pci_firmware_load(void *cookie, const char *drv, const char *name,
    const uint8_t **data, size_t *len)
{
	struct rtw88_pci_softc *psc = cookie;
	firmware_handle_t fh;
	size_t size;
	int error;

	error = firmware_open(drv, name, &fh);
	if (error != 0)
		return error;
	size = (size_t)firmware_get_size(fh);
	if (size == 0) {
		firmware_close(fh);
		return EINVAL;
	}
	if (psc->sc_fw != NULL) {
		kmem_free(psc->sc_fw, psc->sc_fwlen);
		psc->sc_fw = NULL;
		psc->sc_fwlen = 0;
	}
	psc->sc_fw = kmem_zalloc(size, KM_NOSLEEP);
	if (psc->sc_fw == NULL) {
		firmware_close(fh);
		return ENOMEM;
	}
	psc->sc_fwlen = size;
	error = firmware_read(fh, 0, psc->sc_fw, size);
	firmware_close(fh);
	if (error != 0) {
		kmem_free(psc->sc_fw, psc->sc_fwlen);
		psc->sc_fw = NULL;
		psc->sc_fwlen = 0;
		return error;
	}
	*data = psc->sc_fw;
	*len = psc->sc_fwlen;
	return 0;
}

static void
rtw88_pci_firmware_release(void *cookie)
{
	struct rtw88_pci_softc *psc = cookie;

	if (psc->sc_fw != NULL) {
		kmem_free(psc->sc_fw, psc->sc_fwlen);
		psc->sc_fw = NULL;
		psc->sc_fwlen = 0;
	}
}

static int
rtw88_pci_bus_master(void *cookie, int enable)
{
	struct rtw88_pci_softc *psc = cookie;
	pcireg_t command, want;

	if (!rtw88_pci_present(psc) || !psc->sc_command_saved)
		return ENXIO;
	command = pci_conf_read(psc->sc_pa.pa_pc, psc->sc_pa.pa_tag,
	    PCI_COMMAND_STATUS_REG);
	if (command == UINT32_MAX)
		return ENXIO;
	want = command & 0xffff;
	if (enable)
		want |= PCI_COMMAND_MASTER_ENABLE;
	else
		want &= ~PCI_COMMAND_MASTER_ENABLE;
	pci_conf_write(psc->sc_pa.pa_pc, psc->sc_pa.pa_tag,
	    PCI_COMMAND_STATUS_REG, want);
	command = pci_conf_read(psc->sc_pa.pa_pc, psc->sc_pa.pa_tag,
	    PCI_COMMAND_STATUS_REG);
	if (command == UINT32_MAX)
		return ENXIO;
	if (enable) {
		if ((command & PCI_COMMAND_MASTER_ENABLE) == 0)
			return EIO;
	} else if ((command & PCI_COMMAND_MASTER_ENABLE) != 0)
		return EIO;
	return 0;
}

static void
rtw88_pci_intr_disestablish(void *cookie)
{
	struct rtw88_pci_softc *psc = cookie;

	mutex_enter(&psc->sc_lock);
	psc->sc_stopping = 1;
	/* All masks were zero at attach and are never enabled in this stage. */
	if (psc->sc_mapped && rtw88_pci_present(psc))
		(void)rtw88_pci_mask(psc);
	mutex_exit(&psc->sc_lock);
	/* Remove future dispatch before draining already fetched handlers. */
	pci_intr_disestablish(psc->sc_pa.pa_pc, psc->sc_ih);
	/*
	 * A low-priority cross-call waits for IPL_NET activity on every CPU.
	 * No driver lock is held. If xc_wait sleeps, cv_wait releases BKL,
	 * allowing a pending non-MPSAFE handler to complete. Keep the softc,
	 * mutex and DMA alive until this barrier returns.
	 */
	xc_barrier(0);
	psc->sc_ih = NULL;
	pci_intr_release(psc->sc_pa.pa_pc, psc->sc_pih, 1);
	psc->sc_pih = NULL;
}

static const struct rtw88_bus_ops rtw88_pci_ops = {
	.read_1 = rtw88_pci_read_1,
	.read_2 = rtw88_pci_read_2,
	.read_4 = rtw88_pci_read_4,
	.write_1 = rtw88_pci_write_1,
	.write_2 = rtw88_pci_write_2,
	.write_4 = rtw88_pci_write_4,
	.delay_us = rtw88_pci_delay,
	.dma_alloc = rtw88_pci_dma_alloc,
	.dma_free = rtw88_pci_dma_free,
	.dma_sync = rtw88_pci_dma_sync,
	.intr_establish = rtw88_pci_intr_establish,
	.intr_disestablish = rtw88_pci_intr_disestablish,
	.intr_barrier = rtw88_pci_intr_barrier,
	.firmware_load = rtw88_pci_firmware_load,
	.firmware_release = rtw88_pci_firmware_release,
	.bus_master = rtw88_pci_bus_master
};

static int
rtw88_pci_match(device_t parent, cfdata_t match, void *aux)
{
	const struct pci_attach_args *pa = aux;

	(void)parent;
	(void)match;
	return PCI_VENDOR(pa->pa_id) == PCI_VENDOR_REALTEK &&
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_REALTEK_RTL8822CE;
}

static void
rtw88_pci_attach(device_t parent, device_t self, void *aux)
{
	struct rtw88_pci_softc *psc = device_private(self);
	struct rtw88_softc *sc = &psc->sc_core;
	const struct pci_attach_args *pa = aux;
	pcireg_t command, type;
	int error = ENXIO;

	(void)parent;
	psc->sc_dev = self;
	psc->sc_pa = *pa;
	sc->sc_bus = psc;
	sc->sc_ops = &rtw88_pci_ops;
	sc->sc_vendor = PCI_VENDOR(pa->pa_id);
	sc->sc_product = PCI_PRODUCT(pa->pa_id);
	aprint_normal(": Realtek RTL8822CE PCI resources\n");
	if (!rtw88_pci_present(psc))
		goto fail;
	command = pci_conf_read(pa->pa_pc, pa->pa_tag, PCI_COMMAND_STATUS_REG);
	if (command == UINT32_MAX || !rtw88_pci_present(psc))
		goto fail;
	/* Do not write status W1C bits or restore a firmware bus master. */
	psc->sc_command = (command & 0xffff) & ~PCI_COMMAND_MASTER_ENABLE;
	psc->sc_command_saved = 1;
	pci_conf_write(pa->pa_pc, pa->pa_tag, PCI_COMMAND_STATUS_REG,
	    psc->sc_command);
	command = pci_conf_read(pa->pa_pc, pa->pa_tag, PCI_COMMAND_STATUS_REG);
	if ((command & PCI_COMMAND_MASTER_ENABLE) != 0)
		goto fail;
	type = pci_mapreg_type(pa->pa_pc, pa->pa_tag, RTW88_PCI_BAR);
	if (PCI_MAPREG_TYPE(type) != PCI_MAPREG_TYPE_MEM)
		goto fail;
	error = pci_mapreg_map(pa, RTW88_PCI_BAR, type, 0,
	    &psc->sc_st, &psc->sc_sh, NULL, &psc->sc_mapsize);
	if (error != 0)
		goto fail;
	psc->sc_mapped = 1;
	if (psc->sc_mapsize < RTW88_PCI_MAP_MIN) {
		error = EINVAL;
		goto fail;
	}
	error = rtw88_pci_mask(psc);
	if (error != 0)
		goto fail;
	/* This is mandatory. Never fall back to an unconstrained parent. */
	error = bus_dmatag_subregion(pa->pa_dmat, 0, UINT32_MAX,
	    &psc->sc_dmat, BUS_DMA_NOWAIT);
	if (error != 0)
		goto fail;
	mutex_init(&psc->sc_lock, MUTEX_DEFAULT, IPL_NET);
	psc->sc_lock_initialized = 1;
	error = rtw88_core_attach(sc);
	if (error != 0)
		goto fail;
	/*
	 * Identify reads physical EFUSE through its command register. It does
	 * not start the radio, publish DMA, or load firmware.
	 */
	error = rtw8822c_identify(sc);
	if (error != 0)
		goto fail;
	error = rtw88_net_attach(sc, self, &psc->sc_lock, sc->sc_mac,
	    &psc->sc_net);
	if (error != 0)
		goto fail;
	(void)RUN_ONCE(&rtwce_diag_once, rtwce_diag_init);
	mutex_enter(&rtwce_diag_lock);
	rtwce_sc = psc;
	mutex_exit(&rtwce_diag_lock);
	aprint_normal_dev(self,
	    "station attached; radio and DMA remain disabled until interface init\n");
	return;
fail:
	aprint_error_dev(self, "resource attachment failed: %d\n", error);
	(void)rtw88_pci_detach(self, 0);
}

static int
rtw88_pci_detach(device_t self, int flags)
{
	struct rtw88_pci_softc *psc = device_private(self);
	int error;

	(void)flags;
	/* Wait for an entered diagnostic, then block new diagnostics. */
	(void)RUN_ONCE(&rtwce_diag_once, rtwce_diag_init);
	mutex_enter(&rtwce_diag_lock);
	psc->sc_detaching = 1;
	if (rtwce_sc == psc)
		rtwce_sc = NULL;
	mutex_exit(&rtwce_diag_lock);
	if (psc->sc_net != NULL) {
		rtw88_net_detach(psc->sc_net);
		psc->sc_net = NULL;
	}
	error = rtw88_core_detach(&psc->sc_core);
	if (error != 0) {
		/* Keep BAR, tags, IRQ and the instance lock alive for a retry. */
		mutex_enter(&rtwce_diag_lock);
		if (rtwce_sc == NULL)
			rtwce_sc = psc;
		mutex_exit(&rtwce_diag_lock);
		aprint_error_dev(self, "detach blocked by incomplete shutdown: %d\n", error);
		return error;
	}
	if (psc->sc_lock_initialized) {
		mutex_destroy(&psc->sc_lock);
		psc->sc_lock_initialized = 0;
	}
	if (psc->sc_dmat != NULL) {
		bus_dmatag_destroy(psc->sc_dmat);
		psc->sc_dmat = NULL;
	}
	if (psc->sc_mapped) {
		bus_space_unmap(psc->sc_st, psc->sc_sh, psc->sc_mapsize);
		psc->sc_mapped = 0;
	}
	if (psc->sc_command_saved) {
		/* Released DMA mappings must never regain firmware bus mastering. */
		if (rtw88_pci_present(psc))
			pci_conf_write(psc->sc_pa.pa_pc, psc->sc_pa.pa_tag,
			    PCI_COMMAND_STATUS_REG,
			    psc->sc_command & ~PCI_COMMAND_MASTER_ENABLE);
		psc->sc_command_saved = 0;
	}
	return 0;
}

CFATTACH_DECL3_NEW(rtw88_pci, sizeof(struct rtw88_pci_softc),
    rtw88_pci_match, rtw88_pci_attach, rtw88_pci_detach, NULL, NULL, NULL,
    DVF_DETACH_SHUTDOWN);
