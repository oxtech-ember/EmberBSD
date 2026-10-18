/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _DEV_IC_CIXVPU_COMMAND_H_
#define _DEV_IC_CIXVPU_COMMAND_H_

#ifdef _KERNEL
#include <sys/types.h>
#else
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#endif

#define CIXVPU_COMMAND_GO 1001U
#define CIXVPU_COMMAND_STOP 1002U
#define CIXVPU_COMMAND_INPUT_FLUSH 1003U
#define CIXVPU_COMMAND_OUTPUT_FLUSH 1004U
#define CIXVPU_COMMAND_PING 1006U
#define CIXVPU_COMMAND_JOB 1009U
#define CIXVPU_COMMAND_SET_OPTION 1010U
#define CIXVPU_COMMAND_IDLE_ACK 1012U
#define CIXVPU_COMMAND_JOB_DISABLE_BNDMGR UINT32_C(1)
#define CIXVPU_COMMAND_NALU_FORMAT 1U
#define CIXVPU_COMMAND_B_FRAMES 17U
#define CIXVPU_COMMAND_TEXT_BYTES 128U

enum cixvpu_command_response_kind {
	CIXVPU_COMMAND_CONFIRMED,
	CIXVPU_COMMAND_OPTION_FAILED,
	CIXVPU_COMMAND_FATAL,
	CIXVPU_COMMAND_STATE,
	CIXVPU_COMMAND_JOB_DEQUEUED
};
struct cixvpu_command_response {
	enum cixvpu_command_response_kind kind;
	uint32_t index, state, error, valid_job;
	bool index_valid;
	uint8_t text[CIXVPU_COMMAND_TEXT_BYTES]; /* Raw bytes, not a C string. */
};

/*
 * Pure payload codec for protocol 3.5; original AI-assisted BSD implementation.
 * Facts: Sky1-Linux/linux-sky1 57e018a398248d7e5e4d798610df79a557c0629f,
 * CIX fw_v3/mve_protocol_def.h and mvx_firmware_v2.c. No GPL code is copied.
 *
 * JOB writes 8 bytes: cores 1..65535, frames 0..65535 (zero means unlimited),
 * and only the defined DISABLE_BNDMGR flag. Hardware/scheduler core limits
 * are caller policy. The one-frame fixture is 1/1/0, not a serializer limit.
 * Option writes 8 bytes. It accepts NALU index 1 with values 1, 2, 4, 8, 16, 32
 * (32 is the protocol's debug MBINFO value), or B-frames index 17 with any
 * uint32_t count. Codec/firmware acceptance is separate from wire validity.
 *
 * Only payloads are written. Use the existing ring codec for framing/checksum.
 * Output capacity must describe addressable bytes. Wire buffers may be
 * unaligned; scalar/structure outputs require natural alignment. Output
 * regions must not overlap. EMSGSIZE means a short output buffer. EINVAL
 * means invalid arguments/ranges/flags/overlap; ENOTSUP is an unknown option.
 * Every failure leaves all output bytes and scalars unchanged.
 *
 * Response consumes a caller-owned coherent payload, without ring/cursor I/O.
 * Recognized codes: 2003 CONFIRM, 2017 OPTION_FAILED, 2010 FATAL, 2011 STATE,
 * and 2004 JOB_DEQUEUED. CONFIRM permits exact 0/4 bytes and preserves whether
 * an index exists; it does not infer a match for an absent index. STATE must
 * be exact 4 bytes and STOPPED 0/RUNNING 2. FATAL/OPTION_FAILED are exact 132
 * bytes (LE32 value plus 128 raw text bytes); neither requires a NUL byte.
 * JOB_DEQUEUED is exact 4 bytes; valid_job is preserved as raw protocol data.
 * Other codes return ENOTSUP; malformed recognized payloads return EPROTO.
 * NULL payload is permitted only with zero length. Input/output must not
 * overlap. Outputs are unchanged on every error. No state machine, sticky
 * error, DMA ownership, ACK, MMIO or native command submission is provided.
 */
int cixvpu_command_job(uint32_t cores, uint32_t frames, uint32_t flags,
    void *out, size_t capacity, size_t *length);
int cixvpu_command_option(uint32_t index, uint32_t value, void *out,
    size_t capacity, size_t *length);
int cixvpu_command_response(uint16_t code, const void *payload, size_t length,
    struct cixvpu_command_response *out);

#endif
