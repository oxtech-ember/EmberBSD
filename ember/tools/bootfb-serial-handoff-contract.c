/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual joined boot job, issuer, close, facade and PLCOM provider. */
#include <sys/queue.h>
#include <limits.h>
#include <stdarg.h>
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
#include "bcm2712_display_handoffvar.h"
#include "display-types.h"
#include "hsm-types.h"
#include "acpi-types.h"
#include "bvb-types.h"
#include "core-types.h"
#include "disp-types.h"
#include "bcm2712_display_quiesce.h"
#include "native-owner.h"
static struct device gpu;
static struct bcmv3d_softc gpu_sc;
static unsigned renderer_calls, joined;
static int renderer_error, query_error, firmware_error, firmware_reply, firmware_gate;
static bool expect_closed;
static unsigned firmware_calls, firmware_status_queries, firmware_diagnostics;
static bool firmware_permitted, firmware_reenter;
static uint32_t firmware_length;
#include "bootfb-display-services.h"
static int bcmv3d_bootfb_notify(struct bcmv3d_softc *);
static void bcmv3d_bootfb_claim_job(device_t);
extern void serial_fixture_drop_hold(void);
#define BCMMBOX_CHANARM2VC 8
static const uint8_t notify_request[24] = {
	24, 0, 0, 0, 0, 0, 0, 0, 0x66, 0, 3, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};
static const uint8_t notify_reply[24] = {
	24, 0, 0, 0, 0, 0, 0, 0x80, 0x66, 0, 3, 0,
	0, 0, 0, 0, 0, 0, 0, 0x80, 0, 0, 0, 0
};
/* Independent24-byte reply: desired length4, but value buffer and payload0. */
static const uint8_t notify_truncated_reply[24] = {
	24, 0, 0, 0, 0, 0, 0, 0x80, 0x66, 0, 3, 0,
	0, 0, 0, 0, 4, 0, 0, 0x80, 0, 0, 0, 0
};
/* Mailbox service is controlled. The fixed exchange and protocol source are real. */
static int bcmmbox_request(uint8_t channel, void *packet, size_t length, uint32_t *response)
{
	if (length != 24) return display_mailbox(channel,packet,length,response);
	CHECK(firmware_permitted);
	CHECK(channel == 8 && length == 24 && response != NULL);
	CHECK(packet != NULL && ((uintptr_t)packet & 15) == 0);
	CHECK(memcmp(packet, notify_request, sizeof(notify_request)) == 0);
	CHECK(gpu_sc.sc_bootfb.phase == BCMV3D_BOOTFB_CLOSED && serial_fixture_held());
	CHECK(!cold && !init_started && giant > 0 && cn_publication_closed());
	CHECK(gpu_sc.sc_bootfb.firmware.attempted && !gpu_sc.sc_bootfb.firmware.complete);
	firmware_calls++;
	if (firmware_reenter) {
		CHECK(bcmv3d_bootfb_notify(&gpu_sc) == EBUSY && firmware_calls == 1);
		bcmv3d_bootfb_claim_job(&gpu);
		CHECK(gpu_sc.sc_bootfb.phase == BCMV3D_BOOTFB_CLOSED && firmware_calls == 1);
	}
	if (firmware_error != 0)
		return firmware_error;
	memcpy(packet, notify_reply, sizeof(notify_reply));
	if (firmware_reply == 1) ((uint8_t *)packet)[4] ^= 1;
	if (firmware_reply == 2) ((uint8_t *)packet)[19] = 0;
	if (firmware_reply == 3)
		memcpy(packet, notify_truncated_reply, sizeof(notify_truncated_reply));
	if (firmware_reply == 4 || firmware_reply == 5) {
		((uint8_t *)packet)[16] = (uint8_t)firmware_length;
		((uint8_t *)packet)[17] = (uint8_t)(firmware_length >> 8);
		((uint8_t *)packet)[18] = (uint8_t)(firmware_length >> 16);
		((uint8_t *)packet)[19] = (uint8_t)(firmware_length >> 24);
		if (firmware_reply == 4) ((uint8_t *)packet)[19] |= 0x80;
	}
	*response = 0x100;
	return 0;
}
/* Capture exactly the real diagnostic. Request bytes are never a failed reply. */
static void firmware_message(const char *format, ...)
{
	char message[192], expected[192];
	va_list ap;
	int n;
	uint32_t tag_response = 0x80000000U;

	if (firmware_reply == 2) tag_response = 0;
	if (firmware_reply == 3) tag_response = 0x80000004U;
	if (firmware_reply == 4) tag_response |= firmware_length;
	if (firmware_reply == 5) tag_response = firmware_length;
	firmware_diagnostics++;
	CHECK(firmware_diagnostics == 1 && firmware_calls == 1);
	va_start(ap, format); n = vsnprintf(message, sizeof(message), format, ap); va_end(ap);
	CHECK(n > 0 && (size_t)n < sizeof(message));
	if (firmware_error != 0) {
		snprintf(expected, sizeof(expected),
		    "bcmv3d: display notify transport error %d; no response\n", firmware_error);
	} else {
		snprintf(expected, sizeof(expected),
		    "bcmv3d: display notify transport error 0; reply "
		    "00000018 %08x 00030066 00000000 %08x 00000000\n",
		    firmware_reply == 1 ? 0x80000001U : 0x80000000U,
		    tag_response);
	}
	CHECK(strcmp(message, expected) == 0);
}
#undef aprint_normal
#define aprint_normal firmware_message
#include "native-endian.h"
static void print_device_message(device_t d, const char *f, ...) { (void)d; (void)f; }
#define aprint_normal_dev print_device_message
#define aprint_error_dev print_device_message
void bcm2712_display_acpi_observe(device_t d, ACPI_HANDLE h) { CHECK(d == &gpu && h == &gpu); }
static int bcmv3d_observe(struct bcmv3d_softc *s) { CHECK(s == &gpu_sc && gpu.dv_pending == 0); return 0; }
int bcmv3d_takeover_probe(device_t d, bus_space_tag_t t) { CHECK(d == &gpu && t == &tag); return 0; }
static int bcmv3d_render_attach(device_t d, bus_space_tag_t t, bus_dma_tag_t m,
    ACPI_HANDLE h, const struct bcmv3d_bootfb_owner *owner)
{
	CHECK(d == &gpu && t == &tag && m == &gpu && h == &gpu);
	CHECK(gpu.dv_pending == 0 && joined == 1);
	CHECK(owner == (expect_closed ? &gpu_sc.sc_bootfb : NULL));
	renderer_calls++;
	if (display_gate == 11) gpu_sc.sc_bootfb.firmware.attempted = false;
	if (display_gate == 12) gpu_sc.sc_bootfb.firmware.complete = false;
	if (display_gate == 13) gpu_sc.sc_bootfb.firmware.fault = EIO;
	if (display_gate == 14) gpu_sc.sc_bst = NULL;
	if (display_gate == 15) gpu.private = NULL;
	if (display_gate == 16) gpu_sc.sc_bootfb.lease->issued = false;
	if (display_gate == 17) gpu_sc.sc_bootfb.phase = BCMV3D_BOOTFB_RETAINED;
	return renderer_error;

}
static int native_status(struct wsdisplay_bootfb_lease *l, struct simplefb_console_close_status *out)
{
	static struct wsdisplay_bootfb_lease foreign;
	int error;

	firmware_status_queries++;
	if (query_error || (firmware_gate == 8 && firmware_status_queries == 2))
		return EIO;
	error = simplefb_console_close_status(l, out);
	if (error == 0 && firmware_status_queries == 2) {
		switch (firmware_gate) {
		case 1: out->complete = false; break;
		case 2: out->closed = false; break;
		case 3: out->drained = false; break;
		case 4: out->error = EIO; break;
		case 5: out->mi_error = EIO; break;
		case 6: out->md_error = EIO; break;
		case 7: out->attempted = false; break;
		case 9: serial_fixture_drop_hold(); break;
		case 10: gpu_sc.sc_bootfb.lease = &foreign; break;
		}
	}
	if (error == 0 && firmware_status_queries == 3) {
		switch (display_gate) {
		case 1: out->complete = false; break;
		case 2: out->closed = false; break;
		case 3: out->drained = false; break;
		case 4: out->error = EIO; break;
		case 5: out->mi_error = EIO; break;
		case 6: out->md_error = EIO; break;
		case 7: out->attempted = false; break;
		case 8: return EIO;
		case 9: serial_fixture_drop_hold(); break;
		case 10: gpu_sc.sc_bootfb.lease = &foreign; break;
		}
	}
	return error;
}
#define simplefb_console_close_status(...) native_status(__VA_ARGS__)
#define bus_space_map display_map
#define bus_space_peek_4 display_peek
#define bus_space_poke_4 display_poke
#define bus_space_barrier display_barrier
#define delay(us) display_delay(us)
#include "native-job.h"
#undef bus_space_map
#undef bus_space_peek_4
#undef bus_space_poke_4
#undef bus_space_barrier
#undef delay
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
	display_initialize();
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
	firmware_calls = firmware_status_queries = firmware_diagnostics = 0; firmware_error = firmware_reply = firmware_gate = 0;
	firmware_permitted = true; firmware_reenter = false; firmware_length = 0;
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
	CHECK(firmware_diagnostics == firmware_calls);
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
		CHECK(firmware_calls == 0 && !gpu_sc.sc_bootfb.firmware.attempted);
		int first_error = gpu_sc.sc_bootfb.first_error;
		CHECK(bcmv3d_bootfb_notify(&gpu_sc) != 0);
		CHECK(gpu_sc.sc_bootfb.first_error == first_error && firmware_calls == 0);
	}
	native_initialize(false); query_error = 1; expect_closed = false; firmware_permitted = false; native_finish();
	CHECK(gpu_sc.sc_bootfb.phase == BCMV3D_BOOTFB_RETAINED && serial_fixture_held());
	CHECK(gpu_sc.sc_bootfb.cancel_error == 0);
	CHECK(wsdisplay_bootfb.phase == BOOTFB_CLOSED && fallback_calls == 0);
	/* Setup failure after CLOSED must not cancel or run the genfb fallback. */
	native_initialize(false); renderer_error = EIO; native_finish();
	CHECK(gpu_sc.sc_bootfb.phase == BCMV3D_BOOTFB_CLOSED && fallback_calls == 0 && serial_fixture_held());
}
static void tests_firmware(void)
{
	static const int errors[] = { ETIMEDOUT, ENXIO, EIO, EBUSY, EOPNOTSUPP, EINVAL };
	struct bcm2712_display_handoff_status saved;
unsigned calls;

/* Context refusals precede state changes and every mailbox operation. */
native_initialize(false); firmware_permitted = false;
CHECK(bcmv3d_bootfb_notify(NULL) == EINVAL);
CHECK(bcmv3d_bootfb_notify(&gpu_sc) == EWOULDBLOCK);
CHECK(wsdisplay_bootfb_serial_closed(NULL) == EWOULDBLOCK);
cold = false;
CHECK(wsdisplay_bootfb_serial_closed(NULL) == EPERM);
hard_irq = true;
CHECK(bcmv3d_bootfb_notify(&gpu_sc) == EWOULDBLOCK);
CHECK(wsdisplay_bootfb_serial_closed(NULL) == EWOULDBLOCK); hard_irq = false;
soft_irq = true;
CHECK(bcmv3d_bootfb_notify(&gpu_sc) == EWOULDBLOCK);
CHECK(wsdisplay_bootfb_serial_closed(NULL) == EWOULDBLOCK); soft_irq = false;
panicstr = "firmware context";
CHECK(bcmv3d_bootfb_notify(&gpu_sc) == EWOULDBLOCK);
CHECK(wsdisplay_bootfb_serial_closed(NULL) == EWOULDBLOCK); panicstr = NULL;
int rs = pserialize_read_enter();
CHECK(bcmv3d_bootfb_notify(&gpu_sc) == EDEADLK);
CHECK(wsdisplay_bootfb_serial_closed(NULL) == EDEADLK); pserialize_read_exit(rs);
CHECK(firmware_status_queries == 0 && firmware_calls == 0);
CHECK(!gpu_sc.sc_bootfb.firmware.attempted && !gpu_sc.sc_bootfb.firmware.complete);
CHECK(gpu_sc.sc_bootfb.firmware.fault == 0 && gpu_sc.sc_bootfb.first_error == 0);
firmware_permitted = true; native_finish();
CHECK(wsdisplay_bootfb_serial_closed(gpu_sc.sc_bootfb.lease) == 0);
struct wsdisplay_bootfb_lease foreign = {0};
CHECK(wsdisplay_bootfb_serial_closed(&foreign) == EPERM);

for (unsigned first = 0; first < 2; first++) {

		native_initialize(first); firmware_reenter = true; native_finish();
		CHECK(firmware_calls == 1 && gpu_sc.sc_bootfb.firmware.attempted);
		CHECK(gpu_sc.sc_bootfb.firmware.complete && gpu_sc.sc_bootfb.firmware.fault == 0);
		CHECK(gpu_sc.sc_bootfb.first_error == 0 && gpu_sc.sc_bootfb.phase == BCMV3D_BOOTFB_CLOSED);
		CHECK(fallback_calls == 0 && serial_fixture_held());
		memcpy(&saved, &gpu_sc.sc_bootfb.firmware, sizeof(saved));
		bcmv3d_bootfb_claim_job(&gpu);
		CHECK(gpu_sc.sc_bootfb.phase == BCMV3D_BOOTFB_CLOSED);
		CHECK(bcmv3d_bootfb_notify(&gpu_sc) == EBUSY && firmware_calls == 1);
		CHECK(memcmp(&saved, &gpu_sc.sc_bootfb.firmware, sizeof(saved)) == 0);
		CHECK(firmware_diagnostics == 1);
		CHECK(gpu_sc.sc_bootfb.first_error == 0);
	}
	/* The real truncated length4 packet must complete the same native owner. */
	native_initialize(false); firmware_reply = 3; firmware_reenter = true; native_finish();
	CHECK(firmware_calls == 1 && firmware_diagnostics == 1);
	CHECK(gpu_sc.sc_bootfb.firmware.complete && gpu_sc.sc_bootfb.firmware.fault == 0);
	CHECK(gpu_sc.sc_bootfb.first_error == 0 && fallback_calls == 0 && serial_fixture_held());
	CHECK(bcmv3d_bootfb_notify(&gpu_sc) == EBUSY && firmware_calls == 1);
	static const uint32_t lengths[] = {0, 1, 2, 3, 4, 5, 8, 0x100, 0x12345678, 0x7fffffff};
	for (unsigned i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
		native_initialize(false); firmware_reply = 4; firmware_length = lengths[i]; native_finish();
		CHECK(firmware_calls == 1 && firmware_diagnostics == 1);
		CHECK(gpu_sc.sc_bootfb.firmware.complete && gpu_sc.sc_bootfb.firmware.fault == 0);
		CHECK(gpu_sc.sc_bootfb.phase == BCMV3D_BOOTFB_CLOSED && gpu_sc.sc_bootfb.first_error == 0);
		CHECK(fallback_calls == 0 && serial_fixture_held());
		if (lengths[i] == 0) continue;
		native_initialize(false); firmware_reply = 5; firmware_length = lengths[i]; native_finish();
		CHECK(!gpu_sc.sc_bootfb.firmware.complete && gpu_sc.sc_bootfb.firmware.fault == EIO);
		CHECK(gpu_sc.sc_bootfb.first_error == EIO && firmware_calls == 1 && fallback_calls == 0);
		CHECK(bcmv3d_bootfb_notify(&gpu_sc) == EIO && firmware_calls == 1);
	}
	for (unsigned i = 0; i < sizeof(errors) / sizeof(errors[0]); i++) {
		native_initialize(false); firmware_error = errors[i]; native_finish();
		CHECK(firmware_calls == 1 && gpu_sc.sc_bootfb.firmware.attempted);
		CHECK(!gpu_sc.sc_bootfb.firmware.complete && gpu_sc.sc_bootfb.firmware.fault == errors[i]);
		CHECK(gpu_sc.sc_bootfb.first_error == errors[i] && fallback_calls == 0);
		CHECK(gpu_sc.sc_bootfb.phase == BCMV3D_BOOTFB_CLOSED && serial_fixture_held());
		calls = firmware_status_queries; bcmv3d_bootfb_claim_job(&gpu);
		CHECK(bcmv3d_bootfb_notify(&gpu_sc) == errors[i] && firmware_calls == 1);
		CHECK(firmware_status_queries == calls && gpu_sc.sc_bootfb.first_error == errors[i]);
		CHECK(simplefb_console_cancel(gpu_sc.sc_bootfb.lease) == EPERM);
	}
	for (int response = 1; response <= 2; response++) {
		int expected = response == 1 ? EIO : EOPNOTSUPP;
		native_initialize(false); firmware_reply = response; native_finish();
		CHECK(firmware_calls == 1 && gpu_sc.sc_bootfb.firmware.attempted);
		CHECK(!gpu_sc.sc_bootfb.firmware.complete && gpu_sc.sc_bootfb.firmware.fault == expected);
		CHECK(gpu_sc.sc_bootfb.first_error == expected && fallback_calls == 0);
		CHECK(bcmv3d_bootfb_notify(&gpu_sc) == expected && firmware_calls == 1);
	}
	for (int gate = 1; gate <= 10; gate++) {
		native_initialize(false); firmware_gate = gate; firmware_permitted = false; native_finish();
		CHECK(gpu_sc.sc_bootfb.phase == BCMV3D_BOOTFB_CLOSED && fallback_calls == 0);
		CHECK(firmware_calls == 0 && !gpu_sc.sc_bootfb.firmware.attempted);
		CHECK(!gpu_sc.sc_bootfb.firmware.complete && gpu_sc.sc_bootfb.firmware.fault != 0);
		CHECK(gpu_sc.sc_bootfb.first_error == gpu_sc.sc_bootfb.firmware.fault);
		calls = firmware_status_queries;
		CHECK(bcmv3d_bootfb_notify(&gpu_sc) == gpu_sc.sc_bootfb.firmware.fault);
		CHECK(firmware_calls == 0 && firmware_status_queries == calls && firmware_diagnostics == 0);
	}
	/* A raw CPU close has complete value facts, but no held serial route. */
	native_initialize(false); cold = false;
	struct wsdisplay_bootfb_lease *raw;
	CHECK(simplefb_console_claim(&raw, &parsed) == 0 && simplefb_console_close(raw) == 0);
	gpu_sc.sc_bootfb.lease = raw; gpu_sc.sc_bootfb.phase = BCMV3D_BOOTFB_CLOSED;
	firmware_permitted = false;
	CHECK(bcmv3d_bootfb_notify(&gpu_sc) == ENXIO);
	CHECK(firmware_calls == 0 && !gpu_sc.sc_bootfb.firmware.attempted && !serial_fixture_held());
	native_finish(); CHECK(fallback_calls == 0);
	/* Fixed transport rejects foreign context, size and NULL packet before I/O. */
	native_initialize(false); firmware_permitted = false;
	uint8_t packet[24] = {0};
	CHECK(bcmv3d_bootfb_exchange(&gpu, packet, sizeof(packet)) == EINVAL);
	CHECK(bcmv3d_bootfb_exchange(NULL, packet, sizeof(packet) - 4) == EINVAL);
	CHECK(bcmv3d_bootfb_exchange(NULL, NULL, sizeof(packet)) == EINVAL && firmware_calls == 0 && firmware_diagnostics == 0);
	firmware_permitted = true; native_finish();
}
static void display_retry(int expected)
{
	unsigned calls = display_clock_calls, maps = display_map_calls, bus = qb.operations;
	int first = gpu_sc.sc_bootfb.first_error;
	CHECK(bcmv3d_bootfb_stop_display(&gpu_sc) == expected);
	bcmv3d_bootfb_claim_job(&gpu);
	CHECK(bcmv3d_finalize(&gpu) == 0);
	CHECK(display_clock_calls == calls && display_map_calls == maps && qb.operations == bus);
	CHECK(gpu_sc.sc_bootfb.first_error == first && fallback_calls == 0);
}
static void tests_display(void)
{
	unsigned calls, operations;
	native_initialize(false); display_permitted = false;
	CHECK(bcmv3d_bootfb_stop_display(NULL) == EINVAL);
	CHECK(bcmv3d_bootfb_stop_display(&gpu_sc) == EWOULDBLOCK);
	cold = false; hard_irq = true;
	CHECK(bcmv3d_bootfb_stop_display(&gpu_sc) == EWOULDBLOCK); hard_irq = false;
	soft_irq = true; CHECK(bcmv3d_bootfb_stop_display(&gpu_sc) == EWOULDBLOCK); soft_irq = false;
	panicstr = "display context"; CHECK(bcmv3d_bootfb_stop_display(&gpu_sc) == EWOULDBLOCK); panicstr = NULL;
	int rs = pserialize_read_enter(); CHECK(bcmv3d_bootfb_stop_display(&gpu_sc) == EDEADLK); pserialize_read_exit(rs);
	CHECK(display_clock_calls == 0 && display_map_calls == 0 && !gpu_sc.sc_bootfb.display.attempted);
	display_permitted = true; native_finish();
	for (unsigned first = 0; first < 2; first++) {
		for (unsigned gen = 0x53; gen <= 0x54; gen++) {
			native_initialize(first); qb_initialize(gen,true); display_reenter = true; native_finish();
			CHECK(gpu_sc.sc_bootfb.display.complete && gpu_sc.sc_bootfb.display.fault == 0);
			CHECK(gpu_sc.sc_bootfb.display.stop.complete && gpu_sc.sc_bootfb.display.mapped == 3);
			CHECK(gpu_sc.sc_bootfb.phase == BCMV3D_BOOTFB_CLOSED && serial_fixture_held());
			CHECK(qb.writes == 5 && qb.microseconds == 20000 && (gpu_sc.sc_bootfb.display.stop.control & 0x40000000U) != 0);
			for (unsigned i = 0; i < 4; i++) CHECK(display_firmware[i].sets == 0 && display_firmware[i].rate == 240000000);
			calls = display_clock_calls; operations = qb.operations;
			display_retry(EBUSY);
		}
	}
	/* Activation is positive and never lowers any inherited clock rate. */
	native_initialize(false); qb_initialize(0x54,true);
	for (unsigned i = 0; i < 4; i++) display_firmware[i].state = 0;
	display_firmware[0].rate = display_firmware[1].rate = display_firmware[3].rate = 0;
	native_finish(); CHECK(gpu_sc.sc_bootfb.display.complete);
	for (unsigned i = 0; i < 4; i++) CHECK(display_firmware[i].state == 1 && display_firmware[i].rate > 0 && display_firmware[i].enables == 1 && display_firmware[i].disables == 0);
	CHECK(display_firmware[0].rate >= 149985000 && display_firmware[1].rate >= 75000000);
	for (unsigned fault = 1; fault <= calls; fault++) {
		native_initialize(false); display_fail_clock = fault; native_finish();
		CHECK(gpu_sc.sc_bootfb.display.attempted && !gpu_sc.sc_bootfb.display.complete);
		CHECK(gpu_sc.sc_bootfb.display.fault == EIO && gpu_sc.sc_bootfb.first_error == EIO);
		CHECK(display_clock_calls == fault && display_map_calls == 0 && qb.operations == 0);
		display_retry(EIO);
	}
	for (unsigned fault = 1; fault <= 5; fault++) {
		native_initialize(false); display_fail_resource = fault; native_finish();
		CHECK(gpu_sc.sc_bootfb.display.fault == EINVAL && display_resource_calls == fault);
		CHECK(display_map_calls == 0 && qb.operations == 0); display_retry(EINVAL);
	}
	for (unsigned fault = 1; fault <= 3; fault++) {
		native_initialize(false); display_fail_map = fault; native_finish();
		CHECK(gpu_sc.sc_bootfb.display.fault == ENOMEM && display_map_calls == fault);
		CHECK(gpu_sc.sc_bootfb.display.mapped == fault-1 && qb.operations == 0); display_retry(ENOMEM);
	}
	for (unsigned fault = 1; fault <= operations; fault++) {
		native_initialize(false); qb_initialize(0x53,true); qb.fault_at = fault; native_finish();
		CHECK(gpu_sc.sc_bootfb.display.fault == EIO && qb.operations == fault);
		CHECK(gpu_sc.sc_bootfb.display.mapped == 3 && !gpu_sc.sc_bootfb.display.complete); display_retry(EIO);
	}
	for (unsigned gate = 1; gate <= 17; gate++) {
		native_initialize(false); display_gate = gate; display_permitted = false; native_finish();
		if (gate == 15) gpu.private = &gpu_sc;
		CHECK(gpu_sc.sc_bootfb.display.fault != 0 && !gpu_sc.sc_bootfb.display.attempted);
		CHECK(display_clock_calls == 0 && display_map_calls == 0 && qb.operations == 0);
		display_retry(gpu_sc.sc_bootfb.display.fault);
	}
	for (unsigned stuck = 0; stuck < 3; stuck++) {
		native_initialize(false); qb_initialize(0x53,true);
		qb.vid_stuck = stuck == 0; qb.pv_stuck = stuck == 1; qb.hvs_stuck = stuck == 2;
		native_finish(); CHECK(!gpu_sc.sc_bootfb.display.complete);
		CHECK(gpu_sc.sc_bootfb.display.fault == (stuck == 1 ? EIO : ETIMEDOUT));
		CHECK(qb.writes == (stuck == 0 ? 1U : stuck == 1 ? 2U : 5U));
		display_retry(gpu_sc.sc_bootfb.display.fault);
	}
	/* Unknown version, active second pipeline, and mismatched PV clock stop before writes. */
	for (unsigned gate = 0; gate < 5; gate++) {
		native_initialize(false); qb_initialize(0x54,true);
		if (gate == 0) qb.regs[0][0] = 0x55;
		if (gate == 1) qb.regs[2][0] |= 1;
		if (gate == 2) qb.regs[0][(qb.ctrl+0x40)/4] |= 0x80000000U;
		if (gate == 3) qb.regs[0][(qb.stat+0x80)/4] |= 0x2000;
		if (gate == 4) qb.regs[1][0] |= 4;
		native_finish(); CHECK(gpu_sc.sc_bootfb.display.fault == (gate == 0 ? EOPNOTSUPP : EBUSY));
		CHECK(qb.writes == 0 && qb.delays == 0); display_retry(gpu_sc.sc_bootfb.display.fault);
	}
	/* A failed renderer never invokes the continuation or creates a provider. */
	native_initialize(false); renderer_error = EIO; display_permitted = false; native_finish();
	CHECK(!gpu_sc.sc_bootfb.render_attached && !gpu_sc.sc_bootfb.display.attempted);
	CHECK(bcmv3d_bootfb_stop_display(&gpu_sc) == EPERM);
	CHECK(display_clock_calls == 0 && display_map_calls == 0 && qb.operations == 0);
	/* Raw CPU close cannot upgrade itself with copied firmware value facts. */
	native_initialize(false); cold = false;
	struct wsdisplay_bootfb_lease *raw;
	CHECK(simplefb_console_claim(&raw,&parsed) == 0 && simplefb_console_close(raw) == 0);
	gpu_sc.sc_bootfb.lease = raw; gpu_sc.sc_bootfb.phase = BCMV3D_BOOTFB_CLOSED;
	gpu_sc.sc_bootfb.firmware.attempted = gpu_sc.sc_bootfb.firmware.complete = gpu_sc.sc_bootfb.render_attached = true;
	display_permitted = false;
	CHECK(bcmv3d_bootfb_stop_display(&gpu_sc) == ENXIO && display_clock_calls == 0 && display_map_calls == 0);
	native_finish(); CHECK(fallback_calls == 0);
}
int main(void)
{
	pthread_mutexattr_t a;
	CHECK(pthread_mutexattr_init(&a) == 0 && pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE) == 0);
	CHECK(pthread_mutex_init(&giant_mutex, &a) == 0 && pthread_mutexattr_destroy(&a) == 0);
	tests_native(); tests_firmware(); tests_display();
	CHECK(giant == 0 && read_depth == 0 && creates == destroys && init_started);
	display_initialize();
	CHECK(pthread_mutex_destroy(&giant_mutex) == 0);
	printf("native serial boot handoff: %u actual-source checks PASS\n", atomic_load(&checks));
	return 0;
}
