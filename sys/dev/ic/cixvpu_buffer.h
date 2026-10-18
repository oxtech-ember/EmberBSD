/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _DEV_IC_CIXVPU_BUFFER_H_
#define _DEV_IC_CIXVPU_BUFFER_H_

#ifdef _KERNEL
#include <sys/types.h>
#else
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#endif

#define CIXVPU_BUFFER_FRAME 3001U
#define CIXVPU_BUFFER_BITSTREAM 3002U
#define CIXVPU_BUFFER_FRAME_BYTES 72U
#define CIXVPU_BUFFER_BITSTREAM_BYTES 40U

struct cixvpu_buffer_span {
	uint32_t address, bytes; /* Firmware VA/capacity, never a CPU/bus address. */
};
struct cixvpu_buffer_nv12 {
	uint64_t handle, tag; /* Opaque identities, never dereferenced. */
	uint32_t width, height, stride_y, stride_uv;
	struct cixvpu_buffer_span y, uv;
	bool force_idr, eos;
};
struct cixvpu_buffer_bitstream {
	uint64_t handle, tag;
	struct cixvpu_buffer_span data;
};
struct cixvpu_buffer_frame_result {
	uint32_t flags;
	bool rejected, corrupt; /* Parsing success does not mean frame success. */
};
struct cixvpu_buffer_bitstream_result {
	uint32_t offset, filled, flags;
	uint16_t remaining_kb;
	uint8_t frame_type, transform;
};

/*
 * Original, AI-assisted pure protocol codec, not a firmware driver.
 * Facts: Sky1-Linux/linux-sky1 57e018a398248d7e5e4d798610df79a557c0629f,
 * CIX fw_v3/mve_protocol_def.h and the v2 producer. No GPL code is copied.
 *
 * The closed FRAME profile is progressive, positive even NV12 dimensions
 * through 65535, format0x0201 and positive int32 strides at least width.
 * Full Y/UV capacity spans must be disjoint and inside [0x70000000,0xf0000000).
 * Checked stride*rows must fit each capacity. No extra stride/page alignment
 * is inferred. TOP_PRESENT and optional FORCE_IDR/EOS are emitted. Maximum
 * dimensions equal visible dimensions; all unused/reserved bytes are zero.
 * Other formats, odd dimensions, negative strides and empty EOS are outside
 * this local profile, not alleged protocol-wide restrictions.
 *
 * BITSTREAM encodes an empty output slot in [0x20000000,0x70000000).
 * Neither aperture check proves mapped backing, permissions or ownership.
 * A caller must exclude logical RPC reservations and bind slots to actual
 * owned pages before device exposure. No physical size/default is selected.
 *
 * Return decoders require exact payload72/40 and immutable expected slots.
 * FRAME matches every byte except raw flags, including unused fields.
 * BITSTREAM matches handle/tag/VA/capacity and bounds offset and filled bytes
 * by subtraction. Its expected.tag identifies the correlated outstanding
 * INPUT user_data_tag, not necessarily the original empty output slot tag.
 * Flags/type/transform/remaining are numeric observations;
 * they do not imply consumption, EOF, complete output, release or success.
 * FRAME additionally reports REJECTED/CORRUPT. No encoded bytes are accessed.
 *
 * All objects must be addressable for their stated sizes and immutable inputs
 * remain stable. Value/scalar outputs require natural alignment. Wire bytes
 * may be unaligned. Outputs must not overlap inputs or one another, including
 * whole capacity spans and scalar slots. Invalid arguments, slot geometry,
 * arithmetic spans or aliases return EINVAL; short encoding capacity returns
 * EMSGSIZE. Wrong return shape/identity/geometry/bounds returns EPROTO.
 * Every error preserves all outputs. Success writes only fixed payload bytes
 * or the result value. This is not pointer authentication or a global memory
 * guard. No ring ACK, session, DMA sync, native submission or owner change.
 */
int cixvpu_buffer_frame_nv12(const struct cixvpu_buffer_nv12 *slot,
    void *out, size_t capacity, size_t *length);
int cixvpu_buffer_bitstream(const struct cixvpu_buffer_bitstream *slot,
    void *out, size_t capacity, size_t *length);
int cixvpu_buffer_frame_return(const void *payload, size_t length,
    const struct cixvpu_buffer_nv12 *expected,
    struct cixvpu_buffer_frame_result *out);
int cixvpu_buffer_bitstream_return(const void *payload, size_t length,
    const struct cixvpu_buffer_bitstream *expected,
    struct cixvpu_buffer_bitstream_result *out);

#endif
