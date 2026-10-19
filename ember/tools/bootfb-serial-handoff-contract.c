/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual joined boot job, issuer, close, facade and PLCOM provider. */
#include <sys/queue.h>
#include <limits.h>
#define BOOTFB_SERIAL_CONTRACT 1
#define main console_close_original_main
#include "console-close-contract.c"
#undef main

int *serial_test_giant(void) { return &giant; }
bool *serial_test_cold(void) { return &cold; }
bool *serial_test_irq(void) { return &hard_irq; }
bool *serial_test_softirq(void) { return &soft_irq; }
const char **serial_test_panic(void) { return &panicstr; }
extern struct consdev *serial_fixture_setup(void);
extern void serial_fixture_held_mutations(void), serial_fixture_revoke(void);
extern bool serial_fixture_held(void);
static unsigned serial_puts, serial_gets, serial_on, serial_off;
static dev_t serial_dev, open_expected_dev, competing_dev;
void serial_test_putc(dev_t d, int c) { CHECK(d == serial_dev && c != 0); serial_puts++; }
int serial_test_getc(dev_t d) { CHECK(d == serial_dev); serial_gets++; return 83; }
void serial_test_pollc(dev_t d, int on) { CHECK(d == serial_dev); if (on) serial_on++; else serial_off++; }

typedef void *bus_dma_tag_t;
typedef void *ACPI_HANDLE;
#include "native-owner.h"
static struct device gpu;
static struct bcmv3d_softc gpu_sc;
static unsigned renderer_calls, joined;
static int renderer_error, query_error;
static bool expect_closed;
static void print_device_message(device_t d, const char *f, ...) { (void)d; (void)f; }
#define aprint_normal_dev print_device_message
#define aprint_error_dev print_device_message
static void bcm2712_display_acpi_observe(device_t d, ACPI_HANDLE h) { CHECK(d == &gpu && h == &gpu); }
static int bcmv3d_observe(struct bcmv3d_softc *s) { CHECK(s == &gpu_sc && gpu.dv_pending == 0); return 0; }
int bcmv3d_takeover_probe(device_t d, bus_space_tag_t t) { CHECK(d == &gpu && t == &tag); return 0; }
static int bcmv3d_render_attach(device_t d, bus_space_tag_t t, bus_dma_tag_t m,
    ACPI_HANDLE h, const struct bcmv3d_bootfb_owner *owner)
{
	CHECK(d == &gpu && t == &tag && m == &gpu && h == &gpu);
	CHECK(gpu.dv_pending == 0 && joined == 1);
	CHECK(owner == (expect_closed ? &gpu_sc.sc_bootfb : NULL));
	renderer_calls++; return renderer_error;
}
static int native_status(struct wsdisplay_bootfb_lease *l, struct simplefb_console_close_status *out)
{ return query_error ? EIO : simplefb_console_close_status(l, out); }
#define simplefb_console_close_status(...) native_status(__VA_ARGS__)
#include "native-job.h"
#undef simplefb_console_close_status

/* Actual cnopen dispatch; vnode and tty driver services remain controlled. */
struct vnode { dev_t device; };
static struct vnode console_vnode;
static int cn_lock, opened;
#undef minor
#define minor(d) ((unsigned)(d) & 0xffffU)
#define NNULLCONS 0
#define LK_EXCLUSIVE 1
#define LK_RETRY 2
#define VOP_UNLOCK(v) ((void)(v))
static void panic(const char *m) { fprintf(stderr, "unexpected panic: %s\n", m); abort(); }
static int cdevvp(dev_t d, struct vnode **out) { CHECK(d == open_expected_dev); console_vnode.device = d; *out = &console_vnode; return 0; }
static void vn_lock(struct vnode *v, int f) { CHECK(v == &console_vnode && f == 3); }
static void *kauth_cred_get(void) { return NULL; }
static int VOP_OPEN(struct vnode *v, int f, void *cred) { CHECK(v->device == open_expected_dev && f == 7 && cred == NULL); opened++; return 0; }
#include "native-cnopen.h"
#define WSDISPLAYMINOR(u,s) ((u) * 256 + (s))
static unsigned device_unit(device_t d) { CHECK(d == &gpu); return 0; }
#include "native-cdev-commit.h"

static void native_initialize(bool renderer_first)
{
	CHECK(TAILQ_EMPTY(&config_finalize_list) && TAILQ_EMPTY(&config_pending));
	CHECK(TAILQ_EMPTY(&interrupt_config_queue));
	config_finalize_done = 0; init_started = false; joined = 0;
	KERNEL_LOCK(1, NULL);
	fixture_previous = serial_fixture_setup(); serial_dev = fixture_previous->cn_dev; open_expected_dev = serial_dev;
	KERNEL_UNLOCK_ONE(NULL);
	cold = true; initialize();
	CHECK(wsdisplay_ocn == fixture_previous && wsdisplay_cons.cn_dev == NODEV);
	CHECK(wsdisplay_bootfb.phase == BOOTFB_DEFERRED && !wsdisplay_bootfb_lease.issued);
	memset(&gpu, 0, sizeof(gpu)); memset(&gpu_sc, 0, sizeof(gpu_sc)); gpu.private = &gpu_sc;
	gpu_sc.sc_dev = &gpu; gpu_sc.sc_bst = &tag; gpu_sc.sc_dmat = &gpu; gpu_sc.sc_handle = &gpu;
	if (renderer_first) {
		struct finalize_hook *fb = TAILQ_FIRST(&config_finalize_list);
		CHECK(fb != NULL); TAILQ_REMOVE(&config_finalize_list, fb, f_list);
		CHECK(config_finalize_register(&gpu, bcmv3d_finalize) == 0);
		TAILQ_INSERT_TAIL(&config_finalize_list, fb, f_list);
	} else CHECK(config_finalize_register(&gpu, bcmv3d_finalize) == 0);
	renderer_calls = renderer_error = query_error = 0; expect_closed = true;
	serial_puts = serial_gets = serial_on = serial_off = 0;
	opened = 0; bcmv3d_bootfb_queue(&gpu_sc);
	CHECK(gpu.dv_pending == 1 && !TAILQ_EMPTY(&config_pending));
}
static void native_finish(void)
{
	cold = false;
	config_finalize();
	CHECK(gpu.dv_pending == 0 && TAILQ_EMPTY(&config_pending));
	CHECK(TAILQ_EMPTY(&config_finalize_list) && joined == 1 && giant == 0);
	init_started = true;
}
static void held_at_allocation(void)
{
	CHECK(serial_fixture_held() && wsdisplay_bootfb.phase == BOOTFB_CLOSING);
	CHECK(wsdisplay_cons.cn_dev == serial_dev && !cn_publication_closed());
	struct wsdisplay_bootfb_lease foreign = {0};
	wsdisplay_early_serial_cancel(&foreign);
	CHECK(wsdisplay_cons.cn_dev == serial_dev && serial_fixture_held());
	CHECK(wsdisplay_bootfb_cancel(&wsdisplay_bootfb_lease) == EPERM);
	serial_fixture_held_mutations();
}
static void held_at_barrier(void)
{
	CHECK(giant == 0 && serial_fixture_held() && cn_publication_closed());
	serial_fixture_held_mutations();
}
static void freeze_during_poll_admission(void) { native_finish(); }
static void competing_console_at_allocation(void)
{
	static char competing_cookie;
	CHECK(serial_fixture_held() && wsdisplay_cons.cn_dev == serial_dev);
	CHECK(wsdisplay_console_enter(&gpu) == 0);
	CHECK(wsdisplay_cnattach_checked(&screen, &competing_cookie, 0, 0, 0) == 0);
	fixture_cdev_commit(&gpu, 17);
	competing_dev = wsdisplay_cons.cn_dev; open_expected_dev = competing_dev;
	CHECK(competing_dev != NODEV && competing_dev != serial_dev);
	wsdisplay_console_exit(&gpu);
}
static void tests_native(void)
{
	for (unsigned first = 0; first < 2; first++) {
		native_initialize(first); allocation_hook = held_at_allocation; perform_hook = held_at_barrier;
		native_finish(); lease = gpu_sc.sc_bootfb.lease;
		CHECK(gpu_sc.sc_bootfb.phase == BCMV3D_BOOTFB_CLOSED && renderer_calls == 1);
		CHECK(wsdisplay_bootfb.phase == BOOTFB_CLOSED && fallback_calls == 0 && barriers == 1);
		CHECK(wsdisplay_console_generation == 2 && serial_fixture_held());
		CHECK(cn_tab == &wsdisplay_cons && cn_tab->cn_dev == serial_dev);
		CHECK(simplefb_console_cancel(lease) == EPERM);
		CHECK(simplefb_console_close_serial(lease) == 0 && barriers == 1);
		CHECK(cnopen(0, 7, 0, curlwp) == 0 && opened == 1);
		CHECK(cnopen(0, 7, 0, curlwp) == 0 && opened == 1);
		wsdisplay_cnputc(0, 's'); CHECK(wsdisplay_getc(0) == 83);
		wsdisplay_pollc(0, 1); wsdisplay_pollc(0, 0);
		CHECK(serial_puts == 1 && serial_gets == 1 && serial_on == 1 && serial_off == 1);
		CHECK(output_calls == 0 && old_output == 0 && kbd_gets == 0 && kbd_on == 0 && kbd_off == 0);
		KERNEL_LOCK(1, NULL); cn_bootfb_serial_release(lease); CHECK(serial_fixture_held()); KERNEL_UNLOCK_ONE(NULL);
	}
	/* Deterministic freeze after the poll admission load, before its CAS. */
	native_initialize(false); cold = false; admission_hook = freeze_during_poll_admission;
	wsdisplay_pollc(0, 1); wsdisplay_pollc(0, 0);
	CHECK(gpu_sc.sc_bootfb.phase == BCMV3D_BOOTFB_CLOSED && barriers == 1);
	CHECK(serial_on == 1 && serial_off == 1 && kbd_on == 0 && kbd_off == 0);
	CHECK(!wsdisplay_serial_pollmode && !wsdisplay_poll_admitted && !wsdisplay_cons_pollmode);
	/* A real checked competing attach commits its own cdev while allocation yields. */
	native_initialize(false); expect_closed = false; allocation_hook = competing_console_at_allocation;
	native_finish();
	CHECK(gpu_sc.sc_bootfb.phase == BCMV3D_BOOTFB_RETAINED && !cn_publication_closed());
	CHECK(!serial_fixture_held() && wsdisplay_serial_console == NULL && barriers == 0);
	CHECK(wsdisplay_console_initted == 2 && wsdisplay_cons.cn_dev == competing_dev);
	CHECK(wsdisplay_console_generation == 4 && fallback_calls == 0);
	CHECK(cnopen(0, 7, 0, curlwp) == 0 && opened == 1);
	/* Invalid contexts and foreign/stale leases cannot acquire a provider. */
	native_initialize(false); expect_closed = false;
	{
		struct wsdisplay_bootfb_lease foreign = {0};
		struct wsdisplay_bootfb_lease *issued = NULL;
		dev_t canary = 37;
		CHECK(wsdisplay_bootfb_close_serial(NULL) == EWOULDBLOCK);
		cold = false;
		CHECK(simplefb_console_claim(&issued, &parsed) == 0);
		KERNEL_LOCK(1, NULL);
		CHECK(cn_bootfb_serial_hold(&foreign, fixture_previous, &canary) == EPERM && canary == 37);
		CHECK(cn_bootfb_serial_hold(issued, &old_cons, &canary) == EPERM && canary == 37);
		KERNEL_UNLOCK_ONE(NULL);
		CHECK(simplefb_console_close_serial(NULL) == EPERM);
		CHECK(simplefb_console_close_serial(&foreign) == EPERM);
		hard_irq = true; CHECK(simplefb_console_close_serial(issued) == EWOULDBLOCK); hard_irq = false;
		soft_irq = true; CHECK(simplefb_console_close_serial(issued) == EWOULDBLOCK); soft_irq = false;
		panicstr = "context"; CHECK(simplefb_console_close_serial(issued) == EWOULDBLOCK); panicstr = NULL;
		int rs = pserialize_read_enter(); CHECK(simplefb_console_close_serial(issued) == EDEADLK); pserialize_read_exit(rs);
		CHECK(cn_bootfb_console_unused() == EDEADLK);
		struct proc foreign_proc; curlwp->l_proc = &foreign_proc;
		CHECK(simplefb_console_close_serial(issued) == EPERM); curlwp->l_proc = &proc0;
		CHECK(!serial_fixture_held() && creates == 0 && !cn_publication_closed());
		CHECK(simplefb_console_cancel(issued) == 0);
		CHECK(simplefb_console_close_serial(issued) == EPERM && !serial_fixture_held());
	}
	native_finish(); CHECK(gpu_sc.sc_bootfb.phase == BCMV3D_BOOTFB_REFUSED && fallback_calls != 0); fallback_calls = 0;
	/* Every refusal before freeze preserves the one actual fallback. */
	for (unsigned reason = 0; reason < 6; reason++) {
		native_initialize(false); expect_closed = false;
		if (reason == 0) { KERNEL_LOCK(1, NULL); serial_fixture_revoke(); KERNEL_UNLOCK_ONE(NULL); }
		if (reason == 1) wsdisplay_ocn = &old_cons;
		if (reason == 2) cn_devvp[0] = &console_vnode;
		if (reason == 3) constty = (struct tty *)&cookie;
		if (reason == 4) wsdisplay_cons_pollmode = 1;
		if (reason == 5) { CHECK(cn_publication_enter() == 0); }
		native_finish();
		CHECK(gpu_sc.sc_bootfb.phase == BCMV3D_BOOTFB_CANCELED && !serial_fixture_held());
		CHECK(!cn_publication_closed() && barriers == 0 && fallback_calls != 0);
		if (reason == 5) { cn_publication_exit(); CHECK(wsdisplay_console_generation == 3); }
		fallback_calls = 0;
	}
	for (int fault = 1; fault <= 4; fault++) {
		native_initialize(false); barrier_action = fault; native_finish(); panicstr = NULL;
		CHECK(gpu_sc.sc_bootfb.phase == BCMV3D_BOOTFB_CLOSED && gpu_sc.sc_bootfb.first_error != 0);
		CHECK(wsdisplay_bootfb.phase == BOOTFB_CLOSED && serial_fixture_held() && fallback_calls == 0);
		CHECK(simplefb_console_cancel(gpu_sc.sc_bootfb.lease) == EPERM);
	}
	native_initialize(false); query_error = 1; expect_closed = false; native_finish();
	CHECK(gpu_sc.sc_bootfb.phase == BCMV3D_BOOTFB_RETAINED && serial_fixture_held());
	CHECK(gpu_sc.sc_bootfb.cancel_error == 0);
	CHECK(wsdisplay_bootfb.phase == BOOTFB_CLOSED && fallback_calls == 0);
	/* Setup failure after CLOSED must not cancel or run the genfb fallback. */
	native_initialize(false); renderer_error = EIO; native_finish();
	CHECK(gpu_sc.sc_bootfb.phase == BCMV3D_BOOTFB_CLOSED && fallback_calls == 0 && serial_fixture_held());
}
int main(void)
{
	pthread_mutexattr_t a;
	CHECK(pthread_mutexattr_init(&a) == 0 && pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE) == 0);
	CHECK(pthread_mutex_init(&giant_mutex, &a) == 0 && pthread_mutexattr_destroy(&a) == 0);
	tests_native();
	CHECK(giant == 0 && read_depth == 0 && creates == destroys && init_started);
	CHECK(pthread_mutex_destroy(&giant_mutex) == 0);
	printf("native serial boot handoff: %u actual-source checks PASS\n", atomic_load(&checks));
	return 0;
}
