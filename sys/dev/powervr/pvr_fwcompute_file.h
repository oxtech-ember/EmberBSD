/* Origin: EmberBSD; common version-3 compute image mechanism. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Packed compute-image firmware file, version 3. See a733-gpu-fwcompute.md. */
#ifndef _PVR_FWCOMPUTE_FILE_H
#define _PVR_FWCOMPUTE_FILE_H

#include <sys/types.h>
#ifndef _KERNEL
#include <stdint.h>
#endif

#include "pvr_fwcompute.h"
#include "pvr_fwcompute_cmd.h"

/* "7CM3" in native little-endian byte order. */
#define PVR_FWCOMPUTE_FILE_MAGIC	UINT32_C(0x334d4337)
#define PVR_FWCOMPUTE_FILE_VERSION	3
#define PVR_FWCOMPUTE_FILE_PREFIX	UINT32_C(104)
#define PVR_FWCOMPUTE_ENTRY	UINT32_C(24)
#define PVR_FWCOMPUTE_FILE_MAX	(512 * 1024)
#define PVR_FWCOMPUTE_STATE_BYTES	UINT32_C(56)
#define PVR_FWCOMPUTE_RESOURCES	10
#define PVR_FWCOMPUTE_VM_REGIONS	(PVR_FWCOMPUTE_RESOURCES + 3)
#define PVR_FWCOMPUTE_BORDER_BYTES	UINT32_C(262144)
/* Mesa 26.2.4: align 72 bytes to a 64-byte cache line, then allow 4 cores. */
#define PVR_FWCOMPUTE_RESUME_BYTES	UINT32_C(512)

#define PVR_FWCOMPUTE_FLAG_RO	UINT16_C(0x0001)
#define PVR_FWCOMPUTE_FLAG_BYPASS	UINT16_C(0x0002)

/* Exactly one resource of each role must be present. */
enum pvr_fwcompute_role {
	PVR_FWCOMPUTE_SCRATCH = 0,
	PVR_FWCOMPUTE_RESUME,
	PVR_FWCOMPUTE_IDFWDF_TEXTURE,
	PVR_FWCOMPUTE_IDFWDF_SHAREDS,
	PVR_FWCOMPUTE_CDM_STREAM,
	PVR_FWCOMPUTE_CTX_STATE_A,
	PVR_FWCOMPUTE_CTX_STATE_B,
	PVR_FWCOMPUTE_PDS_PROGRAMS,
	PVR_FWCOMPUTE_USC_PROGRAMS,
	PVR_FWCOMPUTE_BORDER_TABLE
};

/*
 * Declared firmware-window placement for the CPU-side objects. The six
 * objects sit between the firmware code heap end and the fixed config
 * pages. Nothing backs or maps this window until the heap stage runs.
 */
#define PVR_FWCOMPUTE_SPAN_BASE	UINT32_C(0xc0800000)
/* Six eight-aligned objects: 232 + 32 + 32768 + 4 + 4 + 32, padded. */
#define PVR_FWCOMPUTE_SPAN_BYTES	UINT32_C(33080)

/* Wire layouts, little-endian, no implicit padding. */
struct pvr_fwcompute_file_header {
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
struct pvr_fwcompute_file_entry {
	uint64_t va;
	uint32_t offset;
	uint32_t bytes;
	uint16_t role;
	uint16_t flags;
};

/* Validated view of one packed compute image. */
struct pvr_fwcompute_file {
	uint32_t header_bytes;
	uint32_t payload_bytes;
	uint32_t static_offset;
	uint32_t command_offset;
	struct rogue_fwif_cmd_compute command;
	uint64_t context_state_va;
	uint32_t priority, pid, context_id, job_id;
	uint8_t payload_digest[32];
	struct pvr_fwcompute_file_entry
	    resource[PVR_FWCOMPUTE_RESOURCES];
};

/*
 * Validate the structural version-3 layout and profile-specific command
 * for the exact A733 or GE8300 BVNC. This does not qualify shader bytes,
 * relocation, GPU mappings, or a GE producer manifest. The buffer
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
int pvr_fwcompute_file_parse(const void *, size_t, uint64_t,
    struct pvr_fwcompute_file *);

/* Relocate CDM-2 stream PDS addresses into the dual-mapped low page. */
void pvr_fwcompute_file_relocate_stream(uint32_t *, size_t,
    uint64_t);

/* Add the PDS region base to the context-switch address pairs in place. */
void pvr_fwcompute_file_relocate(uint8_t state[56], uint64_t);

/* Deterministic six-object firmware spans for the CPU objects. */
void pvr_fwcompute_file_fwspans(
    struct pvr_fwcompute_fwspan *);

#endif /* _PVR_FWCOMPUTE_FILE_H */
