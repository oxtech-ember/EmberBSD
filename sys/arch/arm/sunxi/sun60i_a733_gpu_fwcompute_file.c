/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#include <sys/types.h>
#ifdef _KERNEL
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/systm.h>
#else
#include <errno.h>
#include <stddef.h>
#include <string.h>
#endif

#include "sun60i_a733_gpu_fwcompute_file.h"

_Static_assert(sizeof(struct a733_gpu_fwcompute_file_header) ==
    A733_GPU_FWCOMPUTE_FILE_HEADER, "packed compute file header");
_Static_assert(offsetof(struct a733_gpu_fwcompute_file_header, usc_va) == 56,
    "compute file header alignment");
_Static_assert(A733_GPU_FWCOMPUTE_FILE_MAX >= A733_GPU_FWCOMPUTE_FILE_HEADER,
    "compute file bound");

/* Inclusive ends avoid overflow for spans ending at the type limit. */
static bool
fwfile_overlap(uint64_t a, uint64_t asize, uint64_t b, uint64_t bsize)
{
	return a <= b + bsize - 1 && b <= a + asize - 1;
}

/* Nonzero span below the 40-bit GPU limit, within the given alignment. */
static bool
fwfile_gpu_span(uint64_t va, uint64_t bytes, uint64_t alignment)
{
	return va != 0 && va < (UINT64_C(1) << 40) &&
	    (va & (alignment - 1)) == 0 &&
	    bytes <= (UINT64_C(1) << 40) - va;
}

int
sun60i_a733_gpu_fwcompute_file_parse(const void *data, size_t size,
    uint64_t bvnc, struct a733_gpu_fwcompute_file_spans *out)
{
	struct a733_gpu_fwcompute_file_header header;
	struct {
		uint32_t offset, bytes;
	} span[4];
	uint64_t va[4], alignment[4];
	uint32_t payload;
	size_t i, j;

	if (data == NULL || out == NULL || size < A733_GPU_FWCOMPUTE_FILE_HEADER ||
	    size > A733_GPU_FWCOMPUTE_FILE_MAX)
		return EINVAL;
	memcpy(&header, data, sizeof(header));
	if (header.magic != A733_GPU_FWCOMPUTE_FILE_MAGIC ||
	    header.version != A733_GPU_FWCOMPUTE_FILE_VERSION ||
	    header.header_bytes != A733_GPU_FWCOMPUTE_FILE_HEADER ||
	    header.reserved != 0 || header.bvnc != bvnc)
		return EINVAL;
	payload = header.payload_bytes;
	if ((size_t)payload != size - A733_GPU_FWCOMPUTE_FILE_HEADER)
		return EINVAL;

	span[0].offset = header.usc_offset;
	span[0].bytes = header.usc_bytes;
	span[1].offset = header.pds_offset;
	span[1].bytes = header.pds_bytes;
	span[2].offset = header.stream_offset;
	span[2].bytes = header.stream_bytes;
	span[3].offset = header.state_offset;
	span[3].bytes = A733_GPU_FWCOMPUTE_STATE_BYTES;
	for (i = 0; i < sizeof(span) / sizeof(span[0]); i++) {
		if (span[i].offset > payload || span[i].bytes > payload ||
		    span[i].offset % 4 != 0 ||
		    (i < 3 && (span[i].bytes < 4 || span[i].bytes % 4 != 0)) ||
		    span[i].bytes > payload - span[i].offset)
			return EINVAL;
		for (j = 0; j < i; j++) {
			if (fwfile_overlap(span[i].offset, span[i].bytes,
			    span[j].offset, span[j].bytes))
				return EINVAL;
		}
	}

	va[0] = header.usc_va;
	va[1] = header.pds_va;
	va[2] = header.stream_va;
	alignment[0] = 16;
	alignment[1] = 16;
	alignment[2] = 4;
	for (i = 0; i < 3; i++) {
		if (!fwfile_gpu_span(va[i], span[i].bytes, alignment[i]))
			return EINVAL;
		for (j = 0; j < i; j++) {
			if (fwfile_overlap(va[i], span[i].bytes, va[j],
			    span[j].bytes))
				return EINVAL;
		}
	}
	if (!fwfile_gpu_span(header.context_state_va, 16, 16) ||
	    header.priority > 2 ||
	    fwfile_overlap(header.context_state_va, 16, header.stream_va,
	    span[2].bytes))
		return EINVAL;

	out->usc_offset = header.usc_offset;
	out->usc_bytes = header.usc_bytes;
	out->pds_offset = header.pds_offset;
	out->pds_bytes = header.pds_bytes;
	out->stream_offset = header.stream_offset;
	out->stream_bytes = header.stream_bytes;
	out->state_offset = header.state_offset;
	out->usc_va = header.usc_va;
	out->pds_va = header.pds_va;
	out->stream_va = header.stream_va;
	out->context_state_va = header.context_state_va;
	out->priority = header.priority;
	out->pid = header.pid;
	out->context_id = header.context_id;
	out->job_id = header.job_id;
	return 0;
}

void
sun60i_a733_gpu_fwcompute_file_fwspans(
    struct sun60i_a733_gpu_fwcompute_fwspan *fw)
{
	static const uint32_t sizes[A733_GPU_FWCOMPUTE_OBJECTS] = {
		sizeof(struct rogue_fwif_fwcomputecontext),
		sizeof(struct rogue_fwif_cccb_ctl),
		A733_GPU_FWCOMPUTE_CCCB_SIZE,
		sizeof(struct rogue_fwif_compute_ctx_state),
		sizeof(uint32_t),
		sizeof(struct rogue_fwif_fwmemcontext)
	};
	uint32_t offset = 0;
	size_t i;

	for (i = 0; i < A733_GPU_FWCOMPUTE_OBJECTS; i++) {
		/* Pad each offset to keep every firmware address eight-aligned. */
		offset = (offset + 7U) & ~7U;
		fw[i].addr = A733_GPU_FWCOMPUTE_SPAN_BASE + offset;
		fw[i].size = sizes[i];
		offset += sizes[i];
	}
}
