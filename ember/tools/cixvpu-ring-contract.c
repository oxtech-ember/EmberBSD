/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. AI-assisted real-source tests. */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cixvpu_ring.h"

static unsigned int checks;

#define CHECK(expr) do { \
	checks++; \
	if (!(expr)) { \
		fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); \
		exit(EXIT_FAILURE); \
	} \
} while (0)

struct guarded_ring {
	uint8_t bytes[CIXVPU_RING_BYTES + 2];
};

static uint8_t *
reset_ring(struct guarded_ring *ring)
{

	memset(ring->bytes, 0xa5, sizeof(ring->bytes));
	return ring->bytes + 1;
}

static void
check_guards(const struct guarded_ring *ring)
{

	CHECK(ring->bytes[0] == 0xa5);
	CHECK(ring->bytes[sizeof(ring->bytes) - 1] == 0xa5);
}

static void
header(uint8_t *wire, uint16_t pos, uint16_t code, uint16_t size)
{
	size_t offset = (size_t)pos * 4;

	wire[offset] = (uint8_t)code;
	wire[offset + 1] = (uint8_t)(code >> 8);
	wire[offset + 2] = (uint8_t)size;
	wire[offset + 3] = (uint8_t)(size >> 8);
}

static void
read_error(const uint8_t *wire, size_t wire_size, uint16_t read,
    uint16_t write, size_t capacity, int expected, size_t required)
{
	uint8_t before[CIXVPU_RING_BYTES], output[CIXVPU_RING_BYTES + 2];
	uint8_t original[sizeof(output)];
	uint16_t code = 0xfeed, next = 0xbeef;
	size_t size = (size_t)0xabcdef;

	memcpy(before, wire, sizeof(before));
	memset(output, 0xc9, sizeof(output));
	memcpy(original, output, sizeof(original));
	CHECK(cixvpu_ring_read(wire, wire_size, read, write, &code,
	    output + 1, capacity, &size, &next) == expected);
	CHECK(code == 0xfeed && next == 0xbeef);
	CHECK(size == (expected == EMSGSIZE ? required : (size_t)0xabcdef));
	CHECK(memcmp(output, original, sizeof(output)) == 0);
	CHECK(memcmp(wire, before, sizeof(before)) == 0);
}

static void
write_error(uint8_t *wire, size_t wire_size, uint16_t read, uint16_t write,
    const void *payload, size_t size, int expected)
{
	uint8_t before[CIXVPU_RING_BYTES];
	uint16_t next = 0xbeef;

	memcpy(before, wire, sizeof(before));
	CHECK(cixvpu_ring_write(wire, wire_size, read, write, 0x1234,
	    payload, size, &next) == expected);
	CHECK(next == 0xbeef);
	CHECK(memcmp(wire, before, sizeof(before)) == 0);
}

static void
test_wire(void)
{
	static const uint8_t message[] = { 0, 0xfe, 0, 0x81, 0xaa };
	static const uint8_t expected[] = {
		0x34, 0x12, 5, 0, 0, 0xfe, 0, 0x81, 0xaa, 0, 0, 0
	};
	struct guarded_ring ring;
	uint8_t output[sizeof(message) + 2], *wire = reset_ring(&ring);
	uint16_t next = 0xbeef, code = 0xfeed;
	size_t size = (size_t)0xabcdef;

	CHECK(cixvpu_ring_write(wire, CIXVPU_RING_BYTES, 0, 0, 0x1234,
	    message, sizeof(message), &next) == 0);
	CHECK(next == 3);
	CHECK(memcmp(wire, expected, sizeof(expected)) == 0);
	CHECK(wire[sizeof(expected)] == 0xa5);
	read_error(wire, CIXVPU_RING_BYTES, 0, next, 0, EMSGSIZE, 5);
	read_error(wire, CIXVPU_RING_BYTES, 0, next, 4, EMSGSIZE, 5);
	CHECK(cixvpu_ring_read(wire, CIXVPU_RING_BYTES, 0, next, &code,
	    NULL, 0, &size, &next) == EMSGSIZE);
	CHECK(size == 5 && code == 0xfeed && next == 3);
	memset(output, 0xc9, sizeof(output));
	CHECK(cixvpu_ring_read(wire, CIXVPU_RING_BYTES, 0, 3, &code,
	    output + 1, sizeof(message), &size, &next) == 0);
	CHECK(code == 0x1234 && size == sizeof(message) && next == 3);
	CHECK(memcmp(output + 1, message, sizeof(message)) == 0);
	CHECK(output[0] == 0xc9 && output[sizeof(output) - 1] == 0xc9);
	CHECK(memcmp(wire, expected, sizeof(expected)) == 0);
	check_guards(&ring);
}

static void
test_empty_full(void)
{
	struct guarded_ring ring;
	uint8_t *wire = reset_ring(&ring), output[4] = { 1, 2, 3, 4 };
	uint16_t code = 0xfeed, next = 0xbeef;
	size_t size = (size_t)0xabcdef;

	read_error(wire, CIXVPU_RING_BYTES, 0, 0, 4, EAGAIN, 0);
	read_error(wire, CIXVPU_RING_BYTES, 1019, 1019, 4, EAGAIN, 0);
	write_error(wire, CIXVPU_RING_BYTES, 1, 0, NULL, 0, EAGAIN);
	write_error(wire, CIXVPU_RING_BYTES, 0, 1019, NULL, 0, EAGAIN);
	write_error(wire, CIXVPU_RING_BYTES, 2, 0, output, 1, EAGAIN);
	CHECK(cixvpu_ring_write(wire, CIXVPU_RING_BYTES, 2, 0, 0,
	    NULL, 0, &next) == 0);
	CHECK(next == 1);
	CHECK(cixvpu_ring_read(wire, CIXVPU_RING_BYTES, 0, 1, &code,
	    NULL, 0, &size, &next) == 0);
	CHECK(code == 0 && size == 0 && next == 1);
	CHECK(cixvpu_ring_write(wire, CIXVPU_RING_BYTES, 1019, 1019,
	    0xffff, NULL, 0, &next) == 0);
	CHECK(next == 0);
	CHECK(cixvpu_ring_read(wire, CIXVPU_RING_BYTES, 1019, 0,
	    &code, output, sizeof(output), &size, &next) == 0);
	CHECK(next == 0 && code == 0xffff && size == 0);
	CHECK(output[0] == 1 && output[3] == 4);
	check_guards(&ring);
}

static void
test_wrap_all_positions(void)
{
	struct guarded_ring ring;
	uint8_t message[CIXVPU_RING_MAX_PAYLOAD], output[sizeof(message) + 2];
	uint8_t *wire;
	uint16_t pos, next, code;
	size_t size, i, padded, offset, j;

	for (i = 0; i < sizeof(message); i++)
		message[i] = (uint8_t)(i * 37);
	for (pos = 0; pos < CIXVPU_RING_WORDS; pos++) {
		uint16_t expected;

		wire = reset_ring(&ring);
		/* Include the maximum at every possible word alignment. */
		size = CIXVPU_RING_MAX_PAYLOAD;
		CHECK(cixvpu_ring_write(wire, CIXVPU_RING_BYTES, pos, pos,
		    0xabcd, message, size, &next) == 0);
		expected = (uint16_t)((pos + 1019U) % CIXVPU_RING_WORDS);
		CHECK(next == expected);
		write_error(wire, CIXVPU_RING_BYTES, pos, next, NULL, 0,
		    EAGAIN);
		memset(output, 0xc9, sizeof(output));
		CHECK(cixvpu_ring_read(wire, CIXVPU_RING_BYTES, pos, next,
		    &code, output + 1, sizeof(message), &size, &next) == 0);
		CHECK(next == expected && code == 0xabcd);
		CHECK(size == sizeof(message));
		CHECK(memcmp(message, output + 1, size) == 0);
		CHECK(output[0] == 0xc9 && output[size + 1] == 0xc9);
		check_guards(&ring);
		/* Exercise payload padding at both sides of the ring boundary. */
		for (j = 1; j <= 7; j++) {
			wire = reset_ring(&ring);
			CHECK(cixvpu_ring_write(wire, CIXVPU_RING_BYTES, pos,
			    pos, 0x1234, message, j, &next) == 0);
			padded = (j + 3) / 4 * 4;
			offset = ((size_t)pos * 4 + 4) % CIXVPU_RING_BYTES;
			for (i = 0; i < padded; i++)
				CHECK(wire[(offset + i) % CIXVPU_RING_BYTES] ==
				    (i < j ? message[i] : 0));
			memset(output, 0xc9, sizeof(output));
			CHECK(cixvpu_ring_read(wire, CIXVPU_RING_BYTES, pos,
			    next, &code, output + 1, sizeof(message), &size,
			    &next) == 0);
			CHECK(code == 0x1234 && size == j);
			CHECK(memcmp(message, output + 1, size) == 0);
			CHECK(output[0] == 0xc9 && output[size + 1] == 0xc9);
			check_guards(&ring);
		}
	}
}

static void
test_malformed(void)
{
	struct guarded_ring ring;
	uint8_t *wire = reset_ring(&ring);
	uint16_t pos;

	for (pos = 0; pos < CIXVPU_RING_WORDS; pos++) {
		header(wire, pos, 0x3456, 4073);
		read_error(wire, CIXVPU_RING_BYTES, pos,
		    (uint16_t)((pos + 1U) % CIXVPU_RING_WORDS), 0,
		    EPROTO, 0);
		header(wire, pos, 0x3456, 0xffff);
		read_error(wire, CIXVPU_RING_BYTES, pos,
		    (uint16_t)((pos + 1U) % CIXVPU_RING_WORDS), 4072,
		    EPROTO, 0);
		header(wire, pos, 0x3456, 1);
		read_error(wire, CIXVPU_RING_BYTES, pos,
		    (uint16_t)((pos + 1U) % CIXVPU_RING_WORDS), 0,
		    EPROTO, 0);
		header(wire, pos, 0x3456, 4072);
		read_error(wire, CIXVPU_RING_BYTES, pos,
		    (uint16_t)((pos + 1018U) % CIXVPU_RING_WORDS), 4072,
		    EPROTO, 0);
	}
	check_guards(&ring);
}

static void
test_invalid(void)
{
	struct guarded_ring ring;
	uint8_t *wire = reset_ring(&ring), before[CIXVPU_RING_BYTES];
	uint8_t payload[16] = { 0 }, output[16], original[16];
	uint16_t code = 0xfeed, next = 0xbeef;
	size_t size = (size_t)0xabcdef;

	memset(output, 0xc9, sizeof(output));
	memcpy(original, output, sizeof(original));
	memcpy(before, wire, sizeof(before));
	read_error(wire, 0, 0, 1, 16, EINVAL, 0);
	read_error(wire, CIXVPU_RING_BYTES - 1, 0, 1, 16, EINVAL, 0);
	read_error(wire, CIXVPU_RING_BYTES + 1, 0, 1, 16, EINVAL, 0);
	read_error(wire, CIXVPU_RING_BYTES, 1020, 1, 16, EINVAL, 0);
	read_error(wire, CIXVPU_RING_BYTES, 0, 1020, 16, EINVAL, 0);
	read_error(wire, CIXVPU_RING_BYTES, 0xffff, 1, 16, EINVAL, 0);
	read_error(wire, CIXVPU_RING_BYTES, 0, 0xffff, 16, EINVAL, 0);
	write_error(wire, 0, 0, 0, payload, 1, EINVAL);
	write_error(wire, CIXVPU_RING_BYTES - 1, 0, 0, payload, 1, EINVAL);
	write_error(wire, CIXVPU_RING_BYTES + 1, 0, 0, payload, 1, EINVAL);
	write_error(wire, CIXVPU_RING_BYTES, 1020, 0, payload, 1, EINVAL);
	write_error(wire, CIXVPU_RING_BYTES, 0, 1020, payload, 1, EINVAL);
	write_error(wire, CIXVPU_RING_BYTES, 0xffff, 0, payload, 1, EINVAL);
	write_error(wire, CIXVPU_RING_BYTES, 0, 0xffff, payload, 1, EINVAL);
	write_error(wire, CIXVPU_RING_BYTES, 0, 0, payload, 4073, EINVAL);
	write_error(wire, CIXVPU_RING_BYTES, 0, 0, payload, SIZE_MAX, EINVAL);
	write_error(wire, CIXVPU_RING_BYTES, 0, 0, NULL, 1, EINVAL);
	write_error(wire, CIXVPU_RING_BYTES, 0, 0, wire + 10, 1, EINVAL);
	CHECK(cixvpu_ring_read(NULL, CIXVPU_RING_BYTES, 0, 1, &code,
	    output, sizeof(output), &size, &next) == EINVAL);
	CHECK(cixvpu_ring_write(NULL, CIXVPU_RING_BYTES, 0, 0, 0,
	    payload, 1, &next) == EINVAL);
	CHECK(cixvpu_ring_write(wire, CIXVPU_RING_BYTES, 0, 0, 0,
	    payload, 1, NULL) == EINVAL);
	CHECK(cixvpu_ring_write(wire, CIXVPU_RING_BYTES, 0, 0, 0,
	    payload, 1, (uint16_t *)(void *)(wire + 3)) == EINVAL);
	CHECK(cixvpu_ring_write(wire, CIXVPU_RING_BYTES, 0, 0, 0,
	    payload, 1, (uint16_t *)(void *)payload) == EINVAL);
	CHECK(cixvpu_ring_read(wire, CIXVPU_RING_BYTES, 0, 1, NULL,
	    output, sizeof(output), &size, &next) == EINVAL);
	CHECK(cixvpu_ring_read(wire, CIXVPU_RING_BYTES, 0, 1, &code,
	    output, sizeof(output), NULL, &next) == EINVAL);
	CHECK(cixvpu_ring_read(wire, CIXVPU_RING_BYTES, 0, 1, &code,
	    output, sizeof(output), &size, NULL) == EINVAL);
	CHECK(cixvpu_ring_read(wire, CIXVPU_RING_BYTES, 0, 1, &code,
	    NULL, 1, &size, &next) == EINVAL);
	CHECK(cixvpu_ring_read(wire, CIXVPU_RING_BYTES, 0, 1, &code,
	    output, SIZE_MAX, &size, &next) == EINVAL);
	CHECK(cixvpu_ring_read(wire, CIXVPU_RING_BYTES, 0, 1, &code,
	    wire + 10, 16, &size, &next) == EINVAL);
	CHECK(cixvpu_ring_read(wire, CIXVPU_RING_BYTES, 0, 1, &code,
	    output, sizeof(output), &size, &code) == EINVAL);
	CHECK(cixvpu_ring_read(wire, CIXVPU_RING_BYTES, 0, 1, &code,
	    &next, sizeof(next), &size, &next) == EINVAL);
	CHECK(cixvpu_ring_read(wire, CIXVPU_RING_BYTES, 0, 1, &code,
	    output, sizeof(output), (size_t *)(void *)output, &next) ==
	    EINVAL);
	CHECK(code == 0xfeed && next == 0xbeef && size == (size_t)0xabcdef);
	CHECK(memcmp(output, original, sizeof(output)) == 0);
	CHECK(memcmp(wire, before, sizeof(before)) == 0);
	check_guards(&ring);
}

static void
test_sequences(void)
{
	struct guarded_ring ring;
	uint8_t *wire = reset_ring(&ring), input[101] = { 0 };
	uint8_t output[sizeof(input) + 2];
	uint16_t read = 1009, write = 1009, pending, code, next, sequence;
	size_t size, bytes, i;
	unsigned int batch;

	/* Delay consumption to exercise multiple committed messages per snapshot. */
	for (batch = 0; batch < 64; batch++) {
		for (sequence = 0; sequence < 32; sequence++) {
			bytes = (batch + sequence) % sizeof(input);
			for (i = 0; i < bytes; i++)
				input[i] = (uint8_t)(batch ^ sequence ^ i);
			CHECK(cixvpu_ring_write(wire, CIXVPU_RING_BYTES,
			    read, write, sequence, input, bytes, &pending) == 0);
			write = pending;
		}
		for (sequence = 0; sequence < 32; sequence++) {
			bytes = (batch + sequence) % sizeof(input);
			for (i = 0; i < bytes; i++)
				input[i] = (uint8_t)(batch ^ sequence ^ i);
			memset(output, 0xc9, sizeof(output));
			CHECK(cixvpu_ring_read(wire, CIXVPU_RING_BYTES,
			    read, write, &code, output + 1, sizeof(input),
			    &size, &next) == 0);
			CHECK(code == sequence && size == bytes);
			CHECK(memcmp(input, output + 1, size) == 0);
			CHECK(output[0] == 0xc9 && output[size + 1] == 0xc9);
			read = next;
		}
		CHECK(read == write);
		read_error(wire, CIXVPU_RING_BYTES, read, write,
		    sizeof(input), EAGAIN, 0);
	}
	check_guards(&ring);
}

static void
sum_write_error(uint8_t *wire, uint16_t read, uint16_t write,
    const void *payload, size_t size,
    const struct cixvpu_ring_checksum *state, int expected)
{
	uint8_t before[CIXVPU_RING_BYTES];
	struct cixvpu_ring_checksum original = *state, result, unchanged;
	uint16_t next = 0xbeef;

	memcpy(before, wire, sizeof(before));
	memset(&result, 0x96, sizeof(result));
	unchanged = result;
	CHECK(cixvpu_ring_write_sum(wire, CIXVPU_RING_BYTES, read, write,
	    1001, payload, size, state, &next, &result) == expected);
	CHECK(next == 0xbeef);
	CHECK(memcmp(wire, before, sizeof(before)) == 0);
	CHECK(memcmp(state, &original, sizeof(original)) == 0);
	CHECK(memcmp(&result, &unchanged, sizeof(result)) == 0);
}

static void
sum_read_error(const uint8_t *wire, uint16_t read, uint16_t write,
    size_t capacity, const struct cixvpu_ring_checksum *state,
    int expected, size_t required)
{
	uint8_t before[CIXVPU_RING_BYTES], output[CIXVPU_RING_BYTES + 2];
	uint8_t original_output[sizeof(output)];
	struct cixvpu_ring_checksum original = *state, result, unchanged;
	uint16_t code = 0xfeed, next = 0xbeef;
	size_t size = (size_t)0xabcdef;

	memcpy(before, wire, sizeof(before));
	memset(output, 0xc9, sizeof(output));
	memcpy(original_output, output, sizeof(output));
	memset(&result, 0x96, sizeof(result));
	unchanged = result;
	CHECK(cixvpu_ring_read_sum(wire, CIXVPU_RING_BYTES, read, write,
	    state, &code, output + 1, capacity, &size, &next, &result) ==
	    expected);
	CHECK(code == 0xfeed && next == 0xbeef);
	CHECK(size == (expected == EMSGSIZE ? required : (size_t)0xabcdef));
	CHECK(memcmp(output, original_output, sizeof(output)) == 0);
	CHECK(memcmp(wire, before, sizeof(before)) == 0);
	CHECK(memcmp(state, &original, sizeof(original)) == 0);
	CHECK(memcmp(&result, &unchanged, sizeof(result)) == 0);
}

/* Reference arithmetic uses complete LE words, independently of ring helpers. */
static uint32_t
reference_sum(uint32_t previous, uint16_t code, const uint8_t *payload,
    size_t size)
{
	size_t offset, i;

	previous += (uint32_t)code + (uint32_t)size * 65536U;
	for (offset = 0; offset < size; offset += 4) {
		uint32_t word = 0;

		for (i = 4; i > 0; i--) {
			word *= 256U;
			if (offset + i - 1 < size)
				word += payload[offset + i - 1];
		}
		previous += word;
	}
	return previous;
}

static uint16_t
firmware_frame(uint8_t *wire, uint16_t pos, uint16_t code,
    const uint8_t *payload, size_t size)
{
	size_t i, padded = (size + 3) / 4 * 4;
	size_t offset = ((size_t)pos * 4 + 4) % CIXVPU_RING_BYTES;

	header(wire, pos, code, (uint16_t)size);
	for (i = 0; i < padded; i++)
		wire[(offset + i) % CIXVPU_RING_BYTES] = i < size ? payload[i] : 0;
	return (uint16_t)((pos + 1 + padded / 4) % CIXVPU_RING_WORDS);
}

static void
test_sum_command_vectors(void)
{
	/* Protocol GO=1001; SET_OPTION=1010, STREAM_ESCAPING index=2,arg=1. */
	static const uint8_t go[] = { 0xe9, 3, 0, 0, 0xe9, 3, 0, 0 };
	static const uint8_t option[] = { 2, 0, 0, 0, 1, 0, 0, 0 };
	static const uint8_t option_frame[] = {
		0xf2, 3, 8, 0, 0xde, 7, 8, 0,
		2, 0, 0, 0, 1, 0, 0, 0
	};
	struct guarded_ring ring;
	struct cixvpu_ring_checksum state = { 0, { 0, 0, 0 } }, result;
	uint8_t *wire = reset_ring(&ring);
	uint16_t next = 0xbeef;

	CHECK(cixvpu_ring_write_sum(wire, CIXVPU_RING_BYTES, 0, 0, 1001,
	    NULL, 0, &state, &next, &result) == 0);
	CHECK(next == 2 && memcmp(wire, go, sizeof(go)) == 0);
	CHECK(result.sum == 1001 && result.reserved[0] == 0 &&
	    result.reserved[1] == 0 && result.reserved[2] == 1001);
	CHECK(state.sum == 0 && state.reserved[2] == 0);
	state = result;
	CHECK(cixvpu_ring_write_sum(wire, CIXVPU_RING_BYTES, 0, next, 1010,
	    option, sizeof(option), &state, &next, &result) == 0);
	CHECK(next == 6);
	CHECK(memcmp(wire + 8, option_frame, sizeof(option_frame)) == 0);
	CHECK(result.sum == 0x000807de && result.reserved[0] == 0 &&
	    result.reserved[1] == 1001 && result.reserved[2] == 0x000807de);
	state = result;
	CHECK(cixvpu_ring_write_sum(wire, CIXVPU_RING_BYTES, 0, next, 1001,
	    NULL, 0, &state, &next, &result) == 0);
	CHECK(result.sum == 0x00080bc7 && result.reserved[0] == 1001 &&
	    result.reserved[1] == 0x000807de &&
	    result.reserved[2] == 0x00080bc7);
	state.sum = UINT32_MAX;
	CHECK(cixvpu_ring_write_sum(wire, CIXVPU_RING_BYTES, 1019, 1019,
	    1001, NULL, 0, &state, &next, &result) == 0);
	CHECK(next == 1 && result.sum == 1000 && wire[0] == 0xe8 &&
	    wire[1] == 3 && wire[2] == 0 && wire[3] == 0);
	check_guards(&ring);
}

static void
test_sum_all_positions(void)
{
	struct guarded_ring ring;
	struct cixvpu_ring_checksum state = {
		UINT32_MAX - 10U, { 0x1234, 0x5678, 0xabcd }
	}, result;
	uint8_t payload[CIXVPU_RING_MAX_PAYLOAD], output[sizeof(payload) + 2];
	uint8_t *wire;
	uint16_t pos, next, code;
	size_t i, size, j, padded;

	for (i = 0; i < sizeof(payload); i++)
		payload[i] = (uint8_t)(i * 17);
	for (pos = 0; pos < CIXVPU_RING_WORDS; pos++) {
		uint32_t expected;
		size_t offset;

		wire = reset_ring(&ring);
		size = CIXVPU_RING_SUM_MAX_PAYLOAD;
		expected = reference_sum(state.sum, 1010, payload, size);
		CHECK(cixvpu_ring_write_sum(wire, CIXVPU_RING_BYTES, pos, pos,
		    1010, payload, size, &state, &next, &result) == 0);
		CHECK(next == (uint16_t)((pos + 1019U) % CIXVPU_RING_WORDS));
		CHECK(result.sum == expected && result.reserved[0] == 0x5678 &&
		    result.reserved[1] == 0xabcd && result.reserved[2] == expected);
		offset = ((size_t)pos * 4 + 4) % CIXVPU_RING_BYTES;
		for (i = 0; i < 4; i++)
			CHECK(wire[offset + i] == (uint8_t)(expected >> (i * 8)));
		sum_write_error(wire, pos, next, NULL, 0, &state, EAGAIN);
		sum_write_error(wire, pos, pos, payload, 4069, &state, EINVAL);
		check_guards(&ring);
		for (j = 1; j <= 7; j++) {
			wire = reset_ring(&ring);
			expected = reference_sum(state.sum, 1010, payload, j);
			CHECK(cixvpu_ring_write_sum(wire, CIXVPU_RING_BYTES,
			    pos, pos, 1010, payload, j, &state, &next,
			    &result) == 0);
			CHECK(result.sum == expected);
			offset = ((size_t)pos * 4 + 8) % CIXVPU_RING_BYTES;
			padded = (j + 3) / 4 * 4;
			for (i = 0; i < padded; i++)
				CHECK(wire[(offset + i) % CIXVPU_RING_BYTES] ==
				    (i < j ? payload[i] : 0));
			check_guards(&ring);
		}
		/* RX uses no inline checksum; its maximum is still 4072. */
		wire = reset_ring(&ring);
		next = firmware_frame(wire, pos, 2011, payload, sizeof(payload));
		expected = reference_sum(state.sum, 2011, payload, sizeof(payload));
		state.reserved[0] = expected;
		state.reserved[1] = 0x5678;
		state.reserved[2] = 0xabcd;
		memset(output, 0xc9, sizeof(output));
		CHECK(cixvpu_ring_read_sum(wire, CIXVPU_RING_BYTES, pos,
		    next, &state, &code, output + 1, sizeof(payload), &size,
		    &next, &result) == 0);
		CHECK(result.sum == expected && code == 2011 &&
		    size == sizeof(payload));
		CHECK(memcmp(output + 1, payload, sizeof(payload)) == 0);
		CHECK(output[0] == 0xc9 && output[sizeof(output) - 1] == 0xc9);
		check_guards(&ring);
		state.reserved[0] = 0x1234;
	}
}

static void
test_sum_receive_snapshot(void)
{
	/* Two firmware messages: PONG2009, STATE_CHANGE2011 with LE32 state=1. */
	static const uint8_t pong[] = { 0xd9, 7, 0, 0 };
	static const uint8_t state_change[] = { 0xdb, 7, 4, 0, 1, 0, 0, 0 };
	struct guarded_ring ring;
	struct cixvpu_ring_checksum state = {
		0, { 0x12345678, 2009, 0x00040fb5 }
	}, result;
	uint8_t *wire = reset_ring(&ring), output[8];
	uint16_t code, next;
	size_t size;
	unsigned int match;

	memcpy(wire + 1019 * 4, pong, sizeof(pong));
	memcpy(wire, state_change, sizeof(state_change));
	for (match = 0; match < 3; match++) {
		state.reserved[0] = 1;
		state.reserved[1] = 2;
		state.reserved[2] = 3;
		state.reserved[match] = 0x00040fb5;
		CHECK(cixvpu_ring_read_sum(wire, CIXVPU_RING_BYTES, 1019,
		    2, &state, &code, output, sizeof(output), &size, &next,
		    &result) == 0);
		CHECK(code == 2009 && size == 0 && next == 0 && result.sum == 2009);
		CHECK(memcmp(state.reserved, result.reserved,
		    sizeof(state.reserved)) == 0);
	}
	state = result;
	sum_read_error(wire, 0, 2, 0, &state, EMSGSIZE, 4);
	sum_read_error(wire, 0, 2, 3, &state, EMSGSIZE, 4);
	CHECK(cixvpu_ring_read_sum(wire, CIXVPU_RING_BYTES, 0, 2,
	    &state, &code, output, sizeof(output), &size, &next, &result) == 0);
	CHECK(code == 2011 && size == 4 && next == 2 &&
	    result.sum == 0x00040fb5 && output[0] == 1 && output[3] == 0);
	state.reserved[2]++;
	sum_read_error(wire, 0, 2, sizeof(output), &state, EPROTO, 0);
	state.reserved[2]--;
	wire[4] ^= 1;
	sum_read_error(wire, 0, 2, sizeof(output), &state, EPROTO, 0);
	wire[4] ^= 1;
	sum_read_error(wire, 0, 1, sizeof(output), &state, EPROTO, 0);
	sum_read_error(wire, 2, 2, sizeof(output), &state, EAGAIN, 0);
	state.sum = UINT32_MAX;
	state.reserved[0] = 2008;
	CHECK(cixvpu_ring_read_sum(wire, CIXVPU_RING_BYTES, 1019, 0,
	    &state, &code, output, sizeof(output), &size, &next, &result) == 0);
	CHECK(result.sum == 2008 && next == 0);
	memset(&state, 0, sizeof(state));
	header(wire, 0, 0, 0);
	CHECK(cixvpu_ring_read_sum(wire, CIXVPU_RING_BYTES, 0, 1,
	    &state, &code, output, sizeof(output), &size, &next, &result) == 0);
	CHECK(result.sum == 0 && code == 0 && size == 0);
	header(wire, 0, 1, 0);
	sum_read_error(wire, 0, 1, sizeof(output), &state, EPROTO, 0);
	check_guards(&ring);
}

static void
test_sum_invalid(void)
{
	struct guarded_ring ring;
	struct cixvpu_ring_checksum state = { 1, { 2, 3, 4 } }, result, original;
	uint8_t *wire = reset_ring(&ring), before[CIXVPU_RING_BYTES], output[8];
	uint16_t code = 0xfeed, next = 0xbeef;
	size_t size = (size_t)0xabcdef;

	memset(&result, 0x96, sizeof(result));
	original = result;
	memcpy(before, wire, sizeof(before));
	sum_write_error(wire, 0, 1018, NULL, 0, &state, EAGAIN);
	sum_write_error(wire, 1020, 0, NULL, 0, &state, EINVAL);
	sum_write_error(wire, 0, 0, NULL, 1, &state, EINVAL);
	sum_write_error(wire, 0, 0, NULL, SIZE_MAX, &state, EINVAL);
	CHECK(cixvpu_ring_write_sum(wire, CIXVPU_RING_BYTES, 0, 0, 1001,
	    NULL, 0, NULL, &next, &result) == EINVAL);
	CHECK(cixvpu_ring_write_sum(wire, CIXVPU_RING_BYTES, 0, 0, 1001,
	    NULL, 0, &state, &next, NULL) == EINVAL);
	CHECK(cixvpu_ring_write_sum(wire, CIXVPU_RING_BYTES, 0, 0, 1001,
	    NULL, 0, &state, &next, &state) == EINVAL);
	CHECK(cixvpu_ring_write_sum(wire, CIXVPU_RING_BYTES, 0, 0, 1001,
	    NULL, 0, &state, (uint16_t *)(void *)&state, &result) == EINVAL);
	CHECK(cixvpu_ring_write_sum(wire, CIXVPU_RING_BYTES, 0, 0, 1001,
	    &state, 4, &state, &next, &result) == EINVAL);
	CHECK(cixvpu_ring_write_sum(wire, CIXVPU_RING_BYTES, 0, 0, 1001,
	    NULL, 0, (const struct cixvpu_ring_checksum *)(const void *)wire,
	    &next, &result) == EINVAL);
	CHECK(cixvpu_ring_read_sum(wire, CIXVPU_RING_BYTES, 0, 1, NULL,
	    &code, output, sizeof(output), &size, &next, &result) == EINVAL);
	CHECK(cixvpu_ring_read_sum(wire, CIXVPU_RING_BYTES, 0, 1, &state,
	    &code, output, sizeof(output), &size, &next, NULL) == EINVAL);
	CHECK(cixvpu_ring_read_sum(wire, CIXVPU_RING_BYTES, 0, 1, &state,
	    &code, output, sizeof(output), &size, &next, &state) == EINVAL);
	CHECK(cixvpu_ring_read_sum(wire, CIXVPU_RING_BYTES, 0, 1, &state,
	    (uint16_t *)(void *)&state, output, sizeof(output), &size, &next,
	    &result) == EINVAL);
	CHECK(cixvpu_ring_read_sum(wire, CIXVPU_RING_BYTES, 0, 1, &state,
	    &code, &state, sizeof(state), &size, &next, &result) == EINVAL);
	CHECK(code == 0xfeed && next == 0xbeef && size == (size_t)0xabcdef);
	CHECK(state.sum == 1 && state.reserved[0] == 2 &&
	    state.reserved[1] == 3 && state.reserved[2] == 4);
	CHECK(memcmp(&result, &original, sizeof(result)) == 0);
	CHECK(memcmp(wire, before, sizeof(before)) == 0);
	check_guards(&ring);
}

int
main(void)
{

	test_wire();
	test_empty_full();
	test_wrap_all_positions();
	test_malformed();
	test_invalid();
	test_sequences();
	test_sum_command_vectors();
	test_sum_all_positions();
	test_sum_receive_snapshot();
	test_sum_invalid();
	printf("CIXVPU_RING_CONTRACT_PASS checks=%u\n", checks);
	return EXIT_SUCCESS;
}
