/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. AI-assisted source contract. */
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arm/sunxi/powervr/pvr_rogue_fwif.h>
#include <arm/sunxi/powervr/pvr_rogue_fwif_client.h>
#include <arm/sunxi/powervr/pvr_rogue_fwif_shared_check.h>
#include <arm/sunxi/powervr/pvr_rogue_fwif_client_check.h>
#include "pvr_stream.h"

static unsigned checks;
#define CHECK(x) do { checks++; if (!(x)) { \
	fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); abort(); \
} } while (0)

static void
put32(void *where, uint32_t value)
{
	uint8_t *p = where;
	unsigned i;

	for (i = 0; i < 4; i++)
		p[i] = (uint8_t)(value >> (i * 8));
}

static void
put64(void *where, uint64_t value)
{
	uint8_t *p = where;

	put32(p, (uint32_t)value);
	put32(p + 4, (uint32_t)(value >> 32));
}

static void
header(uint8_t *p, size_t size)
{
	memset(p, 0, size);
	put32(p, (uint32_t)size);
}

static void
compute(void)
{
	struct {
		uint64_t before;
		struct rogue_fwif_cmd_compute out;
		uint64_t after;
	} guarded, saved;
	uint8_t storage[PVR_STREAM_COMPUTE_SIZE + 9], expected[96], input[sizeof(storage)];
	uint8_t *p = storage + 1;
	uint32_t flags, bit;
	size_t n;

	memset(storage, 0x7d, sizeof(storage));
	header(p, PVR_STREAM_COMPUTE_SIZE);
	put64(p + 8, UINT64_C(0x0102030405060708));
	put64(p + 16, UINT64_C(0x1112131415161718));
	put64(p + 24, UINT64_C(0x2122232425262728));
	put32(p + 32, UINT32_C(0x31323334));
	put32(p + 36, UINT32_C(0x41424344));
	memcpy(input, storage, sizeof(input));
	for (flags = 0; flags < 4; flags++) {
		memset(&guarded, 0xa5, sizeof(guarded));
		memset(expected, 0, sizeof(expected));
		/* Pinned FWIF client_check offsets, independent of decoder fields. */
		memcpy(expected + 8, p + 8, 8);
		memcpy(expected + 40, p + 16, 8);
		memcpy(expected + 48, p + 24, 8);
		memcpy(expected + 60, p + 32, 4);
		memcpy(expected + 72, p + 36, 4);
		put32(expected + 80, ((flags & 1) ? 4U : 0) |
		    ((flags & 2) ? 32U : 0));
		CHECK(pvr_stream_compute(PVR_STREAM_BVNC, p, 44, flags,
		    &guarded.out) == 0);
		CHECK(memcmp(&guarded.out, expected, sizeof(expected)) == 0);
		CHECK(guarded.before == UINT64_C(0xa5a5a5a5a5a5a5a5));
		CHECK(guarded.after == guarded.before);
		CHECK(memcmp(storage, input, sizeof(storage)) == 0);
	}
	memset(&guarded, 0xa5, sizeof(guarded));
	memcpy(&saved, &guarded, sizeof(saved));
	for (n = 0; n < 44; n++) {
		CHECK(pvr_stream_compute(PVR_STREAM_BVNC, p, n, 0,
		    &guarded.out) == EINVAL);
		CHECK(memcmp(&guarded, &saved, sizeof(saved)) == 0);
	}
	CHECK(pvr_stream_compute(PVR_STREAM_BVNC, p, 48, 0,
	    &guarded.out) == EOPNOTSUPP);
	CHECK(pvr_stream_compute(PVR_STREAM_BVNC, p, SIZE_MAX, 0,
	    &guarded.out) == EOPNOTSUPP);
	CHECK(pvr_stream_compute(PVR_STREAM_BVNC ^ 1, p, 44, 0,
	    &guarded.out) == EOPNOTSUPP);
	CHECK(pvr_stream_compute(PVR_STREAM_BVNC, NULL, 44, 0,
	    &guarded.out) == EINVAL);
	CHECK(pvr_stream_compute(PVR_STREAM_BVNC, p, 44, 0, NULL) == EINVAL);
	for (bit = 2; bit < 32; bit++) {
		CHECK(pvr_stream_compute(PVR_STREAM_BVNC, p, 44, 1U << bit,
		    &guarded.out) == EOPNOTSUPP);
		CHECK(memcmp(&guarded, &saved, sizeof(saved)) == 0);
	}
	put32(p + 40, 1);
	CHECK(pvr_stream_compute(PVR_STREAM_BVNC, p, 44, 0,
	    &guarded.out) == EOPNOTSUPP);
	put32(p + 40, 0);
	put32(p + 4, 1);
	CHECK(pvr_stream_compute(PVR_STREAM_BVNC, p, 44, 0,
	    &guarded.out) == EINVAL);
	put32(p + 4, 0);
	put32(p, 40);
	CHECK(pvr_stream_compute(PVR_STREAM_BVNC, p, 44, 0,
	    &guarded.out) == EINVAL);
	CHECK(memcmp(&guarded, &saved, sizeof(saved)) == 0);
}

static void
static_contexts(void)
{
	struct rogue_fwif_cdm_registers_cswitch compute_out, compute_saved;
	struct {
		uint64_t before;
		struct rogue_fwif_static_rendercontext_state out;
		uint64_t after;
	} render_out, render_saved;
	/* The wire stores tasks 0..4 before resumes 0..4. The FW struct does not. */
	static const unsigned offsets[23] = {
		0, 8, 16,
		24, 32, 40, 72, 80, 48, 56, 64, 88, 96,
		104, 112, 120, 152, 160, 128, 136, 144, 168, 176,
	};
	uint8_t storage[201], expected[368], copy[sizeof(storage)];
	uint8_t *p = storage + 1;
	size_t i, n;

	memset(storage, 0x7d, sizeof(storage));
	header(p, 64);
	for (i = 0; i < 7; i++)
		put64(p + 8 + i * 8, UINT64_C(0xf102030405060700) + i);
	memcpy(copy, storage, sizeof(copy));
	memset(&compute_out, 0xa5, sizeof(compute_out));
	CHECK(pvr_stream_static_compute(PVR_STREAM_BVNC, p, 64,
	    &compute_out) == 0);
	CHECK(memcmp(&compute_out, p + 8, 56) == 0);
	CHECK(memcmp(storage, copy, sizeof(storage)) == 0);
	memset(&compute_out, 0xa5, sizeof(compute_out));
	memcpy(&compute_saved, &compute_out, sizeof(compute_saved));
	for (n = 0; n < 64; n++) {
		CHECK(pvr_stream_static_compute(PVR_STREAM_BVNC, p, n,
		    &compute_out) == EINVAL);
		CHECK(memcmp(&compute_out, &compute_saved, sizeof(compute_out)) == 0);
	}
	CHECK(pvr_stream_static_compute(PVR_STREAM_BVNC, p, 72,
	    &compute_out) == EOPNOTSUPP);
	CHECK(pvr_stream_static_compute(PVR_STREAM_BVNC ^ 1, p, 64,
	    &compute_out) == EOPNOTSUPP);
	CHECK(pvr_stream_static_compute(PVR_STREAM_BVNC, NULL, 64,
	    &compute_out) == EINVAL);
	CHECK(pvr_stream_static_compute(PVR_STREAM_BVNC, p, 64, NULL) == EINVAL);
	put32(p + 4, 1);
	CHECK(pvr_stream_static_compute(PVR_STREAM_BVNC, p, 64,
	    &compute_out) == EINVAL);
	put32(p + 4, 0); put32(p, 56);
	CHECK(pvr_stream_static_compute(PVR_STREAM_BVNC, p, 64,
	    &compute_out) == EINVAL);
	CHECK(memcmp(&compute_out, &compute_saved, sizeof(compute_out)) == 0);

	header(p, 192);
	memset(expected, 0, sizeof(expected));
	for (i = 0; i < 23; i++) {
		put64(p + 8 + i * 8, UINT64_C(0xf102030405060700) + i);
		put64(expected + offsets[i], UINT64_C(0xf102030405060700) + i);
	}
	memcpy(copy, storage, sizeof(copy));
	memset(&render_out, 0xa5, sizeof(render_out));
	CHECK(pvr_stream_static_render(PVR_STREAM_BVNC, p, 192,
	    &render_out.out) == 0);
	CHECK(memcmp(&render_out.out, expected, sizeof(expected)) == 0);
	CHECK(render_out.before == UINT64_C(0xa5a5a5a5a5a5a5a5));
	CHECK(render_out.after == render_out.before);
	CHECK(memcmp(storage, copy, sizeof(storage)) == 0);
	memset(&render_out, 0xa5, sizeof(render_out));
	memcpy(&render_saved, &render_out, sizeof(render_saved));
	for (n = 0; n < 192; n++) {
		CHECK(pvr_stream_static_render(PVR_STREAM_BVNC, p, n,
		    &render_out.out) == EINVAL);
		CHECK(memcmp(&render_out, &render_saved, sizeof(render_out)) == 0);
	}
	CHECK(pvr_stream_static_render(PVR_STREAM_BVNC, p, 200,
	    &render_out.out) == EOPNOTSUPP);
	CHECK(pvr_stream_static_render(PVR_STREAM_BVNC ^ 1, p, 192,
	    &render_out.out) == EOPNOTSUPP);
	CHECK(pvr_stream_static_render(PVR_STREAM_BVNC, NULL, 192,
	    &render_out.out) == EINVAL);
	CHECK(pvr_stream_static_render(PVR_STREAM_BVNC, p, 192, NULL) == EINVAL);
	put32(p + 4, 1);
	CHECK(pvr_stream_static_render(PVR_STREAM_BVNC, p, 192,
	    &render_out.out) == EINVAL);
	put32(p + 4, 0); put32(p, 184);
	CHECK(pvr_stream_static_render(PVR_STREAM_BVNC, p, 192,
	    &render_out.out) == EINVAL);
	CHECK(memcmp(&render_out, &render_saved, sizeof(render_out)) == 0);
}

int
main(void)
{
	compute();
	static_contexts();
	CHECK(pvr_stream_static_transfer(PVR_STREAM_BVNC, NULL, 0) == 0);
	CHECK(pvr_stream_static_transfer(PVR_STREAM_BVNC, "ignored", 0) == 0);
	CHECK(pvr_stream_static_transfer(PVR_STREAM_BVNC, NULL, 1) == EINVAL);
	CHECK(pvr_stream_static_transfer(PVR_STREAM_BVNC ^ 1, NULL, 0) == EOPNOTSUPP);
	printf("PASS: %u native stream checks; copied bytes, not GPU execution\n", checks);
	return 0;
}
