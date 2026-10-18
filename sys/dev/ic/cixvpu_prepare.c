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

#define PREPARE_TABLES (CIXVPU_FW_PLAN_MAX_CORES + 2U)

struct cixvpu_prepare_entry {
	uint32_t table, backing, address, word;
	unsigned int attribute, permission;
	bool link;
};

struct cixvpu_prepare_boot {
	bool enabled, published;
	int error;
	uint16_t cursor;
	struct cixvpu_ring_checksum checksum;
	struct cixvpu_dma_boot_snapshot snapshot;
};

struct cixvpu_prepared {

	struct cixvpu_dma *dma;
	struct cixvpu_fw_plan plan;
	struct cixvpu_prepare_boot boot;
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
	if (roots != 1 || work->count != plan->cores + 2)
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
		    p->permission != map.permission || p->role >= CIXVPU_FW_PLAN_ROOT)
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
    struct cixvpu_prepared **out, bool boot)
{
	static const uint8_t boot_version[16] = "r0p0-sum0005";
	struct cixvpu_fw_plan snapshot, checked;
	struct cixvpu_prepared *prepared = NULL;
	struct cixvpu_prepare_work *work = NULL;
	struct cixvpu_prepare_span spans[] = {
		{ image, image_size }, { expected_plan, sizeof(*expected_plan) },
		{ expected_digest, 32 }, { owned, sizeof(*owned) }, { out, sizeof(*out) }
	};
	SHA256_CTX hash;
	uint8_t digest[SHA256_DIGEST_LENGTH], pin[SHA256_DIGEST_LENGTH];
	size_t work_bytes, prepared_bytes, capacity, pages;
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
	for (i = 0; i < __arraycount(spans); i++)
		if (!cixvpu_prepare_span_valid(&spans[i]))
			return EINVAL;
	/* Publishing handles must not change each other or immutable inputs. */
	for (i = 3; i < __arraycount(spans); i++)
		for (j = 0; j < i; j++)
			if (cixvpu_prepare_overlap(&spans[i], &spans[j]))
				return EINVAL;
	snapshot = *expected_plan;
	memcpy(pin, expected_digest, sizeof(pin));
	(void)SHA256_Init(&hash);
	(void)SHA256_Update(&hash, image, image_size);
	(void)SHA256_Final(digest, &hash);
	if (memcmp(digest, pin, sizeof(pin)) != 0)
		return EINVAL;
	error = cixvpu_fw_plan_build(image, image_size, snapshot.cores, &checked);
	if (error != 0)
		return error;
	if (snapshot.cores != checked.cores ||
	    snapshot.backing_pages != checked.backing_pages ||
	    snapshot.mappings != checked.mappings ||
	    !cixvpu_prepare_info_equal(&snapshot.info, &checked.info))
		return EINVAL;
	if (boot && (checked.cores != 1 || checked.info.major != 3 ||
	    checked.info.minor != 5 || memcmp(checked.info.version,
	    boot_version, sizeof(checked.info.version)) != 0))
		return ENOTSUP;
	capacity = (size_t)checked.mappings + CIXVPU_FW_PLAN_MAX_CORES + 1;
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
	    __arraycount(spans), work);
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

	return cixvpu_prepare_common(owned, plan, image, size, pin, out, false);
}

int
cixvpu_prepare_boot_cpu(struct cixvpu_dma **owned,
    const struct cixvpu_fw_plan *plan, const void *image, size_t size,
    const uint8_t pin[32], struct cixvpu_prepared **out)
{

	return cixvpu_prepare_common(owned, plan, image, size, pin, out, true);
}

static int
cixvpu_prepare_boot_output(struct cixvpu_prepared *p, void *out,
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
	capacity = (size_t)p->plan.mappings + CIXVPU_FW_PLAN_MAX_CORES + 1;
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
	error = cixvpu_prepare_boot_output(p, out, sizeof(*out),
	    _Alignof(enum cixvpu_boot_debug_result), &fault);
	if (error != 0) {
		if (fault && p->boot.published) {
			p->boot.error = error;
			p->boot.snapshot.rpc_valid = false;
		}
		return error;
	}
	if (!p->boot.published)
		return EBUSY;
	p->boot.snapshot.rpc_valid = false;
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
cixvpu_prepared_boot_receive(struct cixvpu_prepared *p,
    struct cixvpu_boot_reply *out)
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

	error = cixvpu_prepare_boot_output(p, out, sizeof(*out),
	    _Alignof(struct cixvpu_boot_reply), &fault);
	if (error != 0) {
		if (fault && p->boot.published) {
			p->boot.error = error;
			p->boot.snapshot.rpc_valid = false;
		}
		return error;
	}
	if (!p->boot.published)
		return EBUSY;
	if (p->boot.error != 0)
		return p->boot.error;
	p->boot.snapshot.rpc_valid = false;
	error = cixvpu_dma_boot_snapshot(p->dma, p, &p->plan, &p->boot.snapshot);
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
	*out = reply;
	return 0;
failed:
	if (error != EAGAIN)
		p->boot.error = error;
	return error;
}
