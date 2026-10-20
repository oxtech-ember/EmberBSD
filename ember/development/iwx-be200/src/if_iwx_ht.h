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
#define IWX_VHT_CAP_RXLDPC	0x00000010
#define IWX_VHT_CAP_SGI80	0x00000020
#define IWX_VHT_CAP_RXSTBC_MASK	0x00000700
#define IWX_VHT_CAP_RXSTBC_1	0x00000100
#define IWX_VHT_CAP_AMPDU_EXP7	(7U << 23)

/* Firmware values, checked against if_iwxreg.h by the driver. */
#define IWX_HT_POS_1_BELOW	0
#define IWX_HT_POS_2_BELOW	1
#define IWX_HT_POS_1_ABOVE	4
#define IWX_HT_POS_2_ABOVE	5
#define IWX_HT_WIDTH_20		0
#define IWX_HT_WIDTH_40		1
#define IWX_HT_WIDTH_80		2
#define IWX_HT_TLC_MODE_NON_HT	0
#define IWX_HT_TLC_MODE_HT	1
#define IWX_HT_TLC_MODE_VHT	2
#define IWX_HT_TLC_STBC		0x0001
#define IWX_HT_TLC_LDPC		0x0002

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
};

/* Channel layout for the PHY context and TLC. */
struct iwx_ht_chan {
	uint8_t	sco;		/* IWX_HT_SCO_* in effect */
	uint8_t	vht80;		/* 80 MHz VHT operation */
	uint8_t	width;		/* IWX_HT_WIDTH_* */
	uint8_t	ctrl_pos;	/* IWX_HT_POS_* */
};

/* TLC_MNG_CONFIG fields that depend on the negotiation. */
struct iwx_ht_tlc {
	uint8_t		mode;		/* IWX_HT_TLC_MODE_* */
	uint8_t		width;		/* IWX_HT_WIDTH_* (TLC uses 0..2 too) */
	uint8_t		sgi;		/* bit per width with short GI */
	uint16_t	flags;		/* IWX_HT_TLC_STBC | IWX_HT_TLC_LDPC */
	uint16_t	rates[2];	/* MCS bitmap per spatial stream */
	uint16_t	max_mpdu_len;
};

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
	    IWX_VHT_CAP_RXSTBC_1 | IWX_VHT_CAP_AMPDU_EXP7);
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
	size_t off, n, need;
	int pass, i;

	if (len < fixed || !iwx_ht_ies_valid(body + fixed, len - fixed))
		return 0;
	need = len + 2 + IWX_HT_HTCAP_LEN +
	    ((loc->band5 && loc->vht) ? 2 + IWX_HT_VHTCAP_LEN : 0);
	if (need > outmax)
		return 0;

	for (i = 0; i < (int)fixed; i++)
		out[i] = body[i];
	n = fixed;
	for (pass = 0; pass < 2; pass++) {
		for (off = fixed; off < len; off += 2 + body[off + 1]) {
			uint8_t id = body[off], elen = body[off + 1];

			if (id == IWX_HT_EID_HTCAP || id == IWX_HT_EID_VHTCAP)
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
		}
	}
	return n;
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
		}
	}
	/* VHT without HT is not a valid 802.11ac association. */
	if (!p->ht)
		p->vht = p->vhtop = 0;
	return 0;
}

/*
 * Choose the channel layout.  `chan' is the IEEE number of the BSS
 * (primary) channel; HT operation must agree with it or 20 MHz is used.
 */
static inline void
iwx_ht_chan(const struct iwx_ht_peer *p, const struct iwx_ht_local *loc,
    int chan, struct iwx_ht_chan *c)
{
	int center = 0, sco;

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
	case 3:	/* deprecated 80+80: CCFS0 is the primary 80 */
		center = p->vht_ccfs0;
		break;
	case 2:	/* deprecated 160: CCFS0 is the 160 MHz centre */
		center = chan < p->vht_ccfs0 ?
		    p->vht_ccfs0 - 8 : p->vht_ccfs0 + 8;
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

	if (loc->vht && p->vht && loc->band5) {
		t->mode = IWX_HT_TLC_MODE_VHT;
		t->max_mpdu_len = 3895;
		if ((p->vhtcaps & IWX_VHT_CAP_SGI80) && c->vht80)
			t->sgi |= 1 << IWX_HT_WIDTH_80;
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
