/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual publication and wsdisplay bodies. Kernel services are controlled. */
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned int checks;
#define CHECK(x) do { checks++; if (!(x)) { \
	fprintf(stderr, "console contract failed %s:%d: %s\n", \
	    __FILE__, __LINE__, #x); abort(); } } while (0)
#define KASSERT CHECK
#define KERNEL_LOCK(n, arg) do { CHECK((n) == 1); giant++; } while (0)
#define KERNEL_UNLOCK_ONE(arg) do { CHECK(giant > 0); giant--; } while (0)
#define KERNEL_LOCKED_P() (giant > 0)
#define ASSERT_SLEEPABLE() CHECK(!hard_irq && !soft_irq)
#define atomic_load_acquire(p) __atomic_load_n((p), __ATOMIC_ACQUIRE)
#define atomic_load_relaxed(p) __atomic_load_n((p), __ATOMIC_RELAXED)
#define WSDISPLAY_BOOTFB_HANDOFF 1
#define WSDISPLAY_EARLYFB_HOOKS 1
#define atomic_store_release(p, v) __atomic_store_n((p), (v), __ATOMIC_RELEASE)
#define NWSKBD 1
#define NWSMUX 1
#define WSDISPLAY_MAXSCREEN 2
#define WSDISPLAY_DELSCR_FORCE 1
#define DETACH_FORCE 1
#define MUTEX_DEFAULT 0
#define IPL_NONE 0
#define WSEMULDISPLAYDEVCF_CONSOLE 0
#define WSEMULDISPLAYDEVCF_KBDMUX 1
#define WSEMULDISPLAYDEVCF_CONSOLE_DEFAULT (-1)
#define WSDISPLAYMINOR(u, s) ((u) * 256 + (s))
/* The controlled tty/device service uses one small numeric namespace. */
#ifdef makedev
#undef makedev
#endif
#define makedev(m, n) ((m) * 65536 + (n))

struct device;
typedef struct device *device_t;
typedef const char *cfdriver_t;
struct cfdata { int cf_loc[2]; };
typedef struct cfdata *cfdata_t;
struct cfattach { const char *ca_name; };
struct cfargs { int unused; };
struct lwp { int token; };
static struct lwp thread_a, thread_b, *curlwp = &thread_a;
static int giant;
static bool hard_irq, soft_irq, cold = true;
static const char *panicstr;
static unsigned int replay_calls, read_sections;
static int replay_action;
static int pserialize_read_enter(void) { return read_sections++; }
static void pserialize_read_exit(int s) { CHECK(read_sections == (unsigned int)s + 1); read_sections--; }
static bool cpu_intr_p(void) { return hard_irq; }
static bool cpu_softintr_p(void) { return soft_irq; }
static unsigned int atomic_cas_uint(unsigned int *p, unsigned int old,
    unsigned int value)
{

	(void)__atomic_compare_exchange_n(p, &old, value, false,
	    __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
	return old;
}
struct consdev { int cn_dev; void (*cn_halt)(int); };
struct wsscreen_descr { const char *name; int nrows, ncols; void *textops; };
struct wsemul_ops {
	const char *name;
	void *(*cnattach)(const struct wsscreen_descr *, void *, int, int, long);
};
struct wsscreen_internal {
	void *emulops, *emulcookie, *wsemulcookie;
	const struct wsscreen_descr *scrdata;
	const struct wsemul_ops *wsemul;
};
struct wsscreen_list { int nscreens; };
struct wsdisplay_accessops { int token; };
struct wsevsrc { device_t me_dispdv, me_dv; };
struct wsmux_softc { struct wsevsrc sc_base; };
struct wsdisplay_softc {
	device_t sc_dev;
	bool sc_attach_done, sc_attach_initialized, sc_attach_hold;
	int sc_attach_error, sc_attach_close_error;
	bool sc_isconsole;
	int sc_flagsmtx, sc_flagscv, sc_focusidx;
	struct wsscreen_internal *sc_scr[WSDISPLAY_MAXSCREEN], *sc_focus;
	struct wsevsrc *sc_input;
	const struct wsdisplay_accessops *sc_accessops;
	void *sc_accesscookie;
	const struct wsscreen_list *sc_scrdata;
};
struct device {
	struct wsdisplay_softc softc;
	const char *driver;
	struct cfattach attachment;
	struct cfdata data;
	int unit, refs;
	bool live;
};
typedef struct { bool active; } deviter_t;
struct wsemuldisplaydev_attach_args {
	int console;
	const struct wsscreen_list *scrdata;
	const struct wsdisplay_accessops *accessops;
	void *accesscookie;
};

static struct consdev wsdisplay_cons, foreign_cons, *cn_tab, *wsdisplay_ocn;
static struct wsscreen_internal wsdisplay_console_conf, screen;
static struct wsdisplay_softc *wsdisplay_console_device;
static int wsdisplay_console_initted, wsdisplay_console_attached;
static int wsdisplay_defaultscreens = 2;
static int wsdisplay_cdevsw;
static struct wsmux_softc mux;
static struct device parent, other_parent, child, replacement;
static unsigned int effects, emulator_calls, release_calls, detach_calls;
static unsigned int reader_guards, lookups, stale_accesses;
static bool missing_child, skip_attach, force_attach_error, foreign_child;
static bool disappear_child, replace_child;
static int detach_error;
static int emul_error;

static bool device_is_a(device_t d, const char *name)
{ return strcmp(d->driver, name) == 0; }
static struct cfattach *device_cfattach(device_t d) { return &d->attachment; }
static struct cfdata *device_cfdata(device_t d) { return &d->data; }
static void *device_private(device_t d)
{
	if (!d->live) stale_accesses++;
	CHECK(d->live);
	return &d->softc;
}
static cfdriver_t device_cfdriver(device_t d) { return d->driver; }
static int device_unit(device_t d) { return d->unit; }
static const char *device_xname(device_t d) { return d->driver; }
static void device_release(device_t d)
{ CHECK(d->refs > 0); d->refs--; release_calls++; }
static device_t device_lookup_acquire(cfdriver_t driver, int unit)
{
	device_t d;
	CHECK(reader_guards == 1);
	lookups++;
	CHECK(strcmp(driver, "wsdisplay") == 0 && unit == child.unit);
	d = replace_child ? &replacement : &child;
	if (!d->live) return NULL;
	d->refs++;
	return d;
}
static void deviter_init(deviter_t *it, int flags)
{
	CHECK(flags == 0 && giant > 0);
	it->active = true;
	reader_guards++;
}
static void deviter_release(deviter_t *it)
{
	CHECK(it->active && reader_guards == 1);
	reader_guards--;
	it->active = false;
}
static void aprint_error_dev(device_t d, const char *format, ...)
{ (void)d; (void)format; }
static void aprint_normal(const char *format, ...) { (void)format; }
static void aprint_naive(const char *format, ...) { (void)format; }
static int cdevsw_lookup_major(void *p) { CHECK(p == &wsdisplay_cdevsw); return 7; }
static void mutex_init(int *p, int type, int ipl)
{ CHECK(type == 0 && ipl == 0); *p = 1; effects++; }
static void cv_init(int *p, const char *name)
{ CHECK(strcmp(name, "wssw") == 0); *p = 1; effects++; }
static void mutex_destroy(int *p) { CHECK(*p == 1); *p = 0; effects++; }
static void cv_destroy(int *p) { CHECK(*p == 1); *p = 0; effects++; }
static struct wsmux_softc *wsmux_getmux(int n)
{ CHECK(n >= 0); effects++; return &mux; }
static struct wsmux_softc *wsmux_create(const char *s, int n)
{ CHECK(strcmp(s, "dmux") == 0 && n >= 0); effects++; return &mux; }
static void wsmux_set_display(struct wsmux_softc *m, device_t d)
{ CHECK(m == &mux && d != NULL); effects++; }
static struct wsevsrc *wskbd_set_console_display(device_t d, struct wsevsrc *s)
{ CHECK(d != NULL && s == &mux.sc_base); effects++; return NULL; }
static struct wsscreen_internal *wsscreen_attach(struct wsdisplay_softc *s,
    int a, int b, int c, int d, int e, int f, int g)
{ (void)s; (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g;
  effects++; return &screen; }
static int wsdisplay_addscreen(struct wsdisplay_softc *s, int n, int a, int b)
{ (void)s; (void)n; (void)a; (void)b; effects++; return 0; }
static void wsdisplay_addscreen_print(struct wsdisplay_softc *s, int a, int b)
{ (void)s; (void)a; (void)b; effects++; }
static int wsdisplay_delscreen(struct wsdisplay_softc *s, int n, int flags)
{ (void)flags; s->sc_scr[n] = NULL; effects++; return 0; }
static bool wsdisplay_suspend(device_t d, const void *q) { (void)d; (void)q; return true; }
static bool pmf_device_register(device_t d, bool (*s)(device_t, const void *), void *r)
{ CHECK(d != NULL && s == wsdisplay_suspend && r == NULL); effects++; return true; }
static void *emulator_attach(const struct wsscreen_descr *t, void *cookie,
    int col, int row, long attr)
{ CHECK(t != NULL && cookie != NULL && col >= 0 && row >= 0); (void)attr;
  emulator_calls++; return cookie; }
static const struct wsemul_ops emulator = { "fixture", emulator_attach };
static const struct wsemul_ops *wsemul_pick(int n) { CHECK(n == 0); return &emulator; }
static void halt(int n) { (void)n; effects++; }
static device_t config_found_acquire(device_t, void *, int (*)(void *, const char *),
    const struct cfargs *);
static int config_detach_release(device_t, int);
static int wsemuldisplaydevprint(void *p, const char *s) { (void)p; (void)s; return 0; }

#include "early-api.h"
struct tty { int t_dev; };
struct fixture_uart { device_t sc_dev; unsigned int sc_hwflags; };
static struct consdev meson_uart_consdev, imxucons, exynos_uart_consdev;
static struct consdev plcomcons, comcons;
static unsigned int disable_calls;
#define COM_HW_CONSOLE 1
#define ISSET(v, bits) (((v) & (bits)) != 0)
#include "serial-body.h"
#include "publication-body.h"
#include "display-body.h"

/* This service controls autoconf and reference bookkeeping, not real detach. */
static device_t
config_found_acquire(device_t p, void *aux, int (*print)(void *, const char *),
    const struct cfargs *args)
{
	(void)args;
	CHECK(giant > 0 && print == wsemuldisplaydevprint);
	if (((struct wsemuldisplaydev_attach_args *)aux)->console) {
		struct wsemuldisplaydev_attach_args copied =
		    *(struct wsemuldisplaydev_attach_args *)aux;
		struct lwp *saved = curlwp;

		CHECK(wsdisplay_emul_match(&other_parent, &child.data, aux) == 0);
		CHECK(wsdisplay_emul_match(p, &child.data, &copied) == 0);
		curlwp = &thread_b;
		CHECK(wsdisplay_emul_match(p, &child.data, aux) == 0);
		curlwp = saved;
	}
	if (missing_child) return NULL;
	CHECK(wsdisplay_emul_match(p, &child.data, aux) != 0);
	child.refs++;
	if (foreign_child) child.driver = "foreign";
	else if (!skip_attach) wsdisplay_emul_attach(p, &child, aux);
	if (force_attach_error) {
		child.softc.sc_attach_done = true;
		child.softc.sc_attach_error = emul_error;
	}
	return &child;
}
static int
config_detach_release(device_t d, int flags)
{
	int error;
	CHECK(reader_guards == 1 && giant > 0 && flags == 0);
	detach_calls++;
	device_release(d); /* The real API consumes this reference on every return. */
	CHECK(d->refs == 0); /* No extra localcount reference may pin the drain. */
	if (disappear_child || replace_child) d->live = false;
	if (detach_error != 0) return detach_error;
	error = wsdisplay_emul_detach(d, flags);
	if (error != 0) return error;
	d->live = false;
	return 0;
}

static const struct wsscreen_descr type = { "screen", 24, 80, NULL };
static int cookie;
static struct wsscreen_list list;
static struct wsdisplay_accessops access;
static struct wsemuldisplaydev_attach_args args;
static void
reset(void)
{
	CHECK(giant == 0 && reader_guards == 0);
	memset(&parent, 0, sizeof(parent));
	memset(&other_parent, 0, sizeof(other_parent));
	memset(&child, 0, sizeof(child));
	memset(&replacement, 0, sizeof(replacement));
	memset(&wsdisplay_parent_operation, 0, sizeof(wsdisplay_parent_operation));
	memset(&wsdisplay_console_operation, 0, sizeof(wsdisplay_console_operation));
	memset(&wsdisplay_console_conf, 0, sizeof(wsdisplay_console_conf));
	cn_publication = 0;
	wsdisplay_early_phase = WSDISPLAY_EARLY_NONE;
	wsdisplay_early_type = NULL;
	wsdisplay_early_cookie = NULL;
	panicstr = NULL;
	replay_calls = read_sections = 0;
	replay_action = 0;
	wsdisplay_console_initted = wsdisplay_console_attached = 0;
	wsdisplay_console_device = NULL;
	wsdisplay_ocn = NULL;
	cn_tab = &foreign_cons;
	foreign_cons.cn_halt = halt;
	foreign_cons.cn_dev = 123;
	wsdisplay_cons.cn_dev = 456;
	child.driver = replacement.driver = "wsdisplay";
	child.attachment.ca_name = replacement.attachment.ca_name = "wsdisplay_emul";
	child.data.cf_loc[0] = -1;
	child.unit = replacement.unit = 1;
	child.live = parent.live = other_parent.live = replacement.live = true;
	args = (struct wsemuldisplaydev_attach_args){ 1, &list, &access, &cookie };
	hard_irq = soft_irq = false;
	cold = true;
	missing_child = skip_attach = force_attach_error = foreign_child = false;
	disappear_child = replace_child = false;
	detach_error = emul_error = 0;
	effects = emulator_calls = release_calls = detach_calls = lookups = 0;
	stale_accesses = 0;
	curlwp = &thread_a;
}
static void
publication_tests(void)
{
	reset(); giant = 1;
	CHECK(cn_set_tab_checked(NULL) == EINVAL && cn_tab == &foreign_cons);
	CHECK(cn_publication_enter() == 0);
	CHECK(cn_publication_freeze() == EBUSY && !cn_publication_closed());
	cn_publication_exit();
	CHECK(cn_set_tab_checked(&wsdisplay_cons) == 0);
	CHECK(cn_publication_freeze() == 0 && cn_publication_closed());
	CHECK(cn_set_tab_checked(&foreign_cons) == EBUSY && cn_tab == &wsdisplay_cons);
	CHECK(cn_publication_freeze() == EALREADY);
	CHECK(wsdisplay_cnattach_checked(&type, &cookie, 0, 0, 0) == EBUSY);
	CHECK(wsdisplay_preattach_checked(&type, &cookie, 0, 0, 0) == EBUSY);
	CHECK(emulator_calls == 0 && effects == 0);
	giant = 0;
	reset();
	cn_publication = CN_PUBLICATION_COUNT;
	CHECK(cn_publication_enter() == EOVERFLOW && cn_publication == CN_PUBLICATION_COUNT);
	cn_publication = 0;
	CHECK(wsdisplay_preattach_checked(NULL, &cookie, 0, 0, 0) == EINVAL);
	CHECK(wsdisplay_preattach_checked(&type, NULL, 0, 0, 0) == EINVAL);
	CHECK(wsdisplay_preattach_checked(&type, &cookie, -1, 0, 0) == EINVAL);
	CHECK(wsdisplay_preattach_checked(&type, &cookie, 0, 24, 0) == EINVAL);
	CHECK(effects == 0 && emulator_calls == 0);
	CHECK(wsdisplay_preattach_checked(&type, &cookie, 0, 0, 0) == 0);
	CHECK(wsdisplay_preattach_checked(&type, &cookie, 0, 0, 0) == EBUSY);
	CHECK(wsdisplay_console_enter(&parent) == 0);
	CHECK(wsdisplay_cnattach_checked(&type, &cookie, 0, 0, 0) == 0);
	CHECK(wsdisplay_cnattach_checked(&type, &cookie, 0, 0, 0) == EBUSY);
	CHECK(wsdisplay_predetach_checked() == EBUSY);
	CHECK(wsdisplay_cndetach_checked() == EBUSY);
	wsdisplay_console_exit(&parent);
	giant = 1;
	CHECK(wsdisplay_cndetach_checked() == 0);
	CHECK(wsdisplay_predetach_checked() == EBUSY);
	CHECK(wsdisplay_preattach_checked(&type, &cookie, 0, 0, 0) == 0);
	CHECK(wsdisplay_predetach_checked() == 0);
	giant = 0;
}
static void
reservation_tests(void)
{
	reset();
	CHECK(wsdisplay_console_enter(NULL) == EINVAL && giant == 0);
	hard_irq = true;
	CHECK(wsdisplay_console_enter(&parent) == EWOULDBLOCK && giant == 0);
	hard_irq = false; soft_irq = true;
	CHECK(wsdisplay_console_enter(&parent) == EWOULDBLOCK && giant == 0);
	soft_irq = false;
	CHECK(wsdisplay_console_enter(&parent) == 0 && giant == 1);
	CHECK(wsdisplay_console_enter(&other_parent) == EBUSY && giant == 1);
	curlwp = &thread_b;
	CHECK(wsdisplay_console_enter(&parent) == EBUSY && giant == 1);
	curlwp = &thread_a;
	CHECK(wsdisplay_console_enter(&parent) == 0 && giant == 2);
	CHECK(cn_publication_freeze() == EBUSY);
	wsdisplay_console_exit(&parent);
	CHECK(giant == 1 && wsdisplay_console_operation.depth == 1);
	CHECK(wsdisplay_cnattach_checked(&type, &cookie, 0, 0, 0) == 0);
	CHECK(wsdisplay_console_enter(&parent) == EBUSY && giant == 1);
	wsdisplay_console_exit(&parent);
	CHECK(giant == 0 && cn_publication == 0);
	CHECK(cn_publication_freeze() == 0);
	CHECK(wsdisplay_console_enter(&parent) == EBUSY && giant == 0);
}
static void
replay(void *arg)
{
	CHECK(arg == &cookie && read_sections == 1);
	CHECK(cn_publication_freeze() == EBUSY);
	replay_calls++;
	if (replay_action == 1) panicstr = "fixture panic";
	if (replay_action == 2) cn_tab = &foreign_cons;
}
static void
early_tests(void)
{
	struct wsdisplay_early_output value;

	reset();
	CHECK(cn_publication_freeze() == 0);
	CHECK(wsdisplay_early_preattach(&type, &cookie, 0, 0, 0,
	    replay, &cookie) == EBUSY);
	CHECK(replay_calls == 0 && emulator_calls == 0 && effects == 0);
	CHECK(wsdisplay_early_phase == WSDISPLAY_EARLY_NONE);
	reset();
	CHECK(wsdisplay_early_preattach(&type, &cookie, 0, 24, 0,
	    replay, &cookie) == EINVAL);
	CHECK(replay_calls == 0 && emulator_calls == 0 && cn_publication == 0);
	CHECK(wsdisplay_early_output_snapshot(&value) == 0 && !value.ready);
	reset();
	CHECK(wsdisplay_early_preattach(&type, &cookie, 0, 0, 0,
	    replay, &cookie) == 0);
	CHECK(replay_calls == 1 && read_sections == 0 && cn_publication == 0);
	CHECK(wsdisplay_early_output_snapshot(&value) == 0 && value.ready && value.bound);
	for (int action = 1; action <= 2; action++) {
		reset(); replay_action = action;
		CHECK(wsdisplay_early_preattach(&type, &cookie, 0, 0, 0,
		    replay, &cookie) == (action == 1 ? EWOULDBLOCK : ENXIO));
		CHECK(replay_calls == 1 && read_sections == 0 && cn_publication == 0);
		CHECK(wsdisplay_early_output_snapshot(&value) == 0 && !value.ready);
	}
}
static void
serial_tests(void)
{
	struct tty tp;
	struct fixture_uart uart = { &child, COM_HW_CONSOLE };
	struct consdev *descriptors[] = { &meson_uart_consdev, &imxucons,
	    &exynos_uart_consdev, &plcomcons, &comcons };
	void (*writers[])(struct tty *, struct fixture_uart *, int) = {
	    serial_meson, serial_imx, serial_exynos, serial_plcom, serial_com };

	reset(); cn_tab = &wsdisplay_cons;
	for (unsigned int i = 0; i < 5; i++) {
		tp.t_dev = makedev(7, 1);
		descriptors[i]->cn_dev = 999;
		writers[i](&tp, &uart, 7);
		CHECK(descriptors[i]->cn_dev == makedev(7, 1));
		CHECK(tp.t_dev == makedev(7, 1) && wsdisplay_cons.cn_dev == 456);
	}
	disable_calls = 0;
	CHECK(serial_detach(&uart, 0) == EBUSY);
	CHECK(serial_detach(&uart, 2) == EBUSY && disable_calls == 0);
	uart.sc_hwflags = 0;
	CHECK(serial_detach(&uart, 0) == 0 && disable_calls == 1);
}
static int
rejected_child(device_t *output)
{
	int error;

	CHECK(wsdisplay_console_enter(&parent) == 0);
	error = wsdisplay_emul_attach_checked(&parent, &args, NULL, output);
	wsdisplay_console_exit(&parent);
	return error;
}
static void
parent_tests(void)
{
	device_t output, poison = &other_parent;
	unsigned int saved;

	reset(); output = poison;
	CHECK(wsdisplay_emul_attach_checked(NULL, &args, NULL, &output) == EINVAL);
	hard_irq = true;
	CHECK(wsdisplay_emul_attach_checked(&parent, &args, NULL, &output) == EWOULDBLOCK);
	hard_irq = false;
	CHECK(output == poison && effects == 0 && child.refs == 0);
	CHECK(wsdisplay_emul_attach_checked(&parent, &args, NULL, &output) == EBUSY);
	CHECK(output == poison && effects == 0 && detach_calls == 0);
	CHECK(wsdisplay_emul_match(&parent, &child.data, &args) == 0);
	wsdisplay_emul_attach(&parent, &child, &args);
	CHECK(child.softc.sc_attach_done && child.softc.sc_attach_error == EBUSY);
	CHECK(effects == 0 && foreign_cons.cn_dev == 123);
	reset(); output = poison;
	CHECK(cn_publication_freeze() == 0);
	CHECK(wsdisplay_emul_attach_checked(&parent, &args, NULL, &output) == EBUSY);
	CHECK(output == poison && effects == 0 && child.refs == 0);
	reset(); output = poison;
	CHECK(rejected_child(&output) == EBUSY);
	CHECK(output == poison && effects == 0 && detach_calls == 1 && child.refs == 0);
	CHECK(wsdisplay_cons.cn_dev == 456);
	CHECK(!child.live && !reader_guards && !stale_accesses);
	reset(); output = poison; cn_tab = &wsdisplay_cons;
	CHECK(rejected_child(&output) == EBUSY);
	CHECK(output == poison && effects == 0);
	reset(); output = poison;
	missing_child = true;
	CHECK(rejected_child(&output) == ENXIO);
	CHECK(output == poison && effects == 0 && detach_calls == 0);
	reset(); output = poison;
	foreign_child = true;
	CHECK(rejected_child(&output) == EPROTO);
	CHECK(output == poison && child.refs == 0 && detach_calls == 0);
	reset(); output = poison;
	skip_attach = true;
	CHECK(rejected_child(&output) == EPROTO);
	CHECK(output == poison && child.refs == 0 && detach_calls == 1);
	for (int kind = 0; kind < 3; kind++) {
		reset(); output = poison;
		detach_error = EIO;
		disappear_child = kind == 1;
		replace_child = kind == 2;
		CHECK(rejected_child(&output) == EBUSY);
		CHECK(output == poison && detach_calls == 1 && lookups == 1);
		CHECK(!reader_guards && !stale_accesses && replacement.refs == 0);
		if (kind == 0) {
			CHECK(child.refs == 1 && child.softc.sc_attach_hold);
			CHECK(child.softc.sc_attach_error == EBUSY);
			CHECK(child.softc.sc_attach_close_error == EIO);
			saved = effects;
			CHECK(wsdisplay_emul_detach(&child, DETACH_FORCE) == EBUSY);
			CHECK(effects == saved && child.refs == 1);
		} else CHECK(child.refs == 0);
	}
	reset(); output = poison;
	CHECK(wsdisplay_console_enter(&parent) == 0);
	CHECK(wsdisplay_cnattach_checked(&type, &cookie, 0, 0, 0) == 0);
	CHECK(wsdisplay_emul_attach_checked(&parent, &args, NULL, &output) == 0);
	CHECK(output == &child && child.refs == 1 && child.softc.sc_attach_done);
	CHECK(wsdisplay_console_device == &child.softc && effects > 0);
	CHECK(wsdisplay_cons.cn_dev == makedev(7, WSDISPLAYMINOR(1, 0)));
	CHECK(foreign_cons.cn_dev == 123 && giant == 1 && cn_publication == 1);
	device_release(output);
	wsdisplay_console_exit(&parent);
	CHECK(giant == 0 && cn_publication == 0);
	reset(); output = poison; args.console = 0;
	CHECK(wsdisplay_emul_attach_checked(&parent, &args, NULL, &output) == 0);
	CHECK(output == &child && child.refs == 1 && wsdisplay_console_device == NULL);
	device_release(output);
	CHECK(foreign_cons.cn_dev == 123 && giant == 0 && cn_publication == 0);
	reset(); output = poison;
	CHECK(wsdisplay_console_enter(&other_parent) == 0);
	CHECK(wsdisplay_emul_attach_checked(&parent, &args, NULL, &output) == EBUSY);
	CHECK(output == poison && child.refs == 0 && effects == 0);
	wsdisplay_console_exit(&other_parent);
}
static void
foreign_mutator_test(void)
{
	struct consdev *binding;
	unsigned int count, saved;

	reset();
	CHECK(wsdisplay_preattach_checked(&type, &cookie, 0, 0, 0) == 0);
	CHECK(wsdisplay_console_enter(&parent) == 0);
	count = cn_publication; saved = effects; binding = cn_tab;
	/* Giant is now held by the second thread while the owner sleeps. */
	curlwp = &thread_b;
	CHECK(wsdisplay_predetach_checked() == EBUSY);
	CHECK(cn_tab == binding && wsdisplay_console_initted == 1);
	CHECK(cn_publication == count && effects == saved && giant == 1);
	curlwp = &thread_a;
	wsdisplay_console_exit(&parent);
}
static void
mutator_context_tests(void)
{
	unsigned int count, saved;
	struct consdev *binding;

	reset();
	CHECK(cn_set_tab_checked(&wsdisplay_cons) == EDEADLK);
	CHECK(wsdisplay_predetach_checked() == EDEADLK);
	CHECK(wsdisplay_cndetach_checked() == EDEADLK);
	CHECK(wsdisplay_cnattach_checked(&type, &cookie, 0, 0, 0) == EDEADLK);
	CHECK(effects == 0 && emulator_calls == 0 && cn_publication == 0);
	CHECK(wsdisplay_preattach_checked(&type, &cookie, 0, 0, 0) == 0);
	CHECK(wsdisplay_console_enter(&parent) == 0);
	count = cn_publication; saved = effects; binding = cn_tab;
	/* Model a sleeping owner's dropped Giant, then another thread's entry. */
	curlwp = &thread_b;
	CHECK(wsdisplay_predetach_checked() == EBUSY);
	CHECK(wsdisplay_cndetach_checked() == EBUSY);
	CHECK(wsdisplay_cnattach_checked(&type, &cookie, 0, 0, 0) == EBUSY);
	CHECK(cn_set_tab_checked(&foreign_cons) == EBUSY);
	CHECK(cn_tab == binding && wsdisplay_console_initted == 1);
	CHECK(cn_publication == count && effects == saved && giant == 1);
	curlwp = &thread_a;
	CHECK(wsdisplay_predetach_checked() == EBUSY);
	CHECK(cn_publication == count && cn_tab == binding);
	wsdisplay_console_exit(&parent);
	reset(); cold = false;
	CHECK(wsdisplay_preattach_checked(&type, &cookie, 0, 0, 0) == EBUSY);
	CHECK(effects == 0 && emulator_calls == 0 && cn_publication == 0);
	for (int irq = 0; irq < 2; irq++) {
		reset(); giant = 1; hard_irq = irq == 0; soft_irq = irq == 1;
		CHECK(wsdisplay_predetach_checked() == EWOULDBLOCK);
		CHECK(wsdisplay_cndetach_checked() == EWOULDBLOCK);
		CHECK(wsdisplay_cnattach_checked(&type, &cookie, 0, 0, 0) == EWOULDBLOCK);
		CHECK(cn_set_tab_checked(&foreign_cons) == EWOULDBLOCK);
		CHECK(effects == 0 && emulator_calls == 0 && cn_publication == 0);
		giant = 0;
	}
}

int
main(void)
{
	if (getenv("CONSOLE_CASE") != NULL) {
		CHECK(strcmp(getenv("CONSOLE_CASE"), "foreign-mutator") == 0);
		foreign_mutator_test();
		printf("console foreign mutator: %u checks PASS\n", checks);
		return 0;
	}
	publication_tests();
	reservation_tests();
	parent_tests();
	early_tests();
	serial_tests();
	mutator_context_tests();
	printf("console publication: %u checks PASS\n", checks);
	return 0;
}
