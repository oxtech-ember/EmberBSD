/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. Original AI-assisted tests. */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cixvpu_buffer.h"
#include "cixvpu_ring.h"

static unsigned int checks;
#define CHECK(c) do { checks++; if (!(c)) { \
	fprintf(stderr, "FAIL line %u: %s\n", (unsigned int)__LINE__, #c); \
	exit(1); } } while (0)
static const struct cixvpu_buffer_nv12 frame = {
	.handle = UINT64_C(0x8877665544332211),
	.tag = UINT64_C(0x1020304050607080),
	.width = 2, .height = 4, .stride_y = 4, .stride_uv = 6,
	.y = {0x70000001, 16}, .uv = {0x70001003, 12}, .force_idr = true
};
static const struct cixvpu_buffer_bitstream stream = {
	.handle = UINT64_C(0x8877665544332211),
	.tag = UINT64_C(0x1020304050607080), .data = {0x24400003, 0x10203}
};
/* Hand-derived literal vectors, including every unused byte. */
static const uint8_t frame_bytes[72] = {
	0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,
	0x80,0x70,0x60,0x50,0x40,0x30,0x20,0x10,
	4,4,0,0, 4,0,2,0, 1,2,0,0,
	1,0,0,0x70, 3,0x10,0,0x70, 0,0,0,0,
	0,0,0,0, 0,0,0,0, 0,0,0,0,
	4,0,0,0, 6,0,0,0, 0,0,0,0, 2,0,4,0, 0,0,0,0
};
static const uint8_t stream_bytes[40] = {
	0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,
	0x80,0x70,0x60,0x50,0x40,0x30,0x20,0x10,
	0,0,0,0, 3,2,1,0, 0,0,0,0, 0,0,0,0, 3,0,0x40,0x24,
	0,0,0,0
};
static const uint8_t returned[40] = {
	0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,
	0x80,0x70,0x60,0x50,0x40,0x30,0x20,0x10,
	0x80,0,0,0x80, 3,2,1,0, 0,1,0,0, 3,2,0,0, 3,0,0x40,0x24,
	6,7,0x44,0x33
};

static void
bad_frame(const struct cixvpu_buffer_nv12 *slot)
{
	uint8_t data[80], before[80];
	size_t length = 99;

	memset(data, 0xa5, sizeof(data)); memcpy(before, data, sizeof(data));
	CHECK(cixvpu_buffer_frame_nv12(slot, data, 72, &length) == EINVAL);
	CHECK(length == 99 && memcmp(data, before, sizeof(data)) == 0);
}

static void
bad_stream(const struct cixvpu_buffer_bitstream *slot)
{
	uint8_t data[48], before[48];
	size_t length = 99;

	memset(data, 0xa5, sizeof(data)); memcpy(before, data, sizeof(data));
	CHECK(cixvpu_buffer_bitstream(slot, data, 40, &length) == EINVAL);
	CHECK(length == 99 && memcmp(data, before, sizeof(data)) == 0);
}

static void
encode_tests(void)
{
	uint8_t data[80], before[80];
	size_t length;
	struct cixvpu_buffer_nv12 f;
	struct cixvpu_buffer_bitstream b;

	memset(data, 0xa5, sizeof(data));
	CHECK(cixvpu_buffer_frame_nv12(&frame, data + 1, 72, &length) == 0);
	CHECK(length == 72 && memcmp(data + 1, frame_bytes, 72) == 0);
	CHECK(data[0] == 0xa5 && data[73] == 0xa5);
	memset(data, 0xa5, sizeof(data));
	CHECK(cixvpu_buffer_bitstream(&stream, data + 1, 40, &length) == 0);
	CHECK(length == 40 && memcmp(data + 1, stream_bytes, 40) == 0);
	CHECK(data[0] == 0xa5 && data[41] == 0xa5);
	for (size_t cap = 0; cap < 72; cap++) {
		memset(data, 0xa5, sizeof(data)); memcpy(before, data, sizeof(data));
		length = 99;
		CHECK(cixvpu_buffer_frame_nv12(&frame, data, cap, &length) == EMSGSIZE);
		CHECK(length == 99 && memcmp(data, before, sizeof(data)) == 0);
		if (cap < 40) {
			CHECK(cixvpu_buffer_bitstream(&stream, data, cap, &length) == EMSGSIZE);
			CHECK(length == 99 && memcmp(data, before, sizeof(data)) == 0);
		}
	}
	f = frame; f.eos = true;
	CHECK(cixvpu_buffer_frame_nv12(&f, data, 73, &length) == 0);
	CHECK(data[16] == 4 && data[17] == 4 && data[18] == 1);
	CHECK(data[72] == 0xa5);
	f = frame; f.force_idr = false;
	CHECK(cixvpu_buffer_frame_nv12(&f, data, 72, &length) == 0);
	CHECK(data[16] == 4 && data[17] == 0 && data[18] == 0);
	const uint32_t dimensions[] = {0, 1, 3, 65535, 65536, UINT32_MAX};
	for (size_t i = 0; i < sizeof(dimensions) / sizeof(dimensions[0]); i++) {
		f = frame; f.width = dimensions[i];
		bad_frame(&f);
		f = frame; f.height = dimensions[i];
		bad_frame(&f);
	}
	const uint32_t strides[] = {0, 1, 0x80000000U, UINT32_MAX};
	for (size_t i = 0; i < sizeof(strides) / sizeof(strides[0]); i++) {
		f = frame; f.stride_y = strides[i];
		bad_frame(&f);
		f = frame; f.stride_uv = strides[i];
		bad_frame(&f);
	}
	f = frame; f.y.bytes = 15;
	bad_frame(&f);
	f = frame; f.uv.bytes = 11;
	bad_frame(&f);
	f = frame; f.y.bytes = 0x1002;
	CHECK(cixvpu_buffer_frame_nv12(&f, data, 72, &length) == 0);
	f.y.bytes++;
	bad_frame(&f);
	f = frame; f.y.address = 0xeffffff0;
	CHECK(cixvpu_buffer_frame_nv12(&f, data, 72, &length) == 0);
	f.y.address++;
	bad_frame(&f);
	f = frame; f.uv.address = UINT32_MAX - 3;
	bad_frame(&f);
	f = frame; f.y.address = 0x6fffffff;
	bad_frame(&f);
	f = frame; f.y.bytes = 0;
	bad_frame(&f);
	f = frame; f.height = 8192; f.stride_y = 2; f.y.bytes = 16384;
	f.uv.address = 0x71000001; f.stride_uv = 0x100000;
	bad_frame(&f); /* UV multiplication would wrap exactly to zero in uint32. */
	f = frame; f.height = 4096; f.stride_y = 0x100000;
	f.uv.bytes = 4096; f.stride_uv = 2;
	bad_frame(&f); /* Independent Y multiplication overflow. */
	f = frame; f.width = 65534; f.height = 2;
	f.stride_y = f.stride_uv = 65534; f.y.bytes = 131068;
	f.uv.address = 0x71000003; f.uv.bytes = 65534;
	CHECK(cixvpu_buffer_frame_nv12(&f, data, 72, &length) == 0);
	CHECK(data[22] == 254 && data[23] == 255 && data[64] == 254 && data[65] == 255);
	b = stream; b.data.address = 0x6fffffff; b.data.bytes = 1;
	CHECK(cixvpu_buffer_bitstream(&b, data, 40, &length) == 0);
	b.data.bytes++;
	bad_stream(&b);
	b = stream; b.data.address = 0x1fffffff;
	bad_stream(&b);
	b = stream; b.data.bytes = 0;
	bad_stream(&b);
}

static void
return_tests(void)
{
	uint8_t payload[74];
	struct cixvpu_buffer_frame_result f, fbefore;
	struct cixvpu_buffer_bitstream_result b, bbefore;

	memcpy(payload + 1, frame_bytes, 72);
	CHECK(cixvpu_buffer_frame_return(payload + 1, 72, &frame, &f) == 0);
	CHECK(f.flags == 0x404 && !f.rejected && !f.corrupt);
	payload[17] = 0; payload[18] = 0x30;
	CHECK(cixvpu_buffer_frame_return(payload + 1, 72, &frame, &f) == 0);
	CHECK(f.flags == 0x3000 && f.rejected && f.corrupt);
	for (size_t i = 0; i < 72; i++) {
		if (i >= 16 && i < 20) continue; /* Raw flags are observations. */
		memcpy(payload, frame_bytes, 72); payload[i] ^= 1;
		memset(&f, 0xa5, sizeof(f)); memcpy(&fbefore, &f, sizeof(f));
		CHECK(cixvpu_buffer_frame_return(payload, 72, &frame, &f) == EPROTO);
		CHECK(memcmp(&f, &fbefore, sizeof(f)) == 0);
	}
	memcpy(payload + 1, returned, 40);
	CHECK(cixvpu_buffer_bitstream_return(payload + 1, 40, &stream, &b) == 0);
	CHECK(b.offset == 256 && b.filled == 515 && b.flags == 0x80000080U);
	CHECK(b.frame_type == 6 && b.transform == 7 && b.remaining_kb == 0x3344);
	CHECK((b.flags & 0x10) == 0); /* CODECCONFIG is not ENDOFFRAME. */
	const size_t identity[] = {0,7,8,15,20,23,32,35};
	for (size_t i = 0; i < sizeof(identity) / sizeof(identity[0]); i++) {
		memcpy(payload, returned, 40); payload[identity[i]] ^= 1;
		memset(&b, 0xa5, sizeof(b)); memcpy(&bbefore, &b, sizeof(b));
		CHECK(cixvpu_buffer_bitstream_return(payload, 40, &stream, &b) == EPROTO);
		CHECK(memcmp(&b, &bbefore, sizeof(b)) == 0);
	}
	memcpy(payload, returned, 40);
	memcpy(payload + 24, (uint8_t[]){3,2,1,0}, 4); memset(payload + 28, 0, 4);
	CHECK(cixvpu_buffer_bitstream_return(payload, 40, &stream, &b) == 0);
	CHECK(b.offset == 0x10203 && b.filled == 0);
	payload[28] = 1;
	memset(&b, 0xa5, sizeof(b)); memcpy(&bbefore, &b, sizeof(b));
	CHECK(cixvpu_buffer_bitstream_return(payload, 40, &stream, &b) == EPROTO);
	CHECK(memcmp(&b, &bbefore, sizeof(b)) == 0);
	memcpy(payload, returned, 40); memset(payload + 24, 255, 8);
	CHECK(cixvpu_buffer_bitstream_return(payload, 40, &stream, &b) == EPROTO);
	CHECK(memcmp(&b, &bbefore, sizeof(b)) == 0);
	memcpy(payload, returned, 40);
	memcpy(payload + 28, (uint8_t[]){0,255,255,255}, 4);
	CHECK(cixvpu_buffer_bitstream_return(payload, 40, &stream, &b) == EPROTO);
	CHECK(memcmp(&b, &bbefore, sizeof(b)) == 0);
	for (size_t length = 0; length <= 73; length++) {
		memset(&f, 0xa5, sizeof(f)); memcpy(&fbefore, &f, sizeof(f));
		if (length != 72) {
			CHECK(cixvpu_buffer_frame_return(payload, length, &frame, &f) == EPROTO);
			CHECK(memcmp(&f, &fbefore, sizeof(f)) == 0);
		}
		memset(&b, 0xa5, sizeof(b)); memcpy(&bbefore, &b, sizeof(b));
		if (length != 40) {
			CHECK(cixvpu_buffer_bitstream_return(payload, length, &stream, &b) == EPROTO);
			CHECK(memcmp(&b, &bbefore, sizeof(b)) == 0);
		}
	}
}

static void
alias_tests(void)
{
	_Alignas(size_t) uint8_t data[80];
	struct cixvpu_buffer_nv12 f = frame;
	struct cixvpu_buffer_bitstream b = stream;
	struct cixvpu_buffer_frame_result fr;
	struct cixvpu_buffer_bitstream_result br;
	uint8_t fr_before[sizeof(fr)], br_before[sizeof(br)];
	_Alignas(size_t) uint8_t scalar[2 * sizeof(size_t)];
	_Alignas(struct cixvpu_buffer_frame_result) uint8_t bad_frame_out[16];
	_Alignas(struct cixvpu_buffer_bitstream_result) uint8_t bad_stream_out[32];
	struct {
		uint8_t prefix[128];
		struct cixvpu_buffer_nv12 slot;
	} whole;
	uint8_t whole_before[sizeof(whole)];
	struct { uint8_t prefix[128]; size_t length; } far;
	size_t length = 99;

	memset(data, 0xa5, sizeof(data));
	memset(&fr, 0xa5, sizeof(fr)); memcpy(fr_before, &fr, sizeof(fr));
	memset(&br, 0xa5, sizeof(br)); memcpy(br_before, &br, sizeof(br));
	CHECK(cixvpu_buffer_frame_nv12(NULL, data, 72, &length) == EINVAL);
	CHECK(cixvpu_buffer_frame_nv12(&f, NULL, 72, &length) == EINVAL);
	CHECK(cixvpu_buffer_frame_nv12(&f, data, 72, NULL) == EINVAL);
	CHECK(cixvpu_buffer_frame_nv12(&f, data, SIZE_MAX, &length) == EINVAL);
	CHECK(cixvpu_buffer_frame_nv12(&f, data, 72, (size_t *)(void *)(data + 1)) == EINVAL);
	memset(scalar, 0xa5, sizeof(scalar));
	CHECK(cixvpu_buffer_frame_nv12(&f, data, 72,
	    (size_t *)(void *)(scalar + 1)) == EINVAL);
	CHECK(scalar[1] == 0xa5);
	CHECK(cixvpu_buffer_frame_nv12(&f, data, 72, (size_t *)(void *)data) == EINVAL);
	CHECK(cixvpu_buffer_frame_nv12(&f, &f, sizeof(f), &length) == EINVAL);
	CHECK(cixvpu_buffer_frame_nv12(&f, data, 72, (size_t *)(void *)&f) == EINVAL);
	CHECK(cixvpu_buffer_bitstream(&b, &b, sizeof(b), &length) == EINVAL);
	CHECK(cixvpu_buffer_bitstream(&b, data, 40, (size_t *)(void *)&b) == EINVAL);
	CHECK(length == 99 && data[0] == 0xa5 && f.handle == frame.handle);
	memset(&whole, 0xa5, sizeof(whole)); whole.slot = frame;
	memcpy(whole_before, &whole, sizeof(whole));
	CHECK(cixvpu_buffer_frame_nv12(&whole.slot, &whole, sizeof(whole), &length) == EINVAL);
	CHECK(memcmp(&whole, whole_before, sizeof(whole)) == 0 && whole.prefix[0] == 0xa5);
	memset(&far, 0xa5, sizeof(far)); far.length = 99;
	CHECK(cixvpu_buffer_frame_nv12(&f, &far, sizeof(far), &far.length) == EINVAL);
	CHECK(far.length == 99 && far.prefix[0] == 0xa5);
	CHECK(cixvpu_buffer_frame_return(NULL, 72, &f, &fr) == EINVAL);
	CHECK(cixvpu_buffer_frame_return(NULL, 0, &f, &fr) == EPROTO);
	CHECK(cixvpu_buffer_frame_return(data, SIZE_MAX, &f, &fr) == EINVAL);
	CHECK(cixvpu_buffer_frame_return(data, 72, NULL, &fr) == EINVAL);
	CHECK(cixvpu_buffer_frame_return(data, 72, &f, NULL) == EINVAL);
	CHECK(cixvpu_buffer_frame_return(data, 72, &f,
	    (struct cixvpu_buffer_frame_result *)(void *)data) == EINVAL);
	CHECK(cixvpu_buffer_frame_return(data, 72, &f,
	    (struct cixvpu_buffer_frame_result *)(void *)&f) == EINVAL);
	CHECK(cixvpu_buffer_bitstream_return(data, 40, &b,
	    (struct cixvpu_buffer_bitstream_result *)(void *)&b) == EINVAL);
	CHECK(cixvpu_buffer_bitstream_return(data, 40, &b,
	    (struct cixvpu_buffer_bitstream_result *)(void *)(data + 1)) == EINVAL);
	memset(bad_frame_out, 0xa5, sizeof(bad_frame_out));
	memset(bad_stream_out, 0xa5, sizeof(bad_stream_out));
	CHECK(cixvpu_buffer_frame_return(frame_bytes, 72, &f,
	    (struct cixvpu_buffer_frame_result *)(void *)(bad_frame_out + 1)) == EINVAL);
	CHECK(cixvpu_buffer_bitstream_return(returned, 40, &b,
	    (struct cixvpu_buffer_bitstream_result *)(void *)(bad_stream_out + 1)) == EINVAL);
	CHECK(bad_frame_out[1] == 0xa5 && bad_stream_out[1] == 0xa5);
	CHECK(cixvpu_buffer_frame_nv12(&f,
	    (void *)(uintptr_t)(UINTPTR_MAX - 4), 72, &length) == EINVAL);
	CHECK(cixvpu_buffer_bitstream_return(
	    (void *)(uintptr_t)(UINTPTR_MAX - 4), 40, &b, &br) == EINVAL);
	f.y.bytes = 0;
	CHECK(cixvpu_buffer_frame_return(frame_bytes, 72, &f, &fr) == EINVAL);
	b.data.bytes = 0;
	CHECK(cixvpu_buffer_bitstream_return(returned, 40, &b, &br) == EINVAL);
	CHECK(memcmp(&fr, fr_before, sizeof(fr)) == 0);
	CHECK(memcmp(&br, br_before, sizeof(br)) == 0);
}

static void
ring_tests(void)
{
	uint8_t ring[CIXVPU_RING_BYTES], wire[72], payload[72];
	struct cixvpu_ring_checksum sum = {0}, after;
	struct cixvpu_buffer_frame_result result;
	uint16_t next, code;
	size_t length;

	CHECK(cixvpu_buffer_frame_nv12(&frame, wire, 72, &length) == 0);
	CHECK(cixvpu_ring_write_sum(ring, sizeof(ring), 1018, 1018,
	    CIXVPU_BUFFER_FRAME, wire, length, &sum, &next, &after) == 0);
	CHECK(next == 18 && ring[4072] == 0xb9 && ring[4073] == 0x0b);
	CHECK(memcmp(ring, frame_bytes, 72) == 0);
	CHECK(cixvpu_buffer_bitstream(&stream, wire, 40, &length) == 0);
	CHECK(cixvpu_ring_write_sum(ring, sizeof(ring), 0, 0,
	    CIXVPU_BUFFER_BITSTREAM, wire, length, &sum, &next, &after) == 0);
	CHECK(next == 12 && memcmp(ring + 8, stream_bytes, 40) == 0);
	memcpy(ring, (uint8_t[]){0xb9,0x0b,72,0}, 4);
	memcpy(ring + 4, frame_bytes, 72);
	CHECK(cixvpu_ring_read(ring, sizeof(ring), 0, 19, &code, payload, 72,
	    &length, &next) == 0);
	CHECK(code == 3001 && length == 72 && next == 19);
	CHECK(cixvpu_buffer_frame_return(payload, length, &frame, &result) == 0);
	payload[0] ^= 1; next = 99;
	CHECK(cixvpu_buffer_frame_return(payload, length, &frame, &result) == EPROTO);
	CHECK(next == 99); /* Pure decoder has no caller cursor. */
}

int
main(void)
{
	encode_tests();
	return_tests();
	alias_tests();
	ring_tests();
	printf("cixvpu buffer: %u checks, 0 failures\n", checks);
	return 0;
}
