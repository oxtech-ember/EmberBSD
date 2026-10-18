/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Original AI-assisted implementation of the facts cited in the header. */
#ifdef _KERNEL
#include <sys/errno.h>
#include <sys/systm.h>
#else
#include <errno.h>
#include <string.h>
#endif
#include "cixvpu_buffer.h"

static bool
cixvpu_buffer_span(const void *p, size_t size)
{
	return size <= UINTPTR_MAX - (uintptr_t)p;
}

static bool
cixvpu_buffer_value(const void *p, size_t size, size_t alignment)
{
	return p != NULL && (uintptr_t)p % alignment == 0 &&
	    cixvpu_buffer_span(p, size);
}

static bool
cixvpu_buffer_overlap(const void *a, size_t asize, const void *b, size_t bsize)
{
	return asize != 0 && bsize != 0 && (uintptr_t)a < (uintptr_t)b + bsize &&
	    (uintptr_t)b < (uintptr_t)a + asize;
}

static uint32_t
cixvpu_buffer_le32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
	    (uint32_t)p[3] << 24;
}

static void
cixvpu_buffer_put32(uint8_t *p, uint32_t value)
{
	p[0] = (uint8_t)value;
	p[1] = (uint8_t)(value >> 8);
	p[2] = (uint8_t)(value >> 16);
	p[3] = (uint8_t)(value >> 24);
}

static void
cixvpu_buffer_put64(uint8_t *p, uint64_t value)
{
	cixvpu_buffer_put32(p, (uint32_t)value);
	cixvpu_buffer_put32(p + 4, (uint32_t)(value >> 32));
}

static bool
cixvpu_buffer_va(const struct cixvpu_buffer_span *span, uint64_t begin,
    uint64_t end)
{
	return span->bytes != 0 && span->address >= begin &&
	    (uint64_t)span->address + span->bytes <= end;
}

static bool
cixvpu_buffer_nv12_valid(const struct cixvpu_buffer_nv12 *slot)
{
	uint64_t yend, uvend;

	if (slot->width == 0 || slot->height == 0 ||
	    slot->width > UINT16_MAX || slot->height > UINT16_MAX ||
	    (slot->width & 1) != 0 || (slot->height & 1) != 0 ||
	    slot->stride_y < slot->width || slot->stride_uv < slot->width ||
	    slot->stride_y > INT32_MAX || slot->stride_uv > INT32_MAX ||
	    !cixvpu_buffer_va(&slot->y, UINT32_C(0x70000000),
	    UINT32_C(0xf0000000)) ||
	    !cixvpu_buffer_va(&slot->uv, UINT32_C(0x70000000),
	    UINT32_C(0xf0000000)) ||
	    (uint64_t)slot->stride_y * slot->height > slot->y.bytes ||
	    (uint64_t)slot->stride_uv * (slot->height / 2) > slot->uv.bytes)
		return false;
	yend = (uint64_t)slot->y.address + slot->y.bytes;
	uvend = (uint64_t)slot->uv.address + slot->uv.bytes;
	return yend <= slot->uv.address || uvend <= slot->y.address;
}

static bool
cixvpu_buffer_stream_valid(const struct cixvpu_buffer_bitstream *slot)
{
	return cixvpu_buffer_va(&slot->data, UINT32_C(0x20000000),
	    UINT32_C(0x70000000));
}

static int
cixvpu_buffer_encode_args(const void *slot, size_t size, size_t alignment,
    const void *out, size_t capacity, const size_t *length, size_t needed)
{
	if (!cixvpu_buffer_value(slot, size, alignment) || out == NULL ||
	    !cixvpu_buffer_span(out, capacity) ||
	    !cixvpu_buffer_value(length, sizeof(*length), _Alignof(size_t)) ||
	    cixvpu_buffer_overlap(slot, size, out, capacity) ||
	    cixvpu_buffer_overlap(slot, size, length, sizeof(*length)) ||
	    cixvpu_buffer_overlap(out, capacity, length, sizeof(*length)))
		return EINVAL;
	return capacity < needed ? EMSGSIZE : 0;
}

static int
cixvpu_buffer_return_args(const void *payload, size_t length, const void *slot,
    size_t size, size_t alignment, const void *out, size_t outsize,
    size_t outalign)
{
	if ((payload == NULL && length != 0) ||
	    !cixvpu_buffer_span(payload, length) ||
	    !cixvpu_buffer_value(slot, size, alignment) ||
	    !cixvpu_buffer_value(out, outsize, outalign) ||
	    cixvpu_buffer_overlap(payload, length, out, outsize) ||
	    cixvpu_buffer_overlap(slot, size, out, outsize))
		return EINVAL;
	return 0;
}

static void
cixvpu_buffer_frame_bytes(const struct cixvpu_buffer_nv12 *slot, uint8_t p[72])
{
	uint32_t flags = 4;

	if (slot->force_idr)
		flags |= UINT32_C(0x400);
	if (slot->eos)
		flags |= UINT32_C(0x10000);
	memset(p, 0, 72);
	cixvpu_buffer_put64(p, slot->handle);
	cixvpu_buffer_put64(p + 8, slot->tag);
	cixvpu_buffer_put32(p + 16, flags);
	p[20] = (uint8_t)slot->height;
	p[21] = (uint8_t)(slot->height >> 8);
	p[22] = (uint8_t)slot->width;
	p[23] = (uint8_t)(slot->width >> 8);
	p[24] = 1;
	p[25] = 2;
	cixvpu_buffer_put32(p + 28, slot->y.address);
	cixvpu_buffer_put32(p + 32, slot->uv.address);
	cixvpu_buffer_put32(p + 52, slot->stride_y);
	cixvpu_buffer_put32(p + 56, slot->stride_uv);
	p[64] = p[22];
	p[65] = p[23];
	p[66] = p[20];
	p[67] = p[21];
}

int
cixvpu_buffer_frame_nv12(const struct cixvpu_buffer_nv12 *slot, void *out,
    size_t capacity, size_t *length)
{
	uint8_t p[72];
	int error;

	error = cixvpu_buffer_encode_args(slot, sizeof(*slot),
	    _Alignof(struct cixvpu_buffer_nv12), out, capacity, length, sizeof(p));
	if (error != 0)
		return error;
	if (!cixvpu_buffer_nv12_valid(slot))
		return EINVAL;
	cixvpu_buffer_frame_bytes(slot, p);
	memcpy(out, p, sizeof(p));
	*length = sizeof(p);
	return 0;
}

int
cixvpu_buffer_bitstream(const struct cixvpu_buffer_bitstream *slot, void *out,
    size_t capacity, size_t *length)
{
	uint8_t p[40];
	int error;

	error = cixvpu_buffer_encode_args(slot, sizeof(*slot),
	    _Alignof(struct cixvpu_buffer_bitstream), out, capacity, length, sizeof(p));
	if (error != 0)
		return error;
	if (!cixvpu_buffer_stream_valid(slot))
		return EINVAL;
	memset(p, 0, sizeof(p));
	cixvpu_buffer_put64(p, slot->handle);
	cixvpu_buffer_put64(p + 8, slot->tag);
	cixvpu_buffer_put32(p + 20, slot->data.bytes);
	cixvpu_buffer_put32(p + 32, slot->data.address);
	memcpy(out, p, sizeof(p));
	*length = sizeof(p);
	return 0;
}

int
cixvpu_buffer_frame_return(const void *payload, size_t length,
    const struct cixvpu_buffer_nv12 *expected,
    struct cixvpu_buffer_frame_result *out)
{
	struct cixvpu_buffer_frame_result result;
	const uint8_t *p = payload;
	uint8_t canonical[72];
	int error;

	error = cixvpu_buffer_return_args(payload, length, expected,
	    sizeof(*expected), _Alignof(struct cixvpu_buffer_nv12), out, sizeof(*out),
	    _Alignof(struct cixvpu_buffer_frame_result));
	if (error != 0)
		return error;
	if (!cixvpu_buffer_nv12_valid(expected))
		return EINVAL;
	if (length != sizeof(canonical))
		return EPROTO;
	cixvpu_buffer_frame_bytes(expected, canonical);
	if (memcmp(p, canonical, 16) != 0 ||
	    memcmp(p + 20, canonical + 20, 52) != 0)
		return EPROTO;
	memset(&result, 0, sizeof(result));
	result.flags = cixvpu_buffer_le32(p + 16);
	result.rejected = (result.flags & UINT32_C(0x1000)) != 0;
	result.corrupt = (result.flags & UINT32_C(0x2000)) != 0;
	memcpy(out, &result, sizeof(result));
	return 0;
}

int
cixvpu_buffer_bitstream_return(const void *payload, size_t length,
    const struct cixvpu_buffer_bitstream *expected,
    struct cixvpu_buffer_bitstream_result *out)
{
	struct cixvpu_buffer_bitstream_result result;
	const uint8_t *p = payload;
	uint8_t identity[16];
	int error;

	error = cixvpu_buffer_return_args(payload, length, expected,
	    sizeof(*expected), _Alignof(struct cixvpu_buffer_bitstream), out,
	    sizeof(*out), _Alignof(struct cixvpu_buffer_bitstream_result));
	if (error != 0)
		return error;
	if (!cixvpu_buffer_stream_valid(expected))
		return EINVAL;
	if (length != 40)
		return EPROTO;
	cixvpu_buffer_put64(identity, expected->handle);
	cixvpu_buffer_put64(identity + 8, expected->tag);
	if (memcmp(p, identity, sizeof(identity)) != 0 ||
	    cixvpu_buffer_le32(p + 20) != expected->data.bytes ||
	    cixvpu_buffer_le32(p + 32) != expected->data.address)
		return EPROTO;
	memset(&result, 0, sizeof(result));
	result.offset = cixvpu_buffer_le32(p + 24);
	result.filled = cixvpu_buffer_le32(p + 28);
	if (result.offset > expected->data.bytes ||
	    result.filled > expected->data.bytes - result.offset)
		return EPROTO;
	result.flags = cixvpu_buffer_le32(p + 16);
	result.frame_type = p[36];
	result.transform = p[37];
	result.remaining_kb = (uint16_t)p[38] | (uint16_t)p[39] << 8;
	memcpy(out, &result, sizeof(result));
	return 0;
}
