/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef ESPWIFI_ASSOC_CAPS_H
#define ESPWIFI_ASSOC_CAPS_H

/* Payload capacity of the selected S31 ieee80211_add_extcap ABI. */
#define ESPWIFI_EXTCAP_LEN 12

void espwifi_vendor_assoc_caps_clear(void);
void espwifi_vendor_assoc_caps_set(const uint8_t *, size_t);

#endif
