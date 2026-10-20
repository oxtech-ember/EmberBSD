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

static const struct iwx_ht_local loc5 = { 1, 2, 1, 1, 1 };
static const struct iwx_ht_local loc2 = { 0, 2, 1, 1, 1 };

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

int
main(void)
{
	test_addba();
	test_rewrite();
	test_parse_and_chan();
	test_tlc();
	printf("iwx-ht-contract: %u checks, %u failures\n", checks, failures);
	return failures != 0;
}
