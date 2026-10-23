/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD bounded beacon views and switch-announcement contracts. */
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
		size_t payload;

		if (left < 2)
			return -1;
		payload = ie[1];
		if (payload > left - 2)
			return -1;
		switch (ie[0]) {
		case 0: /* SSID */
			pending.ssid = ie;
			if (payload <= 32)
				pending.ssid_valid = ie;
			break;
		case 1: /* Supported Rates */
			pending.rates = ie;
			if (payload <= 15)
				pending.rates_valid = ie;
			break;
		case 2: /* FH Parameter Set */
			if (payload < 5)
				return -1;
			pending.fh = ie;
			break;
		case 3: /* DS Parameter Set */
			if (payload < 1)
				return -1;
			pending.ds = ie;
			if (payload == 1)
				pending.ds_exact = ie;
			break;
		case 5: /* TIM: count, period, control and at least one bitmap byte */
			if (payload < 4)
				return -1;
			pending.tim = ie;
			pending.timoff = len - left;
			break;
		case 6: /* IBSS Parameter Set */
			break;
		case 7: pending.country = ie; break;
		case 37: /* Channel Switch Announcement: mode, channel, count. */
			if (payload != 3 || pending.csa != NULL ||
			    ie[2] > 1 || ie[3] == 0)
				return -1;
			pending.csa = ie;
			break;
		case 60: /* Extended CSA adds an operating class. */
			if (payload != 4 || pending.xcsa != NULL ||
			    ie[2] > 1 || ie[4] == 0)
				return -1;
			pending.xcsa = ie;
			break;
		case 40: /* Quiet: count, period, duration and offset. */
			if (payload != 6 || pending.quiet != NULL)
				return -1;
			pending.quiet = ie;
			break;
		case 42: /* ERP */
			if (payload == 1)
				pending.erp = ie;
			else
				pending.bad_erp++;
			break;
		case 48: pending.rsn = pending.security = ie; break;
		case 50: pending.xrates = ie; break;
		case 54: /* Mobility Domain */
			if (payload >= 3)
				pending.mdie = ie;
			pending.unknown++;
			break;
		case 61: /* HT Operation: primary channel is observation only. */
			if (payload >= 22)
				pending.ht = ie;
			pending.unknown++;
			break;
		case 70:
			if (payload == 5)
				pending.rrm = ie;
			break;
		case 127:
			pending.extcap = ie;
			if (payload >= 1 && payload <= 32)
				pending.extcap_bounded = ie;
			break;
		case 221: /* Microsoft WPA/WME OUI */
			if (left >= 6 && payload >= 4 && ie[2] == 0 &&
			    ie[3] == 0x50 && ie[4] == 0xf2) {
				if (ie[5] == 1)
					pending.security = ie;
				else if (left >= 8 && payload >= 6 && ie[5] == 2 &&
				    (ie[6] == 0 || ie[6] == 1)) {
					if (pending.wme != NULL)
						pending.wme_duplicate = 1;
					pending.wme = ie;
				}
			}
			break;
		case 244:
			if (payload >= 1 && payload <= 16 &&
			    (ie[2] & 0x0f) + 1U == payload)
				pending.rsnx = ie;
			break;
		default:
			pending.unknown++;
			break;
		}
		left -= (size_t)payload + 2;
		ie += (size_t)payload + 2;
	}
	(void)ieee80211_phy_ies_parse(&pending.phy, data, len);
	*ies = pending;
	return 0;
}

int
ieee80211_sta_action_announcement(const uint8_t *body, size_t len)
{
	if (len < 2)
		return 0;
	if (body == NULL)
		return 1;
	return (body[0] == 0 || body[0] == 4) && body[1] == 4;
}

int
ieee80211_sta_beacon_announcement(const uint8_t *body, size_t len)
{
	struct ieee80211_beacon_ies ies;

	if (body == NULL || len < 12 ||
	    ieee80211_beacon_ies_parse(&ies, body + 12, len - 12) != 0)
		return 1;
	return ies.csa != NULL || ies.xcsa != NULL || ies.quiet != NULL;
}

uint16_t
ieee80211_sta_beacon_interval(const uint8_t *body, size_t len,
    unsigned channel)
{
	struct ieee80211_beacon_ies ies;

	if (body == NULL || len < 12 || channel == 0 || channel > 255 ||
	    (body[10] & 0x03) != 0x01 ||
	    ieee80211_beacon_ies_parse(&ies, body + 12, len - 12) != 0)
		return 0;
	if (ies.ssid == NULL || ies.ssid[1] > 32 || ies.rates == NULL ||
	    ies.rates[1] == 0 || ies.rates[1] > 15 ||
	    ies.csa != NULL || ies.xcsa != NULL || ies.quiet != NULL)
		return 0;
	if (ies.ds != NULL && (ies.ds[1] != 1 || ies.ds[2] != channel))
		return 0;
	/* DS is optional on 5 GHz. A present HT primary must not contradict
	 * the channel of this observation, even when DS agrees with it. */
	if (ies.ht != NULL && ies.ht[2] != channel)
		return 0;
	return (uint16_t)(body[8] | (uint16_t)body[9] << 8);
}
