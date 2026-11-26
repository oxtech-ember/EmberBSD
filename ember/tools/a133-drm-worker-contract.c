/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD, AI-assisted. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define container_of(ptr, type, member) \
	((type *)((char *)(ptr) - offsetof(type, member)))

struct drm_crtc { int unused; };
struct drm_plane { int unused; };
struct drm_device { int unused; };
struct drm_framebuffer { int unused; };
struct drm_gem_object { unsigned refs; };
struct drm_plane_state { struct drm_framebuffer *fb; int *fence; };
struct drm_crtc_state { bool active; int *event; };
struct drm_atomic_state {
	struct drm_plane_state plane;
	struct drm_crtc_state crtc;
};
struct test_dma { struct { uint32_t ds_addr; } dm_segs[1]; };
struct test_buffer { struct drm_gem_object base; struct test_dma *dmamap; };
struct a133_drm_fb { struct drm_framebuffer base; struct test_buffer *object; };
struct work { int unused; };
struct a133_commit {
	struct work work;
	struct drm_atomic_state *state;
	bool nonblock, done;
};
struct a133_drm {
	struct drm_crtc crtc;
	struct drm_plane plane;
	uint32_t boot_addr;
	struct drm_gem_object *shown;
	bool vblank_irq, vblank_on, busy, lock;
	int idle_cv;
	struct drm_device *dev;
};

static struct a133_drm *device;
static unsigned holds, vblank, finished, hw_done, clean, cleanup_done;
static uint32_t flipped;
static int result;

static struct drm_plane_state *
drm_atomic_get_new_plane_state(struct drm_atomic_state *s, struct drm_plane *p)
{
	assert(p == &device->plane);
	return &s->plane;
}

static struct drm_crtc_state *
drm_atomic_get_new_crtc_state(struct drm_atomic_state *s, struct drm_crtc *c)
{
	assert(c == &device->crtc);
	return &s->crtc;
}

static int ember_drm_flip_wait_producer(int *fence)
{
	assert(!vblank && device->busy);
	return fence ? *fence : 0;
}
static void drm_crtc_vblank_on(struct drm_crtc *c)
{
	assert(c == &device->crtc && !device->vblank_on && !finished);
}
static int drm_crtc_vblank_get(struct drm_crtc *c)
{
	assert(c == &device->crtc && device->vblank_on && !finished);
	vblank++;
	return 0;
}
static void drm_crtc_vblank_put(struct drm_crtc *c)
{
	assert(c == &device->crtc && vblank == 1 && finished == 1);
	vblank--;
}
static void drm_crtc_vblank_off(struct drm_crtc *c)
{
	assert(c == &device->crtc && !vblank && finished == 1);
}
static void a133_flip(struct a133_drm *sc, uint32_t addr, uint64_t *t, uint64_t *ns)
{
	assert(sc == device && sc->busy && !finished && !flipped);
	flipped = addr;
	*t = 42;
	*ns = 1234500;
}
static void drm_gem_object_get(struct drm_gem_object *obj)
{
	assert(flipped && !finished && obj != device->shown);
	obj->refs++;
	holds++;
}
static void drm_gem_object_put_unlocked(struct drm_gem_object *obj)
{
	assert(flipped && !finished && obj == device->shown && obj->refs);
	obj->refs--;
}
static void mutex_enter(bool *lock) { assert(!*lock); *lock = true; }
static void mutex_exit(bool *lock) { assert(*lock); *lock = false; }
static void cv_broadcast(int *cv)
{
	assert(cv == &device->idle_cv && device->lock && !device->busy);
}
static void ember_drm_flip_finish(struct drm_crtc *crtc, int **event,
    uint64_t sequence, uint64_t ns, int error)
{
	assert(crtc == &device->crtc && !device->busy && !device->lock);
	assert(finished == 0 && hw_done == 0);
	assert(error == 0 ? sequence == 42 && ns == 1234500 : sequence == 0 && ns == 0);
	finished++;
	result = error;
	*event = NULL;
}
static void drm_atomic_helper_commit_hw_done(struct drm_atomic_state *state)
{
	assert(!state->crtc.event && finished == 1 && !vblank);
	hw_done++;
}
static void drm_atomic_helper_cleanup_planes(struct drm_device *dev, struct drm_atomic_state *state)
{
	assert(dev == device->dev && !state->crtc.event && hw_done == 1);
	clean++;
}
static void drm_atomic_helper_commit_cleanup_done(struct drm_atomic_state *state)
{
	assert(!state->crtc.event && clean == 1);
	cleanup_done++;
}
static void drm_atomic_state_put(struct drm_atomic_state *state)
{
	assert(!state->crtc.event && cleanup_done == 1);
}
static void complete_all(bool *done) { assert(!*done); *done = true; }
static void destroy_completion(bool *done) { assert(!*done && cleanup_done == 1); }
static void kmem_free(void *job, size_t len)
{
	assert(job != NULL && len == sizeof(struct a133_commit));
}

#include "worker.c"

int
main(void)
{
	for (int scenario = 0; scenario < 5; scenario++) {
		struct drm_gem_object old = {1};
		struct test_dma map = {{{0x2000}}};
		struct test_buffer buffer = {{1}, &map};
		struct a133_drm_fb fb = {{0}, &buffer};
		struct drm_device dev = {0};
		struct a133_drm sc = {.shown = &old, .boot_addr = 0x1000,
		    .busy = true, .vblank_irq = scenario != 3,
		    .vblank_on = scenario == 4, .dev = &dev};
		int error = scenario == 1 ? ETIMEDOUT : scenario == 2 ? EIO : 0;
		int event = 1;
		struct drm_atomic_state state = {
			.plane = {&fb.base, &error},
			.crtc = {scenario != 4, scenario == 3 ? NULL : &event},
		};
		struct a133_commit job = {.state = &state, .nonblock = scenario == 3};
		device = &sc;
		holds = vblank = finished = hw_done = clean = cleanup_done = 0;
		flipped = 0;
		result = -1;
		a133_commit_work(&job.work, &sc);
		assert(result == error && finished == 1 && job.done != job.nonblock);
		assert(!state.crtc.event && !sc.busy && !vblank && cleanup_done == 1);
		if (error) {
			assert(!flipped && !holds && old.refs == 1 && sc.shown == &old);
		} else if (scenario == 4) {
			assert(flipped == sc.boot_addr && !holds && !old.refs && sc.shown == NULL);
		} else {
			assert(flipped == 0x2000 && holds == 1 && old.refs == 0);
			assert(sc.shown == &buffer.base && buffer.base.refs == 2);
		}
	}
	puts("PASS A133 worker orders scanout retention, event, and cleanup");
	return 0;
}
