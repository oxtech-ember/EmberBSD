/* SPDX-License-Identifier: BSD-2-Clause */

#ifndef _RISCV_ESPRESSIF_WIFI_FULLMAC_DEBUG_H_
#define _RISCV_ESPRESSIF_WIFI_FULLMAC_DEBUG_H_

#include <stdint.h>

#ifdef ESPWIFI_VENDOR_DEBUG_ONCE
void espwifi_vendor_debug_once_begin(const unsigned char *);
void espwifi_vendor_debug_once_end(void);
#ifdef ESPWIFI_RX_CHAIN_ONCE
void espwifi_vendor_debug_assoc_snapshot(const unsigned char *);
#else
#define espwifi_vendor_debug_assoc_snapshot(bssid) ((void)0)
#endif
#else
#define espwifi_vendor_debug_once_begin(bssid) ((void)0)
#define espwifi_vendor_debug_once_end() ((void)0)
#define espwifi_vendor_debug_assoc_snapshot(bssid) ((void)0)
#endif

int espwifi_vendor_action_tx_submit(const void *, unsigned long, uint32_t,
	    uint32_t, const unsigned char *);

#endif /* _RISCV_ESPRESSIF_WIFI_FULLMAC_DEBUG_H_ */
