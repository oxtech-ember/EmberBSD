/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD iwx(4) HT/VHT negotiation contract, AI-assisted. */
/*
 * Host contract for if_iwx_ht.h: the association request rewrite, the
 * association response parser and the channel and TLC choices made from
 * them.  The header is compiled unmodified.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "if_iwx_ht.h"

static unsigned int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static const struct iwx_ht_local loc5 = { 1, 2, 1, 1, 1, 0, 0, 0 };
static const struct iwx_ht_local loc2 = { 0, 2, 1, 1, 1, 0, 0, 0 };

/* Find element `id' in out[fixed..len); returns its offset or -1. */
static int
find_ie(const uint8_t *b, size_t len, size_t fixed, uint8_t id)
{
	size_t off;

	for (off = fixed; off + 2 <= len; off += 2 + b[off + 1])
		if (b[off] == id)
			return (int)off;
	return -1;
}

static size_t
assoc_req(uint8_t *b, int reassoc, int with_htcap)
{
	static const uint8_t ssid[] = "Netcraze-1387";
	static const uint8_t rates[] = { 1, 8, 0x8c, 0x12, 0x98, 0x24,
	    0xb0, 0x48, 0x60, 0x6c };
	static const uint8_t wme[] = { 221, 7, 0x00, 0x50, 0xf2, 0x02, 0x00,
	    0x01, 0x00 };
	static const uint8_t rsn[] = { 48, 20, 1, 0, 0x00, 0x0f, 0xac, 4, 1,
	    0, 0x00, 0x0f, 0xac, 4, 1, 0, 0x00, 0x0f, 0xac, 2, 0, 0 };
	size_t n = 0;

	b[n++] = 0x11; b[n++] = 0x00;	/* capinfo */
	b[n++] = 0x0a; b[n++] = 0x00;	/* listen interval */
	if (reassoc) {
		memset(b + n, 0xaa, 6);
		n += 6;
	}
	b[n++] = 0;
	b[n++] = sizeof(ssid) - 1;
	memcpy(b + n, ssid, sizeof(ssid) - 1);
	n += sizeof(ssid) - 1;
	memcpy(b + n, rates, sizeof(rates));
	n += sizeof(rates);
	memcpy(b + n, wme, sizeof(wme));
	n += sizeof(wme);
	if (with_htcap) {
		b[n++] = IWX_HT_EID_HTCAP;
		b[n++] = IWX_HT_HTCAP_LEN;
		memset(b + n, 0x5a, IWX_HT_HTCAP_LEN);
		n += IWX_HT_HTCAP_LEN;
	}
	memcpy(b + n, rsn, sizeof(rsn));
	n += sizeof(rsn);
	return n;
}

static void
test_rewrite(void)
{
	uint8_t in[256], out[512];
	size_t len, n;
	int ssid, rates, rsn, ht, vht, wme;

	len = assoc_req(in, 0, 0);
	n = iwx_ht_rewrite_assoc(in, len, 4, &loc5, out, sizeof(out));
	CHECK(n == len + 28 + 14);
	CHECK(memcmp(out, in, 4) == 0);
	ssid = find_ie(out, n, 4, 0);
	rates = find_ie(out, n, 4, 1);
	rsn = find_ie(out, n, 4, 48);
	ht = find_ie(out, n, 4, IWX_HT_EID_HTCAP);
	vht = find_ie(out, n, 4, IWX_HT_EID_VHTCAP);
	wme = find_ie(out, n, 4, IWX_HT_EID_VENDOR);
	/* 802.11 order: SSID, rates, RSN, HT caps, VHT caps, vendor. */
	CHECK(ssid == 4);
	CHECK(ssid < rates && rates < rsn && rsn < ht && ht < vht &&
	    vht < wme);
	CHECK(wme + 9 == (int)n);
	CHECK(out[ht + 1] == 26);
	CHECK(iwx_ht_le16(out + ht + 2) == 0x016f);
	CHECK(out[ht + 4] == 0x17);
	CHECK(out[ht + 5] == 0xff && out[ht + 6] == 0xff &&
	    out[ht + 7] == 0x00);
	CHECK(out[ht + 17] == 0x01);
	CHECK(out[vht + 1] == 12);
	CHECK(iwx_ht_le32(out + vht + 2) == 0x03800130);
	CHECK(iwx_ht_le16(out + vht + 6) == 0xfffa);
	CHECK(iwx_ht_le16(out + vht + 10) == 0xfffa);
	CHECK(iwx_ht_ies_valid(out + 4, n - 4));

	/* 2.4 GHz: HT 20 MHz only, no VHT. */
	n = iwx_ht_rewrite_assoc(in, len, 4, &loc2, out, sizeof(out));
	CHECK(n == len + 28);
	ht = find_ie(out, n, 4, IWX_HT_EID_HTCAP);
	CHECK(ht > 0 && iwx_ht_le16(out + ht + 2) == 0x012d);
	CHECK(find_ie(out, n, 4, IWX_HT_EID_VHTCAP) < 0);

	/* One stream: only MCS 0-7, VHT map 1 SS. */
	{
		struct iwx_ht_local l1 = loc5;

		l1.nss = 1;
		l1.allow40 = 0;
		n = iwx_ht_rewrite_assoc(in, len, 4, &l1, out, sizeof(out));
		ht = find_ie(out, n, 4, IWX_HT_EID_HTCAP);
		vht = find_ie(out, n, 4, IWX_HT_EID_VHTCAP);
		CHECK(out[ht + 5] == 0xff && out[ht + 6] == 0x00);
		CHECK((iwx_ht_le16(out + ht + 2) & IWX_HT_CAP_CW40) == 0);
		CHECK(iwx_ht_le16(out + vht + 6) == 0xfffe);
	}

	/* Reassociation keeps the 10-byte fixed part. */
	len = assoc_req(in, 1, 0);
	n = iwx_ht_rewrite_assoc(in, len, 10, &loc5, out, sizeof(out));
	CHECK(n == len + 42);
	CHECK(memcmp(out, in, 10) == 0);
	CHECK(find_ie(out, n, 10, 0) == 10);

	/* An existing HT capabilities element is replaced, not duplicated. */
	len = assoc_req(in, 0, 1);
	n = iwx_ht_rewrite_assoc(in, len, 4, &loc5, out, sizeof(out));
	CHECK(n == len + 14);
	ht = find_ie(out, n, 4, IWX_HT_EID_HTCAP);
	CHECK(ht > 0 && out[ht + 2] != 0x5a);
	CHECK(find_ie(out + ht + 28, n - ht - 28, 0, IWX_HT_EID_HTCAP) < 0);

	/* Malformed or oversized input leaves the frame alone. */
	len = assoc_req(in, 0, 0);
	CHECK(iwx_ht_rewrite_assoc(in, len - 1, 4, &loc5, out,
	    sizeof(out)) == 0);
	CHECK(iwx_ht_rewrite_assoc(in, 3, 4, &loc5, out, sizeof(out)) == 0);
	CHECK(iwx_ht_rewrite_assoc(in, len, 4, &loc5, out, len + 41) == 0);
	CHECK(iwx_ht_rewrite_assoc(in, len, 4, &loc5, out, len + 42) ==
	    len + 42);
	/* Fixed part only. */
	CHECK(iwx_ht_rewrite_assoc(in, 4, 4, &loc5, out, sizeof(out)) ==
	    4 + 42);
}

/* Build an association response with the given elements. */
static size_t
assoc_resp(uint8_t *b, int primary, uint8_t htop0, int vht, uint8_t vw,
    uint8_t c0, uint8_t c1)
{
	size_t n = 0;

	b[n++] = 0x11; b[n++] = 0x04;	/* capinfo */
	b[n++] = 0; b[n++] = 0;		/* status */
	b[n++] = 0x01; b[n++] = 0xc0;	/* AID 1 */
	b[n++] = 1; b[n++] = 4; b[n++] = 0x8c; b[n++] = 0x98; b[n++] = 0xb0;
	b[n++] = 0x6c;
	b[n++] = IWX_HT_EID_HTCAP; b[n++] = 26;
	memset(b + n, 0, 26);
	iwx_ht_put16(b + n, 0x09ef);	/* LDPC CW40 SGI20/40 RXSTBC1 */
	b[n + 2] = 0x1b;
	b[n + 3] = 0xff; b[n + 4] = 0xff;
	n += 26;
	b[n++] = IWX_HT_EID_HTOP; b[n++] = 22;
	memset(b + n, 0, 22);
	b[n] = primary;
	b[n + 1] = htop0;
	iwx_ht_put16(b + n + 2, 0x0004);
	n += 22;
	if (vht) {
		b[n++] = IWX_HT_EID_VHTCAP; b[n++] = 12;
		memset(b + n, 0, 12);
		iwx_ht_put32(b + n, 0x0f8b79b2);	/* SGI80 RXLDPC RXSTBC */
		iwx_ht_put16(b + n + 4, 0xfffa);
		iwx_ht_put16(b + n + 8, 0xfffa);
		n += 12;
		b[n++] = IWX_HT_EID_VHTOP; b[n++] = 5;
		b[n] = vw; b[n + 1] = c0; b[n + 2] = c1;
		b[n + 3] = 0xfc; b[n + 4] = 0xff;
		n += 5;
	}
	b[n++] = 221; b[n++] = 4; b[n++] = 0x00; b[n++] = 0x50;
	b[n++] = 0xf2; b[n++] = 0x02;
	return n;
}

static void
chan_for(int primary, uint8_t htop0, int vht, uint8_t vw, uint8_t c0,
    uint8_t c1, const struct iwx_ht_local *loc, struct iwx_ht_peer *p,
    struct iwx_ht_chan *c)
{
	uint8_t b[256];
	size_t n = assoc_resp(b, primary, htop0, vht, vw, c0, c1);

	CHECK(iwx_ht_parse(b, n, 6, p) == 0);
	iwx_ht_chan(p, loc, primary, c);
}

static void
test_parse_and_chan(void)
{
	struct iwx_ht_peer p;
	struct iwx_ht_chan c;
	uint8_t b[256];
	size_t n;
	static const struct { int ch; uint8_t htop0; uint8_t pos; } vht80[] = {
		{ 36, 0x05, IWX_HT_POS_2_BELOW },
		{ 40, 0x07, IWX_HT_POS_1_BELOW },
		{ 44, 0x05, IWX_HT_POS_1_ABOVE },
		{ 48, 0x07, IWX_HT_POS_2_ABOVE },
		{ 149, 0x05, IWX_HT_POS_2_BELOW },
		{ 161, 0x07, IWX_HT_POS_2_ABOVE },
	};
	unsigned int i;

	/* Netcraze-1387 style: channel 149, VHT 80 MHz centred on 155. */
	n = assoc_resp(b, 149, 0x05, 1, 1, 155, 0);
	CHECK(iwx_ht_parse(b, n, 6, &p) == 0);
	CHECK(p.ht && p.htop && p.vht && p.vhtop);
	CHECK(p.htcaps == 0x09ef && p.ampdu_param == 0x1b);
	CHECK(p.rxmcs[0] == 0xff && p.rxmcs[1] == 0xff && p.rxmcs[2] == 0);
	CHECK(p.primary == 149 && p.htop0 == 0x05 && p.htop1 == 0x0004);
	CHECK(p.vhtcaps == 0x0f8b79b2 && p.vht_rxmcs == 0xfffa);
	CHECK(p.vht_width == 1 && p.vht_ccfs0 == 155 && p.vht_ccfs1 == 0);
	iwx_ht_chan(&p, &loc5, 149, &c);
	CHECK(c.vht80 && c.width == IWX_HT_WIDTH_80 &&
	    c.ctrl_pos == IWX_HT_POS_2_BELOW && c.sco == IWX_HT_SCO_ABOVE);

	for (i = 0; i < sizeof(vht80) / sizeof(vht80[0]); i++) {
		int ch = vht80[i].ch;
		int center = ch <= 48 ? 42 : 155;

		chan_for(ch, vht80[i].htop0, 1, 1, center, 0, &loc5, &p, &c);
		CHECK(c.vht80 && c.ctrl_pos == vht80[i].pos);
	}

	/* HT 40 MHz only, secondary below: control channel above. */
	chan_for(40, 0x07, 0, 0, 0, 0, &loc5, &p, &c);
	CHECK(!p.vht && c.width == IWX_HT_WIDTH_40 &&
	    c.ctrl_pos == IWX_HT_POS_1_ABOVE && c.sco == IWX_HT_SCO_BELOW);
	chan_for(36, 0x05, 0, 0, 0, 0, &loc5, &p, &c);
	CHECK(c.width == IWX_HT_WIDTH_40 && c.ctrl_pos == IWX_HT_POS_1_BELOW);

	/* AP restricts STAs to 20 MHz (STA channel width bit clear). */
	chan_for(36, 0x01, 1, 1, 42, 0, &loc5, &p, &c);
	CHECK(c.width == IWX_HT_WIDTH_20 && !c.vht80 && c.sco == 0);
	/* Regulatory forbids 40 MHz. */
	{
		struct iwx_ht_local l = loc5;

		l.allow40 = 0;
		chan_for(36, 0x05, 1, 1, 42, 0, &l, &p, &c);
		CHECK(c.width == IWX_HT_WIDTH_20);
		l = loc5;
		l.allow80 = 0;
		chan_for(36, 0x05, 1, 1, 42, 0, &l, &p, &c);
		CHECK(c.width == IWX_HT_WIDTH_40 && !c.vht80);
		l = loc5;
		l.vht = 0;
		chan_for(36, 0x05, 1, 1, 42, 0, &l, &p, &c);
		CHECK(c.width == IWX_HT_WIDTH_40 && !c.vht80);
	}
	/* 2.4 GHz stays at 20 MHz. */
	chan_for(6, 0x05, 0, 0, 0, 0, &loc2, &p, &c);
	CHECK(c.width == IWX_HT_WIDTH_20);
	/* HT operation for another channel: 20 MHz. */
	n = assoc_resp(b, 36, 0x05, 1, 1, 42, 0);
	CHECK(iwx_ht_parse(b, n, 6, &p) == 0);
	iwx_ht_chan(&p, &loc5, 40, &c);
	CHECK(c.width == IWX_HT_WIDTH_20);
	/* VHT operation says 20/40: stay at 40. */
	chan_for(36, 0x05, 1, 0, 0, 0, &loc5, &p, &c);
	CHECK(c.width == IWX_HT_WIDTH_40 && !c.vht80);
	/* 160 MHz, new signalling: CCFS0 is the primary 80. */
	chan_for(36, 0x05, 1, 1, 42, 50, &loc5, &p, &c);
	CHECK(c.vht80 && c.ctrl_pos == IWX_HT_POS_2_BELOW);
	chan_for(60, 0x07, 1, 1, 58, 50, &loc5, &p, &c);
	CHECK(c.vht80 && c.ctrl_pos == IWX_HT_POS_1_ABOVE);
	/* 160 MHz, deprecated signalling: CCFS0 is the 160 MHz centre. */
	chan_for(36, 0x05, 1, 2, 50, 0, &loc5, &p, &c);
	CHECK(c.vht80 && c.ctrl_pos == IWX_HT_POS_2_BELOW);
	chan_for(64, 0x07, 1, 2, 50, 0, &loc5, &p, &c);
	CHECK(c.vht80 && c.ctrl_pos == IWX_HT_POS_2_ABOVE);
	/* Inconsistent centre: keep HT 40. */
	chan_for(36, 0x05, 1, 1, 58, 0, &loc5, &p, &c);
	CHECK(c.width == IWX_HT_WIDTH_40 && !c.vht80);

	/* Legacy AP: no HT. */
	n = 0;
	b[n++] = 0x11; b[n++] = 0; b[n++] = 0; b[n++] = 0; b[n++] = 1;
	b[n++] = 0xc0; b[n++] = 1; b[n++] = 1; b[n++] = 0x8c;
	CHECK(iwx_ht_parse(b, n, 6, &p) == 0);
	CHECK(!p.ht && !p.vht);
	iwx_ht_chan(&p, &loc5, 36, &c);
	CHECK(c.width == IWX_HT_WIDTH_20);
	/* VHT without HT is ignored. */
	b[n++] = IWX_HT_EID_VHTCAP; b[n++] = 12;
	memset(b + n, 0, 12);
	n += 12;
	CHECK(iwx_ht_parse(b, n, 6, &p) == 0);
	CHECK(!p.vht);
	/* Short elements are ignored, truncated bodies rejected. */
	n = assoc_resp(b, 36, 0x05, 0, 0, 0, 0);
	b[13] = 10;			/* HT caps element too short */
	CHECK(iwx_ht_parse(b, 14 + 10, 6, &p) == 0 && !p.ht);
	n = assoc_resp(b, 36, 0x05, 0, 0, 0, 0);
	CHECK(iwx_ht_parse(b, n - 1, 6, &p) == -1);
	CHECK(iwx_ht_parse(b, 5, 6, &p) == -1);
}

static void
test_tlc(void)
{
	struct iwx_ht_peer p;
	struct iwx_ht_chan c;
	struct iwx_ht_tlc t;

	chan_for(149, 0x05, 1, 1, 155, 0, &loc5, &p, &c);
	iwx_ht_tlc(&p, &loc5, &c, &t);
	CHECK(t.mode == IWX_HT_TLC_MODE_VHT && t.width == IWX_HT_WIDTH_80);
	CHECK(t.rates[0] == 0x03ff && t.rates[1] == 0x03ff);
	CHECK(t.sgi == ((1 << 0) | (1 << 1) | (1 << 2)));
	CHECK(t.flags == (IWX_HT_TLC_LDPC | IWX_HT_TLC_STBC));
	CHECK(t.max_mpdu_len == 3895);

	/* One stream, AP limited to MCS 0-7 and 0-8. */
	{
		struct iwx_ht_local l1 = loc5;

		l1.nss = 1;
		p.vht_rxmcs = 0xfff4;	/* SS1 0-7, SS2 0-8 */
		iwx_ht_tlc(&p, &l1, &c, &t);
		CHECK(t.rates[0] == 0x00ff && t.rates[1] == 0);
		CHECK((t.flags & IWX_HT_TLC_STBC) == 0);
		iwx_ht_tlc(&p, &loc5, &c, &t);
		CHECK(t.rates[0] == 0x00ff && t.rates[1] == 0x01ff);
		p.vht_rxmcs = 0xfffe;	/* SS1 0-9, SS2 not supported */
		iwx_ht_tlc(&p, &loc5, &c, &t);
		CHECK(t.rates[0] == 0x03ff && t.rates[1] == 0);
	}

	/* VHT at 20 MHz drops MCS 9. */
	chan_for(36, 0x01, 1, 1, 42, 0, &loc5, &p, &c);
	iwx_ht_tlc(&p, &loc5, &c, &t);
	CHECK(t.mode == IWX_HT_TLC_MODE_VHT && t.width == IWX_HT_WIDTH_20);
	CHECK(t.rates[0] == 0x01ff && t.sgi == 1);

	/* HT 40 MHz. */
	chan_for(40, 0x07, 0, 0, 0, 0, &loc5, &p, &c);
	iwx_ht_tlc(&p, &loc5, &c, &t);
	CHECK(t.mode == IWX_HT_TLC_MODE_HT && t.width == IWX_HT_WIDTH_40);
	CHECK(t.rates[0] == 0xff && t.rates[1] == 0xff);
	CHECK(t.sgi == 3 && t.max_mpdu_len == 3839);
	/* VHT AP but VHT not offered: HT. */
	{
		struct iwx_ht_local l = loc5;

		l.vht = 0;
		chan_for(36, 0x05, 1, 1, 42, 0, &l, &p, &c);
		iwx_ht_tlc(&p, &l, &c, &t);
		CHECK(t.mode == IWX_HT_TLC_MODE_HT &&
		    t.width == IWX_HT_WIDTH_40);
	}
	/* Legacy. */
	memset(&p, 0, sizeof(p));
	iwx_ht_chan(&p, &loc5, 36, &c);
	iwx_ht_tlc(&p, &loc5, &c, &t);
	CHECK(t.mode == IWX_HT_TLC_MODE_NON_HT && t.rates[0] == 0 &&
	    t.max_mpdu_len == 2346 && t.sgi == 0);
}

static void
test_addba(void)
{
	/* ADDBA request: category 3, action 0, token 7, params, timeout, SSC. */
	static const uint8_t req[] = { 3, 0, 7, 0x02, 0x10, 0x00, 0x00,
	    0x10, 0x00 };
	uint8_t resp[16];

	memset(resp, 0xee, sizeof(resp));
	CHECK(iwx_ht_addba_decline(req, sizeof(req), resp) == 9);
	CHECK(resp[0] == 3 && resp[1] == 1 && resp[2] == 7);
	CHECK(iwx_ht_le16(resp + 3) == 37);	/* request declined */
	CHECK(resp[5] == 0x02 && resp[6] == 0x10);	/* parameters echoed */
	CHECK(resp[7] == 0 && resp[8] == 0);	/* no timeout */
	CHECK(resp[9] == 0xee);
	/* Not an ADDBA request, or too short: nothing to send. */
	CHECK(iwx_ht_addba_decline(req, 8, resp) == 0);
	{
		uint8_t other[9];

		memcpy(other, req, sizeof(other));
		other[1] = 1;		/* ADDBA response */
		CHECK(iwx_ht_addba_decline(other, 9, resp) == 0);
		other[0] = 4; other[1] = 0;	/* public action */
		CHECK(iwx_ht_addba_decline(other, 9, resp) == 0);
	}
}

static int released[64], nreleased, dropped;

static void
rel(void *ctx, void *pkt)
{
	(void)ctx;
	released[nreleased++] = (int)(intptr_t)pkt - 1;
}

static void
drop(void *ctx, void *pkt)
{
	(void)ctx; (void)pkt;
	dropped++;
}

#define RX(r, sn, nssn, f) iwx_ht_reorder_rx((r), (sn), (nssn), (f), \
    (void *)(intptr_t)((sn) + 1), rel, drop, NULL)	/* never NULL */

static void
test_ba_frames(void)
{
	static const uint8_t req[] = { 3, 0, 9, 0x1b, 0x10, 0x88, 0x13,
	    0x50, 0x01 };
	struct iwx_ht_addba a;
	uint8_t resp[16];

	/* params 0x101b: A-MSDU, immediate, TID 6, buffer 64; SSC 0x150. */
	CHECK(iwx_ht_addba_parse(req, sizeof(req), &a) == 0);
	CHECK(a.token == 9 && a.tid == 6 && a.bufsize == 64 && a.amsdu &&
	    a.immediate && a.timeout == 5000 && a.ssn == 0x15);
	CHECK(iwx_ht_addba_parse(req, 8, &a) == -1);
	CHECK(iwx_ht_addba_accept(&a, 32, resp) == 9);
	CHECK(resp[0] == 3 && resp[1] == 1 && resp[2] == 9);
	CHECK(iwx_ht_le16(resp + 3) == 0);
	/* immediate, TID 6, buffer 32, no A-MSDU */
	CHECK(iwx_ht_le16(resp + 5) == ((32 << 6) | (6 << 2) | 0x2));
	CHECK(iwx_ht_le16(resp + 7) == 5000);	/* timeout echoed */
	{
		/* Buffer size 0 in the request means "any": use ours. */
		uint8_t r0[9];

		memcpy(r0, req, 9);
		iwx_ht_put16(r0 + 3, 0x0002 | (2 << 2));
		CHECK(iwx_ht_addba_parse(r0, 9, &a) == 0 && a.bufsize == 0);
		iwx_ht_addba_accept(&a, 64, resp);
		CHECK(((iwx_ht_le16(resp + 5) >> 6) & 0x3ff) == 64);
	}
	{
		/* DELBA from the originator for TID 5. */
		uint8_t d[6] = { 3, 2, 0, 0, 37, 0 };
		int tid, initiator;

		iwx_ht_put16(d + 2, (5 << 12) | (1 << 11));
		CHECK(iwx_ht_delba_parse(d, 6, &tid, &initiator) == 0 &&
		    tid == 5 && initiator == 1);
		CHECK(iwx_ht_delba_parse(d, 5, &tid, &initiator) == -1);
		d[1] = 1;
		CHECK(iwx_ht_delba_parse(d, 6, &tid, &initiator) == -1);
	}
}

static void
test_reorder(void)
{
	struct iwx_ht_reorder r;

	/* In-order frames are released at once. */
	iwx_ht_reorder_init(&r, 100, 64);
	nreleased = dropped = 0;
	RX(&r, 100, 101, 0);
	RX(&r, 101, 102, 0);
	CHECK(nreleased == 2 && released[0] == 100 && released[1] == 101);
	CHECK(r.head_sn == 102 && r.num_stored == 0);

	/* A hole: 103 and 104 wait until 102 arrives. */
	nreleased = 0;
	RX(&r, 103, 102, 0);
	RX(&r, 104, 102, 0);
	CHECK(nreleased == 0 && r.num_stored == 2);
	RX(&r, 102, 105, 0);
	CHECK(nreleased == 3 && released[0] == 102 && released[1] == 103 &&
	    released[2] == 104 && r.head_sn == 105 && r.num_stored == 0);

	/* Old or duplicate frames are dropped. */
	dropped = 0;
	RX(&r, 104, 105, IWX_HT_RO_OLD_SN);
	RX(&r, 105, 106, IWX_HT_RO_DUP);
	CHECK(dropped == 2 && r.head_sn == 105);
	/* A frame stored twice: the second copy is dropped. */
	nreleased = dropped = 0;
	RX(&r, 107, 105, 0);
	RX(&r, 107, 105, 0);
	CHECK(dropped == 1 && r.num_stored == 1);

	/* Firmware frame release moves the window over the hole. */
	iwx_ht_reorder_release(&r, 108, rel, NULL);
	CHECK(nreleased == 1 && released[0] == 107 && r.head_sn == 108 &&
	    r.num_stored == 0);

	/* Sequence numbers wrap at 4096. */
	iwx_ht_reorder_init(&r, 4094, 64);
	nreleased = 0;
	RX(&r, 4095, 4094, 0);
	RX(&r, 0, 4094, 0);
	CHECK(nreleased == 0 && r.num_stored == 2);
	RX(&r, 4094, 1, 0);
	CHECK(nreleased == 3 && released[0] == 4094 && released[1] == 4095 &&
	    released[2] == 0 && r.head_sn == 1);

	/* Flush releases everything stored, in order. */
	iwx_ht_reorder_init(&r, 10, 8);
	nreleased = 0;
	RX(&r, 12, 10, 0);
	RX(&r, 15, 10, 0);
	iwx_ht_reorder_flush(&r, rel, NULL);
	CHECK(nreleased == 2 && released[0] == 12 && released[1] == 15 &&
	    r.num_stored == 0);

	/* The first frame of a session with an old SN passes through. */
	iwx_ht_reorder_init(&r, 50, 64);
	nreleased = dropped = 0;
	RX(&r, 40, 50, IWX_HT_RO_OLD_SN);
	CHECK(nreleased == 1 && dropped == 0 && !r.valid);
	RX(&r, 50, 51, 0);
	CHECK(r.valid && nreleased == 2 && r.head_sn == 51);
	CHECK(IWX_HT_SEQ_LT(4095, 0) && !IWX_HT_SEQ_LT(0, 4095) &&
	    !IWX_HT_SEQ_LT(7, 7));
}

/* ifconfig media word for the negotiated mode. */
static void
test_media(void)
{
	CHECK(iwx_ht_media(IWX_HT_TLC_MODE_VHT, 1) ==
	    (IWX_HT_IFM_VHT | IWX_HT_IFM_11AC));
	CHECK(iwx_ht_media(IWX_HT_TLC_MODE_HT, 1) ==
	    (IWX_HT_IFM_MCS | IWX_HT_IFM_11NA));
	CHECK(iwx_ht_media(IWX_HT_TLC_MODE_HT, 0) ==
	    (IWX_HT_IFM_MCS | IWX_HT_IFM_11NG));
	/* Legacy and unknown modes keep what net80211 reports. */
	CHECK(iwx_ht_media(IWX_HT_TLC_MODE_NON_HT, 1) == 0);
	CHECK(iwx_ht_media(IWX_HT_TLC_MODE_NON_HT, 0) == 0);
	CHECK(iwx_ht_media(7, 1) == 0);
	/* The word never touches bits outside subtype and mode. */
	CHECK((iwx_ht_media(IWX_HT_TLC_MODE_VHT, 1) &
	    ~(IWX_HT_IFM_TMASK | IWX_HT_IFM_MMASK)) == 0);
}

/* Our AP on channel 56 of a 160 MHz block: HT 40 below, VHT primary 80. */
static void
csa_peer56(struct iwx_ht_peer *p)
{
	memset(p, 0, sizeof(*p));
	p->ht = p->htop = p->vht = p->vhtop = 1;
	p->htcaps = IWX_HT_CAP_CW40;
	p->primary = 56;
	p->htop0 = IWX_HT_SCO_BELOW | IWX_HT_STA_CHAN_WIDTH;
	p->vht_width = 1;
	p->vht_ccfs0 = 58;
	p->vht_ccfs1 = 50;
}

static void
test_csa(void)
{
	/* Beacon tail: CSA to 36 in 5 beacons, wrapper with the new width. */
	static const uint8_t wide[] = { 0, 3, 'a', 'b', 'c',
	    37, 3, 1, 36, 5, 196, 5, 194, 3, 1, 42, 50, 221, 2, 0, 0 };
	static const uint8_t plain[] = { 0, 3, 'a', 'b', 'c', 37, 3, 0, 36, 1 };
	static const uint8_t none[] = { 0, 3, 'a', 'b', 'c', 3, 1, 56 };
	static const uint8_t ecsa[] = { 60, 4, 0, 128, 100, 3 };
	static const uint8_t both[] = { 37, 3, 1, 36, 5, 60, 4, 1, 115, 36, 5 };
	static const uint8_t differ[] = { 37, 3, 1, 36, 5, 60, 4, 1, 115, 40,
	    5 };
	static const uint8_t with_sco[] = { 37, 3, 1, 44, 2, 62, 1, 1 };
	static const uint8_t bad_sco[] = { 37, 3, 1, 44, 2, 62, 1, 2 };
	static const uint8_t shortcsa[] = { 37, 2, 1, 36 };
	static const uint8_t trunc[] = { 37, 3, 1, 36 };
	static const uint8_t badwrap[] = { 37, 3, 1, 36, 5, 196, 4, 194, 3, 1,
	    42 };
	static const uint8_t old160[] = { 37, 3, 1, 36, 5, 196, 5, 194, 3, 2,
	    50, 0 };
	/* Action frames: category, action, then the announcement. */
	static const uint8_t act_csa[] = { 0, 4, 37, 3, 1, 36, 5, 62, 1, 1 };
	static const uint8_t act_ecsa[] = { 4, 4, 1, 128, 100, 7, 194, 3, 1,
	    106, 0 };
	static const uint8_t act_prot[] = { 9, 4, 0, 128, 149, 1 };
	static const uint8_t act_other[] = { 3, 0, 1, 2, 3, 4 };
	static const uint8_t act_short[] = { 4, 4, 1, 128, 100 };
	struct iwx_ht_csa c;
	struct iwx_ht_peer old, p;
	struct iwx_ht_local loc = loc5;
	struct iwx_ht_chan cur, nc;

	CHECK(iwx_ht_csa_parse(wide, sizeof(wide), &c) == 1);
	CHECK(c.mode == 1 && c.chan == 36 && c.count == 5);
	CHECK(!c.have_sco && c.have_wbcs && c.width == 1 && c.ccfs0 == 42 &&
	    c.ccfs1 == 50);
	CHECK(iwx_ht_csa_parse(plain, sizeof(plain), &c) == 1);
	CHECK(c.mode == 0 && c.chan == 36 && c.count == 1 && !c.have_wbcs &&
	    !c.have_sco);
	CHECK(iwx_ht_csa_parse(none, sizeof(none), &c) == 0);
	CHECK(iwx_ht_csa_parse(none, 0, &c) == 0);
	CHECK(iwx_ht_csa_parse(ecsa, sizeof(ecsa), &c) == 1);
	CHECK(c.mode == 0 && c.chan == 100 && c.count == 3);
	CHECK(iwx_ht_csa_parse(both, sizeof(both), &c) == 1 && c.chan == 36);
	/* Two announcements naming different channels are not followed. */
	CHECK(iwx_ht_csa_parse(differ, sizeof(differ), &c) == -1);
	CHECK(iwx_ht_csa_parse(with_sco, sizeof(with_sco), &c) == 1);
	CHECK(c.chan == 44 && c.have_sco && c.sco == IWX_HT_SCO_ABOVE);
	CHECK(iwx_ht_csa_parse(bad_sco, sizeof(bad_sco), &c) == -1);
	/* A short element is no announcement; a cut one is malformed. */
	CHECK(iwx_ht_csa_parse(shortcsa, sizeof(shortcsa), &c) == 0);
	CHECK(iwx_ht_csa_parse(trunc, sizeof(trunc), &c) == -1);
	CHECK(iwx_ht_csa_parse(badwrap, sizeof(badwrap), &c) == -1);
	CHECK(iwx_ht_csa_parse(old160, sizeof(old160), &c) == 1 &&
	    c.width == 2 && c.ccfs0 == 50);

	{
		/* Sub-elements before the width; a short one is skipped. */
		static const uint8_t wrap2[] = { 37, 3, 1, 36, 5, 196, 9,
		    195, 2, 0, 0, 194, 3, 1, 42, 0 };
		static const uint8_t wrap_short[] = { 37, 3, 1, 36, 5, 196, 4,
		    194, 2, 1, 42 };
		static const uint8_t ecsa_short[] = { 60, 3, 0, 128, 100 };
		static const uint8_t sco_empty[] = { 37, 3, 1, 44, 2, 62, 0 };
		static const uint8_t ecsa6[] = { 60, 4, 0, 131, 5, 3 };
		static const uint8_t act_differ[] = { 4, 4, 1, 128, 100, 7,
		    37, 3, 1, 104, 7 };

		CHECK(iwx_ht_csa_parse(wrap2, sizeof(wrap2), &c) == 1 &&
		    c.have_wbcs && c.ccfs0 == 42);
		CHECK(iwx_ht_csa_parse(wrap_short, sizeof(wrap_short),
		    &c) == 1 && !c.have_wbcs);
		CHECK(iwx_ht_csa_parse(ecsa_short, sizeof(ecsa_short),
		    &c) == 0);
		CHECK(iwx_ht_csa_parse(sco_empty, sizeof(sco_empty),
		    &c) == 1 && !c.have_sco);
		/* The operating class tells a 6 GHz channel 5 from 2.4 GHz. */
		CHECK(iwx_ht_csa_parse(ecsa6, sizeof(ecsa6), &c) == 1 &&
		    c.chan == 5 && c.opclass == 131 && iwx_ht_csa_6ghz(&c));
		CHECK(iwx_ht_csa_parse(ecsa, sizeof(ecsa), &c) == 1 &&
		    c.opclass == 128 && !iwx_ht_csa_6ghz(&c));
		CHECK(iwx_ht_csa_parse(plain, sizeof(plain), &c) == 1 &&
		    c.opclass == 0 && !iwx_ht_csa_6ghz(&c));
		CHECK(iwx_ht_csa_action(act_differ, sizeof(act_differ),
		    &c) == -1);
	}
	CHECK(iwx_ht_csa_action(act_csa, sizeof(act_csa), &c) == 1);
	CHECK(c.chan == 36 && c.count == 5 && c.have_sco &&
	    c.sco == IWX_HT_SCO_ABOVE);
	CHECK(iwx_ht_csa_action(act_ecsa, sizeof(act_ecsa), &c) == 1);
	CHECK(c.mode == 1 && c.chan == 100 && c.count == 7 && c.have_wbcs &&
	    c.width == 1 && c.ccfs0 == 106);
	CHECK(iwx_ht_csa_action(act_prot, sizeof(act_prot), &c) == 1);
	CHECK(c.chan == 149 && c.count == 1 && !c.have_wbcs);
	CHECK(iwx_ht_csa_action(act_other, sizeof(act_other), &c) == 0);
	CHECK(iwx_ht_csa_action(act_short, sizeof(act_short), &c) == -1);
	CHECK(iwx_ht_csa_action(act_csa, 1, &c) == 0);
	/* An announcement frame without the announcement is malformed. */
	CHECK(iwx_ht_csa_action(act_csa, 2, &c) == -1);

	/* 40 MHz pairs of the 5 GHz band. */
	CHECK(iwx_ht_sco5(36) == IWX_HT_SCO_ABOVE);
	CHECK(iwx_ht_sco5(40) == IWX_HT_SCO_BELOW);
	CHECK(iwx_ht_sco5(64) == IWX_HT_SCO_BELOW);
	CHECK(iwx_ht_sco5(100) == IWX_HT_SCO_ABOVE);
	CHECK(iwx_ht_sco5(144) == IWX_HT_SCO_BELOW);
	CHECK(iwx_ht_sco5(149) == IWX_HT_SCO_ABOVE);
	CHECK(iwx_ht_sco5(161) == IWX_HT_SCO_BELOW);
	CHECK(iwx_ht_sco5(165) == IWX_HT_SCO_NONE);
	CHECK(iwx_ht_sco5(38) == IWX_HT_SCO_NONE);
	CHECK(iwx_ht_sco5(68) == IWX_HT_SCO_NONE);
	CHECK(iwx_ht_sco5(11) == IWX_HT_SCO_NONE);

	/* 80 MHz on 56 follows to 36 inside the same 160 MHz block. */
	csa_peer56(&old);
	iwx_ht_chan(&old, &loc, 56, &cur);
	CHECK(cur.width == IWX_HT_WIDTH_80 &&
	    cur.ctrl_pos == IWX_HT_POS_1_BELOW);
	CHECK(iwx_ht_csa_parse(wide, sizeof(wide), &c) == 1);
	iwx_ht_csa_peer(&old, &c, &p);
	CHECK(p.primary == 36 && p.vht_width == 1 && p.vht_ccfs0 == 42 &&
	    p.vht_ccfs1 == 50);
	CHECK((p.htop0 & 0x03) == IWX_HT_SCO_ABOVE &&
	    (p.htop0 & IWX_HT_STA_CHAN_WIDTH));
	CHECK(p.htcaps == old.htcaps && p.ht && p.vht);
	CHECK(iwx_ht_csa_follow(&p, &loc, 1, &cur, 36, &nc) == 1);
	CHECK(nc.width == IWX_HT_WIDTH_80 && nc.vht80 &&
	    nc.sco == IWX_HT_SCO_ABOVE && nc.ctrl_pos == IWX_HT_POS_2_BELOW);
	/* The deprecated 160 MHz encoding names the same primary 80. */
	CHECK(iwx_ht_csa_parse(old160, sizeof(old160), &c) == 1);
	iwx_ht_csa_peer(&old, &c, &p);
	CHECK(iwx_ht_csa_follow(&p, &loc, 1, &cur, 36, &nc) == 1 &&
	    nc.ctrl_pos == IWX_HT_POS_2_BELOW);
	/* Without the new width the BSS narrows: not followed at 80 MHz. */
	CHECK(iwx_ht_csa_parse(plain, sizeof(plain), &c) == 1);
	iwx_ht_csa_peer(&old, &c, &p);
	CHECK((p.htop0 & (0x03 | IWX_HT_STA_CHAN_WIDTH)) == 0 &&
	    p.vht_width == 0);
	CHECK(iwx_ht_csa_follow(&p, &loc, 1, &cur, 36, &nc) == 0);
	CHECK(nc.width == IWX_HT_WIDTH_20);
	/* A 40 MHz announcement: followed by a 40 MHz link only. */
	CHECK(iwx_ht_csa_parse(with_sco, sizeof(with_sco), &c) == 1);
	iwx_ht_csa_peer(&old, &c, &p);
	CHECK(iwx_ht_csa_follow(&p, &loc, 1, &cur, 44, &nc) == 0);
	cur.width = IWX_HT_WIDTH_40;
	CHECK(iwx_ht_csa_follow(&p, &loc, 1, &cur, 44, &nc) == 1 &&
	    nc.sco == IWX_HT_SCO_ABOVE && !nc.vht80);
	/* An explicit "no secondary channel" wins over a width element. */
	c.have_sco = 1;
	c.sco = IWX_HT_SCO_NONE;
	c.have_wbcs = 1;
	c.width = 1;
	c.ccfs0 = 42;
	iwx_ht_csa_peer(&old, &c, &p);
	cur.width = IWX_HT_WIDTH_80;
	CHECK(iwx_ht_csa_follow(&p, &loc, 1, &cur, 44, &nc) == 0 &&
	    nc.width == IWX_HT_WIDTH_20);
	/* Regulatory limits of the new channel and a band change. */
	iwx_ht_chan(&old, &loc, 56, &cur);
	CHECK(iwx_ht_csa_parse(wide, sizeof(wide), &c) == 1);
	iwx_ht_csa_peer(&old, &c, &p);
	loc.allow80 = 0;
	CHECK(iwx_ht_csa_follow(&p, &loc, 1, &cur, 36, &nc) == 0);
	loc = loc5;
	CHECK(iwx_ht_csa_follow(&p, &loc, 0, &cur, 36, &nc) == 0);
	CHECK(iwx_ht_csa_follow(&p, &loc2, 1, &cur, 36, &nc) == 0);
	/* A legacy 20 MHz link follows inside its band. */
	memset(&old, 0, sizeof(old));
	memset(&cur, 0, sizeof(cur));
	CHECK(iwx_ht_csa_parse(plain, sizeof(plain), &c) == 1);
	iwx_ht_csa_peer(&old, &c, &p);
	CHECK(iwx_ht_csa_follow(&p, &loc, 1, &cur, 36, &nc) == 1 &&
	    nc.width == IWX_HT_WIDTH_20);
	CHECK(iwx_ht_csa_follow(&p, &loc2, 0, &cur, 6, &nc) == 1);
	/* HT on 2.4 GHz is 20 MHz here and follows as such. */
	csa_peer56(&old);
	old.primary = 1;
	iwx_ht_chan(&old, &loc2, 1, &cur);
	CHECK(cur.width == IWX_HT_WIDTH_20);
	iwx_ht_csa_peer(&old, &c, &p);
	CHECK(iwx_ht_csa_follow(&p, &loc2, 0, &cur, 6, &nc) == 1 &&
	    nc.width == IWX_HT_WIDTH_20);
}

/* Beacon loss is acted on once per IWX_HT_BMISS_STEP missed beacons. */
static void
test_bmiss(void)
{
	uint32_t level = 0, n;
	int acts = 0;

	for (n = 1; n < IWX_HT_BMISS_STEP; n++)
		CHECK(iwx_ht_bmiss(n, &level) == 0);
	CHECK(iwx_ht_bmiss(IWX_HT_BMISS_STEP, &level) == 1);
	/* The firmware repeats and may skip counts. */
	CHECK(iwx_ht_bmiss(IWX_HT_BMISS_STEP, &level) == 0);
	CHECK(iwx_ht_bmiss(IWX_HT_BMISS_STEP + 3, &level) == 0);
	CHECK(iwx_ht_bmiss(2 * IWX_HT_BMISS_STEP + 1, &level) == 1);
	CHECK(iwx_ht_bmiss(2 * IWX_HT_BMISS_STEP + 2, &level) == 0);
	/* A received beacon restarts the count. */
	CHECK(iwx_ht_bmiss(1, &level) == 0 && level == 0);
	CHECK(iwx_ht_bmiss(IWX_HT_BMISS_STEP, &level) == 1);
	level = 0;
	for (n = 1; n <= 5 * IWX_HT_BMISS_STEP; n++)
		acts += iwx_ht_bmiss(n, &level);
	CHECK(acts == 5);
}

/* As chan_for(), for an AP that has the 160 MHz capability bit. */
static void
chan160(int primary, uint8_t htop0, int vht, uint8_t vw, uint8_t c0,
    uint8_t c1, const struct iwx_ht_local *loc, struct iwx_ht_peer *p,
    struct iwx_ht_chan *c)
{
	chan_for(primary, htop0, vht, vw, c0, c1, loc, p, c);
	p->vhtcaps |= IWX_VHT_CAP_CW160;
	iwx_ht_chan(p, loc, primary, c);
}

/* 160 MHz: offered only on request and where the channel allows it. */
static void
test_width160(void)
{
	static const int pos[8] = { IWX_HT_POS_4_BELOW, IWX_HT_POS_3_BELOW,
	    IWX_HT_POS_2_BELOW, IWX_HT_POS_1_BELOW, IWX_HT_POS_1_ABOVE,
	    IWX_HT_POS_2_ABOVE, IWX_HT_POS_3_ABOVE, IWX_HT_POS_4_ABOVE };
	struct iwx_ht_local l160 = loc5;
	struct iwx_ht_peer p;
	struct iwx_ht_chan c;
	struct iwx_ht_tlc t;
	uint8_t frm[16];
	int i;

	l160.vht160 = 1;
	l160.allow160 = 1;

	/* The capability element says so only when 160 MHz is offered. */
	CHECK(iwx_ht_put_vhtcap(frm, &loc5) == 14);
	CHECK((iwx_ht_le32(frm + 2) & (IWX_VHT_CAP_CW160 |
	    IWX_VHT_CAP_SGI160)) == 0);
	CHECK(iwx_ht_put_vhtcap(frm, &l160) == 14);
	CHECK((iwx_ht_le32(frm + 2) & (IWX_VHT_CAP_CW160 |
	    IWX_VHT_CAP_SGI160)) == (IWX_VHT_CAP_CW160 | IWX_VHT_CAP_SGI160));
	CHECK(iwx_ht_le32(frm + 2) & IWX_VHT_CAP_SGI80);

	/* Every primary channel of the block 36-64, centre 50. */
	for (i = 0; i < 8; i++) {
		int chan = 36 + 4 * i;
		int sco = i % 2 == 0 ? 0x05 : 0x07;
		int c80 = chan <= 48 ? 42 : 58;

		chan160(chan, sco, 1, 1, c80, 50, &l160, &p, &c);
		CHECK(c.width == IWX_HT_WIDTH_160 && c.vht80);
		CHECK(c.ctrl_pos == pos[i]);
		/* The deprecated encoding gives the same layout. */
		chan160(chan, sco, 1, 2, 50, 0, &l160, &p, &c);
		CHECK(c.width == IWX_HT_WIDTH_160 && c.ctrl_pos == pos[i]);
		/* Not offered, or not allowed here: the primary 80 MHz. */
		chan160(chan, sco, 1, 1, c80, 50, &loc5, &p, &c);
		CHECK(c.width == IWX_HT_WIDTH_80 && c.vht80);
		CHECK(c.ctrl_pos == pos[2 + i % 4]);
	}
	l160.allow160 = 0;
	chan160(56, 0x07, 1, 1, 58, 50, &l160, &p, &c);
	CHECK(c.width == IWX_HT_WIDTH_80 && c.ctrl_pos == IWX_HT_POS_1_BELOW);
	l160.allow160 = 1;
	/* A second segment that is not the 160 MHz centre is 80+80. */
	chan160(56, 0x07, 1, 1, 58, 106, &l160, &p, &c);
	CHECK(c.width == IWX_HT_WIDTH_80);
	chan160(56, 0x07, 1, 1, 58, 0, &l160, &p, &c);
	CHECK(c.width == IWX_HT_WIDTH_80);
	/* A centre that is no 160 MHz channel keeps the 80 MHz layout. */
	chan160(56, 0x07, 1, 1, 58, 66, &l160, &p, &c);
	CHECK(c.width == IWX_HT_WIDTH_80 && c.ctrl_pos == IWX_HT_POS_1_BELOW);
	chan160(56, 0x07, 1, 2, 66, 0, &l160, &p, &c);
	CHECK(c.width != IWX_HT_WIDTH_160);
	chan160(149, 0x05, 1, 1, 155, 163, &l160, &p, &c);
	CHECK(c.width == IWX_HT_WIDTH_160 &&
	    c.ctrl_pos == IWX_HT_POS_4_BELOW);
	chan160(100, 0x05, 1, 1, 106, 114, &l160, &p, &c);
	CHECK(c.width == IWX_HT_WIDTH_160 &&
	    c.ctrl_pos == IWX_HT_POS_4_BELOW);

	/* Rate scaling: 160 MHz has its own rate set and short GI bit. */
	chan160(56, 0x07, 1, 1, 58, 50, &l160, &p, &c);
	p.vhtcaps |= IWX_VHT_CAP_SGI160;
	iwx_ht_tlc(&p, &l160, &c, &t);
	CHECK(t.mode == IWX_HT_TLC_MODE_VHT && t.width == IWX_HT_WIDTH_160);
	CHECK(t.rates[0] == 0x03ff && t.rates[1] == 0x03ff);
	CHECK(t.rates160[0] == 0x03ff && t.rates160[1] == 0x03ff);
	CHECK(t.sgi & (1 << IWX_HT_WIDTH_160));
	p.vhtcaps &= ~IWX_VHT_CAP_SGI160;
	iwx_ht_tlc(&p, &l160, &c, &t);
	CHECK((t.sgi & (1 << IWX_HT_WIDTH_160)) == 0);
	/* At 80 MHz the 160 MHz rate set stays empty. */
	chan160(56, 0x07, 1, 1, 58, 50, &loc5, &p, &c);
	iwx_ht_tlc(&p, &loc5, &c, &t);
	CHECK(t.width == IWX_HT_WIDTH_80 && t.rates160[0] == 0 &&
	    t.rates160[1] == 0 && (t.sgi & (1 << IWX_HT_WIDTH_160)) == 0);

	/* Offered but not allowed on this channel: not in the element. */
	l160.allow160 = 0;
	CHECK(iwx_ht_put_vhtcap(frm, &l160) == 14);
	CHECK((iwx_ht_le32(frm + 2) & (IWX_VHT_CAP_CW160 |
	    IWX_VHT_CAP_SGI160)) == 0);
	l160.allow160 = 1;
	/* 80+80 in the deprecated encoding stays on the primary 80 MHz. */
	chan160(56, 0x07, 1, 3, 58, 106, &l160, &p, &c);
	CHECK(c.width == IWX_HT_WIDTH_80);

	/*
	 * An AP without the 160 MHz capability bit names the 160 MHz
	 * centre in HT operation (CCFS2) when it supports Extended NSS BW;
	 * without either its operation element is not believed.
	 */
	chan160(56, 0x07, 1, 1, 58, 50, &l160, &p, &c);
	CHECK(IWX_VHT_CAP_CWSET(p.vhtcaps) == 1);
	p.vhtcaps &= ~IWX_VHT_CAP_CW160;
	iwx_ht_chan(&p, &l160, 56, &c);
	CHECK(c.width == IWX_HT_WIDTH_80);
	p.vht_ccfs1 = 0;
	p.vhtcaps |= 1U << 30;			/* Extended NSS BW = 1 */
	iwx_ht_chan(&p, &l160, 56, &c);
	CHECK(c.width == IWX_HT_WIDTH_80);	/* no CCFS2 either */
	p.htop1 |= 50 << 5;
	iwx_ht_chan(&p, &l160, 56, &c);
	CHECK(c.width == IWX_HT_WIDTH_160 &&
	    c.ctrl_pos == IWX_HT_POS_2_ABOVE);
	/* Such an AP receives half of its streams at 160 MHz. */
	CHECK(iwx_ht_vht_nss160(&p) == 1);
	iwx_ht_tlc(&p, &l160, &c, &t);
	CHECK(t.rates[1] == 0x03ff && t.rates160[0] == 0x03ff &&
	    t.rates160[1] == 0);
	p.vht_rxmcs = 0xffaa;			/* four streams, MCS 0-9 */
	CHECK(iwx_ht_vht_nss160(&p) == 2);
	iwx_ht_tlc(&p, &l160, &c, &t);
	CHECK(t.rates160[0] == 0x03ff && t.rates160[1] == 0x03ff);
	p.vhtcaps |= 3U << 30;			/* three quarters */
	CHECK(iwx_ht_vht_nss160(&p) == 3);
	p.vht_rxmcs = 0xfffa;
	CHECK(iwx_ht_vht_nss160(&p) == 1);
	p.vhtcaps &= ~(3U << 30);
	CHECK(iwx_ht_vht_nss160(&p) == 0);
	/* With the capability bit CCFS1 wins; CCFS2 only fills a gap. */
	p.vhtcaps |= IWX_VHT_CAP_CW160;
	CHECK(iwx_ht_vht_nss160(&p) == 2);
	p.vht_ccfs1 = 0;
	iwx_ht_chan(&p, &l160, 56, &c);
	CHECK(c.width == IWX_HT_WIDTH_80);	/* Extended NSS BW is 0 */
	p.vhtcaps |= 1U << 30;
	iwx_ht_chan(&p, &l160, 56, &c);
	CHECK(c.width == IWX_HT_WIDTH_160);
	/* One local stream: one rate set at 160 MHz. */
	{
		struct iwx_ht_local l1 = l160;

		l1.nss = 1;
		chan160(56, 0x07, 1, 1, 58, 50, &l1, &p, &c);
		iwx_ht_tlc(&p, &l1, &c, &t);
		CHECK(t.width == IWX_HT_WIDTH_160 &&
		    t.rates160[0] == 0x03ff && t.rates160[1] == 0);
	}

	/* A switch inside the 160 MHz block keeps a 160 MHz link. */
	{
		static const uint8_t wide[] = { 37, 3, 1, 36, 5, 196, 5, 194,
		    3, 1, 42, 50 };
		struct iwx_ht_csa csa;
		struct iwx_ht_peer np;
		struct iwx_ht_chan cur, nc;

		chan160(56, 0x07, 1, 1, 58, 50, &l160, &p, &cur);
		CHECK(iwx_ht_csa_parse(wide, sizeof(wide), &csa) == 1);
		iwx_ht_csa_peer(&p, &csa, &np);
		CHECK(iwx_ht_csa_follow(&np, &l160, 1, &cur, 36, &nc) == 1);
		CHECK(nc.width == IWX_HT_WIDTH_160 &&
		    nc.ctrl_pos == IWX_HT_POS_4_BELOW);
	}
}

/* HE elements of an AP: capabilities with 160 MHz and PPE, operation. */
static const uint8_t ap_hecap[] = { 255, 30, 35,
    0x05, 0x00, 0x88, 0x02, 0x00, 0x10,
    0x0c, 0x20, 0x08, 0x18, 0x00, 0x00, 0x80, 0x80, 0x00, 0x00, 0x00,
    0xfa, 0xff, 0xfa, 0xff, 0xf6, 0xff, 0xfa, 0xff,
    0x61, 0x1c, 0xc7, 0x71 };
static const uint8_t ap_heop[] = { 255, 7, 36, 0xf4, 0x3f, 0x00, 0x05,
    0xfc, 0xff };

static size_t
he_resp(uint8_t *b, int with_cap, int with_op)
{
	size_t n = assoc_resp(b, 56, 0x07, 1, 1, 58, 50);

	if (with_cap) {
		memcpy(b + n, ap_hecap, sizeof(ap_hecap));
		n += sizeof(ap_hecap);
	}
	if (with_op) {
		memcpy(b + n, ap_heop, sizeof(ap_heop));
		n += sizeof(ap_heop);
	}
	return n;
}

/* 802.11ax: offered on request, used when the AP has both elements. */
static void
test_he(void)
{
	static const uint8_t own80[] = { 255, 26, 35,
	    0x00, 0x78, 0x20, 0x80, 0x80, 0xab,
	    0x04, 0x3f, 0x4e, 0x09, 0xfd, 0x01, 0x8c, 0x16, 0x0e, 0xfc, 0x01,
	    0xfa, 0xff, 0xfa, 0xff, 0x61, 0x1c, 0xc7, 0x71 };
	static const uint8_t own160[] = { 255, 30, 35,
	    0x00, 0x78, 0x20, 0x80, 0x80, 0xab,
	    0x0c, 0x3f, 0x4e, 0x09, 0xfd, 0x09, 0x8c, 0x16, 0x0e, 0xfc, 0x01,
	    0xfa, 0xff, 0xfa, 0xff, 0xfa, 0xff, 0xfa, 0xff,
	    0x61, 0x1c, 0xc7, 0x71 };
	static const uint8_t own1ss[] = { 255, 25, 35,
	    0x00, 0x78, 0x20, 0x80, 0x80, 0xab,
	    0x04, 0x3f, 0x4a, 0x09, 0xfd, 0x00, 0x8c, 0x0e, 0x0e, 0xfc, 0x01,
	    0xfe, 0xff, 0xfe, 0xff, 0x60, 0x1c, 0x07 };
	struct iwx_ht_local lhe = loc5, l;
	struct iwx_ht_peer p;
	struct iwx_ht_chan c;
	struct iwx_ht_tlc t;
	uint8_t b[512], out[512], in[256], ext[2][5][2];
	size_t n, len;
	int ht, vht, he, wme, i, j;

	lhe.he = 1;

	/* Our element: no +HTC, no A-MSDU in A-MPDU, VHT-sized A-MPDUs. */
	CHECK(iwx_ht_put_hecap(out, &loc5) == 0);
	CHECK(iwx_ht_put_hecap(out, &lhe) == sizeof(own80));
	CHECK(memcmp(out, own80, sizeof(own80)) == 0);
	l = lhe;
	l.vht160 = l.allow160 = 1;
	CHECK(iwx_ht_put_hecap(out, &l) == sizeof(own160));
	CHECK(memcmp(out, own160, sizeof(own160)) == 0);
	l = lhe;
	l.nss = 1;
	CHECK(iwx_ht_put_hecap(out, &l) == sizeof(own1ss));
	CHECK(memcmp(out, own1ss, sizeof(own1ss)) == 0);
	/* HE needs VHT and the 5 GHz band in this driver. */
	l = lhe;
	l.vht = 0;
	CHECK(iwx_ht_put_hecap(out, &l) == 0);
	l = lhe;
	l.band5 = 0;
	CHECK(iwx_ht_put_hecap(out, &l) == 0);

	/* The association request gets it after VHT, before WMM. */
	len = assoc_req(in, 0, 0);
	n = iwx_ht_rewrite_assoc(in, len, 4, &lhe, out, sizeof(out));
	CHECK(n == len + 28 + 14 + sizeof(own80));
	ht = find_ie(out, n, 4, IWX_HT_EID_HTCAP);
	vht = find_ie(out, n, 4, IWX_HT_EID_VHTCAP);
	he = find_ie(out, n, 4, IWX_HT_EID_EXT);
	wme = find_ie(out, n, 4, 221);
	CHECK(ht > 0 && ht < vht && vht < he && he < wme);
	CHECK(memcmp(out + he, own80, sizeof(own80)) == 0);
	CHECK(iwx_ht_ies_valid(out + 4, n - 4));
	CHECK(iwx_ht_rewrite_assoc(in, len, 4, &lhe, out,
	    len + 28 + 14 + sizeof(own80) - 1) == 0);
	/* An HE element already in the request is replaced, not doubled. */
	memcpy(in + len, own160, sizeof(own160));
	n = iwx_ht_rewrite_assoc(in, len + sizeof(own160), 4, &lhe, out,
	    sizeof(out));
	CHECK(n == len + 28 + 14 + sizeof(own80));

	/* The AP's answer. */
	n = he_resp(b, 1, 1);
	CHECK(iwx_ht_parse(b, n, 6, &p) == 0);
	CHECK(p.ht && p.vht && p.he && p.heop);
	CHECK(p.he_mac[0] == 0x05 && p.he_mac[2] == 0x88 &&
	    p.he_phy[0] == 0x0c && p.he_phy[6] == 0x80);
	CHECK(p.he_rxmcs80 == 0xfffa && p.he_txmcs80 == 0xfffa &&
	    p.he_rxmcs160 == 0xfff6 && p.he_txmcs160 == 0xfffa);
	CHECK(p.he_ppe_len == 4 && p.he_ppe[0] == 0x61 &&
	    p.he_ppe[3] == 0x71);
	CHECK(p.heop_params == 0x05003ff4 && p.he_basic == 0xfffc);
	CHECK(iwx_ht_he_color(&p) == 5 && !iwx_ht_he_color_disabled(&p));
	p.heop_params |= 0x80000000;
	CHECK(iwx_ht_he_color_disabled(&p));
	/* One element alone, or a cut one, is no HE; the rest stays. */
	n = he_resp(b, 1, 0);
	CHECK(iwx_ht_parse(b, n, 6, &p) == 0 && p.he && !p.heop && p.vht);
	n = he_resp(b, 1, 1);
	b[n - sizeof(ap_heop) - sizeof(ap_hecap) + 1] = 20;	/* too short */
	b[n - sizeof(ap_heop) - sizeof(ap_hecap) + 22] = 221;
	b[n - sizeof(ap_heop) - sizeof(ap_hecap) + 23] = 8;
	CHECK(iwx_ht_parse(b, n, 6, &p) == 0 && !p.he && p.vht);

	/* Rate scaling: HE mode, MCS 0-11, no HT/VHT short GI. */
	n = he_resp(b, 1, 1);
	CHECK(iwx_ht_parse(b, n, 6, &p) == 0);
	iwx_ht_chan(&p, &lhe, 56, &c);
	CHECK(c.width == IWX_HT_WIDTH_80);
	iwx_ht_tlc(&p, &lhe, &c, &t);
	CHECK(t.mode == IWX_HT_TLC_MODE_HE && t.width == IWX_HT_WIDTH_80);
	CHECK(t.rates[0] == 0x0fff && t.rates[1] == 0x0fff);
	CHECK(t.rates160[0] == 0 && t.rates160[1] == 0 && t.sgi == 0);
	CHECK(t.flags == (IWX_HT_TLC_LDPC | IWX_HT_TLC_STBC |
	    IWX_HT_TLC_HE_DCM_NSS_1));
	CHECK(t.max_mpdu_len == 3895);
	/* Without the request, or without HE operation, it stays VHT. */
	iwx_ht_tlc(&p, &loc5, &c, &t);
	CHECK(t.mode == IWX_HT_TLC_MODE_VHT && t.rates[0] == 0x03ff);
	p.heop = 0;
	iwx_ht_tlc(&p, &lhe, &c, &t);
	CHECK(t.mode == IWX_HT_TLC_MODE_VHT);
	p.heop = 1;
	/* The AP's own limits per stream. */
	p.he_rxmcs80 = 0xfff4;		/* SS1 MCS 0-7, SS2 MCS 0-9 */
	iwx_ht_tlc(&p, &lhe, &c, &t);
	CHECK(t.rates[0] == 0x00ff && t.rates[1] == 0x03ff);
	p.he_rxmcs80 = 0xfffe;		/* one stream */
	iwx_ht_tlc(&p, &lhe, &c, &t);
	CHECK(t.mode == IWX_HT_TLC_MODE_HE && t.rates[0] == 0x0fff &&
	    t.rates[1] == 0);
	p.he_rxmcs80 = 0xfffa;
	p.he_phy[1] = 0;		/* no LDPC */
	p.he_phy[2] = 0;		/* no STBC */
	p.he_phy[3] = 0;		/* no DCM */
	iwx_ht_tlc(&p, &lhe, &c, &t);
	CHECK(t.flags == 0);
	/* A BSS that requires three streams is joined as VHT. */
	p.he_basic = 0xffc0;
	iwx_ht_tlc(&p, &lhe, &c, &t);
	CHECK(t.mode == IWX_HT_TLC_MODE_VHT);
	/* An all-zero basic set is taken as no demand, as Linux does. */
	p.he_basic = 0;
	iwx_ht_tlc(&p, &lhe, &c, &t);
	CHECK(t.mode == IWX_HT_TLC_MODE_HE);
	p.he_basic = 0xfffc;
	/* An 80+80 map moves the PPE field; a cut PPE field is no HE. */
	{
		static const uint8_t cap8080[] = { 255, 34, 35,
		    0x05, 0x00, 0x88, 0x02, 0x00, 0x10,
		    0x1c, 0x20, 0x08, 0x18, 0x00, 0x00, 0x80, 0x80, 0, 0, 0,
		    0xfa, 0xff, 0xfa, 0xff, 0xf6, 0xff, 0xfa, 0xff,
		    0xfe, 0xff, 0xfe, 0xff, 0x61, 0x1c, 0xc7, 0x71 };
		struct iwx_ht_peer q;

		n = he_resp(b, 0, 1);
		memcpy(b + n, cap8080, sizeof(cap8080));
		CHECK(iwx_ht_parse(b, n + sizeof(cap8080), 6, &q) == 0);
		CHECK(q.he && q.he_rxmcs160 == 0xfff6 && q.he_ppe_len == 4 &&
		    q.he_ppe[0] == 0x61 && q.he_ppe[3] == 0x71);
		b[n + 1] = 33;			/* PPE one byte short */
		CHECK(iwx_ht_parse(b, n + sizeof(cap8080) - 1, 6, &q) == 0);
		CHECK(!q.he && q.heop && q.vht);
	}
	/* 160 MHz HE rates come from the AP's 160 MHz map. */
	n = he_resp(b, 1, 1);
	CHECK(iwx_ht_parse(b, n, 6, &p) == 0);
	p.vhtcaps |= IWX_VHT_CAP_CW160;
	l = lhe;
	l.vht160 = l.allow160 = 1;
	iwx_ht_chan(&p, &l, 56, &c);
	CHECK(c.width == IWX_HT_WIDTH_160);
	iwx_ht_tlc(&p, &l, &c, &t);
	CHECK(t.mode == IWX_HT_TLC_MODE_HE && t.width == IWX_HT_WIDTH_160);
	CHECK(t.rates[0] == 0x0fff && t.rates[1] == 0x0fff);
	CHECK(t.rates160[0] == 0x0fff && t.rates160[1] == 0x03ff);
	CHECK(t.flags & IWX_HT_TLC_STBC);	/* phy[7] has RX above 80 */
	p.he_phy[0] &= ~0x08;			/* no 160 MHz in HE */
	iwx_ht_tlc(&p, &l, &c, &t);
	CHECK(t.rates160[0] == 0 && t.rates160[1] == 0);

	/* Packet extension thresholds from the AP's PPE field. */
	n = he_resp(b, 1, 1);
	CHECK(iwx_ht_parse(b, n, 6, &p) == 0);
	iwx_ht_he_pkt_ext(&p, ext);
	for (i = 0; i < 2; i++)
		for (j = 0; j < 5; j++) {
			CHECK(ext[i][j][0] == 7);
			CHECK(ext[i][j][1] == (j == 2 || j == 3 ? 0 : 7));
		}
	/* One stream and one RU with 16-QAM and 64-QAM thresholds. */
	p.he_ppe[0] = 0x20;		/* NSS 1, RU 996 */
	p.he_ppe[1] = 0x0d;		/* PPET16 = 2 (bits 7-9), PPET8 = 3 */
	p.he_ppe[0] |= 0x00;
	p.he_ppe_len = 2;
	iwx_ht_he_pkt_ext(&p, ext);
	CHECK(ext[0][2][0] == 3 && ext[0][2][1] == 2);
	CHECK(ext[0][0][0] == 7 && ext[1][2][0] == 7 && ext[1][2][1] == 7);
	/* Without PPE the nominal padding applies to every width. */
	p.he_phy[6] &= ~0x80;
	p.he_ppe_len = 0;
	p.he_phy[9] = 2 << 6;		/* 16 us */
	iwx_ht_he_pkt_ext(&p, ext);
	CHECK(ext[0][0][0] == 7 && ext[0][0][1] == 0 && ext[1][4][1] == 0);
	p.he_phy[9] = 1 << 6;		/* 8 us */
	iwx_ht_he_pkt_ext(&p, ext);
	CHECK(ext[1][3][0] == 0 && ext[1][3][1] == 7);
	p.he_phy[9] = 0;		/* none */
	iwx_ht_he_pkt_ext(&p, ext);
	CHECK(ext[0][2][0] == 7 && ext[0][2][1] == 7);
	p.he_phy[9] = 3 << 6;		/* reserved: see PPE, which is absent */
	iwx_ht_he_pkt_ext(&p, ext);
	CHECK(ext[1][1][0] == 7 && ext[1][1][1] == 7);

	/* ifconfig keeps the 11ac word: net80211 has none for 11ax. */
	CHECK(iwx_ht_media(IWX_HT_TLC_MODE_HE, 1) ==
	    (IWX_HT_IFM_VHT | IWX_HT_IFM_11AC));
}

int
main(void)
{
	test_addba();
	test_ba_frames();
	test_reorder();
	test_rewrite();
	test_parse_and_chan();
	test_tlc();
	test_media();
	test_csa();
	test_bmiss();
	test_width160();
	test_he();
	printf("iwx-ht-contract: %u checks, %u failures\n", checks, failures);
	return failures != 0;
}
