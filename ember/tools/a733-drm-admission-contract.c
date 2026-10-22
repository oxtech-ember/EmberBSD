/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD, AI-assisted. Actual mode and allocation controls. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
static unsigned checks, creates, handles, put_calls, probes, probe_calls, frees, locks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); abort(); } } while (0)
#define DRM_MODE_FLAG_PHSYNC 1
#define DRM_MODE_FLAG_NHSYNC 2
#define DRM_MODE_FLAG_PVSYNC 4
#define DRM_MODE_FLAG_NVSYNC 8
#define PAGE_SIZE 4096
/* Same definition as the DRM <linux/mm.h> header in the kernel build. */
#define PAGE_MASK (~(PAGE_SIZE - 1))
#define VMEM_ADDR_MIN 0
#define VMEM_ADDR_MAX UINT64_MAX
#define VM_BESTFIT 1
#define VM_NOSLEEP 2
typedef uint64_t vmem_addr_t;
typedef struct vmem { int id; } vmem_t;
typedef struct { int held; } kmutex_t;
#define ERR_PTR(x) ((void *)(intptr_t)(x))
#define PTR_ERR(x) ((int)(intptr_t)(x))
#define IS_ERR(x) ((uintptr_t)(x) >= (uintptr_t)-4095)
struct sun60i_a733_output_mode {
	uint32_t pixel_clock_hz, hdisplay, hsync_start, hsync_end, htotal,
	         vdisplay, vsync_start, vsync_end, vtotal;
	bool hsync_positive, vsync_positive;
};
struct drm_display_mode {
	int clock, hdisplay, hsync_start, hsync_end, htotal, vdisplay, vsync_start,
	    vsync_end, vtotal, hskew, vscan;
	uint32_t flags;
};
struct drm_gem_object {
	unsigned refs;
};
struct drm_gem_cma_object {
	struct drm_gem_object base;
};
struct drm_file {
	int id;
};
struct drm_device {
	int id;
	void *dev_private;
	vmem_t *cma_pool;
};
struct a733_drm {
	kmutex_t pool_lock;
	vmem_t *pool;
};
static vmem_t pool;
static int probe_error;
static vmem_addr_t probed;
static uint64_t probed_size;
static void mutex_enter(kmutex_t *m){
	CHECK(!m->held);
	m->held = 1;
	locks++;
}
static void mutex_exit(kmutex_t *m){
	CHECK(m->held);
	m->held = 0;
}
static kmutex_t *pool_lock;
static int vmem_xalloc(vmem_t *vm, uint64_t size, uint64_t align, uint64_t phase, uint64_t nocross, vmem_addr_t min, vmem_addr_t max, int flags, vmem_addr_t *out){
	CHECK(vm == &pool && pool_lock->held && size && size % 4096 == 0 && align == PAGE_SIZE && !phase && !nocross && min == VMEM_ADDR_MIN && max == VMEM_ADDR_MAX && flags == (VM_BESTFIT | VM_NOSLEEP));
	probe_calls++;
	if (probe_error)
		return probe_error;
	probes++;
	probed = 0x40000000 + probes * 4096;
	probed_size = size;
	*out = probed;
	return 0;
}
static void vmem_xfree(vmem_t *vm, vmem_addr_t addr, uint64_t size){
	CHECK(vm == &pool && pool_lock->held && addr == probed && size == probed_size);
	frees++;
}
struct drm_mode_create_dumb {
	uint32_t height, width, bpp, flags, handle, pitch;
	uint64_t size;
};
static struct drm_gem_cma_object object;
static size_t allocated;
static int create_error, handle_error;
static struct drm_gem_cma_object *drm_gem_cma_create(struct drm_device *dev, size_t size){
	CHECK(dev && pool_lock->held && frees == probes && size == probed_size);
	creates++;
	allocated = size;
	if (create_error)
		return NULL;
	object.base.refs = 1;
	return &object;
}
static int drm_gem_handle_create(struct drm_file *file, struct drm_gem_object *obj, uint32_t * handle){
	CHECK(file && obj == &object.base && obj->refs == 1);
	handles++;
	if (handle_error)
		return -handle_error;
	*handle = 77;
	obj->refs++;
	return 0;
}
static void drm_gem_object_put_unlocked(struct drm_gem_object *obj){
	CHECK(obj == &object.base && obj->refs);
	obj->refs--;
	put_calls++;
}
#include "admission.c"

int
main(void)
{
	struct sun60i_a733_output_mode n = {27000000, 720, 732, 796, 864, 576, 581, 586, 625, false, false};
	struct drm_display_mode m = {27000, 720, 732, 796, 864, 576, 581, 586, 625, 0, 0, 10}, bad;
	CHECK(a733_drm_mode_matches(&n, &m));

	bad = m;
	bad.clock++;
	CHECK(!a733_drm_mode_matches(&n, &bad));
	bad = m;
	bad.hdisplay++;
	CHECK(!a733_drm_mode_matches(&n, &bad));
	bad = m;
	bad.hsync_start++;
	CHECK(!a733_drm_mode_matches(&n, &bad));
	bad = m;
	bad.hsync_end++;
	CHECK(!a733_drm_mode_matches(&n, &bad));
	bad = m;
	bad.htotal++;
	CHECK(!a733_drm_mode_matches(&n, &bad));
	bad = m;
	bad.vdisplay++;
	CHECK(!a733_drm_mode_matches(&n, &bad));
	bad = m;
	bad.vsync_start++;
	CHECK(!a733_drm_mode_matches(&n, &bad));
	bad = m;
	bad.vsync_end++;
	CHECK(!a733_drm_mode_matches(&n, &bad));
	bad = m;
	bad.vtotal++;
	CHECK(!a733_drm_mode_matches(&n, &bad));
	bad = m;
	bad.hskew++;
	CHECK(!a733_drm_mode_matches(&n, &bad));
	bad = m;
	bad.vscan++;
	CHECK(!a733_drm_mode_matches(&n, &bad));
	for (unsigned flags = 0; flags < 32; flags++) {
		bad = m;
		bad.flags = flags;
		CHECK(a733_drm_mode_matches(&n, &bad) == (flags == 10));
	}
	n.pixel_clock_hz++;
	CHECK(!a733_drm_mode_matches(&n, &m));
	n.pixel_clock_hz--;
	bad = m;
	bad.clock = -27000;
	CHECK(!a733_drm_mode_matches(&n, &bad));
	struct drm_file file = {1};
	struct a733_drm sc = {.pool = &pool};
	struct drm_device dev = {1, &sc, &pool};
	pool_lock = &sc.pool_lock;
	for (unsigned width = 1; width <= 1920; width++)
		for (unsigned height = 1; height <= 1080; height += 1079) {
			struct drm_mode_create_dumb a = {.width = width,.height = height,.bpp = 32};
			CHECK(a733_dumb_create(&file, &dev, &a) == 0);
			CHECK(a.pitch == ((width + 15) / 16) * 64 && a.handle == 77);
			CHECK(a.size == ((uint64_t) a.pitch * height + 4095) / 4096 * 4096 && allocated == a.size);
			CHECK(object.base.refs == 1);
			object.base.refs = 0;
		}
	static const struct { uint32_t width, height; uint64_t size; } sizes[] = {
		{1920, 1080, 8294400}, {1280, 1024, 5242880}, {64, 64, 16384},
		{1, 1, 4096},
	};
	for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
		struct drm_mode_create_dumb d = {.width = sizes[i].width,
		    .height = sizes[i].height, .bpp = 32};
		CHECK(a733_dumb_create(&file, &dev, &d) == 0);
		CHECK(d.size == sizes[i].size && allocated == sizes[i].size);
		object.base.refs = 0;
	}
	struct drm_mode_create_dumb a = {.width = 720,.height = 576,.bpp = 32};
	unsigned before = creates;
	const unsigned invalid[] = {0, 1921, UINT32_MAX};
	for (unsigned i = 0; i < 3; i++) {
		a.width = invalid[i];
		CHECK(a733_dumb_create(&file, &dev, &a) == -EINVAL);
	}
	a.width = 720;
	a.height = 0;
	CHECK(a733_dumb_create(&file, &dev, &a) == -EINVAL);
	a.height = 1081;
	CHECK(a733_dumb_create(&file, &dev, &a) == -EINVAL);
	a.height = 576;
	a.bpp = 16;
	CHECK(a733_dumb_create(&file, &dev, &a) == -EINVAL);
	a.bpp = 32;
	a.flags = 1;
	CHECK(a733_dumb_create(&file, &dev, &a) == -EINVAL);
	a.flags = 0;
	CHECK(creates == before);
	unsigned oldputs = put_calls, oldhandles = handles;
	handle_error = ENOMEM;
	CHECK(a733_dumb_create(&file, &dev, &a) == -ENOMEM);
	CHECK(put_calls == oldputs + 1 && handles == oldhandles + 1 && object.base.refs == 0);
	handle_error = 0;
	create_error = ENOMEM;
	oldputs = put_calls;
	oldhandles = handles;
	CHECK(a733_dumb_create(&file, &dev, &a) == -ENOMEM);
	CHECK(put_calls == oldputs && handles == oldhandles && !sc.pool_lock.held);
	create_error = 0;
	/* A pool without a proven fit never reaches the blocking helper path. */
	before = creates;
	unsigned oldprobes = probe_calls;
	probe_error = ENOMEM;
	CHECK(a733_dumb_create(&file, &dev, &a) == -ENOMEM);
	CHECK(creates == before && probe_calls == oldprobes + 1 && !sc.pool_lock.held);
	probe_error = 0;
	dev.cma_pool = NULL;
	CHECK(a733_dumb_create(&file, &dev, &a) == -ENOMEM);
	CHECK(creates == before && probe_calls == oldprobes + 1 && !sc.pool_lock.held);
	dev.cma_pool = &pool;
	sc.pool = NULL;
	CHECK(a733_dumb_create(&file, &dev, &a) == -ENOMEM);
	CHECK(creates == before && probe_calls == oldprobes + 1 && !sc.pool_lock.held);
	sc.pool = &pool;
	CHECK(a733_dumb_create(&file, &dev, &a) == 0 && creates == before + 1 && frees == probes && !sc.pool_lock.held);
	object.base.refs = 0;
	printf("PASS %u exact mode and allocation checks\n", checks);
	return 0;
}
