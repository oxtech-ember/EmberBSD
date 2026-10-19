/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD, AI-assisted. Actual atomic event completion controls. */
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
static unsigned checks, sent, cancelled, completed, released, signalled;
#define CHECK(x) do { checks++; if (!(x)) {fprintf(stderr,"%d: %s\n",__LINE__,#x);abort();} } while (0)
struct completion {
	bool done;
};
struct dma_fence {
	int status;
	unsigned refs;
};
struct drm_pending_event {
	struct dma_fence *fence;
	struct completion *completion;
	void (*completion_release) (struct completion *);
};
struct drm_pending_vblank_event {
	struct drm_pending_event base;
	unsigned pipe;
	union {
		struct {
			uint32_t sequence, tv_sec, tv_usec;
		}      vbl;
	}     event;
};
struct drm_crtc {
	unsigned index;
};
struct drm_crtc_state {
	struct drm_pending_vblank_event *event;
};
struct drm_device {
	bool event_lock;
};
struct a733_drm {
	struct drm_device *dev;
	struct drm_crtc crtc;
};
struct sun60i_a733_de352_completion {
	uint64_t observed_sequence, observed_time_ns;
};
static struct drm_pending_vblank_event observed;
static bool *held_lock;
#define spin_lock_irqsave(p,f) do { (f)=0; CHECK(!*(p)); *(p)=true; held_lock=(p); } while (0)
#define spin_unlock_irqrestore(p,f) do { (void)(f); CHECK(*(p)); *(p)=false; } while (0)
static unsigned
drm_crtc_index(struct drm_crtc *crtc)
{
	return crtc->index;
}
static void complete_all(struct completion *c){
	CHECK(*held_lock && !c->done);
	c->done = true;
	completed++;
}
static void release_completion(struct completion *c){
	CHECK(*held_lock && c->done);
	released++;
}
static void dma_fence_set_error(struct dma_fence *f, int error){
	CHECK(*held_lock && error < 0 && f->status == 0);
	f->status = error;
}
static int dma_fence_signal(struct dma_fence *f){
	CHECK(*held_lock && f->refs == 1);
	if (f->status == 0)
		f->status = 1;
	signalled++;
	return 0;
}
static void
drm_send_event_locked(struct drm_device *dev, struct drm_pending_event *event)
{
	CHECK(dev->event_lock);
	sent++;
	if (event->completion) {
		complete_all(event->completion);
		event->completion_release(event->completion);
		event->completion = NULL;
	}
	if (event->fence) {
		dma_fence_signal(event->fence);
		event->fence->refs--;
	}
	observed = *(struct drm_pending_vblank_event *)event;
	free(event);
}
static void
drm_event_cancel_free(struct drm_device *dev, struct drm_pending_event *event)
{
	CHECK(!dev->event_lock && event->completion == NULL && event->completion_release == NULL);
	cancelled++;
	if (event->fence) {
		CHECK(event->fence->status < 0 && event->fence->refs == 1);
		event->fence->refs--;
	}
	free(event);
}
#include "event.c"
int
main(void)
{
	struct drm_device dev = {0};
	struct a733_drm sc = {.dev = &dev,.crtc = {3}};
	struct sun60i_a733_de352_completion done = {1234, UINT64_C(555123456789)};
	const int errors[] = {0, EIO, ETIMEDOUT, ENOMEM, ENXIO, EBUSY};
	for (unsigned e = 0; e < sizeof(errors) / sizeof(errors[0]); e++)
		for (unsigned flags = 0; flags < 4; flags++) {
			struct completion completion = {false};
			struct dma_fence fence = {0, 1};
			struct drm_crtc_state state = {calloc(1, sizeof(*state.event))};
			CHECK(state.event);
			if (flags & 1) {
				state.event->base.completion = &completion;
				state.event->base.completion_release = release_completion;
			}
			if (flags & 2)
				state.event->base.fence = &fence;
			unsigned oldsent = sent, oldcancelled = cancelled,
			         oldcompleted = completed, oldreleased = released,
			         oldsignal = signalled;
			a733_commit_event(&sc, &state, &done, errors[e]);
			CHECK(state.event == NULL && !dev.event_lock);
			CHECK(completed == oldcompleted + ((flags & 1) != 0) && released == oldreleased + ((flags & 1) != 0));
			CHECK(signalled == oldsignal + ((flags & 2) != 0));
			if (errors[e]) {
				CHECK(sent == oldsent && cancelled == oldcancelled + 1);
				if (flags & 2)
					CHECK(fence.status == -errors[e] && fence.refs == 0);
			} else {
				CHECK(sent == oldsent + 1 && cancelled == oldcancelled);
				CHECK(observed.pipe == 3 && observed.event.vbl.sequence == 0 && observed.event.vbl.tv_sec == 555 && observed.event.vbl.tv_usec == 123456);
				if (flags & 2)
					CHECK(fence.status == 1 && fence.refs == 0);
			}
		}
	struct drm_crtc_state empty = {NULL};
	unsigned oldsent = sent, oldcancelled = cancelled;
	a733_commit_event(&sc, &empty, &done, EIO);
	CHECK(sent == oldsent && cancelled == oldcancelled && !dev.event_lock);
	printf("PASS %u output event checks\n", checks);
	return 0;
}
