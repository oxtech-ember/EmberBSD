/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD; AI-assisted RTL8723D descriptor contracts. */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <dev/ic/rtw8723d_rx.h>
#include <dev/ic/rtw8723d_tx.h>

static void
put32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

int
main(void)
{
	uint8_t rxwire[37] = {0}, txwire[40], saved[40];
	struct rtw88_rx_info rx, oldrx;
	struct rtw88_tx_info tx = {0};
	uint16_t checksum = 0;
	unsigned i;

	/* 3 payload bytes, 8 PHY bytes, 2-byte shift, plus PHY and CRC flags. */
	put32(rxwire, 3U | (1U << 16) | (2U << 24) | (1U << 26) | (1U << 14));
	put32(rxwire + 4, 42);
	put32(rxwire + 8, 1U << 28);
	put32(rxwire + 12, 19);
	put32(rxwire + 20, 0x12345678);
	assert(rtw8723d_rx_decode(rxwire, sizeof(rxwire), &rx) == 0);
	assert(rx.packet_size == 3 && rx.driver_info_offset == 26 &&
	    rx.driver_info_size == 8 && rx.payload_offset == 34);
	assert(rx.mac_id == 42 && rx.rate == 19 && rx.tsf_low == 0x12345678);
	assert(rx.flags == (RTW88_RX_PHY_STATUS | RTW88_RX_CRC_ERROR | RTW88_RX_C2H));
	assert(rx.sequence == 0 && rx.generation == 0 && rx.ppdu_count == 0);
	oldrx = rx;
	assert(rtw8723d_rx_decode(rxwire, sizeof(rxwire) - 1, &rx) == EINVAL);
	assert(memcmp(&rx, &oldrx, sizeof(rx)) == 0);
	assert(rtw8723d_rx_decode(rxwire, 23, &rx) == EINVAL);
	assert(memcmp(&rx, &oldrx, sizeof(rx)) == 0);

	tx.packet_size = 42; tx.queue = 17;
	memset(txwire, 0xa5, sizeof(txwire));
	assert(rtw8723d_tx_encode(txwire, sizeof(txwire), &tx) == 0);
	assert(txwire[0] == 42 && txwire[1] == 0 && txwire[2] == 40 &&
	    txwire[3] == 4 && txwire[5] == 17 && (txwire[7] & 0x20) != 0);
	for (i = 0; i < 32; i += 2)
		checksum ^= (uint16_t)txwire[i] | (uint16_t)txwire[i + 1] << 8;
	assert(checksum == UINT16_MAX);
	memcpy(saved, txwire, sizeof(saved));
	tx.bandwidth = 1;
	assert(rtw8723d_tx_encode(txwire, sizeof(txwire), &tx) == EOPNOTSUPP);
	assert(memcmp(txwire, saved, sizeof(saved)) == 0);
	tx.bandwidth = 0; tx.packet_size = 0;
	assert(rtw8723d_tx_encode(txwire, sizeof(txwire), &tx) == EINVAL);
	assert(memcmp(txwire, saved, sizeof(saved)) == 0);
	assert(rtw8723d_tx_encode(txwire, sizeof(txwire) - 1, &tx) == EINVAL);
	puts("PASS: RTL8723D RX bounds and TX descriptor checksum and refusals");
	return 0;
}
