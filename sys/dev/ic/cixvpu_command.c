/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Original AI-assisted implementation. Protocol facts are cited in the header. */
#ifdef _KERNEL
#include <sys/errno.h>
#include <sys/systm.h>
#else
#include <errno.h>
#include <string.h>
#endif
#include "cixvpu_command.h"

static bool
cixvpu_command_span(const void *p, size_t size)
{
	return size <= UINTPTR_MAX - (uintptr_t)p;
}

static bool
cixvpu_command_overlap(const void *a, size_t asize, const void *b, size_t bsize)
{
	return asize != 0 && bsize != 0 && (uintptr_t)a < (uintptr_t)b + bsize &&
	    (uintptr_t)b < (uintptr_t)a + asize;
}

static uint32_t
cixvpu_command_le32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
	    (uint32_t)p[3] << 24;
}

static void
cixvpu_command_put32(uint8_t *p, uint32_t value)
{
	p[0] = (uint8_t)value;
	p[1] = (uint8_t)(value >> 8);
	p[2] = (uint8_t)(value >> 16);
	p[3] = (uint8_t)(value >> 24);
}

static int
cixvpu_command_output(const void *out, size_t capacity, const size_t *length,
    size_t required)
{
	if (out == NULL || length == NULL ||
	    (uintptr_t)length % _Alignof(size_t) != 0 ||
	    !cixvpu_command_span(out, capacity) ||
	    !cixvpu_command_span(length, sizeof(*length)) ||
	    cixvpu_command_overlap(out, capacity, length, sizeof(*length)))
		return EINVAL;
	return capacity < required ? EMSGSIZE : 0;
}

int
cixvpu_command_job(uint32_t cores, uint32_t frames, uint32_t flags, void *out,
    size_t capacity, size_t *length)
{
	uint8_t *p = out;
	int error;

	if (cores == 0 || cores > UINT16_MAX || frames > UINT16_MAX ||
	    (flags & ~CIXVPU_COMMAND_JOB_DISABLE_BNDMGR) != 0)
		return EINVAL;
	error = cixvpu_command_output(out, capacity, length, 8);
	if (error != 0)
		return error;
	p[0] = (uint8_t)cores;
	p[1] = (uint8_t)(cores >> 8);
	p[2] = (uint8_t)frames;
	p[3] = (uint8_t)(frames >> 8);
	cixvpu_command_put32(p + 4, flags);
	*length = 8;
	return 0;
}

int
cixvpu_command_option(uint32_t index, uint32_t value, void *out,
    size_t capacity, size_t *length)
{
	uint8_t *p = out;
	int error;

	if (index != CIXVPU_COMMAND_NALU_FORMAT && index != CIXVPU_COMMAND_B_FRAMES &&
	    index != CIXVPU_COMMAND_P_FRAMES && index != CIXVPU_COMMAND_FIXED_QP)
		return ENOTSUP;
	if (index == CIXVPU_COMMAND_NALU_FORMAT && value != 1 && value != 2 &&
	    value != 4 && value != 8 && value != 16 && value != 32)
		return EINVAL;
	if (index == CIXVPU_COMMAND_FIXED_QP && value > 51)
		return EINVAL;
	error = cixvpu_command_output(out, capacity, length, 8);
	if (error != 0)
		return error;
	cixvpu_command_put32(p, index);
	cixvpu_command_put32(p + 4, value);
	*length = 8;
	return 0;
}

int
cixvpu_command_hevc_main(uint32_t main_tier_level, void *out, size_t capacity,
    size_t *length)
{
	uint8_t *p = out;
	int error;

	switch (main_tier_level) {
	case CIXVPU_COMMAND_HEVC_LEVEL_1:
	case CIXVPU_COMMAND_HEVC_LEVEL_2:
	case CIXVPU_COMMAND_HEVC_LEVEL_21:
	case CIXVPU_COMMAND_HEVC_LEVEL_3:
	case CIXVPU_COMMAND_HEVC_LEVEL_31:
	case CIXVPU_COMMAND_HEVC_LEVEL_4:
	case CIXVPU_COMMAND_HEVC_LEVEL_41:
	case CIXVPU_COMMAND_HEVC_LEVEL_5:
	case CIXVPU_COMMAND_HEVC_LEVEL_51:
	case CIXVPU_COMMAND_HEVC_LEVEL_52:
	case CIXVPU_COMMAND_HEVC_LEVEL_6:
	case CIXVPU_COMMAND_HEVC_LEVEL_61:
	case CIXVPU_COMMAND_HEVC_LEVEL_62:
		break;
	default:
		return EINVAL;
	}
	error = cixvpu_command_output(out, capacity, length, 8);
	if (error != 0)
		return error;
	cixvpu_command_put32(p, CIXVPU_COMMAND_PROFILE_LEVEL);
	p[4] = 1; /* HEVC Main profile, LE16. */
	p[5] = 0;
	p[6] = (uint8_t)main_tier_level;
	p[7] = 0;
	*length = 8;
	return 0;
}

int
cixvpu_command_input_frame_rate(uint32_t fps_q16, void *out, size_t capacity,
    size_t *length)
{
	uint8_t *p = out;
	int error;

	if (fps_q16 == 0)
		return EINVAL;
	error = cixvpu_command_output(out, capacity, length, 8);
	if (error != 0)
		return error;
	cixvpu_command_put32(p, CIXVPU_COMMAND_FRAME_RATE);
	cixvpu_command_put32(p + 4, fps_q16);
	*length = 8;
	return 0;
}

int
cixvpu_command_input_rate_control_off(void *out, size_t capacity,
    size_t *length)
{
	uint8_t *p = out;
	int error;

	error = cixvpu_command_output(out, capacity, length, 16);
	if (error != 0)
		return error;
	cixvpu_command_put32(p, CIXVPU_COMMAND_RATE_CONTROL);
	cixvpu_command_put32(p + 4, 0);
	cixvpu_command_put32(p + 8, 0);
	cixvpu_command_put32(p + 12, 0);
	*length = 16;
	return 0;
}

int
cixvpu_command_response(uint16_t code, const void *payload, size_t length,
    struct cixvpu_command_response *out)
{
	struct cixvpu_command_response response;
	const uint8_t *p = payload;

	if (out == NULL || (payload == NULL && length != 0) ||
	    (uintptr_t)out % _Alignof(struct cixvpu_command_response) != 0 ||
	    !cixvpu_command_span(payload, length) ||
	    !cixvpu_command_span(out, sizeof(*out)) ||
	    cixvpu_command_overlap(payload, length, out, sizeof(*out)))
		return EINVAL;
	memset(&response, 0, sizeof(response));
	switch (code) {
	case 2003:
		if (length != 0 && length != 4)
			return EPROTO;
		response.kind = CIXVPU_COMMAND_CONFIRMED;
		if (length == 4) {
			response.index = cixvpu_command_le32(p);
			response.index_valid = true;
		}
		break;
	case 2004:
		if (length != 4)
			return EPROTO;
		response.kind = CIXVPU_COMMAND_JOB_DEQUEUED;
		response.valid_job = cixvpu_command_le32(p);
		break;
	case 2010:
	case 2017:
		if (length != 132)
			return EPROTO;
		if (code == 2010) {
			response.kind = CIXVPU_COMMAND_FATAL;
			response.error = cixvpu_command_le32(p);
		} else {
			response.kind = CIXVPU_COMMAND_OPTION_FAILED;
			response.index = cixvpu_command_le32(p);
			response.index_valid = true;
		}
		memcpy(response.text, p + 4, sizeof(response.text));
		break;
	case 2011:
		if (length != 4)
			return EPROTO;
		response.state = cixvpu_command_le32(p);
		if (response.state != 0 && response.state != 2)
			return EPROTO;
		response.kind = CIXVPU_COMMAND_STATE;
		break;
	default:
		return ENOTSUP;
	}
	memcpy(out, &response, sizeof(response));
	return 0;
}
