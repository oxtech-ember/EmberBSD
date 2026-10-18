/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: original bounded firmware layout planner, AI-assisted. */
#ifndef _DEV_IC_CIXVPU_FW_PLAN_H_
#define _DEV_IC_CIXVPU_FW_PLAN_H_

#include "cixvpu_fw.h"
#include "cixvpu_mmu.h"

#define CIXVPU_FW_PLAN_MAX_CORES 4U
#define CIXVPU_FW_PLAN_MAX_PAGES 2048U
#define CIXVPU_FW_PLAN_NO_CORE UINT32_MAX

/* Closed arena: RESIZE_SIZE is mapped before exposure; CAPACITY reserves VA. */
#define CIXVPU_FW_PLAN_BOOT_RPC_ADDRESS UINT32_C(0x20000000)
#define CIXVPU_FW_PLAN_BOOT_RPC_INITIAL_SIZE UINT32_C(0x00020000)
#define CIXVPU_FW_PLAN_BOOT_RPC_CAPACITY UINT32_C(0x04000000)
#define CIXVPU_FW_PLAN_BOOT_RPC_REGION 0U
#define CIXVPU_FW_PLAN_BOOT_RPC_LOG2_ALIGNMENT 22U
#define CIXVPU_FW_PLAN_BOOT_RPC_RESIZE_SIZE UINT32_C(0x00080000)
#define CIXVPU_FW_PLAN_BOOT_RPC_INITIAL_PAGES \
    (CIXVPU_FW_PLAN_BOOT_RPC_INITIAL_SIZE / CIXVPU_FW_PAGE_SIZE)
#define CIXVPU_FW_PLAN_BOOT_RPC_BACKING_PAGES \
    (CIXVPU_FW_PLAN_BOOT_RPC_RESIZE_SIZE / CIXVPU_FW_PAGE_SIZE)
/* Compatibility name for the initial ALLOC request, not physical backing. */
#define CIXVPU_FW_PLAN_BOOT_RPC_PAGES CIXVPU_FW_PLAN_BOOT_RPC_INITIAL_PAGES

/* Exact second object, after the first logical capacity reservation. */
#define CIXVPU_FW_PLAN_BOOT_RPC_SECOND_ADDRESS UINT32_C(0x24000000)
#define CIXVPU_FW_PLAN_BOOT_RPC_SECOND_SIZE UINT32_C(0x00004000)
#define CIXVPU_FW_PLAN_BOOT_RPC_SECOND_LOG2_ALIGNMENT 12U
#define CIXVPU_FW_PLAN_BOOT_RPC_SECOND_PAGES 4U

/* Optional stopped-only local slot layout, not an encoder geometry default. */
#define CIXVPU_FW_PLAN_FRAME_ADDRESS UINT32_C(0x70000000)
#define CIXVPU_FW_PLAN_BITSTREAM_ADDRESS UINT32_C(0x24400000)
struct cixvpu_fw_slot_request {
	uint32_t width, height, stride_y, stride_uv, bitstream_bytes;
};
enum cixvpu_fw_slot_id {
	CIXVPU_FW_SLOT_Y, CIXVPU_FW_SLOT_UV, CIXVPU_FW_SLOT_BITSTREAM
};
struct cixvpu_fw_slot_span {
	uint32_t address, bytes, rounded_bytes, first_backing, pages;
};

enum cixvpu_fw_plan_role {
	CIXVPU_FW_PLAN_TEXT,
	CIXVPU_FW_PLAN_PRIVATE,
	CIXVPU_FW_PLAN_SHARED,
	CIXVPU_FW_PLAN_MSG_HOST,
	CIXVPU_FW_PLAN_MSG_MVE,
	CIXVPU_FW_PLAN_BUF_IN_HOST,
	CIXVPU_FW_PLAN_BUF_IN_MVE,
	CIXVPU_FW_PLAN_BUF_OUT_HOST,
	CIXVPU_FW_PLAN_BUF_OUT_MVE,
	CIXVPU_FW_PLAN_RPC,
	CIXVPU_FW_PLAN_PRINT,
	CIXVPU_FW_PLAN_RPC_DATA,
	CIXVPU_FW_PLAN_ROOT,
	CIXVPU_FW_PLAN_LEAF,
	CIXVPU_FW_PLAN_RPC_SECOND_DATA,
	CIXVPU_FW_PLAN_FRAME_Y,
	CIXVPU_FW_PLAN_FRAME_UV,
	CIXVPU_FW_PLAN_BITSTREAM
};

/* Immutable value, opaque by contract. Getters recheck all layout fields. */
struct cixvpu_fw_plan {
	struct cixvpu_fw_info info;
	uint32_t cores;
	uint32_t boot_rpc;             /* Closed reserve profile: exactly 0 or 1. */
	uint32_t backing_pages;
	uint32_t mappings;
	uint32_t slots_enabled;        /* Exactly0/1; disabled request is all zero. */
	struct cixvpu_fw_slot_request slots;
};

struct cixvpu_fw_plan_page {
	uint32_t backing_index;
	enum cixvpu_fw_plan_role role;
	uint32_t core;                 /* NO_CORE for shared/common/root pages. */
	uint32_t ordinal;              /* Page ordinal within its backing role. */
	uint32_t image_offset;         /* TEXT only; all other pages are zeroed. */
	uint32_t image_bytes;          /* Copy only these bytes, zero the rest. */
	uint32_t leaf_address;         /* LEAF only: aligned 4 MiB coverage base. */
	unsigned int attribute;
	unsigned int permission;
};

struct cixvpu_fw_plan_mapping {
	uint32_t address;              /* Firmware VA, never a DMA bus address. */
	uint32_t backing_index;
	uint32_t leaf_backing_index;
	enum cixvpu_fw_plan_role role;
	unsigned int attribute;
	unsigned int permission;
};

/*
 * Pure protocol-3 layout: contiguous cores 0..cores-1, seven common pages,
 * 128 print pages and root plus cores+1 leaf tables. No memory allocation,
 * firmware copy, DMA address, PTE encoding, device I/O or execution occurs.
 * Call the unchanged parser, then require text at core_base+0x1000 to fit
 * before BSS and inside the 1 MiB core region. Trailing image bytes retain
 * parser semantics. A future loader owns its staging-size limit.
 *
 * Page enumeration is the unique allocation/free owner list. Mapping
 * enumeration may alias TEXT/SHARED backing across cores; never free from
 * that list. Table page attributes describe ROOT's L0 pointer and LEAF's
 * root link. Data page attributes describe leaf PTEs. These attributes do
 * not assert host DMA coherency. The caller supplies validated bus addresses
 * only in a later stopped-device MMU construction stage.
 *
 * Outputs remain unchanged on any error. Treat a successful plan as immutable.
 * Getters reject inconsistent or out-of-range layout fields, including counts;
 * this is not authentication of a caller-modified but self-consistent plan.
 * Return EINVAL for invalid input, E2BIG for the backing-page quota, or
 * ERANGE for an enumeration index outside the validated plan.
 */
int cixvpu_fw_plan_build(const void *, size_t, unsigned int,
    struct cixvpu_fw_plan *);
/*
 * Pure one-core protocol-3.5 closed reserve variant.
 * Adds 128 SHARED_RW/WRITE pages at ADDRESS and four at SECOND_ADDRESS,
 * each arena with a separate PRIVATE/READ leaf.
 * Initial ALLOC remains 128 KiB; measured RESIZE backing is 512 KiB.
 * The complete [ADDRESS, ADDRESS+CAPACITY) interval is logically reserved.
 * The 128-to-512 KiB tail is RW before exposure, unlike reference growth
 * from no-access PTEs. The remaining reserve entries stay invalid/zero, unlike
 * the reference's dummy-PA no-access encoding. No memory allocation, live PTE
 * update, resize/free operation or RPC occurs in this pure planner.
 */
int cixvpu_fw_plan_build_boot_rpc(const void *, size_t, unsigned int,
    struct cixvpu_fw_plan *);
/*
 * Pure one-core protocol3.5 boot_rpc1 slot variant. Positive even dimensions
 * <=65535 and int32 positive strides>=width. Capacity is explicit, not a board
 * default. Y/UV required bytes are stride*rows, rounded separately for storage.
 * Y starts FRAME_ADDRESS; UV follows rounded Y. They fit one4MiB leaf.
 * BITSTREAM starts BITSTREAM_ADDRESS and fits one4MiB leaf. Both exclude the
 * full64MiB RPC logical reserve and second16KiB object. New unique data pages
 * precede ROOT and two leaves follow the existing leaves; the2048-page quota
 * includes them. PTE attributes are SHARED_RW/WRITE, not a host sync assertion.
 * Existing builders retain their indices, counts and mappings with slots off.
 *
 * Neither allocation, input copy, preparation nor sync is added. Existing native
 * consumers reject this new profile; no active attachment or GO selects it.
 * slot returns checked firmware VA metadata, never a KVA, bus address or owner
 * capability. It returns ENOTSUP with slots off, ERANGE for an unknown slot.
 * New builder/slot reject output overlap with the request, plan or inspected
 * image header. The parser still reads only the header, not the claimed extent. All errors preserve out.
 * Getters recheck metadata; this is not caller-pointer authentication.
 */
int cixvpu_fw_plan_build_frame_slots(const void *, size_t, unsigned int,
    const struct cixvpu_fw_slot_request *, struct cixvpu_fw_plan *);
int cixvpu_fw_plan_slot(const struct cixvpu_fw_plan *, unsigned int,
    struct cixvpu_fw_slot_span *);
int cixvpu_fw_plan_page(const struct cixvpu_fw_plan *, unsigned int,
    struct cixvpu_fw_plan_page *);
int cixvpu_fw_plan_mapping(const struct cixvpu_fw_plan *, unsigned int,
    struct cixvpu_fw_plan_mapping *);

#endif
