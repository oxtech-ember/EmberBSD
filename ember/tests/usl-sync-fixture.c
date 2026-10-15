/* SPDX-License-Identifier: BSD-2-Clause */
@LICENSE@
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#define KASSERT assert
#define KM_SLEEP 0
#define MUTEX_DEFAULT 0
#define IPL_TTY 1
#undef NSIG
#define NSIG @NSIG@
typedef int pid_t;
typedef unsigned int u_int;
typedef struct { int held, spin; } kmutex_t;
static int irq, spinheld, now, signals, callbacks, result, frees;
static int allocations;
static int hook_readers;
static int hz = 100;
static kmutex_t proc_lock, process_lock;
struct proc { pid_t p_pid; kmutex_t *p_lock; };
static struct proc process = { 42, &process_lock };
static int alive = 1;
static void (*lock_hook)(void);
static void (*signal_hook)(void);
static kmutex_t *hook_lock;
static void mutex_init(kmutex_t *m, int type, int ipl)
{ (void)type; m->held = 0; m->spin = ipl != 0; }
static void mutex_destroy(kmutex_t *m) { assert(!m->held); }
static void mutex_enter(kmutex_t *m)
{
    assert(!m->held);
    if (!m->spin) {
        assert(!irq && !spinheld && !hook_readers);
        if (lock_hook && m == hook_lock) {
            void (*fn)(void) = lock_hook;
            lock_hook = NULL;
            fn(); /* Model mutation while an adaptive lock drops the BKL. */
        }
    } else spinheld++;
    m->held = 1;
}
static void mutex_exit(kmutex_t *m)
{ assert(m->held); m->held = 0; if (m->spin) spinheld--; }
#define mutex_spin_enter mutex_enter
#define mutex_spin_exit mutex_exit
static int cpu_intr_p(void) { return irq; }
static int getticks(void) { return now; }
static struct proc *proc_find(pid_t pid)
{ assert(proc_lock.held && !irq); return alive && pid == 42 ? &process : NULL; }
typedef struct { int ksi_signo; } ksiginfo_t;
#define KSI_INIT_EMPTY(k) memset((k), 0, sizeof(*(k)))
static int kpsignal2(struct proc *p, ksiginfo_t *k)
{
    assert(!irq && !spinheld && !hook_readers && proc_lock.held && p->p_lock->held);
    assert(k->ksi_signo == 10 || k->ksi_signo == 12);
    signals++;
    if (signal_hook) {
        void (*fn)(void) = signal_hook; signal_hook = NULL; fn();
    }
    return 0;
}
static void psignal(struct proc *p, int signo)
{
    ksiginfo_t k;
    assert(!irq);
    KSI_INIT_EMPTY(&k); k.ksi_signo = signo;
    mutex_enter(p->p_lock); (void)kpsignal2(p, &k); mutex_exit(p->p_lock);
}
typedef struct {
    void (*fn)(void *); void *arg; int pending, due, running, valid;
} callout_t;
static callout_t *queue[128];
static unsigned nqueue;
static callout_t *running;
static void callout_init(callout_t *c, unsigned flags)
{
    assert(flags == 0); memset(c, 0, sizeof(*c)); c->valid = 1;
    assert(nqueue < 128); queue[nqueue++] = c;
}
static void callout_setfunc(callout_t *c, void (*fn)(void *), void *arg)
{ c->fn = fn; c->arg = arg; }
static void callout_schedule(callout_t *c, int delay)
{ assert(c->valid && delay > 0); c->pending = 1; c->due = now + delay; }
static bool callout_stop(callout_t *c)
{ bool expired = !c->pending; c->pending = 0; return expired; }
static bool callout_halt(callout_t *c, void *lock)
{ (void)lock; assert(!irq && !spinheld); return callout_stop(c); }
static void callout_destroy(callout_t *c)
{
    assert(!c->pending && (!c->running || running == c)); c->valid = 0;
    for (unsigned i = 0; i < nqueue; i++) if (queue[i] == c) queue[i] = NULL;
}
static void dispatch(callout_t *c)
{
    assert(!irq && !running && c->valid);
    c->pending = 0; c->running = 1; running = c;
    c->fn(c->arg);
    /* A kernel callout can free its own storage. Do not touch c here. */
    for (unsigned i = 0; i < nqueue; i++)
        if (queue[i] == c) { c->running = 0; break; }
    running = NULL;
}
static void advance(int ticks)
{
    now += ticks;
    for (;;) {
        callout_t *next = NULL;
        for (unsigned i = 0; i < nqueue; i++)
            if (queue[i] && queue[i]->pending && queue[i]->due <= now) {
                next = queue[i]; break;
            }
        if (!next) break;
        dispatch(next);
    }
}
static void *kmem_intr_alloc(size_t size, int flags)
{ (void)flags; assert(!hook_readers); void *p = malloc(size); assert(p); allocations++; return p; }
static void kmem_intr_free(void *p, size_t size)
{ (void)size; assert(!spinheld && !hook_readers); frees++; free(p); }
struct wsscreen { void *cookie; const struct wscons_syncops *ops; };
struct wscons_syncops {
    int (*detach)(void *, int, void (*)(void *, int, int), void *);
    int (*attach)(void *, int, void (*)(void *, int, int), void *);
    int (*check)(void *); void (*destroy)(void *);
};
static int fail_init;
static int wsscreen_attach_sync(struct wsscreen *s,
    const struct wscons_syncops *ops, void *cookie)
{
    if (fail_init) return EBUSY;
    if (s->ops && s->ops->check(s->cookie)) return EBUSY;
    s->cookie = cookie; s->ops = ops; return 0;
}
static int wsscreen_detach_sync(struct wsscreen *s)
{ s->cookie = NULL; s->ops = NULL; return 0; }
static int wsscreen_lookup_sync(struct wsscreen *s,
    const struct wscons_syncops *ops, void **cookie)
{ (void)ops; if (!s->cookie) return EINVAL; *cookie = s->cookie; return 0; }

@OWNER_SUPPORT@
@FUNCTIONS@
@IOCTL_FUNCTION@

static struct wsscreen screen;
static void (*callback_hook)(void);
static struct usl_syncdata *sd;
static unsigned tests;
static struct usl_syncdata *replacement;
static void completed(void *arg, int error, int waitok)
{
    assert(arg == &screen && !hook_readers); (void)waitok; callbacks++; result = error;
    if (callback_hook) {
        void (*fn)(void) = callback_hook; callback_hook = NULL; fn();
    }
}
static void setup_signals(int acqsig, int relsig)
{
    memset(queue, 0, sizeof(queue)); nqueue = 0; now = 0;
    irq = spinheld = signals = callbacks = frees = result = 0;
    proc_lock.held = process_lock.held = 0; alive = 1;
    fail_init = 0; callback_hook = lock_hook = signal_hook = NULL;
    assert(usl_sync_init(&screen, &sd, &process, acqsig, relsig, SHRT_MAX) == 0);
    assert(usl_sync_get(&screen) == sd);
}
static void setup(void) { setup_signals(12, 10); }
static void finish(void)
{
    if (screen.cookie) usl_sync_destroy(screen.cookie);
    advance(1); assert(frees == 1 && !screen.cookie);
}
static void check(const char *name)
{ assert(!spinheld && !proc_lock.held && !process_lock.held); tests++; puts(name); }
static void destroy_callback(void) { usl_sync_destroy(sd); }
static void attach_callback(void)
{ assert(usl_attachproc(sd, 0, completed, &screen) == EAGAIN); }
static int hook_ack_value;
static void ack_hook(void) { assert(usl_detachack(sd, hook_ack_value) == 0); }
static void new_generation_hook(void)
{
    assert(usl_detachack(sd, 1) == 0);
    assert(usl_detachproc(sd, 0, completed, &screen) == EAGAIN);
}
static void destroy_hook(void) { usl_sync_destroy(sd); }
static void retire_during_check(void)
{
    usl_sync_destroy(sd);
    advance(1); /* Another CPU while a process-context check drops the BKL. */
    assert(!frees && !screen.cookie);
}
static void owner_dead(void) { alive = 0; }
static void replace_owner(void)
{
    assert(!screen.cookie);
    assert(usl_sync_init(&screen, &replacement, &process, 12, 10, 0) == 0);
}
@OWNER_TESTS@
int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    (void)mutex_init; (void)mutex_destroy; (void)cpu_intr_p; (void)getticks;
    (void)callout_halt; (void)callout_destroy; (void)usl_attachack; (void)psignal;
    (void)exithook_establish; (void)exithook_disestablish; (void)doexithooks;
    (void)kauth_authorize_generic; (void)wsdisplay_internal_ioctl;
    if (argc > 1 && strcmp(argv[1], "owner") == 0) {
        owner_tests(); return 0;
    }
    setup(); irq = 1;
    assert(usl_detachproc(sd, 0, completed, &screen) == EAGAIN);
    assert(!signals && !callbacks);
    irq = 0; advance(1);
    assert(signals == 1 && !callbacks);
    assert(usl_detachack(sd, 1) == 0);
    assert(callbacks == 1 && result == 0);
    finish();
    check("PASS hard-IRQ detach notification and acknowledgement");
    if (argc > 1 && strcmp(argv[1], "detach") == 0) return 0;

    setup(); irq = 1;
    assert(usl_attachproc(sd, 0, completed, &screen) == EAGAIN);
    assert(!signals && !callbacks);
    irq = 0; advance(1); assert(signals == 1);
    assert(usl_attachack(sd, 1) == 0);
    assert(callbacks == 1 && result == 0); finish();
    check("PASS hard-IRQ attach notification and acknowledgement");

    for (int attach = 0; attach < 2; attach++) {
        setup();
        assert((attach ? usl_attachproc : usl_detachproc)
            (sd, 1, completed, &screen) == EAGAIN);
        assert(signals == 1 && !callbacks);
        assert((attach ? usl_attachack : usl_detachack)(sd, 0) == 0);
        assert(callbacks == 1 && result == EIO);
        advance(5 * hz); assert(callbacks == 1); finish();
        check("PASS process-context immediate notification and denial");
    }
    for (int attach = 0; attach < 2; attach++) {
        for (int ack = 0; ack < 2; ack++) {
            setup();
            assert((attach ? usl_attachproc : usl_detachproc)
                (sd, 0, completed, &screen) == EAGAIN);
            assert((attach ? usl_attachack : usl_detachack)(sd, ack) == 0);
            advance(5 * hz); assert(!signals && callbacks == 1);
            assert(result == (ack ? 0 : EIO)); finish();
            check("PASS early ACK/deny cancels deferred signal and timer");
        }
    }
    for (int attach = 0; attach < 2; attach++) {
        setup();
        assert((attach ? usl_attachproc : usl_detachproc)
            (sd, 0, completed, &screen) == EAGAIN);
        irq = 1; usl_sync_destroy(sd); irq = 0;
        assert(!screen.cookie && !frees && callbacks == 1);
        assert(result == (attach ? ENXIO : 0));
        /* Model already-entered invocations before the drain, not after free. */
        usl_detachtimeout(sd); usl_attachtimeout(sd);
        usl_sync_destroy(sd);
        advance(1); assert(frees == 1 && callbacks == 1 && !signals);
        check("PASS IRQ destroy unpublishes once and late jobs cannot complete");
    }
    for (int attach = 0; attach < 2; attach++) {
        setup();
        assert((attach ? usl_attachproc : usl_detachproc)
            (sd, 0, completed, &screen) == EAGAIN);
        advance(1); advance(5 * hz - 1); assert(!callbacks);
        advance(1); assert(callbacks == 1 && result == EIO);
        advance(5 * hz); assert(callbacks == 1); finish();
        check("PASS normal timeout begins after deferred notification");
    }
    for (int lock = 0; lock < 2; lock++) {
        for (int ack = 0; ack < 2; ack++) {
            setup(); hook_lock = lock ? &process_lock : &proc_lock;
            hook_ack_value = ack; lock_hook = ack_hook;
            assert(usl_detachproc(sd, 0, completed, &screen) == EAGAIN);
            advance(1); assert(!signals && callbacks == 1);
            assert(result == (ack ? 0 : EIO)); finish();
            check("PASS ACK/deny during adaptive lock wait prevents stale signal");
        }
        setup(); hook_lock = lock ? &process_lock : &proc_lock;
        lock_hook = destroy_hook;
        assert(usl_detachproc(sd, 0, completed, &screen) == EAGAIN);
        advance(1); assert(!signals && callbacks == 1 && !frees);
        advance(1); assert(frees == 1);
        check("PASS destroy during adaptive lock wait prevents stale signal");

        setup(); hook_lock = lock ? &process_lock : &proc_lock;
        lock_hook = new_generation_hook;
        assert(usl_detachproc(sd, 0, completed, &screen) == EAGAIN);
        advance(1); assert(!signals && callbacks == 1);
        advance(1); assert(signals == 1 && callbacks == 1);
        assert(usl_detachack(sd, 1) == 0);
        assert(callbacks == 2); finish();
        check("PASS new generation survives an older notification's lock wait");
    }
    setup(); hook_lock = &proc_lock; lock_hook = retire_during_check;
    assert(usl_sync_check(sd) == 0); assert(!frees);
    advance(1); assert(frees == 1);
    check("PASS operation pin prevents retirement across a process lock wait");

    setup(); assert(usl_detachproc(sd, 0, completed, &screen) == EAGAIN);
    advance(1); assert(usl_detachack(sd, 1) == 0);
    advance(10); assert(usl_detachproc(sd, 0, completed, &screen) == EAGAIN);
    usl_detachtimeout(sd); assert(callbacks == 1); /* Still awaiting notify. */
    advance(1); usl_detachtimeout(sd); assert(callbacks == 1);
    advance(5 * hz - 1); assert(callbacks == 1);
    advance(1); assert(callbacks == 2 && result == EIO); finish();
    check("PASS stale timeout cannot shorten the current generation deadline");

    setup(); callback_hook = destroy_callback;
    assert(usl_detachproc(sd, 1, completed, &screen) == EAGAIN);
    assert(usl_detachack(sd, 1) == 0);
    assert(callbacks == 1 && !frees && !screen.cookie);
    advance(1); assert(frees == 1 && callbacks == 1);
    check("PASS ACK callback can destroy its cookie exactly once");

    setup(); callback_hook = destroy_callback;
    assert(usl_attachproc(sd, 0, completed, &screen) == EAGAIN);
    advance(1); advance(5 * hz);
    assert(callbacks == 1 && result == EIO && !frees);
    advance(1); assert(frees == 1 && callbacks == 1);
    check("PASS timeout callback can destroy its cookie without use-after-free");

    setup(); callback_hook = attach_callback;
    assert(usl_detachproc(sd, 1, completed, &screen) == EAGAIN);
    assert(usl_detachack(sd, 1) == 0);
    advance(1); assert(signals == 2 && callbacks == 1);
    assert(usl_attachack(sd, 1) == 0);
    assert(callbacks == 2); finish();
    check("PASS callback can start a fresh attach without callback corruption");

    setup(); callback_hook = replace_owner;
    assert(usl_detachproc(sd, 0, completed, &screen) == EAGAIN);
    usl_sync_destroy(sd); assert(screen.cookie == replacement && callbacks == 1);
    usl_sync_destroy(sd); assert(screen.cookie == replacement);
    advance(1); assert(frees == 1 && screen.cookie == replacement);
    usl_sync_destroy(replacement); advance(1); assert(frees == 2);
    check("PASS reentrant owner replacement remains published while old cookie retires");

    setup(); alive = 0;
    assert(usl_detachproc(sd, 1, completed, &screen) == 0);
    assert(!callbacks && !signals && !screen.cookie);
    advance(1); assert(frees == 1);
    check("PASS dead owner preserves synchronous detach result");

    for (int attach = 0; attach < 2; attach++) {
        setup(); hook_lock = &proc_lock; lock_hook = owner_dead;
        assert((attach ? usl_attachproc : usl_detachproc)
            (sd, 0, completed, &screen) == EAGAIN);
        advance(1); assert(!signals && callbacks == 1);
        assert(result == (attach ? ENXIO : 0));
        advance(1); assert(frees == 1);
        check("PASS dead owner preserves deferred detach/attach completion");
    }
    setup();
    assert(usl_detachproc(sd, 0, NULL, &screen) == EINVAL);
    assert(usl_detachack(sd, 1) == EINVAL);
    assert(usl_attachack(sd, 1) == EINVAL);
    assert(usl_detachproc(sd, 0, completed, &screen) == EAGAIN);
    assert(usl_attachproc(sd, 0, completed, &screen) == EBUSY);
    assert(usl_detachack(sd, 1) == 0); finish();
    check("PASS invalid or overlapping requests preserve the pending callback");

    memset(queue, 0, sizeof(queue)); nqueue = 0; frees = 0;
    fail_init = 1;
    assert(usl_sync_init(&screen, &sd, &process, 12, 10, 0) == EBUSY);
    assert(frees == 1);
    for (unsigned i = 0; i < nqueue; i++) assert(queue[i] == NULL);
    check("PASS unpublished init failure destroys all callout storage");

    setup(); signal_hook = destroy_hook;
    assert(usl_detachproc(sd, 0, completed, &screen) == EAGAIN);
    advance(1); assert(signals == 1 && callbacks == 1 && !screen.cookie);
    advance(1); assert(frees == 1 && callbacks == 1);
    check("PASS cancellation after signal commit does not promise signal withdrawal");

    for (int field = 0; field < 2; field++) {
        const int invalid[] = { -1, NSIG, SHRT_MAX };
        for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
            int before = allocations; nqueue = 0; screen.cookie = &screen;
            fail_init = 0;
            struct usl_syncdata *unchanged = (void *)&screen;
            assert(usl_sync_init(&screen, &unchanged, &process,
                field ? 12 : invalid[i], field ? invalid[i] : 10, 0) == EINVAL);
            assert(allocations == before && screen.cookie == &screen);
            assert(unchanged == (void *)&screen && !nqueue);
            check("PASS invalid signal rejected before allocation or publication");
        }
    }
    setup_signals(0, 0);
    assert(usl_detachproc(sd, 0, completed, &screen) == EAGAIN);
    advance(1); assert(!signals && !callbacks);
    advance(5 * hz); assert(callbacks == 1 && result == EIO); finish();
    check("PASS signal zero retains silent notification and timeout semantics");
    setup_signals(NSIG - 1, NSIG - 1); finish();
    check("PASS highest valid signal and unused frsig remain accepted");
    printf("PASS %u USL sync boundary cases\n", tests);
    return 0;
}
