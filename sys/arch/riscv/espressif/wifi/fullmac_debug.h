/* SPDX-License-Identifier: BSD-2-Clause */

#ifndef _RISCV_ESPRESSIF_WIFI_FULLMAC_DEBUG_H_
#define _RISCV_ESPRESSIF_WIFI_FULLMAC_DEBUG_H_

#ifdef ESPWIFI_VENDOR_DEBUG_ONCE
void espwifi_vendor_debug_once_begin(const unsigned char *);
void espwifi_vendor_debug_once_end(void);
#else
#define espwifi_vendor_debug_once_begin(bssid) ((void)0)
#define espwifi_vendor_debug_once_end() ((void)0)
#endif

#endif /* _RISCV_ESPRESSIF_WIFI_FULLMAC_DEBUG_H_ */
