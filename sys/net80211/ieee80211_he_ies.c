/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD shared bounded HE (802.11ax) wire values, AI-assisted. */
/*
 * Field layout: IEEE Std 802.11ax-2021, 9.4.2.248 (HE Capabilities) and
 * 9.4.2.249 (HE Operation). The pure codec allocates no memory, retains no
 * input pointers and grants no PHY, rate or channel permission.
 */
#include <sys/types.h>
#include <sys/systm.h>
#include <net80211/ieee80211_he_ies.h>

static uint16_t
he_get16(const uint8_t *p)
{

	return p[0] | (uint16_t)p[1] << 8;
}

static void
he_put16(uint8_t *p, uint16_t value)
{

	p[0] = value;
	p[1] = value >> 8;
}

/* Octets of the PPE Thresholds field that starts with octet p0. */
static size_t
he_ppe_len(uint8_t p0)
{
	unsigned nss = (p0 & 0x07) + 1, ru = 0, bits, i;

	for (i = 3; i < 7; i++)
		ru += (p0 >> i) & 1;
	bits = 7 + nss * ru * 6;
	return (bits + 7) / 8;
}

/* Parse one HE Capabilities element body (after the extension ID). */
static int
he_cap_parse(struct ieee80211_he_cap *cap, const uint8_t *p, size_t n)
{
	size_t maps = 4, need;

	memset(cap, 0, sizeof(*cap));
	if (n < IEEE80211_HE_MAC_CAP_LEN + IEEE80211_HE_PHY_CAP_LEN + 4)
		return -1;
	memcpy(cap->mac, p, IEEE80211_HE_MAC_CAP_LEN);
	memcpy(cap->phy, p + IEEE80211_HE_MAC_CAP_LEN,
	    IEEE80211_HE_PHY_CAP_LEN);
	if (cap->phy[0] & IEEE80211_HE_PHY0_CW_160_5G)
		maps += 4;
	if (cap->phy[0] & IEEE80211_HE_PHY0_CW_80P80_5G)
		maps += 4;
	need = IEEE80211_HE_MAC_CAP_LEN + IEEE80211_HE_PHY_CAP_LEN + maps;
	if (n < need)
		return -1;
	p += IEEE80211_HE_MAC_CAP_LEN + IEEE80211_HE_PHY_CAP_LEN;
	cap->rx_map80 = he_get16(p);
	cap->tx_map80 = he_get16(p + 2);
	p += maps;
	if (cap->phy[6] & IEEE80211_HE_PHY6_PPE_PRESENT) {
		if (n == need)
			return -1;
		cap->ppe_len = he_ppe_len(p[0]);
		if (cap->ppe_len > IEEE80211_HE_PPE_MAXLEN ||
		    n != need + cap->ppe_len)
			return -1;
		memcpy(cap->ppe, p, cap->ppe_len);
	} else if (n != need)
		return -1;
	return 0;
}

/* Parse one HE Operation element body (after the extension ID). */
static int
he_op_parse(struct ieee80211_he_operation *op, const uint8_t *p, size_t n)
{
	size_t need = 6;

	memset(op, 0, sizeof(*op));
	if (n < need)
		return -1;
	op->params = p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16;
	op->bss_color = p[3];
	op->basic_map = he_get16(p + 4);
	if (op->params & IEEE80211_HE_OP_VHT_INFO)
		need += 3;
	if (op->params & IEEE80211_HE_OP_COHOSTED)
		need += 1;
	if (op->params & IEEE80211_HE_OP_6GHZ_INFO)
		need += 5;
	return n == need ? 0 : -1;
}

int
ieee80211_he_ies_parse(struct ieee80211_he_ies *ies, const uint8_t *p,
    size_t len)
{
	struct ieee80211_he_ies pending;
	size_t n;

	if (ies == NULL)
		return -1;
	memset(ies, 0, sizeof(*ies));
	memset(&pending, 0, sizeof(pending));
	if (p == NULL && len != 0)
		return -1;
	while (len != 0) {
		if (len < 2 || p[1] > len - 2)
			return -1;
		n = p[1];
		if (p[0] == IEEE80211_ELEMID_EXTENSION && n >= 1) {
			if (p[2] == IEEE80211_ELEMID_EXT_HE_CAP) {
				if (pending.cap_present ||
				    he_cap_parse(&pending.cap, p + 3, n - 1) != 0)
					return -1;
				pending.cap_present = true;
			} else if (p[2] == IEEE80211_ELEMID_EXT_HE_OP) {
				if (pending.operation_present ||
				    he_op_parse(&pending.operation, p + 3,
				    n - 1) != 0)
					return -1;
				pending.operation_present = true;
			}
		}
		p += n + 2;
		len -= n + 2;
	}
	*ies = pending;
	return 0;
}

int
ieee80211_he_cap_encode(uint8_t *buf, size_t len,
    const struct ieee80211_he_cap *cap)
{
	struct ieee80211_he_cap c;
	size_t body, ppe = 0;

	if (buf == NULL || cap == NULL)
		return -1;
	c = *cap;
	if (c.phy[0] & (IEEE80211_HE_PHY0_CW_160_5G |
	    IEEE80211_HE_PHY0_CW_80P80_5G))
		return -1;
	if (c.phy[6] & IEEE80211_HE_PHY6_PPE_PRESENT) {
		if (c.ppe_len == 0 || c.ppe_len > IEEE80211_HE_PPE_MAXLEN ||
		    he_ppe_len(c.ppe[0]) != c.ppe_len)
			return -1;
		ppe = c.ppe_len;
	} else if (c.ppe_len != 0)
		return -1;
	body = IEEE80211_HE_CAP_MINLEN + ppe;
	if (len < body + 2)
		return -1;
	buf[0] = IEEE80211_ELEMID_EXTENSION;
	buf[1] = body;
	buf[2] = IEEE80211_ELEMID_EXT_HE_CAP;
	memcpy(buf + 3, c.mac, IEEE80211_HE_MAC_CAP_LEN);
	memcpy(buf + 3 + IEEE80211_HE_MAC_CAP_LEN, c.phy,
	    IEEE80211_HE_PHY_CAP_LEN);
	he_put16(buf + 20, c.rx_map80);
	he_put16(buf + 22, c.tx_map80);
	memcpy(buf + 24, c.ppe, ppe);
	return (int)(body + 2);
}

int
ieee80211_he_local_check(const struct ieee80211_he_cap *cap)
{
	static const uint8_t phy_allowed[IEEE80211_HE_PHY_CAP_LEN] = {
		[0] = IEEE80211_HE_PHY0_CW_40_2G | IEEE80211_HE_PHY0_CW_40_80_5G,
		[1] = IEEE80211_HE_PHY1_LDPC,
		[9] = IEEE80211_HE_PHY9_PADDING_MASK,
	};
	unsigned i;

	if (cap == NULL || cap->ppe_len != 0 || cap->rx_map80 != cap->tx_map80)
		return -1;
	for (i = 0; i < IEEE80211_HE_MAC_CAP_LEN; i++)
		if (cap->mac[i] != 0)
			return -1;
	for (i = 0; i < IEEE80211_HE_PHY_CAP_LEN; i++)
		if ((cap->phy[i] & ~phy_allowed[i]) != 0)
			return -1;
	/* Padding value 3 is reserved. */
	if ((cap->phy[9] & IEEE80211_HE_PHY9_PADDING_MASK) ==
	    IEEE80211_HE_PHY9_PADDING_MASK)
		return -1;
	/* NSS1 alone or NSS1 and NSS2 with the same support, MCS 0-7/9/11. */
	switch (cap->rx_map80) {
	case 0xfffc: case 0xfffd: case 0xfffe:
	case 0xfff0: case 0xfff5: case 0xfffa:
		return 0;
	default:
		return -1;
	}
}
