/*	EmberBSD: HT/VHT negotiation for iwx(4) on the legacy net80211.	*/

/*
 * Copyright (c) 2026 EmberBSD contributors.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * The NetBSD net80211 stack predates 802.11n: it never sends HT or VHT
 * capabilities and ignores them in replies.  The firmware does rate
 * selection itself, so the driver negotiates HT/VHT on its own:
 *
 *  - iwx_ht_rewrite_assoc() adds HT (and on 5 GHz VHT) capability
 *    elements to the (re)association request built by net80211;
 *  - iwx_ht_parse() reads the AP's HT/VHT elements from the
 *    (re)association response;
 *  - iwx_ht_chan() and iwx_ht_tlc() turn both sides into the PHY
 *    context and TLC (rate scaling) settings for the firmware.
 *
 * No block-ack sessions are set up: the driver declines ADDBA requests
 * (iwx_ht_addba_decline()), so the AP sends single MPDUs at HT/VHT rates.
 *
 * Everything here is plain C over byte buffers so that the host contract
 * test (ember/tools/iwx-ht-contract.c) runs the same code.
 */

#ifndef _IF_IWX_HT_H_
#define _IF_IWX_HT_H_

#define IWX_HT_EID_HTCAP	45
#define IWX_HT_EID_HTOP		61
#define IWX_HT_EID_VHTCAP	191
#define IWX_HT_EID_VHTOP	192
#define IWX_HT_EID_VENDOR	221
#define IWX_HT_EID_EXT		255	/* then the element ID extension */
#define IWX_HE_EXT_CAP		35	/* HE capabilities */
#define IWX_HE_EXT_OP		36	/* HE operation */

/*
 * Channel switch announcement elements (802.11-2020 9.4.2.18, 9.4.2.19,
 * 9.4.2.52, 9.4.2.160, 9.4.2.162).
 */
#define IWX_HT_EID_CSA		37
#define IWX_HT_EID_ECSA		60
#define IWX_HT_EID_SCO		62
#define IWX_HT_EID_WBCS		194
#define IWX_HT_EID_CSWRAP	196

#define IWX_HT_HTCAP_LEN	26
#define IWX_HT_HTOP_LEN		22
#define IWX_HT_VHTCAP_LEN	12
#define IWX_HT_VHTOP_LEN	5

/* HT capability info bits (802.11-2020 9.4.2.55.2). */
#define IWX_HT_CAP_LDPC		0x0001
#define IWX_HT_CAP_CW40		0x0002
#define IWX_HT_CAP_SMPS_OFF	0x000c
#define IWX_HT_CAP_SGI20	0x0020
#define IWX_HT_CAP_SGI40	0x0040
#define IWX_HT_CAP_RXSTBC_MASK	0x0300
#define IWX_HT_CAP_RXSTBC_1	0x0100

/* VHT capability info bits (802.11-2020 9.4.2.157.2). */
#define IWX_VHT_CAP_CW160	0x00000004	/* channel width set: 160 MHz */
#define IWX_VHT_CAP_RXLDPC	0x00000010
#define IWX_VHT_CAP_SGI80	0x00000020
#define IWX_VHT_CAP_SGI160	0x00000040
#define IWX_VHT_CAP_RXSTBC_MASK	0x00000700
#define IWX_VHT_CAP_RXSTBC_1	0x00000100
#define IWX_VHT_CAP_AMPDU_EXP7	(7U << 23)
#define IWX_VHT_CAP_CWSET(caps)	(((caps) >> 2) & 3)	/* channel width set */
#define IWX_VHT_CAP_EXTNSS(caps) (((caps) >> 30) & 3)	/* Extended NSS BW */

/* Firmware values, checked against if_iwxreg.h by the driver. */
#define IWX_HT_POS_1_BELOW	0
#define IWX_HT_POS_2_BELOW	1
#define IWX_HT_POS_3_BELOW	2
#define IWX_HT_POS_4_BELOW	3
#define IWX_HT_POS_1_ABOVE	4
#define IWX_HT_POS_2_ABOVE	5
#define IWX_HT_POS_3_ABOVE	6
#define IWX_HT_POS_4_ABOVE	7
#define IWX_HT_WIDTH_20		0
#define IWX_HT_WIDTH_40		1
#define IWX_HT_WIDTH_80		2
#define IWX_HT_WIDTH_160	3
#define IWX_HT_TLC_MODE_NON_HT	0
#define IWX_HT_TLC_MODE_HT	1
#define IWX_HT_TLC_MODE_VHT	2
#define IWX_HT_TLC_MODE_HE	3
#define IWX_HT_TLC_STBC		0x0001
#define IWX_HT_TLC_LDPC		0x0002
#define IWX_HT_TLC_HE_DCM_NSS_1	0x0008

/* <net/if_media.h> values, checked against it by the driver. */
#define IWX_HT_IFM_TMASK	0x0000001f
#define IWX_HT_IFM_MMASK	0x00070000
#define IWX_HT_IFM_MCS		24
#define IWX_HT_IFM_VHT		25
#define IWX_HT_IFM_11NA		0x00050000
#define IWX_HT_IFM_11NG		0x00060000
#define IWX_HT_IFM_11AC		0x00070000

/* Secondary channel offset (HT operation, byte 1 bits 0-1). */
#define IWX_HT_SCO_NONE		0
#define IWX_HT_SCO_ABOVE	1
#define IWX_HT_SCO_BELOW	3
#define IWX_HT_STA_CHAN_WIDTH	0x04

/* What this station offers. */
struct iwx_ht_local {
	int	band5;		/* the BSS channel is in the 5 GHz band */
	int	nss;		/* spatial streams, 1 or 2 */
	int	vht;		/* offer VHT (used on 5 GHz only) */
	int	allow40;	/* regulatory allows 40 MHz on this channel */
	int	allow80;	/* regulatory allows 80 MHz on this channel */
	int	vht160;		/* offer 160 MHz VHT operation */
	int	allow160;	/* regulatory allows 160 MHz on this channel */
	int	he;		/* offer HE (802.11ax); needs VHT and 5 GHz */
};

/* What the AP answered. */
struct iwx_ht_peer {
	int		ht;		/* HT capabilities present */
	int		htop;		/* HT operation present */
	int		vht;		/* VHT capabilities present */
	int		vhtop;		/* VHT operation present */
	uint16_t	htcaps;
	uint8_t		ampdu_param;
	uint8_t		rxmcs[10];
	uint8_t		primary;
	uint8_t		htop0;
	uint16_t	htop1;
	uint32_t	vhtcaps;
	uint16_t	vht_rxmcs;
	uint8_t		vht_width;
	uint8_t		vht_ccfs0;
	uint8_t		vht_ccfs1;
	int		he;		/* HE capabilities present */
	int		heop;		/* HE operation present */
	uint8_t		he_mac[6];
	uint8_t		he_phy[11];
	uint16_t	he_rxmcs80;
	uint16_t	he_txmcs80;
	uint16_t	he_rxmcs160;	/* valid with he_phy[0] & 0x08 */
	uint16_t	he_txmcs160;
	uint8_t		he_ppe[25];	/* PPE thresholds, he_phy[6] & 0x80 */
	uint8_t		he_ppe_len;
	uint32_t	heop_params;	/* with the BSS colour byte on top */
	uint16_t	he_basic;	/* basic HE-MCS and NSS set */
};

/* Channel layout for the PHY context and TLC. */
struct iwx_ht_chan {
	uint8_t	sco;		/* IWX_HT_SCO_* in effect */
	uint8_t	vht80;		/* VHT operation, 80 MHz or wider */
	uint8_t	width;		/* IWX_HT_WIDTH_* */
	uint8_t	ctrl_pos;	/* IWX_HT_POS_* */
};

/* A channel switch announced by the AP. */
struct iwx_ht_csa {
	uint8_t	mode;		/* 1: no transmissions until the switch */
	uint8_t	chan;		/* new primary channel */
	uint8_t	count;		/* beacons left; 0: any time now */
	uint8_t	opclass;	/* new operating class, 0 if not given */
	uint8_t	have_sco;	/* secondary channel offset element seen */
	uint8_t	sco;		/* IWX_HT_SCO_* */
	uint8_t	have_wbcs;	/* wide bandwidth channel switch seen */
	uint8_t	width;		/* as in the VHT operation element */
	uint8_t	ccfs0;
	uint8_t	ccfs1;
};

/* TLC_MNG_CONFIG fields that depend on the negotiation. */
struct iwx_ht_tlc {
	uint8_t		mode;		/* IWX_HT_TLC_MODE_* */
	uint8_t		width;		/* IWX_HT_WIDTH_* (TLC uses 0..2 too) */
	uint8_t		sgi;		/* bit per width with short GI */
	uint16_t	flags;		/* IWX_HT_TLC_STBC | IWX_HT_TLC_LDPC */
	uint16_t	rates[2];	/* MCS bitmap per spatial stream */
	uint16_t	rates160[2];	/* the same at 160 MHz */
	uint16_t	max_mpdu_len;
};

/*
 * Media subtype and mode for the negotiated TLC mode, or 0 to keep the
 * legacy word of net80211, which knows no HT or VHT mode.
 */
static inline int
iwx_ht_media(int tlc_mode, int band5)
{
	switch (tlc_mode) {
	case IWX_HT_TLC_MODE_VHT:
	case IWX_HT_TLC_MODE_HE:	/* <net/if_media.h> has no 11ax word */
		return IWX_HT_IFM_VHT | IWX_HT_IFM_11AC;
	case IWX_HT_TLC_MODE_HT:
		return IWX_HT_IFM_MCS |
		    (band5 ? IWX_HT_IFM_11NA : IWX_HT_IFM_11NG);
	default:
		return 0;
	}
}

static inline uint16_t
iwx_ht_le16(const uint8_t *p)
{
	return (uint16_t)(p[0] | (p[1] << 8));
}

static inline uint32_t
iwx_ht_le32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	    ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline void
iwx_ht_put16(uint8_t *p, uint16_t v)
{
	p[0] = v & 0xff;
	p[1] = v >> 8;
}

static inline void
iwx_ht_put32(uint8_t *p, uint32_t v)
{
	p[0] = v & 0xff;
	p[1] = (v >> 8) & 0xff;
	p[2] = (v >> 16) & 0xff;
	p[3] = v >> 24;
}

/* Check that [ies, ies + len) is a sequence of whole elements. */
static inline int
iwx_ht_ies_valid(const uint8_t *ies, size_t len)
{
	size_t off = 0;

	while (off < len) {
		if (len - off < 2 || len - off - 2 < ies[off + 1])
			return 0;
		off += 2 + ies[off + 1];
	}
	return 1;
}

/* Write the HT capabilities element; returns its size. */
static inline size_t
iwx_ht_put_htcap(uint8_t *frm, const struct iwx_ht_local *loc)
{
	uint16_t caps;
	int i;

	for (i = 0; i < 2 + IWX_HT_HTCAP_LEN; i++)
		frm[i] = 0;
	frm[0] = IWX_HT_EID_HTCAP;
	frm[1] = IWX_HT_HTCAP_LEN;
	caps = IWX_HT_CAP_LDPC | IWX_HT_CAP_SMPS_OFF | IWX_HT_CAP_SGI20 |
	    IWX_HT_CAP_RXSTBC_1;
	if (loc->band5 && loc->allow40)
		caps |= IWX_HT_CAP_CW40 | IWX_HT_CAP_SGI40;
	iwx_ht_put16(frm + 2, caps);
	/* A-MPDU: 64 KB, 4 us spacing; no BA session is set up anyway. */
	frm[4] = 0x03 | (5 << 2);
	/* Supported MCS set: RX MCS 0-7 per stream, TX set defined = RX. */
	frm[5] = 0xff;
	if (loc->nss >= 2)
		frm[6] = 0xff;
	frm[5 + 12] = 0x01;
	return 2 + IWX_HT_HTCAP_LEN;
}

/* Write the VHT capabilities element; returns its size. */
static inline size_t
iwx_ht_put_vhtcap(uint8_t *frm, const struct iwx_ht_local *loc)
{
	uint16_t map = 0;
	int ss;

	frm[0] = IWX_HT_EID_VHTCAP;
	frm[1] = IWX_HT_VHTCAP_LEN;
	iwx_ht_put32(frm + 2, IWX_VHT_CAP_RXLDPC | IWX_VHT_CAP_SGI80 |
	    IWX_VHT_CAP_RXSTBC_1 | IWX_VHT_CAP_AMPDU_EXP7 |
	    (loc->vht160 && loc->allow160 ?
	    IWX_VHT_CAP_CW160 | IWX_VHT_CAP_SGI160 : 0));
	/* MCS 0-9 for each supported stream, "not supported" for the rest. */
	for (ss = 0; ss < 8; ss++)
		map |= (uint16_t)((ss < loc->nss ? 2 : 3) << (2 * ss));
	iwx_ht_put16(frm + 6, map);	/* RX MCS map */
	iwx_ht_put16(frm + 8, 0);	/* RX highest rate: derive from map */
	iwx_ht_put16(frm + 10, map);	/* TX MCS map */
	iwx_ht_put16(frm + 12, 0);
	return 2 + IWX_HT_VHTCAP_LEN;
}

/*
 * Write the HE capabilities element; returns its size, 0 if HE is not
 * offered.  The values are those of Linux iwlwifi for a two-antenna FM
 * radio in station mode on 5 GHz, less what the host side here does not
 * do: no HT control field in received frames (+HTC-HE, OM control), no
 * A-MSDU inside A-MPDU, and A-MPDUs no longer than with VHT.  Triggered
 * transmissions, sounding feedback and the like are done by the firmware.
 */
static inline size_t
iwx_ht_put_hecap(uint8_t *frm, const struct iwx_ht_local *loc)
{
	static const uint8_t mac[6] = { 0x00, 0x78, 0x20, 0x80, 0x80, 0xab };
	static const uint8_t phy[11] = { 0x04, 0x3f, 0x4e, 0x09, 0xfd, 0x01,
	    0x8c, 0x16, 0x0e, 0xfc, 0x01 };
	/* PPE thresholds: 16 us on RU 996 and 2x996 for every stream. */
	static const uint8_t ppe2[4] = { 0x61, 0x1c, 0xc7, 0x71 };
	static const uint8_t ppe1[3] = { 0x60, 0x1c, 0x07 };
	int two = loc->nss >= 2, w160 = loc->vht160 && loc->allow160;
	uint16_t map = two ? 0xfffa : 0xfffe;	/* MCS 0-11 per stream */
	size_t n = 0, i;

	if (!loc->he || !loc->vht || !loc->band5)
		return 0;
	frm[n++] = IWX_HT_EID_EXT;
	frm[n++] = 0;				/* length, set below */
	frm[n++] = IWX_HE_EXT_CAP;
	for (i = 0; i < sizeof(mac); i++)
		frm[n++] = mac[i];
	for (i = 0; i < sizeof(phy); i++)
		frm[n++] = phy[i];
	if (w160) {
		frm[n - 11] |= 0x08;		/* 160 MHz in 5 GHz */
		frm[n - 6] |= 0x08;		/* sounding dimensions above 80 */
	}
	if (!two) {
		frm[n - 9] &= ~0x04;		/* no STBC transmission */
		frm[n - 6] = 0;			/* one sounding dimension */
		frm[n - 4] = 0x0e;		/* Nc 1 */
	}
	for (i = 0; i < (w160 ? 2U : 1U); i++) {
		iwx_ht_put16(frm + n, map);	/* RX */
		iwx_ht_put16(frm + n + 2, map);	/* TX */
		n += 4;
	}
	for (i = 0; i < (two ? sizeof(ppe2) : sizeof(ppe1)); i++)
		frm[n++] = two ? ppe2[i] : ppe1[i];
	frm[1] = (uint8_t)(n - 2);
	return n;
}

/*
 * Rebuild a (re)association request body with our HT/VHT capabilities.
 * `fixed' is the length of the fixed fields (4, or 10 for a reassociation).
 * Elements are emitted in the order 802.11 expects: the existing
 * non-vendor elements, HT capabilities, VHT capabilities, then vendor
 * elements (WMM).  Existing HT/VHT capabilities are replaced.
 * Returns the new body length, or 0 if the body is malformed or does not
 * fit in `outmax'; the caller then sends the original frame.
 */
static inline size_t
iwx_ht_rewrite_assoc(const uint8_t *body, size_t len, size_t fixed,
    const struct iwx_ht_local *loc, uint8_t *out, size_t outmax)
{
	uint8_t he[40];
	size_t off, n, need;
	int pass, i;

	if (len < fixed || !iwx_ht_ies_valid(body + fixed, len - fixed))
		return 0;
	need = len + 2 + IWX_HT_HTCAP_LEN +
	    ((loc->band5 && loc->vht) ? 2 + IWX_HT_VHTCAP_LEN : 0) +
	    iwx_ht_put_hecap(he, loc);
	if (need > outmax)
		return 0;

	for (i = 0; i < (int)fixed; i++)
		out[i] = body[i];
	n = fixed;
	for (pass = 0; pass < 2; pass++) {
		for (off = fixed; off < len; off += 2 + body[off + 1]) {
			uint8_t id = body[off], elen = body[off + 1];

			if (id == IWX_HT_EID_HTCAP || id == IWX_HT_EID_VHTCAP ||
			    (id == IWX_HT_EID_EXT && elen >= 1 &&
			    body[off + 2] == IWX_HE_EXT_CAP))
				continue;
			if ((id == IWX_HT_EID_VENDOR) != (pass == 1))
				continue;
			for (i = 0; i < 2 + elen; i++)
				out[n + i] = body[off + i];
			n += 2 + elen;
		}
		if (pass == 0) {
			n += iwx_ht_put_htcap(out + n, loc);
			if (loc->band5 && loc->vht)
				n += iwx_ht_put_vhtcap(out + n, loc);
			n += iwx_ht_put_hecap(out + n, loc);
		}
	}
	return n;
}

/*
 * Read the body of an HE capabilities element (after the extension ID):
 * MAC and PHY capabilities, one MCS/NSS pair per supported width class
 * and the optional PPE thresholds.  A short element is ignored.
 */
static inline void
iwx_ht_parse_hecap(const uint8_t *e, size_t len, struct iwx_ht_peer *p)
{
	size_t mcs, ppe = 0, i;

	if (len < 6 + 11 + 4)
		return;
	mcs = 4 + ((e[6] & 0x08) ? 4 : 0) + ((e[6] & 0x10) ? 4 : 0);
	if (len < 17 + mcs)
		return;
	if (e[6 + 6] & 0x80) {
		uint8_t hdr;
		unsigned nss, ru;

		if (len < 17 + mcs + 1)
			return;
		hdr = e[17 + mcs];
		nss = (hdr & 7) + 1;
		ru = ((hdr >> 3) & 1) + ((hdr >> 4) & 1) + ((hdr >> 5) & 1) +
		    ((hdr >> 6) & 1);
		ppe = (7 + 6 * nss * ru + 7) / 8;
		if (len < 17 + mcs + ppe || ppe > sizeof(p->he_ppe))
			return;
	}
	for (i = 0; i < 6; i++)
		p->he_mac[i] = e[i];
	for (i = 0; i < 11; i++)
		p->he_phy[i] = e[6 + i];
	p->he_rxmcs80 = iwx_ht_le16(e + 17);
	p->he_txmcs80 = iwx_ht_le16(e + 19);
	if (e[6] & 0x08) {
		p->he_rxmcs160 = iwx_ht_le16(e + 21);
		p->he_txmcs160 = iwx_ht_le16(e + 23);
	}
	for (i = 0; i < ppe; i++)
		p->he_ppe[i] = e[17 + mcs + i];
	p->he_ppe_len = (uint8_t)ppe;
	p->he = 1;
}

/* BSS colour of the AP and whether it is switched off (HE operation). */
static inline int
iwx_ht_he_color(const struct iwx_ht_peer *p)
{
	return (p->heop_params >> 24) & 0x3f;
}

static inline int
iwx_ht_he_color_disabled(const struct iwx_ht_peer *p)
{
	return (p->heop_params >> 31) & 1;
}

/*
 * Can this association run in HE mode: both elements of the AP, HE
 * offered here, and a basic HE-MCS and NSS set that asks for no stream
 * beyond ours (MCS 0-11 on the streams we have).  The request has
 * offered HE by then, so a refusal here is logged by the driver.
 */
static inline int
iwx_ht_he_usable(const struct iwx_ht_peer *p, const struct iwx_ht_local *loc)
{
	int ss;

	if (!loc->he || !loc->vht || !loc->band5 || !p->vht || !p->he ||
	    !p->heop)
		return 0;
	/* Some APs send an all-zero set; Linux takes it as no demand. */
	if (p->he_basic == 0)
		return 1;
	for (ss = loc->nss < 2 ? loc->nss : 2; ss < 8; ss++)
		if (((p->he_basic >> (2 * ss)) & 3) != 3)
			return 0;
	return 1;
}

/* MCS bitmap for one field of an HE MCS map: MCS 0-7, 0-9, 0-11, none. */
static inline uint16_t
iwx_ht_he_rates(uint16_t map, int ss)
{
	static const uint16_t rates[4] = { 0x00ff, 0x03ff, 0x0fff, 0 };

	return rates[(map >> (2 * ss)) & 3];
}

/* Three bits of the PPE thresholds field at bit position `pos'. */
static inline uint8_t
iwx_ht_he_ppe_bits(const struct iwx_ht_peer *p, unsigned pos)
{
	unsigned v = p->he_ppe[pos / 8];

	if (pos / 8 + 1 < sizeof(p->he_ppe))
		v |= (unsigned)p->he_ppe[pos / 8 + 1] << 8;
	return (v >> (pos % 8)) & 7;
}

/*
 * Packet extension thresholds for the firmware, indexed by stream,
 * width (20, 40, 80, 160, 320 MHz) and low/high threshold: constellation
 * numbers 0 (BPSK) to 5 (1024-QAM), 7 for none.  At or above the high
 * threshold the AP wants 16 us of packet extension, from the low one
 * 8 us.  Taken from the AP's PPE thresholds (PPET16 comes first), or
 * from its nominal packet padding when it sends none.
 */
static inline void
iwx_ht_he_pkt_ext(const struct iwx_ht_peer *p, uint8_t ext[2][5][2])
{
	unsigned nss, ru, pos = 7, pad;
	uint8_t low = 7, high = 7;
	int i, bw;

	for (i = 0; i < 2; i++)
		for (bw = 0; bw < 5; bw++)
			ext[i][bw][0] = ext[i][bw][1] = 7;
	if ((p->he_phy[6] & 0x80) && p->he_ppe_len > 0) {
		nss = (p->he_ppe[0] & 7) + 1;
		ru = (p->he_ppe[0] >> 3) & 0xf;
		for (i = 0; i < (int)nss && i < 2; i++)
			for (bw = 0; bw < 4; bw++) {
				if (!((ru >> bw) & 1))
					continue;
				if (pos + 6 > 8U * p->he_ppe_len)
					return;
				ext[i][bw][1] = iwx_ht_he_ppe_bits(p, pos);
				ext[i][bw][0] = iwx_ht_he_ppe_bits(p, pos + 3);
				pos += 6;
			}
		return;
	}
	pad = p->he_phy[9] >> 6;
	if (pad == 1)
		low = 0;		/* 8 us from BPSK up */
	else if (pad == 2)
		high = 0;		/* 16 us from BPSK up */
	else
		return;
	for (i = 0; i < 2; i++)
		for (bw = 0; bw < 5; bw++) {
			ext[i][bw][0] = low;
			ext[i][bw][1] = high;
		}
}

/*
 * Read HT/VHT elements from a (re)association response body.
 * `fixed' is 6 (capabilities, status, AID).  Returns 0 on success.
 */
static inline int
iwx_ht_parse(const uint8_t *body, size_t len, size_t fixed,
    struct iwx_ht_peer *p)
{
	size_t off;
	int i;

	for (i = 0; i < (int)sizeof(*p); i++)
		((uint8_t *)p)[i] = 0;
	if (len < fixed || !iwx_ht_ies_valid(body + fixed, len - fixed))
		return -1;
	for (off = fixed; off < len; off += 2 + body[off + 1]) {
		const uint8_t *e = body + off + 2;
		uint8_t elen = body[off + 1];

		switch (body[off]) {
		case IWX_HT_EID_HTCAP:
			if (elen < IWX_HT_HTCAP_LEN)
				break;
			p->ht = 1;
			p->htcaps = iwx_ht_le16(e);
			p->ampdu_param = e[2];
			for (i = 0; i < 10; i++)
				p->rxmcs[i] = e[3 + i];
			break;
		case IWX_HT_EID_HTOP:
			if (elen < IWX_HT_HTOP_LEN)
				break;
			p->htop = 1;
			p->primary = e[0];
			p->htop0 = e[1];
			p->htop1 = iwx_ht_le16(e + 2);
			break;
		case IWX_HT_EID_VHTCAP:
			if (elen < IWX_HT_VHTCAP_LEN)
				break;
			p->vht = 1;
			p->vhtcaps = iwx_ht_le32(e);
			p->vht_rxmcs = iwx_ht_le16(e + 4);
			break;
		case IWX_HT_EID_VHTOP:
			if (elen < IWX_HT_VHTOP_LEN)
				break;
			p->vhtop = 1;
			p->vht_width = e[0];
			p->vht_ccfs0 = e[1];
			p->vht_ccfs1 = e[2];
			break;
		case IWX_HT_EID_EXT:
			if (elen >= 1 && e[0] == IWX_HE_EXT_CAP)
				iwx_ht_parse_hecap(e + 1, elen - 1, p);
			else if (elen >= 7 && e[0] == IWX_HE_EXT_OP) {
				p->heop = 1;
				p->heop_params = iwx_ht_le32(e + 1);
				p->he_basic = iwx_ht_le16(e + 5);
			}
			break;
		}
	}
	/* VHT without HT is not a valid 802.11ac association. */
	if (!p->ht)
		p->vht = p->vhtop = 0;
	/* HE on 5 GHz comes with VHT. */
	if (!p->vht)
		p->he = p->heop = 0;
	return 0;
}

/*
 * The second centre channel of an AP whose VHT operation has width 1.
 * An AP that receives fewer streams at 160 MHz than at 80 MHz (Extended
 * NSS BW) names it in HT operation instead (CCFS2); an AP with neither
 * the 160 MHz capability nor Extended NSS BW has none (802.11-2020
 * Table 9-274, read as Linux does).
 */
static inline int
iwx_ht_vht_ccfs1(const struct iwx_ht_peer *p)
{
	int set = IWX_VHT_CAP_CWSET(p->vhtcaps);
	int ext = IWX_VHT_CAP_EXTNSS(p->vhtcaps);
	int ccfs2 = (p->htop1 >> 5) & 0xff;

	if (set == 0)
		return ext == 0 ? 0 : ccfs2;
	if (set == 1 && (ext == 1 || ext == 2) && p->vht_ccfs1 == 0)
		return ccfs2;
	return p->vht_ccfs1;
}

/* Spatial streams the AP receives at 160 MHz (the same table). */
static inline int
iwx_ht_vht_nss160(const struct iwx_ht_peer *p)
{
	int ext = IWX_VHT_CAP_EXTNSS(p->vhtcaps);
	int nss = 0, ss;

	for (ss = 0; ss < 8; ss++)
		if (((p->vht_rxmcs >> (2 * ss)) & 3) != 3)
			nss = ss + 1;
	if (IWX_VHT_CAP_CWSET(p->vhtcaps) != 0)
		return nss;
	if (ext == 3)
		return nss * 3 / 4;
	return ext == 0 ? 0 : nss / 2;
}

/*
 * Choose the channel layout.  `chan' is the IEEE number of the BSS
 * (primary) channel; HT operation must agree with it or 20 MHz is used.
 */
static inline void
iwx_ht_chan(const struct iwx_ht_peer *p, const struct iwx_ht_local *loc,
    int chan, struct iwx_ht_chan *c)
{
	int center = 0, center160 = 0, ccfs1, sco;

	c->sco = IWX_HT_SCO_NONE;
	c->vht80 = 0;
	c->width = IWX_HT_WIDTH_20;
	c->ctrl_pos = IWX_HT_POS_1_BELOW;

	if (!p->ht || !p->htop || !loc->band5 || p->primary != chan)
		return;

	sco = p->htop0 & 0x03;
	if ((p->htop0 & IWX_HT_STA_CHAN_WIDTH) &&
	    (p->htcaps & IWX_HT_CAP_CW40) && loc->allow40 &&
	    (sco == IWX_HT_SCO_ABOVE || sco == IWX_HT_SCO_BELOW)) {
		c->sco = sco;
		c->width = IWX_HT_WIDTH_40;
		c->ctrl_pos = (sco == IWX_HT_SCO_ABOVE) ?
		    IWX_HT_POS_1_BELOW : IWX_HT_POS_1_ABOVE;
	}

	if (!loc->vht || !p->vht || !p->vhtop || !loc->allow80 ||
	    c->width != IWX_HT_WIDTH_40)
		return;
	switch (p->vht_width) {
	case 1:	/* 80 MHz; with CCFS1 set, CCFS0 is the primary 80 */
		center = p->vht_ccfs0;
		/* A second centre 8 channels away is that of 160 MHz. */
		ccfs1 = iwx_ht_vht_ccfs1(p);
		if (ccfs1 == p->vht_ccfs0 + 8 || ccfs1 + 8 == p->vht_ccfs0)
			center160 = ccfs1;
		break;
	case 3:	/* deprecated 80+80: CCFS0 is the primary 80 */
		center = p->vht_ccfs0;
		break;
	case 2:	/* deprecated 160: CCFS0 is the 160 MHz centre */
		center = chan < p->vht_ccfs0 ?
		    p->vht_ccfs0 - 8 : p->vht_ccfs0 + 8;
		center160 = p->vht_ccfs0;
		break;
	default:
		return;
	}
	switch (chan - center) {
	case -6:
		c->ctrl_pos = IWX_HT_POS_2_BELOW;
		break;
	case -2:
		c->ctrl_pos = IWX_HT_POS_1_BELOW;
		break;
	case 2:
		c->ctrl_pos = IWX_HT_POS_1_ABOVE;
		break;
	case 6:
		c->ctrl_pos = IWX_HT_POS_2_ABOVE;
		break;
	default:
		return;		/* inconsistent operation element: keep 40 */
	}
	c->vht80 = 1;
	c->width = IWX_HT_WIDTH_80;

	/* 160 MHz only on its three 5 GHz channels and only if offered. */
	if (!loc->vht160 || !loc->allow160 ||
	    (center160 != 50 && center160 != 114 && center160 != 163))
		return;
	switch (chan - center160) {
	case -14:
		c->ctrl_pos = IWX_HT_POS_4_BELOW;
		break;
	case -10:
		c->ctrl_pos = IWX_HT_POS_3_BELOW;
		break;
	case -6:
		c->ctrl_pos = IWX_HT_POS_2_BELOW;
		break;
	case -2:
		c->ctrl_pos = IWX_HT_POS_1_BELOW;
		break;
	case 2:
		c->ctrl_pos = IWX_HT_POS_1_ABOVE;
		break;
	case 6:
		c->ctrl_pos = IWX_HT_POS_2_ABOVE;
		break;
	case 10:
		c->ctrl_pos = IWX_HT_POS_3_ABOVE;
		break;
	case 14:
		c->ctrl_pos = IWX_HT_POS_4_ABOVE;
		break;
	default:
		return;		/* keep the consistent 80 MHz layout */
	}
	c->width = IWX_HT_WIDTH_160;
}

/*
 * Collect a channel switch announcement from elements.  `have' says that
 * csa->chan is already known.  Returns 1 if the elements, or the caller,
 * name a new channel, 0 if not, and -1 for elements that are malformed or
 * disagree; the caller then keeps away from the announcement.
 */
static inline int
iwx_ht_csa_elems(const uint8_t *ies, size_t len, struct iwx_ht_csa *csa,
    int have)
{
	size_t off, sub;

	if (!iwx_ht_ies_valid(ies, len))
		return -1;
	for (off = 0; off < len; off += 2 + ies[off + 1]) {
		const uint8_t *e = ies + off + 2;
		uint8_t elen = ies[off + 1];
		uint8_t mode, chan, count;

		switch (ies[off]) {
		case IWX_HT_EID_CSA:
		case IWX_HT_EID_ECSA:
			if (ies[off] == IWX_HT_EID_CSA) {
				if (elen < 3)
					break;
				mode = e[0];
				chan = e[1];
				count = e[2];
			} else {
				if (elen < 4)
					break;
				mode = e[0];
				csa->opclass = e[1];
				chan = e[2];
				count = e[3];
			}
			if (have && csa->chan != chan)
				return -1;
			if (!have) {
				csa->mode = mode;
				csa->chan = chan;
				csa->count = count;
			}
			have = 1;
			break;
		case IWX_HT_EID_SCO:
			if (elen < 1)
				break;
			if (e[0] != IWX_HT_SCO_NONE &&
			    e[0] != IWX_HT_SCO_ABOVE &&
			    e[0] != IWX_HT_SCO_BELOW)
				return -1;
			csa->have_sco = 1;
			csa->sco = e[0];
			break;
		case IWX_HT_EID_CSWRAP:
			/* Beacons carry the new width inside the wrapper. */
			if (!iwx_ht_ies_valid(e, elen))
				return -1;
			for (sub = 0; sub < elen; sub += 2 + e[sub + 1]) {
				if (e[sub] != IWX_HT_EID_WBCS ||
				    e[sub + 1] < 3)
					continue;
				csa->have_wbcs = 1;
				csa->width = e[sub + 2];
				csa->ccfs0 = e[sub + 3];
				csa->ccfs1 = e[sub + 4];
			}
			break;
		case IWX_HT_EID_WBCS:
			if (elen < 3)
				break;
			csa->have_wbcs = 1;
			csa->width = e[0];
			csa->ccfs0 = e[1];
			csa->ccfs1 = e[2];
			break;
		}
	}
	return have;
}

/*
 * Look for a channel switch announcement among the elements of a beacon
 * or probe response.  Returns 1 with *csa filled, 0 if there is none and
 * -1 if the elements are malformed.
 */
static inline int
iwx_ht_csa_parse(const uint8_t *ies, size_t len, struct iwx_ht_csa *csa)
{
	int i;

	for (i = 0; i < (int)sizeof(*csa); i++)
		((uint8_t *)csa)[i] = 0;
	return iwx_ht_csa_elems(ies, len, csa, 0);
}

/*
 * The same for an action frame body, which starts with the category.
 * Channel Switch Announcement (spectrum management) carries elements;
 * Extended Channel Switch Announcement (public, or its protected dual)
 * carries the mode, operating class, channel and count as fixed fields.
 * Returns 0 for any other action frame.
 */
static inline int
iwx_ht_csa_action(const uint8_t *body, size_t len, struct iwx_ht_csa *csa)
{
	int i;

	for (i = 0; i < (int)sizeof(*csa); i++)
		((uint8_t *)csa)[i] = 0;
	if (len < 2 || body[1] != 4)
		return 0;
	if (body[0] == 0)
		return iwx_ht_csa_elems(body + 2, len - 2, csa, 0) == 1 ?
		    1 : -1;
	if (body[0] != 4 && body[0] != 9)
		return 0;
	if (len < 6)
		return -1;
	csa->mode = body[2];
	csa->opclass = body[3];
	csa->chan = body[4];
	csa->count = body[5];
	return iwx_ht_csa_elems(body + 6, len - 6, csa, 1) == 1 ? 1 : -1;
}

/*
 * The announcement names a 6 GHz operating class (802.11-2020 Table E-4).
 * Channel numbers there overlap those of 2.4 and 5 GHz, so such a switch
 * is never taken for a channel of the current band.
 */
static inline int
iwx_ht_csa_6ghz(const struct iwx_ht_csa *csa)
{
	return csa->opclass >= 131 && csa->opclass <= 137;
}

/*
 * Where the secondary channel of a 40 MHz pair lies when `chan' is the
 * primary channel in the 5 GHz band; IWX_HT_SCO_NONE if it has no pair.
 */
static inline int
iwx_ht_sco5(int chan)
{
	int base;

	if (chan >= 36 && chan <= 64)
		base = 36;
	else if (chan >= 100 && chan <= 144)
		base = 100;
	else if (chan >= 149 && chan <= 161)
		base = 149;
	else
		return IWX_HT_SCO_NONE;
	if ((chan - base) % 4 != 0)
		return IWX_HT_SCO_NONE;
	return (chan - base) % 8 == 0 ? IWX_HT_SCO_ABOVE : IWX_HT_SCO_BELOW;
}

/*
 * The AP's operation after the announced switch: its capabilities stay,
 * the HT and VHT operation follow the announcement.  Without a secondary
 * channel offset or a new width the BSS is 20 MHz wide afterwards.
 */
static inline void
iwx_ht_csa_peer(const struct iwx_ht_peer *old, const struct iwx_ht_csa *csa,
    struct iwx_ht_peer *p)
{
	int sco = IWX_HT_SCO_NONE;

	*p = *old;
	if (csa->have_sco)
		sco = csa->sco;
	else if (csa->have_wbcs)
		sco = iwx_ht_sco5(csa->chan);
	p->primary = csa->chan;
	p->htop0 = old->htop0 & ~(0x03 | IWX_HT_STA_CHAN_WIDTH);
	if (sco != IWX_HT_SCO_NONE)
		p->htop0 |= sco | IWX_HT_STA_CHAN_WIDTH;
	p->vht_width = csa->have_wbcs ? csa->width : 0;
	p->vht_ccfs0 = csa->have_wbcs ? csa->ccfs0 : 0;
	p->vht_ccfs1 = csa->have_wbcs ? csa->ccfs1 : 0;
}

/*
 * Channel layout on the announced channel `chan' in *nc.  Returns 1 if
 * the association can move there as it is: the band and the width stay.
 * Anything else needs a new negotiation with the AP.
 */
static inline int
iwx_ht_csa_follow(const struct iwx_ht_peer *p, const struct iwx_ht_local *loc,
    int band5, const struct iwx_ht_chan *cur, int chan,
    struct iwx_ht_chan *nc)
{
	iwx_ht_chan(p, loc, chan, nc);
	return (loc->band5 != 0) == (band5 != 0) && nc->width == cur->width;
}

/*
 * Beacon loss as the firmware counts it: `missed' consecutive beacons
 * since the last one received.  Returns 1 each time the count passes
 * another multiple of IWX_HT_BMISS_STEP, 0 otherwise; *level is the
 * multiple acted on last and falls back when beacons arrive again.  The
 * firmware repeats its notification and may skip counts.
 */
#define IWX_HT_BMISS_STEP	8

static inline int
iwx_ht_bmiss(uint32_t missed, uint32_t *level)
{
	uint32_t l = missed / IWX_HT_BMISS_STEP;

	if (l <= *level) {
		*level = l;
		return 0;
	}
	*level = l;
	return 1;
}

/* TLC settings for the negotiated mode. */
static inline void
iwx_ht_tlc(const struct iwx_ht_peer *p, const struct iwx_ht_local *loc,
    const struct iwx_ht_chan *c, struct iwx_ht_tlc *t)
{
	int ss;

	t->mode = IWX_HT_TLC_MODE_NON_HT;
	t->width = IWX_HT_WIDTH_20;
	t->sgi = 0;
	t->flags = 0;
	t->rates[0] = t->rates[1] = 0;
	t->rates160[0] = t->rates160[1] = 0;
	t->max_mpdu_len = 2346;		/* IEEE80211_MAX_LEN */
	if (!p->ht)
		return;

	t->width = c->width;
	if (p->htcaps & IWX_HT_CAP_SGI20)
		t->sgi |= 1 << IWX_HT_WIDTH_20;
	if ((p->htcaps & IWX_HT_CAP_SGI40) && c->width >= IWX_HT_WIDTH_40)
		t->sgi |= 1 << IWX_HT_WIDTH_40;
	if (p->htcaps & IWX_HT_CAP_LDPC)
		t->flags |= IWX_HT_TLC_LDPC;
	if (loc->nss >= 2 && (p->htcaps & IWX_HT_CAP_RXSTBC_MASK))
		t->flags |= IWX_HT_TLC_STBC;

	if (iwx_ht_he_usable(p, loc)) {
		/* HE rates only: no HT or VHT short GI, HE flags. */
		t->mode = IWX_HT_TLC_MODE_HE;
		t->max_mpdu_len = 3895;
		t->sgi = 0;
		t->flags = 0;
		if (p->he_phy[1] & 0x20)
			t->flags |= IWX_HT_TLC_LDPC;
		if (loc->nss >= 2 && (p->he_phy[2] & 0x08) &&
		    (c->width != IWX_HT_WIDTH_160 || (p->he_phy[7] & 0x80)))
			t->flags |= IWX_HT_TLC_STBC;
		if (p->he_phy[3] & 0x18)
			t->flags |= IWX_HT_TLC_HE_DCM_NSS_1;
		for (ss = 0; ss < 2 && ss < loc->nss; ss++) {
			t->rates[ss] = iwx_ht_he_rates(p->he_rxmcs80, ss);
			if (c->width == IWX_HT_WIDTH_160 &&
			    (p->he_phy[0] & 0x08))
				t->rates160[ss] =
				    iwx_ht_he_rates(p->he_rxmcs160, ss);
		}
		return;
	}

	if (loc->vht && p->vht && loc->band5) {
		t->mode = IWX_HT_TLC_MODE_VHT;
		t->max_mpdu_len = 3895;
		if ((p->vhtcaps & IWX_VHT_CAP_SGI80) && c->vht80)
			t->sgi |= 1 << IWX_HT_WIDTH_80;
		if ((p->vhtcaps & IWX_VHT_CAP_SGI160) &&
		    c->width == IWX_HT_WIDTH_160)
			t->sgi |= 1 << IWX_HT_WIDTH_160;
		if (p->vhtcaps & IWX_VHT_CAP_RXLDPC)
			t->flags |= IWX_HT_TLC_LDPC;
		if (loc->nss >= 2 && (p->vhtcaps & IWX_VHT_CAP_RXSTBC_MASK))
			t->flags |= IWX_HT_TLC_STBC;
		for (ss = 0; ss < 2 && ss < loc->nss; ss++) {
			switch ((p->vht_rxmcs >> (2 * ss)) & 3) {
			case 0:
				t->rates[ss] = 0x00ff;
				break;
			case 1:
				t->rates[ss] = 0x01ff;
				break;
			case 2:
				/* MCS 9 is not valid at 20 MHz with 1-2 SS. */
				t->rates[ss] = c->width == IWX_HT_WIDTH_20 ?
				    0x01ff : 0x03ff;
				break;
			default:
				break;
			}
			/*
			 * MCS 0-9 are all valid at 160 MHz with 1-2 SS, for
			 * as many streams as the AP receives at that width.
			 */
			if (c->width == IWX_HT_WIDTH_160 &&
			    ss < iwx_ht_vht_nss160(p))
				t->rates160[ss] = t->rates[ss];
		}
		return;
	}

	t->mode = IWX_HT_TLC_MODE_HT;
	t->max_mpdu_len = 3839;
	for (ss = 0; ss < 2 && ss < loc->nss; ss++)
		t->rates[ss] = p->rxmcs[ss];
}

/*
 * Answer an ADDBA request (Block Ack category 3, action 0) with
 * "request declined" (status 37).  net80211 cannot answer it, and some
 * APs drop an HT station that leaves ADDBA requests unanswered.
 * Returns the response body length (9), or 0 if `req' is not a request.
 */
static inline size_t
iwx_ht_addba_decline(const uint8_t *req, size_t len, uint8_t *resp)
{
	if (len < 9 || req[0] != 3 || req[1] != 0)
		return 0;
	resp[0] = 3;			/* Block Ack */
	resp[1] = 1;			/* ADDBA response */
	resp[2] = req[2];		/* dialog token */
	iwx_ht_put16(resp + 3, 37);	/* request declined */
	resp[5] = req[3];		/* Block Ack parameter set */
	resp[6] = req[4];
	iwx_ht_put16(resp + 7, 0);	/* timeout */
	return 9;
}

/* Block Ack (802.11-2020 9.6.4): ADDBA/DELBA and the receive reorder. */
struct iwx_ht_addba {
	uint8_t		token;
	uint8_t		tid;
	uint8_t		amsdu;
	uint8_t		immediate;
	uint16_t	bufsize;	/* 0: the originator lets us choose */
	uint16_t	timeout;	/* TUs, 0 = none */
	uint16_t	ssn;		/* starting sequence number */
};

/* Parse an ADDBA request body.  Returns 0, or -1 if it is not one. */
static inline int
iwx_ht_addba_parse(const uint8_t *b, size_t len, struct iwx_ht_addba *a)
{
	uint16_t params;

	if (len < 9 || b[0] != 3 || b[1] != 0)
		return -1;
	params = iwx_ht_le16(b + 3);
	a->token = b[2];
	a->amsdu = params & 0x1;
	a->immediate = (params >> 1) & 0x1;
	a->tid = (params >> 2) & 0xf;
	a->bufsize = (params >> 6) & 0x3ff;
	a->timeout = iwx_ht_le16(b + 5);
	a->ssn = iwx_ht_le16(b + 7) >> 4;
	return 0;
}

/*
 * Build an ADDBA response accepting `a' with a window of at most
 * `ourmax' frames.  A-MSDU in A-MPDU is refused (net80211's software
 * CCMP would drop subframes sharing a PN).  The timeout is echoed; the
 * driver keeps no inactivity timer of its own.  Returns the length (9).
 */
static inline size_t
iwx_ht_addba_accept(const struct iwx_ht_addba *a, uint16_t ourmax,
    uint8_t *resp)
{
	uint16_t size = (a->bufsize == 0 || a->bufsize > ourmax) ?
	    ourmax : a->bufsize;

	resp[0] = 3;
	resp[1] = 1;
	resp[2] = a->token;
	iwx_ht_put16(resp + 3, 0);	/* success */
	iwx_ht_put16(resp + 5, (uint16_t)((size << 6) | (a->tid << 2) | 0x2));
	iwx_ht_put16(resp + 7, a->timeout);	/* echo; we never time out */
	return 9;
}

/* Parse a DELBA body.  Returns 0, or -1 if it is not one. */
static inline int
iwx_ht_delba_parse(const uint8_t *b, size_t len, int *tid, int *initiator)
{
	uint16_t params;

	if (len < 6 || b[0] != 3 || b[1] != 2)
		return -1;
	params = iwx_ht_le16(b + 2);
	*initiator = (params >> 11) & 0x1;
	*tid = (params >> 12) & 0xf;
	return 0;
}

#define IWX_HT_SEQ_LT(a, b)	((((a) - (b)) & 0xfff) > 2048)
#define IWX_HT_RO_OLD_SN	0x1	/* firmware: SN below the window */
#define IWX_HT_RO_DUP		0x2	/* firmware: duplicate */
#define IWX_HT_RO_MAX		64

/*
 * Receive reorder buffer for one BA session, after Linux iwlwifi.  The
 * firmware gives each MPDU its SN and the window's next expected SN
 * (NSSN); frames are handed on strictly in SN order.
 */
struct iwx_ht_reorder {
	uint16_t	head_sn;
	uint16_t	num_stored;
	uint16_t	buf_size;
	int		valid;
	void		*slot[IWX_HT_RO_MAX];
};

typedef void (*iwx_ht_pkt_fn)(void *, void *);

static inline void
iwx_ht_reorder_init(struct iwx_ht_reorder *r, uint16_t ssn, uint16_t size)
{
	int i;

	r->head_sn = ssn & 0xfff;
	r->num_stored = 0;
	r->buf_size = (size == 0 || size > IWX_HT_RO_MAX) ? IWX_HT_RO_MAX : size;
	r->valid = 0;
	for (i = 0; i < IWX_HT_RO_MAX; i++)
		r->slot[i] = NULL;
}

/* Release stored frames below `nssn' and move the head there. */
static inline void
iwx_ht_reorder_release(struct iwx_ht_reorder *r, uint16_t nssn,
    iwx_ht_pkt_fn release, void *ctx)
{
	nssn &= 0xfff;
	while (IWX_HT_SEQ_LT(r->head_sn, nssn)) {
		int i = r->head_sn % r->buf_size;

		if (r->slot[i] != NULL) {
			void *pkt = r->slot[i];

			r->slot[i] = NULL;
			r->num_stored--;
			release(ctx, pkt);
		}
		r->head_sn = (r->head_sn + 1) & 0xfff;
	}
}

/* Release everything stored, in SN order (session teardown). */
static inline void
iwx_ht_reorder_flush(struct iwx_ht_reorder *r, iwx_ht_pkt_fn release,
    void *ctx)
{
	int n;

	for (n = 0; n < r->buf_size && r->num_stored > 0; n++)
		iwx_ht_reorder_release(r, (r->head_sn + 1) & 0xfff, release,
		    ctx);
}

/*
 * Take one MPDU.  Every packet ends in exactly one call of `release'
 * (now or later, in order) or `drop'.
 */
static inline void
iwx_ht_reorder_rx(struct iwx_ht_reorder *r, uint16_t sn, uint16_t nssn,
    int flags, void *pkt, iwx_ht_pkt_fn release, iwx_ht_pkt_fn drop,
    void *ctx)
{
	int i;

	sn &= 0xfff;
	nssn &= 0xfff;
	if (!r->valid) {
		if (flags & IWX_HT_RO_OLD_SN) {
			release(ctx, pkt);	/* sent before the session */
			return;
		}
		r->valid = 1;
	}
	if (flags & (IWX_HT_RO_DUP | IWX_HT_RO_OLD_SN)) {
		drop(ctx, pkt);
		return;
	}
	if (r->num_stored == 0 && IWX_HT_SEQ_LT(sn, nssn)) {
		r->head_sn = nssn;
		release(ctx, pkt);
		return;
	}
	if (r->num_stored == 0 && sn == r->head_sn) {
		r->head_sn = (r->head_sn + 1) & 0xfff;
		release(ctx, pkt);
		return;
	}
	i = sn % r->buf_size;
	if (r->slot[i] != NULL) {
		drop(ctx, pkt);
		return;
	}
	r->slot[i] = pkt;
	r->num_stored++;
	iwx_ht_reorder_release(r, nssn, release, ctx);
}

#endif /* _IF_IWX_HT_H_ */
