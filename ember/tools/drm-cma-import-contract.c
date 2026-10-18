/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD, AI-assisted; native imported-page ownership controls. */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static unsigned checks;
#define CHECK(x) do { checks++; if (!(x)) { \
	fprintf(stderr, "line %u: %s\n", __LINE__, #x); exit(1); } } while (0)
#define KASSERT(x) CHECK(x)
#define PAGE_SIZE 4096
#define PAGE_SHIFT 12
#define KM_SLEEP 1
#define BUS_DMA_WAITOK 1
#define BUS_DMA_PREFETCHABLE 2
#define VMEM_ADDR_MIN 0
#define VMEM_ADDR_MAX UINTPTR_MAX
#define VM_BESTFIT 1
#define VM_NOSLEEP 2
#define roundup(x, y) (((x) + (y) - 1) & ~((y) - 1))
typedef uintptr_t bus_addr_t;
typedef size_t bus_size_t;
typedef uintptr_t paddr_t;
typedef void *bus_dma_tag_t;
typedef uintptr_t vmem_addr_t;
typedef struct { bus_addr_t ds_addr; bus_size_t ds_len; } bus_dma_segment_t;
struct vm_page { uintptr_t address; };
struct page { struct vm_page p_vmp; };
#define VM_PAGE_TO_PHYS(p) ((p)->address)
#define PHYS_TO_BUS_MEM(t, p) ((void)(t), (p))
struct sg_table { struct page **pages; unsigned npages; };
struct dma_buf_attachment { int pinned; };
struct drm_device { bus_dma_tag_t dmat; void *cma_pool; };
struct drm_gem_object { size_t size; struct dma_buf_attachment *import_attach; };
struct drm_gem_cma_object {
	struct drm_gem_object base;
	void *dmamap;
	bus_dma_segment_t dmasegs[1];
	bus_size_t dmasize;
	bus_dma_tag_t dmat;
	struct sg_table *sgt;
	void *vaddr;
	vmem_addr_t vmem_addr;
	void *vmem_pool;
};
#define to_drm_gem_cma_obj(p) ((struct drm_gem_cma_object *)(p))
static unsigned objects, mappings, maps, owned, borrowed_frees;
static unsigned sg_frees, detaches, releases, fail_step, call_step, import_error;
static bool pool_success, current_import;
static char memory[PAGE_SIZE * 8];
static int bus_dmamem_import_pages(bus_dma_tag_t, bus_dma_segment_t *, int,
    int *, struct page *const *, unsigned);

static bool
fail(void)
{
	return ++call_step == fail_step;
}

static void *
kmem_zalloc(size_t n, int flags)
{
	CHECK(flags == KM_SLEEP);
	objects++;
	return calloc(1, n);
}

static void
kmem_free(void *p, size_t n)
{
	CHECK(p && n == sizeof(struct drm_gem_cma_object) && objects);
	objects--;
	free(p);
}

static int
vmem_xalloc(void *pool, size_t n, size_t align, size_t phase, size_t nocross,
    uintptr_t low, uintptr_t high, int flags, uintptr_t *out)
{
	CHECK(pool && n && align == PAGE_SIZE && !phase && !nocross &&
	    !low && high == UINTPTR_MAX && flags == 3);
	if (!pool_success)
		return ENOMEM;
	*out = 0x80000;
	owned++;
	return 0;
}

static void
vmem_xfree(void *pool, uintptr_t address, size_t n)
{
	CHECK(pool && address == 0x80000 && n && owned);
	owned--;
}

static int
bus_dmamem_alloc(bus_dma_tag_t tag, size_t n, size_t align, size_t boundary,
    bus_dma_segment_t *s, int capacity, int *count, int flags)
{
	CHECK(tag && n && align == PAGE_SIZE && !boundary && capacity == 1 &&
	    flags == BUS_DMA_WAITOK);
	if (fail())
		return ENOMEM;
	*count = 1;
	s[0].ds_addr = 0x80000;
	s[0].ds_len = n;
	owned++;
	return 0;
}

static void
bus_dmamem_free(bus_dma_tag_t tag, bus_dma_segment_t *s, int n)
{
	CHECK(tag && s && n == 1);
	if (current_import) {
		borrowed_frees++;
		return;
	}
	CHECK(owned);
	owned--;
}

static int
bus_dmamem_map(bus_dma_tag_t tag, bus_dma_segment_t *s, int n,
    size_t size, void **out, int flags)
{
	CHECK(tag && s && n == 1 && size <= sizeof(memory) && flags == 3);
	if (fail())
		return EIO;
	*out = memory;
	mappings++;
	return 0;
}

static void
bus_dmamem_unmap(bus_dma_tag_t tag, void *p, size_t n)
{
	CHECK(tag && p == memory && n && mappings);
	mappings--;
}

static int
bus_dmamap_create(bus_dma_tag_t tag, size_t n, int cap, size_t max,
    size_t boundary, int flags, void **out)
{
	CHECK(tag && n && cap == 1 && max == n && !boundary &&
	    flags == BUS_DMA_WAITOK);
	if (fail())
		return EIO;
	*out = &maps;
	maps++;
	return 0;
}

static void
bus_dmamap_destroy(bus_dma_tag_t tag, void *m)
{
	CHECK(tag && m == &maps && maps);
	maps--;
}

static int
bus_dmamap_load(bus_dma_tag_t tag, void *m, void *p, size_t n,
    void *process, int flags)
{
	CHECK(tag && m == &maps && p == memory && n && !process &&
	    flags == BUS_DMA_WAITOK);
	return fail() ? EIO : 0;
}

static void
bus_dmamap_unload(bus_dma_tag_t tag, void *m)
{
	CHECK(tag && m == &maps && maps);
}

static int
drm_prime_sg_to_bus_dmamem(bus_dma_tag_t tag, bus_dma_segment_t *s,
    int cap, int *n, struct sg_table *sg)
{
	if (import_error)
		return -(int)import_error;
	return -bus_dmamem_import_pages(tag, s, cap, n, sg->pages, sg->npages);
}

/* The generic PRIME layer owns this provider callback. */
void
drm_prime_gem_destroy(struct drm_gem_object *o, struct sg_table *s)
{
	CHECK(o->import_attach && o->import_attach->pinned && s &&
	    !mappings && !maps);
	o->import_attach->pinned = 0;
	sg_frees++;
	detaches++;
}

/* The old implementation calls this incomplete cleanup directly. */
void
drm_prime_sg_free(struct sg_table *s)
{
	CHECK(s);
	sg_frees++;
}

static void
drm_gem_private_object_init(struct drm_device *d, struct drm_gem_object *o,
    size_t n)
{
	CHECK(d && o && n);
	o->size = n;
}

static void
drm_gem_free_mmap_offset(struct drm_gem_object *o)
{
	CHECK(o);
}

static void
drm_gem_object_release(struct drm_gem_object *o)
{
	CHECK(o);
	releases++;
}

#include "bodies.h"

static void
reset(void)
{
	CHECK(!objects && !mappings && !maps && !owned);
	borrowed_frees = sg_frees = detaches = releases = 0;
	fail_step = call_step = import_error = 0;
	pool_success = current_import = false;
	memset(memory, 0x5a, sizeof(memory));
}

static void
page_cases(void)
{
	struct page pages[8];
	struct page *p[8];
	bus_dma_segment_t s[3], canary;
	int n;

	memset(&canary, 0xa5, sizeof(canary));
	for (unsigned i = 0; i < 8; i++) {
		pages[i].p_vmp.address = 0x1000 + i * PAGE_SIZE;
		p[i] = &pages[i];
	}
	for (unsigned count = 1; count <= 8; count++) {
		n = -1;
		memset(s, 0xa5, sizeof(s));
		CHECK(bus_dmamem_import_pages(NULL, s, 1, &n, p, count) == 0);
		CHECK(n == 1 && s[0].ds_addr == 0x1000 &&
		    s[0].ds_len == count * PAGE_SIZE);
		CHECK(memcmp(&s[1], &canary, sizeof(canary)) == 0);
	}
	pages[2].p_vmp.address = 0x10000;
	n = -1;
	CHECK(bus_dmamem_import_pages(NULL, s, 1, &n, p, 3) == EFBIG);
	CHECK(n == -1 && memcmp(&s[1], &canary, sizeof(canary)) == 0);
	CHECK(bus_dmamem_import_pages(NULL, s, 2, &n, p, 3) == 0);
	CHECK(n == 2 && s[0].ds_len == 2 * PAGE_SIZE &&
	    s[1].ds_addr == 0x10000);
	CHECK(memcmp(&s[2], &canary, sizeof(canary)) == 0);
	n = -1;
	CHECK(bus_dmamem_import_pages(NULL, s, 0, &n, p, 1) == EINVAL);
	CHECK(n == -1);
	CHECK(bus_dmamem_import_pages(NULL, s, -1, &n, p, 1) == EINVAL);
	CHECK(n == -1);
	CHECK(bus_dmamem_import_pages(NULL, s, 1, &n, p, 0) == EINVAL);
	CHECK(n == -1);
	/* Address wrap must not merge the last page with page zero. */
	pages[0].p_vmp.address = UINTPTR_MAX - PAGE_SIZE + 1;
	pages[1].p_vmp.address = 0;
	CHECK(bus_dmamem_import_pages(NULL, s, 2, &n, p, 2) == 0);
	CHECK(n == 2 && s[0].ds_len == PAGE_SIZE && s[1].ds_len == PAGE_SIZE);
}

static void
cma_cases(bool baseline)
{
	struct page pages[2] = {{{0x1000}}, {{0x2000}}};
	struct page *p[2] = {&pages[0], &pages[1]};
	struct sg_table sg = {p, 2};
	struct dma_buf_attachment attach = {1};
	struct drm_device d = {(void *)&sg, NULL};
	struct drm_gem_cma_object *c;

	reset();
	current_import = true;
	c = drm_gem_cma_create_internal(&d, 2 * PAGE_SIZE, &sg);
	CHECK(c && memory[0] == 0x5a);
	/* The generic importer publishes this field after successful creation. */
	c->base.import_attach = &attach;
	drm_gem_cma_free_object(&c->base);
	CHECK(borrowed_frees == 0 && sg_frees == 1 && detaches == 1 &&
	    !attach.pinned && releases == 1);
	if (baseline)
		return;
	for (unsigned stage = 1; stage <= 3; stage++) {
		reset();
		current_import = true;
		fail_step = stage;
		attach.pinned = 1;
		c = drm_gem_cma_create_internal(&d, 2 * PAGE_SIZE, &sg);
		CHECK(c == NULL);
		CHECK(!objects && !mappings && !maps && !owned &&
		    !borrowed_frees && !sg_frees && !detaches && attach.pinned);
	}
	reset();
	current_import = true;
	import_error = EFBIG;
	CHECK(drm_gem_cma_create_internal(&d, 2 * PAGE_SIZE, &sg) == NULL);
	CHECK(!objects && !mappings && !maps && !borrowed_frees && !sg_frees);
	for (unsigned stage = 0; stage <= 4; stage++) {
		reset();
		fail_step = stage;
		c = drm_gem_cma_create(&d, PAGE_SIZE);
		if (stage) {
			CHECK(c == NULL);
		} else {
			CHECK(c && memory[0] == 0);
			drm_gem_cma_free_object(&c->base);
		}
		CHECK(!objects && !mappings && !maps && !owned &&
		    !borrowed_frees && !sg_frees);
	}
	for (unsigned stage = 0; stage <= 3; stage++) {
		reset();
		pool_success = true;
		d.cma_pool = &d;
		fail_step = stage;
		c = drm_gem_cma_create(&d, PAGE_SIZE);
		if (stage) {
			CHECK(c == NULL);
		} else {
			CHECK(c && c->vmem_pool == &d);
			drm_gem_cma_free_object(&c->base);
		}
	}
	reset();
}

int
main(int argc, char **argv)
{
	if (argc == 2 && strcmp(argv[1], "cma-baseline") == 0) {
		cma_cases(true);
	} else {
		page_cases();
		cma_cases(false);
	}
	printf("Native CMA import: %u checks passed\n", checks);
	return 0;
}
