/* SPDX-License-Identifier: BSD-2-Clause */
/* Original bounded IE views, AI-assisted. See docs/net80211-beacon-ies.md. */
#include <sys/types.h>
#include <sys/systm.h>
#include <net80211/ieee80211_beacon_ies.h>

int
ieee80211_beacon_ies_parse(struct ieee80211_beacon_ies *ies,
    const uint8_t *data, size_t len)
{
	struct ieee80211_beacon_ies pending = {0};
	const uint8_t *ie = data;
	size_t left = len;

	memset(ies, 0, sizeof(*ies));
	if (data == NULL && len != 0)
		return -1;
	while (left != 0) {
		if (left < 2 || ie[1] > left - 2)
			return -1;
		switch (ie[0]) {
		case 0: /* SSID */
			pending.ssid = ie;
			if (ie[1] <= 32)
				pending.ssid_valid = ie;
			break;
		case 1: /* Supported Rates */
			pending.rates = ie;
			if (ie[1] <= 15)
				pending.rates_valid = ie;
			break;
		case 2: /* FH Parameter Set */
			if (ie[1] < 5)
				return -1;
			pending.fh = ie;
			break;
		case 3: /* DS Parameter Set */
			if (ie[1] < 1)
				return -1;
			pending.ds = ie;
			if (ie[1] == 1)
				pending.ds_exact = ie;
			break;
		case 5: /* TIM: count, period, control and at least one bitmap byte */
			if (ie[1] < 4)
				return -1;
			pending.tim = ie;
			pending.timoff = len - left;
			break;
		case 6: /* IBSS Parameter Set */
			break;
		case 7: pending.country = ie; break;
		case 42: /* ERP */
			if (ie[1] == 1)
				pending.erp = ie;
			else
				pending.bad_erp++;
			break;
		case 48: pending.rsn = pending.security = ie; break;
		case 50: pending.xrates = ie; break;
		case 54: /* Mobility Domain */
			if (ie[1] >= 3)
				pending.mdie = ie;
			pending.unknown++;
			break;
		case 61: /* HT Operation: primary channel is observation only. */
			if (ie[1] >= 22)
				pending.ht = ie;
			pending.unknown++;
			break;
		case 70:
			if (ie[1] == 5)
				pending.rrm = ie;
			break;
		case 127:
			pending.extcap = ie;
			if (ie[1] >= 1 && ie[1] <= 32)
				pending.extcap_bounded = ie;
			break;
		case 221: /* Microsoft WPA/WME OUI */
			if (ie[1] >= 4 && ie[2] == 0 && ie[3] == 0x50 &&
			    ie[4] == 0xf2) {
				if (ie[5] == 1)
					pending.security = ie;
				else if (ie[5] == 2 && ie[1] >= 6 &&
				    (ie[6] == 0 || ie[6] == 1)) {
					if (pending.wme != NULL)
						pending.wme_duplicate = 1;
					pending.wme = ie;
				}
			}
			break;
		case 244:
			if (ie[1] >= 1 && ie[1] <= 16 &&
			    (ie[2] & 0x0f) + 1 == ie[1])
				pending.rsnx = ie;
			break;
		default:
			pending.unknown++;
			break;
		}
		left -= (size_t)ie[1] + 2;
		ie += (size_t)ie[1] + 2;
	}
	(void)ieee80211_phy_ies_parse(&pending.phy, data, len);
	*ies = pending;
	return 0;
}
