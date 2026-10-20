/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Run the actual event helper and DRM cleanup bodies with controlled services. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "CHECK %d: %s\n", __LINE__, #x); exit(1); } } while (0)
/* These values come from the target sys/errno.h and linux/errno.h. */
#undef ELAST
#define ELAST 98
#define ERESTARTSYS (ELAST + 1)
#define container_of(p, t, m) ((t *)((char *)(p) - offsetof(t, m)))
#define DRM_DEBUG_ATOMIC(...) ((void)0)

struct list_head { struct list_head *next, *prev; };
static void
list_init(struct list_head *head)
{
	head->next = head->prev = head;
}
static void
list_add_tail(struct list_head *entry, struct list_head *head)
{
	entry->prev = head->prev;
	entry->next = head;
	head->prev->next = entry;
	head->prev = entry;
}
static void
list_del(struct list_head *entry)
{
	CHECK(entry->next != NULL && entry->prev != NULL);
	CHECK(entry->next->prev == entry && entry->prev->next == entry);
	entry->prev->next = entry->next;
	entry->next->prev = entry->prev;
	entry->next = entry->prev = NULL;
}
#define list_for_each_entry_safe(p, n, h, m) \
	for (struct list_head *_cursor = (h)->next, *_next; \
	    _cursor != (h) && ((_next = _cursor->next), \
	    (p) = container_of(_cursor, __typeof__(*(p)), m), \
	    (n) = _next == (h) ? NULL : \
	    container_of(_next, __typeof__(*(n)), m), (void)(n), true); \
	    _cursor = _next)

struct completion { unsigned int done; };
struct dma_fence { unsigned int refs, signals, puts; int error; };
struct drm_event { unsigned int length; };
struct drm_file;
struct drm_pending_event {
	struct completion *completion;
	void (*completion_release)(struct completion *);
	struct drm_event *event;
	struct dma_fence *fence;
	struct drm_file *file_priv;
	struct list_head link, pending_link;
};
struct drm_pending_vblank_event {
	struct drm_pending_event base;
	struct drm_event event;
};
struct drm_mode_config { int num_crtc, num_total_plane; };
struct drm_device { bool event_lock; struct drm_mode_config mode_config; };
struct drm_minor { struct drm_device *dev; };
struct drm_file {
	struct drm_minor *minor;
	struct list_head pending_event_list, event_list;
	unsigned int event_space;
};
struct drm_crtc_commit {
	struct completion flip_done;
	unsigned int refs;
	bool abort_completion;
	struct drm_pending_vblank_event *event;
};
struct drm_crtc_state {
	struct drm_crtc_commit *commit;
	struct drm_pending_vblank_event *event;
	void *mode_blob, *degamma_lut, *ctm, *gamma_lut;
};
struct drm_crtc;
struct drm_crtc_funcs {
	void (*atomic_destroy_state)(struct drm_crtc *, struct drm_crtc_state *);
};
struct drm_crtc { const struct drm_crtc_funcs *funcs; };
/* The full clear function is compiled; these unrelated arrays are empty. */
#define OTHER_STATE(t) \
	struct t; \
	struct t##_funcs { void (*atomic_destroy_state)(struct t *, void *); }; \
	struct t { const struct t##_funcs *funcs; }
OTHER_STATE(drm_connector);
OTHER_STATE(drm_plane);
OTHER_STATE(drm_private_obj);
#define STATE_ENTRY(t, s) struct { struct t *ptr; s *state, *old_state, *new_state; struct drm_crtc_commit *commit; }
struct drm_atomic_state {
	struct drm_device *dev;
	int num_connector, num_private_objs;
	STATE_ENTRY(drm_crtc, struct drm_crtc_state) crtcs[1];
	STATE_ENTRY(drm_connector, void) connectors[1];
	STATE_ENTRY(drm_plane, void) planes[1];
	STATE_ENTRY(drm_private_obj, void) private_objs[1];
	struct drm_crtc_commit *fake_commit;
};

static char order[128];
static size_t order_size;
static unsigned int event_frees, commit_frees, completion_puts;
static bool irq_context, helper_active, abort_cleanup;
static int close_hook;
static struct drm_pending_event *owned;
/* Keep the allocation identity separate from the first-member DRM base. */
static struct drm_pending_vblank_event *allocation;
static struct drm_file *open_file;
static struct drm_device *active_dev;

static void
record(char token)
{
	CHECK(order_size + 1 < sizeof(order));
	order[order_size++] = token;
	order[order_size] = '\0';
}
static void
lock_event(bool *lock, unsigned long *flags)
{
	CHECK(!*lock);
	*flags = irq_context;
	*lock = true;
	record('L');
}
static void
unlock_event(bool *lock, unsigned long flags)
{
	CHECK(*lock && flags == (unsigned long)irq_context);
	*lock = false;
	record('U');
}
#define spin_lock_irqsave(lock, flags) lock_event((lock), &(flags))
#define spin_unlock_irqrestore(lock, flags) unlock_event((lock), (flags))

static void drm_events_release(struct drm_file *);
static void
maybe_close(int hook)
{
	if (close_hook == hook) {
		close_hook = 0;
		drm_events_release(open_file);
	}
}
static void
complete_all(struct completion *completion)
{
	CHECK(!active_dev->event_lock && helper_active);
	CHECK(owned->completion == NULL);
	CHECK(owned->completion_release == NULL);
	CHECK(completion->done == 0);
	completion->done++;
	record('C');
	maybe_close(1);
}
static void
dma_fence_set_error(struct dma_fence *fence, int error)
{
	CHECK(!active_dev->event_lock && fence->signals == 0);
	CHECK(error < 0 && error >= -ELAST);
	fence->error = error;
	record('E');
}
static void
dma_fence_signal(struct dma_fence *fence)
{
	CHECK(!active_dev->event_lock && fence->signals == 0);
	CHECK(fence->error < 0);
	fence->signals++;
	record('S');
	maybe_close(2);
}
static void
dma_fence_put(struct dma_fence *fence)
{
	CHECK(!active_dev->event_lock && fence->refs > 1);
	CHECK(fence->signals == 1 && fence->error < 0);
	fence->refs--;
	fence->puts++;
	record('P');
}
static void
kfree(void *pointer)
{
	const struct drm_pending_event *event = pointer;

	if (pointer == NULL)
		return;
	CHECK(!active_dev->event_lock);
	CHECK(pointer == owned && event_frees == 0);
	CHECK(allocation != NULL && &allocation->base == event);
	if (!abort_cleanup) {
		CHECK(event->completion == NULL);
		CHECK(event->completion_release == NULL);
	}
	event_frees++;
	record('F');
	owned = NULL;
	free(allocation);
	allocation = NULL;
}
static void
drm_crtc_commit_put(struct drm_crtc_commit *commit)
{
	CHECK(commit->refs != 0);
	if (helper_active) {
		CHECK(owned->completion == NULL);
		CHECK(owned->completion_release == NULL);
		CHECK(commit->flip_done.done == 1);
		completion_puts++;
		record('R');
	}
	if (--commit->refs == 0) {
		commit_frees++;
		free(commit);
	}
}
static void
drm_property_blob_put(const void *blob)
{
	CHECK(blob == NULL);
}
static void
drm_connector_put(struct drm_connector *connector)
{
	(void)connector;
	CHECK(false);
}
/* A source control calls this instead of cancel. No success event is valid. */
void drm_send_event_locked(struct drm_device *, struct drm_pending_event *);
void
drm_send_event_locked(struct drm_device *dev, struct drm_pending_event *event)
{
	(void)dev;
	(void)event;
	CHECK(false);
}

#include "core-bodies.h"
#include "event-types.h"
#include "event-body.h"

static void
destroy_crtc_state(struct drm_crtc *crtc, struct drm_crtc_state *state)
{
	(void)crtc;
	__drm_atomic_helper_crtc_destroy_state(state);
}
static const struct drm_crtc_funcs crtc_funcs = {
	.atomic_destroy_state = destroy_crtc_state
};

static void
reset(struct drm_device *dev, struct drm_file *file, struct drm_minor *minor)
{
	memset(dev, 0, sizeof(*dev));
	memset(file, 0, sizeof(*file));
	minor->dev = dev;
	file->minor = minor;
	file->event_space = 2048;
	list_init(&file->pending_event_list);
	list_init(&file->event_list);
	order_size = event_frees = commit_frees = completion_puts = 0;
	order[0] = '\0';
	close_hook = 0;
	helper_active = abort_cleanup = false;
	owned = NULL;
	allocation = NULL;
	active_dev = dev;
	open_file = file;
}
static struct drm_pending_vblank_event *
new_event(struct drm_file *file)
{
	struct drm_pending_vblank_event *event = calloc(1, sizeof(*event));

	CHECK(event != NULL && owned == NULL);
	allocation = event;
	owned = &event->base;
	event->event.length = sizeof(event->event);
	event->base.event = &event->event;
	if (file != NULL) {
		CHECK(file->event_space >= event->event.length);
		file->event_space -= event->event.length;
		event->base.file_priv = file;
		list_add_tail(&event->base.pending_link,
		    &file->pending_event_list);
	}
	return event;
}
static void
call_detached(struct drm_device *dev,
    struct drm_pending_vblank_event **owner, int error)
{
	struct drm_pending_vblank_event *event;
	unsigned long flags;

	/* Model the caller's required detach, including its CRTC-state pointer. */
	spin_lock_irqsave(&dev->event_lock, flags);
	event = *owner;
	*owner = NULL;
	spin_unlock_irqrestore(&dev->event_lock, flags);
	order_size = 0;
	order[0] = '\0';
	helper_active = true;
	bcmv3d_kms_event_fail(dev, event, error);
	helper_active = false;
}
static void
check_case(bool completion, bool fence, bool file_event, int hook,
    int error, const char *expected)
{
	struct drm_device dev;
	struct drm_file file;
	struct drm_minor minor;
	struct drm_pending_vblank_event *event, *owner;
	struct drm_crtc_commit *commit = NULL;
	struct drm_crtc_state crtc_state = { 0 };
	struct drm_crtc crtc = { .funcs = &crtc_funcs };
	struct drm_atomic_state state = { .dev = &dev };
	struct dma_fence out_fence = { .refs = 2 };
	int expected_error = error > 0 && error <= ELAST ? error : EIO;

	reset(&dev, &file, &minor);
	event = new_event(file_event ? &file : NULL);
	owner = event;
	if (completion) {
		commit = calloc(1, sizeof(*commit));
		CHECK(commit != NULL);
		/* State, atomic entry, and setup_commit's completion reference. */
		commit->refs = 3;
		commit->abort_completion = true;
		/* swap_state cleared commit->event; caller clears state->event. */
		crtc_state.commit = commit;
		dev.mode_config.num_crtc = 1;
		state.crtcs[0].ptr = &crtc;
		state.crtcs[0].state = &crtc_state;
		state.crtcs[0].commit = commit;
		event->base.completion = &commit->flip_done;
		event->base.completion_release = release_crtc_commit;
	}
	if (fence)
		event->base.fence = &out_fence;
	if (hook == 3)
		drm_events_release(&file);
	else
		close_hook = hook;
	call_detached(&dev, &owner, error);
	CHECK(owner == NULL && owned == NULL && event_frees == 1);
	CHECK(!dev.event_lock && strcmp(order, expected) == 0);
	CHECK(file.pending_event_list.next == &file.pending_event_list);
	CHECK(file.event_list.next == &file.event_list);
	CHECK(file.event_space == (file_event && hook != 0 ?
	    2048 - sizeof(struct drm_event) : 2048));
	CHECK(out_fence.refs == (fence ? 1U : 2U));
	CHECK(out_fence.puts == (fence ? 1U : 0U));
	CHECK(out_fence.signals == (fence ? 1U : 0U));
	CHECK(out_fence.error == (fence ? -expected_error : 0));
	if (completion) {
		CHECK(commit->refs == 2 && commit->flip_done.done == 1);
		CHECK(completion_puts == 1 && commit->abort_completion);
		/* Run both real cleanup layers after the event was freed. */
		drm_atomic_state_default_clear(&state);
		CHECK(commit_frees == 1 && state.crtcs[0].commit == NULL);
		CHECK(state.crtcs[0].ptr == NULL);
	}
	/* Once-only ownership: the caller retained NULL, never a stale pointer. */
	call_detached(&dev, &owner, EIO);
	CHECK(event_frees == 1 && strcmp(order, "") == 0);
}
static void
check_optional_completion(void)
{
	struct drm_device dev;
	struct drm_file file;
	struct drm_minor minor;
	struct completion completion = { 0 };
	struct drm_pending_vblank_event *owner;

	reset(&dev, &file, &minor);
	owner = new_event(NULL);
	owner->base.completion = &completion;
	call_detached(&dev, &owner, EIO);
	CHECK(completion.done == 1 && completion_puts == 0);
	CHECK(event_frees == 1 && strcmp(order, "CLUF") == 0);
}
static void
check_abort_baseline(void)
{
	struct drm_device dev;
	struct drm_file file;
	struct drm_minor minor;
	struct drm_crtc_commit *commit;
	struct drm_crtc_state crtc_state = { 0 };
	struct drm_crtc crtc = { .funcs = &crtc_funcs };
	struct drm_atomic_state state = { .dev = &dev };

	reset(&dev, &file, &minor);
	commit = calloc(1, sizeof(*commit));
	CHECK(commit != NULL);
	commit->refs = 3;
	commit->abort_completion = true;
	commit->event = new_event(NULL);
	commit->event->base.completion = &commit->flip_done;
	commit->event->base.completion_release = release_crtc_commit;
	crtc_state.commit = commit;
	crtc_state.event = commit->event;
	dev.mode_config.num_crtc = 1;
	state.crtcs[0].ptr = &crtc;
	state.crtcs[0].state = &crtc_state;
	state.crtcs[0].commit = commit;
	/* Pre-swap abort consumes its reference without signaling a completion. */
	abort_cleanup = true;
	drm_atomic_state_default_clear(&state);
	CHECK(event_frees == 1 && commit_frees == 1 && completion_puts == 0);
	CHECK(strcmp(order, "F") == 0);
}
int
main(void)
{
	unsigned int cases = 1;

	const int errors[] = { EIO, EINVAL, ELAST, 0, -EIO,
	    ERESTARTSYS, 4095, 2147483647 };

	bcmv3d_kms_event_fail(NULL, NULL, EIO);
	for (unsigned int irq = 0; irq < 2; irq++) {
		irq_context = irq != 0;
		check_case(false, false, true, 0, EIO, "LUF"); cases++;
		check_case(true, false, true, 0, EIO, "CRLUF"); cases++;
		check_case(false, true, true, 0, EIO, "ESLUPF"); cases++;
		check_case(true, true, true, 0, EIO, "ECRSLUPF"); cases++;
		check_case(true, true, false, 0, EIO, "ECRSLUPF"); cases++;
		check_case(true, true, true, 1, EIO, "ECLURSLUPF"); cases++;
		check_case(true, true, true, 2, EIO, "ECRSLULUPF"); cases++;
		check_case(true, true, true, 3, EIO, "ECRSLUPF"); cases++;
		for (size_t i = 0; i < sizeof(errors) / sizeof(errors[0]); i++) {
			check_case(true, true, true, 0, errors[i], "ECRSLUPF");
			cases++;
		}
		check_optional_completion(); cases++;
		check_abort_baseline(); cases++;
	}
	printf("PASS %u actual-source event scenarios (process and IRQ services)\n", cases);
	return 0;
}
