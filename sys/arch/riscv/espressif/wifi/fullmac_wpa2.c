/* SPDX-License-Identifier: BSD-2-Clause */
/* ESP-IDF ABI bridge for an externally managed WPA2 station. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
#include "esp_wifi_driver.h"
#include "wpa_ie_bridge.h"

#include "../esp_wifi_os.h"

#define WLAN_FC_STYPE_ASSOC_RESP 1
#define WLAN_FC_STYPE_REASSOC_RESP 3
#define WLAN_FC_STYPE_AUTH 11
#define WLAN_FC_STYPE_ACTION 13
#define ESPWIFI_SAE_BODY_MAX (1536 - 30)

void forced_memzero(void *, size_t);

/*
 * Adapted from Espressif ESP-IDF 6.1 components/wpa_supplicant/src/rsn_supp/wpa.c
 * cipher_type_map_supp_to_public(), Copyright (c) 2021 Espressif Systems
 * (Shanghai) Co., Ltd., Apache-2.0. Keep the vendor enum conversion at this
 * ABI boundary; WPA parsing remains in the selected common supplicant source.
 */
static wifi_cipher_type_t
espwifi_cipher_type(unsigned cipher)
{
	switch (cipher) {
	case ESPWIFI_WPA_CIPHER_NONE:
		return WIFI_CIPHER_TYPE_NONE;
	case ESPWIFI_WPA_CIPHER_WEP40:
		return WIFI_CIPHER_TYPE_WEP40;
	case ESPWIFI_WPA_CIPHER_WEP104:
		return WIFI_CIPHER_TYPE_WEP104;
	case ESPWIFI_WPA_CIPHER_TKIP:
		return WIFI_CIPHER_TYPE_TKIP;
	case ESPWIFI_WPA_CIPHER_CCMP:
		return WIFI_CIPHER_TYPE_CCMP;
	case ESPWIFI_WPA_CIPHER_TKIP_CCMP:
		return WIFI_CIPHER_TYPE_TKIP_CCMP;
	case ESPWIFI_WPA_CIPHER_AES_CMAC128:
		return WIFI_CIPHER_TYPE_AES_CMAC128;
	case ESPWIFI_WPA_CIPHER_AES_GMAC128:
		return WIFI_CIPHER_TYPE_AES_GMAC128;
	case ESPWIFI_WPA_CIPHER_AES_GMAC256:
		return WIFI_CIPHER_TYPE_AES_GMAC256;
	case ESPWIFI_WPA_CIPHER_SMS4:
		return WIFI_CIPHER_TYPE_SMS4;
	case ESPWIFI_WPA_CIPHER_GCMP:
		return WIFI_CIPHER_TYPE_GCMP;
	case ESPWIFI_WPA_CIPHER_GCMP256:
		return WIFI_CIPHER_TYPE_GCMP256;
	default:
		return WIFI_CIPHER_TYPE_UNKNOWN;
	}
}

static int
espwifi_wpa2_parse_ie(const u8 *ie, size_t len, wifi_wpa_ie_t *data)
{
	struct espwifi_wpa_ie parsed;
	int error;

	if (ie == NULL || data == NULL)
		return -1;
	memset(data, 0, sizeof(*data));
	error = espwifi_parse_wpa_ie(ie, len, &parsed);
	if (error != 0)
		return error;
	data->proto = parsed.proto;
	data->pairwise_cipher = espwifi_cipher_type(parsed.pairwise_cipher);
	data->group_cipher = espwifi_cipher_type(parsed.group_cipher);
	data->key_mgmt = parsed.key_mgmt;
	data->capabilities = parsed.capabilities;
	data->num_pmkid = parsed.num_pmkid;
	data->pmkid = parsed.pmkid;
	data->mgmt_group_cipher = espwifi_cipher_type(parsed.mgmt_group_cipher);
	/* The shared WPA parser has no RSNXE result in this version. */
	return 0;
}

static int
espwifi_wpa2_sta_connect(uint8_t *bssid)
{
	if (bssid == NULL)
		return -1;
	return esp_wifi_sta_connect_internal(bssid) == ESP_OK ? 0 : -1;
}

static bool
espwifi_wpa2_sta_init(void)
{
	return true;
}

static bool
espwifi_wpa2_sta_deinit(void)
{
	return true;
}

static void
espwifi_wpa2_sta_connected(uint8_t *bssid)
{
	/* IDF calls this after its connection completion; association is earlier. */
	(void)bssid;
}

static void
espwifi_wpa2_sta_disconnected(uint8_t reason)
{
	(void)reason;
	espwifi_os_link(0, NULL);
}

static bool
espwifi_wpa2_sta_in_4way(void)
{
	return espwifi_os_wpa_in_4way();
}

static int
espwifi_wpa2_sta_rx_eapol(u8 *source, u8 *buffer, u32 len)
{
	return espwifi_os_rx_eapol(source, buffer, len);
}

static int
espwifi_wpa2_sta_rx_mgmt(u8 subtype, u8 *body, size_t len, u8 *sender,
    int8_t rssi, u8 channel, u64 current_tsf)
{
	(void)rssi;
	(void)channel;
	(void)current_tsf;
	switch (subtype) {
	case WLAN_FC_STYPE_ASSOC_RESP:
	case WLAN_FC_STYPE_REASSOC_RESP:
		return espwifi_os_assoc_response(subtype, body,
		    (unsigned int)len, sender) ? 0 : -1;
	case WLAN_FC_STYPE_AUTH:
	case WLAN_FC_STYPE_ACTION:
		return -1;
	default:
		return -1;
	}
}

/* IDF consumes the returned body synchronously after this callback returns. */
static uint8_t espwifi_sae_body[ESPWIFI_SAE_BODY_MAX];

static uint8_t *
espwifi_wpa3_build_sae_msg(uint8_t *bssid, uint32_t type, size_t *len)
{
	unsigned int body_len = 0;
	int error;

	if (len == NULL)
		return NULL;
	*len = 0;
	forced_memzero(espwifi_sae_body, sizeof(espwifi_sae_body));
	if (bssid == NULL || (type != ESPWIFI_SAE_COMMIT &&
	    type != ESPWIFI_SAE_CONFIRM))
		return NULL;
	error = espwifi_os_sae_build(bssid, type, espwifi_sae_body,
	    sizeof(espwifi_sae_body), &body_len);
	if (error != 0 || body_len == 0 || body_len > sizeof(espwifi_sae_body)) {
		forced_memzero(espwifi_sae_body, sizeof(espwifi_sae_body));
		return NULL;
	}
	*len = body_len;
	return espwifi_sae_body;
}

static int
espwifi_wpa3_parse_sae_msg(uint8_t *buf, size_t len, uint32_t type,
	    uint16_t status)
{
	if (buf == NULL || len == 0 || len > ESPWIFI_SAE_BODY_MAX ||
	    (type != ESPWIFI_SAE_COMMIT && type != ESPWIFI_SAE_CONFIRM))
		return ESP_FAIL;
	return espwifi_os_sae_parse(buf, len, type, status) == 0 ? ESP_OK : ESP_FAIL;
}

/* IDF unregister owns/frees the registered table through the OS allocator. */
int
espwifi_wpa2_register(void)
{
	struct wpa_funcs *callbacks;
	esp_err_t error;

	callbacks = espwifi_os_malloc_internal(sizeof(*callbacks));
	if (callbacks == NULL)
		return -1;
	memset(callbacks, 0, sizeof(*callbacks));
	callbacks->wpa_sta_init = espwifi_wpa2_sta_init;
	callbacks->wpa_sta_deinit = espwifi_wpa2_sta_deinit;
	callbacks->wpa_sta_connect = espwifi_wpa2_sta_connect;
	callbacks->wpa_sta_connected_cb = espwifi_wpa2_sta_connected;
	callbacks->wpa_sta_disconnected_cb = espwifi_wpa2_sta_disconnected;
	callbacks->wpa_sta_rx_eapol = espwifi_wpa2_sta_rx_eapol;
	callbacks->wpa_sta_in_4way_handshake = espwifi_wpa2_sta_in_4way;
	callbacks->wpa_sta_rx_mgmt = espwifi_wpa2_sta_rx_mgmt;
	callbacks->wpa_parse_wpa_ie = espwifi_wpa2_parse_ie;
	callbacks->wpa3_build_sae_msg = espwifi_wpa3_build_sae_msg;
	callbacks->wpa3_parse_sae_msg = espwifi_wpa3_parse_sae_msg;
	/* Match esp_supplicant_init ordering: install filter before callback table. */
	error = esp_wifi_register_mgmt_frame_internal(
	    (1U << WLAN_FC_STYPE_ASSOC_RESP) |
	    (1U << WLAN_FC_STYPE_REASSOC_RESP), 0);
	if (error != ESP_OK) {
		espwifi_os_free(callbacks);
		return -1;
	}
	if (esp_wifi_register_wpa_cb_internal(callbacks) != ESP_OK) {
		(void)esp_wifi_register_mgmt_frame_internal(0, 0);
		espwifi_os_free(callbacks);
		return -1;
	}
	return 0;
}

/* Accept only a single, explicitly selected RSN/CCMP/PSK suite. */
static bool
espwifi_wpa2_rsn(const uint8_t *ie, unsigned len)
{
	static const uint8_t ccmp[] = { 0x00, 0x0f, 0xac, 0x04 };
	static const uint8_t psk[] = { 0x00, 0x0f, 0xac, 0x02 };

	if (len < 20 || ie[0] != 48 || (unsigned)ie[1] + 2 != len ||
	    ie[2] != 1 || ie[3] != 0 || memcmp(ie + 4, ccmp, 4) != 0 ||
	    ie[8] != 1 || ie[9] != 0 || memcmp(ie + 10, ccmp, 4) != 0 ||
	    ie[14] != 1 || ie[15] != 0 || memcmp(ie + 16, psk, 4) != 0)
		return false;
	/* PMF needs the IGTK and SA Query paths, not available yet. */
	if (len == 21 || (len >= 22 && (ie[20] & 0xc0) != 0))
		return false;
	/* PMKSA lists and management ciphers need explicit support first. */
	return len == 20 || len == 22 ||
	    (len == 24 && ie[22] == 0 && ie[23] == 0);
}

int
espwifi_ext_connect(const unsigned char *ssid, unsigned int ssid_len,
    const unsigned char *bssid, const unsigned char *rsn, unsigned int rsn_len)
{
	static const uint8_t zero_bssid[6];
	wifi_config_t config;
	uint8_t assoc_ie[24];
	esp_err_t error;

	if (ssid == NULL || bssid == NULL || rsn == NULL || ssid_len == 0 ||
	    ssid_len > sizeof(config.sta.ssid) || rsn_len > sizeof(assoc_ie) ||
	    memchr(ssid, 0, ssid_len) != NULL || (bssid[0] & 1) != 0 ||
	    memcmp(bssid, zero_bssid, sizeof(zero_bssid)) == 0 ||
	    !espwifi_wpa2_rsn(rsn, rsn_len))
		return -1;
	memset(&config, 0, sizeof(config));
	memcpy(config.sta.ssid, ssid, ssid_len);
	memcpy(config.sta.bssid, bssid, sizeof(config.sta.bssid));
	config.sta.bssid_set = true;
	config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
	error = esp_wifi_set_config(WIFI_IF_STA, &config);
	if (error != ESP_OK)
		return -1;
	memcpy(assoc_ie, rsn, rsn_len);
	error = esp_wifi_set_appie_internal(WIFI_APPIE_ASSOC_REQ,
	    assoc_ie, (uint16_t)rsn_len, 0);
	if (error != ESP_OK)
		return -1;
	error = esp_wifi_connect();
	if (error != ESP_OK) {
		(void)esp_wifi_unset_appie_internal(WIFI_APPIE_ASSOC_REQ);
		return -1;
	}
	return 0;
}

int
espwifi_ext_set_key(const unsigned char *bssid, unsigned int index,
    int group, int transmit, int modify, const unsigned char *sequence,
    unsigned int sequence_len, const unsigned char *key, unsigned int key_len)
{
	uint8_t addr[6], seq[8], secret[16];
	enum key_flag flags;
	int error;

	if (bssid == NULL || key == NULL || key_len != sizeof(secret) ||
	    index > 3 || (group != 0 && group != 1) ||
	    (transmit != 0 && transmit != 1) || (modify != 0 && modify != 1) ||
	    sequence_len > sizeof(seq) ||
	    (sequence_len != 0 && sequence == NULL))
		return -1;
	memcpy(addr, bssid, sizeof(addr));
	memset(seq, 0, sizeof(seq));
	if (sequence_len != 0)
		memcpy(seq, sequence, sequence_len);
	memcpy(secret, key, sizeof(secret));
	flags = (group ? KEY_FLAG_GROUP : KEY_FLAG_PAIRWISE) | KEY_FLAG_RX;
	if (transmit)
		flags |= KEY_FLAG_TX;
	if (modify)
		flags |= KEY_FLAG_MODIFY;
	error = esp_wifi_set_sta_key_internal(WIFI_WPA_ALG_CCMP, addr,
	    index, transmit, seq, sequence_len, secret, sizeof(secret), flags);
	forced_memzero(secret, sizeof(secret));
	forced_memzero(seq, sizeof(seq));
	return error == ESP_OK ? 0 : -1;
}
