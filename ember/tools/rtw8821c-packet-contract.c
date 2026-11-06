/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Source-level descriptor contract; not hardware acceptance. */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rtw8821c_hw.h"

static unsigned failures;
#define CHECK(x) do { if (!(x)) { \
 fprintf(stderr, "line %d: %s\n", __LINE__, #x); failures++; \
} } while (0)

static void
test_tx(void)
{
	/* Donor checksum helper covers 32 bytes; PCI use is not established. */
	static const uint8_t expected[48] = {
		0x34,0x12,0x30,0x85,0xab,0x11,0xda,0x20,
		0x00,0x10,0xd8,0x00,0x80,0x95,0x22,0x00,
		0x45,0x00,0x00,0x08,0xd0,0x12,0x00,0x00,
		0xbc,0x0a,0x00,0x00,0x26,0x33,0x00,0x00,
		0x00,0x80,0x00,0x00,0xb5,0x90,0x78,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
	};
	struct rtw88_tx_info info = {
		.packet_size=0x1234, .flags=RTW88_TX_FLAGS,
		.sequence=0x789, .report_sequence=0xabc, .mac_id=0xab,
		.queue=17, .rate_id=0x1a, .security=3, .rate=0x45,
		.bandwidth=2, .stbc=2, .ampdu_density=5, .ampdu_factor=17,
		.hw_ssn_sel=2, .tim_offset=0x35
	};
	uint8_t buf[50], before[50], other[48];
	size_t i;

	memset(buf, 0xa5, sizeof(buf));
	CHECK(rtw8821c_tx_encode(buf + 1, 48, &info) == 0);
	CHECK(memcmp(buf + 1, expected, 48) == 0);
	CHECK(buf[0] == 0xa5 && buf[49] == 0xa5);
	CHECK(rtw88_tx_encode(other, sizeof(other), &info) == 0);
	CHECK(memcmp(other, expected, 48) != 0);
	{
		static const uint8_t simple[48] = {
			1,0,0x30,4, [28]=0x31, [29]=4, [33]=0x80
		};
		struct rtw88_tx_info one = { .packet_size=1, .flags=RTW88_TX_HWSEQ };
		CHECK(rtw8821c_tx_encode(buf + 1, 48, &one) == 0);
		CHECK(memcmp(buf + 1, simple, 48) == 0);
		CHECK(rtw88_tx_encode(other, 48, &one) == 0);
		CHECK(other[28] == 0x31 && other[29] == 0x84);
	}
	for (i = 0; i < 48; i++) {
		memset(buf, 0xa5, sizeof(buf)); memcpy(before, buf, sizeof(buf));
		CHECK(rtw8821c_tx_encode(buf + 1, i, &info) == EINVAL);
		CHECK(memcmp(buf, before, sizeof(buf)) == 0);
	}
	memset(buf, 0xa5, sizeof(buf)); memcpy(before, buf, sizeof(buf));
	CHECK(rtw8821c_tx_encode(buf + 1, 48, NULL) == EINVAL);
	CHECK(memcmp(buf, before, sizeof(buf)) == 0);
	CHECK(rtw8821c_tx_encode(NULL, 48, &info) == EINVAL);
	{
		const struct rtw88_tx_profile invalid = { .checksum_bytes = 255 };
		memset(buf, 0xa5, sizeof(buf)); memcpy(before, buf, sizeof(buf));
		CHECK(rtw88_tx_encode_profile(buf + 1, 48, &info, &invalid) == EINVAL);
		CHECK(memcmp(buf, before, sizeof(buf)) == 0);
	}
#define BAD(field, value, err) do { \
 info.field = (value); memset(buf, 0xa5, sizeof(buf)); \
 memcpy(before, buf, sizeof(buf)); \
 CHECK(rtw8821c_tx_encode(buf + 1, 48, &info) == (err)); \
 CHECK(memcmp(buf, before, sizeof(buf)) == 0); info.field = 0; \
} while (0)
	BAD(data_sc, 1, ENOTSUP); /* No selected-donor proof for this field. */
	BAD(packet_offset, 1, ENOTSUP);
	BAD(sequence, 4096, EINVAL);
	BAD(queue, 20, EINVAL);
	BAD(rate_id, 32, EINVAL);
	BAD(bandwidth, 4, EINVAL);
	BAD(packet_size, 65536, EINVAL);
#undef BAD
	info.packet_size = 1;
	info.data_sc = 15;
	CHECK(rtw88_tx_encode(other, sizeof(other), &info) == 0);
	CHECK((other[20] & 15) == 15);
}

static void
test_rx(void)
{
	/* 24-byte header + 8-byte driver info + 3 shift + 2 payload. */
	static const uint8_t raw[37] = {
		2,0xc0,0x51,0x0f,0xff,0,0,0, 0,0,0,0x70,
		0xff,0,0,0, 0x30,0,0,0, 0x78,0x56,0x34,0x12
	};
	uint8_t data[38]; struct rtw88_rx_info out, before; size_t i;
	memset(data, 0, sizeof(data)); memcpy(data + 1, raw, sizeof(raw));
	memset(&out, 0xa5, sizeof(out));
	CHECK(rtw8821c_rx_decode(data + 1, 37, &out) == 0);
	CHECK(out.packet_size == 2 && out.driver_info_offset == 27);
	CHECK(out.driver_info_size == 8 && out.payload_offset == 35 && out.shift == 3);
	CHECK(out.mac_id == 0x7f && out.rate == 0x7f && out.bandwidth == 3);
	CHECK(out.tsf_low == 0x12345678);
	CHECK(out.flags == 0x1f && out.security == 5 && out.ppdu_count == 3);
	CHECK(out.sequence == 0 && out.generation == 0 && out.host_epoch_valid == 0);
	CHECK(out.epoch == 0 && out.key_epoch == 0);
	CHECK(out.signal.generation == 0 && out.signal.power[0] == 0 &&
	    out.signal.power[1] == 0 && out.signal.length == 0);
	CHECK(out.signal.valid == 0 && out.signal.reason == 0 && out.signal.page == 0 &&
	    out.signal.rate == 0 && out.signal.channel == 0 && out.signal.paths == 0 &&
	    out.signal.bandwidth == 0 && out.signal.rxsc == 0);
	CHECK(out.signal.pwdb[0] == 0 && out.signal.pwdb[1] == 0 &&
	    out.signal.gain[0] == 0 && out.signal.gain[1] == 0 && out.signal.quality == 0);
	for (i = 0; i < 37; i++) {
		memset(&out, 0xa5, sizeof(out)); memcpy(&before, &out, sizeof(out));
		CHECK(rtw8821c_rx_decode(data + 1, i, &out) == EINVAL);
		CHECK(memcmp(&out, &before, sizeof(out)) == 0);
	}
	memset(&out, 0xa5, sizeof(out)); memcpy(&before, &out, sizeof(out));
	CHECK(rtw8821c_rx_decode(NULL, 37, &out) == EINVAL);
	CHECK(memcmp(&out, &before, sizeof(out)) == 0);
	CHECK(rtw8821c_rx_decode(data + 1, 37, NULL) == EINVAL);
	{
		uint8_t *large = calloc(16530, 1);
		if (large == NULL) exit(1);
		large[0] = 0xff; large[1] = 0x3f;
		large[2] = 0x0f; large[3] = 0x03;
		CHECK(rtw8821c_rx_decode(large, 16530, &out) == 0);
		CHECK(out.packet_size == 16383 && out.driver_info_size == 120 &&
		    out.shift == 3 && out.payload_offset == 147);
		memset(&out, 0xa5, sizeof(out)); memcpy(&before, &out, sizeof(out));
		CHECK(rtw8821c_rx_decode(large, 16529, &out) == EINVAL);
		CHECK(memcmp(&out, &before, sizeof(out)) == 0);
		free(large);
	}
}

int
main(void)
{
	test_tx(); test_rx();
	if (failures) return 1;
	puts("PASS: RTL8821C source packet contract");
	return 0;
}
