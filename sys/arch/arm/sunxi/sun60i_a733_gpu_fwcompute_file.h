/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Packed compute-image firmware file, version 3. See a733-gpu-fwcompute.md. */
#ifndef _SUN60I_A733_GPU_FWCOMPUTE_FILE_H
#define _SUN60I_A733_GPU_FWCOMPUTE_FILE_H

#include <sys/types.h>
#ifndef _KERNEL
#include <stdint.h>
#endif

#include "sun60i_a733_gpu_fwcompute.h"
#include "sun60i_a733_gpu_fwcompute_cmd.h"

/* "7CM3" in native little-endian byte order. */
#define A733_GPU_FWCOMPUTE_FILE_MAGIC	UINT32_C(0x334d4337)
#define A733_GPU_FWCOMPUTE_FILE_VERSION	3
#define A733_GPU_FWCOMPUTE_FILE_PREFIX	UINT32_C(104)
#define A733_GPU_FWCOMPUTE_ENTRY	UINT32_C(24)
#define A733_GPU_FWCOMPUTE_FILE_MAX	(512 * 1024)
#define A733_GPU_FWCOMPUTE_STATE_BYTES	UINT32_C(56)
#define A733_GPU_FWCOMPUTE_RESOURCES	10
#define A733_GPU_FWCOMPUTE_VM_REGIONS	(A733_GPU_FWCOMPUTE_RESOURCES + 3)
#define A733_GPU_FWCOMPUTE_BORDER_BYTES	UINT32_C(262144)
/* Mesa 26.2.4: align 72 bytes to a 64-byte cache line, then allow 4 cores. */
#define A733_GPU_FWCOMPUTE_RESUME_BYTES	UINT32_C(512)

#define A733_GPU_FWCOMPUTE_FLAG_RO	UINT16_C(0x0001)
#define A733_GPU_FWCOMPUTE_FLAG_BYPASS	UINT16_C(0x0002)

/* Exactly one resource of each role must be present. */
enum a733_gpu_fwcompute_role {
	A733_GPU_FWCOMPUTE_SCRATCH = 0,
	A733_GPU_FWCOMPUTE_RESUME,
	A733_GPU_FWCOMPUTE_IDFWDF_TEXTURE,
	A733_GPU_FWCOMPUTE_IDFWDF_SHAREDS,
	A733_GPU_FWCOMPUTE_CDM_STREAM,
	A733_GPU_FWCOMPUTE_CTX_STATE_A,
	A733_GPU_FWCOMPUTE_CTX_STATE_B,
	A733_GPU_FWCOMPUTE_PDS_PROGRAMS,
	A733_GPU_FWCOMPUTE_USC_PROGRAMS,
	A733_GPU_FWCOMPUTE_BORDER_TABLE
};

/*
 * Declared firmware-window placement for the CPU-side objects. The six
 * objects sit between the firmware code heap end and the fixed config
 * pages. Nothing backs or maps this window until the heap stage runs.
 */
#define A733_GPU_FWCOMPUTE_SPAN_BASE	UINT32_C(0xc0800000)
/* Six eight-aligned objects: 232 + 32 + 32768 + 4 + 4 + 32, padded. */
#define A733_GPU_FWCOMPUTE_SPAN_BYTES	UINT32_C(33080)

/* Wire layouts, little-endian, no implicit padding. */
struct a733_gpu_fwcompute_file_header {
	uint32_t magic;
	uint32_t version;
	uint64_t bvnc;
	uint32_t payload_bytes;
	uint32_t header_bytes;
	uint32_t resources;
	uint32_t static_offset;
	uint32_t static_bytes;
	uint32_t priority;
	uint32_t pid;
	uint32_t context_id;
	uint32_t job_id;
	uint32_t reserved;
	uint64_t context_state_va;
	uint8_t payload_digest[32];
	uint32_t command_offset;
	uint32_t command_bytes;
};
struct a733_gpu_fwcompute_file_entry {
	uint64_t va;
	uint32_t offset;
	uint32_t bytes;
	uint16_t role;
	uint16_t flags;
};

/* Validated view of one packed compute image. */
struct a733_gpu_fwcompute_file {
	uint32_t header_bytes;
	uint32_t payload_bytes;
	uint32_t static_offset;
	uint32_t command_offset;
	struct rogue_fwif_cmd_compute command;
	uint64_t context_state_va;
	uint32_t priority, pid, context_id, job_id;
	uint8_t payload_digest[32];
	struct a733_gpu_fwcompute_file_entry
	    resource[A733_GPU_FWCOMPUTE_RESOURCES];
};

/*
 * Validate one complete version-3 file for the expected BVNC. The buffer
 * needs no alignment. Requirements: the exact ten roles once each, four-
 * byte-aligned nonoverlapping payload spans, four-kilobyte-aligned virtual
 * addresses below 40 bits with page-disjoint ranges, a 56-byte static
 * state, and a nonzero multiple-of-four length for every resource. The
 * context-state address must equal the RESUME resource base. That resource
 * must be writable, SLC-bypassed, and at least RESUME_BYTES long. The
 * read-only border table holds all 64 Mesa entries. Decode the 48-byte
 * command payload and bind its three addresses to the resource roles. The
 * payload digest is copied out, not verified here. Return positive errno;
 * leave *out untouched on every error.
 */
int sun60i_a733_gpu_fwcompute_file_parse(const void *, size_t, uint64_t,
    struct a733_gpu_fwcompute_file *);

/* Relocate CDM-2 stream PDS addresses into the dual-mapped low page. */
void sun60i_a733_gpu_fwcompute_file_relocate_stream(uint32_t *, size_t,
    uint64_t);

/* Add the PDS region base to the context-switch address pairs in place. */
void sun60i_a733_gpu_fwcompute_file_relocate(uint8_t state[56], uint64_t);

/* Deterministic six-object firmware spans for the CPU objects. */
void sun60i_a733_gpu_fwcompute_file_fwspans(
    struct sun60i_a733_gpu_fwcompute_fwspan *);

#endif /* _SUN60I_A733_GPU_FWCOMPUTE_FILE_H */
