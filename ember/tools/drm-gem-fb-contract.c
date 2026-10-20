/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual GEM FB/format-fill/slab bodies. Controlled DRM and kmem services. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
static unsigned checks;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "CHECK %d: %s\n", __LINE__, #x); exit(1); } checks++; } while (0)
#define KASSERT CHECK
#define KASSERTMSG(x,...) CHECK(x)
#define EXPORT_SYMBOL(x)
#ifndef __arraycount
#define __arraycount(a) (sizeof(a)/sizeof((a)[0]))
#endif
#ifndef __aligned
#define __aligned(n) __attribute__((aligned(n)))
#endif
#ifndef ALIGNBYTES
#define ALIGNBYTES 15
#endif
#define GFP_KERNEL 0
#define __GFP_ZERO 1
#define ERR_PTR(x) ((void *)(intptr_t)(x))
#define PTR_ERR(x) ((int)(intptr_t)(x))
#define IS_ERR(x) ((uintptr_t)(x) >= (uintptr_t)-4095)
typedef unsigned gfp_t;
static void *allocation;
static size_t allocation_size;
static bool fail_alloc;
static unsigned allocations, frees, cleanups, lookups;
static int fail_init;
static int linux_gfp_to_kmem(gfp_t flags) { return flags; }
static void *
kmem_intr_alloc(size_t size, int flags)
{
	(void)flags;
	CHECK(allocation == NULL);
	if (fail_alloc) return NULL;
	allocation = malloc(size); CHECK(allocation != NULL); allocation_size = size;
	memset(allocation, 0xa5, size); allocations++;
	return allocation;
}
static void *
kmem_intr_zalloc(size_t size, int flags)
{
	void *p = kmem_intr_alloc(size, flags);
	if (p) memset(p, 0, size);
	return p;
}
static void
kmem_intr_free(void *p, size_t size)
{
	CHECK(p == allocation && size == allocation_size);
	free(p); allocation = NULL; allocation_size = 0; frees++;
}
static void kmem_free(void *p, size_t size) { kmem_intr_free(p, size); }
#include "slab-body.h"
struct drm_device { unsigned unused; };
struct drm_file { unsigned unused; };
struct drm_gem_object { uint64_t size; unsigned refs; };
struct drm_format_info { unsigned num_planes, hsub, vsub, cpp[4]; };
struct drm_mode_fb_cmd2 { uint32_t width, height, pixel_format, pitches[4], offsets[4], handles[4], flags; uint64_t modifier[4]; };
struct drm_framebuffer;
struct drm_framebuffer_funcs { void (*destroy)(struct drm_framebuffer *); };
struct drm_framebuffer {
	struct drm_device *dev;
	const struct drm_format_info *format;
	const struct drm_framebuffer_funcs *funcs;
	uint32_t width, height, pitches[4], offsets[4], flags;
	uint64_t modifier;
	struct drm_gem_object *obj[4];
};
static struct drm_gem_object objects[4];
static struct drm_format_info format = {.num_planes=1, .hsub=1, .vsub=1, .cpp={4,4,4,4}};
static const struct drm_format_info *drm_get_format_info(struct drm_device *d, const struct drm_mode_fb_cmd2 *cmd)
{ (void)d; return cmd->pixel_format ? &format : NULL; }
#include "fill-body.h"
static struct drm_gem_object *
drm_gem_object_lookup(struct drm_file *file, unsigned handle)
{
	(void)file; lookups++;
	if (handle == 0 || handle > 4) return NULL;
	objects[handle-1].refs++; return &objects[handle-1];
}
static void
drm_gem_object_put_unlocked(struct drm_gem_object *o)
{
	unsigned i;
	if (!o) return;
	for (i=0; i<4 && o!=&objects[i]; i++) {}
	CHECK(i < 4 && o->refs > 1); o->refs--;
}
static int drm_gem_handle_create(struct drm_file *file, struct drm_gem_object *obj, unsigned *handle)
{ (void)file; CHECK(obj==&objects[0]); *handle=1; return 0; }
static void drm_framebuffer_cleanup(struct drm_framebuffer *fb)
{ CHECK(fb != NULL); cleanups++; }
static int
drm_framebuffer_init(struct drm_device *d, struct drm_framebuffer *fb, const struct drm_framebuffer_funcs *funcs)
{
	unsigned i;
	CHECK(fb->dev == d && fb->format == &format);
	for (i = format.num_planes; i < 4; i++)
		CHECK(fb->obj[i] == NULL);
	fb->funcs = funcs;
	return fail_init;
}
#include "fb-body.h"
static const struct drm_framebuffer_funcs funcs = {.destroy=drm_gem_fb_destroy};
static void
run(unsigned planes, int failure)
{
	struct drm_device d = {0}; struct drm_file file = {0};
	struct drm_mode_fb_cmd2 cmd = {.width=64,.height=64,.pixel_format=1};
	struct drm_framebuffer *fb;
	unsigned i, handle;
	int expected=0;

	CHECK(allocation == NULL); format.num_planes=planes;
	for (i=0;i<4;i++) { objects[i]=(struct drm_gem_object){.size=16384,.refs=1}; cmd.pitches[i]=256; cmd.handles[i]=i+1; }
	fail_alloc=false; fail_init=0; lookups=0;
	if (failure==1) { fail_alloc=true; expected=-ENOMEM; }
	if (failure==2) { cmd.pixel_format=0; expected=-EINVAL; }
	if (failure>=3 && failure<=6) { cmd.handles[(unsigned)failure-3]=0; expected=-ENOENT; }
	if (failure>=7 && failure<=10) { objects[(unsigned)failure-7].size=1; expected=-EINVAL; }
	if (failure==11) { fail_init=-EIO; expected=-EIO; }
	fb=drm_gem_fb_create_with_funcs(&d,&file,&cmd,&funcs);
	if (expected) CHECK(IS_ERR(fb) && PTR_ERR(fb)==expected);
	else {
		CHECK(!IS_ERR(fb) && fb->format==&format);
		for(i=0;i<planes;i++) CHECK(fb->obj[i]==&objects[i] && objects[i].refs==2);
		CHECK(drm_gem_fb_create_handle(fb,&file,&handle)==0 && handle==1);
		fb->funcs->destroy(fb);
	}
	CHECK(allocation==NULL && allocations==frees);
	for(i=0;i<4;i++) CHECK(objects[i].refs==1);
}
int
main(void)
{
	unsigned planes, i;
	const char *only=getenv("GEM_FB_CASE");
	if (only) { run(1,strcmp(only,"failure")==0?2:0); return 0; }
	for(planes=1;planes<=4;planes++) {
		run(planes,0); run(planes,1); run(planes,2); run(planes,11);
		for(i=0;i<planes;i++) { run(planes,3+i); run(planes,7+i); }
	}
	CHECK(cleanups==4);
	printf("PASS actual GEM framebuffer lifetime: %u checks, 36 allocation/lookup/size/init/success cases\n",checks);
	return 0;
}
