/* SPDX-License-Identifier: BSD-2-Clause */
/* Original bounded scan observations, AI-assisted. */
#include <sys/types.h>
#include <sys/systm.h>
#include <net80211/ieee80211_phy_ies.h>

size_t
ieee80211_phy_ies_size(const struct ieee80211_phy_ies *ies, size_t budget)
{

	if (ies->len > sizeof(ies->data) || ies->len > budget)
		return 0;
	return ies->len;
}

/*
 * Wire lengths follow FreeBSD ieee80211.h at
 * d2145e9b96448da34d3cfede24c509af6d830c92.
 */
int
ieee80211_phy_ies_parse(struct ieee80211_phy_ies *ies, const uint8_t *data,
    size_t len)
{
	struct ieee80211_phy_ies pending = {0};
	size_t size;
	unsigned seen = 0, bit, expected;

	memset(ies, 0, sizeof(*ies));
	if (data == NULL && len != 0)
		return -1;
	while (len != 0) {
		if (len < 2 || data[1] > len - 2)
			return -1;
		size = (size_t)data[1] + 2;
		switch (data[0]) {
		case 45: /* HT Capabilities */
			bit = 1;
			expected = 26;
			break;
		case 61: /* HT Operation */
			bit = 2;
			expected = 22;
			break;
		case 191: /* VHT Capabilities */
			bit = 4;
			expected = 12;
			break;
		case 192: /* VHT Operation */
			bit = 8;
			expected = 5;
			break;
		default:
			data += size;
			len -= size;
			continue;
		}
		if ((seen & bit) != 0 || data[1] != expected ||
		    size > sizeof(pending.data) - pending.len)
			return -1;
		seen |= bit;
		memcpy(pending.data + pending.len, data, size);
		pending.len += size;
		data += size;
		len -= size;
	}
	*ies = pending;
	return 0;
}
