/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native RTL8822C firmware contracts, AI-assisted. */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rtw88_hw.h"
#include "rtw8822c.h"

static unsigned int failures;
static unsigned int assertions;

#define CHECK(expr) do { \
	assertions++; \
	if (!(expr)) { \
		fprintf(stderr, "%s:%d: FAIL: %s\n", __func__, __LINE__, \
		    #expr); \
		failures++; \
	} \
} while (0)

/* Synthetic 9.9.11 metadata; reserved fields and build date are zero. */
static const uint8_t header[64] = {
	0x22, 0x88, 0x00, 0x00, 0x09, 0x00, 0x09, 0x0b,
	0x00, 0x00, 0x00, 0x00, 0xe7, 0x01, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x10, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x20, 0x80, 0x80, 0x4b, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x30, 0xd3, 0x00, 0x00, 0xd0, 0xf7, 0x01, 0x00,
	0x00, 0x00, 0x10, 0x80, 0x00, 0x00, 0x03, 0x80
};

static uint8_t image[202457];

static void
reset_image(void)
{

	memset(image, 0, sizeof(image));
	memcpy(image, header, sizeof(header));
}

static void
put32(uint8_t *p, uint32_t v)
{

	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

/* A failure must not publish any partially validated section. */
static void
reject(const uint8_t *data, size_t size, int expected)
{
	struct rtw88_fw_info info, before;

	memset(&info, 0xa5, sizeof(info));
	memcpy(&before, &info, sizeof(before));
	CHECK(rtw88_fw_parse(data, size, &info) == expected);
	CHECK(memcmp(&info, &before, sizeof(info)) == 0);
}

/* Allocations match short lengths so sanitizers detect early decoding. */
static void
test_fw_short(void)
{
	size_t i;

	for (i = 0; i < 64; i++) {
		uint8_t *data;

		data = malloc(i == 0 ? 1 : i);
		if (data == NULL) {
			perror("malloc");
			exit(1);
		}
		memcpy(data, header, i);
		reject(data, i, EINVAL);
		free(data);
	}
	reject(NULL, 202456, EINVAL);
	CHECK(rtw88_fw_parse(header, 64, NULL) == EINVAL);
}

/* Catch u32 checksum wrapping, missing trailers, and out-of-file ranges. */
static void
test_fw_section_overflow(void)
{
	static const size_t size_fields[] = { 36, 48, 52 };
	static const size_t truncated[] = { 64, 19399, 73471, 202455 };
	size_t i;

	for (i = 0; i < sizeof(size_fields) / sizeof(size_fields[0]); i++) {
		reset_image();
		put32(image + size_fields[i], UINT32_MAX);
		reject(image, 202456, EOVERFLOW);
		put32(image + size_fields[i], UINT32_MAX - 7);
		reject(image, 202456, EOVERFLOW);
		put32(image + size_fields[i], UINT32_MAX - 8);
		reject(image, 202456, EINVAL);
	}
	reset_image();
	for (i = 0; i < sizeof(truncated) / sizeof(truncated[0]); i++)
		reject(image, truncated[i], EINVAL);
	reject(image, 202457, EINVAL);
}

/* Common 0x8822 signature does not identify the silicon family. */
static void
test_fw_unsupported_profile(void)
{
	static const size_t addresses[] = { 32, 56, 60 };
	static const uint32_t untagged[] = { 0x00200000, 0x00100000,
	    0x00030000 };
	size_t i;

	reset_image();
	image[0] = 0x21;
	reject(image, 202456, ENOTSUP);
	for (i = 0; i < sizeof(addresses) / sizeof(addresses[0]); i++) {
		reset_image();
		put32(image + addresses[i], 0x80000000);
		reject(image, 202456, ENOTSUP);
		put32(image + addresses[i], untagged[i]);
		reject(image, 202456, ENOTSUP);
	}
	reset_image();
	image[24] &= (uint8_t)~0x10;
	/* Disabled EMEM leaves undeclared bytes after IMEM. */
	reject(image, 202456, EINVAL);
	/* A structurally valid two-section image is still unsupported. */
	reject(image, 73472, ENOTSUP);
	reset_image();
	/* Move one payload byte between sections: total size stays valid. */
	put32(image + 36, 19329);
	put32(image + 48, 54063);
	reject(image, 202456, ENOTSUP);
}

/* Catch swapped sections, omitted checksum bytes, and endian mistakes. */
static void
exact_sections(const uint8_t *data)
{
	struct rtw88_fw_info info;
	int error;

	memset(&info, 0, sizeof(info));
	error = rtw88_fw_parse(data, 202456, &info);
	CHECK(error == 0);
	if (error != 0)
		return;
	CHECK(info.signature == 0x8822);
	CHECK(info.version == 9);
	CHECK(info.subversion == 9);
	CHECK(info.subindex == 11);
	CHECK(info.h2c_version == 15);
	CHECK(info.feature == 0x000001e7);
	CHECK(info.dmem.offset == 64);
	CHECK(info.dmem.payload_size == 19328);
	CHECK(info.dmem.size == 19336);
	CHECK(info.dmem.load_address == 0x00200000);
	CHECK(info.imem.offset == 19400);
	CHECK(info.imem.payload_size == 54064);
	CHECK(info.imem.size == 54072);
	CHECK(info.imem.load_address == 0x00030000);
	CHECK(info.emem.offset == 73472);
	CHECK(info.emem.payload_size == 128976);
	CHECK(info.emem.size == 128984);
	CHECK(info.emem.load_address == 0x00100000);
	CHECK(info.emem.offset + info.emem.size == 202456);
}

static void
test_fw_exact_sections(void)
{
	uint8_t *unaligned;

	reset_image();
	exact_sections(image);
	unaligned = malloc(202457);
	if (unaligned == NULL) {
		perror("malloc");
		exit(1);
	}
	memcpy(unaligned + 1, image, 202456);
	exact_sections(unaligned + 1);
	free(unaligned);
}

/* Fixtures are hash-verified by the wrapper, not trusted by their names. */
static void
test_fw_fixture(const char *path, size_t size, int supported)
{
	uint8_t *data;
	FILE *fp;

	fp = fopen(path, "rb");
	if (fp == NULL) {
		perror(path);
		exit(1);
	}
	data = malloc(size);
	if (data == NULL) {
		perror("malloc");
		exit(1);
	}
	if (fread(data, 1, size, fp) != size || fgetc(fp) != EOF ||
	    ferror(fp) || fclose(fp) != 0) {
		fprintf(stderr, "%s: fixture read failed\n", path);
		exit(1);
	}
	if (supported)
		exact_sections(data);
	else
		reject(data, size, ENOTSUP);
	free(data);
}

/* Literal records from the documented header format, not a map encoder. */
static const uint8_t efuse_mac_record[] = {
	0x8f, 0x48, 0x02, 0x11, 0x22, 0x33, 0x44, 0x55
};

static void
efuse_reject(const uint8_t *data, size_t size, int error)
{
	struct rtw8822c_efuse out, before;

	memset(&out, 0xa5, sizeof(out));
	memcpy(&before, &out, sizeof(before));
	CHECK(rtw8822c_efuse_decode(data, size, &out) == error);
	CHECK(memcmp(&before, &out, sizeof(out)) == 0);
}

/* Catch speculative extended-header and incomplete-word reads. */
static void
test_efuse_truncated_extended_header(void)
{
	static const uint8_t partial[] = { 0x8f, 0x48, 0x02, 0x11,
	    0x22, 0x33, 0x44 };
	static const uint8_t normal[] = { 0x0e, 0x12 };
	size_t i;

	for (i = 1; i <= sizeof(partial); i++) {
		uint8_t *p = malloc(i);

		if (p == NULL)
			exit(1);
		memcpy(p, partial, i);
		efuse_reject(p, i, EINVAL);
		free(p);
	}
	efuse_reject(normal, sizeof(normal), EINVAL);
	efuse_reject(NULL, 0, EINVAL);
	efuse_reject(partial, 0, EINVAL);
	efuse_reject(partial, 513, EINVAL);
	efuse_reject(partial, SIZE_MAX, EINVAL);
	CHECK(rtw8822c_efuse_decode(partial, sizeof(partial), NULL) == EINVAL);
}

/* A valid earlier MAC must not hide a malformed later record. */
static void
test_efuse_truncated_after_mac(void)
{
	static const uint8_t suffix[] = { 0x0f, 0x2e, 0x12 };
	size_t i;

	for (i = 1; i <= sizeof(suffix); i++) {
		uint8_t *data = malloc(8 + i);

		if (data == NULL)
			exit(1);
		memcpy(data, efuse_mac_record, 8);
		memcpy(data + 8, suffix, i);
		efuse_reject(data, 8 + i, EINVAL);
		free(data);
	}
}

/* Block 96 begins at 768; block 95 word 3 is the last legal word. */
static void
test_efuse_logical_overflow(void)
{
	static const uint8_t overflow[] = { 0x0f, 0xce, 0x12, 0x34 };
	static const uint8_t top[] = { 0xef, 0xf7, 0x12, 0x34 };
	uint8_t valid[12];
	struct rtw8822c_efuse out = { 0 };

	efuse_reject(overflow, sizeof(overflow), EOVERFLOW);
	efuse_reject(top, sizeof(top), EOVERFLOW);
	memcpy(valid, efuse_mac_record, 8);
	valid[8] = 0xef;
	valid[9] = 0xb7;
	valid[10] = 0x12;
	valid[11] = 0x34;
	CHECK(rtw8822c_efuse_decode(valid, sizeof(valid), &out) == 0);
	CHECK(out.logical[766] == 0x12 && out.logical[767] == 0x34);
}

/* No random-address fallback may turn invalid identity into success. */
static void
test_efuse_mac_invalid(void)
{
	uint8_t data[8];
	static const uint8_t blank[] = { 0xff };

	memcpy(data, efuse_mac_record, sizeof(data));
	data[2] = 0x01;
	efuse_reject(data, sizeof(data), EINVAL);
	memset(data + 2, 0, 6);
	efuse_reject(data, sizeof(data), EINVAL);
	memset(data + 2, 0xff, 6);
	efuse_reject(data, sizeof(data), EINVAL);
	efuse_reject(blank, sizeof(blank), EINVAL);
}

/* Preserve exact bytes, sparse words, overwrite order, and erased values. */
static void
test_efuse_exact_map(void)
{
	static const uint8_t records[] = {
		0x8f, 0x48, 0x02, 0x11, 0x22, 0x33, 0x44, 0x55,
		0x2e, 0x81, 0xf7,
		0xef, 0x20, 0x34, 0xfe, 0x09, 0x08, 0x07, 0x06, 0x05, 0x04,
		0x0f, 0x30, 0x01, 0x22, 0x03, 0x45, 0x06, 0x07, 0x08, 0xa9,
		0x2f, 0x38, 0xa9, 0x31, 0x05, 'U', 'S', 0x66,
		0x4f, 0x3e, 0x1a, 0x2b,
		0x8f, 0x4b, 0xaa, 0xbb,
		0xff, 0x0f, 0xce
	};
	struct rtw8822c_efuse out = { 0 };
	uint8_t unaligned[sizeof(records) + 1];

	memcpy(unaligned + 1, records, sizeof(records));
	CHECK(rtw8822c_efuse_decode(unaligned + 1, sizeof(records), &out) == 0);
	CHECK(memcmp(out.mac, "\x02\x11\x22\x33\xaa\xbb", 6) == 0);
	CHECK(out.logical[0] == 0xff && out.logical[767] == 0xff);
	CHECK(out.txpower[0][0] == 0x81 && out.txpower[0][1] == 0xf7);
	CHECK(out.txpower[3][41] == 0xff);
	CHECK(out.channel_plan == 0x34 && out.xtal == 0x7e);
	CHECK(out.board_option == 0x22 && out.bt_setting == 0x45);
	CHECK(out.rfe_option == 0x05 && out.power_track_type == 0x0a);
	CHECK(out.country[0] == 'U' && out.country[1] == 'S');
	CHECK(out.thermal[0] == 0x1a && out.thermal[1] == 0x2b);
}

/* The protected tail cannot complete a record or overwrite the map. */
static void
test_efuse_physical_boundary(void)
{
	uint8_t data[512];
	struct rtw8822c_efuse out = { 0 };

	memset(data, 0x1f, sizeof(data)); /* Normal header: no enabled words. */
	memcpy(data, efuse_mac_record, 8);
	data[385] = 0x0e;
	data[386] = 0x12;
	data[387] = 0x34;
	data[388] = 0x0f;
	data[389] = 0xce;
	CHECK(rtw8822c_efuse_decode(data, sizeof(data), &out) == 0);
	CHECK(out.logical[0] == 0x12 && out.logical[1] == 0x34);
	data[385] = 0x1f;
	data[386] = 0x0e;
	efuse_reject(data, sizeof(data), EINVAL);
	data[386] = 0x1f;
	data[387] = 0x0f;
	efuse_reject(data, sizeof(data), EINVAL);
	data[386] = 0x0f;
	data[387] = 0xff; /* Upstream extended termination rule. */
	CHECK(rtw8822c_efuse_decode(data, sizeof(data), &out) == 0);
}

/* A-G fit the upstream eight-bit power mask; cut 7 must not skip it. */
static void
test_identity_unsupported_cut(void)
{
	struct rtw8822c_identity id = { 0x10ec, 0xc822, 0x08000000, 0 };
	unsigned int cut;

	for (cut = 0; cut <= 15; cut++) {
		id.sys_cfg1 = 0x08000000 | (cut << 12);
		CHECK(rtw8822c_validate_identity(&id) == (cut <= 6 ? 0 : ENOTSUP));
	}
	id.sys_cfg1 = 0xffffffff;
	CHECK(rtw8822c_validate_identity(&id) == ENOTSUP);
	id.sys_cfg1 = 0x08800000; /* Test silicon. */
	CHECK(rtw8822c_validate_identity(&id) == ENOTSUP);
	id.sys_cfg1 = 0x08001000;
	id.product = 0xb822;
	CHECK(rtw8822c_validate_identity(&id) == ENOTSUP);
	id.product = 0xc82f; /* Valid upstream device, outside this profile. */
	CHECK(rtw8822c_validate_identity(&id) == ENOTSUP);
	id.product = 0xc822;
	id.vendor = 0x1234;
	CHECK(rtw8822c_validate_identity(&id) == ENOTSUP);
	id.vendor = 0x10ec;
	for (id.rfe_option = 0; id.rfe_option < 8; id.rfe_option++)
		CHECK(rtw8822c_validate_identity(&id) ==
		    (id.rfe_option <= 6 ? 0 : ENOTSUP));
	id.rfe_option = 0xff;
	CHECK(rtw8822c_validate_identity(&id) == ENOTSUP);
	CHECK(rtw8822c_validate_identity(NULL) == EINVAL);
}

/* Catch wrong endian/offsets, missing tail zeroing, and 32-byte checksum. */
static void
test_tx_exact(void)
{
	static const uint8_t expected[48] = {
		0x34, 0x12, 0x30, 0x85, 0xab, 0x11, 0xda, 0x20,
		0x00, 0x10, 0xd8, 0x00, 0x80, 0x95, 0x22, 0x00,
		0x45, 0x00, 0x00, 0x08, 0xd0, 0x12, 0x00, 0x00,
		0xbc, 0x0a, 0x00, 0x00, 0xeb, 0x23, 0x00, 0x00,
		0x00, 0x80, 0x00, 0x00, 0xb5, 0x90, 0x78, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	};
	struct rtw88_tx_info info = {
		.packet_size = 0x1234, .flags = RTW88_TX_FLAGS,
		.sequence = 0x789, .report_sequence = 0xabc,
		.mac_id = 0xab, .queue = 17, .rate_id = 0x1a,
		.security = 3, .rate = 0x45, .bandwidth = 2, .stbc = 2,
		.ampdu_density = 5, .ampdu_factor = 17, .hw_ssn_sel = 2,
		.tim_offset = 0x35
	};
	uint8_t data[50];

	memset(data, 0xa5, sizeof(data));
	CHECK(rtw88_tx_encode(data + 1, 48, &info) == 0);
	CHECK(memcmp(data + 1, expected, 48) == 0);
	CHECK(data[0] == 0xa5 && data[49] == 0xa5);
	/* A second call must replace all flags/checksum, not merge old bits. */
	memset(&info, 0, sizeof(info));
	info.packet_size = 1;
	CHECK(rtw88_tx_encode(data + 1, 49, &info) == 0);
	CHECK(data[1] == 1 && data[3] == 48 && data[4] == 4);
	CHECK(data[29] == 0x31 && data[30] == 0x04);
	CHECK(data[49] == 0xa5);
	data[1] = data[3] = data[4] = data[29] = data[30] = 0;
	CHECK(memcmp(data + 1, (uint8_t[48]){0}, 48) == 0);
}

static void
tx_reject(const struct rtw88_tx_info *info, size_t cap, int error)
{
	uint8_t data[50], before[50];

	memset(data, 0xa5, sizeof(data));
	memcpy(before, data, sizeof(data));
	CHECK(rtw88_tx_encode(data + 1, cap, info) == error);
	CHECK(memcmp(before, data, sizeof(data)) == 0);
}

/* Catch masking invalid fields and writes before all checks complete. */
static void
test_tx_bounds(void)
{
	struct rtw88_tx_info info = { .packet_size = 1 };
	size_t i;

	for (i = 0; i < 48; i++)
		tx_reject(&info, i, EINVAL);
	tx_reject(NULL, 48, EINVAL);
	CHECK(rtw88_tx_encode(NULL, 48, &info) == EINVAL);
#define BAD_TX(field, value, error) do { \
	info.field = (value); \
	tx_reject(&info, 48, (error)); \
	info.field = 0; \
} while (0)
	BAD_TX(packet_size, 0, EINVAL);
	BAD_TX(packet_size, 65536, EINVAL);
	info.packet_size = 1;
	BAD_TX(flags, 0x1000, EINVAL);
	BAD_TX(sequence, 0x1000, EINVAL);
	BAD_TX(report_sequence, 0x1000, EINVAL);
	BAD_TX(queue, 20, EINVAL);
	BAD_TX(rate_id, 32, EINVAL);
	BAD_TX(security, 4, EINVAL);
	BAD_TX(rate, 128, EINVAL);
	BAD_TX(bandwidth, 4, EINVAL);
	BAD_TX(stbc, 4, EINVAL);
	BAD_TX(ampdu_density, 8, EINVAL);
	BAD_TX(ampdu_factor, 32, EINVAL);
	BAD_TX(hw_ssn_sel, 4, EINVAL);
	BAD_TX(tim_offset, 128, EINVAL);
	BAD_TX(packet_offset, 1, ENOTSUP);
	BAD_TX(packet_offset, 255, ENOTSUP);
#undef BAD_TX
	/* Inclusive maxima must fit without truncation. */
	info.packet_size = 65535;
	info.sequence = info.report_sequence = 4095;
	info.mac_id = 255;
	info.queue = 19;
	info.rate_id = info.ampdu_factor = 31;
	info.security = info.bandwidth = info.stbc = info.hw_ssn_sel = 3;
	info.rate = info.tim_offset = 127;
	info.ampdu_density = 7;
	{
		uint8_t data[48];

		CHECK(rtw88_tx_encode(data, sizeof(data), &info) == 0);
	}
}

static void
rx_reject(const uint8_t *data, size_t len)
{
	struct rtw88_rx_info out, before;

	memset(&out, 0xa5, sizeof(out));
	memcpy(&before, &out, sizeof(out));
	CHECK(rtw88_rx_decode(data, len, &out) == EINVAL);
	CHECK(memcmp(&before, &out, sizeof(out)) == 0);
}

/* Allocations expose speculative reads beyond the 24-byte minimum. */
static void
test_rx_bounds(void)
{
	uint8_t data[32] = { 0 };
	struct rtw88_rx_info out;
	size_t i;

	for (i = 0; i < 24; i++) {
		uint8_t *shortbuf = calloc(i == 0 ? 1 : i, 1);

		if (shortbuf == NULL)
			exit(1);
		rx_reject(shortbuf, i);
		free(shortbuf);
	}
	rx_reject(NULL, 24);
	CHECK(rtw88_rx_decode(data, 24, NULL) == EINVAL);
	CHECK(rtw88_rx_decode(data, 24, &out) == 0);
	data[0] = 1;
	rx_reject(data, 24); /* One payload byte outside the receive buffer. */
	data[0] = 0;
	data[2] = 1; /* Driver info in eight-byte units. */
	rx_reject(data, 31);
	CHECK(rtw88_rx_decode(data, 32, &out) == 0);
	data[3] = 1; /* Shift precedes driver info. */
	rx_reject(data, 32);
	data[2] = 0;
	data[0] = 8;
	rx_reject(data, 32); /* Payload alone fits, shift plus payload does not. */
	data[11] = 0x10;
	rx_reject(data, 32); /* C2H must not bypass bounds. */
}

/* Literal LE fields expose CRC/ICV/C2H without assuming successful RX. */
static void
test_rx_exact(void)
{
	static const uint8_t desc[24] = {
		0x34, 0xd2, 0x43, 0x06, 0x65, 0, 0, 0,
		0, 0, 0, 0x70, 0x53, 0, 0, 0,
		0x20, 0, 0, 0, 0x78, 0x56, 0x34, 0x12
	};
	uint8_t *data = calloc(1, 4711);
	struct rtw88_rx_info out = { 0 };

	if (data == NULL)
		exit(1);
	memcpy(data + 1, desc, 24);
	CHECK(rtw88_rx_decode(data + 1, 4710, &out) == 0);
	CHECK(out.packet_size == 0x1234);
	CHECK(out.driver_info_offset == 26 && out.driver_info_size == 24);
	CHECK(out.payload_offset == 50 && out.shift == 2);
	CHECK(out.flags == (RTW88_RX_CRC_ERROR | RTW88_RX_ICV_ERROR |
	    RTW88_RX_PHY_STATUS | RTW88_RX_C2H));
	CHECK(out.security == 4 && out.mac_id == 0x65);
	CHECK(out.ppdu_count == 3 && out.rate == 0x53);
	CHECK(out.bandwidth == 2 && out.tsf_low == 0x12345678);
	rx_reject(data + 1, 4709);
	data[4] |= 8;
	CHECK(rtw88_rx_decode(data + 1, 4710, &out) == 0);
	CHECK((out.flags & RTW88_RX_SWDEC) != 0);
	/* Reuse must clear prior flags; spare buffer capacity is permitted. */
	memset(data + 1, 0, 24);
	CHECK(rtw88_rx_decode(data + 1, 4710, &out) == 0);
	CHECK(out.flags == 0 && out.payload_offset == 24 && out.tsf_low == 0);
	free(data);
}

/* Catch truncation, endpoint off-by-one, overflow and ignored apertures. */
static void
test_dma_ranges(void)
{
	CHECK(rtw88_dma_validate(0, 1, 32) == 0);
	CHECK(rtw88_dma_validate(0xffffffff, 1, 32) == 0);
	CHECK(rtw88_dma_validate(0xffffffff, 2, 32) == EOVERFLOW);
	CHECK(rtw88_dma_validate(0xfffffff0, 16, 32) == 0);
	CHECK(rtw88_dma_validate(0xfffffff0, 17, 32) == EOVERFLOW);
	CHECK(rtw88_dma_validate(UINT64_C(0x100000000), 1, 32) == EOVERFLOW);
	CHECK(rtw88_dma_validate(UINT64_MAX, 2, 32) == EOVERFLOW);
	CHECK(rtw88_dma_validate(2, SIZE_MAX, 32) == EOVERFLOW);
	CHECK(rtw88_dma_validate(0, 0, 32) == EINVAL);
	CHECK(rtw88_dma_validate(0, 1, 0) == EINVAL);
	CHECK(rtw88_dma_validate(0, 1, 65) == EINVAL);
	CHECK(rtw88_dma_validate(0, 1, 33) == ENOTSUP);
	CHECK(rtw88_dma_validate(0, 1, 64) == ENOTSUP);
	CHECK(rtw88_dma_validate(0x7fffffff, 1, 31) == 0);
	CHECK(rtw88_dma_validate(0x7fffffff, 2, 31) == EOVERFLOW);
	CHECK(rtw88_dma_validate(0x80000000, 1, 31) == EOVERFLOW);
	CHECK(rtw88_dma_validate(1, 1, 1) == 0);
	CHECK(rtw88_dma_validate(1, 2, 1) == EOVERFLOW);
	CHECK(rtw88_dma_validate(2, 1, 1) == EOVERFLOW);
#if SIZE_MAX > UINT32_MAX
	CHECK(rtw88_dma_validate(0, (size_t)UINT64_C(0x100000000), 32) == 0);
	CHECK(rtw88_dma_validate(1, (size_t)UINT64_C(0x100000000), 32) ==
	    EOVERFLOW);
#endif
}

int
main(int argc, char **argv)
{

	if (argc != 1 && argc != 3) {
		fprintf(stderr, "usage: %s [RTL8822C_FIRMWARE RTL8822B_FIRMWARE]\n",
		    argv[0]);
		return 2;
	}
	test_tx_exact();
	test_tx_bounds();
	test_rx_bounds();
	test_rx_exact();
	test_dma_ranges();
	if (getenv("RTW88_DESCRIPTORS_ONLY") != NULL)
		goto done;
	test_efuse_truncated_extended_header();
	test_efuse_truncated_after_mac();
	test_efuse_logical_overflow();
	test_efuse_mac_invalid();
	test_efuse_exact_map();
	test_efuse_physical_boundary();
	test_identity_unsupported_cut();
	test_fw_short();
	test_fw_section_overflow();
	test_fw_unsupported_profile();
	test_fw_exact_sections();
	if (argc == 3) {
		test_fw_fixture(argv[1], 202456, 1);
		test_fw_fixture(argv[2], 150984, 0);
	}
done:
	printf("rtw88 contracts: %u assertions, %u failures%s\n", assertions,
	    failures, argc == 3 ? " (real C/B fixtures)" : " (synthetic)");
	return failures != 0;
}
