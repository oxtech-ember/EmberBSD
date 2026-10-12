/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Packed compute-image firmware file. See ember/boot/a733-gpu-fwcompute.md. */
#ifndef _SUN60I_A733_GPU_FWCOMPUTE_FILE_H
#define _SUN60I_A733_GPU_FWCOMPUTE_FILE_H

#include <sys/types.h>
#ifndef _KERNEL
#include <stdint.h>
#endif

#include "sun60i_a733_gpu_fwcompute.h"

/* "7CM1" in native little-endian byte order. */
#define A733_GPU_FWCOMPUTE_FILE_MAGIC	UINT32_C(0x314d4337)
#define A733_GPU_FWCOMPUTE_FILE_VERSION	1
#define A733_GPU_FWCOMPUTE_FILE_HEADER	UINT32_C(104)
#define A733_GPU_FWCOMPUTE_FILE_MAX	(256 * 1024)
#define A733_GPU_FWCOMPUTE_STATE_BYTES	UINT32_C(56)

/*
 * Declared firmware-window placement for CPU-only serialization. The six
 * objects sit between the firmware code heap end and the fixed config
 * pages. Nothing backs or maps this window yet; publication needs a real
 * firmware-heap and page-table stage. Keep both producers identical.
 */
#define A733_GPU_FWCOMPUTE_SPAN_BASE	UINT32_C(0xc0800000)
/* Six eight-aligned objects: 232 + 32 + 32768 + 4 + 4 + 32, padded. */
#define A733_GPU_FWCOMPUTE_SPAN_BYTES	UINT32_C(33080)

/* Wire layout, little-endian, no implicit padding. */
struct a733_gpu_fwcompute_file_header {
	uint32_t magic;
	uint32_t version;
	uint64_t bvnc;
	uint32_t payload_bytes;
	uint32_t header_bytes;
	uint32_t usc_offset, usc_bytes;
	uint32_t pds_offset, pds_bytes;
	uint32_t stream_offset, stream_bytes;
	uint32_t state_offset;
	uint32_t reserved;
	uint64_t usc_va;
	uint64_t pds_va;
	uint64_t stream_va;
	uint64_t context_state_va;
	uint32_t priority;
	uint32_t pid;
	uint32_t context_id;
	uint32_t job_id;
};

/* Validated view of one packed compute image. */
struct a733_gpu_fwcompute_file_spans {
	uint32_t usc_offset, usc_bytes;
	uint32_t pds_offset, pds_bytes;
	uint32_t stream_offset, stream_bytes;
	uint32_t state_offset;
	uint64_t usc_va, pds_va, stream_va, context_state_va;
	uint32_t priority, pid, context_id, job_id;
};

/*
 * Validate one complete file image for the expected BVNC and return its
 * spans. The buffer needs no alignment. Every span stays inside the
 * payload, starts four-byte aligned, and keeps a nonzero multiple-of-four
 * length; the state span holds exactly 56 bytes. Program GPU addresses
 * stay nonzero, below 40 bits, distinct, and correctly aligned. Return
 * positive errno; leave *out untouched on every error.
 */
int sun60i_a733_gpu_fwcompute_file_parse(const void *, size_t, uint64_t,
    struct a733_gpu_fwcompute_file_spans *);

/* Deterministic six-object firmware spans for CPU-only serialization. */
void sun60i_a733_gpu_fwcompute_file_fwspans(
    struct sun60i_a733_gpu_fwcompute_fwspan *);

#endif /* _SUN60I_A733_GPU_FWCOMPUTE_FILE_H */
