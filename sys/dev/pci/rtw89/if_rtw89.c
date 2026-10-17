/*	$NetBSD$	*/

/*-
 * Copyright (c) 2026 The EmberBSD Project.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Driver for the Realtek RTL8852BE 802.11ax PCIe WLAN adapter.
 *
 * The hardware support comes from the Linux rtw89 driver (dual licensed
 * GPL-2.0 OR BSD-3-Clause), compiled against a small Linux API emulation;
 * see rtw89_compat.h and rtw89_bridge.c.  This file attaches the device to
 * autoconf and net80211.  Only station mode with legacy (11a/b/g) rates is
 * offered; WPA/WPA2 use the software crypto of net80211.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/param.h>
#include <sys/types.h>
#include <sys/bus.h>
#include <sys/callout.h>
#include <sys/cpu.h>
#include <sys/device.h>
#include <sys/intr.h>
#include <sys/kernel.h>
#include <sys/kmem.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <prop/proplib.h>
#include <sys/mutex.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/workqueue.h>

#include <net/bpf.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_ether.h>
#include <net/if_media.h>

#include <net80211/ieee80211_var.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include "rtw89_nb.h"

#define RTW89_PCI_VENDOR	0x10ec
#define RTW89_PCI_BAR		PCI_BAR(2)
#define RTW89_TXBUF_SIZE	4096
#define RTW89_SCAN_DWELL_MS	200

typedef int (*rtw89_irq_fn)(int, void *);

struct rtw89_softc {
	device_t		 sc_dev;
	struct ethercom		 sc_ec;
	struct ieee80211com	 sc_ic;
	int			(*sc_newstate)(struct ieee80211com *,
				    enum ieee80211_state, int);

	pci_chipset_tag_t	 sc_pc;
	pcitag_t		 sc_tag;
	bus_dma_tag_t		 sc_dmat;
	bus_space_tag_t		 sc_st;
	bus_space_handle_t	 sc_sh;
	bus_size_t		 sc_sz;
	pci_intr_handle_t	*sc_pihp;
	void			*sc_ih;
	void			*sc_thread_sih;
	rtw89_irq_fn		 sc_irq_handler;
	rtw89_irq_fn		 sc_irq_thread;
	void			*sc_irq_arg;
	uint16_t		 sc_product;

	struct rtw89_nb		*sc_nb;
	bool			 sc_running;
	volatile bool		 sc_txstopped;
	uint8_t			*sc_txbuf;
	void			*sc_tx_sih;
	void			*sc_linklost_sih;

	struct workqueue	*sc_nswq;
	struct work		 sc_nswk;
	kmutex_t		 sc_nslock;
	bool			 sc_nspending;
	enum ieee80211_state	 sc_nsstate;
	int			 sc_nsarg;
	struct callout		 sc_scan_to;
	bool			 sc_scanning;
};

static int	rtw89_match(device_t, cfdata_t, void *);
static void	rtw89_attach(device_t, device_t, void *);
static int	rtw89_detach(device_t, int);
static void	rtw89_attach_hook(device_t);

static int	rtw89_init(struct ifnet *);
static void	rtw89_stop(struct ifnet *, int);
static void	rtw89_start(struct ifnet *);
static int	rtw89_ioctl(struct ifnet *, u_long, void *);
static void	rtw89_watchdog(struct ifnet *);
static int	rtw89_newstate(struct ieee80211com *, enum ieee80211_state,
		    int);
static void	rtw89_newstate_task(struct work *, void *);
static void	rtw89_next_scan(void *);
static void	rtw89_tx_softint(void *);
static void	rtw89_linklost_softint(void *);
static int	rtw89_intr(void *);
static void	rtw89_intr_thread(void *);

CFATTACH_DECL_NEW(rtwx, sizeof(struct rtw89_softc),
    rtw89_match, rtw89_attach, rtw89_detach, NULL);

static int rtw89_debug;

static const uint16_t rtw89_products[] = {
	0xb852,		/* RTL8852BE */
	0xb85b,		/* RTL8852BE (variant) */
};

static int
rtw89_match(device_t parent, cfdata_t match, void *aux)
{
	struct pci_attach_args *pa = aux;
	size_t i;

	if (PCI_VENDOR(pa->pa_id) != RTW89_PCI_VENDOR)
		return 0;
	for (i = 0; i < __arraycount(rtw89_products); i++)
		if (PCI_PRODUCT(pa->pa_id) == rtw89_products[i])
			return 1;
	return 0;
}

static void
rtw89_attach(device_t parent, device_t self, void *aux)
{
	struct rtw89_softc *sc = device_private(self);
	struct pci_attach_args *pa = aux;
	int counts[PCI_INTR_TYPE_SIZE];
	pcireg_t memtype, csr;
	int error;

	sc->sc_dev = self;
	sc->sc_pc = pa->pa_pc;
	sc->sc_tag = pa->pa_tag;
	sc->sc_product = PCI_PRODUCT(pa->pa_id);

	pci_aprint_devinfo(pa, "802.11ax wireless");

	/* The DMA engine only takes 32-bit addresses. */
	if (bus_dmatag_subregion(pa->pa_dmat, 0, 0xffffffff, &sc->sc_dmat,
	    BUS_DMA_WAITOK) != 0)
		sc->sc_dmat = pa->pa_dmat;

	pci_set_powerstate(sc->sc_pc, sc->sc_tag, PCI_PMCSR_STATE_D0);

	memtype = pci_mapreg_type(sc->sc_pc, sc->sc_tag, RTW89_PCI_BAR);
	error = pci_mapreg_map(pa, RTW89_PCI_BAR, memtype,
	    BUS_SPACE_MAP_LINEAR, &sc->sc_st, &sc->sc_sh, NULL, &sc->sc_sz);
	if (error) {
		aprint_error_dev(self, "can't map registers\n");
		return;
	}
	csr = pci_conf_read(sc->sc_pc, sc->sc_tag, PCI_COMMAND_STATUS_REG);
	csr |= PCI_COMMAND_MEM_ENABLE | PCI_COMMAND_MASTER_ENABLE;
	pci_conf_write(sc->sc_pc, sc->sc_tag, PCI_COMMAND_STATUS_REG, csr);

	memset(counts, 0, sizeof(counts));
	counts[PCI_INTR_TYPE_MSI] = 1;
	counts[PCI_INTR_TYPE_INTX] = 1;
	if (pci_intr_alloc(pa, &sc->sc_pihp, counts, PCI_INTR_TYPE_MSI)) {
		aprint_error_dev(self, "can't allocate interrupt\n");
		goto fail_unmap;
	}

	if (rtw89_compat_init(sc->sc_dmat) != 0) {
		aprint_error_dev(self, "can't allocate DMA buffers\n");
		goto fail_intr;
	}

	sc->sc_txbuf = kmem_alloc(RTW89_TXBUF_SIZE, KM_SLEEP);
	mutex_init(&sc->sc_nslock, MUTEX_DEFAULT, IPL_NET);
	callout_init(&sc->sc_scan_to, 0);
	callout_setfunc(&sc->sc_scan_to, rtw89_next_scan, sc);
	sc->sc_tx_sih = softint_establish(SOFTINT_NET, rtw89_tx_softint, sc);
	sc->sc_linklost_sih = softint_establish(SOFTINT_NET,
	    rtw89_linklost_softint, sc);
	if (workqueue_create(&sc->sc_nswq, "rtw89ns", rtw89_newstate_task, sc,
	    PRI_NONE, IPL_NET, 0) != 0) {
		aprint_error_dev(self, "can't create workqueue\n");
		goto fail_intr;
	}

	/* The firmware is read from the root file system. */
	config_mountroot(self, rtw89_attach_hook);
	return;

fail_intr:
	pci_intr_release(sc->sc_pc, sc->sc_pihp, 1);
	sc->sc_pihp = NULL;
fail_unmap:
	bus_space_unmap(sc->sc_st, sc->sc_sh, sc->sc_sz);
	sc->sc_sz = 0;
}

static void
rtw89_add_channels(struct ieee80211com *ic)
{
	static const uint8_t chan5[] = {
		36, 40, 44, 48, 52, 56, 60, 64,
		100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144,
		149, 153, 157, 161, 165,
	};
	size_t i;
	int c;

	for (c = 1; c <= 13; c++) {
		ic->ic_channels[c].ic_freq =
		    ieee80211_ieee2mhz(c, IEEE80211_CHAN_2GHZ);
		ic->ic_channels[c].ic_flags = IEEE80211_CHAN_CCK |
		    IEEE80211_CHAN_OFDM | IEEE80211_CHAN_DYN |
		    IEEE80211_CHAN_2GHZ;
	}
	for (i = 0; i < __arraycount(chan5); i++) {
		c = chan5[i];
		ic->ic_channels[c].ic_freq =
		    ieee80211_ieee2mhz(c, IEEE80211_CHAN_5GHZ);
		ic->ic_channels[c].ic_flags = IEEE80211_CHAN_A;
		/* DFS channels: listen before transmitting */
		if (c >= 52 && c <= 144)
			ic->ic_channels[c].ic_flags |= IEEE80211_CHAN_PASSIVE;
	}
}

static void
rtw89_attach_hook(device_t self)
{
	struct rtw89_softc *sc = device_private(self);
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &sc->sc_ec.ec_if;
	struct rtw89_nb_attach_args aa;
	int error;

	memset(&aa, 0, sizeof(aa));
	aa.aa_dev = self;
	aa.aa_glue = sc;
	aa.aa_mmio = bus_space_vaddr(sc->sc_st, sc->sc_sh);
	aa.aa_mmio_size = sc->sc_sz;
	aa.aa_device_id = sc->sc_product;
	if (aa.aa_mmio == NULL) {
		aprint_error_dev(self, "registers are not linearly mapped\n");
		return;
	}
	error = rtw89_nb_attach(&aa, &sc->sc_nb);
	if (error) {
		aprint_error_dev(self, "hardware initialization failed (%d)\n",
		    error);
		sc->sc_nb = NULL;
		return;
	}
	rtw89_nb_get_macaddr(sc->sc_nb, ic->ic_myaddr);

	ic->ic_ifp = ifp;
	ic->ic_phytype = IEEE80211_T_OFDM;
	ic->ic_opmode = IEEE80211_M_STA;
	ic->ic_state = IEEE80211_S_INIT;
	ic->ic_caps = IEEE80211_C_WPA | IEEE80211_C_SHPREAMBLE |
	    IEEE80211_C_SHSLOT;
	ic->ic_sup_rates[IEEE80211_MODE_11A] = ieee80211_std_rateset_11a;
	ic->ic_sup_rates[IEEE80211_MODE_11B] = ieee80211_std_rateset_11b;
	ic->ic_sup_rates[IEEE80211_MODE_11G] = ieee80211_std_rateset_11g;
	rtw89_add_channels(ic);

	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_init = rtw89_init;
	ifp->if_stop = rtw89_stop;
	ifp->if_ioctl = rtw89_ioctl;
	ifp->if_start = rtw89_start;
	ifp->if_watchdog = rtw89_watchdog;
	IFQ_SET_READY(&ifp->if_snd);
	memcpy(ifp->if_xname, device_xname(self), IFNAMSIZ);

	if_initialize(ifp);
	ieee80211_ifattach(ic);
	ifp->if_percpuq = if_percpuq_create(ifp);
	if_register(ifp);

	sc->sc_newstate = ic->ic_newstate;
	ic->ic_newstate = rtw89_newstate;
	ieee80211_media_init(ic, ieee80211_media_change,
	    ieee80211_media_status);
	ieee80211_announce(ic);

	aprint_normal_dev(self, "address %s\n", ether_sprintf(ic->ic_myaddr));
	if (!pmf_device_register(self, NULL, NULL))
		aprint_error_dev(self, "couldn't establish power handler\n");
}

static int
rtw89_detach(device_t self, int flags)
{
	struct rtw89_softc *sc = device_private(self);
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &sc->sc_ec.ec_if;

	if (sc->sc_nb != NULL) {
		if (ifp->if_softc != NULL) {
			rtw89_stop(ifp, 1);
			pmf_device_deregister(self);
			ieee80211_ifdetach(ic);
			if_detach(ifp);
			ifp->if_softc = NULL;
		}
		rtw89_nb_detach(sc->sc_nb);
		sc->sc_nb = NULL;
	}
	if (sc->sc_nswq != NULL) {
		workqueue_destroy(sc->sc_nswq);
		sc->sc_nswq = NULL;
	}
	callout_halt(&sc->sc_scan_to, NULL);
	callout_destroy(&sc->sc_scan_to);
	if (sc->sc_tx_sih != NULL)
		softint_disestablish(sc->sc_tx_sih);
	if (sc->sc_linklost_sih != NULL)
		softint_disestablish(sc->sc_linklost_sih);
	if (sc->sc_txbuf != NULL) {
		kmem_free(sc->sc_txbuf, RTW89_TXBUF_SIZE);
		mutex_destroy(&sc->sc_nslock);
	}
	if (sc->sc_ih != NULL)
		rtw89_nb_free_irq(self);
	if (sc->sc_pihp != NULL)
		pci_intr_release(sc->sc_pc, sc->sc_pihp, 1);
	if (sc->sc_sz != 0)
		bus_space_unmap(sc->sc_st, sc->sc_sh, sc->sc_sz);
	rtw89_compat_fini();
	return 0;
}

/* ------------------------------------------------------------------ */
/* Services for the Linux side                                         */
/* ------------------------------------------------------------------ */

int
rtw89_nb_request_irq(struct device *dev, rtw89_irq_fn handler,
    rtw89_irq_fn thread, void *arg)
{
	struct rtw89_softc *sc = device_private(dev);
	char intrbuf[PCI_INTRSTR_LEN];
	const char *intrstr;

	if (sc->sc_ih != NULL)
		return -EBUSY;
	sc->sc_irq_handler = handler;
	sc->sc_irq_thread = thread;
	sc->sc_irq_arg = arg;
	sc->sc_thread_sih = softint_establish(SOFTINT_NET, rtw89_intr_thread,
	    sc);
	if (sc->sc_thread_sih == NULL)
		return -ENOMEM;
	intrstr = pci_intr_string(sc->sc_pc, sc->sc_pihp[0], intrbuf,
	    sizeof(intrbuf));
	sc->sc_ih = pci_intr_establish_xname(sc->sc_pc, sc->sc_pihp[0],
	    IPL_NET, rtw89_intr, sc, device_xname(dev));
	if (sc->sc_ih == NULL) {
		softint_disestablish(sc->sc_thread_sih);
		sc->sc_thread_sih = NULL;
		aprint_error_dev(dev, "can't establish interrupt\n");
		return -EIO;
	}
	aprint_normal_dev(dev, "interrupting at %s\n", intrstr);
	return 0;
}

void
rtw89_nb_free_irq(struct device *dev)
{
	struct rtw89_softc *sc = device_private(dev);

	if (sc->sc_ih != NULL) {
		pci_intr_disestablish(sc->sc_pc, sc->sc_ih);
		sc->sc_ih = NULL;
	}
	if (sc->sc_thread_sih != NULL) {
		softint_disestablish(sc->sc_thread_sih);
		sc->sc_thread_sih = NULL;
	}
	sc->sc_irq_handler = NULL;
	sc->sc_irq_thread = NULL;
}

void
rtw89_nb_synchronize_irq(struct device *dev)
{

	/* let a running handler and the scheduled softint drain */
	if (!cold && !cpu_intr_p() && !cpu_softintr_p())
		kpause("rtw89irq", false, MAX(1, mstohz(2)), NULL);
}

static int
rtw89_intr(void *arg)
{
	struct rtw89_softc *sc = arg;
	int ret;

	if (sc->sc_irq_handler == NULL)
		return 0;
	ret = (*sc->sc_irq_handler)(0, sc->sc_irq_arg);
	if (ret == 2 /* IRQ_WAKE_THREAD */)
		softint_schedule(sc->sc_thread_sih);
	return ret != 0;
}

static void
rtw89_intr_thread(void *arg)
{
	struct rtw89_softc *sc = arg;

	if (sc->sc_irq_thread != NULL)
		(void)(*sc->sc_irq_thread)(0, sc->sc_irq_arg);
}

int
rtw89_nb_pci_cfg_read(struct device *dev, int off, int size, uint32_t *val)
{
	struct rtw89_softc *sc = device_private(dev);
	pcireg_t reg;
	int shift = (off & 3) * 8;

	reg = pci_conf_read(sc->sc_pc, sc->sc_tag, off & ~3);
	switch (size) {
	case 1:
		*val = (reg >> shift) & 0xff;
		break;
	case 2:
		*val = (reg >> shift) & 0xffff;
		break;
	default:
		*val = reg;
		break;
	}
	return 0;
}

int
rtw89_nb_pci_cfg_write(struct device *dev, int off, int size, uint32_t val)
{
	struct rtw89_softc *sc = device_private(dev);
	pcireg_t reg, mask;
	int shift = (off & 3) * 8;

	if (size == 4) {
		pci_conf_write(sc->sc_pc, sc->sc_tag, off, val);
		return 0;
	}
	mask = (size == 1 ? 0xffU : 0xffffU) << shift;
	reg = pci_conf_read(sc->sc_pc, sc->sc_tag, off & ~3);
	reg = (reg & ~mask) | ((val << shift) & mask);
	pci_conf_write(sc->sc_pc, sc->sc_tag, off & ~3, reg);
	return 0;
}

int
rtw89_nb_pci_capability(struct device *dev, int id, bool ext)
{
	struct rtw89_softc *sc = device_private(dev);
	int off = 0;

	if (ext) {
		if (!pci_get_ext_capability(sc->sc_pc, sc->sc_tag, id, &off,
		    NULL))
			return 0;
	} else {
		if (!pci_get_capability(sc->sc_pc, sc->sc_tag, id, &off, NULL))
			return 0;
	}
	return off;
}

/* Called from the NAPI softint with the kernel lock held. */
void
rtw89_nb_rx(void *arg, const void *frame, size_t len, int rssi,
    unsigned int freq)
{
	struct rtw89_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &sc->sc_ec.ec_if;
	struct ieee80211_node *ni;
	struct mbuf *m;
	int s;

	if (!sc->sc_running || len < sizeof(struct ieee80211_frame_min))
		return;
	if (rtw89_debug > 2) {
		const struct ieee80211_frame *wh = frame;

		/* cppcheck-suppress invalidPrintfArgType_uint ; len is size_t */
		printf("%s: rx fc %02x%02x len %zu rssi %d freq %u ta %s\n",
		    device_xname(sc->sc_dev), wh->i_fc[0], wh->i_fc[1], len,
		    rssi, freq, ether_sprintf(wh->i_addr2));
	}
	/* net80211 parses management frames in the first mbuf only. */
	if (len > MCLBYTES) {
		if_statinc(ifp, if_ierrors);
		return;
	}
	MGETHDR(m, M_DONTWAIT, MT_DATA);
	if (m != NULL && len > MHLEN) {
		MCLGET(m, M_DONTWAIT);
		if ((m->m_flags & M_EXT) == 0) {
			m_freem(m);
			m = NULL;
		}
	}
	if (m == NULL) {
		if_statinc(ifp, if_ierrors);
		return;
	}
	memcpy(mtod(m, void *), frame, len);
	m->m_pkthdr.len = m->m_len = len;
	m_set_rcvif(m, ifp);
	s = splnet();
	ni = ieee80211_find_rxnode(ic, mtod(m, struct ieee80211_frame_min *));
	ieee80211_input(ic, m, ni, MAX(rssi + 100, 0), 0);
	ieee80211_free_node(ni);
	splx(s);
}

/* May be called with a spin mutex held: only counters and softints. */
void
rtw89_nb_tx_done(void *arg, bool acked)
{
	struct rtw89_softc *sc = arg;
	struct ifnet *ifp = &sc->sc_ec.ec_if;

	if (acked)
		if_statinc(ifp, if_opackets);
	if (sc->sc_running)
		softint_schedule(sc->sc_tx_sih);
}

void
rtw89_nb_queues(void *arg, bool wake)
{
	struct rtw89_softc *sc = arg;

	sc->sc_txstopped = !wake;
	if (wake && sc->sc_running)
		softint_schedule(sc->sc_tx_sih);
}

void
rtw89_nb_link_lost(void *arg)
{
	struct rtw89_softc *sc = arg;

	if (sc->sc_running)
		softint_schedule(sc->sc_linklost_sih);
}

/* ------------------------------------------------------------------ */
/* net80211 glue                                                       */
/* ------------------------------------------------------------------ */

static uint16_t
rtw89_rateset_mask(const struct ieee80211_rateset *rs, bool basic)
{
	static const uint8_t rates[RTW89_NB_NRATES] = {
		2, 4, 11, 22, 12, 18, 24, 36, 48, 72, 96, 108
	};
	uint16_t mask = 0;
	int i, j;

	for (i = 0; i < rs->rs_nrates; i++) {
		if (basic && (rs->rs_rates[i] & IEEE80211_RATE_BASIC) == 0)
			continue;
		for (j = 0; j < RTW89_NB_NRATES; j++)
			if ((rs->rs_rates[i] & IEEE80211_RATE_VAL) == rates[j])
				mask |= 1 << j;
	}
	return mask;
}

static void
rtw89_fill_bss(struct rtw89_softc *sc, struct rtw89_nb_bss *bss)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni = ic->ic_bss;

	memset(bss, 0, sizeof(*bss));
	IEEE80211_ADDR_COPY(bss->bssid, ni->ni_bssid);
	bss->aid = IEEE80211_AID(ni->ni_associd);
	bss->beacon_int = ni->ni_intval;
	bss->dtim_period = ni->ni_dtim_period;
	bss->short_slot = (ic->ic_flags & IEEE80211_F_SHSLOT) != 0;
	bss->rates = rtw89_rateset_mask(&ni->ni_rates, false);
	bss->basic_rates = rtw89_rateset_mask(&ni->ni_rates, true);
	bss->chan = ieee80211_chan2ieee(ic, ni->ni_chan);
	bss->freq = ni->ni_chan->ic_freq;
}

static void
rtw89_set_curchan(struct rtw89_softc *sc, struct ieee80211_channel *c)
{
	struct ieee80211com *ic = &sc->sc_ic;
	int error;

	if (rtw89_debug > 1)
		printf("%s: channel %u (%u MHz)\n", device_xname(sc->sc_dev),
		    ieee80211_chan2ieee(ic, c), c->ic_freq);
	error = rtw89_nb_set_channel(sc->sc_nb, ieee80211_chan2ieee(ic, c),
	    c->ic_freq);
	if (error)
		aprint_error_dev(sc->sc_dev, "can't set channel %u (%d)\n",
		    ieee80211_chan2ieee(ic, c), error);
}

static int
rtw89_newstate(struct ieee80211com *ic, enum ieee80211_state nstate, int arg)
{
	struct rtw89_softc *sc = ic->ic_ifp->if_softc;
	bool enqueue;

	callout_stop(&sc->sc_scan_to);
	mutex_enter(&sc->sc_nslock);
	sc->sc_nsstate = nstate;
	sc->sc_nsarg = arg;
	enqueue = !sc->sc_nspending;
	sc->sc_nspending = true;
	mutex_exit(&sc->sc_nslock);
	if (enqueue)
		workqueue_enqueue(sc->sc_nswq, &sc->sc_nswk, NULL);
	return 0;
}

static void
rtw89_newstate_task(struct work *wk, void *arg)
{
	struct rtw89_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	struct rtw89_nb_bss bss;
	enum ieee80211_state ostate, nstate;
	int nsarg, s, error;

	mutex_enter(&sc->sc_nslock);
	nstate = sc->sc_nsstate;
	nsarg = sc->sc_nsarg;
	sc->sc_nspending = false;
	mutex_exit(&sc->sc_nslock);

	ostate = ic->ic_state;
	if (rtw89_debug)
		printf("%s: %s -> %s\n", device_xname(sc->sc_dev),
		    ieee80211_state_name[ostate], ieee80211_state_name[nstate]);

	if (sc->sc_running && sc->sc_nb != NULL) {
		if (nstate < ostate && ostate >= IEEE80211_S_AUTH)
			rtw89_nb_bss_leave(sc->sc_nb);
		if (sc->sc_scanning && nstate != IEEE80211_S_SCAN) {
			rtw89_nb_scan_end(sc->sc_nb);
			sc->sc_scanning = false;
		}
		switch (nstate) {
		case IEEE80211_S_SCAN:
			if (!sc->sc_scanning) {
				rtw89_nb_scan_start(sc->sc_nb);
				sc->sc_scanning = true;
			}
			rtw89_set_curchan(sc, ic->ic_curchan);
			break;
		case IEEE80211_S_AUTH:
			rtw89_set_curchan(sc, ic->ic_bss->ni_chan);
			rtw89_fill_bss(sc, &bss);
			error = rtw89_nb_bss_join(sc->sc_nb, &bss);
			if (error)
				aprint_error_dev(sc->sc_dev,
				    "can't join BSS (%d)\n", error);
			break;
		case IEEE80211_S_RUN:
			if (ic->ic_opmode != IEEE80211_M_STA)
				break;
			rtw89_fill_bss(sc, &bss);
			error = rtw89_nb_bss_assoc(sc->sc_nb, &bss);
			if (error)
				aprint_error_dev(sc->sc_dev,
				    "can't associate (%d)\n", error);
			break;
		default:
			break;
		}
	}

	s = splnet();
	(void)(*sc->sc_newstate)(ic, nstate, nsarg);
	if (ic->ic_state == IEEE80211_S_SCAN && sc->sc_running)
		callout_schedule(&sc->sc_scan_to, mstohz(RTW89_SCAN_DWELL_MS));
	splx(s);
}

static void
rtw89_next_scan(void *arg)
{
	struct rtw89_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	int s;

	s = splnet();
	if (ic->ic_state == IEEE80211_S_SCAN)
		ieee80211_next_scan(ic);
	splx(s);
}

static void
rtw89_linklost_softint(void *arg)
{
	struct rtw89_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	int s;

	s = splnet();
	if (ic->ic_state == IEEE80211_S_RUN)
		ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
	splx(s);
}

static void
rtw89_tx_softint(void *arg)
{
	struct rtw89_softc *sc = arg;
	struct ifnet *ifp = &sc->sc_ec.ec_if;
	int s;

	s = splnet();
	ifp->if_flags &= ~IFF_OACTIVE;
	rtw89_start(ifp);
	splx(s);
}

static int
rtw89_tx(struct rtw89_softc *sc, struct mbuf *m, struct ieee80211_node *ni,
    unsigned int flags)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_frame *wh;
	struct ieee80211_key *k;
	int len;

	wh = mtod(m, struct ieee80211_frame *);
	if (wh->i_fc[1] & IEEE80211_FC1_WEP) {
		k = ieee80211_crypto_encap(ic, ni, m);
		if (k == NULL)
			return ENOBUFS;
	}
	len = m->m_pkthdr.len;
	if (len > RTW89_TXBUF_SIZE)
		return EMSGSIZE;
	m_copydata(m, 0, len, sc->sc_txbuf);
	return rtw89_nb_tx(sc->sc_nb, sc->sc_txbuf, len, flags);
}

static void
rtw89_start(struct ifnet *ifp)
{
	struct rtw89_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ether_header *eh;
	struct ieee80211_node *ni;
	struct mbuf *m;
	unsigned int flags;

	if ((ifp->if_flags & (IFF_RUNNING | IFF_OACTIVE)) != IFF_RUNNING)
		return;

	for (;;) {
		if (sc->sc_txstopped) {
			ifp->if_flags |= IFF_OACTIVE;
			break;
		}
		flags = 0;
		IF_DEQUEUE(&ic->ic_mgtq, m);
		if (m != NULL) {
			ni = M_GETCTX(m, struct ieee80211_node *);
			M_CLEARCTX(m);
			goto sendit;
		}
		if (ic->ic_state != IEEE80211_S_RUN)
			break;
		IFQ_DEQUEUE(&ifp->if_snd, m);
		if (m == NULL)
			break;
		if (m->m_len < (int)sizeof(*eh) &&
		    (m = m_pullup(m, sizeof(*eh))) == NULL) {
			if_statinc(ifp, if_oerrors);
			continue;
		}
		eh = mtod(m, struct ether_header *);
		if (eh->ether_type == htons(ETHERTYPE_PAE))
			flags |= RTW89_NB_TX_EAPOL;
		ni = ieee80211_find_txnode(ic, eh->ether_dhost);
		if (ni == NULL) {
			m_freem(m);
			if_statinc(ifp, if_oerrors);
			continue;
		}
		bpf_mtap(ifp, m, BPF_D_OUT);
		if ((m = ieee80211_encap(ic, m, ni)) == NULL) {
			ieee80211_free_node(ni);
			if_statinc(ifp, if_oerrors);
			continue;
		}
sendit:
		bpf_mtap3(ic->ic_rawbpf, m, BPF_D_OUT);
		if (rtw89_tx(sc, m, ni, flags) != 0)
			if_statinc(ifp, if_oerrors);
		m_freem(m);
		if (ni != NULL)
			ieee80211_free_node(ni);
	}
}

static int
rtw89_init(struct ifnet *ifp)
{
	struct rtw89_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	int error;

	if (sc->sc_nb == NULL)
		return ENXIO;
	rtw89_stop(ifp, 0);

	IEEE80211_ADDR_COPY(ic->ic_myaddr, CLLADDR(ifp->if_sadl));
	error = rtw89_nb_start(sc->sc_nb, ic->ic_myaddr);
	if (error) {
		aprint_error_dev(sc->sc_dev, "can't start hardware (%d)\n",
		    error);
		return EIO;
	}
	sc->sc_running = true;
	sc->sc_txstopped = false;
	ifp->if_flags &= ~IFF_OACTIVE;
	ifp->if_flags |= IFF_RUNNING;

	if (ic->ic_opmode != IEEE80211_M_MONITOR) {
		if (ic->ic_roaming != IEEE80211_ROAMING_MANUAL)
			ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
	} else
		ieee80211_new_state(ic, IEEE80211_S_RUN, -1);
	return 0;
}

static void
rtw89_stop(struct ifnet *ifp, int disable)
{
	struct rtw89_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	int s;

	if (sc->sc_nb == NULL)
		return;
	callout_halt(&sc->sc_scan_to, NULL);
	s = splnet();
	ieee80211_new_state(ic, IEEE80211_S_INIT, -1);
	splx(s);
	workqueue_wait(sc->sc_nswq, &sc->sc_nswk);
	callout_halt(&sc->sc_scan_to, NULL);

	sc->sc_running = false;
	sc->sc_scanning = false;
	ifp->if_flags &= ~(IFF_RUNNING | IFF_OACTIVE);
	ifp->if_timer = 0;
	rtw89_nb_stop(sc->sc_nb);
}

static void
rtw89_watchdog(struct ifnet *ifp)
{
	struct rtw89_softc *sc = ifp->if_softc;

	ifp->if_timer = 0;
	ieee80211_watchdog(&sc->sc_ic);
}

static int
rtw89_ioctl(struct ifnet *ifp, u_long cmd, void *data)
{
	struct rtw89_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	int s, error = 0;

	switch (cmd) {
	case SIOCSIFFLAGS:
		if ((error = ifioctl_common(ifp, cmd, data)) != 0)
			break;
		switch (ifp->if_flags & (IFF_UP | IFF_RUNNING)) {
		case IFF_UP:
			error = rtw89_init(ifp);
			if (error)
				ifp->if_flags &= ~IFF_UP;
			break;
		case IFF_RUNNING:
			rtw89_stop(ifp, 1);
			break;
		default:
			break;
		}
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		/* all multicast frames are accepted */
		if ((error = ether_ioctl(ifp, cmd, data)) == ENETRESET)
			error = 0;
		break;
	default:
		s = splnet();
		error = ieee80211_ioctl(ic, cmd, data);
		splx(s);
		break;
	}

	if (error == ENETRESET) {
		error = 0;
		if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) ==
		    (IFF_UP | IFF_RUNNING))
			error = rtw89_init(ifp);
	}
	return error;
}

/* ------------------------------------------------------------------ */
/* Module glue                                                         */
/* ------------------------------------------------------------------ */

SYSCTL_SETUP(sysctl_hw_rtw89_setup, "sysctl hw.rtw89 setup")
{
	const struct sysctlnode *node;

	if (sysctl_createv(clog, 0, NULL, &node, CTLFLAG_PERMANENT,
	    CTLTYPE_NODE, "rtw89", SYSCTL_DESCR("rtw89 driver controls"),
	    NULL, 0, NULL, 0, CTL_HW, CTL_CREATE, CTL_EOL) != 0)
		return;
	sysctl_createv(clog, 0, &node, NULL, CTLFLAG_PERMANENT |
	    CTLFLAG_READWRITE, CTLTYPE_INT, "debug",
	    SYSCTL_DESCR("Driver (net80211 glue) debug messages"),
	    NULL, 0, &rtw89_debug, 0, CTL_CREATE, CTL_EOL);
	sysctl_createv(clog, 0, &node, NULL, CTLFLAG_PERMANENT |
	    CTLFLAG_READWRITE, CTLTYPE_INT, "hwdebug",
	    SYSCTL_DESCR("rtw89 debug mask (RTW89_DBG_*)"),
	    NULL, 0, &rtw89_debug_mask, 0, CTL_CREATE, CTL_EOL);
}

MODULE(MODULE_CLASS_DRIVER, if_rtw89, "pci");

#ifdef _MODULE
#include "ioconf.c"
#endif

static int
if_rtw89_modcmd(modcmd_t cmd, void *opaque)
{
	int error = 0;

	switch (cmd) {
	case MODULE_CMD_INIT:
#ifdef _MODULE
		if (opaque != NULL) {
			/* modload -i debug=<mask> */
			prop_dictionary_get_int32(opaque, "debug", &rtw89_debug);
		}
		error = config_init_component(cfdriver_ioconf_if_rtw89,
		    cfattach_ioconf_if_rtw89, cfdata_ioconf_if_rtw89);
#endif
		return error;
	case MODULE_CMD_FINI:
#ifdef _MODULE
		error = config_fini_component(cfdriver_ioconf_if_rtw89,
		    cfattach_ioconf_if_rtw89, cfdata_ioconf_if_rtw89);
#endif
		return error;
	default:
		return ENOTTY;
	}
}
