/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD, AI-assisted. Production shared event and producer fence contract. */
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
	long waited;
};
struct drm_pending_event {
	bool linked;
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
	struct drm_device *dev;
	unsigned index;
};
struct drm_crtc_state {
	struct drm_pending_vblank_event *event;
};
struct drm_device {
	bool event_lock;
};
static struct drm_pending_vblank_event observed;
static unsigned delivered;
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
	if (event->linked) {
		delivered++;
		observed = *(struct drm_pending_vblank_event *)event;
	}
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
static unsigned
msecs_to_jiffies(unsigned delay)
{
	CHECK(delay == 1000);
	return 100;
}

static long
dma_fence_wait_timeout(struct dma_fence *fence, bool interruptible,
    unsigned timeout)
{
	CHECK(!interruptible && timeout == 100);
	return fence->waited;
}

static int
dma_fence_get_status(struct dma_fence *fence)
{
	return fence->status;
}

#include "event.c"
int
main(void)
{
	struct drm_device dev = {0};
	struct drm_crtc crtc = {&dev, 3};
	struct dma_fence producer = {.status = 1, .waited = 1};
	CHECK(ember_drm_flip_wait_producer(NULL) == 0);
	CHECK(ember_drm_flip_wait_producer(&producer) == 0);
	producer.status = 0;
	CHECK(ember_drm_flip_wait_producer(&producer) == EIO);
	producer.status = -EIO;
	CHECK(ember_drm_flip_wait_producer(&producer) == EIO);
	producer.waited = 0;
	CHECK(ember_drm_flip_wait_producer(&producer) == ETIMEDOUT);
	producer.waited = -ECANCELED;
	CHECK(ember_drm_flip_wait_producer(&producer) == ECANCELED);
	/* The low 32 bits go to the 32-bit uapi sequence. */
	const uint64_t sequence = UINT64_C(0x1000004d2), time_ns = UINT64_C(555123456789);
	const int errors[] = {0, EIO, ETIMEDOUT, ENOMEM, ENXIO, EBUSY};
	for (unsigned e = 0; e < sizeof(errors) / sizeof(errors[0]); e++)
		for (unsigned flags = 0; flags < 8; flags++) {
			struct completion completion = {false};
			struct dma_fence fence = {.refs = 1};
			struct drm_crtc_state state = {calloc(1, sizeof(*state.event))};
			CHECK(state.event);
			state.event->base.linked = !(flags & 4);
			if (flags & 1) {
				state.event->base.completion = &completion;
				state.event->base.completion_release = release_completion;
			}
			if (flags & 2)
				state.event->base.fence = &fence;
			unsigned oldsent = sent, oldcancelled = cancelled,
			         oldcompleted = completed, oldreleased = released,
			         oldsignal = signalled, olddelivered = delivered;
			/* Both the CRTC slot and a detached worker slot are owned. */
			struct drm_pending_vblank_event *event = NULL;
			if ((flags & 1) != 0) {
				event = state.event;
				state.event = NULL;
				ember_drm_flip_finish(&crtc, &event, sequence, time_ns, errors[e]);
				CHECK(event == NULL);
			} else
				ember_drm_flip_finish(&crtc, &state.event, sequence, time_ns, errors[e]);
			CHECK(state.event == NULL && !dev.event_lock);
			CHECK(completed == oldcompleted + ((flags & 1) != 0) && released == oldreleased + ((flags & 1) != 0));
			CHECK(signalled == oldsignal + ((flags & 2) != 0));
			CHECK(delivered == olddelivered + (errors[e] == 0 && !(flags & 4)));
			if (errors[e]) {
				CHECK(sent == oldsent && cancelled == oldcancelled + 1);
				if (flags & 2)
					CHECK(fence.status == -errors[e] && fence.refs == 0);
			} else {
				CHECK(sent == oldsent + 1 && cancelled == oldcancelled);
				if (!(flags & 4))
					CHECK(observed.pipe == 3 && observed.event.vbl.sequence == 1234 && observed.event.vbl.tv_sec == 555 && observed.event.vbl.tv_usec == 123456);
				if (flags & 2)
					CHECK(fence.status == 1 && fence.refs == 0);
			}
		}
	unsigned oldsent = sent, oldcancelled = cancelled;
	struct drm_pending_vblank_event *empty = NULL;
	ember_drm_flip_finish(&crtc, &empty, sequence, time_ns, EIO);
	CHECK(sent == oldsent && cancelled == oldcancelled && !dev.event_lock);
	printf("PASS %u output event checks\n", checks);
	return 0;
}
