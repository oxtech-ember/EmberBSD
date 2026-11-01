/* SPDX-License-Identifier: BSD-2-Clause */

#ifndef _ESP_WIFI_WPA_IE_BRIDGE_H_
#define _ESP_WIFI_WPA_IE_BRIDGE_H_

#include <stddef.h>
#include <stdint.h>

enum espwifi_wpa_cipher {
	ESPWIFI_WPA_CIPHER_UNKNOWN,
	ESPWIFI_WPA_CIPHER_NONE,
	ESPWIFI_WPA_CIPHER_WEP40,
	ESPWIFI_WPA_CIPHER_WEP104,
	ESPWIFI_WPA_CIPHER_TKIP,
	ESPWIFI_WPA_CIPHER_CCMP,
	ESPWIFI_WPA_CIPHER_TKIP_CCMP,
	ESPWIFI_WPA_CIPHER_AES_CMAC128,
	ESPWIFI_WPA_CIPHER_SMS4,
	ESPWIFI_WPA_CIPHER_GCMP,
	ESPWIFI_WPA_CIPHER_GCMP256,
	ESPWIFI_WPA_CIPHER_AES_GMAC128,
	ESPWIFI_WPA_CIPHER_AES_GMAC256
};

struct espwifi_wpa_ie {
	int proto;
	int pairwise_cipher;
	int group_cipher;
	int key_mgmt;
	int capabilities;
	size_t num_pmkid;
	const uint8_t *pmkid;
	int mgmt_group_cipher;
};

int	espwifi_parse_wpa_ie(const uint8_t *, size_t, struct espwifi_wpa_ie *);

#endif /* _ESP_WIFI_WPA_IE_BRIDGE_H_ */
