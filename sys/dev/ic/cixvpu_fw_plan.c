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
#define PLAN_LEAF_BYTES UINT32_C(0x00400000)
#define PLAN_FRAME_LEAVES 2U

/* All products and rounded spans are checked before narrowing to uint32_t. */
static int
cixvpu_fw_slots(const struct cixvpu_fw_slot_request *r, uint32_t enabled,
    struct cixvpu_fw_slot_span spans[3])
{
	uint64_t bytes[3];
	unsigned int i;

	if (enabled > 1)
		return EINVAL;
	if (enabled == 0) {
		if (r->width != 0 || r->height != 0 || r->stride_y != 0 ||
		    r->stride_uv != 0 || r->bitstream_bytes != 0)
			return EINVAL;
		for (i = 0; i < 3; i++)
			spans[i] = (struct cixvpu_fw_slot_span){ 0 };
		return 0;
	}
	if (r->width == 0 || r->height == 0 || r->width > 65535 ||
	    r->height > 65535 || (r->width & 1) != 0 ||
	    (r->height & 1) != 0 || r->stride_y < r->width ||
	    r->stride_uv < r->width || r->stride_y > INT32_MAX ||
	    r->stride_uv > INT32_MAX || r->bitstream_bytes == 0)
		return EINVAL;
	bytes[0] = (uint64_t)r->stride_y * r->height;
	bytes[1] = (uint64_t)r->stride_uv * (r->height / 2);
	bytes[2] = r->bitstream_bytes;
	for (i = 0; i < 3; i++) {
		uint64_t rounded;

		rounded = (bytes[i] + CIXVPU_FW_PAGE_SIZE - 1) /
		    CIXVPU_FW_PAGE_SIZE * CIXVPU_FW_PAGE_SIZE;
		if (rounded > PLAN_LEAF_BYTES)
			return E2BIG;
		spans[i] = (struct cixvpu_fw_slot_span){ 0, (uint32_t)bytes[i],
		    (uint32_t)rounded, 0,
		    (uint32_t)(rounded / CIXVPU_FW_PAGE_SIZE) };
	}
	if ((uint64_t)spans[0].rounded_bytes + spans[1].rounded_bytes >
	    (uint64_t)PLAN_FRAME_LEAVES * PLAN_LEAF_BYTES)
		return E2BIG;
	spans[0].address = CIXVPU_FW_PLAN_FRAME_ADDRESS;
	spans[1].address = spans[0].address + spans[0].rounded_bytes;
	spans[2].address = CIXVPU_FW_PLAN_BITSTREAM_ADDRESS;
	return 0;
}

/* Spans have passed the per-plane bounds; disabled slots produce zero leaves. */
static uint32_t
cixvpu_fw_frame_leaves(const struct cixvpu_fw_slot_span spans[3])
{
	uint32_t pages = spans[0].pages + spans[1].pages;

	return (pages + CIXVPU_MMU_ENTRIES - 1) / CIXVPU_MMU_ENTRIES;
}

_Static_assert(CIXVPU_FW_PLAN_BOOT_RPC_INITIAL_SIZE % CIXVPU_FW_PAGE_SIZE == 0 &&
    CIXVPU_FW_PLAN_BOOT_RPC_RESIZE_SIZE % CIXVPU_FW_PAGE_SIZE == 0 &&
    CIXVPU_FW_PLAN_BOOT_RPC_RESIZE_SIZE >= CIXVPU_FW_PLAN_BOOT_RPC_INITIAL_SIZE &&
    CIXVPU_FW_PLAN_BOOT_RPC_RESIZE_SIZE <= CIXVPU_FW_PLAN_BOOT_RPC_CAPACITY &&
    CIXVPU_FW_PLAN_BOOT_RPC_BACKING_PAGES <= 1024,
    "closed RPC backing must fit one leaf and the logical reserve");

_Static_assert(CIXVPU_FW_PLAN_BOOT_RPC_SECOND_ADDRESS ==
    CIXVPU_FW_PLAN_BOOT_RPC_ADDRESS + CIXVPU_FW_PLAN_BOOT_RPC_CAPACITY &&
    CIXVPU_FW_PLAN_BOOT_RPC_SECOND_ADDRESS % (UINT32_C(1) << 22) == 0 &&
    CIXVPU_FW_PLAN_BOOT_RPC_SECOND_SIZE ==
    CIXVPU_FW_PLAN_BOOT_RPC_SECOND_PAGES * CIXVPU_FW_PAGE_SIZE &&
    CIXVPU_FW_PLAN_BOOT_RPC_SECOND_ADDRESS +
    CIXVPU_FW_PLAN_BOOT_RPC_SECOND_SIZE <= UINT32_C(0x70000000),
    "second object must follow the first reserve inside the v3 protected VA");

_Static_assert(CIXVPU_FW_PLAN_FRAME_ADDRESS % PLAN_LEAF_BYTES == 0 &&
    CIXVPU_FW_PLAN_FRAME_ADDRESS >= UINT32_C(0x70000000) &&
    CIXVPU_FW_PLAN_FRAME_ADDRESS < UINT32_C(0xf0000000) &&
    PLAN_FRAME_LEAVES * PLAN_LEAF_BYTES <=
    UINT32_C(0xf0000000) - CIXVPU_FW_PLAN_FRAME_ADDRESS &&
    CIXVPU_FW_PLAN_BITSTREAM_ADDRESS % PLAN_LEAF_BYTES == 0 &&
    CIXVPU_FW_PLAN_BITSTREAM_ADDRESS >=
    CIXVPU_FW_PLAN_BOOT_RPC_ADDRESS + CIXVPU_FW_PLAN_BOOT_RPC_CAPACITY &&
    CIXVPU_FW_PLAN_BITSTREAM_ADDRESS >=
    CIXVPU_FW_PLAN_BOOT_RPC_SECOND_ADDRESS + CIXVPU_FW_PLAN_BOOT_RPC_SECOND_SIZE &&
    CIXVPU_FW_PLAN_BITSTREAM_ADDRESS < UINT32_C(0x70000000) &&
    PLAN_LEAF_BYTES <= UINT32_C(0x70000000) - CIXVPU_FW_PLAN_BITSTREAM_ADDRESS,
    "closed slot leaves must fit their apertures and exclude both RPC objects");

/* Recheck metadata on every public call, before any bitmap or index use. */
static int
cixvpu_fw_plan_check(const struct cixvpu_fw_info *info, unsigned int cores,
    uint32_t boot_rpc, uint32_t slots_enabled,
    const struct cixvpu_fw_slot_request *slots, struct cixvpu_fw_plan *checked)
{
	struct cixvpu_fw_slot_span spans[3];
	uint32_t text_pages, shared_pages, bss_end, first, private_pages = 0;
	uint32_t i;
	uint64_t backing, mappings;
	int error;

	error = cixvpu_fw_slots(slots, slots_enabled, spans);
	if (error != 0)
		return error;
	if (slots_enabled != 0 && boot_rpc != 1)
		return EINVAL;
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
	backing += (uint64_t)boot_rpc * (CIXVPU_FW_PLAN_BOOT_RPC_BACKING_PAGES +
	    CIXVPU_FW_PLAN_BOOT_RPC_SECOND_PAGES + 2);
	mappings += (uint64_t)boot_rpc * (CIXVPU_FW_PLAN_BOOT_RPC_BACKING_PAGES +
	    CIXVPU_FW_PLAN_BOOT_RPC_SECOND_PAGES);
	for (i = 0; i < 3; i++) {
		backing += spans[i].pages;
		mappings += spans[i].pages;
	}
	backing += slots_enabled + cixvpu_fw_frame_leaves(spans);
	if (backing > CIXVPU_FW_PLAN_MAX_PAGES || mappings > UINT32_MAX)
		return E2BIG;
	checked->info = *info;
	checked->cores = cores;
	checked->boot_rpc = boot_rpc;
	checked->backing_pages = (uint32_t)backing;
	checked->mappings = (uint32_t)mappings;
	checked->slots_enabled = slots_enabled;
	checked->slots = *slots;
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
	    plan->slots_enabled, &plan->slots, &checked);
	if (error != 0)
		return error;
	if (plan->backing_pages != checked.backing_pages ||
	    plan->mappings != checked.mappings)
		return EINVAL;
	return 0;
}

static int
cixvpu_fw_plan_build_profile(const void *image, size_t size,
    unsigned int cores, uint32_t boot_rpc,
    const struct cixvpu_fw_slot_request *slots, struct cixvpu_fw_plan *out)
{
	const struct cixvpu_fw_slot_request empty = { 0 };
	struct cixvpu_fw_info info;
	struct cixvpu_fw_plan checked;
	int error;

	if (out == NULL || cixvpu_fw_parse(image, size, &info) != CIXVPU_FW_OK)
		return EINVAL;
	error = cixvpu_fw_plan_check(&info, cores, boot_rpc, slots != NULL,
	    slots != NULL ? slots : &empty, &checked);
	if (error != 0)
		return error;
	*out = checked;
	return 0;
}

int
cixvpu_fw_plan_build(const void *image, size_t size, unsigned int cores,
    struct cixvpu_fw_plan *out)
{

	return cixvpu_fw_plan_build_profile(image, size, cores, 0, NULL, out);
}

int
cixvpu_fw_plan_build_boot_rpc(const void *image, size_t size, unsigned int cores,
    struct cixvpu_fw_plan *out)
{

	return cixvpu_fw_plan_build_profile(image, size, cores, 1, NULL, out);
}

static int
cixvpu_fw_plan_overlap(const void *a, size_t as, const void *b, size_t bs)
{
	uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;

	return x <= y ? y - x < as : x - y < bs;
}

int
cixvpu_fw_plan_build_frame_slots(const void *image, size_t size,
    unsigned int cores, const struct cixvpu_fw_slot_request *request,
    struct cixvpu_fw_plan *out)
{

	if (image == NULL || request == NULL || out == NULL ||
	    CIXVPU_FW_HEADER_SIZE > UINTPTR_MAX - (uintptr_t)image ||
	    sizeof(*request) > UINTPTR_MAX - (uintptr_t)request ||
	    sizeof(*out) > UINTPTR_MAX - (uintptr_t)out ||
	    cixvpu_fw_plan_overlap(out, sizeof(*out), image,
	    CIXVPU_FW_HEADER_SIZE) ||
	    cixvpu_fw_plan_overlap(out, sizeof(*out), request, sizeof(*request)))
		return EINVAL;
	return cixvpu_fw_plan_build_profile(image, size, cores, 1, request, out);
}

int
cixvpu_fw_plan_slot(const struct cixvpu_fw_plan *plan, unsigned int id,
    struct cixvpu_fw_slot_span *out)
{
	struct cixvpu_fw_slot_span spans[3];
	uint32_t first;
	unsigned int i;
	int error;

	if (plan == NULL || out == NULL ||
	    sizeof(*out) > UINTPTR_MAX - (uintptr_t)out ||
	    sizeof(*plan) > UINTPTR_MAX - (uintptr_t)plan ||
	    cixvpu_fw_plan_overlap(plan, sizeof(*plan), out, sizeof(*out)))
		return EINVAL;
	error = cixvpu_fw_plan_validate(plan);
	if (error != 0)
		return error;
	if (plan->slots_enabled == 0)
		return ENOTSUP;
	if (id > CIXVPU_FW_SLOT_BITSTREAM)
		return ERANGE;
	(void)cixvpu_fw_slots(&plan->slots, plan->slots_enabled, spans);
	first = plan->info.text_pages + plan->cores * plan->info.private_pages +
	    plan->info.shared_pages + PLAN_COMMON_PAGES + PLAN_PRINT_PAGES +
	    CIXVPU_FW_PLAN_BOOT_RPC_BACKING_PAGES +
	    CIXVPU_FW_PLAN_BOOT_RPC_SECOND_PAGES;
	for (i = 0; i < id; i++)
		first += spans[i].pages;
	spans[id].first_backing = first;
	*out = spans[id];
	return 0;
}

/* Called only after validation and an index bound check. */
static void
cixvpu_fw_plan_page_get(const struct cixvpu_fw_plan *plan, uint32_t index,
    struct cixvpu_fw_plan_page *out)
{
	const struct cixvpu_fw_info *info = &plan->info;
	struct cixvpu_fw_plan_page page = { 0 };
	struct cixvpu_fw_slot_span spans[3];
	uint32_t shared, common, print, rpc, second, slots, root;

	shared = info->text_pages + plan->cores * info->private_pages;
	common = shared + info->shared_pages;
	print = common + PLAN_COMMON_PAGES;
	rpc = print + PLAN_PRINT_PAGES;
	second = rpc + plan->boot_rpc * CIXVPU_FW_PLAN_BOOT_RPC_BACKING_PAGES;
	slots = second + plan->boot_rpc * CIXVPU_FW_PLAN_BOOT_RPC_SECOND_PAGES;
	(void)cixvpu_fw_slots(&plan->slots, plan->slots_enabled, spans);
	root = slots + spans[0].pages + spans[1].pages + spans[2].pages;
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
	} else if (index < second) {
		page.role = CIXVPU_FW_PLAN_RPC_DATA;
		page.ordinal = index - rpc;
		page.attribute = CIXVPU_MMU_SHARED_RW;
	} else if (index < slots) {
		page.role = CIXVPU_FW_PLAN_RPC_SECOND_DATA;
		page.ordinal = index - second;
		page.attribute = CIXVPU_MMU_SHARED_RW;
	} else if (index < root) {
		uint32_t local, i;

		local = index - slots;
		for (i = 0; i < 2 && local >= spans[i].pages; i++)
			local -= spans[i].pages;
		page.role = i == 0 ? CIXVPU_FW_PLAN_FRAME_Y :
		    i == 1 ? CIXVPU_FW_PLAN_FRAME_UV : CIXVPU_FW_PLAN_BITSTREAM;
		page.ordinal = local;
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
		} else if (page.ordinal == plan->cores + 1) {
			page.leaf_address = CIXVPU_FW_PLAN_BOOT_RPC_ADDRESS;
		} else if (page.ordinal == plan->cores + 2) {
			page.leaf_address = CIXVPU_FW_PLAN_BOOT_RPC_SECOND_ADDRESS;
		} else if (page.ordinal - plan->cores - 3 <
		    cixvpu_fw_frame_leaves(spans)) {
			page.leaf_address = CIXVPU_FW_PLAN_FRAME_ADDRESS +
			    (page.ordinal - plan->cores - 3) * PLAN_LEAF_BYTES;
		} else {
			page.leaf_address = CIXVPU_FW_PLAN_BITSTREAM_ADDRESS;
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
	struct cixvpu_fw_slot_span spans[3];
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
	(void)cixvpu_fw_slots(&plan->slots, plan->slots_enabled, spans);
	first_leaf = plan->backing_pages - plan->cores - 1 - 2 * plan->boot_rpc -
	    plan->slots_enabled - cixvpu_fw_frame_leaves(spans);
	shared = info->text_pages + plan->cores * info->private_pages;
	first = core_maps + PLAN_COMMON_PAGES + PLAN_PRINT_PAGES +
	    plan->boot_rpc * (CIXVPU_FW_PLAN_BOOT_RPC_BACKING_PAGES +
	    CIXVPU_FW_PLAN_BOOT_RPC_SECOND_PAGES);
	if (plan->slots_enabled != 0 && index >= first) {
		local = index - first;
		for (i = 0; i < 2 && local >= spans[i].pages; i++)
			local -= spans[i].pages;
		map.backing_index = shared + info->shared_pages +
		    PLAN_COMMON_PAGES + PLAN_PRINT_PAGES +
		    CIXVPU_FW_PLAN_BOOT_RPC_BACKING_PAGES +
		    CIXVPU_FW_PLAN_BOOT_RPC_SECOND_PAGES + index - first;
		map.address = spans[i].address + local * CIXVPU_FW_PAGE_SIZE;
		map.leaf_backing_index = first_leaf + plan->cores + 3 +
		    (i == 2 ? cixvpu_fw_frame_leaves(spans) :
		    (map.address - CIXVPU_FW_PLAN_FRAME_ADDRESS) / PLAN_LEAF_BYTES);
		goto ready;
	}
	if (index >= core_maps + PLAN_COMMON_PAGES + PLAN_PRINT_PAGES) {
		local = index - core_maps - PLAN_COMMON_PAGES - PLAN_PRINT_PAGES;
		map.backing_index = shared + info->shared_pages +
		    PLAN_COMMON_PAGES + PLAN_PRINT_PAGES + local;
		map.leaf_backing_index = first_leaf + plan->cores + 1;
		if (local < CIXVPU_FW_PLAN_BOOT_RPC_BACKING_PAGES) {
			map.address = CIXVPU_FW_PLAN_BOOT_RPC_ADDRESS +
			    local * CIXVPU_FW_PAGE_SIZE;
		} else {
			map.leaf_backing_index++;
			map.address = CIXVPU_FW_PLAN_BOOT_RPC_SECOND_ADDRESS +
			    (local - CIXVPU_FW_PLAN_BOOT_RPC_BACKING_PAGES) *
			    CIXVPU_FW_PAGE_SIZE;
		}
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
