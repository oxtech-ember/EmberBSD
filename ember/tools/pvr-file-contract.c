/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Actual file handlers; model only DRM, allocation, locks and VM boundaries. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PAGE_SIZE ((size_t)1 << PAGE_SHIFT)
#define KM_SLEEP 1
#define MUTEX_DEFAULT 0
#define IPL_NONE 0
#define GFP_KERNEL 1
#define GFP_NOWAIT 0
#define DRM_RENDER_ALLOW 1
#define DRM_UNLOCKED 2
#define DRM_COMMAND_BASE 0x40
#define DRM_IOWR(n, t) (n)
#define DRM_IOW(n, t) (n)
#define SLOTS 32
#define BASE UINT64_C(0x8000000000)
#define HEAP_SIZE (UINT64_C(128) << 30)
typedef int32_t __s32;
typedef uint16_t __u16;
typedef uint32_t __u32;
typedef uint64_t __u64;
typedef void *bus_dma_tag_t;
#include "pvr-uapi-body.h"
#include "pvr_backend.h"
#include "pvr_file.h"
#include "pvr_exec.h"
#include <arm/sunxi/powervr/pvr_rogue_fwif.h>
#define BUS_DMASYNC_PREREAD 1
#define BUS_DMASYNC_PREWRITE 2
#define BUS_DMASYNC_POSTREAD 4
#define BUS_DMASYNC_POSTWRITE 8

static atomic_uint checks, allocations, objects, vms;
#define CHECK(e) do { atomic_fetch_add(&checks, 1); if (!(e)) { \
	fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #e); abort(); \
} } while (0)
#define KASSERT(e) CHECK(e)
typedef struct {
	pthread_mutex_t lock;
	pthread_t owner;
	bool held;
} kmutex_t;
static _Thread_local kmutex_t *initialized_lock;
static _Thread_local bool closing, preload, destroying;
static pthread_mutex_t rendezvous = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
static bool pause_map, map_entered, destroy_waiting, map_released;
static int create_error, idr_error, mapping_error, map_error, unmap_error;
static unsigned int core_maps, core_unmaps, core_destroys, bo_queries;

static void mutex_init(kmutex_t *m, int type, int level)
{ CHECK(type == MUTEX_DEFAULT && level == IPL_NONE); CHECK(pthread_mutex_init(&m->lock, NULL) == 0); initialized_lock = m; }
static void mutex_enter(kmutex_t *m)
{
	if (destroying) {
		CHECK(pthread_mutex_lock(&rendezvous) == 0);
		/* The rendezvous holds MAP inside the core, under this lock. */
		CHECK(m->held);
		destroy_waiting = true; CHECK(pthread_cond_broadcast(&changed) == 0);
		CHECK(pthread_mutex_unlock(&rendezvous) == 0);
	}
	CHECK(pthread_mutex_lock(&m->lock) == 0);
	CHECK(!m->held); m->owner = pthread_self(); m->held = true;
}
static bool mutex_owned(kmutex_t *m)
{ return m->held && pthread_equal(m->owner, pthread_self()); }
static void mutex_exit(kmutex_t *m)
{ CHECK(mutex_owned(m)); m->held = false; CHECK(pthread_mutex_unlock(&m->lock) == 0); }
static void mutex_destroy(kmutex_t *m)
{ CHECK(!m->held); CHECK(pthread_mutex_destroy(&m->lock) == 0); }
static void *kmem_zalloc(size_t size, int flags)
{ void *p = calloc(1, size); CHECK(flags == KM_SLEEP && p != NULL); atomic_fetch_add(&allocations, 1); return p; }
static void kmem_free(void *p, size_t size)
{ CHECK(p != NULL && atomic_fetch_sub(&allocations, 1) != 0); free(p); }

struct idr { void *slots[SLOTS]; kmutex_t *gate; };
static void idr_init(struct idr *i)
{ memset(i, 0, sizeof(*i)); i->gate = initialized_lock; }
static void idr_preload(int flags)
{ CHECK(flags == GFP_KERNEL && !preload); preload = true; }
static void idr_preload_end(void)
{ CHECK(preload); preload = false; }
static int idr_alloc(struct idr *i, void *p, int start, int end, int flags)
{
	const int error = idr_error;

	CHECK(mutex_owned(i->gate) && preload && start == 1 && end == 0 && flags == GFP_NOWAIT);
	if (error != 0) { CHECK(error < 0); return error; }
	for (int n = 1; n < SLOTS; n++) if (i->slots[n] == NULL) { i->slots[n] = p; return n; }
	return -ENOSPC;
}
static void *idr_find(struct idr *i, int id)
{ CHECK(mutex_owned(i->gate) && id > 0); return id < SLOTS ? i->slots[id] : NULL; }
static void *idr_remove(struct idr *i, int id)
{ void *p; CHECK((closing || mutex_owned(i->gate)) && id > 0 && id < SLOTS); p = i->slots[id]; CHECK(p != NULL); i->slots[id] = NULL; return p; }
static void *idr_get_next(struct idr *i, int *id)
{ CHECK(closing); for (; *id < SLOTS; (*id)++) if (i->slots[*id] != NULL) return i->slots[*id]; return NULL; }
static void idr_destroy(struct idr *i)
{ for (int n = 0; n < SLOTS; n++) CHECK(i->slots[n] == NULL); }

struct drm_device { bus_dma_tag_t dmat; };
struct drm_gem_object { unsigned int refs; uint64_t size, pa, flags; bool pinned; unsigned holds; };
struct drm_file { void *driver_priv; struct drm_gem_object *handles[SLOTS]; };
struct test_map { uint64_t va, pa, size, flags; };
struct pvr_vm { struct test_map ranges[SLOTS]; int (*invalidate)(void *); void *cookie; bool failed; };
static struct pvr_vm *all_vms[SLOTS];
static struct drm_gem_object *all_objects[SLOTS];
static void drm_gem_object_get(struct drm_gem_object *o)
{ CHECK(o != NULL && o->refs != 0); o->refs++; }
static void drm_gem_object_put_unlocked(struct drm_gem_object *o)
{
	CHECK(o != NULL && o->refs != 0);
	if (--o->refs != 0) return;
	CHECK(!o->pinned && o->holds == 0);
	/* A live table cannot outlive its BO reference. */
	for (unsigned v = 0; v < SLOTS; v++) if (all_vms[v] != NULL)
		for (unsigned n = 0; n < SLOTS; n++)
			CHECK(all_vms[v]->ranges[n].size == 0 || all_vms[v]->ranges[n].pa < o->pa ||
			    all_vms[v]->ranges[n].pa - o->pa >= o->size);
	for (unsigned n = 0; n < SLOTS; n++) if (all_objects[n] == o) all_objects[n] = NULL;
	CHECK(atomic_fetch_sub(&objects, 1) != 0); free(o);
}
static struct drm_gem_object *drm_gem_object_lookup(struct drm_file *f, uint32_t h)
{ if (h >= SLOTS || f->handles[h] == NULL) return NULL; drm_gem_object_get(f->handles[h]); return f->handles[h]; }
static const struct drm_pvr_heap *pvr_heap_find(uint64_t va, uint64_t size)
{
	static const struct drm_pvr_heap heap = { .base = BASE, .size = HEAP_SIZE };
	return size != 0 && va >= BASE && va - BASE < HEAP_SIZE && size <= HEAP_SIZE - (va - BASE) ? &heap : NULL;
}
static int pvr_bo_mapping(struct drm_gem_object *o, uint64_t off, uint64_t size,
    uint64_t *pa, uint64_t *flags)
{
	bo_queries++; CHECK(o->refs >= 1);
	if (mapping_error != 0) return mapping_error;
	if (off > o->size || size > o->size - off || size == 0) return EINVAL;
	*pa = o->pa + off; *flags = o->flags; return 0;
}
static int pvr_vm_create(bus_dma_tag_t tag, struct pvr_vm **out)
{
	CHECK(tag != NULL && out != NULL && *out == NULL);
	if (create_error != 0) { CHECK(create_error > 0); return create_error; }
	for (unsigned n = 0; n < SLOTS; n++) if (all_vms[n] == NULL) {
		*out = calloc(1, sizeof(**out)); CHECK(*out != NULL); all_vms[n] = *out;
		atomic_fetch_add(&vms, 1); return 0;
	}
	return ENOMEM;
}
static int pvr_vm_map(struct pvr_vm *vm, uint64_t va, uint64_t pa, uint64_t size, uint64_t flags)
{
	core_maps++;
	CHECK(pthread_mutex_lock(&rendezvous) == 0);
	if (pause_map) {
		map_entered = true; CHECK(pthread_cond_broadcast(&changed) == 0);
		while (!map_released) CHECK(pthread_cond_wait(&changed, &rendezvous) == 0);
	}
	CHECK(pthread_mutex_unlock(&rendezvous) == 0);
	if (map_error != 0) return map_error;
	for (unsigned n = 0; n < SLOTS; n++) {
		struct test_map *m = &vm->ranges[n];
		if (m->size != 0 && va < m->va + m->size && m->va < va + size) return EEXIST;
	}
	for (unsigned n = 0; n < SLOTS; n++) if (vm->ranges[n].size == 0) {
		vm->ranges[n] = (struct test_map){ va, pa, size, flags }; return 0;
	}
	return ENOMEM;
}
static int pvr_vm_unmap(struct pvr_vm *vm, uint64_t va, uint64_t size)
{
	core_unmaps++;
	if (unmap_error != 0) return unmap_error;
	for (unsigned n = 0; n < SLOTS; n++) if (vm->ranges[n].va == va && vm->ranges[n].size == size) {
		memset(&vm->ranges[n], 0, sizeof(vm->ranges[n])); return 0;
	}
	return ENOENT;
}
static void pvr_vm_destroy(struct pvr_vm *vm)
{
	core_destroys++; CHECK(vm != NULL && vm->invalidate == NULL && !vm->failed);
	for (unsigned n = 0; n < SLOTS; n++) if (all_vms[n] == vm) all_vms[n] = NULL;
	CHECK(atomic_fetch_sub(&vms, 1) != 0); free(vm);
}

static kmutex_t device_gate;
static void *orphans[SLOTS];
static unsigned orphan_count, fw_live, pre_syncs, post_syncs;
static int fw_allocate_error, flush_error, free_error;
static bool allocate_then_fail;
const struct pvr_backend *pvr_device_enter(struct drm_device *dev)
{ mutex_enter(&device_gate); destroying = false; return NULL; }
void pvr_device_leave(struct drm_device *dev)
{ mutex_exit(&device_gate); }
void pvr_device_retain(struct drm_device *dev, void *owner)
{ CHECK(mutex_owned(&device_gate) && orphan_count < SLOTS); orphans[orphan_count++] = owner; }
static int test_fw_alloc(void *cookie, size_t size, struct pvr_fw_object *obj)
{
	CHECK(mutex_owned(&device_gate) && obj->owner == NULL);
	if (fw_allocate_error != 0 && !allocate_then_fail) return fw_allocate_error;
	obj->cpu = obj->owner = calloc(1, size); CHECK(obj->cpu != NULL);
	obj->size = size; obj->address = 0xc0400000 + fw_live++ * 4096;
	return fw_allocate_error;
}
static int test_fw_free(void *cookie, struct pvr_fw_object *obj)
{
	CHECK(mutex_owned(&device_gate) && obj->owner != NULL);
	if (free_error != 0) return free_error;
	free(obj->owner); memset(obj, 0, sizeof(*obj)); CHECK(fw_live-- != 0); return 0;
}
static void test_fw_sync(void *cookie, struct pvr_fw_object *obj, int ops)
{ CHECK(mutex_owned(&device_gate) && obj->owner != NULL); CHECK(ops == (BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD)); }
static int test_flush(void *cookie)
{ CHECK(mutex_owned(&device_gate)); return flush_error; }
static void test_fail(void *cookie)
{ flush_error = EIO; }
static const struct pvr_backend backend = {
	.alloc = test_fw_alloc, .free = test_fw_free, .sync = test_fw_sync,
	.flush = test_flush, .fail = test_fail
};
static uint64_t pvr_vm_root_pa(const struct pvr_vm *vm)
{ return 0x12000000; }
static int pvr_vm_flush(struct pvr_vm *vm)
{
	int error = vm->failed ? EIO : vm->invalidate != NULL ? vm->invalidate(vm->cookie) : 0;
	if (error != 0) vm->failed = true;
	return error;
}
static int pvr_vm_publish(struct pvr_vm *vm, int (*invalidate)(void *), void *cookie)
{ CHECK(vm->invalidate == NULL); vm->invalidate = invalidate; vm->cookie = cookie; return pvr_vm_flush(vm); }
static int pvr_vm_quiesce(struct pvr_vm *vm)
{ int error = pvr_vm_flush(vm); if (error == 0) vm->invalidate = NULL; return error; }
static int pvr_bo_cpu(struct drm_gem_object *obj, uint64_t offset, size_t size, uint64_t flags, void **cpu)
{
	if ((obj->flags & flags) != flags) return EACCES;
	CHECK(offset <= obj->size && size <= obj->size - offset);
	*cpu = (void *)(uintptr_t)(obj->pa + offset); return 0;
}
static void pvr_bo_sync(struct drm_gem_object *obj, int operations)
{ CHECK(obj->refs != 0 && mutex_owned(&device_gate)); if (operations == 3) pre_syncs++; else { CHECK(operations == 12); post_syncs++; } }
static int pvr_bo_pin(struct drm_gem_object *obj)
{ CHECK(obj->refs > 0); if (obj->pinned || obj->holds != 0) return EBUSY; obj->pinned = true; return 0; }
static void pvr_bo_unpin(struct drm_gem_object *obj)
{ CHECK(obj->refs > 0 && obj->pinned); obj->pinned = false; }
static int pvr_bo_hold(struct drm_gem_object *obj)
{ CHECK(obj->refs > 0); if (obj->pinned) return EBUSY; if (obj->holds == UINT_MAX) return EOVERFLOW; obj->holds++; return 0; }
static void pvr_bo_unhold(struct drm_gem_object *obj)
{ CHECK(obj->refs > 0 && obj->holds > 0); obj->holds--; }
struct pvr_exec_file { unsigned magic; };

static unsigned exec_live;
struct pvr_exec_file *pvr_exec_open(void)
{ struct pvr_exec_file *p = calloc(1, sizeof(*p)); CHECK(p != NULL); p->magic = 0x1739; exec_live++; return p; }
void pvr_exec_close(struct drm_device *dev, struct pvr_exec_file *exec)
{ CHECK(mutex_owned(&device_gate) && exec_live > 0 && exec->magic == 0x1739); exec_live--; free(exec); }
/* Match the function-like macro shape from kernel sys/malloc.h. */
#define free(a, b) unexpected_backend_free_macro(a, b)
#include "pvr-file-body.h"
#undef free


typedef int (*ioctl_fn)(struct drm_device *, void *, struct drm_file *);
struct drm_ioctl_desc { unsigned command; ioctl_fn function; unsigned flags; };
#define DRM_IOCTL_DEF_DRV(n, f, flags) [DRM_IOCTL_##n - DRM_COMMAND_BASE] = { DRM_IOCTL_##n, f, flags }
static int unsupported(struct drm_device *d, void *a, struct drm_file *f)
{ CHECK(false); return -EOPNOTSUPP; }
#define pvr_dev_query unsupported
#define pvr_create_bo unsupported
#define pvr_get_bo_mmap_offset unsupported
#define pvr_create_context unsupported
#define pvr_destroy_context unsupported
#define pvr_create_free_list unsupported
#define pvr_destroy_free_list unsupported
#define pvr_submit_jobs unsupported
struct test_driver {
	int (*open)(struct drm_device *, struct drm_file *);
	void (*postclose)(struct drm_device *, struct drm_file *);
};
#include "pvr-file-wire.h"

static struct drm_gem_object *new_bo(struct drm_file *f, unsigned h, uint64_t pa)
{
	struct drm_gem_object *o = calloc(1, sizeof(*o));
	CHECK(o != NULL && h != 0 && h < SLOTS && f->handles[h] == NULL);
	*o = (struct drm_gem_object){ 1, 4 * PAGE_SIZE, pa, UINT64_C(1) << 62, false, 0 };
	for (unsigned n = 0; n < SLOTS; n++) if (all_objects[n] == NULL) {
		all_objects[n] = o; f->handles[h] = o; atomic_fetch_add(&objects, 1); return o;
	}
	CHECK(false); return NULL;
}
static void close_bo(struct drm_file *f, unsigned h)
{ struct drm_gem_object *o = f->handles[h]; CHECK(o != NULL); f->handles[h] = NULL; drm_gem_object_put_unlocked(o); }
static void close_file(struct drm_device *d, struct drm_file *f)
{
	/* Real drm_file_free releases GEM handles before postclose. */
	for (unsigned n = 1; n < SLOTS; n++) if (f->handles[n] != NULL) close_bo(f, n);
	closing = true; test_driver.postclose(d, f); closing = false;
	CHECK(f->driver_priv == NULL);
}
static uint32_t create_vm(struct drm_device *d, struct drm_file *f)
{ struct drm_pvr_ioctl_create_vm_context_args a = { .handle = UINT32_MAX }; CHECK(pvr_create_vm_context(d, &a, f) == 0 && a.handle != 0 && a.handle != UINT32_MAX); return a.handle; }
static void destroy_vm(struct drm_device *d, struct drm_file *f, uint32_t h)
{ struct drm_pvr_ioctl_destroy_vm_context_args a = { .handle = h }; CHECK(pvr_destroy_vm_context(d, &a, f) == 0); }

struct race { struct drm_device *dev; struct drm_file *file; struct drm_pvr_ioctl_vm_map_args map; int result; };
static void *race_map(void *p)
{ struct race *r = p; r->result = pvr_vm_map_ioctl(r->dev, &r->map, r->file); return NULL; }
static void *race_destroy(void *p)
{ struct race *r = p; struct drm_pvr_ioctl_destroy_vm_context_args a = { .handle = r->map.vm_context_handle }; destroying = true; r->result = pvr_destroy_vm_context(r->dev, &a, r->file); return NULL; }

static void
firmware_lifetime(struct drm_device *dev)
{
	struct drm_file file = { 0 };
	struct drm_pvr_ioctl_vm_map_args map;
	struct pvr_file_vm *vm;
	struct drm_gem_object *bo;
	uint32_t handle, address = 0xa5;
	void *cpu;
	unsigned before;

	for (unsigned mode = 0; mode < 6; mode++) {
		CHECK(pvr_file_open(dev, &file) == 0);
		handle = create_vm(dev, &file);
		bo = new_bo(&file, 1, 0x62000000);
		map = (struct drm_pvr_ioctl_vm_map_args){ .vm_context_handle = handle,
		    .handle = 1, .device_addr = BASE, .offset = PAGE_SIZE, .size = PAGE_SIZE };
		CHECK(pvr_vm_map_ioctl(dev, &map, &file) == 0);
		(void)pvr_device_enter(dev);
		vm = pvr_file_vm_get(&file, handle); CHECK(vm != NULL && vm->refs == 2);
		CHECK(pvr_file_vm_buffer(vm, BASE + 64, 32, bo->flags, &cpu) == 0);
		CHECK(cpu == (void *)(uintptr_t)(bo->pa + PAGE_SIZE + 64));
		CHECK(pvr_file_vm_buffer(vm, BASE + PAGE_SIZE - 4, 8, 0, &cpu) == ENOENT);
		fw_allocate_error = mode == 1 || mode == 2 ? ENOMEM : 0;
		allocate_then_fail = mode == 2;
		flush_error = mode == 3 ? ETIMEDOUT : 0;
		before = orphan_count;
		int error = pvr_file_vm_publish(vm, &backend, &address);
		CHECK(error == (mode == 1 || mode == 2 ? ENOMEM : mode == 3 ? ETIMEDOUT : 0));
		fw_allocate_error = 0; allocate_then_fail = false;
		if (error == 0) {
			struct rogue_fwif_fwmemcontext *fw = vm->firmware.cpu;
			CHECK(fw->pc_dev_paddr == pvr_vm_root_pa(vm->tables));
			CHECK(fw->page_cat_base_reg_set == UINT32_MAX);
			CHECK(address == vm->firmware.address);
			CHECK(pvr_file_vm_prepare(vm) == 0);
			pvr_file_vm_complete(vm); CHECK(pre_syncs == post_syncs);
			if (mode == 4) free_error = EIO;
			if (mode == 5) pvr_file_vm_fail(vm);
		}
		pvr_device_leave(dev);
		/* Removing the handle must preserve the context's VM reference. */
		destroy_vm(dev, &file, handle); CHECK(vm->refs == 1 && bo->refs == 2);
		close_file(dev, &file); CHECK(bo->refs == 1);
		(void)pvr_device_enter(dev);
		pvr_file_vm_put(vm);
		if (mode >= 2) {
			CHECK(orphan_count == before + 1 && orphans[before] == vm);
			CHECK(atomic_load(&objects) >= 1 && vm->firmware.owner != NULL);
			/* Test teardown follows an explicit model reset, not a driver reset. */
			flush_error = free_error = 0;
			vm->failed = false; vm->tables->failed = false;
			vm->refs = 1; pvr_file_vm_put(vm);
			orphans[--orphan_count] = NULL;
		} else CHECK(orphan_count == before);
		pvr_device_leave(dev);
		CHECK(fw_live == 0);
	}
}

static void
pin_lifetime(struct drm_device *dev)
{
	struct drm_file file = { 0 };
	struct drm_pvr_ioctl_vm_map_args map;
	struct drm_pvr_ioctl_vm_unmap_args unmap;
	struct pvr_file_vm *vm, *alias_vm;
	struct pvr_file_pin pin = { 0 }, second = { 0 };
	struct drm_gem_object *bo;
	struct pvr_file_mapping *mapping;
	unsigned handle, alias_handle, before;

	CHECK(pvr_file_open(dev, &file) == 0);
	CHECK(pvr_file_execution(&file)->magic == 0x1739);
	handle = create_vm(dev, &file);
	alias_handle = create_vm(dev, &file);
	bo = new_bo(&file, 1, 0x72000000);
	map = (struct drm_pvr_ioctl_vm_map_args){ .vm_context_handle = handle,
	    .handle = 1, .device_addr = BASE, .offset = PAGE_SIZE, .size = PAGE_SIZE };
	unmap = (struct drm_pvr_ioctl_vm_unmap_args){ .vm_context_handle = handle,
	    .device_addr = BASE, .size = PAGE_SIZE };
	CHECK(pvr_vm_map_ioctl(dev, &map, &file) == 0);
	map.vm_context_handle = alias_handle;
	CHECK(pvr_vm_map_ioctl(dev, &map, &file) == 0);
	(void)pvr_device_enter(dev);
	vm = pvr_file_vm_get(&file, handle);
	alias_vm = pvr_file_vm_get(&file, alias_handle);
	CHECK(vm != NULL && alias_vm != NULL && vm->refs == 2 && bo->refs == 3);
	CHECK(pvr_file_vm_pin(vm, BASE, 32, 0, NULL) == EINVAL);
	CHECK(pvr_file_vm_pin(vm, BASE, 0, 0, &pin) == EINVAL);
	CHECK(pvr_file_vm_pin(vm, BASE - 1, 32, 0, &pin) == ENOENT);
	CHECK(pvr_file_vm_pin(vm, BASE + PAGE_SIZE - 8, 16, 0, &pin) == ENOENT);
	CHECK(pvr_file_vm_pin(vm, BASE, 32, bo->flags | 1, &pin) == EACCES);
	CHECK(pin.mapping == NULL && pin.cpu == NULL && pin.physical == 0);
	mapping_error = EIO;
	CHECK(pvr_file_vm_pin(vm, BASE, 32, 0, &pin) == EIO);
	mapping_error = 0;
	CHECK(pin.mapping == NULL && vm->mappings->pins == 0 && !bo->pinned);
	CHECK(pvr_file_vm_pin(vm, BASE + 64, 32, bo->flags, &pin) == 0);
	CHECK(pin.cpu == (void *)(uintptr_t)(bo->pa + PAGE_SIZE + 64));
	CHECK(pin.physical == bo->pa + PAGE_SIZE + 64);
	mapping = pin.mapping;
	CHECK(mapping->pins == 1 && bo->refs == 3 && bo->pinned);
	CHECK(pvr_file_vm_pin(vm, BASE, 32, 0, &pin) == EINVAL);
	CHECK(pvr_file_vm_pin(vm, BASE + 128, 16, bo->flags, &second) == EBUSY);
	CHECK(second.mapping == NULL && mapping->pins == 1);
	/* Different VM mappings still refer to one protected firmware array. */
	CHECK(pvr_file_vm_pin(alias_vm, BASE, 32, bo->flags, &second) == EBUSY);
	CHECK(second.mapping == NULL && alias_vm->mappings->pins == 0);
	pvr_file_pin_sync(&pin, BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);
	pvr_file_pin_sync(&pin, BUS_DMASYNC_POSTWRITE | BUS_DMASYNC_POSTREAD);
	CHECK(pre_syncs == post_syncs);
	pvr_device_leave(dev);
	before = core_unmaps;
	CHECK(pvr_vm_unmap_ioctl(dev, &unmap, &file) == -EBUSY);
	CHECK(core_unmaps == before && bo->refs == 3);
	(void)pvr_device_enter(dev);
	pvr_file_unpin(&pin);
	CHECK(mapping->pins == 0 && !bo->pinned && pin.mapping == NULL);
	CHECK(pin.cpu == NULL && pin.physical == 0);
	CHECK(pvr_file_vm_pin(alias_vm, BASE, 32, bo->flags, &second) == 0);
	CHECK(bo->pinned && alias_vm->mappings->pins == 1);
	CHECK(pvr_file_vm_pin(vm, BASE, 32, bo->flags, &pin) == EBUSY);
	pvr_file_unpin(&second);
	CHECK(second.mapping == NULL && !bo->pinned);
	CHECK(pvr_file_vm_pin(vm, BASE, 32, bo->flags, &pin) == 0);
	pvr_file_vm_put(alias_vm);
	pvr_device_leave(dev);
	destroy_vm(dev, &file, alias_handle);
	CHECK(bo->refs == 2 && bo->pinned);
	/* A live owner reference retains pinned backing after both handles close. */
	destroy_vm(dev, &file, handle);
	CHECK(vm->refs == 1 && bo->refs == 2);
	close_file(dev, &file);
	CHECK(bo->refs == 1 && pin.mapping == mapping && bo->pinned);
	(void)pvr_device_enter(dev);
	pvr_file_pin_sync(&pin, BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);
	pvr_file_pin_sync(&pin, BUS_DMASYNC_POSTWRITE | BUS_DMASYNC_POSTREAD);
	pvr_file_unpin(&pin);
	CHECK(mapping->pins == 0 && !bo->pinned && pin.mapping == NULL);
	CHECK(pin.cpu == NULL && pin.physical == 0);
	pvr_file_vm_put(vm);
	pvr_device_leave(dev);
	CHECK(pre_syncs == post_syncs);
}

static void
hold_lifetime(struct drm_device *dev)
{
	struct drm_file file = { 0 };
	struct drm_pvr_ioctl_vm_map_args map;
	struct drm_pvr_ioctl_vm_unmap_args unmap;
	struct pvr_file_vm *vm, *alias;
	struct pvr_file_hold first = { 0 }, second = { 0 }, third = { 0 };
	struct pvr_file_pin pin = { 0 };
	struct drm_gem_object *bo;
	unsigned handle, alias_handle, before;

	CHECK(pvr_file_open(dev, &file) == 0);
	handle = create_vm(dev, &file);
	alias_handle = create_vm(dev, &file);
	bo = new_bo(&file, 1, 0x73000000);
	map = (struct drm_pvr_ioctl_vm_map_args){ .vm_context_handle = handle,
	    .handle = 1, .device_addr = BASE, .offset = PAGE_SIZE, .size = PAGE_SIZE };
	unmap = (struct drm_pvr_ioctl_vm_unmap_args){ .vm_context_handle = handle,
	    .device_addr = BASE, .size = PAGE_SIZE };
	CHECK(pvr_vm_map_ioctl(dev, &map, &file) == 0);
	map.vm_context_handle = alias_handle;
	CHECK(pvr_vm_map_ioctl(dev, &map, &file) == 0);
	(void)pvr_device_enter(dev);
	vm = pvr_file_vm_get(&file, handle);
	alias = pvr_file_vm_get(&file, alias_handle);
	CHECK(pvr_file_vm_hold(vm, BASE, 32, 0, NULL) == EINVAL);
	CHECK(pvr_file_vm_hold(vm, BASE, 0, 0, &first) == EINVAL);
	CHECK(pvr_file_vm_hold(vm, BASE - 1, 32, 0, &first) == ENOENT);
	CHECK(pvr_file_vm_hold(vm, BASE + PAGE_SIZE - 8, 16, 0, &first) == ENOENT);
	CHECK(pvr_file_vm_hold(vm, UINT64_MAX, 16, 0, &first) == ENOENT);
	CHECK(pvr_file_vm_hold(vm, BASE, 32, bo->flags | 1, &first) == EACCES);
	vm->failed = true;
	CHECK(pvr_file_vm_hold(vm, BASE, 32, 0, &first) == EINVAL);
	vm->failed = false;
	vm->mappings->pins = UINT_MAX;
	CHECK(pvr_file_vm_hold(vm, BASE, 32, 0, &first) == EOVERFLOW);
	vm->mappings->pins = 0;
	bo->holds = UINT_MAX;
	CHECK(pvr_file_vm_hold(vm, BASE, 32, 0, &first) == EOVERFLOW);
	CHECK(first.mapping == NULL && vm->mappings->pins == 0);
	bo->holds = 0;
	CHECK(pvr_file_vm_pin(alias, BASE, 32, bo->flags, &pin) == 0);
	CHECK(pvr_file_vm_hold(vm, BASE, 32, 0, &first) == EBUSY);
	CHECK(first.mapping == NULL && vm->mappings->pins == 0 && bo->holds == 0);
	pvr_file_unpin(&pin);
	CHECK(pvr_file_vm_hold(vm, BASE + 64, 32, bo->flags, &first) == 0);
	/* Mesa packs two target spans in one BO and mapping. */
	CHECK(pvr_file_vm_hold(vm, BASE + 128, 32, bo->flags, &second) == 0);
	CHECK(pvr_file_vm_hold(alias, BASE + 64, 32, bo->flags, &third) == 0);
	CHECK(vm->mappings->pins == 2 && alias->mappings->pins == 1);
	CHECK(bo->holds == 3 && bo->refs == 3);
	CHECK(pvr_file_vm_hold(vm, BASE, 32, 0, &first) == EINVAL);
	CHECK(pvr_file_vm_pin(alias, BASE, 32, bo->flags, &pin) == EBUSY);
	pvr_device_leave(dev);
	before = core_unmaps;
	CHECK(pvr_vm_unmap_ioctl(dev, &unmap, &file) == -EBUSY);
	unmap.vm_context_handle = alias_handle;
	CHECK(pvr_vm_unmap_ioctl(dev, &unmap, &file) == -EBUSY);
	CHECK(core_unmaps == before);
	destroy_vm(dev, &file, handle);
	destroy_vm(dev, &file, alias_handle);
	close_file(dev, &file);
	CHECK(vm->refs == 1 && alias->refs == 1 && bo->refs == 2 && bo->holds == 3);
	(void)pvr_device_enter(dev);
	pvr_file_unhold(&first);
	CHECK(first.mapping == NULL && vm->mappings->pins == 1 && bo->holds == 2);
	pvr_file_unhold(&second);
	CHECK(second.mapping == NULL && vm->mappings->pins == 0 && bo->holds == 1);
	pvr_file_vm_put(vm);
	CHECK(bo->refs == 1 && bo->holds == 1);
	pvr_file_unhold(&third);
	CHECK(third.mapping == NULL && alias->mappings->pins == 0 && bo->holds == 0);
	pvr_file_vm_put(alias);
	pvr_device_leave(dev);
}

int
main(void)
{
	struct drm_device dev = { .dmat = &dev };
	struct drm_file a = { 0 }, b = { 0 };
	struct drm_pvr_ioctl_create_vm_context_args create = { .handle = 0xa5, ._padding_4 = 1 };
	struct drm_pvr_ioctl_destroy_vm_context_args destroy = { 0 };
	struct drm_pvr_ioctl_vm_map_args map, invalid;
	struct drm_pvr_ioctl_vm_unmap_args unmap;
	struct drm_gem_object *bo;
	uint32_t first, second, foreign;
	unsigned before;
	struct race rmap, rdestroy;
	pthread_t mapper, destroyer;

	mutex_init(&device_gate, MUTEX_DEFAULT, IPL_NONE);
	_Static_assert(sizeof(create) == 8 && sizeof(destroy) == 8, "VM handle ABI");
	_Static_assert(sizeof(map) == 40 && offsetof(struct drm_pvr_ioctl_vm_map_args, offset) == 24, "MAP ABI");
	_Static_assert(sizeof(unmap) == 24, "UNMAP ABI");
	CHECK(test_driver.open == pvr_file_open && test_driver.postclose == pvr_file_close);
	const ioctl_fn handlers[] = { pvr_create_vm_context, pvr_destroy_vm_context, pvr_vm_map_ioctl, pvr_vm_unmap_ioctl };
	CHECK(sizeof(pvr_ioctls) / sizeof(pvr_ioctls[0]) == 14);
	for (unsigned n = 0; n < 4; n++) {
		CHECK(pvr_ioctls[n + 3].command == DRM_COMMAND_BASE + 3 + n);
		CHECK(pvr_ioctls[n + 3].function == handlers[n]);
		CHECK(pvr_ioctls[n + 3].flags == (DRM_RENDER_ALLOW | DRM_UNLOCKED));
	}
	CHECK(test_driver.open(&dev, &a) == 0 && a.driver_priv != NULL);
	CHECK(test_driver.open(&dev, &b) == 0 && b.driver_priv != a.driver_priv);
	CHECK(pvr_create_vm_context(&dev, &create, &a) == -EINVAL && create.handle == 0xa5);
	create._padding_4 = 0;
	if (PAGE_SHIFT != 12) {
		CHECK(pvr_create_vm_context(&dev, &create, &a) == -EOPNOTSUPP && create.handle == 0xa5);
		CHECK(atomic_load(&vms) == 0);
		goto finish;
	}
	for (unsigned n = 0; n < 2; n++) {
		create_error = n == 0 ? ENOMEM : EIO;
		CHECK(pvr_create_vm_context(&dev, &create, &a) == -create_error && create.handle == 0xa5);
		CHECK(atomic_load(&vms) == 0 && atomic_load(&allocations) == 2);
	}
	create_error = 0;
	idr_error = -ENOSPC;
	CHECK(pvr_create_vm_context(&dev, &create, &a) == -ENOSPC && create.handle == 0xa5);
	CHECK(atomic_load(&vms) == 0 && !preload && atomic_load(&allocations) == 2);
	idr_error = 0;
	first = create_vm(&dev, &a); second = create_vm(&dev, &a);
	foreign = create_vm(&dev, &b); CHECK(first == foreign && second != first);
	bo = new_bo(&a, 1, 0x12000000);
	map = (struct drm_pvr_ioctl_vm_map_args){ .vm_context_handle = first, .device_addr = BASE, .handle = 1, .offset = PAGE_SIZE, .size = PAGE_SIZE };
	invalid = map; invalid.flags = 1; CHECK(pvr_vm_map_ioctl(&dev, &invalid, &a) == -EINVAL);
	invalid = map; invalid._padding_14 = 1; CHECK(pvr_vm_map_ioctl(&dev, &invalid, &a) == -EINVAL);
	invalid = map; invalid.device_addr++; CHECK(pvr_vm_map_ioctl(&dev, &invalid, &a) == -EINVAL);
	invalid = map; invalid.offset++; CHECK(pvr_vm_map_ioctl(&dev, &invalid, &a) == -EINVAL);
	invalid = map; invalid.size--; CHECK(pvr_vm_map_ioctl(&dev, &invalid, &a) == -EINVAL);
	invalid = map; invalid.size = 0; CHECK(pvr_vm_map_ioctl(&dev, &invalid, &a) == -EINVAL);
	invalid = map; invalid.device_addr = BASE + HEAP_SIZE; CHECK(pvr_vm_map_ioctl(&dev, &invalid, &a) == -EINVAL);
	invalid = map; invalid.device_addr = UINT64_MAX - PAGE_SIZE + 1; invalid.size = 2 * PAGE_SIZE; CHECK(pvr_vm_map_ioctl(&dev, &invalid, &a) == -EINVAL);
	invalid = map; invalid.offset = UINT64_MAX - PAGE_SIZE + 1; CHECK(pvr_vm_map_ioctl(&dev, &invalid, &a) == -EINVAL);
	invalid = map; invalid.handle = 2; CHECK(pvr_vm_map_ioctl(&dev, &invalid, &a) == -ENOENT);
	CHECK(pvr_vm_map_ioctl(&dev, &map, &b) == -ENOENT);
	for (unsigned n = 0; n < 3; n++) {
		invalid = map; invalid.vm_context_handle = n == 0 ? 0 : n == 1 ? second + 1 : UINT32_MAX;
		CHECK(pvr_vm_map_ioctl(&dev, &invalid, &a) == -EINVAL);
		destroy.handle = invalid.vm_context_handle; CHECK(pvr_destroy_vm_context(&dev, &destroy, &a) == -EINVAL);
	}
	invalid = map; invalid.vm_context_handle = second; new_bo(&b, 1, 0x22000000);
	CHECK(pvr_vm_map_ioctl(&dev, &invalid, &b) == -EINVAL);
	CHECK(bo->refs == 1 && core_maps == 0);
	mapping_error = EOPNOTSUPP; CHECK(pvr_vm_map_ioctl(&dev, &map, &a) == -EOPNOTSUPP); mapping_error = 0;
	map_error = ENOMEM; CHECK(pvr_vm_map_ioctl(&dev, &map, &a) == -ENOMEM); map_error = 0;
	CHECK(bo->refs == 1);
	CHECK(pvr_vm_map_ioctl(&dev, &map, &a) == 0 && bo->refs == 2);
	struct pvr_file *pf = a.driver_priv;
	struct pvr_file_vm *fvm = pf->vms.slots[first];
	CHECK(fvm->tables->ranges[0].pa == bo->pa + PAGE_SIZE && fvm->tables->ranges[0].flags == bo->flags);
	CHECK(pvr_vm_map_ioctl(&dev, &map, &a) == -EEXIST && bo->refs == 2);
	invalid = map; invalid.vm_context_handle = second;
	CHECK(pvr_vm_map_ioctl(&dev, &invalid, &a) == 0 && bo->refs == 3);
	unmap = (struct drm_pvr_ioctl_vm_unmap_args){ .vm_context_handle = first, .device_addr = BASE, .size = 2 * PAGE_SIZE };
	before = core_unmaps; CHECK(pvr_vm_unmap_ioctl(&dev, &unmap, &a) == -ENOENT && core_unmaps == before);
	unmap.size = PAGE_SIZE; unmap._padding_4 = 1; CHECK(pvr_vm_unmap_ioctl(&dev, &unmap, &a) == -EINVAL); unmap._padding_4 = 0;
	unmap.device_addr++; CHECK(pvr_vm_unmap_ioctl(&dev, &unmap, &a) == -EINVAL); unmap.device_addr--;
	unmap.vm_context_handle = UINT32_MAX; CHECK(pvr_vm_unmap_ioctl(&dev, &unmap, &a) == -EINVAL); unmap.vm_context_handle = first;
	CHECK(pvr_vm_unmap_ioctl(&dev, &unmap, &b) == -ENOENT);
	unmap_error = EIO; CHECK(pvr_vm_unmap_ioctl(&dev, &unmap, &a) == -EIO && bo->refs == 3); unmap_error = 0;
	CHECK(pvr_vm_unmap_ioctl(&dev, &unmap, &a) == 0 && bo->refs == 2);
	CHECK(pvr_vm_unmap_ioctl(&dev, &unmap, &a) == -ENOENT);
	close_bo(&a, 1); CHECK(bo->refs == 1);
	destroy_vm(&dev, &a, second); CHECK(atomic_load(&objects) == 1);
	destroy._padding_4 = 1; destroy.handle = first; CHECK(pvr_destroy_vm_context(&dev, &destroy, &a) == -EINVAL);
	destroy._padding_4 = 0;
	/* MAP holds the file mutex while a competing DESTROY waits. */
	bo = new_bo(&a, 1, 0x32000000);
	pause_map = true; rmap = (struct race){ &dev, &a, map, 123 }; rdestroy = rmap;
	CHECK(pthread_create(&mapper, NULL, race_map, &rmap) == 0);
	CHECK(pthread_mutex_lock(&rendezvous) == 0);
	while (!map_entered) CHECK(pthread_cond_wait(&changed, &rendezvous) == 0);
	CHECK(pthread_mutex_unlock(&rendezvous) == 0);
	CHECK(pthread_create(&destroyer, NULL, race_destroy, &rdestroy) == 0);
	CHECK(pthread_mutex_lock(&rendezvous) == 0);
	while (!destroy_waiting) CHECK(pthread_cond_wait(&changed, &rendezvous) == 0);
	CHECK(bo->refs == 2 && pf->vms.slots[first] != NULL);
	map_released = true; CHECK(pthread_cond_broadcast(&changed) == 0);
	CHECK(pthread_mutex_unlock(&rendezvous) == 0);
	CHECK(pthread_join(mapper, NULL) == 0 && pthread_join(destroyer, NULL) == 0);
	CHECK(rmap.result == 0 && rdestroy.result == 0 && bo->refs == 1);
	pause_map = false;
	/* postclose must drop every mapping after generic DRM drops handles. */
	map.vm_context_handle = create_vm(&dev, &a);
	CHECK(pvr_vm_map_ioctl(&dev, &map, &a) == 0);
	map.device_addr += PAGE_SIZE; CHECK(pvr_vm_map_ioctl(&dev, &map, &a) == 0);
	CHECK(bo->refs == 3);
finish:
	close_file(&dev, &a); close_file(&dev, &b);
	CHECK(atomic_load(&allocations) == 0 && atomic_load(&objects) == 0 && atomic_load(&vms) == 0);
	if (PAGE_SHIFT == 12) { firmware_lifetime(&dev); pin_lifetime(&dev); hold_lifetime(&dev); }
	CHECK(atomic_load(&allocations) == 0 && atomic_load(&objects) == 0 && atomic_load(&vms) == 0);
	CHECK(exec_live == 0); mutex_destroy(&device_gate);
	CHECK(pthread_cond_destroy(&changed) == 0 && pthread_mutex_destroy(&rendezvous) == 0);
	printf("PASS: %u production PowerVR file checks, PAGE_SHIFT=%d\n", atomic_load(&checks), PAGE_SHIFT);
	return 0;
}
