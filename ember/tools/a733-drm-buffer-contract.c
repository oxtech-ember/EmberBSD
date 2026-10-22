/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD, AI-assisted. Actual framebuffer and native mmap checks. */
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/mman.h>
static unsigned checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x);abort();} } while (0)
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define IS_ERR(p) ((uintptr_t)(p)>=(uintptr_t)-4095)
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define DRM_FORMAT_XRGB8888 1
#define DRM_FORMAT_MOD_LINEAR 0
#define DRM_MODE_FB_MODIFIERS 2
#define KM_SLEEP 0
#define PAGE_SIZE 4096u
/* Same definition as the DRM <linux/mm.h> header in the kernel build. */
#define PAGE_MASK (~(PAGE_SIZE - 1))
typedef unsigned u_int;
struct drm_device {
	void *dev_private;
};
struct drm_file {
	int unused;
};
struct uvm_object {
	int unused;
};
struct drm_gem_object {
	struct drm_device *dev;
	size_t size;
	unsigned refs;
	struct uvm_object gemo_uvmobj;
};
struct map {
	unsigned dm_nsegs;
	uint64_t dm_mapsize;
	struct {
		uint64_t ds_addr, ds_len;
	}      dm_segs[1];
};
struct drm_gem_cma_object {
	struct drm_gem_object base;
	void *dmat;
	struct map *dmamap;
	uint64_t dmasize;
};
struct drm_framebuffer {
	unsigned live;
};
struct drm_framebuffer_funcs {
	void (*destroy) (struct drm_framebuffer *);
	int (*create_handle) (struct drm_framebuffer *, struct drm_file *, unsigned *);
};
struct a733_drm_fb {
	struct drm_framebuffer base;
	struct drm_gem_cma_object *object;
};
struct a733_drm {
	struct {
		struct {
			uint32_t hdisplay, vdisplay;
		}      mode;
		void *dmat;
	}      native;
};
struct drm_mode_fb_cmd2 {
	uint32_t pixel_format, width, height, handles[4], pitches[4], offsets[4],
	         flags;
	uint64_t modifier[4];
};
static struct drm_gem_cma_object *lookup;
static unsigned lookup_calls, put_calls, allocations, frames;
static int init_error, handle_error;
static void *kmem_zalloc(size_t n, int flag){
	CHECK(flag == KM_SLEEP);
	void *p = calloc(1, n);
	CHECK(p);
	allocations++;
	return p;
}
static void kmem_free(void *p, size_t n){
	CHECK(p && n && allocations);
	allocations--;
	free(p);
}
static struct drm_gem_object *drm_gem_object_lookup(struct drm_file *f, unsigned handle){
	CHECK(f && handle == 37);
	lookup_calls++;
	if (!lookup)
		return NULL;
	lookup->base.refs++;
	return &lookup->base;
}
static struct drm_gem_cma_object *to_drm_gem_cma_obj(struct drm_gem_object *g){
	return container_of(g, struct drm_gem_cma_object, base);
}
static void drm_gem_object_get(struct drm_gem_object *g){
	CHECK(g->refs);
	g->refs++;
}
static void drm_gem_object_put_unlocked(struct drm_gem_object *g){
	CHECK(g->refs);
	g->refs--;
	put_calls++;
}
static int drm_gem_handle_create(struct drm_file *file, struct drm_gem_object *g, unsigned *h){
	CHECK(file && g && h);
	if (!handle_error) {
		g->refs++;
		*h = 38;
	} return handle_error;
}
static void drm_helper_mode_fill_fb_struct(struct drm_device *d, struct drm_framebuffer *fb, const struct drm_mode_fb_cmd2 *c){
	CHECK(d && fb && c->width == 640 && c->height == 480);
}
static int drm_framebuffer_init(struct drm_device *d, struct drm_framebuffer *fb, const struct drm_framebuffer_funcs *f){
	CHECK(d && fb && f && f->destroy && f->create_handle);
	if (!init_error) {
		frames++;
		fb->live = 1;
	} return init_error;
}
static void drm_framebuffer_cleanup(struct drm_framebuffer *fb){
	CHECK(fb->live && frames);
	frames--;
	fb->live = 0;
}
#include "buffer.c"
int
main(void)
{
	struct a733_drm sc = {.native = {{640, 480}, &sc}};
	struct drm_device dev = {&sc}, foreign = {0};
	struct drm_file file = {0};
	const uint64_t bytes = 2560u * 480u;
	for (unsigned bad = 0; bad < 24; bad++) {
		struct map map = {1, bytes, {{0x100000, bytes}}};
		struct drm_gem_cma_object obj = {{&dev, bytes, 1, {0}}, &sc, &map, bytes};
		lookup = &obj;
		struct drm_mode_fb_cmd2 cmd = {.pixel_format = DRM_FORMAT_XRGB8888,.width = 640,.height = 480,.handles = {37},.pitches = {2560}};
		init_error = 0;
		lookup_calls = put_calls = 0;
		int expected = -EINVAL;
		switch (bad) {
		case 0:
			expected = 0;
			break;
		case 1:
			cmd.pixel_format = 2;
			break;
		case 2:
			cmd.width = 639;
			break;
		case 3:
			cmd.height = 479;
			break;
		case 4:
			cmd.offsets[0] = 4;
			break;
		case 5:
			cmd.modifier[0] = 1;
			break;
		case 6:
			cmd.flags = 1;
			break;
		case 7:
			cmd.pitches[0] = 2559;
			break;
		case 8:
			cmd.pitches[0] = 2561;
			break;
		case 9:
			cmd.handles[1] = 2;
			break;
		case 10:
			lookup = NULL;
			expected = -ENOENT;
			break;
		case 11:
			obj.base.dev = &foreign;
			break;
		case 12:
			obj.dmat = &foreign;
			break;
		case 13:
			obj.dmamap = NULL;
			break;
		case 14:
			map.dm_nsegs = 2;
			break;
		case 15:
			obj.base.size--;
			break;
		case 16:
			obj.dmasize--;
			break;
		case 17:
			map.dm_mapsize--;
			break;
		case 18:
			map.dm_segs[0].ds_len--;
			break;
		case 19:
			map.dm_segs[0].ds_addr++;
			break;
		case 20:
			map.dm_segs[0].ds_addr = UINT64_C(0x100000000);
			break;
		case 21:
			map.dm_segs[0].ds_addr = UINT32_MAX - 63;
			break;
		case 22:
			init_error = -ENOMEM;
			expected = -ENOMEM;
			break;
		case 23:
			cmd.flags = DRM_MODE_FB_MODIFIERS;
			expected = 0;
			break;
		}
		struct drm_framebuffer *fb = a733_fb_create(&dev, &file, &cmd);
		if (expected) {
			CHECK(IS_ERR(fb) && PTR_ERR(fb) == expected);
			CHECK(!allocations && !frames && obj.base.refs == 1);
			CHECK(put_calls == (bad >= 11 ? 1u : 0u));
		} else {
			CHECK(!IS_ERR(fb) && fb && allocations == 1 && frames == 1 && obj.base.refs == 2);
			unsigned h = 0;
			handle_error = -ENOMEM;
			CHECK(a733_fb_handle(fb, &file, &h) == -ENOMEM && obj.base.refs == 2);
			handle_error = 0;
			CHECK(a733_fb_handle(fb, &file, &h) == 0 && h == 38 && obj.base.refs == 3);
			drm_gem_object_put_unlocked(&obj.base);
			a733_fb_destroy(fb);
			CHECK(!allocations && !frames && obj.base.refs == 1);
		}
	}
	for (unsigned bad = 0; bad < 15; bad++) {
		struct drm_gem_object obj = {&dev, 16384, 1, {0}};
		off_t off = 4096;
		size_t size = 4096;
		int prot = PROT_READ | PROT_WRITE, flags = MAP_SHARED, advice = 7,
		    maxprot = 99;
		struct uvm_object *uobj = (void *)(uintptr_t) 0x1234;
		bool valid = false;
		switch (bad) {
		case 0:
			valid = true;
			break;
		case 1:
			off = -4096;
			break;
		case 2:
			off = 1;
			break;
		case 3:
			size = 0;
			break;
		case 4:
			size = 4095;
			break;
		case 5:
			off = 16384;
			break;
		case 6:
			off = 12288;
			size = 8192;
			break;
		case 7:
			prot |= PROT_EXEC;
			break;
		case 8:
			flags = MAP_PRIVATE;
			break;
		case 9:
			flags = MAP_SHARED | MAP_PRIVATE;
			break;
		case 10:
			flags = 0;
			break;
		case 11:
			off = 0;
			size = 16384;
			valid = true;
			break;
		case 12:
			off = 12288;
			valid = true;
			break;
		case 13:
			size = SIZE_MAX & ~(size_t) (PAGE_SIZE - 1);
			break;
		case 14:
			off = 0x7ffffffffffff000LL;
			break;
		}
		off_t original = off;
		int original_flags = flags;
		CHECK(a733_prime_mmap(&obj, &off, size, prot, &flags, &advice, &uobj, &maxprot) == (valid ? 0 : -EINVAL));
		CHECK(off == original && flags == original_flags && advice == 7);
		if (valid)
			CHECK(obj.refs == 2 && uobj == &obj.gemo_uvmobj && maxprot == (PROT_READ | PROT_WRITE));
		else
			CHECK(obj.refs == 1 && uobj == (void *)(uintptr_t) 0x1234 && maxprot == 99);
	}
	printf("PASS %u framebuffer and byte-relative mmap checks\n", checks);
	return 0;
}
