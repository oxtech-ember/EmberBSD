/* SPDX-License-Identifier: BSD-2-Clause */

#ifndef _RISCV_ESPRESSIF_WIFI_FULLMAC_RX_OBSERVE_H_
#define _RISCV_ESPRESSIF_WIFI_FULLMAC_RX_OBSERVE_H_

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Diagnostic metadata only: no packet copies or authentication decisions. */
struct espwifi_rx_observation {
	unsigned mgmt, data, protected_data, aggregate, truncated, llc, eapol;
	unsigned eapol_type, key_info;
};

/* len excludes FCS; the caller supplies only complete RX packet buffers. */
static inline void
espwifi_rx_observe_frame(struct espwifi_rx_observation *s,
    const uint8_t *frame, size_t len, const uint8_t *bssid, const uint8_t *mac)
{
	static const uint8_t snap[] = { 0xaa, 0xaa, 3, 0, 0, 0 };
	size_t header = 24;

	if (frame == NULL || len < header || (frame[0] & 3) != 0 ||
	    memcmp(frame + 10, bssid, 6) != 0)
		return;
	if ((frame[0] & 0x0c) == 0) {
		s->mgmt++;
		return;
	}
	/* Only downlink unicast data from the selected AP to this station. */
	if ((frame[0] & 0x0c) != 8 || (frame[1] & 3) != 2 ||
	    memcmp(frame + 4, mac, 6) != 0)
		return;
	s->data++;
	if ((frame[1] & 0x40) != 0) {
		s->protected_data++;
		return;
	}
	if ((frame[0] & 0x40) != 0) /* Null data has no MSDU. */
		return;
	if ((frame[0] & 0x80) != 0) {
		if (len < header + 2)
			goto truncated;
		if ((frame[header] & 0x80) != 0) {
			s->aggregate++;
			return; /* Do not mistake an A-MSDU header for LLC. */
		}
		header += 2;
		if ((frame[1] & 0x80) != 0)
			header += 4; /* HT control follows QoS control. */
	}
	if (len < header + 8)
		goto truncated;
	if (memcmp(frame + header, snap, sizeof(snap)) != 0)
		return;
	s->llc++;
	if (frame[header + 6] != 0x88 || frame[header + 7] != 0x8e)
		return;
	s->eapol++;
	if (len < header + 12)
		goto truncated;
	s->eapol_type = frame[header + 9];
	s->key_info = 0;
	if (s->eapol_type == 3 && len >= header + 15)
		s->key_info = (unsigned)frame[header + 13] << 8 |
		    frame[header + 14];
	return;
truncated:
	s->truncated++;
}

#endif /* _RISCV_ESPRESSIF_WIFI_FULLMAC_RX_OBSERVE_H_ */
