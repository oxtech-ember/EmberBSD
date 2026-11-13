// SPDX-License-Identifier: BSD-2-Clause
// Origin: EmberBSD contributors (AI-assisted).
// Actual NetBSD ww_mutex and rbtree source; pthreads model kernel primitives.
// This bridge does not model kernel scheduler, LOCKDEBUG or real signals.
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#ifndef __BEGIN_DECLS
#define __BEGIN_DECLS
#define __END_DECLS
#endif
#define __KERNEL_RCSID(a, b)
#ifndef __unused
#define __unused __attribute__((unused))
#endif
#define __diagused __attribute__((unused))
#define __predict_false(x) __builtin_expect(!!(x), 0)
#define ERESTART 85
#define _KERNEL 1
#define CHECK(x)                                                                       \
	do {                                                                           \
		if (!(x)) {                                                            \
			fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__,         \
				__LINE__, #x);                                         \
			abort();                                                       \
		}                                                                      \
	} while (0)
// Kernel assertions and harness checks remain active regardless of NDEBUG.
#define KASSERT(x) CHECK(x)
#define KASSERTMSG(x, ...) CHECK(x)
#define ASSERT_SLEEPABLE() ((void)0)
#define LOCKDEBUG_WANTLOCK(...) ((void)0)
#define LOCKDEBUG_LOCKED(...) ((void)0)
#define LOCKDEBUG_UNLOCKED(...) ((void)0)
#define MUTEX_DEFAULT 0
#define IPL_VM 0
#define panic(...) abort()
typedef _Atomic uint64_t atomic64_t;
#define ATOMIC64_INIT(x) (x)
#define atomic64_inc_return(x) (atomic_fetch_add((x), 1) + 1)
struct lwp;
typedef struct {
	pthread_mutex_t lock;
	_Atomic(struct lwp *) owner;
} kmutex_t;
typedef pthread_cond_t kcondvar_t;
struct lwp {
	atomic_bool signal;
	atomic_bool gated;
};
static _Thread_local struct lwp current_lwp;
static _Atomic(struct lwp *) broadcast_signal;
#define curlwp (&current_lwp)
static void
mutex_init(kmutex_t *m, int t, int i)
{
	atomic_init(&m->owner, NULL);
	CHECK(!pthread_mutex_init(&m->lock, NULL));
}
static void
mutex_destroy(kmutex_t *m)
{
	CHECK(atomic_load(&m->owner) == NULL);
	CHECK(!pthread_mutex_destroy(&m->lock));
}
static void
mutex_enter(kmutex_t *m)
{
	CHECK(atomic_load(&m->owner) != curlwp);
	CHECK(!pthread_mutex_lock(&m->lock));
	CHECK(atomic_exchange(&m->owner, curlwp) == NULL);
}
static void
mutex_exit(kmutex_t *m)
{
	CHECK(atomic_exchange(&m->owner, NULL) == curlwp);
	CHECK(!pthread_mutex_unlock(&m->lock));
}
static bool
mutex_owned(kmutex_t *m)
{
	return atomic_load(&m->owner) == curlwp;
}
static void
cv_init(kcondvar_t *c, const char *s)
{
	CHECK(!pthread_cond_init(c, NULL));
}
static void
cv_destroy(kcondvar_t *c)
{
	CHECK(!pthread_cond_destroy(c));
}
static void
cv_broadcast(kcondvar_t *c)
{
	struct lwp *l = atomic_exchange(&broadcast_signal, NULL);
	if (l)
		atomic_store(&l->signal, true);
	CHECK(!pthread_cond_broadcast(c));
}
static void
cv_wait(kcondvar_t *c, kmutex_t *m)
{
	CHECK(atomic_exchange(&m->owner, NULL) == curlwp);
	CHECK(!pthread_cond_wait(c, &m->lock));
	CHECK(atomic_exchange(&m->owner, curlwp) == NULL);
	/* Delay reacquisition to choose a wakeup order without sleeps. */
	if (atomic_load(&curlwp->gated)) {
		mutex_exit(m);
		while (atomic_load(&curlwp->gated))
			sched_yield();
		mutex_enter(m);
	}
}
static int
cv_wait_sig(kcondvar_t *c, kmutex_t *m)
{
	cv_wait(c, m);
	return atomic_exchange(&curlwp->signal, false) ? EINTR : 0;
}
#include "rbtree-body.h"
#include "rb-body.h"
#include "ww-header-body.h"
#include "ww-body.h"

struct fixture {
	struct ww_class cls;
	struct ww_mutex m;
	atomic_int phase;
	atomic_int done;
	atomic_int result;
	_Atomic(struct lwp *) target;
	_Atomic(struct lwp *) noctx_lwp;
	atomic_int winner;
	bool force_order;
	bool noctx_first;
	int interruptible;
};
static void
init(struct fixture *f)
{
	atomic_init(&f->cls.wwc_ticket, 0);
	atomic_init(&f->phase, 0);
	atomic_init(&f->done, 0);
	atomic_init(&f->result, 999);
	atomic_init(&f->target, NULL);
	atomic_init(&f->noctx_lwp, NULL);
	atomic_init(&f->winner, 0);
	f->force_order = false;
	f->noctx_first = false;
	ww_mutex_init(&f->m, &f->cls);
}
static void
wait_atomic(atomic_int *value, int expected)
{
	while (atomic_load(value) != expected)
		sched_yield();
}
static void
wait_state(struct fixture *f, int state)
{
	for (;;) {
		mutex_enter(&f->m.wwm_lock);
		bool found = f->m.wwm_state == state;
		mutex_exit(&f->m.wwm_lock);
		if (found)
			return;
		sched_yield();
	}
}
static void
wait_tree(struct fixture *f)
{
	for (;;) {
		mutex_enter(&f->m.wwm_lock);
		bool found = RB_TREE_MIN(&f->m.wwm_waiters) != NULL;
		mutex_exit(&f->m.wwm_lock);
		if (found)
			return;
		sched_yield();
	}
}
static void *
noctx(void *arg)
{
	struct fixture *f = arg;
	atomic_store(&f->noctx_lwp, curlwp);
	CHECK(ww_mutex_lock(&f->m, NULL) == 0);
	if (f->force_order) {
		int expected = 0;
		atomic_compare_exchange_strong(&f->winner, &expected, 2);
		if (f->noctx_first)
			atomic_store(&atomic_load(&f->target)->gated, false);
	}
	ww_mutex_unlock(&f->m);
	atomic_fetch_add(&f->done, 1);
	return NULL;
}
static void
dup_case(int irq, int done, int with_waiter, int trylock)
{
	struct fixture f;
	init(&f);
	struct ww_acquire_ctx ctx;
	ww_acquire_init(&ctx, &f.cls);
	CHECK(ww_mutex_lock(&f.m, &ctx) == 0);
	if (done)
		ww_acquire_done(&ctx);
	pthread_t c;
	if (with_waiter) {
		CHECK(!pthread_create(&c, NULL, noctx, &f));
		wait_state(&f, WW_WANTOWN);
	}
	if (trylock)
		CHECK(ww_mutex_trylock(&f.m) == 0);
	else
		CHECK((irq ? ww_mutex_lock_interruptible(&f.m, &ctx)
			   : ww_mutex_lock(&f.m, &ctx)) == -EALREADY);
	CHECK(ctx.wwx_acquired == 1);
	ww_mutex_unlock(&f.m);
	if (with_waiter)
		CHECK(!pthread_join(c, NULL));
	ww_acquire_fini(&ctx);
	ww_mutex_destroy(&f.m);
}
static void *
old_ctx(void *arg)
{
	struct fixture *f = arg;
	struct ww_acquire_ctx ctx;
	ww_acquire_init(&ctx, &f->cls);
	atomic_store(&f->target, curlwp);
	atomic_store(&f->phase, 1);
	wait_atomic(&f->phase, 2);
	int r = f->interruptible ? ww_mutex_lock_interruptible(&f->m, &ctx)
				 : ww_mutex_lock(&f->m, &ctx);
	CHECK(r == 0);
	CHECK(ctx.wwx_acquired == 1);
	if (f->force_order) {
		int expected = 0;
		atomic_compare_exchange_strong(&f->winner, &expected, 1);
		if (!f->noctx_first)
			atomic_store(&atomic_load(&f->noctx_lwp)->gated, false);
	}
	ww_mutex_unlock(&f->m);
	ww_acquire_fini(&ctx);
	atomic_fetch_add(&f->done, 1);
	return NULL;
}
static void
lost_case(int irq, bool noctx_first, bool signal_at_wake)
{
	struct fixture f;
	init(&f);
	f.interruptible = irq;
	f.force_order = true;
	f.noctx_first = noctx_first;
	pthread_t b, c;
	CHECK(!pthread_create(&b, NULL, old_ctx, &f));
	wait_atomic(&f.phase, 1);
	struct ww_acquire_ctx a;
	ww_acquire_init(&a, &f.cls);
	CHECK(ww_mutex_lock(&f.m, &a) == 0);
	atomic_store(&f.phase, 2);
	wait_tree(&f);
	CHECK(!pthread_create(&c, NULL, noctx, &f));
	wait_state(&f, WW_WANTOWN);
	mutex_enter(&f.m.wwm_lock);
	struct lwp *delayed = atomic_load(noctx_first ? &f.target : &f.noctx_lwp);
	CHECK(delayed != NULL);
	atomic_store(&delayed->gated, true);
	if (signal_at_wake)
		atomic_store(&broadcast_signal, atomic_load(&f.target));
	mutex_exit(&f.m.wwm_lock);
	ww_mutex_unlock(&f.m);
	ww_acquire_fini(&a);
	CHECK(!pthread_join(b, NULL));
	CHECK(!pthread_join(c, NULL));
	CHECK(atomic_load(&f.done) == 2);
	CHECK(atomic_load(&f.winner) == (noctx_first ? 2 : 1));
	CHECK(RB_TREE_MIN(&f.m.wwm_waiters) == NULL);
	ww_mutex_destroy(&f.m);
}
static void *
younger_ctx(void *arg)
{
	struct fixture *f = arg;
	struct ww_acquire_ctx c;
	ww_acquire_init(&c, &f->cls);
	int result = f->interruptible ? ww_mutex_lock_interruptible(&f->m, &c)
				      : ww_mutex_lock(&f->m, &c);
	atomic_store(&f->result, result);
	if (result == 0)
		ww_mutex_unlock(&f->m);
	ww_acquire_fini(&c);
	return NULL;
}
static void
priority_case(int irq)
{
	struct fixture f;
	init(&f);
	f.interruptible = irq;
	struct ww_acquire_ctx a;
	ww_acquire_init(&a, &f.cls);
	CHECK(ww_mutex_lock(&f.m, &a) == 0);
	pthread_t c, b;
	CHECK(!pthread_create(&c, NULL, noctx, &f));
	wait_state(&f, WW_WANTOWN);
	CHECK(!pthread_create(&b, NULL, younger_ctx, &f));
	CHECK(!pthread_join(b, NULL));
	CHECK(atomic_load(&f.result) == -EDEADLK);
	CHECK(a.wwx_acquired == 1);
	ww_mutex_unlock(&f.m);
	ww_acquire_fini(&a);
	CHECK(!pthread_join(c, NULL));
	ww_mutex_destroy(&f.m);
}
static void *
signal_waiter(void *arg)
{
	struct fixture *f = arg;
	struct ww_acquire_ctx c;
	ww_acquire_init(&c, &f->cls);
	atomic_store(&f->target, curlwp);
	atomic_store(&f->phase, 1);
	wait_atomic(&f->phase, 2);
	int result = ww_mutex_lock_interruptible(&f->m, &c);
	atomic_store(&f->result, result);
	if (result == 0) {
		CHECK(c.wwx_acquired == 1);
		ww_mutex_unlock(&f->m);
	} else
		CHECK(c.wwx_acquired == 0);
	ww_acquire_fini(&c);
	return NULL;
}
static void
signal_case(bool handoff)
{
	struct fixture f;
	init(&f);
	pthread_t b;
	CHECK(!pthread_create(&b, NULL, signal_waiter, &f));
	wait_atomic(&f.phase, 1);
	struct ww_acquire_ctx a;
	ww_acquire_init(&a, &f.cls);
	CHECK(ww_mutex_lock(&f.m, &a) == 0);
	atomic_store(&f.phase, 2);
	wait_tree(&f);
	mutex_enter(&f.m.wwm_lock);
	struct lwp *target = atomic_load(&f.target);
	CHECK(target);
	if (handoff)
		atomic_store(&broadcast_signal, target);
	else {
		atomic_store(&target->signal, true);
		cv_broadcast(&f.m.wwm_cv);
	}
	mutex_exit(&f.m.wwm_lock);
	if (handoff) {
		ww_mutex_unlock(&f.m);
		ww_acquire_fini(&a);
		CHECK(!pthread_join(b, NULL));
		CHECK(atomic_load(&f.result) == 0);
	} else {
		CHECK(!pthread_join(b, NULL));
		CHECK(atomic_load(&f.result) == -EINTR);
		ww_mutex_unlock(&f.m);
		ww_acquire_fini(&a);
	}
	CHECK(RB_TREE_MIN(&f.m.wwm_waiters) == NULL);
	ww_mutex_destroy(&f.m);
}
struct order_fixture {
	struct ww_class cls;
	struct ww_mutex m;
	atomic_int phase[2];
	atomic_int order;
	int seen[2];
	int irq;
};
struct order_arg {
	struct order_fixture *f;
	int id;
};
static void *
order_waiter(void *arg)
{
	struct order_arg *v = arg;
	struct order_fixture *f = v->f;
	struct ww_acquire_ctx c;
	ww_acquire_init(&c, &f->cls);
	atomic_store(&f->phase[v->id], 1);
	wait_atomic(&f->phase[v->id], 2);
	int r =
	    f->irq ? ww_mutex_lock_interruptible(&f->m, &c) : ww_mutex_lock(&f->m, &c);
	CHECK(r == 0);
	f->seen[atomic_fetch_add(&f->order, 1)] = v->id;
	ww_mutex_unlock(&f->m);
	ww_acquire_fini(&c);
	return NULL;
}
static void *
order_noctx(void *arg)
{
	struct order_fixture *f = arg;
	CHECK(ww_mutex_lock(&f->m, NULL) == 0);
	ww_mutex_unlock(&f->m);
	return NULL;
}
static void
order_case(int irq)
{
	struct order_fixture f;
	atomic_init(&f.cls.wwc_ticket, 0);
	atomic_init(&f.phase[0], 0);
	atomic_init(&f.phase[1], 0);
	atomic_init(&f.order, 0);
	f.irq = irq;
	ww_mutex_init(&f.m, &f.cls);
	struct order_arg x[2] = {{&f, 0}, {&f, 1}};
	pthread_t b[2];
	CHECK(!pthread_create(&b[0], NULL, order_waiter, &x[0]));
	wait_atomic(&f.phase[0], 1);
	CHECK(!pthread_create(&b[1], NULL, order_waiter, &x[1]));
	wait_atomic(&f.phase[1], 1);
	struct ww_acquire_ctx owner;
	ww_acquire_init(&owner, &f.cls);
	CHECK(ww_mutex_lock(&f.m, &owner) == 0);
	for (int i = 0; i < 2; i++) {
		atomic_store(&f.phase[i], 2);
		for (;;) {
			mutex_enter(&f.m.wwm_lock);
			struct ww_acquire_ctx *min = RB_TREE_MIN(&f.m.wwm_waiters);
			struct ww_acquire_ctx *max = RB_TREE_MAX(&f.m.wwm_waiters);
			bool ready = min && (i == 0 || min != max);
			mutex_exit(&f.m.wwm_lock);
			if (ready)
				break;
			sched_yield();
		}
	}
	pthread_t c;
	CHECK(!pthread_create(&c, NULL, order_noctx, &f));
	for (;;) {
		mutex_enter(&f.m.wwm_lock);
		bool ready = f.m.wwm_state == WW_WANTOWN;
		mutex_exit(&f.m.wwm_lock);
		if (ready)
			break;
		sched_yield();
	}
	ww_mutex_unlock(&f.m);
	ww_acquire_fini(&owner);
	for (int i = 0; i < 2; i++)
		CHECK(!pthread_join(b[i], NULL));
	CHECK(!pthread_join(c, NULL));
	CHECK(f.seen[0] == 0 && f.seen[1] == 1);
	CHECK(RB_TREE_MIN(&f.m.wwm_waiters) == NULL);
	ww_mutex_destroy(&f.m);
}
static void
priority_normal(void)
{
	priority_case(0);
}
static void
priority_interruptible(void)
{
	priority_case(1);
}
static void
signal_before(void)
{
	signal_case(false);
}
static void
signal_handoff(void)
{
	signal_case(true);
}
static void
order_normal(void)
{
	order_case(0);
}
static void
order_interruptible(void)
{
	order_case(1);
}

static int failures;
static void
run_case(const char *name, void (*fn)(void))
{
	pid_t p = fork();
	CHECK(p >= 0);
	if (!p) {
		alarm(3);
		fn();
		_exit(0);
	}
	int s;
	CHECK(waitpid(p, &s, 0) == p);
	if (!WIFEXITED(s) || WEXITSTATUS(s)) {
		fprintf(stderr, "FAIL %s status=%d\n", name, s);
		failures++;
		return;
	}
	printf("PASS %s\n", name);
	fflush(stdout);
}
#define DUP(N, I, D, W, T) \
	static void N(void) { dup_case(I, D, W, T); }

/* clang-format off */
DUP(dup_plain, 0, 0, 0, 0)
DUP(dup_plain_done, 0, 1, 0, 0)
DUP(dup_int_plain, 1, 0, 0, 0)
DUP(dup_int_plain_done, 1, 1, 0, 0)
DUP(dup_want, 0, 0, 1, 0)
DUP(dup_want_done, 0, 1, 1, 0)
DUP(dup_int, 1, 0, 1, 0)
DUP(dup_int_done, 1, 1, 1, 0)
DUP(try_want, 0, 0, 1, 1)
/* clang-format on */

static void
lost_normal(void)
{
	lost_case(0, true, false);
}
static void
lost_interruptible(void)
{
	lost_case(1, true, false);
}
static void
lost_ctx_first(void)
{
	lost_case(0, false, false);
}
static void
lost_interruptible_ctx_first(void)
{
	lost_case(1, false, false);
}
static void
lost_signal_at_claim(void)
{
	lost_case(1, false, true);
}

int
main(void)
{
	run_case("dup plain", dup_plain);
	run_case("dup plain done", dup_plain_done);
	run_case("dup interruptible plain", dup_int_plain);
	run_case("dup interruptible plain done", dup_int_plain_done);
	run_case("try WANTOWN", try_want);
	run_case("dup WANTOWN", dup_want);
	run_case("dup WANTOWN done", dup_want_done);
	run_case("dup interruptible", dup_int);
	run_case("dup interruptible done", dup_int_done);
	run_case("lost waiter", lost_normal);
	run_case("lost waiter interruptible", lost_interruptible);
	run_case("context wins wakeup", lost_ctx_first);
	run_case("interruptible context wins wakeup", lost_interruptible_ctx_first);
	run_case("signal with unowned acquisition", lost_signal_at_claim);
	run_case("priority normal", priority_normal);
	run_case("priority interruptible", priority_interruptible);
	run_case("multiple waiters", order_normal);
	run_case("multiple waiters interruptible", order_interruptible);
	run_case("signal before handoff", signal_before);
	run_case("signal with handoff", signal_handoff);
	return failures ? 1 : 0;
}
