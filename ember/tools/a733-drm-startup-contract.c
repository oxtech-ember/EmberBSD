/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD, AI-assisted. Actual startup ownership and retry tests. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static unsigned checks;
#define CHECK(c) do { checks++; if (!(c)) { \
    fprintf(stderr, "%d: %s\n", __LINE__, #c); abort(); } } while (0)
#define container_of(p, t, m) ((t *)((char *)(p) - offsetof(t, m)))
#define MUTEX_DEFAULT 0
#define IPL_SOFTCLOCK 1
#define PRI_NONE 0
#define WQ_MPSAFE 1
#define CALLOUT_MPSAFE 1
static const int hz = 100;
typedef struct { bool held; } kmutex_t;
typedef void *device_t;
struct work { int identity; };
struct timer { void (*fn)(void *); void *arg; bool pending; };
struct queue { void (*fn)(struct work *, void *); void *arg; };
struct a733_drm {
    device_t device;
    kmutex_t lock, startup_lock;
    struct timer startup_timer;
    struct work startup_work;
    struct queue *queue;
    int phandle, fault, stage;
    bool registering;
    bool startup_queued, startup_running, startup_registered, startup_stopped;
};
struct sysctlnode { void *sysctl_data; size_t sysctl_size; };
#define SYSCTLFN_ARGS const struct sysctlnode *rnode, const int *newp
#define SYSCTLFN_CALL(n) (n), newp
static struct a733_drm *current;
static struct queue queue;
static struct work *pending;
static void (*root_callback)(device_t);
static unsigned enqueues, timers, starts, registrations, commits, creates;
static int sink_error, registration_error, queue_error;
static bool tree_locked, inject_callback, inject_sysctl;
static int sysctl_result;
static int a733_stage(SYSCTLFN_ARGS);
static void a733_startup_enqueue(void *);
static void mutex_init(kmutex_t *m, int type, int ipl)
{ CHECK(type == MUTEX_DEFAULT && ipl == IPL_SOFTCLOCK); m->held = false; }
static void mutex_enter(kmutex_t *m)
{ CHECK(!m->held); m->held = true; }
static void mutex_exit(kmutex_t *m)
{ CHECK(m->held); m->held = false; }
#define mutex_spin_enter mutex_enter
#define mutex_spin_exit mutex_exit
static void callout_init(struct timer *t, int flags)
{ CHECK(flags == CALLOUT_MPSAFE); memset(t, 0, sizeof(*t)); }
static void callout_setfunc(struct timer *t, void (*fn)(void *), void *arg)
{ t->fn = fn; t->arg = arg; }
static void callout_schedule(struct timer *t, int ticks)
{
    CHECK(t == &current->startup_timer && ticks == hz);
    CHECK(!current->lock.held && !current->startup_lock.held);
    t->pending = true; timers++;
}
static void config_mountroot(device_t self, void (*fn)(device_t))
{ CHECK(self == current && root_callback == NULL); root_callback = fn; }
static void *device_private(device_t self) { return self; }
static int workqueue_create(struct queue **out, const char *name,
    void (*fn)(struct work *, void *), void *arg, int pri, int ipl, int flags)
{
    CHECK(strcmp(name, "a733kms") == 0 && arg == current);
    CHECK(pri == PRI_NONE && ipl == IPL_SOFTCLOCK && flags == WQ_MPSAFE);
    creates++;
    if (queue_error) return queue_error;
    queue = (struct queue){fn, arg}; *out = &queue; return 0;
}
static void workqueue_enqueue(struct queue *q, struct work *w, void *cpu)
{
    CHECK(q == &queue && cpu == NULL && pending == NULL);
    CHECK(!current->startup_lock.held && !current->lock.held);
    CHECK(w == &current->startup_work && current->startup_queued);
    pending = w; enqueues++;
}
static int sysctl_lookup(struct sysctlnode *n, const int *value)
{
    CHECK(tree_locked && n->sysctl_size == sizeof(int));
    if (value != NULL) memcpy(n->sysctl_data, value, sizeof(int));
    return 0;
}
static void sysctl_unlock(void) { CHECK(tree_locked); tree_locked = false; }
static void sysctl_relock(void) { CHECK(!tree_locked); tree_locked = true; }
static int sun60i_a733_de352_start(int phandle)
{
    CHECK(phandle == 7 && current->registering && !tree_locked);
    CHECK(!current->lock.held && !current->startup_lock.held);
    starts++;
    if (inject_callback) a733_startup_enqueue(current);
    if (inject_sysctl) {
        struct sysctlnode node = {&current->stage, sizeof(int)};
        int stage = 1;
        tree_locked = true;
        sysctl_result = a733_stage(&node, &stage);
        CHECK(tree_locked); tree_locked = false;
    }
    return sink_error;
}
static int a733_register(struct a733_drm *sc)
{
    CHECK(sc == current && sc->registering && !tree_locked);
    CHECK(!sc->lock.held && !sc->startup_lock.held);
    registrations++; return registration_error;
}
static void a733_drm_work(struct work *w, void *arg)
{ CHECK(w != &current->startup_work && arg == current); commits++; }
#include "startup.c"

static void fixture(struct a733_drm *sc)
{
    memset(sc, 0, sizeof(*sc)); current = sc;
    sc->device = sc; sc->phandle = 7;
    pending = NULL; root_callback = NULL;
    enqueues = timers = starts = registrations = commits = creates = 0;
    sink_error = registration_error = queue_error = 0;
    tree_locked = inject_callback = inject_sysctl = false;
    sysctl_result = 0;
}
static void run_work(void)
{
    struct work *w = pending;
    CHECK(w != NULL); pending = NULL;
    queue.fn(w, queue.arg);
}
static void fire_timer(void)
{
    CHECK(current->startup_timer.pending);
    current->startup_timer.pending = false;
    current->startup_timer.fn(current->startup_timer.arg);
}
static int stage_request(struct a733_drm *sc, int stage)
{
    struct sysctlnode node = {&sc->stage, sizeof(int)};
    tree_locked = true; int error = a733_stage(&node, &stage);
    CHECK(tree_locked); tree_locked = false; return error;
}
int main(void)
{
    struct a733_drm sc;
    struct work commit = {1};
    fixture(&sc); CHECK(a733_startup_init(&sc, false) == 0);
    CHECK(root_callback != NULL && pending == NULL && creates == 1);
    sink_error = EAGAIN; root_callback(&sc); root_callback(&sc);
    CHECK(enqueues == 1 && starts == 0);
    inject_callback = inject_sysctl = true;
    run_work(); CHECK(sysctl_result == EBUSY && enqueues == 1);
    CHECK(starts == 1 && registrations == 0 && timers == 1 && sc.fault == 0);
    CHECK(!sc.startup_queued && !sc.startup_running && !sc.startup_stopped);
    fire_timer(); run_work(); CHECK(starts == 2 && timers == 2);
    sink_error = 0; fire_timer(); run_work();
    CHECK(registrations == 1 && sc.stage == 1 && sc.startup_registered);
    CHECK(sc.startup_stopped && !sc.startup_running && !sc.startup_queued);
    unsigned old_enqueues = enqueues;
    sc.startup_timer.fn(sc.startup_timer.arg); root_callback(&sc);
    CHECK(enqueues == old_enqueues && registrations == 1 && creates == 1);
    CHECK(stage_request(&sc, 1) == 0 && registrations == 1);
    queue.fn(&commit, &sc); CHECK(commits == 1);
    /* A manual request can finish while an old retry is pending. */
    fixture(&sc); CHECK(a733_startup_init(&sc, false) == 0);
    sink_error = EAGAIN; root_callback(&sc); run_work();
    sink_error = 0; CHECK(stage_request(&sc, 1) == 0);
    fire_timer(); CHECK(pending == NULL && registrations == 1);
    /* A queued callback after manual registration is a no-op. */
    fixture(&sc); CHECK(a733_startup_init(&sc, false) == 0);
    root_callback(&sc); CHECK(stage_request(&sc, 1) == 0);
    run_work(); CHECK(registrations == 1 && starts == 1);
    for (unsigned failure = 0; failure < 3; failure++) {
        fixture(&sc); CHECK(a733_startup_init(&sc, false) == 0);
        if (failure == 0) sink_error = EIO;
        else registration_error = failure == 1 ? EIO : EAGAIN;
        root_callback(&sc); run_work();
        CHECK(sc.fault == EIO && sc.startup_stopped && timers == 0);
        CHECK(!sc.startup_registered && stage_request(&sc, 1) == EIO);
        old_enqueues = enqueues; root_callback(&sc);
        CHECK(enqueues == old_enqueues);
    }
    fixture(&sc); CHECK(a733_startup_init(&sc, true) == 0);
    CHECK(root_callback == NULL && sc.startup_stopped && starts == 0);
    CHECK(stage_request(&sc, 2) == EINVAL);
    CHECK(stage_request(&sc, 1) == 0 && registrations == 1);
    fixture(&sc); queue_error = ENOMEM;
    CHECK(a733_startup_init(&sc, false) == ENOMEM);
    CHECK(sc.startup_stopped && root_callback == NULL && pending == NULL);
    printf("PASS %u actual KMS startup scheduling checks\n", checks);
    return 0;
}
