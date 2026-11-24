/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* AI-assisted. Exercise native sync_file readiness across callback arming. */
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { \
	if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } \
} while (0)
#define KASSERT(x) CHECK(x)
#define container_of(p, type, member) ((type *)((char *)(p) - offsetof(type, member)))
enum { POLLIN = 1, POLLOUT = 2, NOTE_SUBMIT = 1 };
enum completion { LATER, DURING_ARM, ALREADY_SIGNALED };
struct dma_fence_cb { int unused; };
struct dma_fence {
	enum completion when;
	void (*callback)(struct dma_fence *, struct dma_fence_cb *);
	struct dma_fence_cb *data;
	unsigned calls;
};
struct selinfo { unsigned records, notifications, wakeups; };
struct sync_file {
	bool sf_lock, sf_signalled, sf_polling;
	struct dma_fence *sf_fence;
	struct dma_fence_cb sf_fcb;
	struct selinfo sf_selq;
};
struct file { struct sync_file *f_data; };
static void *curlwp;

static bool mutex_owned(bool *lock) { return *lock; }
static void mutex_enter(bool *lock) { CHECK(!*lock); *lock = true; }
static void mutex_exit(bool *lock) { CHECK(*lock); *lock = false; }

static void
selrecord(void *lwp, struct selinfo *sel)
{
	CHECK(lwp == curlwp);
	sel->records++;
}

static void
selnotify(struct selinfo *sel, int events, long hint)
{
	CHECK(events == POLLIN && hint == NOTE_SUBMIT);
	sel->notifications++;
	if (sel->records != 0)
		sel->wakeups++;

}

static void
signal_fence(struct dma_fence *fence)
{
	CHECK(fence->callback != NULL);
	void (*complete)(struct dma_fence *, struct dma_fence_cb *) = fence->callback;
	fence->callback = NULL;
	complete(fence, fence->data);
}

static int
dma_fence_add_callback(struct dma_fence *fence, struct dma_fence_cb *data,
    void (*complete)(struct dma_fence *, struct dma_fence_cb *))
{
	struct sync_file *sf = container_of(data, struct sync_file, sf_fcb);

	CHECK(!mutex_owned(&sf->sf_lock));
	CHECK(++fence->calls == 1 && fence->callback == NULL);
	if (fence->when == ALREADY_SIGNALED)
		return -ENOENT;
	fence->callback = complete;
	fence->data = data;
	/* Reproduce completion after successful arm and before the caller relocks. */
	if (fence->when == DURING_ARM)
		signal_fence(fence);
	return 0;
}

#include "sync-file-body.h"

static void
check_poll(enum completion when)
{
	struct dma_fence fence = { .when = when };
	struct sync_file sf = { .sf_fence = &fence };
	struct file fp = { .f_data = &sf };

	CHECK(sync_file_poll(&fp, POLLOUT) == 0 && fence.calls == 0);
	CHECK(sync_file_poll(&fp, POLLIN) == (when == LATER ? 0 : POLLIN));
	CHECK(sf.sf_selq.records == (unsigned)(when == LATER));
	if (when == LATER) {
		CHECK(sf.sf_selq.notifications == 0);
		signal_fence(&fence);
		CHECK(sf.sf_selq.wakeups == 1);
	}
	CHECK(sync_file_poll(&fp, POLLIN) == POLLIN && fence.calls == 1);
	CHECK(sf.sf_selq.notifications == 1 && !mutex_owned(&sf.sf_lock));
}

int
main(void)
{
	check_poll(DURING_ARM);
	check_poll(LATER);
	check_poll(ALREADY_SIGNALED);
	puts("sync_file poll callback readiness: PASS");
	return 0;
}
