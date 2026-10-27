/* SPDX-License-Identifier: BSD-2-Clause */
/* Kernel scheduling primitives are replaced; all dispatcher code is real. */
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <sys/queue.h>
#include "autoconf-test-lwp.h"

struct test_device {
	struct test_device *dv_parent;
	unsigned dv_pending, calls;
	bool entered, release;
	int self_error;
};
typedef struct test_device *device_t;
typedef pthread_mutex_t kmutex_t;
typedef pthread_cond_t kcondvar_t;
static _Thread_local bool config_lock_owned;
#define KM_SLEEP 0
#define KASSERT assert
#define KERNEL_LOCKED_P() 1

kmutex_t config_misc_lock = PTHREAD_MUTEX_INITIALIZER;
kcondvar_t config_misc_cv = PTHREAD_COND_INITIALIZER;
bool root_is_mounted;
static atomic_uint allocations;
static unsigned pending_decrements;

static void
mutex_enter(kmutex_t *lock)
{
	int error = pthread_mutex_lock(lock);

	assert(error == 0);
	if (lock == &config_misc_lock)
		config_lock_owned = true;
}

static void
mutex_exit(kmutex_t *lock)
{
	int error;

	if (lock == &config_misc_lock)
		config_lock_owned = false;
	error = pthread_mutex_unlock(lock);
	assert(error == 0);
}

static void *
kmem_alloc(size_t size, int flags)
{
	void *p;

	(void)flags;
	p = calloc(1, size);
	assert(p != NULL);
	atomic_fetch_add(&allocations, 1);
	return p;
}

static void
kmem_free(void *p, size_t size)
{

	(void)size;
	/* Reclaim may block; unrelated autoconfiguration must remain available. */
	assert(!config_lock_owned);
	free(p);
	assert(atomic_fetch_sub(&allocations, 1) != 0);
}

static void
panic(const char *message, ...)
{

	(void)message;
	abort();
}

static void
config_pending_decr(device_t dev)
{

	assert(dev->dv_pending != 0);
	dev->dv_pending--;
	pending_decrements++;
}

#ifndef TEST_PENDING_ONLY
static pthread_mutex_t state_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t state_cv = PTHREAD_COND_INITIALIZER;
static unsigned cancel_waiters;
static bool cancel_done;
static int cancel_result;
#endif

void
cv_broadcast(kcondvar_t *cv)
{
	int error = pthread_cond_broadcast(cv);

	assert(error == 0);
}

void
cv_wait(kcondvar_t *cv, kmutex_t *lock)
{
	int error;

#ifndef TEST_PENDING_ONLY
	mutex_enter(&state_lock);
	cancel_waiters++;
	cv_broadcast(&state_cv);
	mutex_exit(&state_lock);
#endif
	error = pthread_cond_wait(cv, lock);
	assert(error == 0);
}

void
test_kthread_exit(void *cookie, int status)
{

	(void)cookie;
	assert(status == 0);
	pthread_exit(NULL);
}
#define kthread_exit(status) test_kthread_exit(cookie, status)

#include "production.inc"

static void
count_callback(device_t dev)
{

	dev->calls++;
}

static void
pending_contract(void)
{
	struct test_device root_dev = { 0 }, ordinary = { .dv_pending = 1 };
	struct deferred_config *dc;

	root_is_mounted = false;
	config_mountroot(&root_dev, count_callback);
	config_process_deferred(&mountroot_config_queue, NULL);
	assert(root_dev.calls == 1 && root_dev.dv_pending == 0);
	assert(pending_decrements == 0);
	/* Other deferred queues still consume their owner's pending reference. */
	dc = kmem_alloc(sizeof(*dc), KM_SLEEP);
	dc->dc_dev = &ordinary;
	dc->dc_func = count_callback;
	TAILQ_INSERT_TAIL(&deferred_config_queue, dc, dc_queue);
	config_process_deferred(&deferred_config_queue, NULL);
	assert(ordinary.calls == 1 && ordinary.dv_pending == 0);
	assert(pending_decrements == 1 && atomic_load(&allocations) == 0);
	puts("mountroot pending count and ordinary deferred references: PASS");
}

#ifndef TEST_PENDING_ONLY
static void
blocking_callback(device_t dev)
{

	mutex_enter(&state_lock);
	dev->calls++;
	dev->entered = true;
	cv_broadcast(&state_cv);
	while (!dev->release) {
		int error = pthread_cond_wait(&state_cv, &state_lock);

		assert(error == 0);
	}
	mutex_exit(&state_lock);
}

static void *
run_worker(void *arg)
{

	(void)arg;
	config_mountroot_thread(NULL);
	return NULL;
}

static void *
run_registration(void *arg)
{

	config_mountroot(arg, blocking_callback);
	return NULL;
}

static void *
run_deferred(void *arg)
{

	(void)arg;
	config_process_deferred(&mountroot_config_queue, NULL);
	return NULL;
}

static void *
run_cancel(void *arg)
{
	int result = config_mountroot_cancel(arg);

	mutex_enter(&state_lock);
	cancel_result = result;
	cancel_done = true;
	cv_broadcast(&state_cv);
	mutex_exit(&state_lock);
	return NULL;
}

static void
wait_state(bool *flag)
{

	mutex_enter(&state_lock);
	while (!*flag) {
		int error = pthread_cond_wait(&state_cv, &state_lock);

		assert(error == 0);
	}
	mutex_exit(&state_lock);
}

static void
release_callback(device_t dev)
{

	mutex_enter(&state_lock);
	dev->release = true;
	cv_broadcast(&state_cv);
	mutex_exit(&state_lock);
}

static void
join_thread(pthread_t thread)
{
	int error = pthread_join(thread, NULL);

	assert(error == 0);
}

static void
queued_cancel(void)
{
	device_t removed = calloc(1, sizeof(*removed));
	struct test_device survivor = { 0 };
	pthread_t worker;

	assert(removed != NULL);
	root_is_mounted = false;
	config_mountroot(removed, count_callback);
	config_mountroot(&survivor, count_callback);
	assert(config_mountroot_cancel(removed) == 0);
	assert(removed->calls == 0);
	free(removed); /* ASan detects any late callback into the released device. */
	assert(pthread_create(&worker, NULL, run_worker, NULL) == 0);
	join_thread(worker);
	assert(survivor.calls == 1 && atomic_load(&allocations) == 0);
	puts("queued cancellation leaves unrelated callbacks and no late access: PASS");
}

static void
running_cancel(unsigned mode)
{
	struct test_device first = { 0 }, other = { 0 };
	pthread_t worker1, worker2, canceler;

	cancel_waiters = 0; cancel_done = false;
	root_is_mounted = mode == 1;
	if (mode == 1) {
		assert(pthread_create(&worker1, NULL, run_registration, &first) == 0);
		assert(pthread_create(&worker2, NULL, run_registration, &other) == 0);
	} else {
		void *(*dispatch)(void *) = mode == 2 ? run_deferred : run_worker;

		config_mountroot(&first, blocking_callback);
		config_mountroot(&other, blocking_callback);
		root_is_mounted = true;
		assert(pthread_create(&worker1, NULL, dispatch, NULL) == 0);
		assert(pthread_create(&worker2, NULL, dispatch, NULL) == 0);
	}
	wait_state(&first.entered);
	wait_state(&other.entered);
	assert(pthread_create(&canceler, NULL, run_cancel, &first) == 0);
	mutex_enter(&state_lock);
	while (cancel_waiters == 0 && !cancel_done) {
		int error = pthread_cond_wait(&state_cv, &state_lock);

		assert(error == 0);
	}
	assert(!cancel_done); /* Cancellation must drain the active callback. */
	mutex_exit(&state_lock);
	release_callback(&first);
	join_thread(canceler);
	assert(cancel_result == 0 && !other.release);
	release_callback(&other);
	join_thread(worker1);
	join_thread(worker2);
	assert(first.calls == 1 && other.calls == 1);
	assert(atomic_load(&allocations) == 0);
	puts(mode == 1 ? "inline callback drain: PASS" : mode == 2 ?
	    "generic-dispatch callback drain: PASS" : "two-worker callback drain is per-device: PASS");
}

static void
self_callback(device_t dev)
{

	dev->calls++;
	dev->self_error = config_mountroot_cancel(dev);
}

static void
self_cancel(void)
{
	struct test_device dev = { 0 };

	root_is_mounted = true;
	config_mountroot(&dev, self_callback);
	assert(dev.calls == 1 && dev.self_error == EDEADLK);
	assert(config_mountroot_cancel(&dev) == 0);
	assert(atomic_load(&allocations) == 0);
	puts("self cancellation reports deadlock without leaking state: PASS");
}
#endif

int
main(void)
{

	pending_contract();
#ifndef TEST_PENDING_ONLY
	queued_cancel();
	running_cancel(0);
	running_cancel(1);
	running_cancel(2);
	self_cancel();
#endif
	return 0;
}
