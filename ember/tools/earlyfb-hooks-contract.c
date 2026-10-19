/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual ARM preattach, wsdisplay output/poll and vcons replay bodies. */
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

static atomic_uint checks;
#define CHECK(x) do { checks++; if (!(x)) { \
	fprintf(stderr, "check failed %s:%d: %s\n", __FILE__, __LINE__, #x); \
	abort(); } } while (0)
#define KASSERT(x) CHECK(x)
typedef unsigned char u_char;
typedef unsigned long bus_addr_t, bus_size_t;
typedef uintptr_t bus_space_handle_t;
struct bus_space { int token; };
typedef struct bus_space *bus_space_tag_t;
struct bus_space arm_generic_bs_tag;
#define BUS_SPACE_MAP_LINEAR 1
#define BUS_SPACE_MAP_PREFETCHABLE 2
#define VCONS_SCREEN_IS_STATIC 1
#define VCONS_IS_VISIBLE 2
#define SCR_GRAPHICS 1
#define NODEV ((dev_t)-1)
struct rasops_info { int ri_rows, ri_cols, ri_caps, ri_ops; };
struct wsscreen_descr {
	const char *name;
	int ncols, nrows;
	const void *textops;
	int fontwidth, fontheight, capabilities;
	void *modecookie;
};
struct vcons_screen { struct rasops_info scr_ri; int scr_flags, scr_status; };
struct wsdisplay_accessops {
	void (*ioctl)(void);
	void (*mmap)(void);
	void (*pollc)(void);
};
struct vcons_data {
	void (*init_screen)(void *, struct vcons_screen *, int, long *);
};
struct wsemul_ops {
	void *(*cnattach)(const struct wsscreen_descr *, void *, int, int, long);
	void (*output)(void *, const u_char *, unsigned int, int);
};
struct wsscreen_internal {
	const void *emulops, *scrdata;
	void *emulcookie, *wsemulcookie;
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
	dev_t cn_dev;
};
static struct consdev wsdisplay_cons, old_cons, *cn_tab, *wsdisplay_ocn;
static struct wsscreen_internal wsdisplay_console_conf;
static struct display_device *wsdisplay_console_device;
static int wsdisplay_console_initted, wsdisplay_cons_pollmode;
static bool wsdisplay_multicons_suspended, wsdisplay_multicons_enable;
static void (*wsdisplay_cons_kbd_pollc)(dev_t, int);
struct fixture_msgbuf { int msg_bufr, msg_bufx, msg_bufs; char *msg_bufc; };
static char messages[] = "abc";
static struct fixture_msgbuf messages_info = { 0, 3, 4, messages };
static struct fixture_msgbuf *msgbufp = &messages_info;
static const char *panicstr;
static _Thread_local bool hard_interrupt, soft_interrupt;
static _Thread_local int read_depth;
static atomic_uint readers;
static atomic_uint enters, exits;
static unsigned int output_calls, maps, updates, attaches;
static unsigned int kbd_on, kbd_off, old_on, old_off, old_output;
static bool require_reader, pause_output, pause_update, output_waiting;
static bool update_waiting, release_output, release_update, fail_map;
static bool no_node, inject_at_bound, panic_at_update, reenter, change_binding;
static pthread_mutex_t gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake = PTHREAD_COND_INITIALIZER;
#ifdef WSDISPLAY_EARLYFB_HOOKS
static bool cpu_intr_p(void) { return hard_interrupt; }
static bool cpu_softintr_p(void) { return soft_interrupt; }
static int pserialize_read_enter(void)
{
	read_depth++;
	atomic_fetch_add(&readers, 1);
	enters++;
	return read_depth - 1;
}
static void pserialize_read_exit(int previous)
{
	CHECK(read_depth > 0 && previous == read_depth - 1);
	read_depth--;
	atomic_fetch_sub(&readers, 1);
	exits++;
}
#define atomic_load_acquire(p) __atomic_load_n((p), __ATOMIC_ACQUIRE)
static void fixture_store(unsigned int *, unsigned int);
#define atomic_store_release(p, v) fixture_store((p), (v))
static unsigned int atomic_cas_uint(volatile unsigned int *target,
    unsigned int before, unsigned int after)
{
	(void)__atomic_compare_exchange_n(target, &before, after, false,
	    __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
	return before;
}
#endif
#include "early-api.h"
#include "arm-state.h"
static void wsdisplay_cnputc(dev_t, int);
static void vcons_replay_msgbuf(struct vcons_screen *);
static void arm_simplefb_ioctl(void) { }
static void arm_simplefb_mmap(void) { }
static void arm_simplefb_pollc(void) { }
static void arm_simplefb_init_screen(void *cookie, struct vcons_screen *screen,
    int existing, long *attr)
{
	CHECK(cookie != NULL && screen != NULL && existing == 1);
	*attr = 0;
}
static int arm_simplefb_find_node(void) { return no_node ? -1 : 1; }
static int fdtbus_get_reg(int node, int index, bus_addr_t *addr, bus_size_t *size)
{
	CHECK(node == 1 && index == 0);
	*addr = 4096; *size = 8192;
	return 0;
}
static int of_getprop_uint32(int node, const char *name, uint32_t *out)
{
	CHECK(node == 1);
	if (strcmp(name, "width") == 0) *out = 32;
	else if (strcmp(name, "height") == 0) *out = 16;
	else { CHECK(strcmp(name, "stride") == 0); *out = 128; }
	return 0;
}
static const char *fdtbus_get_string(int node, const char *name)
{
	CHECK(node == 1 && strcmp(name, "format") == 0);
	return "x8r8g8b8";
}
static int bus_space_map(bus_space_tag_t tag, bus_addr_t addr, bus_size_t size,
    int flags, bus_space_handle_t *out)
{
	CHECK(tag == &arm_generic_bs_tag && addr == 4096 && size == 8192);
	CHECK(flags == (BUS_SPACE_MAP_LINEAR | BUS_SPACE_MAP_PREFETCHABLE));
	maps++;
	if (fail_map) return EIO;
	*out = 1;
	return 0;
}
static void *bus_space_vaddr(bus_space_tag_t tag, bus_space_handle_t handle)
{
	CHECK(tag == &arm_generic_bs_tag && handle == 1);
	return &arm_generic_bs_tag;
}
static void wsfont_init(void) { }
static void vcons_earlyinit(struct vcons_data *data, void *cookie,
    struct wsscreen_descr *type, struct wsdisplay_accessops *ops)
{
	CHECK(data == &arm_simplefb_vcons_data && cookie == &arm_simplefb_softc);
	CHECK(type == &arm_simplefb_stdscreen && ops == &arm_simplefb_accessops);
}
static void vcons_init_screen(struct vcons_data *data, struct vcons_screen *scr,
    int existing, long *attr)
{
	data->init_screen(&arm_simplefb_softc, scr, existing, attr);
	scr->scr_ri.ri_rows = 2; scr->scr_ri.ri_cols = 4;
}
static void *cnattach(const struct wsscreen_descr *type, void *cookie,
    int col, int row, long attr)
{
	CHECK(type == &arm_simplefb_stdscreen && cookie == &arm_simplefb_screen.scr_ri);
	CHECK(col == 0 && row == 0 && attr == 0);
#ifdef WSDISPLAY_EARLYFB_HOOKS
	struct wsdisplay_early_output value;
	CHECK(wsdisplay_early_output_snapshot(&value) == 0);
	CHECK(!value.bound && !value.ready);
#endif
	attaches++;
	return &arm_simplefb_screen;
}
static void output(void *cookie, const u_char *bytes, unsigned int count, int kernel)
{
	CHECK(cookie == &arm_simplefb_screen && bytes != NULL && count == 1 && kernel == 1);
#ifdef WSDISPLAY_EARLYFB_HOOKS
	CHECK(require_reader ? read_depth > 0 : read_depth == 0);
#endif
	CHECK(pthread_mutex_lock(&gate) == 0);
	output_calls++;
	if (pause_output) {
		output_waiting = true;
		CHECK(pthread_cond_broadcast(&wake) == 0);
		while (!release_output) CHECK(pthread_cond_wait(&wake, &gate) == 0);
	}
	CHECK(pthread_mutex_unlock(&gate) == 0);
}
static const struct wsemul_ops emul = { cnattach, output };
static const struct wsemul_ops *wsemul_pick(int index)
{
	CHECK(index == 0); return &emul;
}
static void cnputc(int c) { wsdisplay_cnputc(0, c); }
#ifdef WSDISPLAY_MULTICONS
static void wsdisplay_multicons_suspend(bool suspended)
{
#ifdef WSDISPLAY_EARLYFB_HOOKS
	CHECK(read_depth > 0);
#endif
	wsdisplay_multicons_suspended = suspended;
}
#endif
#ifdef WSDISPLAY_EARLYFB_HOOKS
static void never_replay(void *arg) { (void)arg; CHECK(false); }
#endif
static void vcons_update_screen(struct vcons_screen *scr)
{
	CHECK(scr == &arm_simplefb_screen);
#ifdef WSDISPLAY_EARLYFB_HOOKS
	CHECK(read_depth > 0);
	struct wsdisplay_early_output value;
	CHECK(wsdisplay_early_output_snapshot(&value) == 0);
	CHECK(value.bound && !value.ready);
	if (reenter) {
		CHECK(wsdisplay_early_preattach(&arm_simplefb_stdscreen,
		    &arm_simplefb_screen.scr_ri, 0, 0, 0, never_replay, NULL) == EBUSY);
	}
#endif
	CHECK(pthread_mutex_lock(&gate) == 0);
	updates++;
	if (pause_update) {
		update_waiting = true;
		CHECK(pthread_cond_broadcast(&wake) == 0);
		while (!release_update) CHECK(pthread_cond_wait(&wake, &gate) == 0);
	}
	if (panic_at_update) panicstr = "fixture panic";
	if (change_binding) wsdisplay_console_initted = 2;
	CHECK(pthread_mutex_unlock(&gate) == 0);
}
static void old_putc(dev_t dev, int c)
{
	CHECK(dev == 0 && c != 0); old_output++;
}
static void old_pollc(dev_t dev, int on)
{
	CHECK(dev == 0); if (on) old_on++; else old_off++;
}
static void kbd_pollc(dev_t dev, int on)
{
	CHECK(dev == NODEV); if (on) kbd_on++; else kbd_off++;
}
#include "mi-state.h"
#include "arm-functions.h"
#include "wsdisplay-functions.h"
#include "replay-function.h"
#ifdef WSDISPLAY_EARLYFB_HOOKS
static void fixture_store(unsigned int *target, unsigned int value)
{
#ifdef WSDISPLAY_EARLYFB_HOOKS
	if (inject_at_bound && value == WSDISPLAY_EARLY_BOUND) {
		/* Actual preattach returned. Its exact published output is tracked. */
		wsdisplay_cnputc(0, 'i');
	}
#endif
	__atomic_store_n(target, value, __ATOMIC_RELEASE);
}
#endif
static void reset(void)
{
	CHECK(atomic_load(&readers) == 0 && read_depth == 0 && enters == exits);
	memset(&arm_simplefb_softc, 0, sizeof(arm_simplefb_softc));
	memset(&arm_simplefb_screen, 0, sizeof(arm_simplefb_screen));
	memset(&wsdisplay_console_conf, 0, sizeof(wsdisplay_console_conf));
	wsdisplay_console_initted = 0; wsdisplay_console_device = NULL;
	old_cons = (struct consdev){ .cn_putc = old_putc, .cn_pollc = old_pollc };
	cn_tab = &old_cons; wsdisplay_ocn = NULL;
	wsdisplay_multicons_enable = true; wsdisplay_multicons_suspended = false;
	wsdisplay_cons_kbd_pollc = kbd_pollc;
	panicstr = NULL; hard_interrupt = soft_interrupt = false;
	maps = updates = attaches = output_calls = 0;
	kbd_on = kbd_off = old_on = old_off = old_output = 0;
	pause_output = pause_update = output_waiting = update_waiting = false;
	release_output = release_update = fail_map = no_node = false;
	inject_at_bound = panic_at_update = reenter = change_binding = false;
	require_reader = true;
#ifdef WSDISPLAY_EARLYFB_HOOKS
	wsdisplay_early_phase = WSDISPLAY_EARLY_NONE;
#endif
}
static void *attach_thread(void *unused)
{
	(void)unused; arm_simplefb_preattach(); return NULL;
}
static void *output_thread(void *unused)
{
	(void)unused; wsdisplay_cnputc(0, 'x'); return NULL;
}
static void wait_for(bool *flag)
{
	CHECK(pthread_mutex_lock(&gate) == 0);
	while (!*flag) CHECK(pthread_cond_wait(&wake, &gate) == 0);
	CHECK(pthread_mutex_unlock(&gate) == 0);
}
static void release(bool *flag)
{
	CHECK(pthread_mutex_lock(&gate) == 0);
	*flag = true;
	CHECK(pthread_cond_broadcast(&wake) == 0);
	CHECK(pthread_mutex_unlock(&gate) == 0);
}
int main(void)
{
	pthread_t worker;
	unsigned int old;
	reset();
#ifdef WSDISPLAY_EARLYFB_HOOKS
	struct wsdisplay_early_output value, saved;
	memset(&value, 0xa5, sizeof(value)); memcpy(&saved, &value, sizeof(saved));
	CHECK(wsdisplay_early_output_snapshot(NULL) == EINVAL);
	CHECK(wsdisplay_early_output_snapshot(&value) == ENXIO);
	CHECK(memcmp(&saved, &value, sizeof(value)) == 0);
	for (unsigned int i = 0; i < 3; i++) {
		reset(); panicstr = i == 0 ? "panic" : NULL;
		hard_interrupt = i == 1; soft_interrupt = i == 2;
		arm_simplefb_preattach();
		CHECK(maps == 0 && attaches == 0 && updates == 0);
		CHECK(wsdisplay_early_output_snapshot(&value) == ENXIO);
	}
#endif
	reset(); fail_map = true; arm_simplefb_preattach();
	CHECK(maps == 1 && attaches == 0 && updates == 0);
	reset(); no_node = true; arm_simplefb_preattach();
	CHECK(maps == 0 && attaches == 0);
	reset(); inject_at_bound = reenter = true; arm_simplefb_preattach();
	CHECK(maps == 1 && attaches == 1 && updates == 1);
	CHECK(atomic_load(&readers) == 0 && enters == exits);
#ifdef WSDISPLAY_EARLYFB_HOOKS
	CHECK(wsdisplay_early_output_snapshot(&value) == 0);
	CHECK(value.bound && value.ready);
	old = maps; arm_simplefb_preattach(); CHECK(maps == old);
#endif
	/* Existing keyboard/old-console poll pairs remain balanced. */
	wsdisplay_pollc(0, 1); wsdisplay_cnputc(0, 'p'); wsdisplay_pollc(0, 0);
	CHECK(kbd_on == 1 && kbd_off == 1 && wsdisplay_cons_pollmode == 0);
#ifdef WSDISPLAY_MULTICONS
	CHECK(old_on == 1 && old_off == 1 && old_output > 0);
#else
	CHECK(old_on == 0 && old_off == 0 && old_output == 0);
#endif
	/* Hold an actual output callback. No stop/drain is attempted. */
	pause_output = true;
	CHECK(pthread_create(&worker, NULL, output_thread, NULL) == 0);
	wait_for(&output_waiting);
#ifdef WSDISPLAY_EARLYFB_HOOKS
	CHECK(atomic_load(&readers) == 1);
#endif
	release(&release_output); CHECK(pthread_join(worker, NULL) == 0);
	CHECK(atomic_load(&readers) == 0 && enters == exits);
	/* The outer reader must cover the final update after all replay output. */
	reset(); pause_update = true;
	CHECK(pthread_create(&worker, NULL, attach_thread, NULL) == 0);
	wait_for(&update_waiting);
#ifdef WSDISPLAY_EARLYFB_HOOKS
	CHECK(wsdisplay_early_output_snapshot(&value) == 0);
	CHECK(value.bound && !value.ready && atomic_load(&readers) == 1);
#endif
	wsdisplay_cnputc(0, 'l');
	release(&release_update); CHECK(pthread_join(worker, NULL) == 0);
#ifdef WSDISPLAY_EARLYFB_HOOKS
	CHECK(wsdisplay_early_output_snapshot(&value) == 0 && value.ready);
	reset(); panic_at_update = true; arm_simplefb_preattach();
	CHECK(wsdisplay_early_output_snapshot(&value) == 0);
	CHECK(value.bound && !value.ready);
	CHECK(wsdisplay_early_phase == WSDISPLAY_EARLY_BOUND);
	reset(); change_binding = true; arm_simplefb_preattach();
	CHECK(wsdisplay_early_output_snapshot(&value) == 0);
	CHECK(!value.bound && !value.ready);
	CHECK(wsdisplay_early_phase == WSDISPLAY_EARLY_BOUND);
	reset(); arm_simplefb_preattach(); panicstr = "later panic";
	CHECK(wsdisplay_early_output_snapshot(&value) == 0);
	CHECK(value.bound && !value.ready);
	wsdisplay_cnputc(0, 'P');
#endif
	/* A foreign cookie does not become a tracked ARM early backend. */
	reset(); arm_simplefb_preattach(); require_reader = false;
	wsdisplay_console_conf.emulcookie = &old_cons;
	old = enters; wsdisplay_cnputc(0, 'f');
	CHECK(enters == exits);
	(void)old;
	CHECK(atomic_load(&readers) == 0);
#ifdef WSDISPLAY_EARLYFB_HOOKS
	CHECK(wsdisplay_early_output_snapshot(&value) == 0 && !value.bound && !value.ready);
	wsdisplay_console_conf.emulcookie = &arm_simplefb_screen.scr_ri;
	wsdisplay_console_conf.scrdata = &old_cons;
	wsdisplay_cnputc(0, 't');
	wsdisplay_console_conf.scrdata = &arm_simplefb_stdscreen;
	wsdisplay_console_initted = 2;
	wsdisplay_cnputc(0, 'r');
	wsdisplay_console_initted = 1;
	struct display_device runtime = { 0 };
	wsdisplay_console_device = &runtime;
	wsdisplay_cnputc(0, 'd');
	CHECK(wsdisplay_early_output_snapshot(&value) == 0 && !value.bound && !value.ready);
	wsdisplay_console_device = NULL;
	wsdisplay_console_initted = 0;
	old = output_calls;
	wsdisplay_cnputc(0, '0');
	CHECK(output_calls == old && atomic_load(&readers) == 0);
	reset();
	CHECK(wsdisplay_early_preattach(NULL, &runtime, 0, 0, 0, never_replay, NULL) == EINVAL);
	CHECK(wsdisplay_early_preattach(&arm_simplefb_stdscreen, NULL, 0, 0, 0, never_replay, NULL) == EINVAL);
	CHECK(wsdisplay_early_preattach(&arm_simplefb_stdscreen, &runtime, 0, 0, 0, NULL, NULL) == EINVAL);
	for (unsigned int i = 0; i < 3; i++) {
		panicstr = i == 0 ? "panic" : NULL;
		hard_interrupt = i == 1; soft_interrupt = i == 2;
		CHECK(wsdisplay_early_preattach(&arm_simplefb_stdscreen,
		    &runtime, 0, 0, 0, never_replay, NULL) == EWOULDBLOCK);
		CHECK(wsdisplay_early_phase == WSDISPLAY_EARLY_NONE);
	}
reset(); arm_simplefb_preattach();
wsdisplay_console_initted = 0;
old = attaches;
CHECK(wsdisplay_early_preattach(&arm_simplefb_stdscreen,
    &runtime, 0, 0, 0, never_replay, NULL) == EBUSY);
CHECK(attaches == old && wsdisplay_early_phase == WSDISPLAY_EARLY_READY);
reset(); wsdisplay_console_initted = 2;
	CHECK(wsdisplay_early_preattach(&arm_simplefb_stdscreen,
	    &runtime, 0, 0, 0, never_replay, NULL) == EBUSY);
	CHECK(wsdisplay_early_phase == WSDISPLAY_EARLY_NONE);
#endif
	printf("early framebuffer hooks: %u checks PASS (%s)\n", checks,
#ifdef WSDISPLAY_EARLYFB_HOOKS
	    "enabled"
#else
	    "disabled"
#endif
	);
	return 0;
}
