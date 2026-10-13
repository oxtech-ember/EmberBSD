/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD production PowerVR BO allocation and mapping contract. */
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#ifndef PAGE_SHIFT
#define PAGE_SHIFT 12
#endif
#define PAGE_SIZE ((size_t)1 << PAGE_SHIFT)
#define KM_SLEEP 0
#define BUS_DMA_NOWAIT 1
#define BUS_DMA_PREFETCHABLE 2
#define BUS_DMASYNC_PREWRITE 4
#define _BUS_DMAMAP_IS_BOUNCING 8
#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4
#define DRIVER_GEM 1
#define roundup2(x, a) (((x) + (a) - 1) & ~((a) - 1))
#define container_of(p, t, m) ((t *)((char *)(p) - offsetof(t, m)))
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
typedef uint16_t __u16;
typedef uint32_t __u32;
typedef uint64_t __u64;
/* UVM exports this kernel type in uvm_param.h, not user sys/types.h. */
typedef off_t voff_t;
typedef void *bus_dma_tag_t;
typedef struct { void *memory; size_t ds_len; uint64_t ds_addr; } bus_dma_segment_t;
typedef struct {
	void *memory;
	bool loaded;
	unsigned int _dm_flags, dm_nsegs;
	size_t dm_mapsize;
	bus_dma_segment_t dm_segs[1];
} *bus_dmamap_t;
struct uvm_object { int unused; };
struct kref { unsigned int refs; };
struct drm_file;
struct drm_vma_offset_node {
	unsigned long start, pages;
	struct drm_file *allowed;
};
struct drm_vma_offset_manager {
	bool locked;
	unsigned long next;
	struct drm_vma_offset_node *nodes[16];
};
struct drm_device {
	bus_dma_tag_t dmat;
	unsigned int refs, features;
	struct drm_vma_offset_manager *vma_offset_manager;
};
struct drm_gem_object;
struct drm_gem_object_funcs { void (*free)(struct drm_gem_object *); };
struct drm_gem_object {
	struct drm_device *dev;
	size_t size;
	struct kref refcount;
	struct uvm_object gemo_uvmobj;
	struct drm_vma_offset_node vma_node;
	const struct drm_gem_object_funcs *funcs;
};
struct drm_gem_cma_object {
	struct drm_gem_object base;
	bus_dmamap_t dmamap;
	bus_dma_segment_t dmasegs[1];
	size_t dmasize;
	bus_dma_tag_t dmat;
	void *vaddr;
};
struct drm_file { struct drm_gem_object *handles[16]; };
struct drm_gem_close { uint32_t handle, pad; };
struct file { struct drm_file *f_data; };
#define to_drm_gem_cma_obj(p) container_of(p, struct drm_gem_cma_object, base)
#include "pvr-uapi-body.h"
#include "pvr-mmu-body.h"

static unsigned int checks, fail, attempts, allocated, mapped, maps, loaded;
static unsigned int objects, structures, syncs;
static struct drm_vma_offset_manager manager;
#define CHECK(e) do { checks++; if (!(e)) { \
	fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #e); abort(); \
} } while (0)

static void *kmem_zalloc(size_t n, int flags)
{ void *p = calloc(1, n); CHECK(p != NULL); structures++; return p; }
static void kmem_free(void *p, size_t n)
{ CHECK(structures != 0); structures--; free(p); }
static int
bus_dmamem_alloc(bus_dma_tag_t tag, size_t size, size_t align, size_t boundary,
    bus_dma_segment_t *segs, int max, int *nsegs, int flags)
{

	CHECK(flags == BUS_DMA_NOWAIT && align == PAGE_SIZE && max == 1);
	CHECK(size != 0 && (size & (PAGE_SIZE - 1)) == 0);
	attempts++;
	if (fail == 1)
		return ENOMEM;
	segs->memory = malloc(size);
	CHECK(segs->memory != NULL);
	memset(segs->memory, 0xa5, size);
	segs->ds_len = size;
	*nsegs = 1;
	allocated++;
	return 0;
}
static void bus_dmamem_free(bus_dma_tag_t t, bus_dma_segment_t *s, int n)
{ CHECK(n == 1 && allocated != 0); allocated--; free(s->memory); }
static int bus_dmamem_map(bus_dma_tag_t t, bus_dma_segment_t *s, int n,
    size_t size, void **p, int flags)
{
	CHECK(flags == (BUS_DMA_NOWAIT | BUS_DMA_PREFETCHABLE));
	CHECK(n == 1 && size == s->ds_len);
	if (fail == 2) return ENOMEM;
	*p = s->memory; mapped++; return 0;
}
static void bus_dmamem_unmap(bus_dma_tag_t t, void *p, size_t n)
{ CHECK(mapped != 0); mapped--; }
static int bus_dmamap_create(bus_dma_tag_t t, size_t n, int ns, size_t max,
    size_t boundary, int flags, bus_dmamap_t *map)
{
	CHECK(flags == BUS_DMA_NOWAIT && ns == 1 && n == max);
	if (fail == 3) return ENOMEM;
	*map = calloc(1, sizeof(**map)); CHECK(*map != NULL); maps++; return 0;
}
static void bus_dmamap_destroy(bus_dma_tag_t t, bus_dmamap_t map)
{ CHECK(maps != 0 && !map->loaded); maps--; free(map); }
static int bus_dmamap_load(bus_dma_tag_t t, bus_dmamap_t map, void *p,
    size_t n, void *proc, int flags)
{
	CHECK(flags == BUS_DMA_NOWAIT);
	if (fail == 4) return EIO;
	map->loaded = true; map->memory = p;
	map->dm_nsegs = 1; map->dm_mapsize = n;
	map->dm_segs[0].ds_len = n;
	map->dm_segs[0].ds_addr = UINT64_C(0x10000000);
	loaded++; return 0;
}
static void bus_dmamap_unload(bus_dma_tag_t t, bus_dmamap_t map)
{ CHECK(loaded != 0 && map->loaded); loaded--; map->loaded = false; }
static void bus_dmamap_sync(bus_dma_tag_t t, bus_dmamap_t map,
    size_t off, size_t len, int flags)
{
	CHECK(map->loaded && off == 0 && flags == BUS_DMASYNC_PREWRITE);
	for (size_t n = 0; n < len; n++)
		CHECK(((uint8_t *)map->memory)[n] == 0);
	syncs++;
}
static void drm_dev_get(struct drm_device *dev)
{ CHECK(dev->refs != 0); dev->refs++; }
static void drm_dev_put(struct drm_device *dev)
{ CHECK(dev->refs != 0); dev->refs--; }
static bool drm_core_check_feature(struct drm_device *dev, unsigned int feature)
{ return (dev->features & feature) != 0; }
static void
drm_gem_private_object_init(struct drm_device *dev, struct drm_gem_object *obj,
    size_t size)
{

	CHECK(size != 0 && size % PAGE_SIZE == 0);
	obj->dev = dev; obj->size = size; obj->refcount.refs = 1; objects++;
}
static void
drm_gem_object_release(struct drm_gem_object *obj)
{

	CHECK(obj->refcount.refs == 0 && objects != 0 && !manager.locked);
	for (unsigned int n = 0; n < 16; n++)
		if (manager.nodes[n] == &obj->vma_node)
			manager.nodes[n] = NULL;
	objects--;
}
static void drm_gem_object_get(struct drm_gem_object *obj)
{ CHECK(obj->refcount.refs != 0); obj->refcount.refs++; }
static void drm_gem_object_put_unlocked(struct drm_gem_object *obj)
{ CHECK(obj->refcount.refs != 0); if (--obj->refcount.refs == 0) obj->funcs->free(obj); }
static int
drm_gem_handle_create(struct drm_file *file, struct drm_gem_object *obj,
    uint32_t *handle)
{

	if (fail == 5) return -EMFILE;
	for (unsigned int n = 1; n < 16; n++) {
		if (file->handles[n] != NULL) continue;
		drm_gem_object_get(obj); file->handles[n] = obj;
		obj->vma_node.allowed = file; *handle = n; return 0;
	}
	return -EMFILE;
}
static struct drm_gem_object *drm_gem_object_lookup(struct drm_file *file,
    uint32_t handle)
{
	if (handle >= 16 || file->handles[handle] == NULL) return NULL;
	drm_gem_object_get(file->handles[handle]); return file->handles[handle];
}
static int drm_gem_handle_delete(struct drm_file *file, uint32_t handle)
{
	struct drm_gem_object *obj;
	if (handle >= 16 || file->handles[handle] == NULL) return -EINVAL;
	obj = file->handles[handle]; file->handles[handle] = NULL;
	obj->vma_node.allowed = NULL; drm_gem_object_put_unlocked(obj); return 0;
}
static int drm_gem_create_mmap_offset(struct drm_gem_object *obj)
{
	if (fail == 6) return -ENOSPC;
	if (obj->vma_node.start != 0) return 0;
	for (unsigned int n = 0; n < 16; n++) {
		if (manager.nodes[n] != NULL) continue;
		manager.nodes[n] = &obj->vma_node;
		obj->vma_node.start = manager.next;
		obj->vma_node.pages = obj->size / PAGE_SIZE;
		manager.next += obj->vma_node.pages + 1;
		return 0;
	}
	return -ENOSPC;
}
static uint64_t drm_vma_node_offset_addr(struct drm_vma_offset_node *node)
{ return (uint64_t)node->start * PAGE_SIZE; }
static unsigned long drm_vma_node_start(struct drm_vma_offset_node *node)
{ return node->start; }
static void drm_vma_offset_lock_lookup(struct drm_vma_offset_manager *m)
{ CHECK(!m->locked); m->locked = true; }
static void drm_vma_offset_unlock_lookup(struct drm_vma_offset_manager *m)
{ CHECK(m->locked); m->locked = false; }
static struct drm_vma_offset_node *
drm_vma_offset_lookup_locked(struct drm_vma_offset_manager *m,
    unsigned long start, unsigned long pages)
{
	CHECK(m->locked);
	for (unsigned int n = 0; n < 16; n++) {
		struct drm_vma_offset_node *p = m->nodes[n];
		if (p != NULL && start >= p->start && pages <= p->pages &&
		    start - p->start <= p->pages - pages)
			return p;
	}
	return NULL;
}
static bool kref_get_unless_zero(struct kref *ref)
{ CHECK(manager.locked); if (ref->refs == 0) return false; ref->refs++; return true; }
static bool drm_vma_node_is_allowed(struct drm_vma_offset_node *n,
    struct drm_file *file)
{ CHECK(!manager.locked); return n->allowed == file; }

#include "pvr-bo-body.h"
#include "pvr-gem-core-body.h"

_Static_assert(sizeof(struct drm_pvr_ioctl_create_bo_args) == 24, "CREATE_BO ABI");
_Static_assert(offsetof(struct drm_pvr_ioctl_create_bo_args, flags) == 16,
    "CREATE_BO flags ABI");
_Static_assert(sizeof(struct drm_pvr_ioctl_get_bo_mmap_offset_args) == 16,
    "MMAP_OFFSET ABI");

static void
empty(struct drm_device *dev)
{

	CHECK(allocated == 0 && mapped == 0 && maps == 0 && loaded == 0);
	CHECK(objects == 0 && structures == 0 && dev->refs == 1);
	CHECK(!manager.locked);
}

static void
reject_map(struct drm_device *dev, struct file *file, off_t off, size_t size,
    int prot, int wanted)
{
	struct uvm_object dummy, *uobj = &dummy;
	voff_t uoffset = 0xabc;

	CHECK(pvr_mmap_object(dev, off, size, prot, &uobj, &uoffset, file) == wanted);
	CHECK(uobj == &dummy && uoffset == 0xabc && !manager.locked);
}

static void
mapping_contract(struct drm_gem_object *obj, unsigned int flags)
{
	bus_dmamap_t map = pvr_to_bo(obj)->cma.dmamap;
	uint64_t pa = 0xdead, bits = 0xbeef, wanted = 0;
	const uint64_t limit = UINT64_C(1) << A733_GPU_MMU_PA_BITS;

	if (flags & 1) wanted |= A733_GPU_MMU_PTE_SLC_BYPASS;
	if (flags & 2) wanted |= A733_GPU_MMU_PTE_PM_FW_PROTECT;
	CHECK(pvr_bo_mapping(obj, 0, PAGE_SIZE, &pa, &bits) == 0);
	CHECK(pa == map->dm_segs[0].ds_addr && bits == wanted);
	pa = 0xdead; bits = 0xbeef;
	CHECK(pvr_bo_mapping(NULL, 0, PAGE_SIZE, &pa, &bits) == EINVAL);
	CHECK(pvr_bo_mapping(obj, 0, 0, &pa, &bits) == EINVAL);
	CHECK(pvr_bo_mapping(obj, 1, PAGE_SIZE, &pa, &bits) == EINVAL);
	CHECK(pvr_bo_mapping(obj, 0, PAGE_SIZE - 1, &pa, &bits) == EINVAL);
	CHECK(pvr_bo_mapping(obj, PAGE_SIZE, PAGE_SIZE, &pa, &bits) == EINVAL);
	CHECK(pvr_bo_mapping(obj, UINT64_MAX, PAGE_SIZE, &pa, &bits) == EINVAL);
	map->_dm_flags = _BUS_DMAMAP_IS_BOUNCING;
	CHECK(pvr_bo_mapping(obj, 0, PAGE_SIZE, &pa, &bits) == EOPNOTSUPP);
	map->_dm_flags = 0; map->dm_nsegs = 2;
	CHECK(pvr_bo_mapping(obj, 0, PAGE_SIZE, &pa, &bits) == EOPNOTSUPP);
	map->dm_nsegs = 1; map->dm_mapsize--;
	CHECK(pvr_bo_mapping(obj, 0, PAGE_SIZE, &pa, &bits) == EOPNOTSUPP);
	map->dm_mapsize++; map->dm_segs[0].ds_len--;
	CHECK(pvr_bo_mapping(obj, 0, PAGE_SIZE, &pa, &bits) == EOPNOTSUPP);
	map->dm_segs[0].ds_len++; map->dm_segs[0].ds_addr++;
	CHECK(pvr_bo_mapping(obj, 0, PAGE_SIZE, &pa, &bits) == EINVAL);
	map->dm_segs[0].ds_addr = limit;
	CHECK(pvr_bo_mapping(obj, 0, PAGE_SIZE, &pa, &bits) == EINVAL);
	CHECK(pa == 0xdead && bits == 0xbeef);
	map->dm_segs[0].ds_addr = limit - PAGE_SIZE;
	CHECK(pvr_bo_mapping(obj, 0, PAGE_SIZE, &pa, &bits) == 0);
	CHECK(pa == limit - PAGE_SIZE && bits == wanted);
	map->dm_segs[0].ds_addr = UINT64_C(0x10000000);
}

int
main(void)
{
	struct drm_device dev = { .refs = 1, .features = DRIVER_GEM,
	    .vma_offset_manager = &manager };
	struct drm_file a = { 0 }, b = { 0 };
	struct file fa = { &a }, fb = { &b };
	struct drm_pvr_ioctl_create_bo_args create, saved;
	struct drm_pvr_ioctl_get_bo_mmap_offset_args mmap_args;
	struct drm_gem_close close_args;
	struct drm_gem_object *obj;
	struct uvm_object *uobj;
	voff_t uoffset;
	uint64_t offset;
	unsigned int old_attempts;
	const uint64_t badsize[] = { 0, 1, 4095, 4097, UINT64_MAX,
	    (uint64_t)INT_MAX * PAGE_SIZE + PAGE_SIZE };

	dev.dmat = &dev;
	manager.next = 0x100000;
	for (unsigned int n = 0; n < sizeof(badsize) / sizeof(badsize[0]); n++) {
		create = (struct drm_pvr_ioctl_create_bo_args){ badsize[n], 0xdead, 0, 4 };
		saved = create; old_attempts = attempts;
		CHECK(pvr_create_bo(&dev, &create, &a) == -EINVAL);
		CHECK(memcmp(&create, &saved, sizeof(create)) == 0 && attempts == old_attempts);
	}
	for (unsigned int n = 1; n <= 5; n++) {
		create = (struct drm_pvr_ioctl_create_bo_args){ PAGE_SIZE, 0xdead, 0, 4 };
		saved = create; fail = n;
		CHECK(pvr_create_bo(&dev, &create, &a) == (n == 4 ? -EIO : n == 5 ? -EMFILE : -ENOMEM));
		CHECK(memcmp(&create, &saved, sizeof(create)) == 0);
		empty(&dev);
	}
	/* Representable large requests reach the allocator; no small driver cap. */
	create = (struct drm_pvr_ioctl_create_bo_args){ 128U * 1024 * 1024, 0xdead, 0, 4 };
	fail = 1; old_attempts = attempts;
	CHECK(pvr_create_bo(&dev, &create, &a) == -ENOMEM && attempts == old_attempts + 1);
	empty(&dev); fail = 0;
	for (unsigned int flags = 0; flags < 16; flags++) {
		create = (struct drm_pvr_ioctl_create_bo_args){ 4096, 0xdead, 0, flags };
		saved = create;
		if (flags >= 8 || (flags & 6) == 6) {
			CHECK(pvr_create_bo(&dev, &create, &a) == -EINVAL);
			CHECK(memcmp(&create, &saved, sizeof(create)) == 0);
			continue;
		}
		CHECK(pvr_create_bo(&dev, &create, &a) == 0);
		obj = a.handles[create.handle];
		CHECK(obj->size == PAGE_SIZE && obj->refcount.refs == 1 && dev.refs == 2);
		CHECK(pvr_to_bo(obj)->flags == flags);
		mapping_contract(obj, flags);
		mmap_args = (struct drm_pvr_ioctl_get_bo_mmap_offset_args){ create.handle, 0, 0xdead };
		CHECK(pvr_get_bo_mmap_offset(&dev, &mmap_args, &a) == 0);
		CHECK(mmap_args.offset != 0 && mmap_args.offset != 0xdead);
		if ((flags & 4) == 0)
			reject_map(&dev, &fa, mmap_args.offset, PAGE_SIZE, PROT_READ, -EINVAL);
		else {
			CHECK(pvr_mmap_object(&dev, mmap_args.offset, PAGE_SIZE,
			    PROT_READ, &uobj, &uoffset, &fa) == 0);
			CHECK(uobj == &obj->gemo_uvmobj && uoffset == 0 && obj->refcount.refs == 2);
			drm_gem_pager_detach(uobj);
		}
		close_args = (struct drm_gem_close){ create.handle, 0 };
		CHECK(drm_gem_close_ioctl(&dev, &close_args, &a) == 0);
		empty(&dev);
	}
	create = (struct drm_pvr_ioctl_create_bo_args){ 2 * PAGE_SIZE, 0xdead, 1, 4 };
	CHECK(pvr_create_bo(&dev, &create, &a) == -EINVAL && create.handle == 0xdead);
	create._padding_c = 0;
	CHECK(pvr_create_bo(&dev, &create, &a) == 0);
	obj = a.handles[create.handle];
	{
		uint64_t pa, bits;
		CHECK(pvr_bo_mapping(obj, PAGE_SIZE, PAGE_SIZE, &pa, &bits) == 0);
		CHECK(pa == UINT64_C(0x10000000) + PAGE_SIZE && bits == 0);
	}
	mmap_args = (struct drm_pvr_ioctl_get_bo_mmap_offset_args){ create.handle, 1, 0xdead };
	CHECK(pvr_get_bo_mmap_offset(&dev, &mmap_args, &a) == -EINVAL && mmap_args.offset == 0xdead);
	mmap_args._padding_4 = 0; fail = 6;
	CHECK(pvr_get_bo_mmap_offset(&dev, &mmap_args, &a) == -ENOSPC && mmap_args.offset == 0xdead);
	CHECK(obj->refcount.refs == 1); fail = 0;
	CHECK(pvr_get_bo_mmap_offset(&dev, &mmap_args, &b) == -ENOENT && mmap_args.offset == 0xdead);
	CHECK(pvr_get_bo_mmap_offset(&dev, &mmap_args, &a) == 0);
	offset = mmap_args.offset;
	CHECK(pvr_get_bo_mmap_offset(&dev, &mmap_args, &a) == 0 && mmap_args.offset == offset);
	reject_map(&dev, &fa, -1, PAGE_SIZE, PROT_READ, -EINVAL);
	reject_map(&dev, &fa, offset + 1, PAGE_SIZE, PROT_READ, -EINVAL);
	reject_map(&dev, &fa, offset, 0, PROT_READ, -EINVAL);
	reject_map(&dev, &fa, offset, PAGE_SIZE - 1, PROT_READ, -EINVAL);
	reject_map(&dev, &fa, offset, PAGE_SIZE, PROT_EXEC, -EINVAL);
	reject_map(&dev, &fa, offset, 3 * PAGE_SIZE, PROT_READ, -EINVAL);
	reject_map(&dev, &fa, offset + PAGE_SIZE, PAGE_SIZE, PROT_READ, -EINVAL);
	reject_map(&dev, &fb, offset, PAGE_SIZE, PROT_READ, -EACCES);
	CHECK(obj->refcount.refs == 1);
	obj->refcount.refs = 0; /* Final put is waiting to remove the VMA node. */
	reject_map(&dev, &fa, offset, PAGE_SIZE, PROT_READ, -EINVAL);
	CHECK(obj->refcount.refs == 0); obj->refcount.refs = 1;
	CHECK(pvr_mmap_object(&dev, offset, PAGE_SIZE, PROT_READ | PROT_WRITE,
	    &uobj, &uoffset, &fa) == 0);
	CHECK(uobj == &obj->gemo_uvmobj && uoffset == 0 && obj->refcount.refs == 2);
	((uint32_t *)pvr_to_bo(obj)->cma.vaddr)[0] = 0x13579bdf;
	drm_gem_pager_reference(uobj);
	CHECK(obj->refcount.refs == 3);
	close_args = (struct drm_gem_close){ create.handle, 0 };
	CHECK(drm_gem_close_ioctl(&dev, &close_args, &a) == 0);
	CHECK(obj->refcount.refs == 2 && objects == 1 && dev.refs == 2);
	reject_map(&dev, &fa, offset, PAGE_SIZE, PROT_READ, -EACCES);
	CHECK(pvr_get_bo_mmap_offset(&dev, &mmap_args, &a) == -ENOENT);
	drm_dev_put(&dev); /* The DRM fd/owner closes before both CPU mappings. */
	CHECK(dev.refs == 1 && objects == 1);
	CHECK(((uint32_t *)pvr_to_bo(obj)->cma.vaddr)[0] == 0x13579bdf);
	drm_gem_pager_detach(uobj);
	CHECK(dev.refs == 1 && objects == 1);
	drm_gem_pager_detach(uobj);
	CHECK(dev.refs == 0 && objects == 0 && structures == 0);
	CHECK(allocated == 0 && mapped == 0 && maps == 0 && loaded == 0);
	printf("PASS: %u PowerVR BO checks (page=%zu, zero-syncs=%u)\n",
	    checks, PAGE_SIZE, syncs);
	return 0;
}
