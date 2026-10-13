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
typedef uint16_t __u16;
typedef uint32_t __u32;
typedef uint64_t __u64;
typedef void *bus_dma_tag_t;
#include "pvr-uapi-body.h"

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
struct drm_gem_object { unsigned int refs; uint64_t size, pa, flags; };
struct drm_file { void *driver_priv; struct drm_gem_object *handles[SLOTS]; };
struct test_map { uint64_t va, pa, size, flags; };
struct pvr_vm { struct test_map ranges[SLOTS]; };
static struct pvr_vm *all_vms[SLOTS];
static struct drm_gem_object *all_objects[SLOTS];
static void drm_gem_object_get(struct drm_gem_object *o)
{ CHECK(o != NULL && o->refs != 0); o->refs++; }
static void drm_gem_object_put_unlocked(struct drm_gem_object *o)
{
	CHECK(o != NULL && o->refs != 0);
	if (--o->refs != 0) return;
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
	bo_queries++; CHECK(o->refs >= 2);
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
	core_destroys++; CHECK(vm != NULL);
	for (unsigned n = 0; n < SLOTS; n++) if (all_vms[n] == vm) all_vms[n] = NULL;
	CHECK(atomic_fetch_sub(&vms, 1) != 0); free(vm);
}

#include "pvr-file-body.h"

typedef int (*ioctl_fn)(struct drm_device *, void *, struct drm_file *);
struct drm_ioctl_desc { unsigned command; ioctl_fn function; unsigned flags; };
#define DRM_IOCTL_DEF_DRV(n, f, flags) { DRM_IOCTL_##n, f, flags }
static int unsupported(struct drm_device *d, void *a, struct drm_file *f)
{ CHECK(false); return -EOPNOTSUPP; }
#define pvr_dev_query unsupported
#define pvr_create_bo unsupported
#define pvr_get_bo_mmap_offset unsupported
struct test_driver {
	int (*open)(struct drm_device *, struct drm_file *);
	void (*postclose)(struct drm_device *, struct drm_file *);
};
#include "pvr-file-wire.h"

static struct drm_gem_object *new_bo(struct drm_file *f, unsigned h, uint64_t pa)
{
	struct drm_gem_object *o = calloc(1, sizeof(*o));
	CHECK(o != NULL && h != 0 && h < SLOTS && f->handles[h] == NULL);
	*o = (struct drm_gem_object){ 1, 4 * PAGE_SIZE, pa, UINT64_C(1) << 62 };
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

	_Static_assert(sizeof(create) == 8 && sizeof(destroy) == 8, "VM handle ABI");
	_Static_assert(sizeof(map) == 40 && offsetof(struct drm_pvr_ioctl_vm_map_args, offset) == 24, "MAP ABI");
	_Static_assert(sizeof(unmap) == 24, "UNMAP ABI");
	CHECK(test_driver.open == pvr_file_open && test_driver.postclose == pvr_file_close);
	const ioctl_fn handlers[] = { pvr_create_vm_context, pvr_destroy_vm_context, pvr_vm_map_ioctl, pvr_vm_unmap_ioctl };
	CHECK(sizeof(pvr_ioctls) / sizeof(pvr_ioctls[0]) == 7);
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
	CHECK(pthread_cond_destroy(&changed) == 0 && pthread_mutex_destroy(&rendezvous) == 0);
	printf("PASS: %u production PowerVR file checks, PAGE_SHIFT=%d\n", atomic_load(&checks), PAGE_SHIFT);
	return 0;
}
