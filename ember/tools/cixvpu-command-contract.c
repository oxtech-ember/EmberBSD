/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. AI-assisted original tests. */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cixvpu_command.h"
#include "cixvpu_ring.h"

static unsigned int checks;
#define CHECK(c) do { checks++; if (!(c)) { \
	fprintf(stderr, "FAIL line %u: %s\n", (unsigned int)__LINE__, #c); \
	exit(1); } } while (0)

/* Independent literal payload/wire bytes, not generated expected values. */
static const uint8_t job[] = {1, 0, 1, 0, 0, 0, 0, 0};
static const uint8_t job_wire[] = {
	0xf1, 3, 8, 0, 0xf2, 3, 9, 0, 1, 0, 1, 0, 0, 0, 0, 0
};
static const uint8_t nalu_wire[] = {
	0xf2, 3, 8, 0, 0xf4, 3, 8, 0, 1, 0, 0, 0, 1, 0, 0, 0
};

/* The absent-feature boundary yields a runtime RED against the old header. */
#ifndef CIXVPU_COMMAND_HEVC_SETUP_API
static int
absent_setup(uint32_t value, void *out, size_t capacity, size_t *length)
{
	(void)value; (void)out; (void)capacity; (void)length;
	return ENOTSUP;
}
#define cixvpu_command_hevc_main absent_setup
#define cixvpu_command_input_frame_rate absent_setup
#define cixvpu_command_input_rate_control_off(out, cap, len) \
	absent_setup(0, out, cap, len)
#endif

/* Payload and framing vectors are literal protocol facts, not struct copies. */
static const struct {
	uint16_t code;
	size_t bytes;
	uint8_t payload[16], wire[24];
} setup[] = {
	{1010, 8, {3,0,0,0,1,0,1,0},
	 {0xf2,3,8,0,0xf6,3,9,0,3,0,0,0,1,0,1,0}},
	{3003, 8, {9,0,0,0,0,0,0x1e,0},
	 {0xbb,0xb,8,0,0xc4,0xb,0x26,0,9,0,0,0,0,0,0x1e,0}},
	{3003,16, {10,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
	 {0xbb,0xb,16,0,0xc5,0xb,16,0,10,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}},
	{1010, 8, {16,0,0,0,0,0,0,0},
	 {0xf2,3,8,0,2,4,8,0,16,0,0,0,0,0,0,0}},
	{1010, 8, {56,0,0,0,26,0,0,0},
	 {0xf2,3,8,0,0x44,4,8,0,56,0,0,0,26,0,0,0}}
};

static int
setup_write(unsigned int kind, void *out, size_t capacity, size_t *length)
{
	switch (kind) {
	case 0: return cixvpu_command_hevc_main(1, out, capacity, length);
	case 1: return cixvpu_command_input_frame_rate(0x001e0000, out, capacity, length);
	case 2: return cixvpu_command_input_rate_control_off(out, capacity, length);
	case 3: return cixvpu_command_option(16, 0, out, capacity, length);
	case 4: return cixvpu_command_option(56, 26, out, capacity, length);
	default: CHECK(false); return EINVAL;
	}
}

static void
setup_tests(void)
{
	_Alignas(size_t) uint8_t storage[64], before[64];
	size_t length = 99;
	const uint32_t levels[] = {1,3,5,7,9,11,13,15,17,19,21,23,25};
	const uint32_t bad_levels[] = {0,2,4,6,8,10,12,14,16,18,20,22,24,26,
	    27,31,255,65535,65536,UINT32_MAX};

	/* New scalar options must fail the old production allowlist causally. */
	CHECK(cixvpu_command_option(16, 0, storage, 8, &length) == 0);
	CHECK(cixvpu_command_option(56, 26, storage, 8, &length) == 0);
	for (unsigned int k = 0; k < sizeof(setup) / sizeof(setup[0]); k++) {
		memset(storage, 0xa5, sizeof(storage)); length = 99;
		CHECK(setup_write(k, storage + 1, setup[k].bytes + 2, &length) == 0);
		CHECK(length == setup[k].bytes);
		CHECK(memcmp(storage + 1, setup[k].payload, length) == 0);
		CHECK(storage[0] == 0xa5);
		for (size_t n = length + 1; n < sizeof(storage); n++)
			CHECK(storage[n] == 0xa5);
		for (size_t cap = 0; cap < setup[k].bytes; cap++) {
			memset(storage, 0xa5, sizeof(storage)); memcpy(before, storage, sizeof(before));
			length = 99;
			CHECK(setup_write(k, storage, cap, &length) == EMSGSIZE);
			CHECK(length == 99 && memcmp(storage, before, sizeof(storage)) == 0);
		}
		CHECK(setup_write(k, NULL, 16, &length) == EINVAL);
		CHECK(setup_write(k, storage, 16, NULL) == EINVAL);
		CHECK(setup_write(k, storage, SIZE_MAX, &length) == EINVAL);
		CHECK(setup_write(k, (void *)(uintptr_t)(UINTPTR_MAX - 7), 16, &length) == EINVAL);
		CHECK(setup_write(k, storage, 16,
		    (size_t *)(void *)(storage + 1)) == EINVAL);
		CHECK(setup_write(k, storage, 16,
		    (size_t *)(uintptr_t)(UINTPTR_MAX - sizeof(size_t) + 1)) == EINVAL);
		/* Reject overlap anywhere in declared capacity, even past payload. */
		CHECK(setup_write(k, storage, 32, (size_t *)(void *)(storage + 24)) == EINVAL);
		CHECK(memcmp(storage, before, sizeof(storage)) == 0 && length == 99);
	}
	for (size_t i = 0; i < sizeof(levels) / sizeof(levels[0]); i++) {
		CHECK(cixvpu_command_hevc_main(levels[i], storage, 8, &length) == 0);
		const uint8_t vector[] = {3,0,0,0,1,0,(uint8_t)levels[i],0};
		CHECK(length == 8 && memcmp(storage, vector, 8) == 0);
	}
	memset(storage, 0xa5, sizeof(storage)); memcpy(before, storage, sizeof(before)); length = 99;
	for (size_t i = 0; i < sizeof(bad_levels) / sizeof(bad_levels[0]); i++) {
		CHECK(cixvpu_command_hevc_main(bad_levels[i], storage, 8, &length) == EINVAL);
		CHECK(length == 99 && memcmp(storage, before, sizeof(storage)) == 0);
	}
	CHECK(cixvpu_command_option(56, 52, storage, 8, &length) == EINVAL);
	CHECK(cixvpu_command_option(56, UINT32_MAX, storage, 8, &length) == EINVAL);
	CHECK(cixvpu_command_input_frame_rate(0, storage, 8, &length) == EINVAL);
	CHECK(length == 99 && memcmp(storage, before, sizeof(storage)) == 0);
	CHECK(cixvpu_command_option(16, UINT32_MAX, storage, 8, &length) == 0);
	CHECK(memcmp(storage, (uint8_t[]){16,0,0,0,255,255,255,255}, 8) == 0);
	CHECK(cixvpu_command_option(56, 0, storage, 8, &length) == 0);
	CHECK(memcmp(storage, (uint8_t[]){56,0,0,0,0,0,0,0}, 8) == 0);
	CHECK(cixvpu_command_option(56, 51, storage, 8, &length) == 0);
	CHECK(memcmp(storage, (uint8_t[]){56,0,0,0,51,0,0,0}, 8) == 0);
	CHECK(cixvpu_command_input_frame_rate(0x1234abcd, storage, 8, &length) == 0);
	CHECK(memcmp(storage, (uint8_t[]){9,0,0,0,0xcd,0xab,0x34,0x12}, 8) == 0);
	CHECK(cixvpu_command_input_frame_rate(1, storage, 8, &length) == 0);
	CHECK(memcmp(storage, (uint8_t[]){9,0,0,0,1,0,0,0}, 8) == 0);
	CHECK(cixvpu_command_input_frame_rate(UINT32_MAX, storage, 8, &length) == 0);
	CHECK(memcmp(storage, (uint8_t[]){9,0,0,0,255,255,255,255}, 8) == 0);
}

static void
setup_ring_tests(void)
{
	uint8_t ring[CIXVPU_RING_BYTES], saved[CIXVPU_RING_BYTES];
	uint8_t payload[CIXVPU_COMMAND_SETUP_MAX_BYTES];
	struct cixvpu_ring_checksum sum = {0}, next_sum;
	uint16_t next;
	size_t length;

	for (unsigned int k = 0; k < sizeof(setup) / sizeof(setup[0]); k++) {
		uint16_t transport = k == 1 || k == 2 ?
		    CIXVPU_COMMAND_INPUT_PARAM : CIXVPU_COMMAND_SET_OPTION;

		CHECK(setup_write(k, payload, sizeof(payload), &length) == 0);
		CHECK(transport == setup[k].code);
		memset(ring, 0xa5, sizeof(ring));
		CHECK(cixvpu_ring_write_sum(ring, sizeof(ring), 1018, 1018,
		    transport, payload, length, &sum, &next, &next_sum) == 0);
		CHECK(memcmp(ring + 4072, setup[k].wire, 8) == 0);
		CHECK(memcmp(ring, setup[k].wire + 8, length) == 0);
		CHECK(next == (k == 2 ? 4 : 2));
		memcpy(saved, ring, sizeof(ring)); next = 99; next_sum.sum = 99;
		CHECK(cixvpu_ring_write_sum(ring, sizeof(ring), 1, 0,
		    transport, payload, length, &sum, &next, &next_sum) == EAGAIN);
		CHECK(next == 99 && next_sum.sum == 99 && memcmp(saved, ring, sizeof(ring)) == 0);
	}
}

static void
payload_tests(void)
{
	_Alignas(size_t) uint8_t storage[32];
	uint8_t before[32];
	size_t length;
	const uint32_t nalu[] = {1, 2, 4, 8, 16, 32};

	memset(storage, 0xa5, sizeof(storage));
	CHECK(cixvpu_command_job(1, 1, 0, storage + 1, 8, &length) == 0);
	CHECK(length == 8 && memcmp(storage + 1, job, 8) == 0);
	CHECK(storage[0] == 0xa5 && storage[9] == 0xa5);
	CHECK(cixvpu_command_job(65535, 0, 1, storage, 8, &length) == 0);
	CHECK(memcmp(storage, (uint8_t[]){255,255,0,0,1,0,0,0}, 8) == 0);
	CHECK(cixvpu_command_job(2, 65535, 0, storage, 8, &length) == 0);
	CHECK(memcmp(storage, (uint8_t[]){2,0,255,255,0,0,0,0}, 8) == 0);
	for (size_t cap = 0; cap < 8; cap++) {
		memset(storage, 0xa5, sizeof(storage));
		memcpy(before, storage, sizeof(before)); length = 99;
		CHECK(cixvpu_command_job(1, 1, 0, storage, cap, &length) == EMSGSIZE);
		CHECK(length == 99 && memcmp(storage, before, sizeof(storage)) == 0);
		CHECK(cixvpu_command_option(1, 1, storage, cap, &length) == EMSGSIZE);
		CHECK(length == 99 && memcmp(storage, before, sizeof(storage)) == 0);
	}
	for (size_t i = 0; i < sizeof(nalu) / sizeof(nalu[0]); i++) {
		const uint8_t expected[] = {1, 0, 0, 0, (uint8_t)nalu[i], 0, 0, 0};

		CHECK(cixvpu_command_option(1, nalu[i], storage + 1, 9, &length) == 0);
		CHECK(length == 8 && memcmp(storage + 1, expected, 8) == 0);
	}
	CHECK(cixvpu_command_option(17, 0, storage, 8, &length) == 0);
	CHECK(memcmp(storage, (uint8_t[]){17,0,0,0,0,0,0,0}, 8) == 0);
	CHECK(cixvpu_command_option(17, UINT32_MAX, storage, 8, &length) == 0);
	CHECK(memcmp(storage, (uint8_t[]){17,0,0,0,255,255,255,255}, 8) == 0);
	memset(storage, 0xa5, sizeof(storage)); memcpy(before, storage, sizeof(before));
	length = 99;
	CHECK(cixvpu_command_job(0, 1, 0, storage, 8, &length) == EINVAL);
	CHECK(cixvpu_command_job(65536, 1, 0, storage, 8, &length) == EINVAL);
	CHECK(cixvpu_command_job(1, 65536, 0, storage, 8, &length) == EINVAL);
	CHECK(cixvpu_command_job(1, 1, 2, storage, 8, &length) == EINVAL);
	CHECK(cixvpu_command_option(1, 0, storage, 8, &length) == EINVAL);
	CHECK(cixvpu_command_option(1, 3, storage, 8, &length) == EINVAL);
	CHECK(cixvpu_command_option(1, 64, storage, 8, &length) == EINVAL);
	CHECK(cixvpu_command_option(3, 1, storage, 8, &length) == ENOTSUP);
	CHECK(length == 99 && memcmp(storage, before, sizeof(storage)) == 0);
	CHECK(cixvpu_command_job(1, 1, 0, NULL, 8, &length) == EINVAL);
	CHECK(cixvpu_command_option(1, 1, storage, 8, NULL) == EINVAL);
	CHECK(cixvpu_command_job(1, 1, 0, storage, SIZE_MAX, &length) == EINVAL);
	CHECK(cixvpu_command_job(1, 1, 0,
	    (void *)(uintptr_t)(UINTPTR_MAX - 4), 8, &length) == EINVAL);
	/* Scalar is valid/aligned but overlaps the actual output region. */
	union { size_t scalar; uint8_t bytes[32]; } alias;
	memset(&alias, 0xa5, sizeof(alias));
	CHECK(cixvpu_command_job(1, 1, 0, alias.bytes, 8, &alias.scalar) == EINVAL);
	CHECK(cixvpu_command_option(1, 1, alias.bytes, 8, &alias.scalar) == EINVAL);
	CHECK(alias.bytes[0] == 0xa5);
	CHECK(cixvpu_command_job(1, 1, 0, storage, 8,
	    (size_t *)(void *)(storage + 1)) == EINVAL);
}

static void
response_tests(void)
{
	struct cixvpu_command_response out, before;
	uint8_t data[134];
	const uint16_t codes[] = {2003, 2004, 2010, 2011, 2017};

	memset(&out, 0xa5, sizeof(out));
	CHECK(cixvpu_command_response(2003, NULL, 0, &out) == 0);
	CHECK(out.kind == CIXVPU_COMMAND_CONFIRMED && !out.index_valid && out.index == 0);
	memset(data, 0, sizeof(data)); data[1] = 17;
	CHECK(cixvpu_command_response(2003, data + 1, 4, &out) == 0);
	CHECK(out.kind == CIXVPU_COMMAND_CONFIRMED && out.index_valid && out.index == 17);
	data[1] = 2;
	CHECK(cixvpu_command_response(2011, data + 1, 4, &out) == 0);
	CHECK(out.kind == CIXVPU_COMMAND_STATE && out.state == 2);
	data[1] = 0;
	CHECK(cixvpu_command_response(2011, data + 1, 4, &out) == 0);
	CHECK(out.state == 0);
	memset(data + 1, 255, 132);
	CHECK(cixvpu_command_response(2010, data + 1, 132, &out) == 0);
	CHECK(out.kind == CIXVPU_COMMAND_FATAL && out.error == UINT32_MAX);
	CHECK(memcmp(out.text, data + 5, 128) == 0 && !out.index_valid);
	CHECK(cixvpu_command_response(2017, data + 1, 132, &out) == 0);
	CHECK(out.kind == CIXVPU_COMMAND_OPTION_FAILED && out.index == UINT32_MAX);
	CHECK(out.index_valid && memcmp(out.text, data + 5, 128) == 0);
	CHECK(cixvpu_command_response(2004, data + 1, 4, &out) == 0);
	CHECK(out.kind == CIXVPU_COMMAND_JOB_DEQUEUED && out.valid_job == UINT32_MAX);
	for (size_t c = 0; c < sizeof(codes) / sizeof(codes[0]); c++) {
		for (size_t length = 0; length <= 133; length++) {
			bool valid = codes[c] == 2003 ? (length == 0 || length == 4) :
			    (codes[c] == 2010 || codes[c] == 2017 ? length == 132 : length == 4);
			if (valid) continue;
			memset(&out, 0xa5, sizeof(out)); memcpy(&before, &out, sizeof(out));
			CHECK(cixvpu_command_response(codes[c], data, length, &out) == EPROTO);
			CHECK(memcmp(&out, &before, sizeof(out)) == 0);
		}
	}
	memset(&out, 0xa5, sizeof(out)); memcpy(&before, &out, sizeof(out));
	memset(data, 0, sizeof(data)); data[0] = 1;
	CHECK(cixvpu_command_response(2011, data, 4, &out) == EPROTO);
	CHECK(cixvpu_command_response(65535, data, 4, &out) == ENOTSUP);
	CHECK(cixvpu_command_response(2003, NULL, 4, &out) == EINVAL);
	CHECK(cixvpu_command_response(2003, data, 4, NULL) == EINVAL);
	CHECK(cixvpu_command_response(2003, data, SIZE_MAX, &out) == EINVAL);
	CHECK(cixvpu_command_response(2003,
	    (const void *)(uintptr_t)(UINTPTR_MAX - 1), 4, &out) == EINVAL);
	CHECK(cixvpu_command_response(2003, &out, 4, &out) == EINVAL);
	CHECK(memcmp(&out, &before, sizeof(out)) == 0);
	_Alignas(struct cixvpu_command_response) uint8_t bad[256];
	CHECK(cixvpu_command_response(2003, NULL, 0,
	    (struct cixvpu_command_response *)(void *)(bad + 1)) == EINVAL);
}

static void
ring_tests(void)
{
	uint8_t ring[CIXVPU_RING_BYTES], before[CIXVPU_RING_BYTES], payload[8];
	struct cixvpu_ring_checksum sum = {0}, next_sum;
	uint16_t next, code, consumed;
	size_t length;
	const uint8_t reply[] = {0xd3,7,4,0,1,0,0,0};
	struct cixvpu_command_response decoded;
	const struct {
		uint16_t code;
		uint8_t wire[8];
	} empty[] = {
		{CIXVPU_COMMAND_GO, {0xe9,3,0,0,0xe9,3,0,0}},
		{CIXVPU_COMMAND_STOP, {0xea,3,0,0,0xea,3,0,0}},
		{CIXVPU_COMMAND_INPUT_FLUSH, {0xeb,3,0,0,0xeb,3,0,0}},
		{CIXVPU_COMMAND_OUTPUT_FLUSH, {0xec,3,0,0,0xec,3,0,0}},
		{CIXVPU_COMMAND_PING, {0xee,3,0,0,0xee,3,0,0}},
		{CIXVPU_COMMAND_IDLE_ACK, {0xf4,3,0,0,0xf4,3,0,0}}
	};

	for (size_t i = 0; i < sizeof(empty) / sizeof(empty[0]); i++) {
		CHECK(cixvpu_ring_write_sum(ring, sizeof(ring), 0, 0, empty[i].code,
		    NULL, 0, &sum, &next, &next_sum) == 0);
		CHECK(next == 2 && memcmp(ring, empty[i].wire, 8) == 0);
	}
	CHECK(cixvpu_command_job(1, 1, 0, payload, 8, &length) == 0);
	memset(ring, 0xa5, sizeof(ring));
	CHECK(cixvpu_ring_write_sum(ring, sizeof(ring), 0, 0, CIXVPU_COMMAND_JOB,
	    payload, length, &sum, &next, &next_sum) == 0);
	CHECK(next == 4 && memcmp(ring, job_wire, sizeof(job_wire)) == 0);
	CHECK(cixvpu_command_option(1, 1, payload, 8, &length) == 0);
	CHECK(cixvpu_ring_write_sum(ring, sizeof(ring), 0, 0, CIXVPU_COMMAND_SET_OPTION,
	    payload, length, &sum, &next, &next_sum) == 0);
	CHECK(memcmp(ring, nalu_wire, sizeof(nalu_wire)) == 0);
	CHECK(cixvpu_ring_write_sum(ring, sizeof(ring), 1018, 1018, CIXVPU_COMMAND_JOB,
	    job, 8, &sum, &next, &next_sum) == 0);
	CHECK(next == 2 && memcmp(ring + 4072, job_wire, 8) == 0);
	CHECK(memcmp(ring, job_wire + 8, 8) == 0);
	sum.sum = UINT32_MAX;
	CHECK(cixvpu_ring_write_sum(ring, sizeof(ring), 0, 0, CIXVPU_COMMAND_GO,
	    NULL, 0, &sum, &next, &next_sum) == 0);
	CHECK(next == 2 && next_sum.sum == 1000);
	CHECK(memcmp(ring, (uint8_t[]){0xe9,3,0,0,0xe8,3,0,0}, 8) == 0);
	memcpy(before, ring, sizeof(before)); next = 99; next_sum.sum = 99;
	CHECK(cixvpu_ring_write_sum(ring, sizeof(ring), 1, 0, CIXVPU_COMMAND_JOB,
	    job, 8, &sum, &next, &next_sum) == EAGAIN);
	CHECK(next == 99 && next_sum.sum == 99 && memcmp(ring, before, sizeof(ring)) == 0);
	memcpy(ring, reply, sizeof(reply));
	CHECK(cixvpu_ring_read(ring, sizeof(ring), 0, 2, &code, payload, 8,
	    &length, &consumed) == 0);
	CHECK(code == 2003 && length == 4 && consumed == 2);
	CHECK(cixvpu_command_response(code, payload, length, &decoded) == 0);
	CHECK(decoded.index_valid && decoded.index == 1);
	/* Decoding a bad reply cannot commit the caller-owned RX cursor. */
	consumed = 99; decoded.state = 77;
	CHECK(cixvpu_command_response(2011, (uint8_t[]){1,0,0,0}, 4, &decoded) == EPROTO);
	CHECK(consumed == 99 && decoded.state == 77);
}

int
main(void)
{
	setup_tests();
	setup_ring_tests();
	payload_tests();
	response_tests();
	ring_tests();
	printf("cixvpu command: %u checks, 0 failures\n", checks);
	return 0;
}
