/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD actual-source PRIME callback/error contract. */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
#define WARN_ON(x) (x)
#define DMA_ATTR_SKIP_CPU_SYNC 1
#define FREAD 1
#define FWRITE 2
#define VM_PROT_READ 1
#define VM_PROT_WRITE 2
#define VM_PROT_EXECUTE 4
struct uvm_object { int id; };
enum dma_data_direction { DMA_NONE, DMA_BIDIRECTIONAL };
struct sg_table { void *sgl; unsigned nents; };
struct drm_gem_object;
struct drm_gem_object_funcs {
 struct sg_table *(*get_sg_table)(struct drm_gem_object *);
};
struct drm_driver {
 struct sg_table *(*gem_prime_get_sg_table)(struct drm_gem_object *);
};
struct drm_device { struct drm_driver *driver; };
struct drm_gem_object { struct drm_device *dev; struct drm_gem_object_funcs *funcs; };
struct dma_buf;
struct dma_buf_ops {
 int (*mmap)(struct dma_buf *, off_t *, size_t, int, int *, int *, struct uvm_object **, int *);
};
struct dma_buf { void *priv; size_t size; struct dma_buf_ops *ops; };
struct dma_buf_attachment { struct dma_buf *dmabuf; void *dev; };
struct file { void *f_data; int f_flag; };
static unsigned map_calls, frees, mmap_calls;
static int map_ok, mmap_error, provider_maxprot = 7;
static struct sg_table table, *provider_result;
static struct sg_table *provider(struct drm_gem_object *obj) { return provider_result; }
static int dma_map_sg_attrs(void *dev, void *sg, unsigned n, int dir, int flags)
{ map_calls++; assert(sg == &table && n == 1); return map_ok; }
static void sg_free_table(struct sg_table *sg) { assert(sg == &table); frees++; }
static void kfree(void *p) { assert(p == &table); frees++; }
static int do_mmap(struct dma_buf *buf, off_t *off, size_t len, int prot,
 int *flags, int *advice, struct uvm_object **obj, int *maxprot)
{ mmap_calls++; *maxprot=provider_maxprot; return mmap_error; }
#include "bodies.h"
/* AI-assisted: a permissive provider must not widen descriptor rights. */
static void
access_modes(struct file *file)
{
 struct uvm_object *uobj = NULL;
 int flags = 0, advice = 0, maxprot;
 off_t off;
 for (unsigned rights = 0; rights < 4; rights++) {
  file->f_flag = rights;
  for (unsigned prot = 0; prot < 16; prot++) {
   for (unsigned provider = 0; provider < 8; provider++) {
    unsigned calls = mmap_calls;
    off = 0;
    maxprot = 0x55;
    provider_maxprot = provider;
    int error = dmabuf_fop_mmap(file, &off, 4096, prot, &flags,
        &advice, &uobj, &maxprot);
    if (prot & ~(VM_PROT_READ | VM_PROT_WRITE)) {
     assert(error == EINVAL && mmap_calls == calls && maxprot == 0x55);
    } else if (prot & ~rights) {
     assert(error == EACCES && mmap_calls == calls && maxprot == 0x55);
    } else {
     assert(error == 0 && mmap_calls == calls + 1);
     assert(maxprot == (int)(provider & rights));
    }
   }
  }
 }
 file->f_flag = FREAD;
 provider_maxprot = 7;
 mmap_error = -EIO;
 maxprot = 0x55;
 off = 0;
 assert(dmabuf_fop_mmap(file, &off, 4096, VM_PROT_READ, &flags,
     &advice, &uobj, &maxprot) == EIO);
 assert(maxprot == 0x55);
 mmap_error = 0;
}
int main(void)
{
 struct drm_gem_object_funcs funcs = {provider};
 struct drm_driver driver = {provider};
 struct drm_device dev = {&driver};
 struct drm_gem_object obj = {&dev, &funcs};
 struct dma_buf_ops ops = {do_mmap};
 struct dma_buf buf = {&obj,8192,&ops};
 struct dma_buf_attachment attach = {&buf,NULL};
 struct file file = {&buf, FREAD | FWRITE};
 struct uvm_object *uobj = NULL;
 int flags=0,advice=0,maxprot=0;
 off_t off;
 table.sgl=&table; table.nents=1;
 provider_result=ERR_PTR(-ENOMEM);
 assert(drm_gem_map_dma_buf(&attach,DMA_BIDIRECTIONAL)==provider_result);
 assert(map_calls==0 && frees==0);
 provider_result=ERR_PTR(-EIO);
 assert(drm_gem_map_dma_buf(&attach,DMA_BIDIRECTIONAL)==provider_result);
 provider_result=NULL;
 assert(IS_ERR(drm_gem_map_dma_buf(&attach,DMA_BIDIRECTIONAL)));
 funcs.get_sg_table=NULL; driver.gem_prime_get_sg_table=NULL;
 assert(IS_ERR(drm_gem_map_dma_buf(&attach,DMA_BIDIRECTIONAL)));
 assert(map_calls==0 && frees==0);
 driver.gem_prime_get_sg_table=provider; provider_result=&table; map_ok=1;
 assert(drm_gem_map_dma_buf(&attach,DMA_BIDIRECTIONAL)==&table);
 obj.funcs=NULL;
 assert(drm_gem_map_dma_buf(&attach,DMA_BIDIRECTIONAL)==&table);
 obj.funcs=&funcs; funcs.get_sg_table=provider; map_ok=0;
 assert(drm_gem_map_dma_buf(&attach,DMA_BIDIRECTIONAL)==ERR_PTR(-ENOMEM));
 assert(map_calls==3 && frees==2);
 assert(drm_gem_map_dma_buf(&attach,DMA_NONE)==ERR_PTR(-EINVAL));
 off=4096; mmap_error=-EINVAL;
 assert(dmabuf_fop_mmap(&file,&off,4096,0,&flags,&advice,&uobj,&maxprot)==EINVAL);
 mmap_error=0;
 assert(dmabuf_fop_mmap(&file,&off,4096,0,&flags,&advice,&uobj,&maxprot)==0);
 off=-4096;
 assert(dmabuf_fop_mmap(&file,&off,4096,0,&flags,&advice,&uobj,&maxprot)==EINVAL);
 off=8192;
 assert(dmabuf_fop_mmap(&file,&off,4096,0,&flags,&advice,&uobj,&maxprot)==EINVAL);
 off=0;
 assert(dmabuf_fop_mmap(&file,&off,SIZE_MAX,0,&flags,&advice,&uobj,&maxprot)==EINVAL);
 assert(mmap_calls==2);
 access_modes(&file);
 puts("PASS: PRIME absent/error providers, DMA map cleanup and native mmap errno/range and descriptor access limits");
 return 0;
}
