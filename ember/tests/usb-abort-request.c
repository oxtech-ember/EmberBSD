/*
 * Origin: EmberBSD (AI-assisted).
 * Extracted ioctl, abort-owner, and queue-start functions run with pthreads.
 * The completion backend models a control pipe and synchronous wakeups.
 * The late-claim backend models an HCD which drops the lock before cancellation.
 */
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef pthread_mutex_t kmutex_t;
typedef pthread_cond_t kcondvar_t;
typedef int usbd_status;
#define Static static
#define __diagused
#define DIAGNOSTIC 1
#define XFER_BUSY 1
#define USBD_NOT_STARTED 0
#define USBD_IN_PROGRESS 1
#define USBD_CANCELLED 2
#define USBD_TIMEOUT 3
#define USBD_NORMAL_COMPLETION 4
#define USBD_STALLED 5
#define USB_CONTROL_ENDPOINT 0
#define USB_ABORT_REQUEST 118
#define USBD_SHORT_XFER_OK 0x04
#define FWRITE 2
#define KASSERT(c) assert(c)
#define KASSERTMSG(c, ...) assert(c)
#define ASSERT_SLEEPABLE() ((void)0)
#define USBHIST_FUNC() ((void)0)
#define USBHIST_CALLARGS(...) ((void)0)
#define USBHIST_LOG(...) ((void)0)
#define SDT_PROBE1(...) ((void)0)
#define SDT_PROBE2(...) ((void)0)
#define SIMPLEQ_FIRST(q) ((q)->first)
#define SIMPLEQ_EMPTY(q) (SIMPLEQ_FIRST(q) == NULL)
#define SIMPLEQ_REMOVE_HEAD(q, next) ((q)->first = (q)->first->next)

struct usbd_xfer;
struct usbd_bus_methods { void (*ubm_abortx)(struct usbd_xfer *); };
struct usbd_bus {
	kmutex_t *ub_lock;
	unsigned ub_usepolling;
	const struct usbd_bus_methods *ub_methods;
};
struct usbd_pipe;
struct usbd_device { struct usbd_bus *ud_bus; struct usbd_pipe *ud_pipe0; };
struct usbd_pipe_methods {
	void (*upm_abort)(struct usbd_xfer *);
	usbd_status (*upm_start)(struct usbd_xfer *);
};
struct usbd_pipe {
	struct usbd_device *up_dev;
	const struct usbd_pipe_methods *up_methods;
	struct { struct usbd_xfer *first; } up_queue;
	bool up_running, up_serialise, up_repeat, up_aborting;
	int *up_abortlwp;
	struct usbd_xfer *up_callingxfer;
	kcondvar_t up_callingcv;
};
struct usbd_xfer {
	struct usbd_bus *ux_bus;
	struct usbd_pipe *ux_pipe;
	struct usbd_xfer *ux_next;
	usbd_status ux_status;
	int ux_state;
	bool ux_done;
	kcondvar_t ux_cv;
};
struct ugen_softc { struct usbd_device *sc_udev; };

static _Thread_local int current_lwp;
static _Thread_local bool owned;
#define curlwp (&current_lwp)
static kmutex_t bus_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gate_cv = PTHREAD_COND_INITIALIZER;
static bool pause_abort, abort_entered, abort_release, late_claim;
static unsigned owner_waiters, aborts, starts, completions, sync_wakeups;
static struct usbd_bus test_bus;
static struct usbd_device test_device;
static struct usbd_pipe test_pipe;
static struct usbd_xfer active, queued;
static struct ugen_softc ugen;

static void
mutex_enter(kmutex_t *lock)
{
	assert(lock == &bus_lock && !owned);
	assert(pthread_mutex_lock(lock) == 0);
	owned = true;
}

static void
mutex_exit(kmutex_t *lock)
{
	assert(lock == &bus_lock && owned);
	owned = false;
	assert(pthread_mutex_unlock(lock) == 0);
}

#define mutex_owned(lock) ((lock) == &bus_lock && owned)
#define usbd_lock_pipe(p) mutex_enter((p)->up_dev->ud_bus->ub_lock)
#define usbd_unlock_pipe(p) mutex_exit((p)->up_dev->ud_bus->ub_lock)
#define cv_broadcast(cv) assert(pthread_cond_broadcast(cv) == 0)

static void
cv_wait(kcondvar_t *cv, kmutex_t *lock)
{
	assert(mutex_owned(lock));
	if (cv == &test_pipe.up_callingcv) {
		assert(pthread_mutex_lock(&gate) == 0);
		owner_waiters++;
		assert(pthread_cond_broadcast(&gate_cv) == 0);
		assert(pthread_mutex_unlock(&gate) == 0);
	}
	owned = false;
	assert(pthread_cond_wait(cv, lock) == 0);
	owned = true;
}

static void usbd_ar_pipe(struct usbd_pipe *);
static void usbd_start_next(struct usbd_pipe *);
static void usbd_suspend_pipe(struct usbd_pipe *);
static void usbd_resume_pipe(struct usbd_pipe *);
void usbd_abort_pipe(struct usbd_pipe *);

static void
usb_transfer_complete(struct usbd_xfer *xfer)
{
	struct usbd_pipe *pipe = xfer->ux_pipe;
	assert(owned && pipe->up_queue.first == xfer && !xfer->ux_done);
	pipe->up_queue.first = xfer->ux_next;
	xfer->ux_done = true;
	completions++;
	cv_broadcast(&xfer->ux_cv);
	if (pipe->up_running && pipe->up_serialise)
		usbd_start_next(pipe);
}

static void
usbd_xfer_cancel_timeout_async(const struct usbd_xfer *xfer)
{
	assert(owned && xfer->ux_status == USBD_IN_PROGRESS);
}

static void
/* The bus method requires a mutable xfer in its callback signature. */
// cppcheck-suppress constParameterCallback
hardware_abort(struct usbd_xfer *xfer)
{
	assert(owned && xfer == &active);
	aborts++;
	assert(pthread_mutex_lock(&gate) == 0);
	if (pause_abort) {
		pause_abort = false;
		abort_entered = true;
		assert(pthread_cond_broadcast(&gate_cv) == 0);
		mutex_exit(&bus_lock);
		while (!abort_release)
			assert(pthread_cond_wait(&gate_cv, &gate) == 0);
		assert(pthread_mutex_unlock(&gate) == 0);
		mutex_enter(&bus_lock);
	} else {
		assert(pthread_mutex_unlock(&gate) == 0);
	}
}

#include "usbd_xfer_abort.h"

static void
pipe_abort(struct usbd_xfer *xfer)
{
	if (!late_claim) {
		usbd_xfer_abort(xfer);
		return;
	}
	/* A vhci-like callback drops the lock before claiming the status. */
	hardware_abort(xfer);
	assert(xfer->ux_status == USBD_IN_PROGRESS);
	xfer->ux_status = USBD_CANCELLED;
	usb_transfer_complete(xfer);
}

static usbd_status
pipe_start(struct usbd_xfer *xfer)
{
	assert(owned && xfer == &queued);
	assert(xfer->ux_status == USBD_NOT_STARTED);
	xfer->ux_status = USBD_IN_PROGRESS;
	starts++;
	return USBD_IN_PROGRESS;
}

#include "usbd_start_next.h"
#include "usbd_ar_pipe.h"
#include "usbd_suspend_pipe.h"
#include "usbd_resume_pipe.h"
#include "usbd_abort_pipe.h"
#include "usbd_abort_default_pipe.h"
#include "usbd_abort_default_request.h"

static int
abort_ioctl(int endpt, int flag)
{
	struct ugen_softc *sc = &ugen;
	(void)flag;
	switch (USB_ABORT_REQUEST) {
#include "abort-case.h"
	default:
		return EINVAL;
	}
	return 0;
}

static int
request_ioctl(int request_flags, int flag)
{
	const struct { int ucr_flags; } request = { request_flags }, *ur = &request;
	(void)ur;
#include "request-flags.h"
	return 0;
}

static const struct usbd_bus_methods bus_methods = { hardware_abort };
static const struct usbd_pipe_methods pipe_methods = { pipe_abort, pipe_start };

static void
init(void)
{
	test_bus.ub_lock = &bus_lock;
	test_bus.ub_methods = &bus_methods;
	test_device.ud_bus = &test_bus;
	test_device.ud_pipe0 = &test_pipe;
	test_pipe.up_dev = &test_device;
	test_pipe.up_methods = &pipe_methods;
	test_pipe.up_serialise = true;
	ugen.sc_udev = &test_device;
	active.ux_bus = queued.ux_bus = &test_bus;
	active.ux_pipe = queued.ux_pipe = &test_pipe;
	assert(pthread_cond_init(&test_pipe.up_callingcv, NULL) == 0);
	assert(pthread_cond_init(&active.ux_cv, NULL) == 0);
	assert(pthread_cond_init(&queued.ux_cv, NULL) == 0);
}

static void
reset(bool with_queue)
{
	assert(test_pipe.up_abortlwp == NULL);
	aborts = starts = completions = sync_wakeups = owner_waiters = 0;
	pause_abort = abort_entered = abort_release = late_claim = false;
	test_pipe.up_running = true;
	test_pipe.up_aborting = false;
	test_pipe.up_queue.first = &active;
	active.ux_next = with_queue ? &queued : NULL;
	active.ux_status = USBD_IN_PROGRESS;
	active.ux_done = false;
	queued.ux_next = NULL;
	queued.ux_status = USBD_NOT_STARTED;
	queued.ux_done = false;
}

static void *
request_abort(void *unused)
{
	(void)unused;
	assert(abort_ioctl(USB_CONTROL_ENDPOINT, FWRITE) == 0);
	return NULL;
}

static void *
pipe_abort_thread(void *unused)
{
	(void)unused;
	usbd_abort_default_pipe(&test_device);
	return NULL;
}

static void *
sync_waiter(void *unused)
{
	(void)unused;
	mutex_enter(&bus_lock);
	while (!queued.ux_done)
		cv_wait(&queued.ux_cv, &bus_lock);
	assert(queued.ux_status == USBD_NORMAL_COMPLETION);
	sync_wakeups++;
	mutex_exit(&bus_lock);
	return NULL;
}

static void
concurrent_aborts(bool whole_pipe_first, bool whole_pipe_second, bool late)
{
	pthread_t first, second;
	reset(!whole_pipe_first && !whole_pipe_second);
	late_claim = late;
	pause_abort = true;
	assert(pthread_create(&first, NULL,
	    whole_pipe_first ? pipe_abort_thread : request_abort, NULL) == 0);
	assert(pthread_mutex_lock(&gate) == 0);
	while (!abort_entered)
		assert(pthread_cond_wait(&gate_cv, &gate) == 0);
	assert(pthread_mutex_unlock(&gate) == 0);
	assert(pthread_create(&second, NULL,
	    whole_pipe_second ? pipe_abort_thread : request_abort, NULL) == 0);
	assert(pthread_mutex_lock(&gate) == 0);
	while (!owner_waiters)
		assert(pthread_cond_wait(&gate_cv, &gate) == 0);
	abort_release = true;
	assert(pthread_cond_broadcast(&gate_cv) == 0);
	assert(pthread_mutex_unlock(&gate) == 0);
	assert(pthread_join(first, NULL) == 0);
	assert(pthread_join(second, NULL) == 0);
	assert(aborts == 1 && completions == 1);
	assert(active.ux_done && active.ux_status == USBD_CANCELLED);
	assert(test_pipe.up_abortlwp == NULL);
	if (!whole_pipe_first && !whole_pipe_second) {
		assert(starts == 1 && queued.ux_status == USBD_IN_PROGRESS);
		assert(test_pipe.up_queue.first == &queued && !queued.ux_done);
	}
}

static void
queued_request(void)
{
	pthread_t waiter;
	reset(true);
	assert(pthread_create(&waiter, NULL, sync_waiter, NULL) == 0);
	assert(abort_ioctl(USB_CONTROL_ENDPOINT, FWRITE) == 0);
	assert(aborts == 1 && active.ux_done && !queued.ux_done);
	assert(starts == 1 && test_pipe.up_queue.first == &queued);
	assert(queued.ux_status == USBD_IN_PROGRESS);
	mutex_enter(&bus_lock);
	queued.ux_status = USBD_NORMAL_COMPLETION;
	usb_transfer_complete(&queued);
	mutex_exit(&bus_lock);
	assert(pthread_join(waiter, NULL) == 0);
	assert(sync_wakeups == 1 && completions == 2);
}

static void
no_access(void)
{
	reset(false);
	assert(abort_ioctl(USB_CONTROL_ENDPOINT, 0) == EPERM);
	assert(aborts == 0 && active.ux_status == USBD_IN_PROGRESS);
	assert(abort_ioctl(1, FWRITE) == EINVAL);
	assert(aborts == 0);
}

static void
inactive(void)
{
	const int statuses[] = {USBD_NOT_STARTED, USBD_CANCELLED, USBD_TIMEOUT,
	    USBD_NORMAL_COMPLETION, USBD_STALLED};
	unsigned i;
	reset(false);
	test_pipe.up_queue.first = NULL;
	assert(abort_ioctl(USB_CONTROL_ENDPOINT, FWRITE) == 0);
	assert(aborts == 0);
	for (i = 0; i < sizeof(statuses) / sizeof(statuses[0]); i++) {
		reset(false);
		active.ux_status = statuses[i];
		assert(abort_ioctl(USB_CONTROL_ENDPOINT, FWRITE) == 0);
		assert(aborts == 0 && active.ux_status == statuses[i]);
		assert(test_pipe.up_queue.first == &active);
	}
}

static void
request_flags(void)
{
	assert(request_ioctl(0, FWRITE) == 0);
	assert(request_ioctl(USBD_SHORT_XFER_OK, FWRITE) == 0);
	assert(request_ioctl(0x10, FWRITE) == EINVAL);
	assert(request_ioctl(0x10 | USBD_SHORT_XFER_OK, FWRITE) == EINVAL);
	assert(request_ioctl(0x10000, FWRITE) == EINVAL);
	assert(request_ioctl(-1, FWRITE) == EINVAL);
	assert(request_ioctl(0, 0) == EPERM);
}

int
main(int argc, char **argv)
{
	const char *which = argc == 2 ? argv[1] : "all";
	alarm(15);
	init();
	if (!strcmp(which, "all") || !strcmp(which, "noaccess"))
		no_access();
	if (!strcmp(which, "all") || !strcmp(which, "queued"))
		queued_request();
	if (!strcmp(which, "all") || !strcmp(which, "concurrent")) {
		concurrent_aborts(false, false, false);
		concurrent_aborts(false, false, true);
		concurrent_aborts(true, false, true);
		concurrent_aborts(false, true, true);
	}
	if (!strcmp(which, "all") || !strcmp(which, "inactive"))
		inactive();
	if (!strcmp(which, "all") || !strcmp(which, "flags"))
		request_flags();
	assert(pthread_cond_destroy(&test_pipe.up_callingcv) == 0);
	assert(pthread_cond_destroy(&active.ux_cv) == 0);
	assert(pthread_cond_destroy(&queued.ux_cv) == 0);
	assert(pthread_mutex_destroy(&bus_lock) == 0);
	puts("USB_ABORT_REQUEST: PASS");
	return 0;
}
