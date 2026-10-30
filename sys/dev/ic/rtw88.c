/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native RTL8822CE resource lifecycle, AI-assisted. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#ifdef _KERNEL
#include <sys/bus.h>
#include <sys/null.h>
#else
#include <string.h>
#endif
#include <dev/ic/rtw88var.h>
#include <dev/ic/rtw88_io.h>
#include <dev/ic/rtw88_internal.h>

#define RTW88_REG_CR		0x0100
/* pci.c rtw_pci_init irq_mask[0], [1], [3]. */
#define RTW88_MIN_FRAME		10	/* 802.11 ACK */
/* main.c hal.rcr: APP_FCS/MIC/ICV/PHYSTS, VHT_DACK, PKTCTL_DLEN,
 * HTC_LOC, AB/AM/APM. VHT_DACK selects the pinned default ACK policy. */
#define RTW88_BEACON_IES	36	/* header 24, timestamp, interval, caps */
#define RTW88_QSEL_MGMT		18
#define RTW88_RATEID_G		7
#define RTW88_RATEID_B_20M	8
/* rtlwifi REG_LBMODE (CR + 3): normal 0, PHY 1, MAC delay 3, DMA 7. */

/* The common queue numbering maps to the stack's logical WME rings. */
static const unsigned rtw88_queue_ring[RTW88_NTXQ] = {
	RTW88_TX_RING_MGMT, RTW88_TX_RING_BE, RTW88_TX_RING_BK,
	RTW88_TX_RING_VI, RTW88_TX_RING_VO
};

/*
 * The chip DDMA source is its internal TX buffer, not the host mapping.
 * A beacon-queue descriptor must copy the chunk there before DDMA runs.
 */

/* Point every RX descriptor at its buffer, owned by the chip. */

/*
 * pci.c rtw_pci_setup: publish rings, clear indices, reset TRX DMA.
 * Used before firmware download and again after it, as the reference.
 */

/*
 * The driver, not the firmware, reports each section to the WCPU ROM:
 * DW_OK, plus CHKSUM_OK when DDMA saw a good checksum. IMEM and EMEM
 * sit below the DMEM base and share the IMEM bits.
 */

/* Dedicated startup event slot, not a DMA C2H packet or an H2C command.
 * Protocol: FreeBSD d2145e9b main.c rtw_chip_efuse_enable/rtw_dump_hw_feature,
 * Realtek BSD-3-Clause option. A bounded snapshot cannot detect short firmware
 * writes: the pinned wire contract supplies exactly thirteen payload bytes. */

/* REG_CR.MACTXEN and REG_TXPAUSE use pinned rtw88 reg.h definitions.
 * Try both independent controls even when one fails. A failed write can have
 * taken effect; report the first error and never restore either TX control.
 */
int
rtw88_tx_quiet(struct rtw88_softc *sc)
{
	uint8_t cr, check;
	int error, e;

	sc->sc_stop_requested = 1;
	sc->sc_phy.channel_valid = 0;
	rtw88_dfs_close(sc);
	/* Also cancel a start that yielded inside the firmware loader. */
	sc->sc_generation++;
	rtw88_ra_invalidate(sc);
#ifdef RTW88_RFK_EXPERIMENT
	sc->sc_power_program.valid = 0;
	sc->sc_operating.prepared = sc->sc_operating.sealed = 0;
#endif
	if (!sc->sc_resources_ready || !sc->sc_running || !sc->sc_started)
		return ENXIO;
	error = sc->sc_ops->write_1(sc->sc_bus, 0x0522, 0xff);
	e = sc->sc_ops->read_1(sc->sc_bus, RTW88_REG_CR, &cr);
	if (e == 0)
		e = sc->sc_ops->write_1(sc->sc_bus, RTW88_REG_CR, cr & ~0x40);
	if (error == 0)
		error = e;
	e = sc->sc_ops->read_1(sc->sc_bus, 0x0522, &check);
	if (e == 0 && check != 0xff)
		e = EIO;
	if (error == 0)
		error = e;
	e = sc->sc_ops->read_1(sc->sc_bus, RTW88_REG_CR, &check);
	if (e == 0 && (check & 0x40) != 0)
		e = EIO;
	if (error == 0)
		error = e;
	return error;
}

int
rtw88_stop(struct rtw88_softc *sc)
{
	int error;

	if (!rtw88_chip_valid(sc))
		return EINVAL;
#ifdef RTW88_RFK_EXPERIMENT
	sc->sc_power_program.valid = 0;
	sc->sc_operating.prepared = sc->sc_operating.sealed = 0;
#endif
	sc->sc_stop_requested = 1;
	memset(&sc->sc_mailbox, 0, sizeof(sc->sc_mailbox));
	memset(&sc->sc_phy, 0, sizeof(sc->sc_phy));
	rtw88_dfs_close(sc);
	memset(&sc->sc_hw_feature, 0, sizeof(sc->sc_hw_feature));
	rtw88_ra_invalidate(sc);
	sc->sc_linked = 0;
	sc->sc_stats.ra_report_valid = 0;
	sc->sc_generation++;
	sc->sc_packet_h2c.active = 0;
	if (sc->sc_hci != NULL) {
		sc->sc_hci_error = sc->sc_hci->quiesce(sc);
		if (sc->sc_hci_error != 0)
			return sc->sc_hci_error;
	}
	sc->sc_started = 0;
	sc->sc_rf_enabled = 0;
	sc->sc_bootstrapping = 0;
	if (sc->sc_power_dirty || sc->sc_powered) {
		error = sc->sc_chip->stop(sc);
		sc->sc_power_error = error;
		if (error == 0) {
			sc->sc_powered = 0;
			sc->sc_power_dirty = 0;
		} else {
			sc->sc_power_dirty = 1;
			printf("rtwce: power off failed: %d; restart blocked\n", error);
		}
	}
	if (sc->sc_hci != NULL) {
		sc->sc_hci_error = sc->sc_hci->stop(sc);
		if (sc->sc_hci_error != 0)
			return sc->sc_hci_error;
	}
	return sc->sc_dma_error != 0 ? sc->sc_dma_error : sc->sc_power_error;
}

/*
 * Core 20 MHz admission: 2.4 GHz 1-13 and the 5 GHz channels below, a
 * superset of the net80211 channel list. Channel 64 needs RTW88_DFS64,
 * and every DFS channel (52-64, 100-144) still needs a slave TX permit
 * before any host frame.
 */
static int
rtw88_channel_admit20(unsigned ch)
{
	return (ch >= 1 && ch <= 13) ||
	    (ch >= 36 && ch <= 64 && ch % 4 == 0 && ch != 64) ||
#ifdef RTW88_DFS64
	    ch == 64 ||
#endif
	    (ch >= 100 && ch <= 144 && ch % 4 == 0) ||
	    (ch >= 149 && ch <= 165 && (ch - 149) % 4 == 0);
}

/*
 * With RTW88_VHT80, a 5 GHz 40 or 80 MHz tuple on an IEEE 802.11-2016
 * Annex E block whose every 20 MHz constituent is itself admitted.
 * Blocks never straddle the DFS boundary, so a wide tuple is DFS exactly
 * when its primary is.
 */
int
rtw88_channel_admit(const struct rtw88_channel_tuple *tuple)
{
#ifdef RTW88_VHT80
	static const uint8_t center40[] = { 38, 46, 54, 62, 102, 110, 118,
	    126, 134, 142, 151, 159 };
	int delta;
	unsigned i, span;
	int aligned = 0;
#endif

	if (tuple->width_mhz == 20)
		return tuple->center == tuple->primary &&
		    rtw88_channel_admit20(tuple->primary) ? 0 : EOPNOTSUPP;
#ifdef RTW88_VHT80
	if ((tuple->width_mhz != 40 && tuple->width_mhz != 80) ||
	    tuple->primary <= 14)
		return EOPNOTSUPP;
	/* IEEE channel block geometry, independent of RF register encoding. */
	delta = (int)tuple->primary - (int)tuple->center;
	if (tuple->width_mhz == 40 && delta != -2 && delta != 2)
		return EOPNOTSUPP;
	if (tuple->width_mhz == 80) {
		if ((delta != -6 && delta != -2 && delta != 2 && delta != 6) ||
		    (tuple->center != 42 && tuple->center != 58 &&
		     tuple->center != 106 && tuple->center != 122 &&
		     tuple->center != 138 && tuple->center != 155))
			return EOPNOTSUPP;
		aligned = 1;
	}
	for (i = 0; i < __arraycount(center40); i++)
		if (tuple->width_mhz == 40 && tuple->center == center40[i])
			aligned = 1;
	if (!aligned)
		return EOPNOTSUPP;
	span = tuple->width_mhz / 20;
	for (i = 0; i < span; i++)
		if (!rtw88_channel_admit20(tuple->center - (span - 1) * 2 + 4 * i))
			return EOPNOTSUPP;
	return 0;
#else
	return EOPNOTSUPP;
#endif
}

int
rtw88_start(struct rtw88_softc *sc)
{
	const struct rtw88_channel_tuple default_channel = {
		RTW88_DEFAULT_CHANNEL, RTW88_DEFAULT_CHANNEL, 20
	};

	return rtw88_start_tuple(sc, &default_channel);
}

/* Snapshot all needed boxes before mutation. Busy is not an IO error.
 * Caller owns the managed worker and serializes with PCI IRQ/stop. */

/* pci.c rtw_pci_start: unmask the reference interrupt causes. */

/* Beacon evidence: BSSID and SSID, the SSID only when its IE fits. */
static void
rtw88_rx_beacon(struct rtw88_softc *sc, const uint8_t *p, size_t len)
{
	struct rtw88_stats *st = &sc->sc_stats;
	size_t n;

	st->rx_beacons++;
	memcpy(st->last_bssid, p + 16, sizeof(st->last_bssid));
	st->last_ssid_len = 0;
	st->last_ssid[0] = '\0';
	if (len < RTW88_BEACON_IES + 2 || p[RTW88_BEACON_IES] != 0)
		return;
	n = p[RTW88_BEACON_IES + 1];
	if (n > sizeof(st->last_ssid) - 1 || RTW88_BEACON_IES + 2 + n > len)
		return;
	memcpy(st->last_ssid, p + RTW88_BEACON_IES + 2, n);
	st->last_ssid[n] = '\0';
	st->last_ssid_len = (uint8_t)n;
}

/*
 * Pinned fw.h C2H_RA_RPT: ID/sequence and seven payload bytes. The wire
 * has no association token. These are unattributed firmware observations,
 * never ACKs or input to host peer/rate policy.
 */
static void
rtw88_ra_report(struct rtw88_softc *sc, const uint8_t *p, size_t len)
{
	struct rtw88_stats *st = &sc->sc_stats;
	unsigned rate, bit, supported;

	if (len < 9) {
		st->ra_malformed++;
		return;
	}
	if (len > 9)
		st->ra_trailing++;
	if (p[3] != 0) {
		st->ra_foreign++;
		return;
	}
	rate = p[2] & 0x7f;
	/* fw.h rate_sgi and bw describe a firmware observation, not admission.
	 * Default builds retain their bounded legacy/HT20 report parser. */
#ifdef RTW88_VHT80
	/* main.h: VHT1SS MCS0..9 = 0x2c..0x35; VHT2SS = 0x36..0x3f.
	 * Legacy fallback may use non-HT duplicate in a wide entry. */
	supported = (rate < 20 || (rate >= 0x2c && rate <= 0x3f)) &&
	    p[8] <= 2 && ((p[2] & 0x80) == 0 || rate >= 12);
#else
	supported = rate < 20 && (p[2] & 0x80) == 0 && p[8] == 0;
#endif
	if (!supported) {
		st->ra_unsupported++;
		return;
	}
	st->ra_reports++;
	st->ra_unattributed++;
	if (!sc->sc_ra.active)
		st->ra_inactive++;
	bit = rate;
#ifdef RTW88_VHT80
	if (rate >= 0x2c)
		bit = rate - 0x2c + 12;
#endif
	/* VHT and HT use overlapping mask bits but distinct descriptor codes.
	 * Never shift by a VHT descriptor rate or treat HT as a VHT mask hit. */
	if (sc->sc_ra.configured &&
	    ((sc->sc_ra.mask & (UINT32_C(1) << bit)) == 0 ||
	     (rate >= 12 && rate < 20 &&
	      sc->sc_ra.phy != RTW88_RA_HT20_1SS) ||
	     (rate >= 0x2c && sc->sc_ra.phy != RTW88_RA_VHT_1SS)))
		st->ra_outside_mask++;
	st->ra_report_rate = (uint8_t)rate;
	st->ra_report_macid = p[3];
	st->ra_report_bw = p[8];
	st->ra_report_sgi = (p[2] >> 7) & 1;
	st->ra_observed_generation = sc->sc_generation;
	st->ra_report_valid = 1;
}

/* One received buffer: classify, count, and keep every error visible. */
void
rtw88_rx_deliver(struct rtw88_softc *sc, const uint8_t *data, size_t len,
    uint64_t sequence)
{
	struct rtw88_rx_info rx;
	const uint8_t *p;

	if (rtw88_chip_rx_desc(sc, data, len, &rx) != 0) {
		sc->sc_stats.rx_errors++;
		return;
	}
	rx.sequence = sequence;
	rx.generation = sc->sc_generation;
	p = data + rx.payload_offset;
	if ((rx.flags & RTW88_RX_C2H) != 0) {
		if (rx.packet_size < 2) {
			sc->sc_stats.rx_errors++;
			return;
		}
		sc->sc_stats.rx_c2h++;
		sc->sc_stats.last_c2h_id = p[0];
		sc->sc_stats.last_c2h_seq = p[1];
		if (p[0] == 0x0c)
			rtw88_ra_report(sc, p, rx.packet_size);
		return;
	}
	if ((rx.flags & (RTW88_RX_CRC_ERROR | RTW88_RX_ICV_ERROR)) != 0) {
		sc->sc_stats.rx_errors++;
		return;
	}
	sc->sc_stats.signal = rx.signal;
	sc->sc_stats.signal_reasons[rx.signal.reason]++;
	if (rx.packet_size < RTW88_MIN_FRAME) {
		sc->sc_stats.rx_short++;
		return;
	}
	/* The diagnostic counters include valid control frames. */
	sc->sc_stats.rx_frames++;
	if (p[0] == 0x80 && rx.packet_size >= RTW88_BEACON_IES)
		rtw88_rx_beacon(sc, p, rx.packet_size);
	/* RCR.APP_FCS is set. Never expose the four FCS bytes to net80211. */
	if (sc->sc_rx_frame != NULL && (rx.packet_size >= 24 + 4 ||
	    (rx.packet_size == 20 + 4 && p[0] == 0x84 && (p[1] & ~0x08) == 0 &&
	     p[16] == 4 && (p[17] & 0x8f) == 0 && (p[18] & 15) == 0))) {
		rx.packet_size -= 4;
		sc->sc_rx_frame(sc->sc_net_cookie, p, rx.packet_size, &rx);
	}
}

/* pci.c rtw_pci_rx_napi, bounded by RTW88_RX_LIMIT per interrupt. */

/* Read the completed DMA producer without consuming or publishing slots. */

static int rtw88_dfs_tx_permitted(const struct rtw88_softc *);

/* A closed DFS permit is no capacity: callers keep frames queued for it. */
unsigned
rtw88_tx_available(struct rtw88_softc *sc, unsigned q)
{
	if (q >= RTW88_NTXQ || !sc->sc_running || sc->sc_stop_requested ||
	    sc->sc_edca_error != 0 ||
	    sc->sc_ra.error != 0 || !rtw88_dfs_tx_permitted(sc))
		return 0;
	return sc->sc_hci->space(sc, q);
}

/*
 * Port0 fields and mailbox layouts are from pinned rtw88 main.c,
 * fw.c/fw.h, sec.c and reg.h. This host-managed legacy station does
 * not enable hardware crypto or LPS; RA is configured separately.
 */

/* EDCA register layout follows pinned BSD-licensed rtw88 mac80211.c. */

/* Pinned fw.c/fw.h RA_INFO register command; legacy, HT20/1SS or VHT/1SS
 * at 40 or 80 MHz. */
void
rtw88_ra_invalidate(struct rtw88_softc *sc)
{
	sc->sc_ra.active = 0;
	sc->sc_ra.configured = 0;
}

int
rtw88_ht20_policy_check(const struct rtw88_ht20_policy *p, unsigned operation)
{

	/* The net80211 envelope: mode 0 (also with non-GF present), mode 2
	 * with non-GF, and non-HT mixed mode 3. */
	if (p->qualification != RTW88_HT20_PROT_MF20_NO_RTS ||
	    (operation != 0 && operation != 3 && operation != 4 &&
	     operation != 6 && operation != 7) ||
	    (p->peer_cap & 0x1c) != 0x0c ||
	    (p->secondary != 0 && p->secondary != 5 && p->secondary != 7) ||
	    p->mtu < RTW88_HT20_MTU_MIN || p->mtu > RTW88_HT20_MTU_MAX ||
	    p->max_mpdu != p->mtu + RTW88_HT20_TX_OVERHEAD ||
	    p->max_mpdu > RTW88_TX_BUF_SIZE - RTW8822C_TX_PACKET_DESC_SIZE ||
	    p->rts_threshold < p->max_mpdu + RTW88_HT20_FCS_SIZE ||
	    p->rts_threshold > RTW88_HT20_THRESHOLD_MAX)
		return EOPNOTSUPP;
	return 0;
}

/* Legacy, HT20 and every current descriptor path use the 20 MHz tuple. */
int
rtw88_channel_narrow(const struct rtw88_softc *sc)
{
	return sc->sc_phy.channel.width_mhz == 20 &&
	    sc->sc_phy.channel.primary == sc->sc_channel &&
	    sc->sc_phy.channel.center == sc->sc_channel;
}

#ifdef RTW88_VHT80

unsigned
rtw88_vht_nss(const struct rtw88_softc *sc)
{
	const struct rtw88_hw_feature *r = &sc->sc_hw_feature.report;
	const unsigned known = RTW88_HW_KNOWN_NSS | RTW88_HW_KNOWN_ANTENNAS;

	return sc->sc_hw_feature.observed && (r->known & known) == known &&
	    r->nss == 2 && r->antennas == 2 ? 2 : 1;
}

/*
 * main.c get_rate_id/get_vht_ra_mask and tx.c get_highest_vht_tx_rate
 * for one negotiated VHT TX map: NSS1 required, NSS2 only on proven 2T2R
 * hardware, no third stream. Entries 0, 1 and 2 are MCS0-7, 0-8 and 0-9:
 * NSS1 bits 12..21 and NSS2 bits 22..31 of the RA mask. The descriptor
 * seed is the highest MCS of the highest stream.
 */
int
rtw88_vht_rates(const struct rtw88_softc *sc, unsigned map,
    struct rtw88_vht_rate_set *out)
{
	static const unsigned bits[3] = { 0xff, 0x1ff, 0x3ff };
	unsigned nss1 = map & 3, nss2 = (map >> 2) & 3;

	if ((map >> 4) != 0xfff || nss1 == 3 ||
	    (nss2 != 3 && rtw88_vht_nss(sc) < 2))
		return EOPNOTSUPP;
	if (out == NULL)
		return 0;
	out->mask = bits[nss1] << 12 | (nss2 != 3 ? bits[nss2] << 22 : 0);
	/* RTW_RATEID_ARFR0_AC_2SS and _ARFR1_AC_1SS; DESC_RATEVHT2SS_MCS0
	 * 0x36 and DESC_RATEVHT1SS_MCS0 0x2c. */
	out->rate_id = nss2 != 3 ? 9 : 10;
	out->seed = (uint8_t)(nss2 != 3 ? 0x36 + 7 + nss2 : 0x2c + 7 + nss1);
	return 0;
}

/*
 * An admitted 40 or 80 MHz tuple whose actual PHY and power program is
 * committed in this generation, with its primary in sc_channel.
 */
int
rtw88_vht_committed(const struct rtw88_softc *sc)
{
	return sc->sc_phy.channel_valid &&
	    sc->sc_phy.channel.primary == sc->sc_channel &&
	    (sc->sc_phy.channel.width_mhz == 40 ||
	     sc->sc_phy.channel.width_mhz == 80) &&
	    rtw88_channel_admit(&sc->sc_phy.channel) == 0;
}

/*
 * Pure admission of one tuple against this core's committed efuse:
 * channel policy, PHY geometry and every TX power validation, with no
 * I/O and no state change. Callers under Giant or the bus lock.
 */

#endif

/* 5 GHz channels whose master must perform radar detection. */
int
rtw88_channel_dfs(unsigned ch)
{
	return (ch >= 52 && ch <= 64) || (ch >= 100 && ch <= 144);
}

/*
 * Host TX on a DFS channel needs a permit for this channel and core
 * generation. Admitted wide tuples are aligned blocks, so the primary
 * decides for the whole span.
 */
static int
rtw88_dfs_tx_permitted(const struct rtw88_softc *sc)
{
	return !rtw88_channel_dfs(sc->sc_channel) ||
	    (sc->sc_dfs.valid && sc->sc_dfs.channel == sc->sc_channel &&
	     sc->sc_dfs.generation == sc->sc_generation);
}

int
rtw88_dfs_open(struct rtw88_softc *sc, unsigned ch, uint32_t gen)
{
	if (!sc->sc_running || !sc->sc_started || sc->sc_stop_requested)
		return ENXIO;
	if (gen != sc->sc_generation)
		return ESTALE;
#ifdef RTW88_DFS64
	if (rtw88_channel_dfs(ch) && sc->sc_channel == ch &&
	    sc->sc_phy.channel_valid && sc->sc_phy.channel.primary == ch &&
	    rtw88_channel_admit(&sc->sc_phy.channel) == 0) {
		sc->sc_dfs.generation = gen;
		sc->sc_dfs.channel = ch;
		sc->sc_dfs.valid = 1;
		return 0;
	}
#else
	(void)ch;
#endif
	return EOPNOTSUPP;
}

void
rtw88_dfs_close(struct rtw88_softc *sc)
{
	memset(&sc->sc_dfs, 0, sizeof(sc->sc_dfs));
}

static int
rtw88_tx_submit(struct rtw88_softc *sc, unsigned q, const void *frame,
    size_t len, int fixed_rate, unsigned ra_mode,
    const struct rtw88_tx_agg *agg)
{
	struct rtw88_tx_info tx;
	struct rtw88_wme_map map;
	enum rtw88_tx_progress progress;
	unsigned rates, qsel, hdrlen;
	const uint8_t *bytes = frame;
	int error;
#ifdef RTW88_VHT80
	uint8_t primary_sc = 0;
	int vht = 0;
#endif

	if (q >= RTW88_NTXQ || (agg != NULL && ra_mode != 1))
		return EINVAL;
	if (sc->sc_edca_error != 0)
		return sc->sc_edca_error;
	if (sc->sc_ra.error != 0)
		return sc->sc_ra.error;
	if (!sc->sc_resources_ready || !sc->sc_running || !sc->sc_started ||
	    sc->sc_stop_requested)
		return ENXIO;
	if (!rtw88_dfs_tx_permitted(sc))
		return EACCES;
	if (rtw88_channel_narrow(sc)) {
		if (ra_mode == 1 && sc->sc_ra.phy == RTW88_RA_VHT_1SS)
			return EOPNOTSUPP;
	} else {
#ifdef RTW88_VHT80
		/*
		 * Qualified wide VHT formats only. Firmware-RA data is VHT at
		 * the committed width its entry was configured for, or non-HT
		 * duplicate after its 6 Mbps fallback. Every other frame is
		 * non-HT OFDM on primary20, placed by DATA_SC: raw frames use
		 * 5 GHz basic OFDM and tx_fixed rejects 11A CCK.
		 */
		if (!rtw88_vht_committed(sc) || ra_mode == 2 ||
		    (ra_mode == 1 && (sc->sc_ra.phy != RTW88_RA_VHT_1SS ||
		     sc->sc_ra.tuple.primary != sc->sc_phy.channel.primary ||
		     sc->sc_ra.tuple.center != sc->sc_phy.channel.center ||
		     sc->sc_ra.tuple.width_mhz !=
		     sc->sc_phy.channel.width_mhz)) ||
		    sc->sc_chip->tx_primary_sc == NULL ||
		    sc->sc_chip->tx_primary_sc(sc, &sc->sc_phy.channel,
		    &primary_sc) != 0)
			return EOPNOTSUPP;
		vht = ra_mode == 1;
#else
		/* Default descriptor encoder and DMA engine are 20 MHz only. */
		return EOPNOTSUPP;
#endif
	}
	if (frame == NULL || len < 24 ||
	    len > RTW88_TX_BUF_SIZE - sc->sc_chip->caps.tx_desc_size)
		return EMSGSIZE;
	qsel = q == RTW88_TXQ_MGMT ? RTW88_QSEL_MGMT : 0;
	if ((bytes[0] & 0x8c) == 0x88) {
		/* HT Control is outside this legacy/QoS hardware prerequisite. */
		if ((bytes[1] & 0x80) != 0)
			return EOPNOTSUPP;
		hdrlen = (bytes[1] & 3) == 3 ? 32 : 26;
		if (len < hdrlen)
			return EMSGSIZE;
		if (q == RTW88_TXQ_MGMT)
			return EOPNOTSUPP;
		error = rtw88_wme_map(bytes[hdrlen - 2] & 15, &map);
		if (error != 0)
			return error;
		/* Queue ownership must match the actual QoS TID. */
		if (map.ring != rtw88_queue_ring[q])
			return EOPNOTSUPP;
		qsel = map.qsel;
	} else if (q != RTW88_TXQ_MGMT && q != RTW88_TXQ_BE) {
		return EOPNOTSUPP;
	}
	memset(&tx, 0, sizeof(tx));
	tx.packet_size = (uint32_t)len;
	tx.queue = (uint8_t)qsel;
	/* main.h DESC_RATE6M and RATEID_G: CCK is invalid on 5 GHz. */
	tx.rate = sc->sc_channel > 14 ? 4 : 0;
	rates = sc->sc_basic_rates & (sc->sc_channel > 14 ? 0xff0U : 0xfffU);
	if (rates != 0)
		tx.rate = (uint8_t)__builtin_ctz(rates);
	if (fixed_rate >= 0)
		tx.rate = (uint8_t)fixed_rate;
	tx.rate_id = tx.rate >= 4 ? RTW88_RATEID_G : RTW88_RATEID_B_20M;
	tx.flags = RTW88_TX_DIS_QSELSEQ | RTW88_TX_USE_RATE |
	    RTW88_TX_DIS_RATE_FB;
	if (ra_mode == 1) {
		tx.mac_id = 0;
		tx.rate_id = sc->sc_ra.rate_id;
		tx.rate = (uint8_t)__builtin_ctz(sc->sc_ra.mask);
		tx.flags &= ~(RTW88_TX_USE_RATE | RTW88_TX_DIS_RATE_FB);
		/* tx.c: DATA_LDPC follows the entry's LDPC. */
		if (sc->sc_ra.ldpc)
			tx.flags |= RTW88_TX_LDPC;
	}
	if (agg != NULL) {
		/* tx.c rtw_tx_fill_tx_desc: AGG_EN, AMPDU_DEN, MAX_AGG_NUM. */
		tx.flags |= RTW88_TX_AMPDU;
		tx.ampdu_density = agg->density;
		tx.ampdu_factor = agg->count;
	}
#ifdef RTW88_VHT80
	if (vht) {
		/* tx.c get_highest_vht_tx_rate seed, not the lowest RA bit. */
		tx.rate = sc->sc_ra.vht_seed;
		/* DATA_BW 40 MHz 1, 80 MHz 2. */
		tx.bandwidth = sc->sc_phy.channel.width_mhz == 80 ? 2 : 1;
		tx.data_sc = RTW88_TX_DATA_SC_DONT_CARE;
	} else if (!rtw88_channel_narrow(sc)) {
		/* Primary20 within the block: upper 1, lower 2, upmost 3 and
		 * lowest 4, as the PHY geometry programs it. */
		tx.data_sc = primary_sc;
	}
#endif
	if (q == RTW88_TXQ_MGMT)
		tx.flags |= RTW88_TX_HWSEQ;
	else
		tx.sequence = (uint16_t)((((const uint8_t *)frame)[22] |
		    ((const uint8_t *)frame)[23] << 8) >> 4);
	if (((const uint8_t *)frame)[4] & 0x01)		/* group DA */
		tx.flags |= RTW88_TX_BMC;
	error = sc->sc_hci->xmit(sc, q, bytes, len, &tx, &progress);
	if (progress == RTW88_TX_NOT_STARTED)
		return error;
	/* Record the encoded descriptor under the caller's bus interrupt lock.
	 * This is an encoding sample, not an ACK or a hardware rate report. */
	if ((bytes[0] & 0x0c) == 0x08 && (bytes[4] & 1) == 0) {
		unsigned rate = tx.rate & 0x7f;

		sc->sc_stats.tx_data_encoded++;
		if (ra_mode == 1)
			sc->sc_stats.tx_ra_encoded++;
		else if (ra_mode == 2)
			sc->sc_stats.tx_ra_protection_limited++;
		if (agg != NULL)
			sc->sc_stats.tx_ampdu_encoded++;
		sc->sc_stats.tx_data_rate = (uint8_t)rate;
		sc->sc_stats.tx_data_basic = (uint16_t)sc->sc_basic_rates;
		if (rate < __arraycount(sc->sc_stats.tx_data_rates))
			sc->sc_stats.tx_data_rates[rate]++;
		else
			sc->sc_stats.tx_data_other++;
	}
	return error;
}

int
rtw88_tx_raw(struct rtw88_softc *sc, unsigned q, const void *frame,
    size_t len)
{
	return rtw88_tx_submit(sc, q, frame, len, -1, 0, NULL);
}

int
rtw88_tx_fixed(struct rtw88_softc *sc, unsigned q, const void *frame,
    size_t len, const struct rtw88_tx_fixed *fixed)
{
	static const uint8_t rates[] = { 2, 4, 11, 22, 12, 18, 24, 36,
	    48, 72, 96, 108 };
	const uint8_t *bytes = frame;
	unsigned rate;

	if (fixed == NULL || fixed->associated != 1 ||
	    fixed->use_protection > 1 || (fixed->peer_mask & ~0xfffU) != 0 ||
	    fixed->channel == 0 || fixed->channel != sc->sc_channel)
		return EINVAL;
	if (frame == NULL || len < 24)
		return EMSGSIZE;
	if (q == RTW88_TXQ_MGMT || (bytes[0] != 0x08 && bytes[0] != 0x88) ||
	    (bytes[1] & 3) != 1 || (bytes[1] & 0x80) != 0 ||
	    (bytes[4] & 1) != 0 || (bytes[16] & 1) != 0)
		return EOPNOTSUPP;
	for (rate = 0; rate < __arraycount(rates); rate++)
		if (fixed->rate500 == rates[rate])
			break;
	if (rate == __arraycount(rates) ||
	    (fixed->peer_mask & (1U << rate)) == 0)
		return EINVAL;
	switch (fixed->mode) {
	case RTW88_FIXED_11A:
		if (fixed->channel <= 14 || rate < 4)
			return EINVAL;
		break;
	case RTW88_FIXED_11B:
		if (fixed->channel > 14 || rate >= 4)
			return EINVAL;
		break;
	case RTW88_FIXED_11G:
		if (fixed->channel > 14)
			return EINVAL;
		break;
	default:
		return EINVAL;
	}
	/* The current descriptor API has no qualified RTS/CTS protection policy. */
	if (rate >= 4 && fixed->use_protection)
		return EOPNOTSUPP;
	return rtw88_tx_submit(sc, q, frame, len, (int)rate, 0, NULL);
}

static int
rtw88_tx_ra(struct rtw88_softc *sc, unsigned q, const void *frame,
    size_t len, uint64_t epoch, unsigned use_protection,
    const struct rtw88_tx_agg *agg)
{
	const uint8_t *bytes = frame;
	unsigned basic, rate;

	if (use_protection > 1)
		return EINVAL;
	if (frame == NULL || len < 24)
		return EMSGSIZE;
	if (q == RTW88_TXQ_MGMT || (bytes[0] != 0x08 && bytes[0] != 0x88) ||
	    (bytes[1] & 3) != 1 || (bytes[1] & 0x80) != 0 ||
	    (bytes[4] & 1) != 0 || (bytes[16] & 1) != 0)
		return EOPNOTSUPP;
	if (sc->sc_ra.error != 0)
		return sc->sc_ra.error;
	if (!sc->sc_linked || !sc->sc_ra.configured ||
	    sc->sc_ra.epoch != epoch ||
	    sc->sc_ra.generation != sc->sc_generation ||
	    sc->sc_ra.channel != sc->sc_channel)
		return EBUSY;
	if (sc->sc_ra.phy == RTW88_RA_HT20_1SS ||
	    sc->sc_ra.phy == RTW88_RA_VHT_1SS) {
		if (use_protection ||
		    rtw88_ht20_policy_check(&sc->sc_ra.ht, sc->sc_ra.ht_operation) != 0 ||
		    (bytes[1] & 0x04) != 0 || (bytes[22] & 15) != 0 ||
		    (bytes[0] == 0x88 &&
		     (len < 26 || (bytes[24] & 0xf0) != 0 || bytes[25] != 0)))
			return EOPNOTSUPP;
		if (len > sc->sc_ra.ht.max_mpdu)
			return EMSGSIZE;
	}
	/* Aggregates exist only on an unprotected HT20 or VHT entry. */
	if (agg != NULL && (use_protection || sc->sc_ra.protection_limited ||
	    (sc->sc_ra.phy != RTW88_RA_HT20_1SS &&
	     sc->sc_ra.phy != RTW88_RA_VHT_1SS)))
		return EOPNOTSUPP;
	if (use_protection) {
		sc->sc_ra.active = 0;
		sc->sc_ra.protection_limited = 1;
	}
	if (sc->sc_ra.protection_limited) {
		basic = sc->sc_basic_rates & 0xfff;
		if (basic == 0)
			return EOPNOTSUPP;
		rate = (unsigned)__builtin_ctz(basic);
		if (rate >= 4 || (sc->sc_ra.common_rates & (1U << rate)) == 0)
			return EOPNOTSUPP;
		return rtw88_tx_submit(sc, q, frame, len, -1, 2, NULL);
	}
	if (!sc->sc_ra.active)
		return EBUSY;
	return rtw88_tx_submit(sc, q, frame, len, -1, 1, agg);
}

int
rtw88_tx_auto(struct rtw88_softc *sc, unsigned q, const void *frame,
    size_t len, uint64_t epoch, unsigned use_protection)
{
	return rtw88_tx_ra(sc, q, frame, len, epoch, use_protection, NULL);
}

#ifdef RTW88_VHT80
int
rtw88_tx_ampdu(struct rtw88_softc *sc, unsigned q, const void *frame,
    size_t len, uint64_t epoch, const struct rtw88_tx_agg *agg)
{
	const uint8_t *bytes = frame;

	if (agg == NULL || agg->tid >= 8 || agg->density > 7 ||
	    agg->count == 0 || agg->count > 31)
		return EINVAL;
	/* A unicast QoS data MPDU of exactly the session's TID. */
	if (frame == NULL || len < 26)
		return EMSGSIZE;
	if (bytes[0] != 0x88 || (bytes[24] & 15) != agg->tid)
		return EOPNOTSUPP;
	return rtw88_tx_ra(sc, q, frame, len, epoch, 0, agg);
}
#endif

/* pci.c rtw_pci_tx_isr: completion is the chip's read index, nothing else. */

/* Same PCI-serialized device-RP decoder as IRQ completion. This is host DMA
 * ownership only: never infer RF idle, air ACK or calibration readiness. */

int
rtw88_channel(struct rtw88_softc *sc, unsigned ch)
{
	const struct rtw88_channel_tuple tuple = { ch, ch, 20 };

	return rtw88_channel_apply(sc, &tuple);
}

static int
rtw88_hci_valid(const struct rtw88_softc *sc)
{
	const struct rtw88_hci_ops *h;

	if (sc == NULL || sc->sc_ops == NULL || (h = sc->sc_hci) == NULL)
		return 0;
	return h->attach != NULL && h->detach != NULL && h->prepare != NULL &&
	    h->setup != NULL && h->start != NULL && h->quiesce != NULL &&
	    h->stop != NULL && h->intr != NULL && h->space != NULL &&
	    h->drain != NULL && h->frontier != NULL && h->fw_section != NULL &&
	    h->xmit != NULL && (h->irq_can_run_inline || sc->sc_defer_irq != NULL);
}

int
rtw88_core_attach(struct rtw88_softc *sc)
{
	int error;

	if (!rtw88_hci_valid(sc) || !rtw88_chip_valid(sc))
		return EINVAL;
	if (sc->sc_chip->attach_check != NULL) {
		error = sc->sc_chip->attach_check(sc);
		if (error != 0)
			return error;
	}
	return sc->sc_hci->attach(sc);
}

int
rtw88_core_detach(struct rtw88_softc *sc)
{
	int error;

	if (!rtw88_hci_valid(sc) || !rtw88_chip_valid(sc))
		return EINVAL;
	if (sc->sc_chip->detach_check != NULL) {
		error = sc->sc_chip->detach_check(sc);
		if (error != 0)
			return error;
	}
	error = rtw88_stop(sc);
	if (error != 0)
		return error;
	if (sc->sc_chip->detach != NULL) {
		error = sc->sc_chip->detach(sc);
		if (error != 0)
			return error;
	}
	return sc->sc_hci->detach(sc);
}

int
rtw88_intr(struct rtw88_softc *sc)
{
	if (sc == NULL || !sc->sc_running || sc->sc_hci == NULL)
		return 0;
	if (sc->sc_hci->irq_can_run_inline)
		return sc->sc_hci->intr(sc);
	if (sc->sc_defer_irq == NULL)
		return 0;
	sc->sc_defer_irq(sc);
	return 1;
}

int
rtw88_rx_frontier(struct rtw88_softc *sc, uint32_t gen, uint64_t *frontier)
{
	if (frontier == NULL)
		return EINVAL;
	*frontier = 0;
	if (!rtw88_hci_valid(sc))
		return EINVAL;
	return sc->sc_hci->frontier(sc, gen, frontier);
}

int
rtw88_tx_drain_snapshot(struct rtw88_softc *sc, unsigned *pending)
{
	if (pending == NULL)
		return EINVAL;
	*pending = 0;
	if (!rtw88_hci_valid(sc))
		return EINVAL;
	return sc->sc_hci->drain(sc, pending);
}
