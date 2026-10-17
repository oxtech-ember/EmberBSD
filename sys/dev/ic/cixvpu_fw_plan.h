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
	CIXVPU_FW_PLAN_ROOT,
	CIXVPU_FW_PLAN_LEAF
};

/* Immutable value, opaque by contract. Getters recheck all layout fields. */
struct cixvpu_fw_plan {
	struct cixvpu_fw_info info;
	uint32_t cores;
	uint32_t backing_pages;
	uint32_t mappings;
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
int cixvpu_fw_plan_page(const struct cixvpu_fw_plan *, unsigned int,
    struct cixvpu_fw_plan_page *);
int cixvpu_fw_plan_mapping(const struct cixvpu_fw_plan *, unsigned int,
    struct cixvpu_fw_plan_mapping *);

#endif
