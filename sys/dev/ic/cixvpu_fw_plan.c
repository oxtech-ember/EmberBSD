/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: original bounded firmware layout planner, AI-assisted. */
#ifdef _KERNEL
#include <sys/null.h>
#include <sys/errno.h>
#else
#include <errno.h>
#endif
#include "cixvpu_fw_plan.h"

/*
 * Layout facts: protocol-3 regions and fw_map_core/map_protocol in the
 * maintained CIX codec, revision 57e018a398248d7e5e4d798610df79a557c0629f.
 * No GPL implementation is copied. In particular, shared BSS uses PRIVATE
 * PTE attributes; only its backing ownership is shared between cores.
 */
#define PLAN_COMMON_PAGES 7U
#define PLAN_PRINT_PAGES 128U
#define PLAN_CORE_STRIDE UINT32_C(0x01000000)
#define PLAN_COMMON_BASE UINT32_C(0x10079000)
#define PLAN_PRINT_BASE UINT32_C(0x10100000)
#define PLAN_COMMON_LEAF UINT32_C(0x10000000)

_Static_assert(CIXVPU_FW_PLAN_BOOT_RPC_INITIAL_SIZE % CIXVPU_FW_PAGE_SIZE == 0 &&
    CIXVPU_FW_PLAN_BOOT_RPC_RESIZE_SIZE % CIXVPU_FW_PAGE_SIZE == 0 &&
    CIXVPU_FW_PLAN_BOOT_RPC_RESIZE_SIZE >= CIXVPU_FW_PLAN_BOOT_RPC_INITIAL_SIZE &&
    CIXVPU_FW_PLAN_BOOT_RPC_RESIZE_SIZE <= CIXVPU_FW_PLAN_BOOT_RPC_CAPACITY &&
    CIXVPU_FW_PLAN_BOOT_RPC_BACKING_PAGES <= 1024,
    "closed RPC backing must fit one leaf and the logical reserve");

/* Recheck metadata on every public call, before any bitmap or index use. */
static int
cixvpu_fw_plan_check(const struct cixvpu_fw_info *info, unsigned int cores,
    uint32_t boot_rpc, struct cixvpu_fw_plan *checked)
{
	uint32_t text_pages, shared_pages, bss_end, first, private_pages = 0;
	uint32_t i;
	uint64_t backing, mappings;

	if (boot_rpc > 1 || (boot_rpc != 0 &&
	    (cores != 1 || info->minor != 5)))
		return EINVAL;
	if (cores == 0 || cores > CIXVPU_FW_PLAN_MAX_CORES || info->major != 3 ||
	    info->text_size < CIXVPU_FW_HEADER_SIZE ||
	    info->text_size > CIXVPU_FW_REGION_SIZE)
		return EINVAL;
	text_pages = (info->text_size - 1) / CIXVPU_FW_PAGE_SIZE + 1;
	if (info->text_pages != text_pages ||
	    info->bss_pages > CIXVPU_FW_BITMAP_WORDS * 32 ||
	    info->bss_address % CIXVPU_FW_PAGE_SIZE != 0 ||
	    info->bss_address < CIXVPU_FW_PAGE_SIZE ||
	    info->bss_address > CIXVPU_FW_REGION_SIZE ||
	    text_pages > (CIXVPU_FW_REGION_SIZE - CIXVPU_FW_PAGE_SIZE) /
	    CIXVPU_FW_PAGE_SIZE ||
	    text_pages > (info->bss_address - CIXVPU_FW_PAGE_SIZE) /
	    CIXVPU_FW_PAGE_SIZE ||
	    info->bss_pages > (CIXVPU_FW_REGION_SIZE - info->bss_address) /
	    CIXVPU_FW_PAGE_SIZE)
		return EINVAL;
	bss_end = info->bss_address + info->bss_pages * CIXVPU_FW_PAGE_SIZE;
	if (info->shared_address % CIXVPU_FW_PAGE_SIZE != 0 ||
	    info->shared_address < info->bss_address ||
	    info->shared_address > bss_end ||
	    info->shared_size > bss_end - info->shared_address)
		return EINVAL;
	shared_pages = info->shared_size == 0 ? 0 :
	    (info->shared_size - 1) / CIXVPU_FW_PAGE_SIZE + 1;
	if (info->shared_pages != shared_pages ||
	    shared_pages > (bss_end - info->shared_address) /
	    CIXVPU_FW_PAGE_SIZE)
		return EINVAL;
	first = (info->shared_address - info->bss_address) / CIXVPU_FW_PAGE_SIZE;
	for (i = 0; i < info->bss_pages; i++) {
		if (i >= first && i - first < shared_pages)
			continue;
		if ((info->bitmap[i / 32] & (UINT32_C(1) << (i % 32))) != 0)
			private_pages++;
	}
	if (info->private_pages != private_pages)
		return EINVAL;
	/* All operands are bounded above. Wide sums also enforce the quota. */
	backing = (uint64_t)text_pages + (uint64_t)cores * private_pages +
	    shared_pages + PLAN_COMMON_PAGES + PLAN_PRINT_PAGES + cores + 2;
	mappings = (uint64_t)cores * (text_pages + private_pages + shared_pages) +
	    PLAN_COMMON_PAGES + PLAN_PRINT_PAGES;
	backing += (uint64_t)boot_rpc * (CIXVPU_FW_PLAN_BOOT_RPC_BACKING_PAGES + 1);
	mappings += (uint64_t)boot_rpc * CIXVPU_FW_PLAN_BOOT_RPC_BACKING_PAGES;
	if (backing > CIXVPU_FW_PLAN_MAX_PAGES || mappings > UINT32_MAX)
		return E2BIG;
	checked->info = *info;
	checked->cores = cores;
	checked->boot_rpc = boot_rpc;
	checked->backing_pages = (uint32_t)backing;
	checked->mappings = (uint32_t)mappings;
	return 0;
}

static int
cixvpu_fw_plan_validate(const struct cixvpu_fw_plan *plan)
{
	struct cixvpu_fw_plan checked;
	int error;

	if (plan == NULL)
		return EINVAL;
	error = cixvpu_fw_plan_check(&plan->info, plan->cores, plan->boot_rpc,
	    &checked);
	if (error != 0)
		return error;
	if (plan->backing_pages != checked.backing_pages ||
	    plan->mappings != checked.mappings)
		return EINVAL;
	return 0;
}

static int
cixvpu_fw_plan_build_profile(const void *image, size_t size,
    unsigned int cores, uint32_t boot_rpc, struct cixvpu_fw_plan *out)
{
	struct cixvpu_fw_info info;
	struct cixvpu_fw_plan checked;
	int error;

	if (out == NULL || cixvpu_fw_parse(image, size, &info) != CIXVPU_FW_OK)
		return EINVAL;
	error = cixvpu_fw_plan_check(&info, cores, boot_rpc, &checked);
	if (error != 0)
		return error;
	*out = checked;
	return 0;
}

int
cixvpu_fw_plan_build(const void *image, size_t size, unsigned int cores,
    struct cixvpu_fw_plan *out)
{

	return cixvpu_fw_plan_build_profile(image, size, cores, 0, out);
}

int
cixvpu_fw_plan_build_boot_rpc(const void *image, size_t size, unsigned int cores,
    struct cixvpu_fw_plan *out)
{

	return cixvpu_fw_plan_build_profile(image, size, cores, 1, out);
}

/* Called only after validation and an index bound check. */
static void
cixvpu_fw_plan_page_get(const struct cixvpu_fw_plan *plan, uint32_t index,
    struct cixvpu_fw_plan_page *out)
{
	const struct cixvpu_fw_info *info = &plan->info;
	struct cixvpu_fw_plan_page page = { 0 };
	uint32_t shared, common, print, rpc, root;

	shared = info->text_pages + plan->cores * info->private_pages;
	common = shared + info->shared_pages;
	print = common + PLAN_COMMON_PAGES;
	rpc = print + PLAN_PRINT_PAGES;
	root = rpc + plan->boot_rpc * CIXVPU_FW_PLAN_BOOT_RPC_BACKING_PAGES;
	page.backing_index = index;
	page.core = CIXVPU_FW_PLAN_NO_CORE;
	page.attribute = CIXVPU_MMU_PRIVATE;
	page.permission = CIXVPU_MMU_WRITE;
	if (index < info->text_pages) {
		page.role = CIXVPU_FW_PLAN_TEXT;
		page.ordinal = index;
		page.image_offset = index * CIXVPU_FW_PAGE_SIZE;
		page.image_bytes = info->text_size - page.image_offset;
		if (page.image_bytes > CIXVPU_FW_PAGE_SIZE)
			page.image_bytes = CIXVPU_FW_PAGE_SIZE;
		page.permission = CIXVPU_MMU_EXECUTE;
	} else if (index < shared) {
		page.role = CIXVPU_FW_PLAN_PRIVATE;
		page.core = (index - info->text_pages) / info->private_pages;
		page.ordinal = (index - info->text_pages) % info->private_pages;
	} else if (index < common) {
		page.role = CIXVPU_FW_PLAN_SHARED;
		page.ordinal = index - shared;
	} else if (index < print) {
		page.role = (enum cixvpu_fw_plan_role)
		    (CIXVPU_FW_PLAN_MSG_HOST + index - common);
		page.attribute = CIXVPU_MMU_COHERENT;
	} else if (index < rpc) {
		page.role = CIXVPU_FW_PLAN_PRINT;
		page.ordinal = index - print;
		page.attribute = CIXVPU_MMU_SHARED_RW;
	} else if (index < root) {
		page.role = CIXVPU_FW_PLAN_RPC_DATA;
		page.ordinal = index - rpc;
		page.attribute = CIXVPU_MMU_SHARED_RW;
	} else if (index == root) {
		page.role = CIXVPU_FW_PLAN_ROOT;
	} else {
		page.role = CIXVPU_FW_PLAN_LEAF;
		page.ordinal = index - root - 1;
		page.permission = CIXVPU_MMU_READ;
		if (page.ordinal < plan->cores) {
			page.core = page.ordinal;
			page.leaf_address = page.core * PLAN_CORE_STRIDE;
		} else if (page.ordinal == plan->cores) {
			page.leaf_address = PLAN_COMMON_LEAF;
		} else {
			page.leaf_address = CIXVPU_FW_PLAN_BOOT_RPC_ADDRESS;
		}
	}
	*out = page;
}

int
cixvpu_fw_plan_page(const struct cixvpu_fw_plan *plan, unsigned int index,
    struct cixvpu_fw_plan_page *out)
{
	int error;

	if (out == NULL)
		return EINVAL;
	error = cixvpu_fw_plan_validate(plan);
	if (error != 0)
		return error;
	if (index >= plan->backing_pages)
		return ERANGE;
	cixvpu_fw_plan_page_get(plan, index, out);
	return 0;
}

int
cixvpu_fw_plan_mapping(const struct cixvpu_fw_plan *plan, unsigned int index,
    struct cixvpu_fw_plan_mapping *out)
{
	struct cixvpu_fw_plan_mapping map = { 0 };
	struct cixvpu_fw_plan_page page;
	const struct cixvpu_fw_info *info;
	uint32_t per_core, core_maps, first_leaf, shared, first, core, local;
	uint32_t i, private_index = 0;
	int error;

	if (out == NULL)
		return EINVAL;
	error = cixvpu_fw_plan_validate(plan);
	if (error != 0)
		return error;
	if (index >= plan->mappings)
		return ERANGE;
	info = &plan->info;
	per_core = info->text_pages + info->private_pages + info->shared_pages;
	core_maps = per_core * plan->cores;
	first_leaf = plan->backing_pages - plan->cores - 1 - plan->boot_rpc;
	shared = info->text_pages + plan->cores * info->private_pages;
	if (index >= core_maps + PLAN_COMMON_PAGES + PLAN_PRINT_PAGES) {
		local = index - core_maps - PLAN_COMMON_PAGES - PLAN_PRINT_PAGES;
		map.backing_index = shared + info->shared_pages +
		    PLAN_COMMON_PAGES + PLAN_PRINT_PAGES + local;
		map.leaf_backing_index = first_leaf + plan->cores + 1;
		map.address = CIXVPU_FW_PLAN_BOOT_RPC_ADDRESS +
		    local * CIXVPU_FW_PAGE_SIZE;
		goto ready;
	}
	if (index >= core_maps) {
		local = index - core_maps;
		map.backing_index = shared + info->shared_pages + local;
		map.leaf_backing_index = first_leaf + plan->cores;
		map.address = local < PLAN_COMMON_PAGES ?
		    PLAN_COMMON_BASE + local * CIXVPU_FW_PAGE_SIZE :
		    PLAN_PRINT_BASE + (local - PLAN_COMMON_PAGES) *
		    CIXVPU_FW_PAGE_SIZE;
		goto ready;
	}
	core = index / per_core;
	local = index % per_core;
	map.leaf_backing_index = first_leaf + core;
	if (local < info->text_pages) {
		map.backing_index = local;
		map.address = core * PLAN_CORE_STRIDE + CIXVPU_FW_PAGE_SIZE +
		    local * CIXVPU_FW_PAGE_SIZE;
		goto ready;
	}
	local -= info->text_pages;
	first = (info->shared_address - info->bss_address) / CIXVPU_FW_PAGE_SIZE;
	for (i = 0; i < info->bss_pages; i++) {
		if (i >= first && i - first < info->shared_pages) {
			map.backing_index = shared + i - first;
		} else if ((info->bitmap[i / 32] &
		    (UINT32_C(1) << (i % 32))) != 0) {
			map.backing_index = info->text_pages +
			    core * info->private_pages + private_index++;
		} else {
			continue;
		}
		if (local == 0) {
			map.address = core * PLAN_CORE_STRIDE + info->bss_address +
			    i * CIXVPU_FW_PAGE_SIZE;
			goto ready;
		}
		local--;
	}
	return EINVAL;
ready:
	cixvpu_fw_plan_page_get(plan, map.backing_index, &page);
	map.role = page.role;
	map.attribute = page.attribute;
	map.permission = page.permission;
	*out = map;
	return 0;
}
