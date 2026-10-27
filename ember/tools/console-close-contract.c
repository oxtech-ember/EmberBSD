/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual console/issuer/FDT bodies. Kernel scheduling services are controlled. */
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include <sys/types.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>

static atomic_uint checks;
#define CHECK(x) do { checks++; if (!(x)) { \
	fprintf(stderr, "close CHECK %s:%d: %s\n", __FILE__, __LINE__, #x); \
	abort(); } } while (0)
#define KASSERT CHECK
#define KASSERTMSG(x, ...) CHECK(x)
#define WSDISPLAY_BOOTFB_HANDOFF 1
#define WSDISPLAY_BOOTFB_DEFER 1
#define WSDISPLAY_EARLYFB_HOOKS 1
#define WSDISPLAY_MULTICONS 1
#define SCR_GRAPHICS 1
#define NODEV ((dev_t)-1)
typedef unsigned char u_char;
static _Thread_local int giant, read_depth;
static _Thread_local bool hard_irq, soft_irq;
static const char *panicstr;
static bool cold = true;
static pthread_mutex_t giant_mutex;
static pthread_mutex_t gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
static atomic_uint readers;
static unsigned barriers, creates, destroys, allocations_hook;
static int barrier_action;
static bool wait_output, output_entered, release_output;
static bool perform_entered, close_done;
static void (*allocation_hook)(void);
static void (*perform_hook)(void);
static void lock_giant(int n) { CHECK(n >= 0); while (n--) { CHECK(pthread_mutex_lock(&giant_mutex) == 0); giant++; } }
static void unlock_giant(void) { CHECK(giant > 0); giant--; CHECK(pthread_mutex_unlock(&giant_mutex) == 0); }
static void unlock_all(int *n) { *n = giant; while (giant) unlock_giant(); }
#define KERNEL_LOCK(n, arg) lock_giant(n)
#define KERNEL_UNLOCK_ONE(arg) unlock_giant()
#define KERNEL_UNLOCK_ALL(arg, n) unlock_all(n)
#define KERNEL_LOCKED_P() (giant > 0)
#define ASSERT_SLEEPABLE() CHECK(!hard_irq && !soft_irq && read_depth == 0)
static bool cpu_intr_p(void) { return hard_irq; }
static bool cpu_softintr_p(void) { return soft_irq; }
static unsigned fixture_load(const unsigned *);
static void (*admission_hook)(void);
#define atomic_load_acquire(p) fixture_load(p)
#define atomic_load_relaxed(p) __atomic_load_n((p), __ATOMIC_RELAXED)
static void fixture_stored(const void *);
static bool inject_closed_replay;
#define atomic_store_release(p, v) do { __atomic_store_n((p), (v), __ATOMIC_RELEASE); fixture_stored(p); } while (0)
#define atomic_load_consume(p) __atomic_load_n((p), __ATOMIC_ACQUIRE)
static unsigned atomic_cas_uint(unsigned *p, unsigned before, unsigned after)
{
	(void)__atomic_compare_exchange_n(p, &before, after, false,
	    __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
	return before;
}
#include "sys/pserialize.h"
static bool pserialize_in_read_section(void) { return read_depth != 0; }
static int pserialize_read_enter(void) { atomic_fetch_add(&readers, 1); return read_depth++; }
static void pserialize_read_exit(int s)
{
	CHECK(read_depth == s + 1); read_depth--;
	CHECK(pthread_mutex_lock(&gate) == 0);
	atomic_fetch_sub(&readers, 1); pthread_cond_broadcast(&changed);
	CHECK(pthread_mutex_unlock(&gate) == 0);
}
/* Actual create is KM_SLEEP, not a fallible caller backend. */
static pserialize_t pserialize_create(void)
{
	pserialize_t p = malloc(sizeof(*p)); CHECK(p != NULL); p->cookie = 123;
	creates++; if (allocation_hook != NULL) { allocations_hook++; allocation_hook(); }
	return p;
}
static void pserialize_destroy(pserialize_t p) { CHECK(p != NULL && p->cookie == 123); destroys++; free(p); }
static void pserialize_perform(pserialize_t p);
#include "simplefbvar.h"
#include "wsdisplay_bootfbvar.h"
#include <dev/cons_handoffvar.h>
struct wsscreen_descr { int nrows, ncols; const void *textops; };
struct wsemul_ops {
	void *(*cnattach)(const struct wsscreen_descr *, void *, int, int, long);
	void (*output)(void *, const u_char *, unsigned, int);
};
struct wsscreen_internal {
	const void *emulops;
	void *emulcookie, *wsemulcookie;
	const struct wsscreen_descr *scrdata;
	const struct wsemul_ops *wsemul;
	int scr_flags;
};
struct display_device {
	struct wsscreen_internal *sc_scr[1];
	struct { void (*pollc)(void *, int); } *sc_accessops;
	void *sc_accesscookie;
};
struct consdev {
	void (*cn_halt)(dev_t);
	void (*cn_putc)(dev_t, int);
	void (*cn_pollc)(dev_t, int);
	int (*cn_getc)(dev_t);
	dev_t cn_dev;
};
static struct consdev wsdisplay_cons, old_cons, *cn_tab, *wsdisplay_ocn;
static struct consdev *fixture_previous;
struct proc { int token; };
struct lwp { struct proc *l_proc; };
static struct proc proc0;
static struct lwp fixture_lwp = { &proc0 }, *curlwp = &fixture_lwp;
struct cfattach { const char *ca_name; };
struct device {
	void *private; const char *driver; struct cfattach attachment;
	struct device *dv_parent; int dv_pending;
#ifdef BOOTFB_SERIAL_CONTRACT
	TAILQ_ENTRY(device) dv_pending_list;
#endif
};
static void *device_private(device_t d);
struct vnode;
#define NULLVP NULL
static struct vnode *cn_devvp[2];
struct tty;
static struct tty *constty;
#ifndef BOOTFB_SERIAL_CONTRACT
int cn_bootfb_serial_hold(struct wsdisplay_bootfb_lease *l, const struct consdev *c, dev_t *d)
{ (void)l; (void)c; (void)d; return ENXIO; }
int cn_bootfb_serial_validate(struct wsdisplay_bootfb_lease *l, const struct consdev *c, dev_t *d)
{ (void)l; (void)c; (void)d; return ENXIO; }
void cn_bootfb_serial_release(struct wsdisplay_bootfb_lease *l) { (void)l; }
#endif
static struct display_device *wsdisplay_console_device;
static struct wsscreen_internal wsdisplay_console_conf;
static int wsdisplay_console_initted, wsdisplay_cons_pollmode;
static int (*wsdisplay_cons_kbd_getc)(dev_t);
static void (*wsdisplay_cons_kbd_pollc)(dev_t, int);
static bool wsdisplay_multicons_enable, wsdisplay_multicons_suspended;
#include "binding-state.h"
static unsigned output_calls, old_output, old_gets, kbd_gets;
static unsigned old_on, old_off, kbd_on, kbd_off, updates, replay_calls;
static int keyboard_result = -1, old_result = -1, nested_result;
static int replay_action, getc_action;
static struct wsdisplay_bootfb_lease *lease;
static void wsdisplay_cnputc(dev_t, int);
static void *emul_attach(const struct wsscreen_descr *t, void *c, int x, int y, long a)
{ (void)t; (void)x; (void)y; (void)a; return c; }
static void output(void *c, const u_char *text, unsigned n, int kernel)
{
	CHECK(c != NULL && text != NULL && n == 1 && kernel == 1);
	CHECK(read_depth > 0); output_calls++;
	CHECK(pthread_mutex_lock(&gate) == 0);
	if (wait_output) {
		output_entered = true; pthread_cond_broadcast(&changed);
		while (!release_output) CHECK(pthread_cond_wait(&changed, &gate) == 0);
	}
	CHECK(pthread_mutex_unlock(&gate) == 0);
}
static const struct wsemul_ops emul = { emul_attach, output };
static const struct wsemul_ops *wsemul_pick(int n) { CHECK(n == 0); return &emul; }
static void old_putc(dev_t d, int c) { (void)d; (void)c; CHECK(read_depth > 0); old_output++; }
static void kbd_poll(dev_t d, int on) { (void)d; if (on) kbd_on++; else kbd_off++; }
static void old_poll(dev_t d, int on) { (void)d; CHECK(!cn_publication_closed()); if (on) old_on++; else old_off++; }
static int kbd_getc(dev_t d) { (void)d; kbd_gets++; return keyboard_result; }
static int old_getc(dev_t d)
{
	(void)d; old_gets++; CHECK(!cn_publication_closed());
	if (getc_action) { nested_result = simplefb_console_close(lease); CHECK(nested_result == EBUSY); }
	return old_result;
}
static struct wsscreen_descr screen = { 10, 20, NULL };
static char cookie;
static int (*finalizer)(device_t);
static unsigned fallback_calls;
#ifndef BOOTFB_SERIAL_CONTRACT
static int config_finalize_register(device_t d, int (*f)(device_t)) { CHECK(d != NULL); finalizer = f; return 0; }
#else
#include "native-config.h"
#endif
static int fallback(device_t d) { CHECK(d != NULL); fallback_calls++; return 0; }
#include "publication.h"
static unsigned fixture_load(const unsigned *p)
{
	unsigned value = __atomic_load_n(p, __ATOMIC_ACQUIRE);
	if (p == &cn_publication && admission_hook != NULL) {
		void (*hook)(void) = admission_hook;
		admission_hook = NULL; hook();
	}
	return value;
}
#include "early-api.h"
#include "display.h"
#include "issuer.c"
static void fixture_stored(const void *p)
{
	/* Defensive CLOSED-guard injection bypasses normal admitted freeze. */
	if (inject_closed_replay && p == &wsdisplay_early_phase &&
	    wsdisplay_early_phase == WSDISPLAY_EARLY_BOUND)
		cn_publication |= CN_PUBLICATION_CLOSED;
}

/* Actual FDT association matcher; parser/platform services have fixed inputs. */
static struct device device;
static void *device_private(device_t d) { return d->private; }
static bool device_is_a(device_t d, const char *s) { return strcmp(d->driver, s) == 0; }
static struct cfattach *device_cfattach(device_t d) { return &d->attachment; }
struct genfb_softc { device_t sc_dev; };
#include "simplefb-state.h"
static struct simplefb_softc softc;
static struct bus_space tag;
static char fdt_blob;
static const void *current_blob = &fdt_blob;
static struct simplefb_framebuffer parsed;
static unsigned matches, node_reads;
static int md_error;
#define BUS_SPACE_MAP_LINEAR 1
#define BUS_SPACE_MAP_PREFETCHABLE 2
static int generic_map(void *c, bus_addr_t a, bus_size_t z, int f, bus_space_handle_t *h)
{ (void)c; (void)a; (void)z; (void)f; (void)h; CHECK(false); return EIO; }
static struct bus_space arm_generic_bs_tag = { generic_map };
static const void *fdtbus_get_data(void) { matches++; return current_blob; }
static int fdtbus_get_stdout_phandle(void) { return 7; }
int simplefb_node_cpu_domain(int p) { CHECK(p == 7); return md_error; }
static bool fdtbus_bootfb_cpu_map(bus_space_tag_t t) { return t == &tag; }
int simplefb_read_node(int p, bus_space_tag_t t, struct simplefb_framebuffer *out)
{ CHECK(p == 7 && t == &tag); node_reads++; *out = parsed; return 0; }
#include "arm-binding.h"
#include "fdt-close.h"
static void pserialize_perform(pserialize_t p)
{
	CHECK(p != NULL && p->cookie == 123); CHECK(giant == 0); barriers++;
	if (perform_hook != NULL) perform_hook();
	if (barrier_action == 1) panicstr = "controlled panic";
	if (barrier_action == 2) wsdisplay_console_generation++;
	if (barrier_action == 3) wsdisplay_console_conf.emulcookie = NULL;
	if (barrier_action == 4) current_blob = NULL;
	if (barrier_action == 5) {
		CHECK(simplefb_console_close(lease) == EBUSY);
		CHECK(wsdisplay_bootfb_cancel(lease) == EPERM);
	}
	CHECK(pthread_mutex_lock(&gate) == 0);
	perform_entered = true; pthread_cond_broadcast(&changed);
	while (atomic_load(&readers) != 0) CHECK(pthread_cond_wait(&changed, &gate) == 0);
	CHECK(pthread_mutex_unlock(&gate) == 0);
}
/* Actual vcons whole replay includes its final screen update. */
#define VCONS_IS_VISIBLE 2
struct vcons_screen { int scr_status; };
struct message_buffer { int msg_bufr, msg_bufx, msg_bufs; char *msg_bufc; };
static char message[] = "xy";
static struct message_buffer message_info = { 0, 2, 3, message }, *msgbufp = &message_info;
static void wsdisplay_multicons_suspend(bool v) { wsdisplay_multicons_suspended = v; }
static void cnputc(int c) { wsdisplay_cnputc(0, c); }
static void vcons_update_screen(struct vcons_screen *s)
{
	CHECK(s != NULL && read_depth > 0); updates++;
	if (replay_action) CHECK(cn_publication_freeze() == EBUSY);
}
#include "replay.h"
static void replay(void *arg) { replay_calls++; vcons_replay_msgbuf(arg); }
static void initialize(void)
{
	struct vcons_screen v = { VCONS_IS_VISIBLE };
	memset(&wsdisplay_bootfb, 0, sizeof(wsdisplay_bootfb));
	memset(&wsdisplay_bootfb_lease, 0, sizeof(wsdisplay_bootfb_lease));
	memset(&simplefb_close, 0, sizeof(simplefb_close));
	memset(&wsdisplay_console_conf, 0, sizeof(wsdisplay_console_conf));
	memset(&arm_simplefb_boot, 0, sizeof(arm_simplefb_boot));
	cn_publication = 0; wsdisplay_early_phase = 0; wsdisplay_console_generation = 0;
	wsdisplay_early_type = NULL; wsdisplay_early_cookie = NULL; wsdisplay_console_device = NULL;
	wsdisplay_cons_pollmode = 0; wsdisplay_poll_admitted = false; wsdisplay_serial_pollmode = false; wsdisplay_console_initted = 0;
	wsdisplay_serial_generation = 0; wsdisplay_serial_dev = NODEV; admission_hook = NULL;
	panicstr = NULL; hard_irq = soft_irq = false; cn_tab = fixture_previous != NULL ? fixture_previous : &old_cons; wsdisplay_ocn = NULL;
	wsdisplay_serial_console = NULL; wsdisplay_serial_lease = NULL; wsdisplay_cons.cn_dev = NODEV; cn_devvp[0] = cn_devvp[1] = NULL; constty = NULL; curlwp->l_proc = &proc0;
	old_cons.cn_putc = old_putc; old_cons.cn_pollc = old_poll; old_cons.cn_getc = old_getc;
	wsdisplay_cons_kbd_getc = kbd_getc; wsdisplay_cons_kbd_pollc = kbd_poll;
	wsdisplay_multicons_enable = true; wsdisplay_multicons_suspended = false;
	CHECK(wsdisplay_early_preattach(&screen, &cookie, 0, 0, 0, replay, &v) == 0);
	CHECK(wsdisplay_console_generation == 1 && replay_calls != 0);
	CHECK(read_depth == 0 && atomic_load(&readers) == 0 && cn_publication == 0);
	device.private = &softc; device.driver = "genfb"; device.attachment.ca_name = "simplefb";
	softc.sc_gen.sc_dev = &device; softc.sc_bst = &tag; softc.sc_phandle = 7;
	parsed = (struct simplefb_framebuffer) { &tag, 4096, 8192, 8192, 64, 32, 256, SIMPLEFB_FORMAT_X8R8G8B8, 32 };
	softc.sc_framebuffer = parsed; tag.bs_map = generic_map; current_blob = &fdt_blob; md_error = 0;
	arm_simplefb_boot.blob = current_blob; arm_simplefb_boot.phandle = 7;
	arm_simplefb_boot.address = parsed.sf_address; arm_simplefb_boot.size = parsed.sf_size;
	arm_simplefb_boot.framebuffer = parsed; arm_simplefb_boot.map_present = true;
	arm_simplefb_boot.map_method = generic_map; arm_simplefb_boot.flags = 3;
	arm_simplefb_boot.type = &screen; arm_simplefb_boot.cookie = &cookie; arm_simplefb_captured = 1;
	KERNEL_LOCK(1, NULL);
	CHECK(wsdisplay_bootfb_defer(&device, &screen, &cookie, fallback) == 0);
	KERNEL_UNLOCK_ONE(NULL);
	lease = NULL;
#ifndef BOOTFB_SERIAL_CONTRACT
	CHECK(simplefb_console_claim(&lease, &parsed) == 0 && lease != NULL);
#endif
	barriers = creates = destroys = matches = node_reads = 0; allocations_hook = 0;
	output_calls = old_output = old_gets = kbd_gets = 0; old_on = old_off = kbd_on = kbd_off = 0;
	barrier_action = getc_action = 0; allocation_hook = perform_hook = NULL;
	wait_output = output_entered = release_output = perform_entered = close_done = false;
}
static void status_is(bool complete, int error)
{
	struct simplefb_console_close_status s;
	CHECK(simplefb_console_close_status(lease, &s) == 0);
	CHECK(s.attempted && s.closed && s.complete == complete && s.error == error);
	CHECK(s.generation == 1);
}
static void check_resident(void)
{
	CHECK(cn_tab == &wsdisplay_cons && wsdisplay_console_initted == 1);
	CHECK(wsdisplay_console_conf.emulcookie == &cookie && wsdisplay_console_device == NULL);
	CHECK(wsdisplay_early_phase == WSDISPLAY_EARLY_READY);
	CHECK(simplefb_console_cancel(lease) == EPERM);
	KERNEL_LOCK(1, NULL); CHECK(finalizer(&device) == 0); KERNEL_UNLOCK_ONE(NULL);
	CHECK(fallback_calls == 0);
}
static void cancel_during_allocation(void) { CHECK(wsdisplay_bootfb_cancel(lease) == 0); }
static void *reader_thread(void *arg) { (void)arg; wsdisplay_cnputc(0, 'a'); return NULL; }
static void *close_thread(void *arg)
{
	(void)arg; CHECK(simplefb_console_close(lease) == 0);
	CHECK(pthread_mutex_lock(&gate) == 0); close_done = true; pthread_cond_broadcast(&changed);
	CHECK(pthread_mutex_unlock(&gate) == 0); return NULL;
}
/* Actual TOLOG is recorded before the existing v_putc route. No new sink. */
#define TOLOG 1
#define TOCONS 2
#define TOTTY 4
#ifndef __predict_true
#define __predict_true(v) (v)
#endif
#define atomic_load_consume(p) __atomic_load_n((p), __ATOMIC_ACQUIRE)
#define atomic_store_relaxed(p, v) __atomic_store_n((p), (v), __ATOMIC_RELAXED)
struct tty { int token; };
static unsigned logged, routed;
static int tputchar(int c, int f, struct tty *t) { (void)c; (void)f; (void)t; return 0; }
static void atomic_cas_ptr(struct tty **p, struct tty *old, struct tty *value)
{ if (*p == old) *p = value; }
static void logputchar(int c) { CHECK(c == 'l'); CHECK(routed == 0); logged++; }
static void route_putc(int c) { routed++; wsdisplay_cnputc(0, c); }
static void (*v_putc)(int) = route_putc;
#include "putone.h"
static void tests(void)
{
	struct simplefb_console_close_status s, saved;
	struct wsdisplay_bootfb_lease foreign = {0};
	for (int depth = 0; depth <= 3; depth++) {
		initialize(); KERNEL_LOCK(depth, NULL); CHECK(simplefb_console_close(lease) == 0);
		CHECK(giant == depth); while (giant) KERNEL_UNLOCK_ONE(NULL);
		status_is(true, 0); CHECK(barriers == 1 && creates == 1 && destroys == 1);
		CHECK(simplefb_console_close_serial(lease) == EPERM);
		cold = false; CHECK(wsdisplay_bootfb_close_serial(lease) == ENXIO); cold = true;
		unsigned m = matches; CHECK(simplefb_console_close(lease) == 0 && barriers == 1 && matches == m);
		check_resident();
		logged = routed = 0; cold = false; putone('l', TOLOG | TOCONS, NULL); cold = true;
		CHECK(logged == 1 && routed == 1 && output_calls == 0 && old_output == 0);
		wsdisplay_cnputc(0, 'z'); CHECK(output_calls == 0 && old_output == 0);
		old_result = 65; keyboard_result = -1; CHECK(wsdisplay_getc(0) == -1 && old_gets == 0);
		keyboard_result = 66; CHECK(wsdisplay_getc(0) == 66 && old_gets == 0);
		wsdisplay_pollc(0, 1); wsdisplay_pollc(0, 0);
		CHECK(kbd_on == 1 && kbd_off == 1 && old_on == 0 && old_off == 0 && cn_publication == CN_PUBLICATION_CLOSED);
	}
	initialize(); CHECK(simplefb_console_close(NULL) == EPERM); CHECK(simplefb_console_close(&foreign) == EPERM);
	memset(&s, 0xa5, sizeof(s)); memcpy(&saved, &s, sizeof(s));
	CHECK(simplefb_console_close_status(&foreign, &s) == EPERM && memcmp(&s, &saved, sizeof(s)) == 0);
	CHECK(simplefb_console_close_status(lease, NULL) == EINVAL && creates == 0);
	hard_irq = true; CHECK(simplefb_console_close(lease) == EWOULDBLOCK); hard_irq = false;
	soft_irq = true; CHECK(simplefb_console_close(lease) == EWOULDBLOCK); soft_irq = false;
	panicstr = "panic"; CHECK(simplefb_console_close(lease) == EWOULDBLOCK); panicstr = NULL;
	int r = pserialize_read_enter(); CHECK(simplefb_console_close(lease) == EDEADLK);
	CHECK(wsdisplay_bootfb_close(lease) == EDEADLK); pserialize_read_exit(r);
	CHECK(creates == 0 && matches == 0 && cn_publication == 0);
	initialize(); CHECK(cn_publication_enter() == 0); CHECK(simplefb_console_close(lease) == EBUSY);
	CHECK(!cn_publication_closed() && wsdisplay_bootfb.phase == BOOTFB_CLAIMED); cn_publication_exit();
	CHECK(simplefb_console_close(lease) == 0);
	initialize(); wsdisplay_pollc(0, 1); CHECK(simplefb_console_close(lease) == EBUSY && !cn_publication_closed());
	CHECK(old_on == 1 && old_off == 0); wsdisplay_pollc(0, 0); CHECK(old_off == 1 && kbd_off == 1);
	CHECK(cn_publication == 0 && simplefb_console_close(lease) == 0);
	initialize(); keyboard_result = -1; old_result = 67; getc_action = 1;
	CHECK(wsdisplay_getc(0) == 67 && old_gets == 1 && nested_result == EBUSY && cn_publication == 0);
	getc_action = 0; CHECK(simplefb_console_close(lease) == 0);
	initialize(); allocation_hook = cancel_during_allocation;
	CHECK(simplefb_console_close(lease) == EBUSY && allocations_hook == 1);
	CHECK(!cn_publication_closed() && barriers == 0 && creates == destroys);
	for (int a = 1; a <= 5; a++) {
		initialize(); barrier_action = a; int expected = a == 1 ? EWOULDBLOCK : a == 2 || a == 3 || a == 4 ? ENXIO : 0;
		CHECK(simplefb_console_close(lease) == expected); panicstr = NULL;
		status_is(expected == 0, expected); CHECK(barriers == 1);
		CHECK(wsdisplay_bootfb_close(lease) == (a == 4 ? 0 : expected));
		unsigned m = matches; current_blob = &fdt_blob; wsdisplay_console_conf.emulcookie = &cookie;
		CHECK(simplefb_console_close(lease) == expected && barriers == 1 && matches == m);
		if (a == 4) { CHECK(simplefb_console_close_status(lease, &s) == 0); CHECK(s.mi_error == 0 && s.drained && s.md_error == ENXIO); }
	}
	initialize(); current_blob = NULL; CHECK(simplefb_console_close(lease) == ENXIO && creates == 0 && !cn_publication_closed());
	initialize(); wsdisplay_cons_pollmode = 1; CHECK(simplefb_console_close(lease) == EBUSY && !cn_publication_closed());
	initialize(); wsdisplay_early_phase = WSDISPLAY_EARLY_BOUND;
	CHECK(simplefb_console_close(lease) == ENXIO && !cn_publication_closed());
	initialize(); CHECK(cn_publication_freeze() == 0); CHECK(simplefb_console_close(lease) == EALREADY); status_is(false, EALREADY);
	CHECK(barriers == 0 && wsdisplay_bootfb_cancel(lease) == EPERM);
initialize(); wsdisplay_console_conf.emulcookie = NULL;
CHECK(wsdisplay_bootfb_close(lease) == ENXIO && !cn_publication_closed());
initialize(); wsdisplay_early_phase = WSDISPLAY_EARLY_BOUND;
CHECK(wsdisplay_bootfb_close(lease) == ENXIO && !cn_publication_closed());
/* Actual initial replay retains publisher admission through final update. */
replay_action = 1; initialize(); replay_action = 0;
unsigned replay_before = replay_calls, update_before = updates;
wsdisplay_console_initted = 0; wsdisplay_console_device = NULL;
wsdisplay_early_phase = 0; cn_publication = 0; cn_tab = &old_cons;
inject_closed_replay = true;
struct vcons_screen v = { VCONS_IS_VISIBLE };
CHECK(wsdisplay_early_preattach(&screen, &cookie, 0, 0, 0, replay, &v) == EBUSY);
CHECK(replay_calls == replay_before && updates == update_before);
CHECK(wsdisplay_early_phase == WSDISPLAY_EARLY_BOUND && cn_publication_closed());
inject_closed_replay = false;
	/* A real admitted cnputc section delays the controlled perform service. */
	initialize(); pthread_t reader, closer; wait_output = true;
	CHECK(pthread_create(&reader, NULL, reader_thread, NULL) == 0);
	CHECK(pthread_mutex_lock(&gate) == 0); while (!output_entered) CHECK(pthread_cond_wait(&changed, &gate) == 0);
	CHECK(pthread_mutex_unlock(&gate) == 0);
	CHECK(pthread_create(&closer, NULL, close_thread, NULL) == 0);
	CHECK(pthread_mutex_lock(&gate) == 0); while (!perform_entered) CHECK(pthread_cond_wait(&changed, &gate) == 0);
	CHECK(!close_done && atomic_load(&readers) == 1); release_output = true; pthread_cond_broadcast(&changed);
	CHECK(pthread_mutex_unlock(&gate) == 0);
	CHECK(pthread_join(reader, NULL) == 0 && pthread_join(closer, NULL) == 0);
	CHECK(close_done && output_calls == 1 && old_output == 1 && atomic_load(&readers) == 0); status_is(true, 0);
}
int main(void)
{
	pthread_mutexattr_t a; CHECK(pthread_mutexattr_init(&a) == 0);
	CHECK(pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE) == 0);
	CHECK(pthread_mutex_init(&giant_mutex, &a) == 0); CHECK(pthread_mutexattr_destroy(&a) == 0);
	tests(); CHECK(giant == 0 && read_depth == 0 && atomic_load(&readers) == 0);
	CHECK(pthread_mutex_destroy(&giant_mutex) == 0);
	printf("console close actual-source checks: %u PASS\n", atomic_load(&checks));
	return 0;
}
