/* SPDX-License-Identifier: BSD-2-Clause */
/* Kernel boundaries for the real USL ioctl and process-exit hook functions. */
@HOOK_LICENSE@
#include <sys/queue.h>
#include <sys/types.h>
typedef unsigned char unchar;
typedef unsigned short ushort;
#include "@SOURCE_ROOT@/sys/dev/wscons/wsdisplay_usl_io.h"
/* Only unrelated ioctl conversion types/constants are stand-ins. */
struct wsdisplay_softc { int unused; };
struct wskbd_bell_data { int which, period, pitch; };
#define WSDISPLAYIO_SMODE 101
#define WSDISPLAYIO_MODE_MAPPED 1
#define WSDISPLAYIO_MODE_EMUL 0
#define WSKBDIO_COMPLEXBELL 102
#define WSKBD_BELL_DOPERIOD 1
#define WSKBD_BELL_DOPITCH 2
#define WSKBDIO_SETLEDS 103
#define WSKBDIO_GETLEDS 104
#define WSKBD_LED_CAPS 1
#define WSKBD_LED_NUM 2
#define WSKBD_LED_SCROLL 4
#define EPASSTHROUGH 1000
struct credential { int administrator; unsigned uid; };
struct lwp { struct proc *l_proc; struct credential *l_cred; };
#define KAUTH_GENERIC_ISSUSER 1
static int auth_error;
static void (*auth_hook)(void);
static int kauth_authorize_generic(struct credential *cred, int action, void *arg)
{
    assert(!irq && !spinheld && action == KAUTH_GENERIC_ISSUSER && arg == NULL);
    if (auth_hook) { void (*fn)(void) = auth_hook; auth_hook = NULL; fn(); }
    return auth_error ? auth_error : (cred && cred->administrator ? 0 : EPERM);
}
static int wsdisplay_internal_ioctl(struct wsdisplay_softc *sc,
    struct wsscreen *scr, u_long cmd, void *data, int flag, struct lwp *l)
{ (void)sc; (void)scr; (void)cmd; (void)data; (void)flag; (void)l; return EPASSTHROUGH; }

/* Run the actual kernel hook list/dispatch/registration code. */
struct hook_desc {
    LIST_ENTRY(hook_desc) hk_list;
    void (*hk_fn)(void *);
    void *hk_arg;
};
typedef LIST_HEAD(, hook_desc) hook_list_t;
typedef struct { int readers, writer; } krwlock_t;
static krwlock_t exithook_lock;
static int fail_hook, hook_count;
static void (*writer_hook)(void);
#define RW_READER 1
#define RW_WRITER 2
static void rw_enter(krwlock_t *lock, int mode)
{
    assert(!irq && !spinheld && !lock->writer);
    if (mode == RW_READER) { lock->readers++; hook_readers++; }
    else {
        assert(mode == RW_WRITER && !lock->readers);
        assert(!proc_lock.held && !process_lock.held);
        if (writer_hook) { void (*fn)(void) = writer_hook; writer_hook = NULL; fn(); }
        assert(!lock->readers); lock->writer = 1;
    }
}
static void rw_exit(krwlock_t *lock)
{
    if (lock->writer) lock->writer = 0;
    else { assert(lock->readers); lock->readers--; hook_readers--; }
}
static int hook_control;
static int hook_init(void) { return 0; }
#define RUN_ONCE(control, fn) ((*(control))++ ? 0 : (fn)())
#define __FPTRCAST(type, fn) ((type)(void (*)(void))(fn))
#define M_DEVBUF 0
#define M_NOWAIT 0
static void *hook_malloc(size_t size, int pool, int flags)
{
    (void)pool; (void)flags; assert(!hook_readers && !spinheld);
    if (fail_hook) return NULL;
    void *p = malloc(size); assert(p); hook_count++; return p;
}
static void hook_free(void *p, int pool)
{ (void)pool; assert(!hook_readers && !spinheld); hook_count--; free(p); }
#define malloc(size, pool, flags) hook_malloc(size, pool, flags)
#define free(pointer, pool) hook_free(pointer, pool)
static hook_list_t exithook_list = LIST_HEAD_INITIALIZER(exithook_list);
@HOOK_FUNCTIONS@
#undef malloc
#undef free
