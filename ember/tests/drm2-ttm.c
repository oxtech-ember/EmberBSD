/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Kernel-side checks of TTM of Linux 6.18 on the layer (drmkms618_ttm): a
 * ttm_device with a fake driver, buffer objects in system memory, the GTT
 * (system pages bound to the "device") and a fake VRAM that is a range of
 * RAM reached through ioremap_wc.  Scenarios: device init and fini, BOs in
 * SYSTEM and TT with the three cache attributes, kmap and vmap, validate
 * between the places, memcpy moves to and from the fake VRAM, eviction by
 * pressure, release without leaking pages; the pool with and without a
 * bus_dma tag, backup and restore of the pages.  The scenarios are written
 * for this layer, after the contract in the drm/ttm headers; the kunit tests of TTM
 * were used only to see which behaviours are worth a check.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/atomic.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/systm.h>

#include <uvm/uvm_extern.h>

#include <linux/dma-resv.h>
#include <linux/iosys-map.h>
#include <linux/mm.h>
#include <linux/slab.h>

#include <drm/drm_drv.h>
#include <drm/drm_gem.h>
#include <drm/drm_vma_manager.h>
#include <drm/ttm/ttm_bo.h>
#include <drm/ttm/ttm_backup.h>
#include <drm/ttm/ttm_device.h>
#include <drm/ttm/ttm_placement.h>
#include <drm/ttm/ttm_pool.h>
#include <drm/ttm/ttm_range_manager.h>
#include <drm/ttm/ttm_resource.h>
#include <drm/ttm/ttm_tt.h>

extern struct x86_bus_dma_tag pci_bus_dma_tag;

void	drm618_ttm_set_dmat(const struct device *, bus_dma_tag_t);
unsigned long	drm618_ttm_pool_pages(void);
struct uvm_object	*ttm_bo_uvm_create(struct ttm_buffer_object *);

static int t_run, t_fail;

#define	CHECK(cond)	do {						\
	t_run++;							\
	if (!(cond)) {							\
		t_fail++;						\
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);	\
	}								\
} while (0)

#define	T_NPAGES_TT	64
#define	T_NPAGES_VRAM	16

static struct drm_device t_drm;
static struct drm_driver t_driver;
static struct drm_vma_offset_manager t_vma;
static struct ttm_device t_bdev;
static paddr_t t_vram_pa;
static struct pglist t_vram_pgl;
static enum ttm_caching t_caching;
static bool t_sg;
static volatile unsigned t_destroyed;

struct t_bo {
	struct ttm_buffer_object	base;
};

/* ---- the fake driver --------------------------------------------------------- */

static struct ttm_tt *
t_tt_create(struct ttm_buffer_object *bo, uint32_t page_flags)
{
	struct ttm_tt *tt = kzalloc(sizeof(*tt), GFP_KERNEL);
	int error;

	if (tt == NULL)
		return NULL;
	if (t_sg)
		error = ttm_sg_tt_init(tt, bo, page_flags, t_caching);
	else
		error = ttm_tt_init(tt, bo, page_flags, t_caching, 0);
	if (error) {
		kfree(tt);
		return NULL;
	}
	return tt;
}

static void
t_tt_destroy(struct ttm_device *bdev __unused, struct ttm_tt *tt)
{

	ttm_tt_fini(tt);
	kfree(tt);
}

static const struct ttm_place t_sys_place = { .mem_type = TTM_PL_SYSTEM };
static const struct ttm_place t_tt_place = { .mem_type = TTM_PL_TT };
static const struct ttm_place t_vram_place = { .mem_type = TTM_PL_VRAM };

static void
t_evict_flags(struct ttm_buffer_object *bo __unused, struct ttm_placement *placement)
{

	placement->num_placement = 1;
	placement->placement = &t_sys_place;
}

static int
t_move(struct ttm_buffer_object *bo, bool evict __unused,
    struct ttm_operation_ctx *ctx, struct ttm_resource *new_mem,
    struct ttm_place *hop __unused)
{
	struct ttm_resource *old = bo->resource;

	if (old == NULL ||
	    (old->mem_type == TTM_PL_SYSTEM && bo->ttm == NULL) ||
	    (old->mem_type == TTM_PL_SYSTEM && new_mem->mem_type == TTM_PL_TT) ||
	    (old->mem_type == TTM_PL_TT && new_mem->mem_type == TTM_PL_SYSTEM)) {
		ttm_bo_move_null(bo, new_mem);
		return 0;
	}
	return ttm_bo_move_memcpy(bo, ctx, new_mem);
}

static int
t_io_mem_reserve(struct ttm_device *bdev __unused, struct ttm_resource *mem)
{

	if (mem->mem_type == TTM_PL_VRAM) {
		mem->bus.offset = t_vram_pa + ((paddr_t)mem->start << PAGE_SHIFT);
		mem->bus.is_iomem = true;
		mem->bus.caching = ttm_write_combined;
	}
	return 0;
}

static const struct ttm_device_funcs t_funcs = {
	.ttm_tt_create = t_tt_create,
	.ttm_tt_destroy = t_tt_destroy,
	.evict_flags = t_evict_flags,
	.eviction_valuable = ttm_bo_eviction_valuable,
	.move = t_move,
	.io_mem_reserve = t_io_mem_reserve,
};

static void
t_bo_destroy(struct ttm_buffer_object *bo)
{
	struct t_bo *b = container_of(bo, struct t_bo, base);

	atomic_inc_uint(&t_destroyed);
	kfree(b);
}

/* A kernel BO of npages pages in the place; reserved on return (NULL on failure). */
static struct t_bo *
t_bo_create(unsigned npages, const struct ttm_place *place, enum ttm_caching caching)
{
	struct t_bo *b = kzalloc(sizeof(*b), GFP_KERNEL);
	struct ttm_placement pl = { .num_placement = 1, .placement = place };
	struct ttm_operation_ctx ctx = { false, false };
	int error;

	if (b == NULL)
		return NULL;
	t_caching = caching;
	drm_gem_private_object_init(&t_drm, &b->base.base, (size_t)npages << PAGE_SHIFT);
	error = ttm_bo_init_reserved(&t_bdev, &b->base, ttm_bo_type_kernel, &pl, 0,
	    &ctx, NULL, NULL, t_bo_destroy);
	if (error) {
		printf("t_bo_create: ttm_bo_init_reserved %d\n", error);
		return NULL;
	}
	return b;
}

static void
t_bo_free(struct t_bo *b)
{

	dma_resv_unlock(b->base.base.resv);
	ttm_bo_put(&b->base);
}

static int
t_validate(struct t_bo *b, const struct ttm_place *place)
{
	struct ttm_placement pl = { .num_placement = 1, .placement = place };
	struct ttm_operation_ctx ctx = { false, false };

	return ttm_bo_validate(&b->base, &pl, &ctx);
}

static void
t_fill(void *va, size_t len, unsigned seed)
{
	uint32_t *p = va;
	size_t i;

	for (i = 0; i < len / sizeof(*p); i++)
		p[i] = seed * 2654435761u + (uint32_t)i;
}

static bool
t_check(const void *va, size_t len, unsigned seed)
{
	const uint32_t *p = va;
	size_t i;

	for (i = 0; i < len / sizeof(*p); i++)
		if (p[i] != seed * 2654435761u + (uint32_t)i)
			return false;
	return true;
}

/* Write a pattern through a kmap of the whole BO. */
static int
t_bo_write(struct t_bo *b, unsigned seed)
{
	struct ttm_bo_kmap_obj map;
	size_t len = b->base.base.size;
	int error = ttm_bo_kmap(&b->base, 0, len >> PAGE_SHIFT, &map);
	bool is_iomem;

	if (error)
		return error;
	t_fill(ttm_kmap_obj_virtual(&map, &is_iomem), len, seed);
	ttm_bo_kunmap(&map);
	return 0;
}

static int
t_bo_verify(struct t_bo *b, unsigned seed, bool *ok)
{
	struct ttm_bo_kmap_obj map;
	size_t len = b->base.base.size;
	int error = ttm_bo_kmap(&b->base, 0, len >> PAGE_SHIFT, &map);
	bool is_iomem;

	if (error)
		return error;
	*ok = t_check(ttm_kmap_obj_virtual(&map, &is_iomem), len, seed);
	ttm_bo_kunmap(&map);
	return 0;
}

int	drm2_ttm_run(void);

int
drm2_ttm_run(void)
{
	struct t_bo *a, *b, *c;
	struct ttm_resource_manager *man;
	unsigned avail0, avail1;
	bool ok;
	int error;

	/* ---- 1. the device ------------------------------------------------------ */
	memset(&t_drm, 0, sizeof(t_drm));
	t_drm.driver = &t_driver;	/* drm_core_check_feature reads it */
	drm_vma_offset_manager_init(&t_vma, 0, 1 << 20);
	TAILQ_INIT(&t_vram_pgl);
	CHECK(uvm_pglistalloc((psize_t)T_NPAGES_VRAM << PAGE_SHIFT, 0, (paddr_t)-1,
	    PAGE_SIZE, 0, &t_vram_pgl, 1, 1) == 0);
	t_vram_pa = VM_PAGE_TO_PHYS(TAILQ_FIRST(&t_vram_pgl));

	/* the layer: ioremap_wc/ioremap/memremap of the RAM behind the fake VRAM, iounmap */
	{
		uint32_t *w = ioremap_wc(t_vram_pa, T_NPAGES_VRAM << PAGE_SHIFT);
		uint32_t *direct = (uint32_t *)page_address((struct page *)PHYS_TO_VM_PAGE(t_vram_pa));

		CHECK(w != NULL);
		if (w != NULL) {
			t_fill(w, T_NPAGES_VRAM << PAGE_SHIFT, 7);
			CHECK(t_check(w, T_NPAGES_VRAM << PAGE_SHIFT, 7));
			iounmap(w);
		}
		w = ioremap(t_vram_pa + 100, 3 * PAGE_SIZE);
		CHECK(w != NULL);
		if (w != NULL) {
			w[0] = 0x12345678;
			CHECK(w[0] == 0x12345678);
			iounmap(w);
		}
		w = memremap(t_vram_pa, PAGE_SIZE, MEMREMAP_WB);
		CHECK(w != NULL);
		if (w != NULL)
			memunmap(w);
		(void)direct;
	}

	avail0 = uvm_availmem(true);
	CHECK(ttm_device_init(&t_bdev, &t_funcs, NULL, NULL, &t_vma, false, false) == 0);
	CHECK(ttm_range_man_init(&t_bdev, TTM_PL_TT, true, T_NPAGES_TT) == 0);
	CHECK(ttm_range_man_init(&t_bdev, TTM_PL_VRAM, false, T_NPAGES_VRAM) == 0);
	man = ttm_manager_type(&t_bdev, TTM_PL_TT);
	CHECK(man != NULL && man->use_tt);
	CHECK(ttm_manager_type(&t_bdev, TTM_PL_VRAM) != NULL);
	CHECK(ttm_manager_type(&t_bdev, TTM_PL_SYSTEM) == &t_bdev.sysman);

	/* ---- 2. a BO in system memory: no pages until it is used ----------------- */
	t_destroyed = 0;
	a = t_bo_create(4, &t_sys_place, ttm_cached);
	CHECK(a != NULL);
	CHECK(a->base.resource->mem_type == TTM_PL_SYSTEM);
	CHECK(a->base.ttm != NULL && !ttm_tt_is_populated(a->base.ttm));

	/* kmap populates it; a pattern written is read back */
	CHECK(t_bo_write(a, 1) == 0);
	CHECK(ttm_tt_is_populated(a->base.ttm));
	CHECK(t_bo_verify(a, 1, &ok) == 0 && ok);

	/* ---- 3. validate SYSTEM -> TT -> SYSTEM keeps the pages ------------------- */
	CHECK(t_validate(a, &t_tt_place) == 0);
	CHECK(a->base.resource->mem_type == TTM_PL_TT);
	CHECK(t_bo_verify(a, 1, &ok) == 0 && ok);
	CHECK(t_validate(a, &t_sys_place) == 0);
	CHECK(a->base.resource->mem_type == TTM_PL_SYSTEM);
	CHECK(t_bo_verify(a, 1, &ok) == 0 && ok);

	/* vmap of the whole object */
	{
		struct iosys_map map;

		CHECK(ttm_bo_vmap(&a->base, &map) == 0);
		CHECK(!map.is_iomem && map.vaddr != NULL);
		CHECK(t_check(map.vaddr, 4 << PAGE_SHIFT, 1));
		t_fill(map.vaddr, 4 << PAGE_SHIFT, 2);
		ttm_bo_vunmap(&a->base, &map);
		CHECK(t_bo_verify(a, 2, &ok) == 0 && ok);
	}
	t_bo_free(a);
	CHECK(t_destroyed == 1);

	/* ---- 3b. a tt with DMA addresses (ttm_sg_tt_init): filled when it is populated --- */
	t_sg = true;
	a = t_bo_create(4, &t_sys_place, ttm_cached);
	CHECK(a != NULL);
	if (a != NULL) {
		unsigned i;

		CHECK(a->base.ttm->dma_address != NULL);
		CHECK(t_validate(a, &t_tt_place) == 0);	/* populates */
		CHECK(ttm_tt_is_populated(a->base.ttm));
		for (i = 0; i < 4; i++)
			CHECK(a->base.ttm->dma_address[i] ==
			    VM_PAGE_TO_PHYS(&a->base.ttm->pages[i]->p_vmp));
		t_bo_free(a);
	}
	t_sg = false;

	/* ---- 4. the three cache attributes: kmap/vmap with the attribute ---------- */
	{
		enum ttm_caching c3[3] = { ttm_cached, ttm_write_combined, ttm_uncached };
		int i;

		for (i = 0; i < 3; i++) {
			struct iosys_map map;

			a = t_bo_create(8, &t_sys_place, c3[i]);
			CHECK(a != NULL && a->base.ttm->caching == c3[i]);
			CHECK(t_validate(a, &t_tt_place) == 0);
			CHECK(ttm_bo_vmap(&a->base, &map) == 0);
			t_fill(map.vaddr, 8 << PAGE_SHIFT, 10 + i);
			CHECK(t_check(map.vaddr, 8 << PAGE_SHIFT, 10 + i));
			ttm_bo_vunmap(&a->base, &map);
			/* one page by kmap with the attribute */
			{
				struct ttm_bo_kmap_obj km;
				bool io;

				CHECK(ttm_bo_kmap(&a->base, 3, 1, &km) == 0);
				((uint32_t *)ttm_kmap_obj_virtual(&km, &io))[5] = 0xabcd0000 + i;
				CHECK(((uint32_t *)ttm_kmap_obj_virtual(&km, &io))[5] == 0xabcd0000u + i);
				ttm_bo_kunmap(&km);
			}
			t_bo_free(a);
		}
	}

	/* ---- 5. a fake VRAM: memcpy moves, eviction by pressure ------------------- */
	a = t_bo_create(T_NPAGES_VRAM, &t_sys_place, ttm_cached);
	CHECK(a != NULL);
	CHECK(t_bo_write(a, 20) == 0);
	CHECK(t_validate(a, &t_vram_place) == 0);		/* SYSTEM -> VRAM, memcpy */
	CHECK(a->base.resource->mem_type == TTM_PL_VRAM);
	CHECK(a->base.resource->bus.is_iomem);
	CHECK(t_bo_verify(a, 20, &ok) == 0 && ok);		/* read through ioremap_wc */
	dma_resv_unlock(a->base.base.resv);

	/* b wants the same VRAM: a is evicted to SYSTEM (evict_flags) */
	b = t_bo_create(T_NPAGES_VRAM, &t_vram_place, ttm_cached);
	CHECK(b != NULL);
	if (b != NULL) {
		CHECK(b->base.resource->mem_type == TTM_PL_VRAM);
		dma_resv_lock(a->base.base.resv, NULL);
		CHECK(a->base.resource->mem_type == TTM_PL_SYSTEM);	/* it was evicted */
		CHECK(t_bo_verify(a, 20, &ok) == 0 && ok);		/* and kept its contents */
		dma_resv_unlock(a->base.base.resv);
		CHECK(t_bo_write(b, 21) == 0);
		CHECK(t_bo_verify(b, 21, &ok) == 0 && ok);
		t_bo_free(b);
	}
	dma_resv_lock(a->base.base.resv, NULL);
	t_bo_free(a);

	/* ---- 5b. the uvm mapping of a BO: a fault enters the pages ----------------------- */
	{
		enum ttm_caching c2[2] = { ttm_cached, ttm_write_combined };
		int i;

		for (i = 0; i < 2; i++) {
			struct uvm_object *uo;
			vaddr_t va = 0;
			unsigned before = t_destroyed;
			size_t len = 8 << PAGE_SHIFT;

			a = t_bo_create(8, &t_sys_place, c2[i]);
			CHECK(a != NULL);
			if (a == NULL)
				continue;
			CHECK(t_bo_write(a, 60 + i) == 0);
			uo = ttm_bo_uvm_create(&a->base);
			dma_resv_unlock(a->base.base.resv);
			CHECK(uvm_map(kernel_map, &va, len, uo, 0, 0,
			    UVM_MAPFLAG(UVM_PROT_RW, UVM_PROT_RW, UVM_INH_NONE,
			    UVM_ADV_RANDOM, 0)) == 0);
			if (va != 0) {
				CHECK(t_check((void *)va, len, 60 + i));	/* the faults read the pages */
				((uint32_t *)va)[PAGE_SIZE / 4 + 1] = 0x600d0000 + i;	/* and a write goes to them */
				uvm_unmap(kernel_map, va, va + len);
			}
			/* the mapping dropped its reference; the object holds the BO until its own is gone */
			uo->pgops->pgo_detach(uo);
			dma_resv_lock(a->base.base.resv, NULL);
			CHECK(t_bo_verify(a, 60 + i, &ok) == 0 && !ok);	/* the write changed one word */
			{
				struct ttm_bo_kmap_obj km;
				bool io;

				CHECK(ttm_bo_kmap(&a->base, 1, 1, &km) == 0);
				CHECK(((uint32_t *)ttm_kmap_obj_virtual(&km, &io))[1] == 0x600d0000u + i);
				ttm_bo_kunmap(&km);
			}
			t_bo_free(a);
			CHECK(t_destroyed == before + 1);	/* nothing holds the BO any more */
		}
		/* an object in the fake VRAM: the fault enters the physical addresses of the aperture */
		a = t_bo_create(4, &t_sys_place, ttm_cached);
		CHECK(a != NULL);
		if (a != NULL) {
			struct uvm_object *uo;
			vaddr_t va = 0;

			CHECK(t_bo_write(a, 70) == 0);
			CHECK(t_validate(a, &t_vram_place) == 0);
			uo = ttm_bo_uvm_create(&a->base);
			dma_resv_unlock(a->base.base.resv);
			CHECK(uvm_map(kernel_map, &va, 4 << PAGE_SHIFT, uo, 0, 0,
			    UVM_MAPFLAG(UVM_PROT_RW, UVM_PROT_RW, UVM_INH_NONE,
			    UVM_ADV_RANDOM, 0)) == 0);
			if (va != 0) {
				CHECK(t_check((void *)va, 4 << PAGE_SHIFT, 70));
				uvm_unmap(kernel_map, va, va + (4 << PAGE_SHIFT));
			}
			uo->pgops->pgo_detach(uo);
			dma_resv_lock(a->base.base.resv, NULL);
			t_bo_free(a);
		}
	}

	/* ---- 6. evict all, release, no pages left --------------------------------- */
	c = t_bo_create(10, &t_tt_place, ttm_cached);
	CHECK(c != NULL);
	CHECK(t_bo_write(c, 30) == 0);
	dma_resv_unlock(c->base.base.resv);
	CHECK(ttm_resource_manager_evict_all(&t_bdev, ttm_manager_type(&t_bdev, TTM_PL_TT)) == 0);
	dma_resv_lock(c->base.base.resv, NULL);
	CHECK(c->base.resource->mem_type == TTM_PL_SYSTEM);
	CHECK(t_bo_verify(c, 30, &ok) == 0 && ok);
	t_bo_free(c);

	/* ---- 6b. a sharp leak check: 128 pages, three times --------------------------- */
	{
		unsigned before, after;
		int i;

		/* one round to size the pools and the kernel address space, not measured */
		c = t_bo_create(128, &t_sys_place, ttm_cached);
		CHECK(c != NULL);
		if (c != NULL) {
			CHECK(t_bo_write(c, 49) == 0);
			t_bo_free(c);
		}
		before = uvm_availmem(true);
		CHECK(drm618_ttm_pool_pages() == 0);	/* every page the pool gave out came back */
		for (i = 0; i < 3; i++) {
			c = t_bo_create(128, &t_sys_place, ttm_cached);
			CHECK(c != NULL);
			if (c == NULL)
				break;
			CHECK(t_bo_write(c, 50 + i) == 0);
			t_bo_free(c);
		}
		CHECK(drm618_ttm_pool_pages() == 0);
		after = uvm_availmem(true);
		CHECK((int)after >= (int)before - 8);	/* a leak would be 128 pages a round */
		if ((int)after < (int)before - 8)
			printf("drm2-ttm: leak check: free pages before %u after %u\n", before, after);
	}

	ttm_range_man_fini(&t_bdev, TTM_PL_TT);
	ttm_range_man_fini(&t_bdev, TTM_PL_VRAM);
	ttm_device_fini(&t_bdev);
	avail1 = uvm_availmem(true);
	/* every page the pools gave out is back (the free page count itself moves with first-use structures) */
	CHECK(drm618_ttm_pool_pages() == 0);
	(void)avail0;
	(void)avail1;

	uvm_pglistfree(&t_vram_pgl);
	drm_vma_offset_manager_destroy(&t_vma);

	/* ---- 7. the pool: pages, zeroing, DMA addresses ---------------------------- */
	{
		struct ttm_pool pool;
		struct ttm_tt tt;
		struct ttm_operation_ctx ctx = { false, false };
		unsigned i, n = 8;
		static struct device *dummy_dev = (struct device *)&t_drm;

		CHECK(ttm_pool_mgr_init(1024) == 0);
		memset(&tt, 0, sizeof(tt));
		tt.num_pages = n;
		tt.page_flags = TTM_TT_FLAG_ZERO_ALLOC;
		tt.pages = kzalloc(n * sizeof(*tt.pages), GFP_KERNEL);
		tt.dma_address = kzalloc(n * sizeof(*tt.dma_address), GFP_KERNEL);

		/* without a tag: bus address = physical address, zeroed pages */
		ttm_pool_init(&pool, dummy_dev, NUMA_NO_NODE, false, false);
		error = ttm_pool_alloc(&pool, &tt, &ctx);
		CHECK(error == 0);
		if (error)
			goto pool_done;
		for (i = 0; i < n; i++) {
			CHECK(tt.pages[i] != NULL);
			CHECK(tt.dma_address[i] == VM_PAGE_TO_PHYS(&tt.pages[i]->p_vmp));
		}
		{
			const uint32_t *w = page_address(tt.pages[0]);
			bool zero = true;
			size_t k;

			for (k = 0; k < PAGE_SIZE / 4; k++)
				zero = zero && w[k] == 0;
			CHECK(zero);
		}
		ttm_pool_free(&pool, &tt);
		CHECK(tt.pages[0] == NULL && tt.dma_address[0] == 0);

		/* with the tag of the PCI bus (identity here): a map, addresses from its segments */
		drm618_ttm_set_dmat(dummy_dev, &pci_bus_dma_tag);
		error = ttm_pool_alloc(&pool, &tt, &ctx);
		if (error)
			printf("drm2-ttm: ttm_pool_alloc with a tag: %d\n", error);
		CHECK(error == 0);
		if (error)
			goto pool_done;
		for (i = 0; i < n; i++)
			CHECK(tt.dma_address[i] == VM_PAGE_TO_PHYS(&tt.pages[i]->p_vmp));
		ttm_pool_free(&pool, &tt);

		/* a dma32 pool allocates below 4 GiB */
		ttm_pool_init(&pool, dummy_dev, NUMA_NO_NODE, false, true);
		error = ttm_pool_alloc(&pool, &tt, &ctx);
		CHECK(error == 0);
		if (error)
			goto pool_done;
		for (i = 0; i < n; i++)
			CHECK(VM_PAGE_TO_PHYS(&tt.pages[i]->p_vmp) < (1ULL << 32));
		ttm_pool_free(&pool, &tt);
		drm618_ttm_set_dmat(dummy_dev, NULL);

		/* backup and restore of the pages through a shmem file */
		ttm_pool_init(&pool, dummy_dev, NUMA_NO_NODE, false, false);
		tt.page_flags = 0;
		error = ttm_pool_alloc(&pool, &tt, &ctx);
		CHECK(error == 0);
		if (error)
			goto pool_done;
		for (i = 0; i < n; i++)
			t_fill(page_address(tt.pages[i]), PAGE_SIZE, 40 + i);
		tt.backup = ttm_backup_shmem_create((loff_t)n << PAGE_SHIFT);
		CHECK(!IS_ERR(tt.backup));
		if (!IS_ERR(tt.backup)) {
			struct ttm_backup_flags bf = { 0 };

			CHECK(ttm_pool_backup(&pool, &tt, &bf) == (long)n);
			for (i = 0; i < n; i++)
				CHECK(ttm_backup_page_ptr_is_handle(tt.pages[i]));
			CHECK(ttm_pool_restore_and_alloc(&pool, &tt, &ctx) == 0);
			for (i = 0; i < n; i++)
				CHECK(t_check(page_address(tt.pages[i]), PAGE_SIZE, 40 + i));
			ttm_pool_free(&pool, &tt);
			ttm_backup_fini(tt.backup);
		} else {
			ttm_pool_free(&pool, &tt);
		}
pool_done:
		kfree(tt.pages);
		kfree(tt.dma_address);
		ttm_pool_fini(&pool);
		ttm_pool_mgr_fini();
	}

	printf("drm2-ttm: %d checks, %d failed\n", t_run, t_fail);
	return t_fail;
}
