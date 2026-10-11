/* Origin: EmberBSD actual-source GEM/BO lifetime contract, 2026-10-11. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PAGE_SIZE 4096
#define KM_SLEEP 0
#define BUS_DMA_WAITOK 1
#define BUS_DMA_PREFETCHABLE 2
#define VM_BESTFIT 1
#define VM_NOSLEEP 2
#define KASSERT assert
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
typedef uint8_t __u8;
typedef uint16_t __u16;
typedef uint32_t __u32;
typedef uint64_t __u64;
typedef int32_t __s32;
typedef int64_t __s64;
typedef void *device_t;
typedef void *ACPI_HANDLE;
typedef void *bus_dma_tag_t;
typedef size_t bus_size_t;
typedef uint64_t bus_addr_t;
typedef uint64_t vmem_addr_t;
typedef int kmutex_t;
typedef int vmem_t;
typedef struct { uint64_t ds_addr, ds_len; void *memory; } bus_dma_segment_t;
typedef struct { int dm_nsegs; bus_dma_segment_t dm_segs[1]; } *bus_dmamap_t;
struct drm_device { void *dev_private; };
struct drm_gem_object;
struct drm_gem_object_funcs { void (*free)(struct drm_gem_object *); };
struct drm_gem_object {
	struct drm_device *dev;
	size_t size;
	unsigned refs;
	uint64_t vma_node;
	const struct drm_gem_object_funcs *funcs;
};
struct drm_gem_cma_object {
	struct drm_gem_object base;
	bus_dmamap_t dmamap;
	bus_dma_segment_t dmasegs[1];
	bus_size_t dmasize;
	bus_dma_tag_t dmat;
	void *vaddr;
};
struct drm_syncobj;
struct drm_file {
	struct drm_gem_object *handles[16];
	struct drm_syncobj *syncs[4];
};
#define to_drm_gem_cma_obj(p) container_of(p,struct drm_gem_cma_object,base)
#include "uapi-body.h"
#include "drv-body.h"

static unsigned checks, fail, allocated, mapped, maps, loaded, ranges, gems;
static unsigned bad_geometry;
static size_t va_sizes[16];
static bus_dma_segment_t dma_segments[16];
#define CHECK(c) do { checks++; if (!(c)) { \
 fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c); exit(1); } } while (0)
static void mutex_enter(kmutex_t *m) { CHECK((*m)++ == 0); }
static void mutex_exit(kmutex_t *m) { CHECK(--*m == 0); }
static void *kmem_zalloc(size_t n, int f) { return calloc(1,n); }
static void kmem_free(void *p, size_t n) { free(p); }
static int vmem_alloc(vmem_t *v, size_t n, int f, vmem_addr_t *a)
{
	if (fail == 1) return ENOMEM;
	for (unsigned i=0;i<16;i++) {
		if (va_sizes[i]) continue;
		va_sizes[i]=n; *a=BCMV3D_VA_START+i*BCMV3D_BO_MAX;
		ranges++; return 0;
	}
	return ENOMEM;
}
static void vmem_free(vmem_t *v, vmem_addr_t a, size_t n)
{
	unsigned i=(a-BCMV3D_VA_START)/BCMV3D_BO_MAX;
	CHECK(ranges>0 && a>=BCMV3D_VA_START && i<16);
	CHECK(va_sizes[i]==n); va_sizes[i]=0; ranges--;
}
static int bus_dmamem_alloc(bus_dma_tag_t t, size_t n, size_t a, size_t b,
    bus_dma_segment_t *s, int max, int *count, int f)
{
	if(fail==2)return ENOMEM;
	for(unsigned i=0;i<16;i++) {
		if(dma_segments[i].memory)continue;
		s->ds_addr=0x20000000+i*BCMV3D_BO_MAX; s->ds_len=n;
		s->memory=malloc(n); memset(s->memory,0xa5,n);
		dma_segments[i]=*s; *count=1; allocated++; return 0;
	}
	return ENOMEM;
}
static void bus_dmamem_free(bus_dma_tag_t t, bus_dma_segment_t *s, int n)
{
	unsigned i=(s->ds_addr-0x20000000)/BCMV3D_BO_MAX;
	CHECK(allocated>0 && i<16 && dma_segments[i].memory==s->memory);
	dma_segments[i].memory=NULL; allocated--; free(s->memory);
}
static int bus_dmamem_map(bus_dma_tag_t t, bus_dma_segment_t *s, int n,
    size_t z, void **p, int f)
{ CHECK(f & BUS_DMA_PREFETCHABLE); if(fail==3)return ENOMEM;
  mapped++; *p=s->memory; return 0; }
static void bus_dmamem_unmap(bus_dma_tag_t t, void *p, size_t n)
{ CHECK(mapped>0); mapped--; }
static int bus_dmamap_create(bus_dma_tag_t t,size_t n,int ns,size_t max,
    size_t b,int f,bus_dmamap_t *m)
{ if(fail==4)return ENOMEM; *m=calloc(1,sizeof(**m)); maps++; return 0; }
static void bus_dmamap_destroy(bus_dma_tag_t t,bus_dmamap_t m)
{ CHECK(maps>0); maps--; free(m); }
static int bus_dmamap_load(bus_dma_tag_t t,bus_dmamap_t m,void *p,
    size_t n,void *l,int f)
{
	if(fail==5)return EIO;
	loaded++; m->dm_nsegs=bad_geometry==1?2:1;
	for(unsigned i=0;i<16;i++)if(dma_segments[i].memory==p)
		m->dm_segs[0]=dma_segments[i];
	if(bad_geometry==2)m->dm_segs[0].ds_len--;
	if(bad_geometry==3)m->dm_segs[0].ds_addr++;
	if(bad_geometry==4)m->dm_segs[0].ds_addr=UINT64_C(1)<<36;
	if(bad_geometry==5)m->dm_segs[0].ds_addr=(UINT64_C(1)<<36)-PAGE_SIZE;
	return 0;
}
static void bus_dmamap_unload(bus_dma_tag_t t,bus_dmamap_t m)
{ CHECK(loaded>0); loaded--; }
static void drm_gem_private_object_init(struct drm_device *d,
    struct drm_gem_object *o,size_t n)
{ o->dev=d; o->size=n; o->refs=1; gems++; }
static void drm_gem_object_release(struct drm_gem_object *o)
{ CHECK(o->refs==0 && gems>0); gems--; }
static void drm_gem_object_get(struct drm_gem_object *o) { CHECK(o->refs); o->refs++; }
static void drm_gem_object_put_unlocked(struct drm_gem_object *o)
{ CHECK(o->refs); if(--o->refs==0)o->funcs->free(o); }
static int drm_gem_handle_create(struct drm_file *f,struct drm_gem_object *o,uint32_t *h)
{
	if (fail == 6)
		return -ENOMEM;
	for (unsigned i = 1; i < 16; i++) {
		if (f->handles[i] != NULL)
			continue;
		f->handles[i] = o;
		drm_gem_object_get(o);
		*h = i;
		return 0;
	}
	return -ENOSPC;
}
static struct drm_gem_object *drm_gem_object_lookup(struct drm_file *f,uint32_t h)
{ if(h>=16 || !f->handles[h])return NULL; drm_gem_object_get(f->handles[h]); return f->handles[h]; }
static int drm_gem_create_mmap_offset(struct drm_gem_object *o)
{ if(fail==7)return -ENOMEM; o->vma_node=UINT64_C(0x100000000); return 0; }
static void drm_gem_free_mmap_offset(struct drm_gem_object *o) { o->vma_node=0; }
static uint64_t drm_vma_node_offset_addr(uint64_t *node) { return *node; }
#include "bo-body.h"

static void close_handle(struct drm_file *f,unsigned h)
{ struct drm_gem_object *o=f->handles[h]; f->handles[h]=NULL; drm_gem_object_put_unlocked(o); }
static void empty(struct bcmv3d_device *c)
{ CHECK(!allocated && !mapped && !maps && !loaded && !ranges && !gems && !c->allocated); }

#ifndef BCMV3D_JOB_CONTRACT
int
main(void)
{
	struct bcmv3d_device c={0};
	struct drm_device d={.dev_private=&c};
	struct drm_file f={0}, other={0};
	struct drm_v3d_create_bo a;
	struct drm_v3d_mmap_bo m;
	struct drm_v3d_get_bo_offset offset;
	struct drm_gem_object *mapping;
	c.drm=&d;
	const uint32_t invalid[]={0,BCMV3D_BO_MAX+1,UINT32_MAX};
	for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
		a=(struct drm_v3d_create_bo){.size=invalid[i]};
		CHECK(bcmv3d_create_bo(&d,&a,&f)==-EINVAL); empty(&c);
	}
	a=(struct drm_v3d_create_bo){.size=4096,.flags=1};
	CHECK(bcmv3d_create_bo(&d,&a,&f)==-EINVAL); empty(&c);
	for(fail=1;fail<=6;fail++) {
		a=(struct drm_v3d_create_bo){.size=4096};
		CHECK(bcmv3d_create_bo(&d,&a,&f)<0); empty(&c);
	}
	fail=0;
	for(bad_geometry=1;bad_geometry<=5;bad_geometry++) {
		a=(struct drm_v3d_create_bo){.size=8192,.handle=99,.offset=99};
		CHECK(bcmv3d_create_bo(&d,&a,&f)<0 && a.handle==0 && a.offset==0);
		empty(&c);
	}
	bad_geometry=0;
	a=(struct drm_v3d_create_bo){.size=4097};
	CHECK(bcmv3d_create_bo(&d,&a,&f)==0);
	CHECK(c.allocated==8192 && a.offset>=BCMV3D_VA_START);
	CHECK(a.offset!=bcmv3d_to_bo(f.handles[a.handle])->cma.dmamap->dm_segs[0].ds_addr);
	for(unsigned i=0;i<8192;i++) CHECK(((uint8_t *)bcmv3d_to_bo(f.handles[a.handle])->cma.vaddr)[i]==0);
	offset=(struct drm_v3d_get_bo_offset){.handle=a.handle};
	CHECK(bcmv3d_get_bo_offset(&d,&offset,&other)==-ENOENT);
	CHECK(bcmv3d_get_bo_offset(&d,&offset,&f)==0 && offset.offset==a.offset);
	m=(struct drm_v3d_mmap_bo){.handle=a.handle,.flags=1};
	CHECK(bcmv3d_mmap_bo(&d,&m,&f)==-EINVAL); m.flags=0;
	CHECK(bcmv3d_mmap_bo(&d,&m,&other)==-ENOENT);
	fail=7; CHECK(bcmv3d_mmap_bo(&d,&m,&f)==-ENOMEM); fail=0;
	CHECK(bcmv3d_mmap_bo(&d,&m,&f)==0 && m.offset!=0);
	mapping=f.handles[a.handle]; drm_gem_object_get(mapping);
	close_handle(&f,a.handle); CHECK(gems==1 && c.allocated==8192);
	drm_gem_object_put_unlocked(mapping); empty(&c);
	for(unsigned cycle=0;cycle<3;cycle++) {
		unsigned handles[4];
		for(unsigned i=0;i<4;i++) {
			a=(struct drm_v3d_create_bo){.size=BCMV3D_BO_MAX};
			CHECK(bcmv3d_create_bo(&d,&a,&f)==0); handles[i]=a.handle;
		}
		CHECK(c.allocated==BCMV3D_BO_TOTAL);
		uint32_t reused=bcmv3d_to_bo(f.handles[handles[1]])->gpu;
		close_handle(&f,handles[1]);
		a=(struct drm_v3d_create_bo){.size=BCMV3D_BO_MAX};
		CHECK(bcmv3d_create_bo(&d,&a,&f)==0 && a.offset==reused);
		handles[1]=a.handle;
		a=(struct drm_v3d_create_bo){.size=4096};
		CHECK(bcmv3d_create_bo(&d,&a,&f)==-ENOMEM);
		for(unsigned i=0;i<4;i++)close_handle(&f,handles[i]);
		empty(&c);
	}
	printf("PASS: %u native BO validation/lifetime checks\n",checks);
	return 0;
}
#endif
