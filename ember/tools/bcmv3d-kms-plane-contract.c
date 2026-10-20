/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Execute actual KMS framebuffer/plane functions with controlled services. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static unsigned int checks, cases;
#define CHECK(x) do { checks++; if (!(x)) { \
 fprintf(stderr, "CHECK %d: %s\n", __LINE__, #x); exit(1); } } while (0)
#define container_of(p,t,m) ((t *)((char *)(p) - offsetof(t,m)))
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define GFP_KERNEL 0
#define DRM_FORMAT_MOD_INVALID UINT64_C(0x00ffffffffffffff)
#define DRM_PLANE_TYPE_PRIMARY 1
#ifndef __arraycount
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#endif
#define BUILD_BUG_ON(x) _Static_assert(!(x), "build condition")
#define WARN_ON(x) (x)
#define round_up(x, a) (((x) + (a) - 1) / (a) * (a))
typedef uint32_t u32;
typedef uint32_t __u32;
typedef uint64_t __u64;
#define DRM_FORMAT_XRGB8888 UINT32_C(0x34325258)
#define DRM_FORMAT_MOD_LINEAR UINT64_C(0)
#define DRM_MODE_FB_MODIFIERS 2U
#define DRM_MODE_FLAG_PHSYNC 1U
#define DRM_MODE_FLAG_PVSYNC 4U
#define DRM_MODE_TYPE_DRIVER 64U
#define DRM_MODE_TYPE_PREFERRED 8U
#define DRM_MODE_ROTATE_0 1U
#define DRM_PLANE_HELPER_NO_SCALING 65536

struct drm_device;
struct drm_file;
struct drm_plane;
struct drm_framebuffer;
struct drm_framebuffer_funcs {
	void (*destroy)(struct drm_framebuffer *);
	int (*create_handle)(struct drm_framebuffer *, struct drm_file *, unsigned int *);
};
struct drm_format_info { uint32_t format; };
struct drm_gem_object { unsigned int refs; uint64_t size; bool native; };
struct bcmv3d_bo { struct { struct drm_gem_object base; } cma; };
struct drm_framebuffer {
	struct drm_gem_object *obj[4];
	const struct drm_format_info *format;
	struct drm_format_info storage;
	const struct drm_framebuffer_funcs *funcs;
	uint64_t modifier;
	unsigned int refs;
};
struct drm_mode_fb_cmd2 {
	uint32_t width, height, pixel_format, flags;
	uint32_t handles[4], pitches[4], offsets[4];
	uint64_t modifier[4];
};
struct drm_display_mode {
	int clock, hdisplay, hsync_start, hsync_end, htotal, hskew;
	int vdisplay, vsync_start, vsync_end, vtotal, vscan;
	uint32_t flags, type;
};
struct dma_fence { unsigned int refs; bool signalled; };
struct drm_crtc { unsigned int identity; };
struct drm_crtc_state { bool active; };
struct drm_atomic_state { struct drm_crtc_state *crtc; };
struct drm_plane_state {
	struct drm_plane *plane;
	struct drm_framebuffer *fb;
	struct drm_crtc *crtc;
	struct drm_atomic_state *state;
	struct dma_fence *fence;
	bool visible;
	uint32_t rotation;
	int crtc_x, crtc_y;
	uint32_t crtc_w, crtc_h, src_x, src_y, src_w, src_h;
};
struct drm_modeset_acquire_ctx;
struct drm_plane_funcs {
	bool (*format_mod_supported)(struct drm_plane *, uint32_t, uint64_t);
	int (*update_plane)(struct drm_plane *, struct drm_crtc *, struct drm_framebuffer *,
	    int, int, unsigned int, unsigned int, uint32_t, uint32_t, uint32_t,
	    uint32_t, struct drm_modeset_acquire_ctx *);
	int (*disable_plane)(struct drm_plane *, struct drm_modeset_acquire_ctx *);
	void (*destroy)(struct drm_plane *);
	void (*reset)(struct drm_plane *);
	struct drm_plane_state *(*atomic_duplicate_state)(struct drm_plane *);
	void (*atomic_destroy_state)(struct drm_plane *, struct drm_plane_state *);
};
struct drm_mode_object { uint32_t id; };
struct drm_property { unsigned int identity; };
struct drm_property_blob {
	struct drm_mode_object base;
	void *data;
	size_t length;
};
struct drm_mode_config { struct drm_property *modifiers_property; };
struct drm_plane {
	struct drm_device *dev;
	struct drm_plane_state *state;
	struct drm_mode_object base;
	const struct drm_plane_funcs *funcs;
	const uint32_t *format_types;
	const uint64_t *modifiers;
	unsigned int format_count, modifier_count;
};
struct bcmv3d_scanout_frame { unsigned int refs; struct drm_gem_object *obj; };
struct bcmv3d_scanout { unsigned int identity; };
struct bcmv3d_kms { int availability, fault; struct drm_plane plane; };
struct bcmv3d_device { struct bcmv3d_scanout *scanout; struct bcmv3d_kms *kms; };
struct drm_device { struct bcmv3d_device *dev_private; struct drm_mode_config mode_config; };
struct drm_file { struct drm_gem_object *handles[16]; };

static struct bcmv3d_bo native_bo, foreign_bo;
static struct bcmv3d_scanout owner;
static struct bcmv3d_kms kms;
static struct bcmv3d_device sc;
static struct drm_device dev;
static struct drm_file file;
static struct drm_plane plane;
static struct drm_crtc crtc;
static struct drm_crtc_state crtc_state;
static struct drm_atomic_state atomic_state;
static struct dma_fence fence;
static struct bcmv3d_scanout_frame frames[8];
static void *allocations[64];
static unsigned int live, frame_count, reserves, frame_puts, native_checks;
static unsigned int helper_creates, helper_destroys, state_destroys;
static unsigned int fence_gets, fence_puts, availability_calls, faults, plane_checks;
static int create_error, reserve_error, put_error, helper_result;
static bool allocation_fail, helper_visible;

static void *kzalloc(size_t, int);
static void kfree(void *);
static void drm_gem_object_put_unlocked(struct drm_gem_object *);
static void drm_gem_fb_destroy(struct drm_framebuffer *);
static int drm_gem_fb_create_handle(struct drm_framebuffer *, struct drm_file *, unsigned int *);
static struct drm_framebuffer *drm_gem_fb_create_with_funcs(struct drm_device *,
    struct drm_file *, const struct drm_mode_fb_cmd2 *, const struct drm_framebuffer_funcs *);
static void drm_framebuffer_put(struct drm_framebuffer *);
static int bcmv3d_bo_reference(struct bcmv3d_device *, struct drm_gem_object *, struct bcmv3d_bo **);
static struct dma_fence *dma_fence_get(struct dma_fence *);
static void dma_fence_put(struct dma_fence *);
static void __drm_atomic_helper_plane_reset(struct drm_plane *, struct drm_plane_state *);
static void __drm_atomic_helper_plane_duplicate_state(struct drm_plane *, struct drm_plane_state *);
static void __drm_atomic_helper_plane_destroy_state(struct drm_plane_state *);
static struct drm_crtc_state *drm_atomic_get_new_crtc_state(struct drm_atomic_state *, struct drm_crtc *);
static int drm_atomic_helper_check_plane_state(struct drm_plane_state *, struct drm_crtc_state *,
    int, int, bool, bool);
static int kms_available(struct bcmv3d_kms *);
static void kms_fault(struct bcmv3d_kms *, int);
static int bcmv3d_scanout_reserve(struct bcmv3d_scanout *, struct drm_gem_object *,
    struct bcmv3d_scanout_frame **);
static int bcmv3d_scanout_put(struct bcmv3d_scanout_frame *);
/* A substituted blocking API must fail a CHECK, not silently act as reserve. */
int bcmv3d_scanout_prepare(struct bcmv3d_scanout *, struct drm_gem_object *,
    struct bcmv3d_scanout_frame **);
int bcmv3d_scanout_finalize(struct bcmv3d_scanout_frame *, long);

static struct drm_property modifiers_property;
static struct drm_property_blob format_blob;
static unsigned int blob_creates, blob_attaches;
static struct drm_property_blob *drm_property_create_blob(struct drm_device *, size_t, const void *);
static void drm_object_attach_property(struct drm_mode_object *, struct drm_property *, uint64_t);
static int drm_atomic_helper_update_plane(struct drm_plane *, struct drm_crtc *,
    struct drm_framebuffer *, int, int, unsigned int, unsigned int, uint32_t,
    uint32_t, uint32_t, uint32_t, struct drm_modeset_acquire_ctx *);
static int drm_atomic_helper_disable_plane(struct drm_plane *, struct drm_modeset_acquire_ctx *);
static void drm_plane_cleanup(struct drm_plane *);
static int drm_universal_plane_init(struct drm_device *, struct drm_plane *, uint32_t,
    const struct drm_plane_funcs *, const uint32_t *, unsigned int, const uint64_t *,
    int, const char *, ...);
const struct drm_plane_funcs *unwired_plane_funcs(void);

#include "kms-plane-body.h"

static struct bcmv3d_kms_plane_state *cleanup_watch;

static void *
kzalloc(size_t size, int flags)
{
	unsigned int i;
	void *memory;

	CHECK(flags == GFP_KERNEL);
	if (allocation_fail)
		return NULL;
	memory = calloc(1, size);
	CHECK(memory != NULL);
	for (i = 0; i < 64; i++) {
		if (allocations[i] == NULL) {
			allocations[i] = memory;
			live++;
			return memory;
		}
	}
	CHECK(false);
	return NULL;
}

static void
kfree(void *memory)
{
	unsigned int i;

	CHECK(memory != NULL);
	for (i = 0; i < 64; i++) {
		if (allocations[i] == memory) {
			allocations[i] = NULL;
			CHECK(live != 0);
			live--;
			free(memory);
			return;
		}
	}
	CHECK(false);
}

static void
object_get(struct drm_gem_object *object)
{
	CHECK(object != NULL && object->refs != 0);
	object->refs++;
}

static void
drm_gem_object_put_unlocked(struct drm_gem_object *object)
{
	CHECK(object != NULL && object->refs != 0);
	object->refs--;
}

static void
drm_gem_fb_destroy(struct drm_framebuffer *fb)
{
	CHECK(fb->refs == 0 && fb->obj[0] != NULL);
	helper_destroys++;
	drm_gem_object_put_unlocked(fb->obj[0]);
	kfree(fb);
}

static int
drm_gem_fb_create_handle(struct drm_framebuffer *fb, struct drm_file *client,
    unsigned int *handle)
{
	CHECK(fb->obj[0] != NULL && client == &file && handle != NULL);
	*handle = 7;
	return 0;
}

static struct drm_framebuffer *
drm_gem_fb_create_with_funcs(struct drm_device *device, struct drm_file *client,
    const struct drm_mode_fb_cmd2 *cmd, const struct drm_framebuffer_funcs *funcs)
{
	struct drm_framebuffer *fb;
	struct drm_gem_object *object;

	CHECK(device == &dev && client == &file);
	helper_creates++;
	if (create_error != 0)
		return ERR_PTR(create_error);
	if (cmd->handles[0] >= 16 || client->handles[cmd->handles[0]] == NULL)
		return ERR_PTR(-ENOENT);
	object = client->handles[cmd->handles[0]];
	/* Model the generic GEM helper's handle lookup and storage bound only. */
	if (object->size < (uint64_t)cmd->pitches[0] * cmd->height + cmd->offsets[0])
		return ERR_PTR(-EINVAL);
	fb = kzalloc(sizeof(*fb), GFP_KERNEL);
	CHECK(fb != NULL);
	object_get(object);
	fb->obj[0] = object;
	fb->refs = 1;
	fb->storage.format = cmd->pixel_format;
	fb->format = &fb->storage;
	fb->modifier = cmd->modifier[0];
	fb->funcs = funcs;
	return fb;
}

static void
drm_framebuffer_put(struct drm_framebuffer *fb)
{
	CHECK(fb != NULL && fb->refs != 0);
	if (--fb->refs == 0) {
		CHECK(fb->funcs != NULL && fb->funcs->destroy == drm_gem_fb_destroy);
		fb->funcs->destroy(fb);
	}
}

static int
bcmv3d_bo_reference(struct bcmv3d_device *native, struct drm_gem_object *object,
    struct bcmv3d_bo **out)
{
	native_checks++;
	if (native != &sc || object == NULL || !object->native)
		return ENXIO;
	CHECK(object == &native_bo.cma.base);
	object_get(object);
	*out = &native_bo;
	return 0;
}

static struct dma_fence *
dma_fence_get(struct dma_fence *f)
{
	fence_gets++;
	if (f != NULL) {
		CHECK(f == &fence && f->refs != 0);
		f->refs++;
	}
	return f;
}

static void
dma_fence_put(struct dma_fence *f)
{
	if (f != NULL) {
		CHECK(f == &fence && f->refs != 0);
		f->refs--;
		fence_puts++;
	}
}

static void
__drm_atomic_helper_plane_reset(struct drm_plane *p, struct drm_plane_state *state)
{
	CHECK(p == &plane && state != NULL);
	state->plane = p;
	state->rotation = DRM_MODE_ROTATE_0;
	p->state = state;
}

static void
__drm_atomic_helper_plane_duplicate_state(struct drm_plane *p,
    struct drm_plane_state *state)
{
	CHECK(p == &plane && p->state != NULL && state != p->state);
	memcpy(state, p->state, sizeof(*state));
	if (state->fb != NULL) {
		CHECK(state->fb->refs != 0);
		state->fb->refs++;
	}
	/* Match the real generic helper: an explicit fence is not duplicated. */
	state->fence = NULL;
}

static void
__drm_atomic_helper_plane_destroy_state(struct drm_plane_state *state)
{
	state_destroys++;
	if (state->fb != NULL)
		drm_framebuffer_put(state->fb);
	if (state->fence != NULL)
		dma_fence_put(state->fence);
}

static struct drm_crtc_state *
drm_atomic_get_new_crtc_state(struct drm_atomic_state *state, struct drm_crtc *c)
{
	CHECK(state == &atomic_state && c == &crtc);
	return state->crtc;
}

static int
drm_atomic_helper_check_plane_state(struct drm_plane_state *state,
    struct drm_crtc_state *c, int min, int max, bool position, bool disabled)
{
	CHECK(state != NULL && c == &crtc_state);
	CHECK(min == DRM_PLANE_HELPER_NO_SCALING && max == min);
	CHECK(!position && disabled);
	plane_checks++;
	state->visible = helper_visible;
	return helper_result;
}

static int
kms_available(struct bcmv3d_kms *k)
{
	CHECK(k == &kms);
	availability_calls++;
	return k->availability;
}

static void
kms_fault(struct bcmv3d_kms *k, int error)
{
	CHECK(k == &kms && error == put_error && error > 0);
	CHECK(cleanup_watch != NULL && cleanup_watch->frame == NULL &&
	    cleanup_watch->explicit == NULL);
	faults++;
	if (k->fault == 0)
		k->fault = error;
}

static int
bcmv3d_scanout_reserve(struct bcmv3d_scanout *scanout, struct drm_gem_object *object,
    struct bcmv3d_scanout_frame **out)
{
	struct bcmv3d_scanout_frame *frame;

	CHECK(scanout == &owner && object == &native_bo.cma.base && *out == NULL);
	reserves++;
	if (reserve_error != 0)
		return reserve_error;
	CHECK(frame_count < 8);
	frame = &frames[frame_count++];
	CHECK(frame->refs == 0);
	frame->refs = 1;
	frame->obj = object;
	object_get(object);
	*out = frame;
	return 0;
}

static int
bcmv3d_scanout_put(struct bcmv3d_scanout_frame *frame)
{
	CHECK(frame != NULL && frame->refs == 1);
	frame_puts++;
	frame->refs = 0;
	drm_gem_object_put_unlocked(frame->obj);
	return put_error;
}

int
bcmv3d_scanout_prepare(struct bcmv3d_scanout *scanout, struct drm_gem_object *object,
    struct bcmv3d_scanout_frame **frame)
{
	(void)scanout; (void)object; (void)frame;
	CHECK(false); /* prepare_fb must never wait for an unsignalled writer. */
	return EIO;
}

int
bcmv3d_scanout_finalize(struct bcmv3d_scanout_frame *frame, long ticks)
{
	(void)frame; (void)ticks;
	CHECK(false);
	return EIO;
}

static struct drm_mode_fb_cmd2
valid_command(void)
{
	return (struct drm_mode_fb_cmd2) { .width = 1920, .height = 1080,
	    .pixel_format = DRM_FORMAT_XRGB8888, .handles = { 7, 0, 0, 0 },
	    .pitches = { 7680, 0, 0, 0 } };
}

static void
fixture_begin(void)
{
	CHECK(live == 0);
	native_bo.cma.base = (struct drm_gem_object) { 1, 8294400, true };
	foreign_bo.cma.base = (struct drm_gem_object) { 1, 8294400, false };
	memset(&file, 0, sizeof(file));
	file.handles[7] = &native_bo.cma.base;
	file.handles[8] = &foreign_bo.cma.base;
	memset(&kms, 0, sizeof(kms));
	memset(frames, 0, sizeof(frames));
	sc = (struct bcmv3d_device) { &owner, &kms };
	dev.dev_private = &sc;
	plane = (struct drm_plane) { .dev = &dev };
	atomic_state.crtc = &crtc_state;
	crtc_state.active = true;
	fence = (struct dma_fence) { 0, false };
	frame_count = reserves = frame_puts = native_checks = 0;
	helper_creates = helper_destroys = state_destroys = 0;
	fence_gets = fence_puts = availability_calls = faults = plane_checks = 0;
	create_error = reserve_error = put_error = helper_result = 0;
	allocation_fail = false;
	helper_visible = true;
	cleanup_watch = NULL;
}

static void
fixture_end(void)
{
	unsigned int i;

	CHECK(live == 0 && fence.refs == 0);
	CHECK(native_bo.cma.base.refs == 1 && foreign_bo.cma.base.refs == 1);
	for (i = 0; i < frame_count; i++)
		CHECK(frames[i].refs == 0);
	drm_gem_object_put_unlocked(&native_bo.cma.base);
	drm_gem_object_put_unlocked(&foreign_bo.cma.base);
	cases++;
}

static struct bcmv3d_kms_plane_state *
active_plane(bool explicit)
{
	struct drm_mode_fb_cmd2 cmd = valid_command();
	struct bcmv3d_kms_plane_state *state;

	kms_plane_reset(&plane);
	CHECK(plane.state != NULL);
	state = kms_plane_state(plane.state);
	state->base.fb = kms_fb_create(&dev, &file, &cmd);
	CHECK(!IS_ERR(state->base.fb));
	state->base.crtc = &crtc;
	state->base.state = &atomic_state;
	state->base.crtc_w = 1920;
	state->base.crtc_h = 1080;
	state->base.src_w = 1920U << 16;
	state->base.src_h = 1080U << 16;
	if (explicit) {
		fence.refs = 1;
		state->base.fence = &fence;
	}
	return state;
}

static void
destroy_plane(void)
{
	struct drm_plane_state *state = plane.state;

	CHECK(state != NULL);
	cleanup_watch = kms_plane_state(state);
	kms_plane_destroy_state(&plane, state);
	plane.state = NULL;
	cleanup_watch = NULL;
}

/* Other plane methods are retained only as actual table entries. */
static int
drm_atomic_helper_update_plane(struct drm_plane *p, struct drm_crtc *c,
    struct drm_framebuffer *fb, int x, int y, unsigned int w, unsigned int h,
    uint32_t sx, uint32_t sy, uint32_t sw, uint32_t sh,
    struct drm_modeset_acquire_ctx *ctx)
{
	(void)p; (void)c; (void)fb; (void)x; (void)y; (void)w; (void)h;
	(void)sx; (void)sy; (void)sw; (void)sh; (void)ctx;
	CHECK(false);
	return -EIO;
}

static int
drm_atomic_helper_disable_plane(struct drm_plane *p,
    struct drm_modeset_acquire_ctx *ctx)
{
	(void)p; (void)ctx;
	CHECK(false);
	return -EIO;
}

static void
drm_plane_cleanup(struct drm_plane *p)
{
	(void)p;
	CHECK(false);
}

const struct drm_plane_funcs *
unwired_plane_funcs(void)
{
	static struct drm_plane_funcs funcs;

	funcs = kms_plane_funcs;
	funcs.format_mod_supported = NULL;
	return &funcs;
}

static struct drm_property_blob *
drm_property_create_blob(struct drm_device *d, size_t size, const void *data)
{
	CHECK(d == &dev && data == NULL && format_blob.data == NULL);
	blob_creates++;
	format_blob.data = kzalloc(size, GFP_KERNEL);
	if (format_blob.data == NULL)
		return ERR_PTR(-ENOMEM);
	format_blob.length = size;
	format_blob.base.id = 23;
	return &format_blob;
}

static void
drm_object_attach_property(struct drm_mode_object *object,
    struct drm_property *property, uint64_t value)
{
	CHECK(object == &kms.plane.base && property == &modifiers_property);
	CHECK(value == format_blob.base.id && format_blob.data != NULL);
	blob_attaches++;
}

static int
drm_universal_plane_init(struct drm_device *d, struct drm_plane *p,
    uint32_t possible_crtcs, const struct drm_plane_funcs *funcs,
    const uint32_t *formats, unsigned int count, const uint64_t *modifiers,
    int type, const char *name, ...)
{
	unsigned int n = 0;

	CHECK(d == &dev && p == &kms.plane && possible_crtcs == 1);
	CHECK(type == DRM_PLANE_TYPE_PRIMARY && strcmp(name, "HVS0") == 0);
	CHECK(count <= 64 && formats != NULL && modifiers != NULL);
	while (modifiers[n] != DRM_FORMAT_MOD_INVALID) {
		CHECK(n < 8);
		n++;
	}
	p->dev = d;
	p->funcs = funcs;
	p->format_types = formats;
	p->format_count = count;
	p->modifiers = modifiers;
	p->modifier_count = n;
	return 0;
}

static void
check_format_blob(void)
{
	static const uint32_t rejected_formats[] = {
		0, UINT32_C(0x34325241), UINT32_C(0x34324258), UINT32_MAX
	};
	static const uint64_t rejected_modifiers[] = {
		1, UINT64_C(0x0700000000000006), DRM_FORMAT_MOD_INVALID, UINT64_MAX
	};
	struct drm_format_modifier_blob header;
	struct drm_format_modifier modifier;
	uint32_t format;
	unsigned int i, j, pairs = 0;

	fixture_begin();
	CHECK(kms_plane_format_mod_supported(&kms.plane,
	    DRM_FORMAT_XRGB8888, DRM_FORMAT_MOD_LINEAR));
	for (i = 0; i < __arraycount(rejected_formats); i++)
		CHECK(!kms_plane_format_mod_supported(&kms.plane,
		    rejected_formats[i], DRM_FORMAT_MOD_LINEAR));
	for (i = 0; i < __arraycount(rejected_modifiers); i++)
		CHECK(!kms_plane_format_mod_supported(&kms.plane,
		    DRM_FORMAT_XRGB8888, rejected_modifiers[i]));
	dev.mode_config.modifiers_property = &modifiers_property;
	CHECK(actual_plane_registration(&dev, &kms) == 0);
	CHECK(create_in_format_blob(&dev, &kms.plane) == 0);
	CHECK(blob_creates == 1 && blob_attaches == 1 && format_blob.data != NULL);
	CHECK(format_blob.length == sizeof(header) + 8 + sizeof(modifier));
	memcpy(&header, format_blob.data, sizeof(header));
	CHECK(header.version == FORMAT_BLOB_CURRENT && header.flags == 0);
	CHECK(header.count_formats == 1 && header.count_modifiers == 1);
	CHECK(header.formats_offset == sizeof(header));
	CHECK(header.modifiers_offset == sizeof(header) + 8);
	memcpy(&format, (char *)format_blob.data + header.formats_offset, sizeof(format));
	memcpy(&modifier, (char *)format_blob.data + header.modifiers_offset, sizeof(modifier));
	CHECK(format == DRM_FORMAT_XRGB8888);
	/* This is the hardware-observed failure: NULL callback emitted mask 0. */
	CHECK(modifier.formats == UINT64_C(1));
	CHECK(modifier.modifier == DRM_FORMAT_MOD_LINEAR);
	CHECK(modifier.offset == 0 && modifier.pad == 0);
	/* Enumerate client-visible pairs through the exported UAPI bit mask. */
	for (i = 0; i < header.count_modifiers; i++) {
		memcpy(&modifier, (char *)format_blob.data +
		    header.modifiers_offset + i * sizeof(modifier), sizeof(modifier));
		for (j = 0; j < 64; j++) {
			if ((modifier.formats & (UINT64_C(1) << j)) == 0)
				continue;
			CHECK(modifier.offset + j < header.count_formats);
			memcpy(&format, (char *)format_blob.data +
			    header.formats_offset + (modifier.offset + j) * sizeof(format),
			    sizeof(format));
			CHECK(format == DRM_FORMAT_XRGB8888 &&
			    modifier.modifier == DRM_FORMAT_MOD_LINEAR);
			pairs++;
		}
	}
	CHECK(pairs == 1 && kms.plane.funcs == &kms_plane_funcs);
	kfree(format_blob.data);
	format_blob.data = NULL;
	allocation_fail = true;
	CHECK(create_in_format_blob(&dev, &kms.plane) == -1);
	CHECK(blob_creates == 2 && blob_attaches == 1 && format_blob.data == NULL);
	fixture_end();
}

static void
check_modes(void)
{
	struct drm_display_mode good = { .clock = 148500, .hdisplay = 1920,
	    .hsync_start = 2008, .hsync_end = 2052, .htotal = 2200,
	    .vdisplay = 1080, .vsync_start = 1084, .vsync_end = 1089, .vtotal = 1125,
	    .flags = DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC };
	struct drm_display_mode mode = good;
	int *fields[] = { &mode.clock, &mode.hdisplay, &mode.hsync_start,
	    &mode.hsync_end, &mode.htotal, &mode.hskew, &mode.vdisplay,
	    &mode.vsync_start, &mode.vsync_end, &mode.vtotal, &mode.vscan };
	unsigned int i;

	CHECK(kms_mode_valid(&good));
	for (i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
		mode = good;
		(*fields[i])++;
		CHECK(!kms_mode_valid(&mode));
	}
	mode = good;
	mode.flags |= 16;
	CHECK(!kms_mode_valid(&mode));
	mode = good;
	mode.type = UINT32_MAX;
	CHECK(kms_mode_valid(&mode));
	cases++;
}

static void
check_framebuffers(void)
{
	struct drm_mode_fb_cmd2 cmd;
	struct drm_framebuffer *fb;
	unsigned int i, j;

	for (i = 0; i < 2; i++) {
		fixture_begin();
		cmd = valid_command();
		cmd.flags = i == 0 ? 0 : DRM_MODE_FB_MODIFIERS;
		fb = kms_fb_create(&dev, &file, &cmd);
		CHECK(!IS_ERR(fb) && fb->refs == 1 && native_checks == 1);
		CHECK(fb->funcs->create_handle == drm_gem_fb_create_handle);
		CHECK(native_bo.cma.base.refs == 2);
		drm_framebuffer_put(fb);
		CHECK(helper_destroys == 1);
		fixture_end();
	}
	for (i = 0; i < 7; i++) {
		fixture_begin();
		cmd = valid_command();
		switch (i) {
		case 0: cmd.width--; break;
		case 1: cmd.height--; break;
		case 2: cmd.pixel_format++; break;
		case 3: cmd.pitches[0]--; break;
		case 4: cmd.offsets[0]++; break;
		case 5: cmd.modifier[0]++; break;
		case 6: cmd.flags = 1; break;
		}
		fb = kms_fb_create(&dev, &file, &cmd);
		CHECK(IS_ERR(fb) && PTR_ERR(fb) == -EINVAL);
		CHECK(helper_creates == 0 && native_checks == 0);
		fixture_end();
	}
	for (i = 1; i < 4; i++) {
		for (j = 0; j < 4; j++) {
			fixture_begin();
			cmd = valid_command();
			switch (j) {
			case 0: cmd.handles[i] = 7; break;
			case 1: cmd.pitches[i] = 7680; break;
			case 2: cmd.offsets[i] = 4; break;
			case 3: cmd.modifier[i] = 1; break;
			}
			fb = kms_fb_create(&dev, &file, &cmd);
			CHECK(IS_ERR(fb) && PTR_ERR(fb) == -EINVAL && helper_creates == 0);
			fixture_end();
		}
	}
	fixture_begin(); cmd = valid_command(); cmd.handles[0] = 8;
	fb = kms_fb_create(&dev, &file, &cmd);
	CHECK(IS_ERR(fb) && PTR_ERR(fb) == -ENXIO);
	CHECK(native_checks == 1 && helper_destroys == 1);
	fixture_end();
	fixture_begin(); cmd = valid_command(); cmd.handles[0] = 0;
	fb = kms_fb_create(&dev, &file, &cmd);
	CHECK(IS_ERR(fb) && PTR_ERR(fb) == -ENOENT && native_checks == 0);
	fixture_end();
	fixture_begin(); cmd = valid_command(); native_bo.cma.base.size--;
	fb = kms_fb_create(&dev, &file, &cmd);
	CHECK(IS_ERR(fb) && PTR_ERR(fb) == -EINVAL && native_checks == 0);
	fixture_end();
	fixture_begin(); cmd = valid_command(); create_error = -ENOMEM;
	fb = kms_fb_create(&dev, &file, &cmd);
	CHECK(IS_ERR(fb) && PTR_ERR(fb) == -ENOMEM && native_checks == 0);
	fixture_end();
}

static void
check_prepare_and_lifetime(void)
{
	struct bcmv3d_kms_plane_state *state, *copy;
	struct drm_plane_state *duplicated;
	unsigned int i;

	fixture_begin();
	CHECK(kms_plane_duplicate(&plane) == NULL);
	allocation_fail = true;
	kms_plane_reset(&plane);
	CHECK(plane.state == NULL && live == 0);
	fixture_end();
	fixture_begin(); state = active_plane(true);
	CHECK(!fence.signalled && kms_plane_prepare(&plane, &state->base) == 0);
	CHECK(reserves == 1 && state->frame != NULL && state->frame->refs == 1);
	CHECK(fence.refs == 2 && state->explicit == &fence && fence_gets == 1);
	CHECK(kms_plane_prepare(&plane, &state->base) == -EBUSY && reserves == 1);
	allocation_fail = true;
	CHECK(kms_plane_duplicate(&plane) == NULL && state->frame->refs == 1);
	allocation_fail = false;
	duplicated = kms_plane_duplicate(&plane);
	CHECK(duplicated != NULL && duplicated != &state->base);
	copy = kms_plane_state(duplicated);
	CHECK(copy->frame == NULL && copy->explicit == NULL && copy->base.fence == NULL);
	CHECK(copy->base.fb == state->base.fb && copy->base.fb->refs == 2);
	CHECK(state->frame->refs == 1 && fence.refs == 2);
	kms_plane_destroy_state(&plane, duplicated);
	CHECK(state->frame->refs == 1 && frame_puts == 0);
	cleanup_watch = state;
	kms_plane_cleanup(&plane, &state->base);
	CHECK(state->frame == NULL && state->explicit == NULL && fence.refs == 1);
	kms_plane_cleanup(&plane, &state->base);
	CHECK(frame_puts == 1 && fence.refs == 1);
	destroy_plane(); fixture_end();
	for (i = 0; i < 4; i++) {
		fixture_begin(); state = active_plane(true);
		switch (i) {
		case 0: state->base.crtc = NULL; break;
		case 1: crtc_state.active = false; break;
		case 2: atomic_state.crtc = NULL; break;
		case 3:
			drm_framebuffer_put(state->base.fb);
			state->base.fb = NULL;
			break;
		}
		CHECK(kms_plane_prepare(&plane, &state->base) == 0);
		CHECK(reserves == 0 && availability_calls == 0 && fence_gets == 0);
		destroy_plane(); fixture_end();
	}
	fixture_begin(); state = active_plane(true); kms.availability = EAGAIN;
	CHECK(kms_plane_prepare(&plane, &state->base) == -EAGAIN);
	CHECK(reserves == 0 && fence_gets == 0 && fence.refs == 1);
	destroy_plane(); fixture_end();
	fixture_begin(); state = active_plane(true); reserve_error = ENOMEM;
	CHECK(kms_plane_prepare(&plane, &state->base) == -ENOMEM);
	CHECK(state->frame == NULL && state->explicit == NULL && fence.refs == 1);
	CHECK(reserves == 1 && fence_gets == 1 && fence_puts == 1);
	cleanup_watch = state;
	kms_plane_cleanup(&plane, &state->base);
	CHECK(frame_puts == 0 && fence.refs == 1);
	destroy_plane(); fixture_end();
	fixture_begin(); state = active_plane(true);
	state->explicit = dma_fence_get(state->base.fence);
	CHECK(kms_plane_prepare(&plane, &state->base) == -EBUSY && reserves == 0);
	destroy_plane(); fixture_end();
	fixture_begin(); state = active_plane(false);
	CHECK(kms_plane_prepare(&plane, &state->base) == 0);
	CHECK(state->frame != NULL && state->explicit == NULL);
	destroy_plane(); fixture_end();
	fixture_begin(); state = active_plane(true);
	CHECK(kms_plane_prepare(&plane, &state->base) == 0);
	put_error = EIO; cleanup_watch = state;
	kms_plane_cleanup(&plane, &state->base);
	CHECK(faults == 1 && kms.fault == EIO && frame_puts == 1 && fence.refs == 1);
	kms_plane_cleanup(&plane, &state->base);
	CHECK(faults == 1 && frame_puts == 1 && fence.refs == 1);
	destroy_plane(); fixture_end();
	fixture_begin(); state = active_plane(true);
	CHECK(kms_plane_prepare(&plane, &state->base) == 0);
	cleanup_watch = state;
	kms_plane_reset(&plane);
	CHECK(frame_puts == 1 && fence.refs == 0 && state_destroys == 1);
	CHECK(plane.state != NULL && kms_plane_state(plane.state)->frame == NULL);
	allocation_fail = true;
	kms_plane_reset(&plane);
	CHECK(plane.state == NULL && state_destroys == 2 && live == 0);
	fixture_end();
}

static void
check_plane_geometry(void)
{
	struct bcmv3d_kms_plane_state *state;
	unsigned int i;

	fixture_begin(); state = active_plane(false);
	state->base.crtc_x = 5000; helper_visible = false;
	CHECK(kms_plane_check(&plane, &state->base) == -EINVAL && plane_checks == 1);
	destroy_plane(); fixture_end();
	for (i = 0; i < 13; i++) {
		fixture_begin(); state = active_plane(false);
		switch (i) {
		case 0: CHECK(kms_plane_check(&plane, &state->base) == 0); break;
		case 1: state->base.crtc_x = 1; break;
		case 2: state->base.crtc_y = 1; break;
		case 3: state->base.crtc_w--; break;
		case 4: state->base.crtc_h--; break;
		case 5: state->base.src_x = 1; break;
		case 6: state->base.src_y = 1; break;
		case 7: state->base.src_w--; break;
		case 8: state->base.src_h--; break;
		case 9: state->base.rotation = 2; break;
		case 10: state->base.fb->storage.format++; break;
		case 11: state->base.fb->modifier = 1; break;
		case 12: helper_visible = false; break;
		}
		if (i != 0)
			CHECK(kms_plane_check(&plane, &state->base) == -EINVAL);
		destroy_plane(); fixture_end();
	}
	fixture_begin(); state = active_plane(false); crtc_state.active = false;
	helper_visible = false;
	CHECK(kms_plane_check(&plane, &state->base) == 0);
	destroy_plane(); fixture_end();
	fixture_begin(); state = active_plane(false); helper_result = -ERANGE;
	CHECK(kms_plane_check(&plane, &state->base) == -ERANGE);
	destroy_plane(); fixture_end();
	fixture_begin(); state = active_plane(false); atomic_state.crtc = NULL;
	CHECK(kms_plane_check(&plane, &state->base) == -EINVAL && plane_checks == 0);
	destroy_plane(); fixture_end();
	fixture_begin(); state = active_plane(false); state->base.crtc = NULL;
	CHECK(kms_plane_check(&plane, &state->base) == -EINVAL && plane_checks == 0);
	drm_framebuffer_put(state->base.fb); state->base.fb = NULL;
	CHECK(kms_plane_check(&plane, &state->base) == 0 && plane_checks == 0);
	destroy_plane(); fixture_end();
}

int
main(void)
{
	check_format_blob();
	check_modes();
	check_framebuffers();
	check_prepare_and_lifetime();
	check_plane_geometry();
	printf("PASS actual KMS framebuffer/plane: %u cases, %u checks; "
	    "XR24/LINEAR blob, reserve-only preparation and balanced ownership\n", cases, checks);
	return 0;
}
