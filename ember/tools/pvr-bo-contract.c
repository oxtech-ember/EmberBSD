/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD production PowerVR BO allocation and mapping contract. */
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>

#ifndef PAGE_SHIFT
#define PAGE_SHIFT 12
#endif
#define PAGE_SIZE ((size_t)1 << PAGE_SHIFT)
#define KM_SLEEP 0
#define KM_NOSLEEP 1
#define BUS_DMA_WAITOK 0
#define BUS_DMA_NOWAIT 1
#define BUS_DMA_PREFETCHABLE 2
#define BUS_DMASYNC_PREWRITE 4
#define BUS_DMASYNC_PREREAD 1
#define BUS_DMASYNC_POSTREAD 2
#define BUS_DMASYNC_POSTWRITE 8
#define _BUS_DMAMAP_COHERENT 2U
#define _BUS_DMAMAP_IS_BOUNCING 8
#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4
#define MAP_SHARED 1
#define MAP_PRIVATE 2
#define DRIVER_GEM 1
#define roundup2(x, a) (((x) + (a) - 1) & ~((a) - 1))
#ifndef howmany
#define howmany(x, y) (((x) + (y) - 1) / (y))
#endif
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define EWOULDBLOCK EAGAIN
#define container_of(p, t, m) ((t *)((char *)(p) - offsetof(t, m)))
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
typedef int32_t __s32;
typedef uint16_t __u16;
typedef uint32_t __u32;
typedef uint64_t __u64;
/* UVM exports this kernel type in uvm_param.h, not user sys/types.h. */
typedef off_t voff_t;
typedef void *bus_dma_tag_t;
typedef struct { void *memory; size_t ds_len; uint64_t ds_addr; } bus_dma_segment_t;
typedef struct {
	void *memory;
	bool loaded, initialized;
	unsigned int device_owned;
	unsigned int _dm_flags, dm_nsegs;
	size_t dm_mapsize;
	bus_dma_segment_t dm_segs[16];
} *bus_dmamap_t;
struct uvm_object { int unused; };
typedef uintptr_t vaddr_t;
typedef uintptr_t paddr_t;
typedef int vm_prot_t;
typedef unsigned int u_int;
void drm_gem_pager_reference(struct uvm_object *);
void drm_gem_pager_detach(struct uvm_object *);
struct vm_page { int unused; };
struct vm_map_entry {
	struct { struct uvm_object *uvm_obj; } object;
	struct { void *ar_amap; } aref;
	voff_t offset;
	vaddr_t start;
	vm_prot_t protection;
};
struct vm_map { void *pmap; };
struct uvm_faultinfo { struct vm_map_entry *entry; struct vm_map *orig_map; };
#define UVM_ET_ISCOPYONWRITE(entry) 0
#define PGO_ALLPAGES 1
#define PGO_DONTCARE ((struct vm_page *)(uintptr_t)-1)
#define PMAP_CANFAIL 8
struct uvm_pagerops {
	void (*pgo_reference)(struct uvm_object *);
	void (*pgo_detach)(struct uvm_object *);
	int (*pgo_fault)(struct uvm_faultinfo *, vaddr_t, struct vm_page **,
	    int, int, vm_prot_t, int);
};
static int last_map_flags, fault_map_flags, fault_enters, fault_unlocks;
static paddr_t fault_enter_pa;
static paddr_t bus_dmamem_mmap(bus_dma_tag_t t, bus_dma_segment_t *s, int n,
    off_t off, int prot, int flags)
{ fault_map_flags = flags; return s->ds_addr + off; }
static paddr_t pmap_phys_address(paddr_t p) { return p; }
static unsigned pmap_mmap_flags(paddr_t p) { return 0; }
static int pmap_enter(void *pmap, vaddr_t va, paddr_t pa, int prot, int flags)
{ fault_enters++; fault_enter_pa = pa; return 0; }
static void pmap_update(void *pmap) { }
static void uvmfault_unlockall(struct uvm_faultinfo *ufi, void *amap,
    struct uvm_object *uobj) { fault_unlocks++; }
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
struct drm_gem_object;
struct drm_driver { void (*gem_free_object)(struct drm_gem_object *); };
struct drm_device {
	bus_dma_tag_t dmat;
	unsigned int refs, features;
	struct drm_vma_offset_manager *vma_offset_manager;
	const struct drm_driver *driver;
};
static uint64_t selected_bvnc = UINT64_C(0x00240038006800b7);
uint64_t pvr_device_bvnc(struct drm_device *d)
{ (void)d; return selected_bvnc; }
struct drm_gem_object;
struct dma_buf;
struct dma_buf_ops { void (*release)(struct dma_buf *); };
struct dma_buf {
	struct drm_gem_object *obj; int flags;
	const struct dma_buf_ops *ops; void *priv; unsigned int refs;
};
struct sg_table { struct drm_gem_object *obj; };
struct drm_gem_object_funcs {
	void (*free)(struct drm_gem_object *);
	struct dma_buf *(*export)(struct drm_gem_object *, int);
	struct sg_table *(*get_sg_table)(struct drm_gem_object *);
};
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
static bool scatter_mode;
static unsigned int scatter_allocations;
static bool segment_metadata_fail;
static struct { int npages, reserve_kernel, reserve_pagedaemon, freemin;
	int wired, freetarg; } uvmexp = { 1048576, 8, 8, 0, 0, 0 };
static int available_pages = 1048576;
static int uvm_availmem(bool cached) { return available_pages; }
static unsigned int atomic_inc_uint_nv(volatile unsigned int *p) { return ++*p; }
static struct drm_vma_offset_manager manager;
#define CHECK(e) do { checks++; if (!(e)) { \
	fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #e); abort(); \
} } while (0)
#define KASSERT(e) CHECK(e)
/* Pagedaemon model: each one-tick pause reclaims this many file pages. */
static bool reclaimable = true;
static int reclaim_per_pause, pause_result;
static unsigned int kicks, pauses;
static uint64_t clock_ns, lowest_available = UINT64_MAX;
#ifndef timespeccmp
#define timespeccmp(a, b, op) ((a)->tv_sec == (b)->tv_sec ? \
    (a)->tv_nsec op (b)->tv_nsec : (a)->tv_sec op (b)->tv_sec)
#endif
static void
getnanouptime(struct timespec *ts)
{
	ts->tv_sec = (time_t)(clock_ns / 1000000000);
	ts->tv_nsec = (long)(clock_ns % 1000000000);
}
static bool uvm_reclaimable(void) { return reclaimable; }
static void uvm_kick_pdaemon(void) { kicks++; }
static int
kpause(const char *wmesg, bool intr, int ticks, void *lock)
{
	CHECK(strcmp(wmesg, "pvrbomem") == 0 && intr && ticks == 1 && lock == NULL);
	pauses++;
	clock_ns += 1000000;	/* one tick at HZ=1000 */
	available_pages += reclaim_per_pause;
	return pause_result;
}
/* Chunk model: one contiguous arena, handed out in sequential segments. */
static bool chunk_mode;
static unsigned int chunk_calls;
static unsigned char chunk_arena[130 * ((size_t)1 << PAGE_SHIFT)];
static size_t chunk_used;

static void *kmem_zalloc(size_t n, int flags)
{ if (flags == KM_NOSLEEP && segment_metadata_fail) return NULL;
  void *p = calloc(1, n); CHECK(p != NULL); structures++; return p; }
static void kmem_free(void *p, size_t n)
{ CHECK(structures != 0); structures--; free(p); }
static int
bus_dmamem_alloc(bus_dma_tag_t tag, size_t size, size_t align, size_t boundary,
    bus_dma_segment_t *segs, int max, int *nsegs, int flags)
{

	CHECK(flags == BUS_DMA_NOWAIT && align == PAGE_SIZE && max >= 1);
	CHECK(size != 0 && (size & (PAGE_SIZE - 1)) == 0);
	attempts++;
	if (fail == 1 || (scatter_mode && max == 1))
		return ENOMEM;
	if (chunk_mode) {
		/* Only chunks succeed, and each chunk takes its free pages. */
		if (max == 1 && size > 64 * PAGE_SIZE)
			return ENOMEM;
		CHECK(max >= (int)(size / PAGE_SIZE) && size <= 2048 * PAGE_SIZE);
		chunk_calls++;
		CHECK(available_pages >= (int)(size / PAGE_SIZE));
		CHECK(chunk_used + size <= sizeof(chunk_arena));
		segs->memory = chunk_arena + chunk_used;
		segs->ds_addr = UINT64_C(0x20000000) + chunk_used;
		segs->ds_len = size;
		chunk_used += size;
		available_pages -= (int)(size / PAGE_SIZE);
		if ((uint64_t)available_pages < lowest_available)
			lowest_available = (uint64_t)available_pages;
		*nsegs = 1;
		allocated++;
		return 0;
	}
	segs->memory = malloc(size);
	CHECK(segs->memory != NULL);
	memset(segs->memory, 0xa5, size);
	segs->ds_len = size;
	segs->ds_addr = UINT64_C(0x10000000);
	*nsegs = 1;
	if (scatter_mode) {
		CHECK(max >= (int)(size / PAGE_SIZE) && size == 3 * PAGE_SIZE);
		segs[0].ds_len = PAGE_SIZE;
		segs[1] = (bus_dma_segment_t){ .memory = (char *)segs->memory + PAGE_SIZE,
		    .ds_len = 2 * PAGE_SIZE, .ds_addr = UINT64_C(0x11000000) };
		*nsegs = 2;
		scatter_allocations++;
	}
	allocated++;
	return 0;
}
static void bus_dmamem_free(bus_dma_tag_t t, bus_dma_segment_t *s, int n)
{
	if (chunk_mode) {
		CHECK(n >= 1 && allocated >= (unsigned int)n);
		for (int i = 0; i < n; i++)
			available_pages += (int)(s[i].ds_len / PAGE_SIZE);
		allocated -= (unsigned int)n;
		chunk_used = 0;
		return;
	}
	CHECK((n == 1 || (scatter_mode && n == 2)) && allocated != 0); allocated--; free(s->memory);
}
static int bus_dmamem_map(bus_dma_tag_t t, bus_dma_segment_t *s, int n,
    size_t size, void **p, int flags)
{
	CHECK(flags == BUS_DMA_NOWAIT ||
	    flags == (BUS_DMA_NOWAIT | BUS_DMA_PREFETCHABLE));
	last_map_flags = flags;
	if (chunk_mode) {
		size_t total = 0;
		for (int i = 0; i < n; i++) {
			CHECK((unsigned char *)s[i].memory == chunk_arena + total);
			total += s[i].ds_len;
		}
		CHECK(total == size);
	} else
		CHECK((n == 1 && size == s->ds_len) ||
		    (scatter_mode && n == 2 && size == s[0].ds_len + s[1].ds_len));
	if (fail == 2) return ENOMEM;
	*p = s->memory; mapped++; return 0;
}
static void bus_dmamem_unmap(bus_dma_tag_t t, void *p, size_t n)
{ CHECK(mapped != 0); mapped--; }
static int bus_dmamap_create(bus_dma_tag_t t, size_t n, int ns, size_t max,
    size_t boundary, int flags, bus_dmamap_t *map)
{
	CHECK(flags == BUS_DMA_NOWAIT && ns >= 1 && ns <= 16 && n == max);
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
	if (scatter_mode) {
		map->dm_nsegs = 2;
		map->dm_segs[0].ds_len = PAGE_SIZE;
		map->dm_segs[1] = (bus_dma_segment_t){ .ds_len = 2 * PAGE_SIZE,
		    .ds_addr = UINT64_C(0x11000000) };
	}
	loaded++; return 0;
}
static void bus_dmamap_unload(bus_dma_tag_t t, bus_dmamap_t map)
{ CHECK(loaded != 0 && map->loaded && map->device_owned == 0); loaded--; map->loaded = false; }
static void bus_dmamap_sync(bus_dma_tag_t t, bus_dmamap_t map,
    size_t off, size_t len, int flags)
{
	CHECK(map->loaded && off == 0 && len == map->dm_mapsize);
	CHECK(flags != 0 && (flags & ~15) == 0);
	if (!map->initialized) {
		CHECK(flags == BUS_DMASYNC_PREWRITE);
		for (size_t n = 0; n < len; n++)
			CHECK(((uint8_t *)map->memory)[n] == 0);
		map->initialized = true;
	}
	if (flags & BUS_DMASYNC_POSTWRITE) {
		CHECK(map->device_owned & BUS_DMASYNC_PREWRITE);
		map->device_owned &= ~BUS_DMASYNC_PREWRITE;
	}
	if (flags & BUS_DMASYNC_POSTREAD) {
		CHECK(map->device_owned & BUS_DMASYNC_PREREAD);
		map->device_owned &= ~BUS_DMASYNC_PREREAD;
	}
	map->device_owned |= flags & (BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
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

static unsigned int exports, sg_exports;
static int export_error, sg_error;
static struct dma_buf exported;
static struct sg_table exported_sg;
static struct drm_gem_object *sg_owner;
static struct dma_buf *
drm_gem_prime_export(struct drm_gem_object *obj, int flags)
{
	exports++;
	if (export_error) return ERR_PTR(export_error);
	exported = (struct dma_buf){ .obj = obj, .flags = flags }; return &exported;
}
static struct sg_table *
drm_prime_bus_dmamem_to_sg(bus_dma_tag_t tag, const bus_dma_segment_t *segs,
    int nsegs)
{
	sg_exports++;
	if (sg_error) return ERR_PTR(sg_error);
	CHECK(nsegs == 1 || (scatter_mode && nsegs == 2));
	exported_sg.obj = sg_owner; return &exported_sg;
}

/* Imports of scanout buffers have their own device contract. */
static void drm_gem_dmabuf_release(struct dma_buf *b) { (void)b; abort(); }
static void drm_gem_cma_free_object(struct drm_gem_object *o) { (void)o; abort(); }
static void get_dma_buf(struct dma_buf *b) { b->refs++; }
static void dma_buf_put(struct dma_buf *b) { CHECK(b->refs != 0); b->refs--; }
static int bus_dmamap_load_raw(bus_dma_tag_t t, bus_dmamap_t map,
    bus_dma_segment_t *segs, int nsegs, size_t size, int flags)
{ (void)t; (void)map; (void)segs; (void)nsegs; (void)size; (void)flags; abort(); }

static unsigned int diagnostic_count;
static char diagnostic_stage[32];
static int diagnostic_error;
static size_t diagnostic_size;
static uintmax_t diagnostic_flags;

static int
capture_bo_diagnostic(const char *format, ...)
{
	char message[256];
	va_list ap;
	int length;

	va_start(ap, format);
	length = vsnprintf(message, sizeof(message), format, ap);
	va_end(ap);
	CHECK(length > 0 && (size_t)length < sizeof(message));
	if (strncmp(message, "PVR_BO_SG_FALLBACK ", 19) == 0)
		return length;
	CHECK(sscanf(message,
	    "PVR_BO_CREATE_FAILED stage=%31s error=%d size=%zu flags=%jx",
	    diagnostic_stage, &diagnostic_error, &diagnostic_size,
	    &diagnostic_flags) == 4);
	diagnostic_count++;
	return length;
}

#define printf capture_bo_diagnostic
#include "pvr-bo-body.h"
#undef printf
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
	selected_bvnc = UINT64_C(0x0016006600360026);
	pvr_to_bo(obj)->cma.dmasegs[0].ds_addr = map->dm_segs[0].ds_addr;
	CHECK(pvr_bo_mapping(obj, 0, PAGE_SIZE, &pa, &bits) == 0);
	map->dm_segs[0].ds_addr += PAGE_SIZE;
	CHECK(pvr_bo_mapping(obj, 0, PAGE_SIZE, &pa, &bits) == EOPNOTSUPP);
	map->dm_segs[0].ds_addr -= PAGE_SIZE;
	selected_bvnc = UINT64_C(0x00240038006800b7);
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

static void
cpu_and_dma_contract(struct drm_gem_object *obj)
{
	struct pvr_bo *bo = pvr_to_bo(obj);
	struct drm_gem_object foreign = *obj;
	void *cpu = (void *)(uintptr_t)0xa5;
	unsigned before;

	CHECK(pvr_bo_cpu(NULL, 0, 1, 0, &cpu) == EINVAL);
	CHECK(pvr_bo_cpu(obj, 0, 1, 0, NULL) == EINVAL);
	CHECK(pvr_bo_cpu(obj, 0, 0, 0, &cpu) == EINVAL);
	CHECK(pvr_bo_cpu(obj, obj->size, 1, 0, &cpu) == EINVAL);
	CHECK(pvr_bo_cpu(obj, UINT64_MAX, 1, 0, &cpu) == EINVAL);
	CHECK(pvr_bo_cpu(obj, 1, obj->size, 0, &cpu) == EINVAL);
	CHECK(pvr_bo_cpu(obj, 0, 1, bo->flags | 0x1000, &cpu) == EACCES);
	foreign.funcs = NULL;
	CHECK(pvr_bo_cpu(&foreign, 0, 1, 0, &cpu) == EINVAL);
	CHECK(cpu == (void *)(uintptr_t)0xa5);
	CHECK(pvr_bo_cpu(obj, 64, 32, bo->flags, &cpu) == 0);
	CHECK(cpu == (uint8_t *)bo->cma.vaddr + 64);
	memset(cpu, 0x7d, 32);
	CHECK(pvr_bo_cpu(obj, obj->size - 1, 1, 0, &cpu) == 0);
	CHECK(cpu == (uint8_t *)bo->cma.vaddr + obj->size - 1);
	CHECK(pvr_bo_pin(NULL) == EINVAL);
	CHECK(pvr_bo_pin(&foreign) == EINVAL);
	CHECK(!bo->firmware_pinned);
	CHECK(pvr_bo_pin(obj) == 0 && bo->firmware_pinned);
	CHECK(pvr_bo_pin(obj) == EBUSY && bo->firmware_pinned);
	pvr_bo_unpin(obj);
	CHECK(!bo->firmware_pinned);
	CHECK(pvr_bo_pin(obj) == 0);
	pvr_bo_unpin(obj);
	CHECK(!bo->firmware_pinned);
	CHECK(pvr_bo_hold(NULL) == EINVAL);
	CHECK(pvr_bo_hold(&foreign) == EINVAL);
	CHECK(pvr_bo_hold(obj) == 0 && bo->firmware_holds == 1);
	CHECK(pvr_bo_hold(obj) == 0 && bo->firmware_holds == 2);
	CHECK(pvr_bo_pin(obj) == EBUSY && !bo->firmware_pinned);
	pvr_bo_unhold(obj);
	CHECK(bo->firmware_holds == 1);
	CHECK(pvr_bo_pin(obj) == EBUSY && !bo->firmware_pinned);
	bo->firmware_holds = UINT_MAX;
	CHECK(pvr_bo_hold(obj) == EOVERFLOW && bo->firmware_holds == UINT_MAX);
	bo->firmware_holds = 1;
	pvr_bo_unhold(obj);
	CHECK(bo->firmware_holds == 0 && pvr_bo_pin(obj) == 0);
	CHECK(pvr_bo_hold(obj) == EBUSY && bo->firmware_holds == 0);
	pvr_bo_unpin(obj);
	/* Duplicate aliases close each DMA direction once. */
	before = syncs;
	pvr_bo_sync(obj, BUS_DMASYNC_POSTREAD);
	CHECK(syncs == before);
	pvr_bo_sync(obj, BUS_DMASYNC_POSTWRITE);
	CHECK(syncs == before + 1 && bo->dma_owned == 0);
	pvr_bo_sync(obj, BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	CHECK(syncs == before + 1);
	pvr_bo_sync(obj, BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	pvr_bo_sync(obj, BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	CHECK(bo->dma_owned == (BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE));
	pvr_bo_sync(obj, BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	CHECK(bo->dma_owned == 0);
	before = syncs;
	pvr_bo_sync(obj, BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	CHECK(syncs == before);
	/* The last GEM put must close this pair before unloading DMA. */
	pvr_bo_sync(obj, BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
}

static void
prime_contract(struct drm_gem_object *obj)
{
	struct pvr_bo *bo = pvr_to_bo(obj);
	unsigned int references = obj->refcount.refs;
	unsigned int before = exports, sg_before = sg_exports;
	const int denied = (bo->flags & (DRM_PVR_BO_PM_FW_PROTECT |
	    DRM_PVR_BO_EMBER_CPU_CACHED)) ? -EACCES : 0;
	sg_owner = obj;

	CHECK(obj->funcs->export != NULL && obj->funcs->get_sg_table != NULL);
	if (denied) {
		CHECK(PTR_ERR(obj->funcs->export(obj, 0x123)) == denied);
		CHECK(PTR_ERR(obj->funcs->get_sg_table(obj)) == denied);
		CHECK(exports == before && sg_exports == sg_before);
	} else {
		CHECK(obj->funcs->export(obj, 0x123) == &exported);
		CHECK(exported.obj == obj && exported.flags == 0x123);
		export_error = -ENOMEM;
		CHECK(PTR_ERR(obj->funcs->export(obj, 7)) == -ENOMEM);
		export_error = 0;
		CHECK(obj->funcs->get_sg_table(obj) == &exported_sg);
		CHECK(exported_sg.obj == obj);
		sg_error = -ENOMEM;
		CHECK(PTR_ERR(obj->funcs->get_sg_table(obj)) == -ENOMEM);
		sg_error = 0;
		bo->cma.dmamap->_dm_flags = _BUS_DMAMAP_IS_BOUNCING;
		sg_before = sg_exports;
		CHECK(PTR_ERR(obj->funcs->get_sg_table(obj)) == -EOPNOTSUPP);
		CHECK(sg_before == sg_exports);
		bo->cma.dmamap->_dm_flags = 0;
	}
	/* Every rejection leaves all output arguments and references unchanged. */
	for (int mode = 0; mode < 4; mode++) {
		for (int prot = 0; prot < 16; prot++) {
			struct uvm_object dummy, *uobj = &dummy;
			off_t off = 0;
			int flags = mode, advice = 0x456, maxprot = 0xabc;
			int want = (mode != MAP_SHARED || (prot & ~3)) ? -EINVAL :
			    ((bo->flags & DRM_PVR_BO_ALLOW_CPU_USERSPACE_ACCESS) ? 0 : -EACCES);
			CHECK(pvr_prime_mmap(obj, &off, PAGE_SIZE, prot, &flags,
			    &advice, &uobj, &maxprot) == want);
			CHECK(off == 0 && flags == mode && advice == 0x456);
			if (want != 0) {
				CHECK(uobj == &dummy && maxprot == 0xabc);
				CHECK(obj->refcount.refs == references);
			} else {
				CHECK(uobj == &obj->gemo_uvmobj && maxprot == 3);
				CHECK(obj->refcount.refs == references + 1);
				drm_gem_pager_detach(uobj);
			}
		}
	}
	{
		const off_t offsets[] = { -1, 1, (off_t)obj->size, INT64_MAX };
		const size_t sizes[] = { 0, PAGE_SIZE - 1, obj->size + PAGE_SIZE, SIZE_MAX };
		for (unsigned int n = 0; n < 8; n++) {
			struct uvm_object dummy, *uobj = &dummy;
			off_t off = n < 4 ? offsets[n] : 0, saved = off;
			size_t size = n < 4 ? PAGE_SIZE : sizes[n - 4];
			int flags = MAP_SHARED, advice = 7, maxprot = 8;
			CHECK(pvr_prime_mmap(obj, &off, size, PROT_READ, &flags,
			    &advice, &uobj, &maxprot) == -EINVAL);
			CHECK(off == saved && flags == MAP_SHARED && advice == 7);
			CHECK(uobj == &dummy && maxprot == 8 && obj->refcount.refs == references);
		}
	}
	CHECK(obj->refcount.refs == references);
}

static void
cached_contract(struct drm_device *dev, struct drm_file *file)
{
	struct drm_pvr_ioctl_create_bo_args args = {
	    .size = PAGE_SIZE, .flags = DRM_PVR_BO_EMBER_CPU_CACHED };
	struct drm_gem_object *obj;
	struct vm_map map = { 0 };
	struct vm_map_entry entry = { .protection = PROT_READ | PROT_WRITE };
	struct uvm_faultinfo ufi = { .entry = &entry, .orig_map = &map };
	struct vm_page *pages[] = { NULL };
	struct drm_gem_close close;
	unsigned int before;
	void *cpu = NULL;

	CHECK(pvr_create_bo(dev, &args, file) == -EINVAL);
	args.flags |= DRM_PVR_BO_ALLOW_CPU_USERSPACE_ACCESS;
	CHECK(pvr_create_bo(dev, &args, file) == 0);
	CHECK(last_map_flags == BUS_DMA_NOWAIT);
	obj = file->handles[args.handle];
	CHECK(pvr_to_bo(obj)->flags == args.flags);
	prime_contract(obj);
	CHECK(pvr_bo_cpu(obj, 0, 4, 0, &cpu) == EACCES && cpu == NULL);
	before = syncs;
	pvr_bo_sync(obj, BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	CHECK(syncs == before);
	entry.object.uvm_obj = &obj->gemo_uvmobj;
	CHECK(pvr_bo_fault(&ufi, 0, pages, 1, 0, PROT_READ, 0) == 0);
	CHECK(fault_map_flags == 0 && fault_enters == 1 && fault_unlocks == 1);
	close = (struct drm_gem_close){ args.handle, 0 };
	CHECK(drm_gem_close_ioctl(dev, &close, file) == 0);
	CHECK(syncs == before + 2);
	empty(dev);
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

	(void)capture_bo_diagnostic;
	dev.dmat = &dev;
	manager.next = 0x100000;
	for (unsigned int n = 0; n < sizeof(badsize) / sizeof(badsize[0]); n++) {
		create = (struct drm_pvr_ioctl_create_bo_args){ badsize[n], 0xdead, 0, 4 };
		saved = create; old_attempts = attempts;
		CHECK(pvr_create_bo(&dev, &create, &a) == -EINVAL);
		CHECK(memcmp(&create, &saved, sizeof(create)) == 0 && attempts == old_attempts);
	}
	CHECK(diagnostic_count == 0);
	for (unsigned int n = 1; n <= 5; n++) {
		static const char *const stages[] = { "dmamem_alloc",
		    "dmamem_map", "dmamap_create", "dmamap_load", "gem_handle" };
		create = (struct drm_pvr_ioctl_create_bo_args){ PAGE_SIZE, 0xdead, 0, 4 };
		saved = create; fail = n;
		CHECK(pvr_create_bo(&dev, &create, &a) == (n == 4 ? -EIO : n == 5 ? -EMFILE : -ENOMEM));
		CHECK(diagnostic_count == n);
		CHECK(strcmp(diagnostic_stage, stages[n - 1]) == 0);
		CHECK(diagnostic_error == (n == 4 ? EIO : n == 5 ? EMFILE : ENOMEM));
		CHECK(diagnostic_size == PAGE_SIZE && diagnostic_flags == 4);
		CHECK(memcmp(&create, &saved, sizeof(create)) == 0);
		empty(&dev);
	}
	/* Representable large requests reach the allocator; no small driver cap.
	 * A chunk that fails despite free pages is retried once, not waited out. */
	create = (struct drm_pvr_ioctl_create_bo_args){ 128U * 1024 * 1024, 0xdead, 0, 4 };
	fail = 1; old_attempts = attempts; pauses = 0;
	CHECK(pvr_create_bo(&dev, &create, &a) == -ENOMEM);
	CHECK(attempts == old_attempts + 3 && pauses == 1 && structures == 0);
	empty(&dev); fail = 0;
	/* Wired memory and the free target bound a request before any wait. */
	uvmexp.wired = uvmexp.npages - 8192; uvmexp.freetarg = 1; fail = 1;
	create = (struct drm_pvr_ioctl_create_bo_args){ (uint64_t)8192 * PAGE_SIZE, 0xdead, 0, 4 };
	old_attempts = attempts; pauses = 0;
	CHECK(pvr_create_bo(&dev, &create, &a) == -ENOMEM);
	CHECK(attempts == old_attempts + 1 && pauses == 0 && structures == 0);
	CHECK(strcmp(diagnostic_stage, "dmamem_alloc") == 0);
	diagnostic_count--;	/* Later checks count only their own reports. */
	empty(&dev);
	uvmexp.wired = uvmexp.freetarg = 0; fail = 0;
	for (unsigned int flags = 0; flags < 16; flags++) {
		create = (struct drm_pvr_ioctl_create_bo_args){ 4096, 0xdead, 0, flags };
		saved = create;
		if (flags >= 8 || (flags & 6) == 6) {
			CHECK(pvr_create_bo(&dev, &create, &a) == -EINVAL);
			CHECK(memcmp(&create, &saved, sizeof(create)) == 0);
			continue;
		}
		CHECK(pvr_create_bo(&dev, &create, &a) == 0);
		CHECK(diagnostic_count == 6);
		obj = a.handles[create.handle];
		CHECK(obj->size == PAGE_SIZE && obj->refcount.refs == 1 && dev.refs == 2);
		CHECK(pvr_to_bo(obj)->flags == flags);
		prime_contract(obj);
		mapping_contract(obj, flags);
		cpu_and_dma_contract(obj);
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
	{
		off_t relative = PAGE_SIZE;
		int flags = MAP_SHARED, advice = 9, maxprot = 0;
		struct uvm_object *prime_uobj = NULL;
		CHECK(pvr_prime_mmap(obj, &relative, PAGE_SIZE, PROT_READ | PROT_WRITE,
		    &flags, &advice, &prime_uobj, &maxprot) == 0);
		CHECK(relative == PAGE_SIZE && prime_uobj == uobj && maxprot == 3);
		CHECK(obj->refcount.refs == 3);
		drm_gem_pager_detach(prime_uobj);
	}

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
	/* The earlier lifetime test deliberately closes the owner reference. */
	dev.refs = 1;
	cached_contract(&dev, &a);
	/* Force the single-span allocator to fail while scattered pages exist. */
	scatter_mode = true;
	create = (struct drm_pvr_ioctl_create_bo_args){
	    .size = 3 * PAGE_SIZE, .flags = DRM_PVR_BO_ALLOW_CPU_USERSPACE_ACCESS };
	CHECK(pvr_create_bo(&dev, &create, &a) == 0);
	CHECK(scatter_allocations == 1);
	obj = a.handles[create.handle];
	CHECK(obj->size == 3 * PAGE_SIZE);
	prime_contract(obj);
	{
		uint64_t pa = 0xdead, bits = 0xbeef;
		uint64_t pages[3 * PAGE_SIZE / 4096];
		CHECK(pvr_bo_mapping(obj, 0, obj->size, &pa, &bits) == EFBIG);
		CHECK(pa == 0xdead && bits == 0xbeef);
		CHECK(pvr_bo_mapping(obj, PAGE_SIZE, 2 * PAGE_SIZE, &pa, &bits) == 0);
		CHECK(pa == UINT64_C(0x11000000) && bits == 0);
		CHECK(pvr_bo_pages(obj, 0, obj->size, pages, &bits) == 0);
		for (unsigned int n = 0; n < 3 * PAGE_SIZE / 4096; n++)
			CHECK(pages[n] == (n < PAGE_SIZE / 4096 ?
			    UINT64_C(0x10000000) + n * 4096 :
			    UINT64_C(0x11000000) + n * 4096 - PAGE_SIZE));
		selected_bvnc = UINT64_C(0x0016006600360026);
		CHECK(pvr_bo_pages(obj, 0, obj->size, pages, &bits) == 0);
		pvr_to_bo(obj)->cma.dmamap->dm_segs[1].ds_addr += PAGE_SIZE;
		CHECK(pvr_bo_pages(obj, 0, obj->size, pages, &bits) == EOPNOTSUPP);
		pvr_to_bo(obj)->cma.dmamap->dm_segs[1].ds_addr -= PAGE_SIZE;
		selected_bvnc = UINT64_C(0x00240038006800b7);
	}
	{
		struct vm_map map = { 0 };
		struct vm_map_entry entry = { .object.uvm_obj = &obj->gemo_uvmobj,
		    .protection = PROT_READ | PROT_WRITE };
		struct uvm_faultinfo ufi = { .entry = &entry, .orig_map = &map };
		struct vm_page *pages[] = { NULL };
		for (unsigned int n = 0; n < 3; n++) {
			entry.offset = n * PAGE_SIZE;
			CHECK(pvr_bo_fault(&ufi, 0, pages, 1, 0, PROT_READ, 0) == 0);
			CHECK(fault_enter_pa == (n == 0 ? UINT64_C(0x10000000) :
			    UINT64_C(0x11000000) + (n - 1) * PAGE_SIZE));
			CHECK(fault_map_flags == BUS_DMA_PREFETCHABLE);
		}
	}
	close_args = (struct drm_gem_close){ create.handle, 0 };
	CHECK(drm_gem_close_ioctl(&dev, &close_args, &a) == 0);
	empty(&dev);
	create = (struct drm_pvr_ioctl_create_bo_args){
	    .size = 3 * PAGE_SIZE, .flags = DRM_PVR_BO_ALLOW_CPU_USERSPACE_ACCESS };
	/* Short free pages and nothing reclaimed: bounded wait, then ENOMEM. */
	available_pages = uvmexp.reserve_kernel + uvmexp.reserve_pagedaemon + 1;
	old_attempts = attempts; pauses = kicks = 0;
	CHECK(pvr_create_bo(&dev, &create, &a) == -ENOMEM);
	CHECK(attempts == old_attempts + 1 && scatter_allocations == 1);
	CHECK(pauses == 2000 && kicks == 2000 && structures == 0);
	empty(&dev);
	/* Nothing reclaimable fails at once; a signal ends the wait with EINTR. */
	pauses = kicks = 0; reclaimable = false;
	CHECK(pvr_create_bo(&dev, &create, &a) == -ENOMEM && pauses == 0);
	empty(&dev);
	reclaimable = true; pause_result = EINTR;
	CHECK(pvr_create_bo(&dev, &create, &a) == -EINTR && pauses == 1);
	empty(&dev);
	pause_result = EWOULDBLOCK;
	/* The pagedaemon refills free pages from cache: the BO then succeeds. */
	pauses = kicks = 0; reclaim_per_pause = 1;
	CHECK(pvr_create_bo(&dev, &create, &a) == 0);
	CHECK(pauses == 3 && kicks == 3 && scatter_allocations == 2);
	close_args = (struct drm_gem_close){ create.handle, 0 };
	CHECK(drm_gem_close_ioctl(&dev, &close_args, &a) == 0);
	empty(&dev);
	reclaim_per_pause = 0;
	available_pages = 1048576;
	segment_metadata_fail = true;
	CHECK(pvr_create_bo(&dev, &create, &a) == -ENOMEM && scatter_allocations == 2);
	empty(&dev);
	segment_metadata_fail = false;
	/* An impossible request must not allocate metadata or drain free pages. */
	fail = 1;
	old_attempts = attempts;
	create = (struct drm_pvr_ioctl_create_bo_args){
	    .size = ((uint64_t)uvmexp.npages + 1) * PAGE_SIZE,
	    .flags = DRM_PVR_BO_ALLOW_CPU_USERSPACE_ACCESS };
	CHECK(pvr_create_bo(&dev, &create, &a) == -ENOMEM);
	CHECK(attempts == old_attempts + 1);
	empty(&dev);
	fail = 0;
	/* GPU-only render targets need virtual, not physical, contiguity too. */
	create = (struct drm_pvr_ioctl_create_bo_args){ .size = 3 * PAGE_SIZE, .flags = 0 };
	CHECK(pvr_create_bo(&dev, &create, &a) == 0);
	obj = a.handles[create.handle];
	{
		uint64_t pages[3 * PAGE_SIZE / 4096], bits;
		CHECK(pvr_bo_pages(obj, 0, obj->size, pages, &bits) == 0);
		CHECK(pages[0] == UINT64_C(0x10000000) && pages[PAGE_SIZE / 4096] == UINT64_C(0x11000000));
	}
	close_args = (struct drm_gem_close){ create.handle, 0 };
	CHECK(drm_gem_close_ioctl(&dev, &close_args, &a) == 0);
	empty(&dev);
	old_attempts = attempts;
	create.flags = DRM_PVR_BO_PM_FW_PROTECT;
	CHECK(pvr_create_bo(&dev, &create, &a) == -ENOMEM && attempts == old_attempts + 1);
	empty(&dev);
	scatter_mode = false;
	/* Large GPU-only BOs accumulate chunks while the pagedaemon reclaims;
	 * a failure releases every chunk already taken. */
	chunk_mode = true; chunk_calls = 0;
	create = (struct drm_pvr_ioctl_create_bo_args){ .size = 130 * PAGE_SIZE, .flags = 0 };
	available_pages = uvmexp.reserve_kernel + uvmexp.reserve_pagedaemon + 70;
	pauses = 0; reclaim_per_pause = 8;
	CHECK(pvr_create_bo(&dev, &create, &a) == 0);
	obj = a.handles[create.handle];
	/* Each call takes what the margin allows: 69 free pages, then the 61
	 * the pagedaemon reclaims; it waits for a useful amount, not single pages. */
	CHECK(obj->size == 130 * PAGE_SIZE && pvr_to_bo(obj)->nsegments == 2);
	CHECK(chunk_calls == 2 && pauses == 8 && chunk_used == 130 * PAGE_SIZE);
	close_args = (struct drm_gem_close){ create.handle, 0 };
	CHECK(drm_gem_close_ioctl(&dev, &close_args, &a) == 0);
	empty(&dev);
	CHECK(allocated == 0);
	available_pages = uvmexp.reserve_kernel + uvmexp.reserve_pagedaemon + 70;
	reclaim_per_pause = 0; pauses = 0;
	CHECK(pvr_create_bo(&dev, &create, &a) == -ENOMEM);
	CHECK(pauses == 2000 && allocated == 0 && structures == 0 &&
	    available_pages == uvmexp.reserve_kernel + uvmexp.reserve_pagedaemon + 70);
	empty(&dev);
	/* Chunks keep free pages near freemin; the pagedaemon supplies the rest. */
	uvmexp.freemin = 200;
	available_pages = 250; reclaim_per_pause = 8; pauses = 0;
	lowest_available = UINT64_MAX;
	CHECK(pvr_create_bo(&dev, &create, &a) == 0);
	CHECK(pauses == 2 && lowest_available == 136);
	obj = a.handles[create.handle];
	close_args = (struct drm_gem_close){ create.handle, 0 };
	CHECK(drm_gem_close_ioctl(&dev, &close_args, &a) == 0);
	empty(&dev);
	uvmexp.freemin = 0; reclaim_per_pause = 0;
	chunk_mode = false;
	available_pages = 1048576;
	printf("PASS: %u PowerVR BO checks (page=%zu, syncs=%u)\n",
	    checks, PAGE_SIZE, syncs);
	return 0;
}
