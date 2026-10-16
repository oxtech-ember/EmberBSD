/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2026 EmberBSD contributors.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * AI-assisted implementation. See cixvpu_ring.h for wire provenance and the
 * caller's synchronization contract. This file does not drive hardware.
 */

#ifdef _KERNEL
#include <sys/null.h>
#include <sys/errno.h>
#else
#include <errno.h>
#endif

#include "cixvpu_ring.h"

static int
cixvpu_ring_span(const void *p, size_t size)
{

	return size == 0 || (p != NULL &&
	    size - 1 <= (uintptr_t)-1 - (uintptr_t)p);
}

static int
cixvpu_ring_overlap(const void *a, size_t asize, const void *b, size_t bsize)
{

	if (asize == 0 || bsize == 0)
		return 0;
	if ((uintptr_t)a <= (uintptr_t)b)
		return (uintptr_t)b - (uintptr_t)a < asize;
	return (uintptr_t)a - (uintptr_t)b < bsize;
}

struct cixvpu_ring_object {
	const void *address;
	size_t size;
};

static int
cixvpu_ring_objects(const struct cixvpu_ring_object *objects, size_t count)
{
	size_t i, j;

	for (i = 0; i < count; i++) {
		if (!cixvpu_ring_span(objects[i].address, objects[i].size))
			return 0;
		for (j = 0; j < i; j++) {
			if (cixvpu_ring_overlap(objects[i].address,
			    objects[i].size, objects[j].address, objects[j].size))
				return 0;
		}
	}
	return 1;
}

static uint32_t
cixvpu_ring_get32(const uint8_t *wire, size_t offset)
{

	return (uint32_t)wire[offset] | (uint32_t)wire[offset + 1] << 8 |
	    (uint32_t)wire[offset + 2] << 16 | (uint32_t)wire[offset + 3] << 24;
}

static void
cixvpu_ring_put32(uint8_t *wire, size_t offset, uint32_t value)
{

	wire[offset] = (uint8_t)value;
	wire[offset + 1] = (uint8_t)(value >> 8);
	wire[offset + 2] = (uint8_t)(value >> 16);
	wire[offset + 3] = (uint8_t)(value >> 24);
}

static uint32_t
cixvpu_ring_sum32(const uint8_t *wire, uint16_t pos, size_t words)
{
	uint32_t sum = 0;
	size_t offset = (size_t)pos * 4, i;

	for (i = 0; i < words; i++) {
		sum += cixvpu_ring_get32(wire, offset);
		offset = (offset + 4) % CIXVPU_RING_BYTES;
	}
	return sum;
}

static unsigned int
cixvpu_ring_used(uint16_t read, uint16_t write)
{

	return write >= read ? (unsigned int)write - read :
	    CIXVPU_RING_WORDS - read + write;
}

static int
cixvpu_ring_read_frame(const void *data, size_t data_size, uint16_t read,
    uint16_t write, uint16_t *code, void *payload, size_t capacity,
    size_t *length, uint16_t *next,
    const struct cixvpu_ring_checksum *checksum,
    struct cixvpu_ring_checksum *next_checksum)
{
	const uint8_t *wire = data;
	uint8_t *out = payload;
	const struct cixvpu_ring_object objects[] = {
		{ data, data_size }, { payload, capacity },
		{ code, sizeof(*code) }, { length, sizeof(*length) },
		{ next, sizeof(*next) },
		{ checksum, checksum == NULL ? 0 : sizeof(*checksum) },
		{ next_checksum, checksum == NULL ? 0 : sizeof(*next_checksum) }
	};
	struct cixvpu_ring_checksum result;
	size_t offset, size, words, i;
	uint16_t message_code;
	unsigned int used;

	if (data_size != CIXVPU_RING_BYTES ||
	    read >= CIXVPU_RING_WORDS || write >= CIXVPU_RING_WORDS ||
	    !cixvpu_ring_objects(objects, sizeof(objects) / sizeof(objects[0])))
		return EINVAL;

	used = cixvpu_ring_used(read, write);
	if (used == 0)
		return EAGAIN;
	offset = (size_t)read * 4;
	message_code = (uint16_t)wire[offset] |
	    (uint16_t)((uint16_t)wire[offset + 1] << 8);
	size = (size_t)wire[offset + 2] | (size_t)wire[offset + 3] << 8;
	if (size > CIXVPU_RING_MAX_PAYLOAD)
		return EPROTO;
	words = 1 + (size + 3) / 4;
	if (words > used)
		return EPROTO;
	if (checksum != NULL) {
		uint32_t snapshot_sum = checksum->sum +
		    cixvpu_ring_sum32(wire, read, used);

		if (snapshot_sum != checksum->reserved[0] &&
		    snapshot_sum != checksum->reserved[1] &&
		    snapshot_sum != checksum->reserved[2])
			return EPROTO;
		result = *checksum;
		result.sum += cixvpu_ring_sum32(wire, read, words);
	}
	if (size > capacity) {
		*length = size;
		return EMSGSIZE;
	}

	offset = (offset + 4) % CIXVPU_RING_BYTES;
	for (i = 0; i < size; i++) {
		out[i] = wire[offset];
		offset = (offset + 1) % CIXVPU_RING_BYTES;
	}
	*code = message_code;
	*length = size;
	*next = (uint16_t)((read + words) % CIXVPU_RING_WORDS);
	if (checksum != NULL)
		*next_checksum = result;
	return 0;
}

static int
cixvpu_ring_write_frame(void *data, size_t data_size, uint16_t read,
    uint16_t write, uint16_t code, const void *payload, size_t size,
    uint16_t *next, const struct cixvpu_ring_checksum *checksum,
    struct cixvpu_ring_checksum *next_checksum)
{
	const uint8_t *in = payload;
	uint8_t *wire = data;
	const struct cixvpu_ring_object objects[] = {
		{ data, data_size }, { payload, size }, { next, sizeof(*next) },
		{ checksum, checksum == NULL ? 0 : sizeof(*checksum) },
		{ next_checksum, checksum == NULL ? 0 : sizeof(*next_checksum) }
	};
	struct cixvpu_ring_checksum result;
	size_t words, offset, padded, i;
	unsigned int free_words;

	if (data_size != CIXVPU_RING_BYTES ||
	    read >= CIXVPU_RING_WORDS || write >= CIXVPU_RING_WORDS ||
	    size > (checksum == NULL ? CIXVPU_RING_MAX_PAYLOAD :
	    CIXVPU_RING_SUM_MAX_PAYLOAD) ||
	    !cixvpu_ring_objects(objects, sizeof(objects) / sizeof(objects[0])))
		return EINVAL;

	padded = (size + 3) / 4 * 4;
	words = 1 + padded / 4 + (checksum != NULL ? 1 : 0);
	free_words = CIXVPU_RING_WORDS - 1 - cixvpu_ring_used(read, write);
	if (words > free_words)
		return EAGAIN;
	if (checksum != NULL) {
		result = *checksum;
		result.sum += (uint32_t)code | (uint32_t)size << 16;
		for (i = 0; i < size; i++)
			result.sum += (uint32_t)in[i] << ((i % 4) * 8);
		result.reserved[0] = checksum->reserved[1];
		result.reserved[1] = checksum->reserved[2];
		result.reserved[2] = result.sum;
	}
	offset = (size_t)write * 4;
	wire[offset] = (uint8_t)code;
	wire[offset + 1] = (uint8_t)(code >> 8);
	wire[offset + 2] = (uint8_t)size;
	wire[offset + 3] = (uint8_t)(size >> 8);
	offset = (offset + 4) % CIXVPU_RING_BYTES;
	if (checksum != NULL) {
		cixvpu_ring_put32(wire, offset, result.sum);
		offset = (offset + 4) % CIXVPU_RING_BYTES;
	}
	for (i = 0; i < padded; i++) {
		wire[offset] = i < size ? in[i] : 0;
		offset = (offset + 1) % CIXVPU_RING_BYTES;
	}
	*next = (uint16_t)((write + words) % CIXVPU_RING_WORDS);
	if (checksum != NULL)
		*next_checksum = result;
	return 0;
}

int
cixvpu_ring_read(const void *data, size_t data_size, uint16_t read,
    uint16_t write, uint16_t *code, void *payload, size_t capacity,
    size_t *length, uint16_t *next)
{

	return cixvpu_ring_read_frame(data, data_size, read, write, code,
	    payload, capacity, length, next, NULL, NULL);
}

int
cixvpu_ring_write(void *data, size_t data_size, uint16_t read,
    uint16_t write, uint16_t code, const void *payload, size_t size,
    uint16_t *next)
{

	return cixvpu_ring_write_frame(data, data_size, read, write, code,
	    payload, size, next, NULL, NULL);
}

int
cixvpu_ring_read_sum(const void *data, size_t data_size, uint16_t read,
    uint16_t write, const struct cixvpu_ring_checksum *checksum,
    uint16_t *code, void *payload, size_t capacity, size_t *length,
    uint16_t *next, struct cixvpu_ring_checksum *next_checksum)
{

	if (checksum == NULL || next_checksum == NULL)
		return EINVAL;
	return cixvpu_ring_read_frame(data, data_size, read, write, code,
	    payload, capacity, length, next, checksum, next_checksum);
}

int
cixvpu_ring_write_sum(void *data, size_t data_size, uint16_t read,
    uint16_t write, uint16_t code, const void *payload, size_t size,
    const struct cixvpu_ring_checksum *checksum, uint16_t *next,
    struct cixvpu_ring_checksum *next_checksum)
{

	if (checksum == NULL || next_checksum == NULL)
		return EINVAL;
	return cixvpu_ring_write_frame(data, data_size, read, write, code,
	    payload, size, next, checksum, next_checksum);
}
