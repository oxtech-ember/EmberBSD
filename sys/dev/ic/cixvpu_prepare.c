/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: original checked CPU-only firmware preparation, AI-assisted. */
#include <sys/param.h>
#include <sys/cpu.h>
#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/sha2.h>
#include <sys/systm.h>
#include <dev/ic/cixvpu_prepare.h>
#include <dev/ic/cixvpu_ring.h>
#include <dev/ic/cixvpu_dma_private.h>

#define PREPARE_TABLES (CIXVPU_FW_PLAN_MAX_CORES + 3U)

struct cixvpu_prepare_entry {
	uint32_t table, backing, address, word;
	unsigned int attribute, permission;
	bool link;
};

struct cixvpu_prepare_boot {
	bool enabled, published;
	bool switched_in, pong;
	int error;
	uint16_t cursor;
	struct cixvpu_ring_checksum checksum;
	struct cixvpu_dma_boot_snapshot snapshot;
};

struct cixvpu_prepare_tx {
	uint16_t read, write;
	struct cixvpu_ring_checksum checksum;
};
struct cixvpu_prepare_queue {
	bool enabled, held, output_eof;
	struct cixvpu_queue_identity identity;
	struct cixvpu_buffer_nv12 frame;
	struct cixvpu_buffer_bitstream output;
	uint8_t frame_wire[CIXVPU_BUFFER_FRAME_BYTES];
	uint8_t output_wire[CIXVPU_BUFFER_BITSTREAM_BYTES];
	struct cixvpu_prepare_tx tx[3];
	uint16_t rx[2];
	unsigned int pending, input_state, output_state;
	uint32_t submissions, returns;
};

struct cixvpu_prepared {

	struct cixvpu_dma *dma;
	struct cixvpu_fw_plan plan;
	struct cixvpu_prepare_boot boot;
	struct cixvpu_prepare_queue queue;
	size_t bytes;
	uint32_t root_entry;
	unsigned int count;
	struct cixvpu_prepare_entry entries[];
};

struct cixvpu_prepare_work {
	unsigned int count, root;
	uint32_t owners[PREPARE_TABLES];
	uint32_t expected[PREPARE_TABLES][CIXVPU_MMU_ENTRIES];
	struct cixvpu_dma_page pages[];
};

struct cixvpu_prepare_span {
	const void *pointer;
	size_t bytes;
};

static bool
cixvpu_prepare_span_valid(const struct cixvpu_prepare_span *s)
{

	return s->pointer != NULL && s->bytes != 0 &&
	    s->bytes <= UINTPTR_MAX - (uintptr_t)s->pointer;
}

static bool
cixvpu_prepare_overlap(const struct cixvpu_prepare_span *a,
    const struct cixvpu_prepare_span *b)
{
	uintptr_t first = (uintptr_t)a->pointer, second = (uintptr_t)b->pointer;

	return first <= second ? second - first < a->bytes :
	    first - second < b->bytes;
}

static bool
cixvpu_prepare_info_equal(const struct cixvpu_fw_info *a,
    const struct cixvpu_fw_info *b)
{

	return a->jump == b->jump && a->minor == b->minor &&
	    a->major == b->major &&
	    memcmp(a->format_bytes, b->format_bytes, sizeof(a->format_bytes)) == 0 &&
	    memcmp(a->description, b->description, sizeof(a->description)) == 0 &&
	    memcmp(a->part, b->part, sizeof(a->part)) == 0 &&
	    memcmp(a->revision, b->revision, sizeof(a->revision)) == 0 &&
	    memcmp(a->version, b->version, sizeof(a->version)) == 0 &&
	    a->text_size == b->text_size && a->text_pages == b->text_pages &&
	    a->bss_address == b->bss_address && a->bss_pages == b->bss_pages &&
	    memcmp(a->bitmap, b->bitmap, sizeof(a->bitmap)) == 0 &&
	    a->shared_address == b->shared_address &&
	    a->shared_size == b->shared_size && a->shared_pages == b->shared_pages &&
	    a->private_pages == b->private_pages;
}

static bool
cixvpu_prepare_page_equal(const struct cixvpu_fw_plan_page *a,
    const struct cixvpu_fw_plan_page *b)
{

	return a->backing_index == b->backing_index && a->role == b->role &&
	    a->core == b->core && a->ordinal == b->ordinal &&
	    a->image_offset == b->image_offset && a->image_bytes == b->image_bytes &&
	    a->leaf_address == b->leaf_address && a->attribute == b->attribute &&
	    a->permission == b->permission;
}

/* All page metadata, address spans and borrowed storage precede CPU writes. */
static int
cixvpu_prepare_gather(struct cixvpu_dma *dma,
    const struct cixvpu_fw_plan *plan, const struct cixvpu_prepare_span *spans,
    size_t nspans, struct cixvpu_prepare_work *work)
{
	struct cixvpu_fw_plan_page expected;
	struct cixvpu_dma_page extra;
	struct cixvpu_prepare_span page, other;
	uint32_t entry;
	unsigned int i, j, roots = 0;
	size_t s;
	int error;

	for (i = 0; i < plan->backing_pages; i++) {
		struct cixvpu_dma_page *p = &work->pages[i];

		error = cixvpu_fw_plan_page(plan, i, &expected);
		if (error != 0)
			return error;
		error = cixvpu_dma_page(dma, i, p);
		if (error != 0)
			return error;
		page = (struct cixvpu_prepare_span){ p->kva, CIXVPU_FW_PAGE_SIZE };
		if (!cixvpu_prepare_page_equal(&p->plan_page, &expected) ||
		    !cixvpu_prepare_span_valid(&page))
			return EINVAL;
		error = cixvpu_mmu_encode(p->bus_address,
		    CIXVPU_MMU_PRIVATE, CIXVPU_MMU_READ, &entry);
		if (error != 0)
			return error;
		for (s = 0; s < nspans; s++)
			if (cixvpu_prepare_overlap(&page, &spans[s]))
				return EINVAL;
		for (j = 0; j < i; j++) {
			other = (struct cixvpu_prepare_span){
			    work->pages[j].kva, CIXVPU_FW_PAGE_SIZE };
			if (p->bus_address == work->pages[j].bus_address ||
			    cixvpu_prepare_overlap(&page, &other))
				return EINVAL;
		}
		if (expected.role == CIXVPU_FW_PLAN_TEXT &&
		    (expected.image_offset > spans[0].bytes ||
		    expected.image_bytes > spans[0].bytes - expected.image_offset))
			return EINVAL;
		if (expected.role != CIXVPU_FW_PLAN_ROOT &&
		    expected.role != CIXVPU_FW_PLAN_LEAF)
			continue;
		if (work->count == PREPARE_TABLES)
			return E2BIG;
		if (expected.role == CIXVPU_FW_PLAN_ROOT) {
			roots++;
			work->root = work->count;
		}
		work->owners[work->count++] = i;
	}
	if (roots != 1 || work->count != plan->cores + 2 + 2 * plan->boot_rpc +
	    2 * plan->slots_enabled)
		return EINVAL;
	/* The caller's allocator snapshot must contain exactly these owners. */
	error = cixvpu_dma_page(dma, plan->backing_pages, &extra);
	return error == ERANGE ? 0 : error != 0 ? error : EINVAL;
}

static int
cixvpu_prepare_record(struct cixvpu_prepared *prepared,
    struct cixvpu_prepare_work *work, uint32_t table, uint32_t backing,
    uint32_t address, unsigned int attribute, unsigned int permission,
    bool link)
{
	struct cixvpu_prepare_entry *record = &prepared->entries[prepared->count];
	unsigned int at, index;
	uint32_t word;
	int error;

	for (at = 0; at < work->count; at++)
		if (work->owners[at] == table)
			break;
	if (at == work->count)
		return EINVAL;
	if (link) {
		if ((address & ((UINT32_C(1) << 22) - 1)) != 0)
			return EINVAL;
		index = address >> 22;
	} else {
		if ((address & (CIXVPU_FW_PAGE_SIZE - 1)) != 0 ||
		    (address & ~((UINT32_C(1) << 22) - 1)) !=
		    work->pages[table].plan_page.leaf_address)
			return EINVAL;
		index = (address >> 12) & (CIXVPU_MMU_ENTRIES - 1);
	}
	if (work->expected[at][index] != 0)
		return EEXIST;
	error = cixvpu_mmu_encode(work->pages[backing].bus_address,
	    attribute, permission, &word);
	if (error != 0)
		return error;
	work->expected[at][index] = word;
	*record = (struct cixvpu_prepare_entry){ table, backing, address, word,
	    attribute, permission, link };
	prepared->count++;
	return 0;
}

static int
cixvpu_prepare_tables(struct cixvpu_prepared *prepared,
    struct cixvpu_prepare_work *work)
{
	const struct cixvpu_fw_plan *plan = &prepared->plan;
	struct cixvpu_fw_plan_mapping map;
	unsigned int i;
	int error;

	for (i = 0; i < plan->mappings; i++) {
		const struct cixvpu_fw_plan_page *p;

		error = cixvpu_fw_plan_mapping(plan, i, &map);
		if (error != 0)
			return error;
		if (map.backing_index >= plan->backing_pages ||
		    map.leaf_backing_index >= plan->backing_pages ||
		    work->pages[map.leaf_backing_index].plan_page.role !=
		    CIXVPU_FW_PLAN_LEAF)
			return EINVAL;
		p = &work->pages[map.backing_index].plan_page;
		if (p->role != map.role || p->attribute != map.attribute ||
		    p->permission != map.permission || p->role == CIXVPU_FW_PLAN_ROOT ||
		    p->role == CIXVPU_FW_PLAN_LEAF)
			return EINVAL;
		error = cixvpu_prepare_record(prepared, work, map.leaf_backing_index,
		    map.backing_index, map.address, map.attribute, map.permission, false);
		if (error != 0)
			return error;
	}
	/* Record root links last; their actual writes follow completed leaves. */
	for (i = 0; i < work->count; i++) {
		unsigned int index;

		index = work->owners[i];
		if (i == work->root)
			continue;
		error = cixvpu_prepare_record(prepared, work, work->owners[work->root],
		    index, work->pages[index].plan_page.leaf_address,
		    CIXVPU_MMU_PRIVATE, CIXVPU_MMU_READ, true);
		if (error != 0)
			return error;
	}
	return cixvpu_mmu_encode(
	    work->pages[work->owners[work->root]].bus_address,
	    CIXVPU_MMU_PRIVATE, CIXVPU_MMU_WRITE, &prepared->root_entry);
}

static int
cixvpu_prepare_common(struct cixvpu_dma **owned,
    const struct cixvpu_fw_plan *expected_plan, const void *image,
    size_t image_size, const uint8_t expected_digest[32],
    struct cixvpu_prepared **out, bool boot,
    const struct cixvpu_frame_source *source)
{
	static const uint8_t boot_version[16] = "r0p0-sum0005";
	struct cixvpu_fw_plan snapshot, checked;
	struct cixvpu_prepared *prepared = NULL;
	struct cixvpu_prepare_work *work = NULL;
	struct cixvpu_frame_source pixels = { 0 };
	struct cixvpu_fw_slot_span planes[2];
	struct cixvpu_prepare_span spans[] = {
		{ image, image_size }, { expected_plan, sizeof(*expected_plan) },
		{ expected_digest, 32 }, { owned, sizeof(*owned) }, { out, sizeof(*out) },
		{ source, sizeof(*source) }, { NULL, 0 }, { NULL, 0 }
	};
	SHA256_CTX hash;
	uint8_t digest[SHA256_DIGEST_LENGTH], pin[SHA256_DIGEST_LENGTH];
	size_t work_bytes, prepared_bytes, capacity, pages, nspans = 5;
	unsigned int i, j;
	uint32_t word;
	int error;

	if (owned == NULL || out == NULL || (void *)owned == (void *)out)
		return EINVAL;
	if (cpu_intr_p() || cpu_softintr_p())
		return EWOULDBLOCK;
	if (*owned == NULL || expected_plan == NULL || expected_digest == NULL ||
	    image_size < CIXVPU_FW_HEADER_SIZE || image_size > CIXVPU_FW_REGION_SIZE)
		return EINVAL;
	if (*out != NULL)
		return EBUSY;
	if (PAGE_SIZE != CIXVPU_FW_PAGE_SIZE)
		return EOPNOTSUPP;
	if (source != NULL) {
		if (!cixvpu_prepare_span_valid(&spans[5]) ||
		    cixvpu_prepare_overlap(&spans[5], &spans[3]) ||
		    cixvpu_prepare_overlap(&spans[5], &spans[4]))
			return EINVAL;
		pixels = *source;
		spans[6] = (struct cixvpu_prepare_span){ pixels.y, pixels.y_bytes };
		spans[7] = (struct cixvpu_prepare_span){ pixels.uv, pixels.uv_bytes };
		nspans = __arraycount(spans);
	}
	for (i = 0; i < nspans; i++)
		if (!cixvpu_prepare_span_valid(&spans[i]))
			return EINVAL;
	/* Publishing handles must not change each other or immutable inputs. */
	for (i = 3; i <= 4; i++)
		for (j = 0; j < nspans; j++)
			if (i != j && cixvpu_prepare_overlap(&spans[i], &spans[j]))
				return EINVAL;
	snapshot = *expected_plan;
	/* Ordinary entry points never silently downgrade a slot request. */
	if (source == NULL && (snapshot.slots_enabled != 0 ||
	    snapshot.slots.width != 0 ||
	    snapshot.slots.height != 0 || snapshot.slots.stride_y != 0 ||
	    snapshot.slots.stride_uv != 0 || snapshot.slots.bitstream_bytes != 0))
		return EINVAL;
	if (source != NULL && (snapshot.slots_enabled != 1 || snapshot.boot_rpc != 1))
		return EINVAL;
	memcpy(pin, expected_digest, sizeof(pin));
	(void)SHA256_Init(&hash);
	(void)SHA256_Update(&hash, image, image_size);
	(void)SHA256_Final(digest, &hash);
	if (memcmp(digest, pin, sizeof(pin)) != 0)
		return EINVAL;
	if (source != NULL)
		error = cixvpu_fw_plan_build_frame_slots(image, image_size,
		    snapshot.cores, &snapshot.slots, &checked);
	else if (snapshot.boot_rpc == 1)
		error = cixvpu_fw_plan_build_boot_rpc(image, image_size,
		    snapshot.cores, &checked);
	else
		error = cixvpu_fw_plan_build(image, image_size, snapshot.cores,
		    &checked);
	if (error != 0)
		return error;
	if (snapshot.cores != checked.cores ||
	    snapshot.boot_rpc != checked.boot_rpc ||
	    snapshot.backing_pages != checked.backing_pages ||
	    snapshot.mappings != checked.mappings ||
	    snapshot.slots_enabled != checked.slots_enabled ||
	    snapshot.slots.width != checked.slots.width ||
	    snapshot.slots.height != checked.slots.height ||
	    snapshot.slots.stride_y != checked.slots.stride_y ||
	    snapshot.slots.stride_uv != checked.slots.stride_uv ||
	    snapshot.slots.bitstream_bytes != checked.slots.bitstream_bytes ||
	    !cixvpu_prepare_info_equal(&snapshot.info, &checked.info))
		return EINVAL;
	if (boot && (checked.cores != 1 || checked.info.major != 3 ||
	    checked.info.minor != 5 || memcmp(checked.info.version,
	    boot_version, sizeof(checked.info.version)) != 0))
		return ENOTSUP;
	if (source != NULL) {
		for (i = 0; i < 2; i++) {
			error = cixvpu_fw_plan_slot(&checked, i, &planes[i]);
			if (error != 0)
				return error;
			if (spans[6 + i].bytes != planes[i].bytes)
				return EINVAL;
		}
	}
	capacity = (size_t)checked.mappings + CIXVPU_FW_PLAN_MAX_CORES + 1 +
	    2 * checked.boot_rpc;
	pages = checked.backing_pages;

	if (checked.backing_pages > CIXVPU_FW_PLAN_MAX_PAGES ||
	    pages > (SIZE_MAX - sizeof(*work)) / sizeof(work->pages[0]) ||
	    capacity > (SIZE_MAX - sizeof(*prepared)) / sizeof(prepared->entries[0]))
		return E2BIG;
	work_bytes = sizeof(*work) + pages * sizeof(work->pages[0]);
	prepared_bytes = sizeof(*prepared) + capacity * sizeof(prepared->entries[0]);
	work = kmem_zalloc(work_bytes, KM_SLEEP);
	if (work == NULL)
		return ENOMEM;
	prepared = kmem_zalloc(prepared_bytes, KM_SLEEP);
	if (prepared == NULL) {
		error = ENOMEM;
		goto failed;
	}
	prepared->bytes = prepared_bytes;
	prepared->plan = checked;
	error = cixvpu_prepare_gather(*owned, &checked, spans,
	    nspans, work);
	if (error != 0)
		goto failed;
	error = cixvpu_prepare_tables(prepared, work);
	if (error != 0)
		goto failed;
	for (i = 0; i < checked.backing_pages; i++)
		memset(work->pages[i].kva, 0, CIXVPU_FW_PAGE_SIZE);
	for (i = 0; i < checked.backing_pages; i++) {
		const struct cixvpu_fw_plan_page *p = &work->pages[i].plan_page;

		if (p->role == CIXVPU_FW_PLAN_TEXT)
			memcpy(work->pages[i].kva,
			    (const uint8_t *)image + p->image_offset,
			    p->image_bytes);
	}
	if (source != NULL) {
		for (i = 0; i < 2; i++) {
			for (j = 0; j < planes[i].pages; j++) {
				size_t offset = (size_t)j * CIXVPU_FW_PAGE_SIZE;
				size_t bytes = planes[i].bytes - offset;

				if (bytes > CIXVPU_FW_PAGE_SIZE)
					bytes = CIXVPU_FW_PAGE_SIZE;
				memcpy(work->pages[planes[i].first_backing + j].kva,
				    (const uint8_t *)spans[6 + i].pointer + offset, bytes);
			}
		}
	}
	for (i = 0; i < prepared->count; i++) {
		const struct cixvpu_prepare_entry *e = &prepared->entries[i];

		if (e->link)
			error = cixvpu_mmu_link(work->pages[e->table].kva,
			    CIXVPU_FW_PAGE_SIZE, e->address,
			    work->pages[e->backing].bus_address);
		else
			error = cixvpu_mmu_map(work->pages[e->table].kva,
			    CIXVPU_FW_PAGE_SIZE, e->address,
			    work->pages[e->backing].bus_address,
			    e->attribute, e->permission);
		if (error != 0)
			goto failed;
	}
	for (i = 0; i < work->count; i++) {
		for (j = 0; j < CIXVPU_MMU_ENTRIES; j++) {
			error = cixvpu_mmu_lookup(work->pages[work->owners[i]].kva,
			    CIXVPU_FW_PAGE_SIZE, j, &word);
			if (error != 0)
				goto failed;
			if (word != work->expected[i][j]) {
				error = EIO;
				goto failed;
			}
		}
	}
	if (boot) {
		struct cixvpu_ring_checksum initial = { 0 }, seeded;
		uint16_t next;
		uint8_t *host = NULL;

		for (i = 0; i < checked.backing_pages; i++)
			if (work->pages[i].plan_page.role == CIXVPU_FW_PLAN_MSG_HOST)
				host = work->pages[i].kva;
		if (host == NULL) {
			error = EINVAL;
			goto failed;
		}
		error = cixvpu_ring_write_sum(host + 16, CIXVPU_RING_BYTES,
		    0, 0, 1006, NULL, 0, &initial, &next, &seeded);
		if (error != 0)
			goto failed;
		host[2] = (uint8_t)next;
		host[3] = (uint8_t)(next >> 8);
		for (i = 0; i < 3; i++)
			for (j = 0; j < 4; j++)
				host[4 + i * 4 + j] = (uint8_t)(seeded.reserved[i] >> (8 * j));
		prepared->boot.enabled = true;
		prepared->queue.tx[0].write = next;
		prepared->queue.tx[0].checksum = seeded;
	}
	error = cixvpu_dma_seal(*owned, prepared);

	if (error != 0)
		goto failed;
	/* Seal is the last fallible operation. Discard views and move the owner. */
	kmem_free(work, work_bytes);
	prepared->dma = *owned;
	*owned = NULL;
	*out = prepared;
	return 0;
failed:
	if (prepared != NULL)
		kmem_free(prepared, prepared_bytes);
	kmem_free(work, work_bytes);
	return error;
}

int
cixvpu_prepared_free(struct cixvpu_prepared **owner)
{
	struct cixvpu_prepared *prepared;
	int error;

	if (owner == NULL || *owner == NULL)
		return EINVAL;
	if (cpu_intr_p() || cpu_softintr_p())
		return EWOULDBLOCK;
	prepared = *owner;
	error = cixvpu_dma_free_prepared(&prepared->dma, prepared);
	if (error != 0)
		return error;
	kmem_free(prepared, prepared->bytes);
	*owner = NULL;
	return 0;
}

int
cixvpu_prepared_sync_initial(struct cixvpu_prepared *prepared)
{

	if (cpu_intr_p() || cpu_softintr_p())
		return EWOULDBLOCK;
	if (prepared == NULL)
		return EINVAL;
	return cixvpu_dma_sync_initial(prepared->dma, prepared, &prepared->plan);
}

int
cixvpu_prepare_cpu(struct cixvpu_dma **owned,
    const struct cixvpu_fw_plan *plan, const void *image, size_t size,
    const uint8_t pin[32], struct cixvpu_prepared **out)
{

	return cixvpu_prepare_common(owned, plan, image, size, pin, out, false, NULL);
}

int
cixvpu_prepare_boot_cpu(struct cixvpu_dma **owned,
    const struct cixvpu_fw_plan *plan, const void *image, size_t size,
    const uint8_t pin[32], struct cixvpu_prepared **out)
{

	return cixvpu_prepare_common(owned, plan, image, size, pin, out, true, NULL);
}

int
cixvpu_prepare_frame_slots_cpu(struct cixvpu_dma **owned,
    const struct cixvpu_fw_plan *plan, const void *image, size_t size,
    const uint8_t pin[32], const struct cixvpu_frame_source *source,
    struct cixvpu_prepared **out)
{

	if (source == NULL)
		return EINVAL;
	return cixvpu_prepare_common(owned, plan, image, size, pin, out, true, source);
}

static int
cixvpu_prepare_boot_output(struct cixvpu_prepared *p, const void *out,
    size_t size, size_t alignment, bool *fault)
{
	struct cixvpu_fw_plan_page page;
	struct cixvpu_prepare_span destination = { out, size };
	struct cixvpu_prepare_span retained;
	size_t capacity;
	int error;

	if (cpu_intr_p() || cpu_softintr_p())
		return EWOULDBLOCK;
	*fault = false;
	if (p == NULL || !cixvpu_prepare_span_valid(&destination) ||
	    (uintptr_t)out % alignment != 0)
		return EINVAL;
	if (!p->boot.enabled)
		return ENOTSUP;
	error = cixvpu_fw_plan_page(&p->plan, 0, &page);
	if (error != 0) {
		*fault = true;
		return error;
	}
	capacity = (size_t)p->plan.mappings + CIXVPU_FW_PLAN_MAX_CORES + 1 +
	    2 * p->plan.boot_rpc;
	if (capacity > (SIZE_MAX - sizeof(*p)) / sizeof(p->entries[0]) ||
	    p->bytes != sizeof(*p) + capacity * sizeof(p->entries[0])) {
		*fault = true;
		return EINVAL;
	}
	retained = (struct cixvpu_prepare_span){ p, p->bytes };
	if (!cixvpu_prepare_span_valid(&retained) ||
	    cixvpu_prepare_overlap(&destination, &retained))
		return EINVAL;
	return cixvpu_dma_boot_output(p->dma, p, out, size, fault);
}

int
cixvpu_prepared_publish_root(struct cixvpu_prepared *p, uint32_t *out)
{
	int error;
	bool fault;

	error = cixvpu_prepare_boot_output(p, out, sizeof(*out),
	    _Alignof(uint32_t), &fault);
	if (error != 0)
		return error;
	if (p->boot.published)
		return EBUSY;
	error = cixvpu_dma_mark_exposed(p->dma, p);
	if (error != 0)
		return error;
	p->boot.published = true;
	*out = p->root_entry;
	return 0;
}

static uint32_t
cixvpu_prepare_get32(const uint8_t *p)
{

	return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
	    (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

int
cixvpu_prepared_boot_rpc_header(struct cixvpu_prepared *p,
    struct cixvpu_boot_rpc_header *out)
{
	struct cixvpu_boot_rpc_header header = { 0 };
	int error;
	bool fault;

	if (cpu_intr_p() || cpu_softintr_p())
		return EWOULDBLOCK;
	if (p == NULL || out == NULL)
		return EINVAL;
	error = cixvpu_prepare_boot_output(p, out, sizeof(*out),
	    _Alignof(struct cixvpu_boot_rpc_header), &fault);
	if (error != 0)
		return error;
	if (p->boot.snapshot.rpc_valid) {
		header.state = cixvpu_prepare_get32(p->boot.snapshot.rpc_header);
		header.callid = cixvpu_prepare_get32(p->boot.snapshot.rpc_header + 4);
		header.size = cixvpu_prepare_get32(p->boot.snapshot.rpc_header + 8);
		header.valid = true;
		if (p->boot.snapshot.rpc_alloc_valid) {
			header.alloc_size = cixvpu_prepare_get32(
			    p->boot.snapshot.rpc_alloc);
			header.alloc_max_size = cixvpu_prepare_get32(
			    p->boot.snapshot.rpc_alloc + 4);
			header.alloc_region = p->boot.snapshot.rpc_alloc[8];
			header.alloc_log2_alignment = p->boot.snapshot.rpc_alloc[9];
			header.alloc_valid = true;
		}
		if (p->boot.snapshot.rpc_resize_valid) {
			header.resize_address = cixvpu_prepare_get32(
			    p->boot.snapshot.rpc_resize);
			header.resize_new_size = cixvpu_prepare_get32(
			    p->boot.snapshot.rpc_resize + 4);
			header.resize_valid = true;
		}
	}
	*out = header;
	return 0;
}

int
cixvpu_prepared_boot_debugparam(struct cixvpu_prepared *p,
    enum cixvpu_boot_debug_result *out)
{
	enum cixvpu_boot_debug_result result;
	uint32_t state;
	int error;
	bool fault = false;

	if (cpu_intr_p() || cpu_softintr_p())
		return EWOULDBLOCK;
	if (p == NULL || out == NULL)
		return EINVAL;
	if (p->boot.error != 0)
		return p->boot.error;
	if (p->queue.held)
		return EBUSY;
	error = cixvpu_prepare_boot_output(p, out, sizeof(*out),
	    _Alignof(enum cixvpu_boot_debug_result), &fault);
	if (error != 0) {
		if (fault && p->boot.published) {
			p->boot.error = error;
			p->boot.snapshot.rpc_valid = false;
			p->boot.snapshot.rpc_alloc_valid = false;
			p->boot.snapshot.rpc_resize_valid = false;
		}
		return error;
	}
	if (!p->boot.published)
		return EBUSY;
	p->boot.snapshot.rpc_valid = false;
	p->boot.snapshot.rpc_alloc_valid = false;
	p->boot.snapshot.rpc_resize_valid = false;
	error = cixvpu_dma_boot_debugparam(p->dma, p, &p->plan,
	    &p->boot.snapshot);
	if (error != 0) {
		if (error != EAGAIN)
			p->boot.error = error;
		return error;
	}
	state = cixvpu_prepare_get32(p->boot.snapshot.rpc_header);
	result = state == 0 ? CIXVPU_BOOT_DEBUG_IDLE :
	    state == 2 ? CIXVPU_BOOT_DEBUG_PENDING : CIXVPU_BOOT_DEBUG_REPLIED;
	*out = result;
	return 0;
}

int
cixvpu_prepared_boot_rpc_step(struct cixvpu_prepared *p,
    enum cixvpu_boot_rpc_result *out)
{
	enum cixvpu_dma_rpc_result observed;
	enum cixvpu_boot_rpc_result result;
	int error;
	bool fault = false;

	if (cpu_intr_p() || cpu_softintr_p())
		return EWOULDBLOCK;
	if (p == NULL || out == NULL)
		return EINVAL;
	if (p->boot.error != 0)
		return p->boot.error;
	if (p->queue.held)
		return EBUSY;
	error = cixvpu_prepare_boot_output(p, out, sizeof(*out),
	    _Alignof(enum cixvpu_boot_rpc_result), &fault);
	if (error != 0) {
		if (fault && p->boot.published) {
			p->boot.error = error;
			p->boot.snapshot.rpc_valid = false;
			p->boot.snapshot.rpc_alloc_valid = false;
			p->boot.snapshot.rpc_resize_valid = false;
		}
		return error;
	}
	if (p->plan.boot_rpc != 1)
		return ENOTSUP;
	if (!p->boot.published)
		return EBUSY;
	p->boot.snapshot.rpc_valid = false;
	p->boot.snapshot.rpc_alloc_valid = false;
	p->boot.snapshot.rpc_resize_valid = false;
	error = cixvpu_dma_boot_rpc_step(p->dma, p, &p->plan,
	    &p->boot.snapshot, &observed);
	if (error != 0) {
		if (error != EAGAIN)
			p->boot.error = error;
		return error;
	}
	switch (observed) {
	case CIXVPU_DMA_RPC_IDLE:
		result = CIXVPU_BOOT_RPC_IDLE;
		break;
	case CIXVPU_DMA_RPC_PENDING:
		result = CIXVPU_BOOT_RPC_PENDING;
		break;
	case CIXVPU_DMA_RPC_ALLOC_REPLIED:
		result = CIXVPU_BOOT_RPC_ALLOC_REPLIED;
		break;
	case CIXVPU_DMA_RPC_DEBUG_REPLIED:
		result = CIXVPU_BOOT_RPC_DEBUG_REPLIED;
		break;
	case CIXVPU_DMA_RPC_SECOND_ALLOC_REPLIED:
		result = CIXVPU_BOOT_RPC_SECOND_ALLOC_REPLIED;
		break;
	case CIXVPU_DMA_RPC_RESIZE_REPLIED:
		result = CIXVPU_BOOT_RPC_RESIZE_REPLIED;
		break;
	default:
		p->boot.error = EPROTO;
		return EPROTO;
	}
	*out = result;
	return 0;
}

static int
cixvpu_prepared_boot_receive_common(struct cixvpu_prepared *p,
    struct cixvpu_boot_reply *out, bool rpc_enabled)
{
	struct cixvpu_boot_reply reply = { 0 };
	struct cixvpu_ring_checksum observed, next_sum;
	uint8_t payload[4];
	uint8_t *message;
	uint16_t next, write;
	size_t length;
	unsigned int i;
	int error;
	bool fault = false;

	if (rpc_enabled) {
		if (cpu_intr_p() || cpu_softintr_p())
			return EWOULDBLOCK;
		if (p == NULL || out == NULL)
			return EINVAL;
		if (p->boot.error != 0)
			return p->boot.error;
	}
	error = cixvpu_prepare_boot_output(p, out, sizeof(*out),
	    _Alignof(struct cixvpu_boot_reply), &fault);
	if (error != 0) {
		if (fault && p->boot.published) {
			p->boot.error = error;
			p->boot.snapshot.rpc_valid = false;
			p->boot.snapshot.rpc_alloc_valid = false;
			p->boot.snapshot.rpc_resize_valid = false;
		}
		return error;
	}
	if (p->queue.enabled || p->queue.held)
		return p->boot.error != 0 ? p->boot.error : EBUSY;
	if (rpc_enabled && p->plan.boot_rpc != 1)
		return ENOTSUP;
	if (!p->boot.published)
		return EBUSY;
	if (p->boot.error != 0)
		return p->boot.error;
	p->boot.snapshot.rpc_valid = false;
	p->boot.snapshot.rpc_alloc_valid = false;
	p->boot.snapshot.rpc_resize_valid = false;
	if (rpc_enabled)
		error = cixvpu_dma_boot_snapshot_rpc(p->dma, p, &p->plan,
		    &p->boot.snapshot);
	else
		error = cixvpu_dma_boot_snapshot(p->dma, p, &p->plan,
		    &p->boot.snapshot);
	if (error != 0)
		goto failed;
	message = p->boot.snapshot.message;
	write = (uint16_t)((uint16_t)message[0] | (uint16_t)message[1] << 8);
	observed = p->boot.checksum;
	for (i = 0; i < 3; i++)
		observed.reserved[i] = cixvpu_prepare_get32(message + 4 + 4 * i);
	error = cixvpu_ring_read_sum(message + 16, CIXVPU_RING_BYTES,
	    p->boot.cursor, write, &observed, &reply.code, payload,
	    sizeof(payload), &length, &next, &next_sum);
	if (error != 0)
		goto failed;
	if (reply.code == 2001 && length == 4) {
		reply.core = cixvpu_prepare_get32(payload);
		if (reply.core != 0) {
			error = EPROTO;
			goto failed;
		}
	} else if (reply.code != 2009 || length != 0) {
		error = EPROTO;
		goto failed;
	}
	error = cixvpu_dma_boot_ack(p->dma, p, &p->plan, next);
	if (error != 0)
		goto failed;
	p->boot.cursor = next;
	p->boot.checksum = next_sum;
	if (reply.code == 2001)
		p->boot.switched_in = true;
	else
		p->boot.pong = true;
	*out = reply;
	return 0;
failed:
	if (error != EAGAIN)
		p->boot.error = error;
	return error;
}

int
cixvpu_prepared_boot_receive(struct cixvpu_prepared *p,
    struct cixvpu_boot_reply *out)
{

	return cixvpu_prepared_boot_receive_common(p, out, false);
}

int
cixvpu_prepared_boot_receive_rpc(struct cixvpu_prepared *p,
    struct cixvpu_boot_reply *out)
{

	return cixvpu_prepared_boot_receive_common(p, out, true);
}

int
cixvpu_prepared_queue_open(struct cixvpu_prepared *p,
    const struct cixvpu_queue_identity *identity)
{
	struct cixvpu_prepare_queue queue = { 0 };
	struct cixvpu_fw_slot_span y, uv, output;
	size_t length;
	int error;
	bool fault = false;

	if (cpu_intr_p() || cpu_softintr_p())
		return EWOULDBLOCK;
	if (p == NULL || identity == NULL)
		return EINVAL;
	if (p->boot.error != 0)
		return p->boot.error;
	if (p->queue.enabled || p->queue.held || !p->boot.published ||
	    !p->boot.switched_in || !p->boot.pong)
		return EBUSY;
	if (p->plan.slots_enabled != 1 || p->plan.boot_rpc != 1 ||
	    p->plan.cores != 1)
		return ENOTSUP;
	error = cixvpu_prepare_boot_output(p, identity, sizeof(*identity),
	    _Alignof(struct cixvpu_queue_identity), &fault);
	if (error != 0) {
		if (fault)
			p->boot.error = error;
		return error;
	}
	error = cixvpu_fw_plan_slot(&p->plan, CIXVPU_FW_SLOT_Y, &y);
	if (error == 0)
		error = cixvpu_fw_plan_slot(&p->plan, CIXVPU_FW_SLOT_UV, &uv);
	if (error == 0)
		error = cixvpu_fw_plan_slot(&p->plan, CIXVPU_FW_SLOT_BITSTREAM, &output);
	if (error != 0) {
		p->boot.error = error;
		return error;
	}
	queue.identity = *identity;
	queue.frame = (struct cixvpu_buffer_nv12){
		.handle = identity->frame_handle, .tag = identity->input_tag,
		.width = p->plan.slots.width, .height = p->plan.slots.height,
		.stride_y = p->plan.slots.stride_y, .stride_uv = p->plan.slots.stride_uv,
		.y = { y.address, y.bytes }, .uv = { uv.address, uv.bytes },
		.force_idr = true, .eos = true
	};
	queue.output = (struct cixvpu_buffer_bitstream){
		.handle = identity->output_handle, .tag = identity->input_tag,
		.data = { output.address, output.bytes }
	};
	error = cixvpu_buffer_frame_nv12(&queue.frame, queue.frame_wire,
	    sizeof(queue.frame_wire), &length);
	if (error == 0)
		error = cixvpu_buffer_bitstream(&queue.output, queue.output_wire,
		    sizeof(queue.output_wire), &length);
	if (error != 0)
		return error;
	memcpy(queue.tx, p->queue.tx, sizeof(queue.tx));
	queue.enabled = true;
	p->queue = queue;
	return 0;
}

static int
cixvpu_prepare_queue_context(struct cixvpu_prepared *p)
{

	if (cpu_intr_p() || cpu_softintr_p())
		return EWOULDBLOCK;
	if (p == NULL)
		return EINVAL;
	if (p->boot.error != 0)
		return p->boot.error;
	return !p->queue.enabled || p->queue.held ? EBUSY : 0;
}

static int
cixvpu_prepare_queue_failure(struct cixvpu_prepared *p, int error)
{

	if (error != 0 && error != EAGAIN && error != EMSGSIZE) {
		if (p->boot.error == 0)
			p->boot.error = error;
		p->queue.held = true;
	}
	return error;
}

static int
cixvpu_prepare_queue_value(struct cixvpu_prepared *p, const void *value,
    size_t bytes, size_t alignment)
{
	int error;
	bool fault = false;

	error = cixvpu_prepare_boot_output(p, value, bytes, alignment, &fault);
	return fault ? cixvpu_prepare_queue_failure(p, error) : error;
}

static unsigned int
cixvpu_prepare_queue_distance(uint16_t from, uint16_t to)
{

	return to >= from ? to - from : 1020 - from + to;
}

int
cixvpu_prepared_queue_rpc_pending(struct cixvpu_prepared *p, bool *out)
{
	uint16_t consumer;
	uint32_t state;
	int error;

	error = cixvpu_prepare_queue_context(p);
	if (error != 0)
		return error;
	error = cixvpu_prepare_queue_value(p, out, sizeof(*out), _Alignof(bool));
	if (error != 0)
		return error;
	p->boot.snapshot.rpc_valid = false;
	p->boot.snapshot.rpc_alloc_valid = false;
	p->boot.snapshot.rpc_resize_valid = false;
	error = cixvpu_dma_queue_cursor(p->dma, p, &p->plan,
	    CIXVPU_DMA_MESSAGE, &p->boot.snapshot, &consumer);
	if (error != 0 && error != EAGAIN)
		return cixvpu_prepare_queue_failure(p, error);
	if (!p->boot.snapshot.rpc_valid)
		return EAGAIN;
	state = cixvpu_prepare_get32(p->boot.snapshot.rpc_header);
	if (error == EAGAIN && state == 1) {
		*out = true;
		return 0;
	}
	if (error != 0)
		return error;
	if (state != 0 && state != 2)
		return cixvpu_prepare_queue_failure(p, EPROTO);
	*out = false;
	return 0;
}

static int
cixvpu_prepare_queue_send(struct cixvpu_prepared *p,
    enum cixvpu_dma_queue queue, uint16_t code, const void *payload,
    size_t length, bool refill)
{
	struct cixvpu_prepare_tx *tx = &p->queue.tx[queue];
	struct cixvpu_ring_checksum next_sum;
	uint16_t read, next;
	int error;

	if (p->queue.submissions == UINT32_MAX)
		return cixvpu_prepare_queue_failure(p, EOVERFLOW);
	error = cixvpu_dma_queue_cursor(p->dma, p, &p->plan, queue,
	    &p->boot.snapshot, &read);
	if (error != 0)
		return cixvpu_prepare_queue_failure(p, error);
	if (tx->read >= 1020 || tx->write >= 1020 ||
	    cixvpu_prepare_queue_distance(tx->read, read) >
	    cixvpu_prepare_queue_distance(tx->read, tx->write))
		return cixvpu_prepare_queue_failure(p, EPROTO);
	error = cixvpu_ring_write_sum(p->boot.snapshot.message + 16,
	    CIXVPU_RING_BYTES, read, tx->write, code, payload, length,
	    &tx->checksum, &next, &next_sum);
	if (error != 0)
		return cixvpu_prepare_queue_failure(p, error);
	error = cixvpu_dma_queue_publish(p->dma, p, &p->plan, queue,
	    tx->write, next, p->boot.snapshot.message + 16,
	    next_sum.reserved, refill);
	if (error != 0)
		return cixvpu_prepare_queue_failure(p, error);
	/* Publication succeeded. Caller must notify once before another step. */
	tx->read = read;
	tx->write = next;
	tx->checksum = next_sum;
	p->queue.submissions++;
	return 0;
}

int
cixvpu_prepared_queue_command(struct cixvpu_prepared *p,
    const struct cixvpu_queue_command *input)
{
	struct cixvpu_queue_command command;
	uint8_t payload[CIXVPU_COMMAND_SETUP_MAX_BYTES];
	uint16_t code;
	size_t length = 0;
	int error;

	error = cixvpu_prepare_queue_context(p);
	if (error != 0)
		return error;
	if (input == NULL)
		return EINVAL;
	error = cixvpu_prepare_queue_value(p, input, sizeof(*input),
	    _Alignof(struct cixvpu_queue_command));
	if (error != 0)
		return error;
	command = *input;
	if (command.kind != CIXVPU_Q_HEVC_MAIN &&
	    command.kind != CIXVPU_Q_FIXED_QP && command.value != 0)
		return EINVAL;
	code = CIXVPU_COMMAND_SET_OPTION;
	switch (command.kind) {
	case CIXVPU_Q_JOB_ONE:
		code = CIXVPU_COMMAND_JOB;
		error = cixvpu_command_job(1, 1, 0, payload, sizeof(payload), &length);
		break;
	case CIXVPU_Q_NALU_START_CODES:
		error = cixvpu_command_option(CIXVPU_COMMAND_NALU_FORMAT, 1,
		    payload, sizeof(payload), &length);
		break;
	case CIXVPU_Q_BFRAMES_ZERO:
		error = cixvpu_command_option(CIXVPU_COMMAND_B_FRAMES, 0,
		    payload, sizeof(payload), &length);
		break;
	case CIXVPU_Q_PFRAMES_ZERO:
		error = cixvpu_command_option(CIXVPU_COMMAND_P_FRAMES, 0,
		    payload, sizeof(payload), &length);
		break;
	case CIXVPU_Q_HEVC_MAIN:
		error = cixvpu_command_hevc_main(command.value, payload,
		    sizeof(payload), &length);
		break;
	case CIXVPU_Q_FIXED_QP:
		error = cixvpu_command_option(CIXVPU_COMMAND_FIXED_QP, command.value,
		    payload, sizeof(payload), &length);
		break;
	case CIXVPU_Q_GO: code = CIXVPU_COMMAND_GO; break;
	case CIXVPU_Q_STOP: code = CIXVPU_COMMAND_STOP; break;
	case CIXVPU_Q_INPUT_FLUSH: code = CIXVPU_COMMAND_INPUT_FLUSH; break;
	case CIXVPU_Q_OUTPUT_FLUSH: code = CIXVPU_COMMAND_OUTPUT_FLUSH; break;
	case CIXVPU_Q_PING: code = CIXVPU_COMMAND_PING; break;
	case CIXVPU_Q_IDLE_ACK: code = CIXVPU_COMMAND_IDLE_ACK; break;
	default: return ENOTSUP;
	}
	if (error != 0)
		return error;
	return cixvpu_prepare_queue_send(p, CIXVPU_DMA_MESSAGE, code,
	    length == 0 ? NULL : payload, length, false);
}

int
cixvpu_prepared_queue_input_param(struct cixvpu_prepared *p,
    const struct cixvpu_queue_input_param *input)
{
	uint8_t payload[CIXVPU_COMMAND_SETUP_MAX_BYTES];
	size_t length;
	int error;

	error = cixvpu_prepare_queue_context(p);
	if (error != 0)
		return error;
	if (p->queue.input_state != 0)
		return EBUSY;
	if (input == NULL)
		return EINVAL;
	error = cixvpu_prepare_queue_value(p, input, sizeof(*input),
	    _Alignof(struct cixvpu_queue_input_param));
	if (error != 0)
		return error;
	switch (input->kind) {
	case CIXVPU_Q_FRAME_RATE:
		error = cixvpu_command_input_frame_rate(input->value, payload,
		    sizeof(payload), &length);
		break;
	case CIXVPU_Q_RC_OFF:
		if (input->value != 0)
			return EINVAL;
		error = cixvpu_command_input_rate_control_off(payload,
		    sizeof(payload), &length);
		break;
	default:
		return ENOTSUP;
	}
	if (error != 0)
		return error;
	return cixvpu_prepare_queue_send(p, CIXVPU_DMA_INPUT,
	    CIXVPU_COMMAND_INPUT_PARAM, payload, length, false);
}

int
cixvpu_prepared_queue_submit(struct cixvpu_prepared *p, enum cixvpu_queue_slot slot)
{
	unsigned int *state;
	int error;

	error = cixvpu_prepare_queue_context(p);
	if (error != 0)
		return error;
	if (slot == CIXVPU_Q_INPUT_ONCE) {
		state = &p->queue.input_state;
		if (*state != 0)
			return EBUSY;
		error = cixvpu_prepare_queue_send(p, CIXVPU_DMA_INPUT,
		    CIXVPU_BUFFER_FRAME, p->queue.frame_wire,
		    sizeof(p->queue.frame_wire), false);
	} else if (slot == CIXVPU_Q_OUTPUT_EMPTY) {
		state = &p->queue.output_state;
		if (*state == 1 || p->queue.output_eof)
			return EBUSY;
		error = cixvpu_prepare_queue_send(p, CIXVPU_DMA_OUTPUT,
		    CIXVPU_BUFFER_BITSTREAM, p->queue.output_wire,
		    sizeof(p->queue.output_wire), *state == 2);
	} else {
		return ENOTSUP;
	}
	if (error == 0)
		*state = 1;
	return error;
}

static int
cixvpu_prepare_queue_read(struct cixvpu_prepared *p,
    enum cixvpu_dma_queue queue, uint16_t cursor, uint16_t *code,
    void *payload, size_t capacity, size_t *length, uint16_t *next,
    struct cixvpu_ring_checksum *next_sum)
{
	struct cixvpu_ring_checksum observed;
	const uint8_t *page = p->boot.snapshot.message;
	uint16_t write;
	int error;

	error = cixvpu_dma_queue_snapshot(p->dma, p, &p->plan, queue, &p->boot.snapshot);
	if (error != 0)
		return cixvpu_prepare_queue_failure(p, error);
	write = (uint16_t)page[0] | (uint16_t)page[1] << 8;
	if (queue == CIXVPU_DMA_MESSAGE) {
		unsigned int i;

		observed = p->boot.checksum;
		for (i = 0; i < 3; i++)
			observed.reserved[i] = cixvpu_prepare_get32(page + 4 + 4 * i);
		error = cixvpu_ring_read_sum(page + 16, CIXVPU_RING_BYTES, cursor,
		    write, &observed, code, payload, capacity, length, next, next_sum);
	} else {
		error = cixvpu_ring_read(page + 16, CIXVPU_RING_BYTES, cursor,
		    write, code, payload, capacity, length, next);
	}
	if (error == EINVAL || error == EMSGSIZE)
		error = EPROTO;
	return cixvpu_prepare_queue_failure(p, error);
}

int
cixvpu_prepared_queue_receive(struct cixvpu_prepared *p,
    struct cixvpu_queue_event *out)
{
	struct cixvpu_queue_event event = { 0 };
	struct cixvpu_ring_checksum next_sum;
	uint8_t payload[CIXVPU_COMMAND_TEXT_BYTES + 4];
	uint16_t code, next;
	size_t length;
	unsigned int pending = 0;
	int error;
	bool fatal = false;

	error = cixvpu_prepare_queue_context(p);
	if (error != 0)
		return error;
	if (p->queue.pending != 0)
		return EBUSY;
	error = cixvpu_prepare_queue_value(p, out, sizeof(*out),
	    _Alignof(struct cixvpu_queue_event));
	if (error != 0)
		return error;
	error = cixvpu_prepare_queue_read(p, CIXVPU_DMA_MESSAGE, p->boot.cursor,
	    &code, payload, sizeof(payload), &length, &next, &next_sum);
	if (error != 0)
		return error;
	if (code == 2005 || code == 2006) {
		if (length != 0 || (code == 2005 ? p->queue.input_state :
		    p->queue.output_state) != 1)
			return cixvpu_prepare_queue_failure(p, EPROTO);
		event.kind = code == 2005 ? CIXVPU_Q_INPUT_AVAILABLE : CIXVPU_Q_OUTPUT_AVAILABLE;
		pending = code == 2005 ? 1 : 2;
	} else if (code == 2007 || code == 2008 || code == 2009 || code == 2013) {
		if (length != 0)
			return cixvpu_prepare_queue_failure(p, EPROTO);
		event.kind = code == 2007 ? CIXVPU_Q_INPUT_FLUSHED : code == 2008 ?
		    CIXVPU_Q_OUTPUT_FLUSHED : code == 2009 ? CIXVPU_Q_PONG : CIXVPU_Q_IDLE;
	} else {
		event.kind = CIXVPU_Q_RESPONSE;
		error = cixvpu_command_response(code, payload, length, &event.response);
		if (error != 0)
			return cixvpu_prepare_queue_failure(p, error);
		fatal = event.response.kind == CIXVPU_COMMAND_FATAL ||
		    event.response.kind == CIXVPU_COMMAND_OPTION_FAILED;
	}
	error = cixvpu_dma_queue_ack(p->dma, p, &p->plan, CIXVPU_DMA_MESSAGE, next);
	if (error != 0)
		return cixvpu_prepare_queue_failure(p, error);
	p->boot.cursor = next;
	p->boot.checksum = next_sum;
	p->queue.pending = pending;
	*out = event;
	if (fatal)
		(void)cixvpu_prepare_queue_failure(p, EIO);
	return 0;
}

int
cixvpu_prepared_queue_input_return(struct cixvpu_prepared *p,
    struct cixvpu_buffer_frame_result *out)
{
	struct cixvpu_buffer_frame_result result;
	struct cixvpu_ring_checksum unused;
	uint8_t payload[CIXVPU_BUFFER_FRAME_BYTES];
	uint16_t code, next;
	size_t length;
	int error;

	error = cixvpu_prepare_queue_context(p);
	if (error != 0)
		return error;
	if (p->queue.pending != 1 || p->queue.input_state != 1)
		return EBUSY;
	if (p->queue.returns == UINT32_MAX)
		return cixvpu_prepare_queue_failure(p, EOVERFLOW);
	error = cixvpu_prepare_queue_value(p, out, sizeof(*out),
	    _Alignof(struct cixvpu_buffer_frame_result));
	if (error != 0)
		return error;
	error = cixvpu_prepare_queue_read(p, CIXVPU_DMA_INPUT, p->queue.rx[0],
	    &code, payload, sizeof(payload), &length, &next, &unused);
	if (error != 0)
		return error;
	if (code != CIXVPU_BUFFER_FRAME)
		return cixvpu_prepare_queue_failure(p, EPROTO);
	error = cixvpu_buffer_frame_return(payload, length, &p->queue.frame, &result);
	if (error != 0)
		return cixvpu_prepare_queue_failure(p, error);
	error = cixvpu_dma_queue_return(p->dma, p, &p->plan, CIXVPU_DMA_INPUT,
	    next, 0, 0, NULL, 0);
	if (error != 0)
		return cixvpu_prepare_queue_failure(p, error);
	p->queue.rx[0] = next;
	p->queue.input_state = 2;
	p->queue.pending = 0;
	p->queue.returns++;
	*out = result;
	return 0;
}

int
cixvpu_prepared_queue_output_copy(struct cixvpu_prepared *p, void *destination,
    size_t capacity, struct cixvpu_buffer_bitstream_result *out)
{
	struct cixvpu_buffer_bitstream_result result;
	struct cixvpu_ring_checksum unused;
	struct cixvpu_prepare_span a = { destination, capacity };
	struct cixvpu_prepare_span b = { out, sizeof(*out) };
	uint8_t payload[CIXVPU_BUFFER_BITSTREAM_BYTES];
	uint16_t code, next;
	size_t length;
	int error;

	error = cixvpu_prepare_queue_context(p);
	if (error != 0)
		return error;
	if (p->queue.pending != 2 || p->queue.output_state != 1)
		return EBUSY;
	if (p->queue.returns == UINT32_MAX)
		return cixvpu_prepare_queue_failure(p, EOVERFLOW);
	error = cixvpu_prepare_queue_value(p, out, sizeof(*out),
	    _Alignof(struct cixvpu_buffer_bitstream_result));
	if (error == 0 && capacity != 0)
		error = cixvpu_prepare_queue_value(p, destination, capacity, 1);
	if (error != 0)
		return error;
	if ((capacity == 0 && destination != NULL) ||
	    (capacity != 0 && cixvpu_prepare_overlap(&a, &b)))
		return EINVAL;
	error = cixvpu_prepare_queue_read(p, CIXVPU_DMA_OUTPUT, p->queue.rx[1],
	    &code, payload, sizeof(payload), &length, &next, &unused);
	if (error != 0)
		return error;
	if (code != CIXVPU_BUFFER_BITSTREAM)
		return cixvpu_prepare_queue_failure(p, EPROTO);
	error = cixvpu_buffer_bitstream_return(payload, length, &p->queue.output, &result);
	if (error != 0)
		return cixvpu_prepare_queue_failure(p, error);
	if (capacity < result.filled)
		return EMSGSIZE;
	error = cixvpu_dma_queue_return(p->dma, p, &p->plan, CIXVPU_DMA_OUTPUT,
	    next, result.offset, result.filled, destination, capacity);
	if (error != 0)
		return cixvpu_prepare_queue_failure(p, error);
	p->queue.rx[1] = next;
	p->queue.output_state = 2;
	p->queue.output_eof = (result.flags & UINT32_C(0x100010)) != 0;
	p->queue.pending = 0;
	p->queue.returns++;
	*out = result;
	return 0;
}

int
cixvpu_prepared_queue_hold(struct cixvpu_prepared *p, int reason)
{

	if (cpu_intr_p() || cpu_softintr_p())
		return EWOULDBLOCK;
	if (p == NULL || reason < 0)
		return EINVAL;
	if (p->boot.error != 0) {
		p->queue.held = true;
		return p->boot.error;
	}
	if (!p->queue.enabled || p->queue.held)
		return EBUSY;
	p->queue.held = true;
	p->boot.error = reason;
	return 0;
}
