/* SPDX-License-Identifier: BSD-2-Clause */
/* Translate the selected common WPA parser result to a neutral OS/IDF ABI. */

#include "utils/includes.h"
#include "utils/common.h"
#include "common/wpa_common.h"
#include "rsn_supp/wpa.h"

#include "wpa_ie_bridge.h"

static enum espwifi_wpa_cipher
espwifi_common_cipher(int cipher)
{
	switch (cipher) {
	case WPA_CIPHER_NONE:
		return ESPWIFI_WPA_CIPHER_NONE;
	case WPA_CIPHER_WEP40:
		return ESPWIFI_WPA_CIPHER_WEP40;
	case WPA_CIPHER_WEP104:
		return ESPWIFI_WPA_CIPHER_WEP104;
	case WPA_CIPHER_TKIP:
		return ESPWIFI_WPA_CIPHER_TKIP;
	case WPA_CIPHER_CCMP:
		return ESPWIFI_WPA_CIPHER_CCMP;
	case WPA_CIPHER_CCMP | WPA_CIPHER_TKIP:
		return ESPWIFI_WPA_CIPHER_TKIP_CCMP;
	case WPA_CIPHER_AES_128_CMAC:
		return ESPWIFI_WPA_CIPHER_AES_CMAC128;
	case WPA_CIPHER_SMS4:
		return ESPWIFI_WPA_CIPHER_SMS4;
	case WPA_CIPHER_GCMP:
		return ESPWIFI_WPA_CIPHER_GCMP;
	case WPA_CIPHER_GCMP_256:
		return ESPWIFI_WPA_CIPHER_GCMP256;
	case WPA_CIPHER_BIP_GMAC_128:
		return ESPWIFI_WPA_CIPHER_AES_GMAC128;
	case WPA_CIPHER_BIP_GMAC_256:
		return ESPWIFI_WPA_CIPHER_AES_GMAC256;
	default:
		return ESPWIFI_WPA_CIPHER_UNKNOWN;
	}
}

int
espwifi_parse_wpa_ie(const uint8_t *ie, size_t len, struct espwifi_wpa_ie *out)
{
	struct wpa_ie_data parsed;
	int error;

	if (ie == NULL || out == NULL)
		return -1;
	memset(out, 0, sizeof(*out));
	error = wpa_parse_wpa_ie(ie, len, &parsed);
	if (error != 0)
		return error;
	out->proto = parsed.proto;
	out->pairwise_cipher = espwifi_common_cipher(parsed.pairwise_cipher);
	out->group_cipher = espwifi_common_cipher(parsed.group_cipher);
	out->key_mgmt = parsed.key_mgmt;
	out->capabilities = parsed.capabilities;
	out->num_pmkid = parsed.num_pmkid;
	out->pmkid = parsed.pmkid;
	out->mgmt_group_cipher = espwifi_common_cipher(parsed.mgmt_group_cipher);
	return 0;
}
