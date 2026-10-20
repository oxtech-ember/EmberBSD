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
#include "mmu4-core-types.h"
#include "mmu4-native-types.h"
#include "mmu4-mmu4-types.h"
#include "mmu4-backing-types.h"
#include "mmu4-adapter-types.h"
#include "publish-hvs-types.h"
#include "publish-pv-types.h"
#include "publish-hdmi-types.h"
#include "publish-types.h"
#include "range-check-types.h"
#include "native-owner.h"
extern void *bootfb_mmu4_reboot(void);
extern void bootfb_mmu4_failure(int);
extern unsigned bootfb_mmu4_calls(void);
extern int bootfb_native_create(struct bcmv3d_device *, struct bcmv3d_mmu4_native **);
extern int bootfb_native_prepare(struct bcmv3d_mmu4_native *);
extern int bootfb_native_program(struct bcmv3d_mmu4_native *);
extern int bootfb_native_status(struct bcmv3d_mmu4_native *, struct bcmv3d_mmu4_native_status *);
static struct bcmv3d_device *fixture_renderer;
static unsigned mmu4_gate, writeback_maps, writeback_reads, writeback_fail_map, writeback_fail_read;
static uint32_t writeback_control[2];
static bool mmu4_permitted, mmu4_reenter;
static int mmu4_tuple_error;
static int bcmv3d_bootfb_program_mmu4(struct bcmv3d_softc *);
static int bcmv3d_bootfb_publish(struct bcmv3d_softc *);
static unsigned hvs_maps, hvs_writeback_reads, hvs_native_queries, hvs_gate;
static unsigned hvs_fail_map, hvs_fail_writeback, hvs_native_fault;
static bool hvs_permitted, hvs_reenter, hvs_progress_delayed, hvs_progress_stuck;
static int hvs_tuple_error;
static uint32_t hvs_busy[2];
static unsigned hvs_callback_case;
static int bcmv3d_publish_read(void *,enum bcmv3d_hvs_publish_window,uint32_t,uint32_t *);
static int bcmv3d_publish_write(void *,enum bcmv3d_hvs_publish_window,uint32_t,uint32_t);
extern void bootfb_publish_locks(void), bootfb_publish_fault(unsigned);
extern int bootfb_publish_status(struct bcmv3d_mmu4_native *, struct bcmv3d_mmu4_native_status *);
static void hvs_before_publish(void);
static int hvs_bus_map(bus_space_tag_t,uint64_t,uint64_t,int,bus_space_handle_t *);
static int hvs_bus_read(bus_space_tag_t,bus_space_handle_t,uint32_t,uint32_t *);
static int hvs_bus_write(bus_space_tag_t,bus_space_handle_t,uint32_t,uint32_t);
static void hvs_bus_barrier(bus_space_tag_t,bus_space_handle_t,unsigned,uint64_t,int);
static void hvs_bus_delay(unsigned);


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
/* Keep the actual extracted decoders distinct from target libc inlines. */
#define le16dec bootfb_fixture_le16dec
#define le32dec bootfb_fixture_le32dec
#include "native-endian.h"
/* Inject a changed prerequisite after actual stop publication, before MMU4. */
static void print_device_message(device_t, const char *, ...)
    __attribute__((format(printf, 2, 3)));
static void print_device_message(device_t dev, const char *f, ...)
{
	struct bcmv3d_bootfb_owner *o = &gpu_sc.sc_bootfb;
	struct bcmv3d_display_stop *d = &o->display;
	(void)dev;
	if (strncmp(f,"boot HVS link:",14) == 0) {
		char actual[512], expected[512];
		va_list ap;

		CHECK(giant > 0 && !o->publish.executing);
		va_start(ap,f); CHECK(vsnprintf(actual,sizeof(actual),f,ap) > 0); va_end(ap);
		CHECK(snprintf(expected,sizeof(expected),
		    "boot HVS link: saved words %u/26; profile %08x/%08x/%08x/%08x/%08x/%08x/%08x/%08x/%08x/%08x/%08x/%08x/%08x/%08x/%08x/%08x/%08x/%08x/%08x/%08x\n", o->publish.status.link_words,
		    o->publish.status.link[0], o->publish.status.link[1], o->publish.status.link[2],
		    o->publish.status.link[3], o->publish.status.link[4], o->publish.status.link[5],
		    o->publish.status.link[6], o->publish.status.link[7], o->publish.status.link[8],
		    o->publish.status.link[9], o->publish.status.link[10], o->publish.status.link[11],
		    o->publish.status.link[12], o->publish.status.link[13], o->publish.status.link[14],
		    o->publish.status.link[15], o->publish.status.link[16], o->publish.status.link[17],
		    o->publish.status.link[18], o->publish.status.link[19]) > 0);
		CHECK(strcmp(actual,expected) == 0);
		return;
	}
	if (strncmp(f,"boot HVS CSC:",13) == 0) {
		char actual[512], expected[512];
		va_list ap;

		CHECK(giant > 0 && !o->publish.executing);
		va_start(ap,f); CHECK(vsnprintf(actual,sizeof(actual),f,ap) > 0); va_end(ap);
		CHECK(snprintf(expected,sizeof(expected),
		    "boot HVS CSC: %08x/%08x/%08x/%08x/%08x/%08x\n",
		    o->publish.status.link[20],o->publish.status.link[21],
		    o->publish.status.link[22],o->publish.status.link[23],
		    o->publish.status.link[24],o->publish.status.link[25]) > 0);
		CHECK(strcmp(actual,expected) == 0);
		return;
	}
	if (strncmp(f,"boot HVS timing:",16) == 0) {
		char actual[512], expected[512];
		const struct bcmv3d_hvs_publish_timing *t = &o->publish.status.timing;
		va_list ap;

		CHECK(giant > 0 && !o->publish.executing);
		va_start(ap,f); CHECK(vsnprintf(actual,sizeof(actual),f,ap) > 0); va_end(ap);
		CHECK(snprintf(expected,sizeof(expected),
		    "boot HVS timing: saved words %u/12; "
		    "PV VCTRL/VSYNCD/HORZA/HORZB/VERTA/VERTB "
		    "%08x/%08x/%08x/%08x/%08x/%08x; "
		    "HDMI HORZA/HORZB/VERTA0/VERTA1/VERTB0/VERTB1 "
		    "%08x/%08x/%08x/%08x/%08x/%08x\n",t->words,
		    t->pv[0],t->pv[1],t->pv[2],t->pv[3],t->pv[4],t->pv[5],
		    t->hdmi[0],t->hdmi[1],t->hdmi[2],t->hdmi[3],t->hdmi[4],t->hdmi[5]) > 0);
		CHECK(strcmp(actual,expected) == 0);
		return;
	}
	if (strncmp(f,"boot MMU4 cached native:",24) == 0) {
		char actual[512], expected[512];
		const struct bcmv3d_bootfb_mmu4 *m = &o->mmu4;
		va_list ap;

		va_start(ap,f); CHECK(vsnprintf(actual,sizeof(actual),f,ap) > 0); va_end(ap);
		CHECK(snprintf(expected,sizeof(expected),
		    "boot MMU4 cached native: valid %u, stage %u, DEBUG %08x valid %u; "
		    "ICAC valid %u, observation stage %u, CONTROL %08x valid %u error %d; "
		    "prepare %d, program %d\n",m->status_valid,(unsigned)m->status.stage,
		    m->status.debug,m->status.debug_valid,m->status.icac_valid,
		    (unsigned)m->status.icac.observation.stage,
		    m->status.icac.observation.raw_control,
		    m->status.icac.observation.raw_valid,m->status.icac.observation.first_error,
		    m->status.prepare_error,m->status.program_error) > 0);
		CHECK(strcmp(actual,expected) == 0);
		return;
	}
	if (strncmp(f,"boot MMU4 at ",13) == 0 && o->mmu4.complete) hvs_before_publish();
	if (strncmp(f,"display stop at ",16) != 0 || !d->complete) return;
	switch (mmu4_gate) {
	case 11: o->firmware.attempted = false; break;
	case 12: o->firmware.complete = false; break;
	case 13: o->firmware.fault = EIO; break;
	case 14: o->renderer = NULL; break;
	case 15: d->attempted = false; break;
	case 16: d->complete = false; break;
	case 17: d->fault = EIO; break;
	case 18: d->mapped = 2; break;
	case 19: d->stop.attempted = false; break;
	case 20: d->stop.snapshot_valid = false; break;
	case 21: d->stop.complete = false; break;
	case 22: d->stop.error = EIO; break;
	case 23: d->hsm = NULL; break;
	case 24: d->bvb = NULL; break;
	case 25: d->core = NULL; break;
	case 26: d->disp = NULL; break;
	case 27: d->stop.pipe[0].complete = false; break;
	case 28: d->stop.pipe[1].complete = false; break;
	case 29: d->stop.pipe[0].error = EIO; break;
	case 30: d->stop.pipe[1].error = EIO; break;
	case 31: d->stop.pipe[0].pv_disabled = false; break;
	case 32: d->stop.pipe[1].pv_disabled = false; break;
	case 33: d->stop.pipe[0].hvs_disabled = false; break;
	case 34: d->stop.pipe[1].hvs_disabled = false; break;
	case 35: d->hsm_status.fault = EIO; break;
	case 36: d->bvb_status.state = 0; break;
	case 37: d->core_status.leases = 0; break;
	case 38: d->disp_status.minimum[0] = 0; break;
	case 39: d->bvb_status.rate = 0; break;
	case 40: gpu.private = NULL; break;
	case 41: o->phase = BCMV3D_BOOTFB_RETAINED; break;
	case 42: CHECK(o->lease != NULL); o->lease->issued = false; break;
	case 43: o->first_error = EPROTO; break;
	}
}

#define aprint_normal_dev print_device_message
#define aprint_error_dev print_device_message
void bcm2712_display_acpi_observe(device_t d, ACPI_HANDLE h) { CHECK(d == &gpu && h == &gpu); }
static int bcmv3d_observe(struct bcmv3d_softc *s) { CHECK(s == &gpu_sc && gpu.dv_pending == 0); return 0; }
int bcmv3d_takeover_probe(device_t d, bus_space_tag_t t) { CHECK(d == &gpu && t == &tag); return 0; }
static int bcmv3d_render_attach(device_t d, bus_space_tag_t t, bus_dma_tag_t m,
    ACPI_HANDLE h, const struct bcmv3d_bootfb_owner *owner, struct bcmv3d_device **out)
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
	if (display_gate == 16) { CHECK(gpu_sc.sc_bootfb.lease != NULL); gpu_sc.sc_bootfb.lease->issued = false; }
	if (display_gate == 17) gpu_sc.sc_bootfb.phase = BCMV3D_BOOTFB_RETAINED;
	CHECK(out != NULL);
	if (renderer_error == 0) *out = fixture_renderer;
	return renderer_error;

}
/* Exact tuple policy body is tested with the real renderer in claim-job fixture. */
static int bcmv3d_render_bootfb_validate(const struct bcmv3d_device *r, device_t d,
    bus_space_tag_t t, bus_dma_tag_t m, ACPI_HANDLE h, const struct bcmv3d_bootfb_owner *o)
{
	if (r == NULL) return EINVAL;
	CHECK(r == fixture_renderer && d == &gpu && t == &tag && m == &gpu && h == &gpu);
	CHECK(o == &gpu_sc.sc_bootfb && giant > 0);
	return gpu_sc.sc_bootfb.publish.attempted || hvs_gate == 40 ? hvs_tuple_error : mmu4_tuple_error;
}
void bootfb_mmu4_check(unsigned stage, const void *p)
{
	CHECK(mmu4_permitted && giant == 0 && !cold);
	CHECK(gpu_sc.sc_bootfb.mmu4.attempted && !gpu_sc.sc_bootfb.mmu4.complete);
	CHECK(gpu_sc.sc_bootfb.renderer == fixture_renderer && serial_fixture_held());
	CHECK(gpu_sc.sc_bootfb.firmware.complete && gpu_sc.sc_bootfb.display.complete);
	CHECK(writeback_maps == 2 && writeback_reads == 2);
	CHECK((writeback_control[0] & 3) == 0 && (writeback_control[1] & 3) == 0);
	if (stage == 1) CHECK(p == fixture_renderer);
	else CHECK(p != NULL && p == gpu_sc.sc_bootfb.mmu4.native);
	if (mmu4_reenter) {
		CHECK(bcmv3d_bootfb_program_mmu4(&gpu_sc) == EBUSY && giant == 0);
		CHECK(!gpu_sc.sc_bootfb.mmu4.complete && gpu_sc.sc_bootfb.mmu4.fault == 0);
	}
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
	if (error == 0 && firmware_status_queries == 4) {
		switch (mmu4_gate) {
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
	if (error == 0 && firmware_status_queries == 5) {
		switch (hvs_gate) {
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
#ifndef __arraycount

#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#endif
#include "publish-hvs-body.h"
#include "publish-pv-body.h"
#include "publish-hdmi-body.h"
#define bcmv3d_mmu4_native_cached_status bootfb_publish_status
#include "publish-body.h"
#undef bcmv3d_mmu4_native_cached_status
#include "bootfb-hvs-services.h"
#define simplefb_console_close_status(...) native_status(__VA_ARGS__)

#define bus_space_map display_map
#define bus_space_peek_4 display_peek
#define bus_space_poke_4 display_poke
#define bus_space_barrier display_barrier
#define delay(us) display_delay(us)
#define bcmv3d_mmu4_native_create bootfb_native_create
#define bcmv3d_mmu4_native_prepare bootfb_native_prepare
#define bcmv3d_mmu4_native_program bootfb_native_program
#define bcmv3d_mmu4_native_cached_status bootfb_native_status
/* The bounded caller has its separate actual-source contract. */
static unsigned range_check_calls;
static int range_check_error;
int
bcmv3d_mmu4_check_ranges(struct bcmv3d_device *renderer,
    struct bcmv3d_mmu4_native *native, struct bcmv3d_mmu4_check_status *s)
{
	CHECK(giant == 0 && !cold && !init_started && serial_fixture_held());
	CHECK(renderer == fixture_renderer && native == gpu_sc.sc_bootfb.mmu4.native);
	CHECK(s == &gpu_sc.sc_bootfb.range_check && !s->attempted);
	CHECK(gpu_sc.sc_bootfb.publish.status.complete && !gpu_sc.sc_bootfb.publish.executing);
	CHECK(gpu_sc.sc_bootfb.mmu4.complete && gpu_sc.sc_bootfb.first_error == 0);
	range_check_calls++;
	s->attempted = true;
	s->complete = range_check_error == 0;
	s->first_error = range_check_error;
	return range_check_error;
}
struct bcmv3d_vblank_native { int unused; };
static struct bcmv3d_vblank_native vblank_fixture;
static unsigned vblank_calls;
static int
bcmv3d_vblank_native_create(struct bcmv3d_device *r,
    const struct bcm2712_display_contract *c, bus_space_handle_t pv,
    bus_space_handle_t hvs, uint32_t variant, struct bcmv3d_vblank_native **out)
{
	struct bcmv3d_bootfb_publish *p = &gpu_sc.sc_bootfb.publish;

	CHECK(giant == 0 && !cold && !init_started && serial_fixture_held());
	CHECK(r == fixture_renderer && c == &gpu_sc.sc_bootfb.display.contract);
	CHECK(p->complete && p->status.complete && !p->executing);
	CHECK(gpu_sc.sc_bootfb.range_check.complete && range_check_calls == 1);
	CHECK(pv == p->maps[BCMV3D_PUBLISH_PV0] && hvs == p->maps[BCMV3D_PUBLISH_HVS]);
	CHECK(variant == (uint32_t)p->frame.variant && out != NULL);
	vblank_calls++;
	*out = &vblank_fixture;
	return 0;
}
#include "native-job.h"
#undef bcmv3d_mmu4_native_create
#undef bcmv3d_mmu4_native_prepare
#undef bcmv3d_mmu4_native_program
#undef bcmv3d_mmu4_native_cached_status

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
fixture_renderer = bootfb_mmu4_reboot();
mmu4_gate = writeback_maps = writeback_reads = writeback_fail_map = writeback_fail_read = 0;
writeback_control[0] = 0x80010000; writeback_control[1] = 0x40020000;
mmu4_permitted = true; mmu4_reenter = false; mmu4_tuple_error = 0;
display_initialize();
	hvs_maps = hvs_writeback_reads = hvs_native_queries = hvs_gate = 0;
	hvs_fail_map = hvs_fail_writeback = hvs_native_fault = 0;
	hvs_permitted = true; hvs_reenter = hvs_progress_delayed = hvs_progress_stuck = false;
	hvs_tuple_error = 0; hvs_partial = hvs_bypass = false; hvs_bypass_mix = 0;
	range_check_calls = 0; range_check_error = 0; vblank_calls = 0;
	hvs_profile_fault = hvs_read_fault = hvs_write_fault = hvs_ignore_write = 0;
	hvs_query_error = 0; hvs_cxm = 1024; hvs_timeout = hvs_eof = false; hvs_busy[0] = hvs_busy[1] = 0; hvs_callback_case = 0;
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
		CHECK(gpu_sc.sc_bootfb.first_error == gpu_sc.sc_bootfb.publish.fault && gpu_sc.sc_bootfb.phase == BCMV3D_BOOTFB_CLOSED);
		CHECK(fallback_calls == 0 && serial_fixture_held());
		memcpy(&saved, &gpu_sc.sc_bootfb.firmware, sizeof(saved));
		bcmv3d_bootfb_claim_job(&gpu);
		CHECK(gpu_sc.sc_bootfb.phase == BCMV3D_BOOTFB_CLOSED);
		CHECK(bcmv3d_bootfb_notify(&gpu_sc) == EBUSY && firmware_calls == 1);
		CHECK(memcmp(&saved, &gpu_sc.sc_bootfb.firmware, sizeof(saved)) == 0);
		CHECK(firmware_diagnostics == 1);
		CHECK(gpu_sc.sc_bootfb.first_error == gpu_sc.sc_bootfb.publish.fault);
	}
	/* The real truncated length4 packet must complete the same native owner. */
	native_initialize(false); firmware_reply = 3; firmware_reenter = true; native_finish();
	CHECK(firmware_calls == 1 && firmware_diagnostics == 1);
	CHECK(gpu_sc.sc_bootfb.firmware.complete && gpu_sc.sc_bootfb.firmware.fault == 0);
	CHECK(gpu_sc.sc_bootfb.first_error == gpu_sc.sc_bootfb.publish.fault && fallback_calls == 0 && serial_fixture_held());
	CHECK(bcmv3d_bootfb_notify(&gpu_sc) == EBUSY && firmware_calls == 1);
	static const uint32_t lengths[] = {0, 1, 2, 3, 4, 5, 8, 0x100, 0x12345678, 0x7fffffff};
	for (unsigned i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
		native_initialize(false); firmware_reply = 4; firmware_length = lengths[i]; native_finish();
		CHECK(firmware_calls == 1 && firmware_diagnostics == 1);
		CHECK(gpu_sc.sc_bootfb.firmware.complete && gpu_sc.sc_bootfb.firmware.fault == 0);
		CHECK(gpu_sc.sc_bootfb.phase == BCMV3D_BOOTFB_CLOSED && gpu_sc.sc_bootfb.first_error == gpu_sc.sc_bootfb.publish.fault);
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
			for (unsigned mask = 1; mask < 4; mask++) {
				native_initialize(first); qb_initialize(gen,mask); display_reenter = true; native_finish();
				CHECK(gpu_sc.sc_bootfb.display.complete && gpu_sc.sc_bootfb.display.fault == 0);
				CHECK(gpu_sc.sc_bootfb.display.stop.complete && gpu_sc.sc_bootfb.display.mapped == 3);
				CHECK(gpu_sc.sc_bootfb.phase == BCMV3D_BOOTFB_CLOSED && serial_fixture_held());
				CHECK(qb.writes == (mask == 3 ? 10U : 5U) && qb.microseconds == 20000 && qb.long_delays == 1);
				for (unsigned i = 0; i < 2; i++) CHECK(gpu_sc.sc_bootfb.display.stop.pipe[i].complete && gpu_sc.sc_bootfb.display.stop.pipe[i].error == 0);
				for (unsigned i = 0; i < 4; i++) CHECK(display_firmware[i].sets == 0 && display_firmware[i].rate == 240000000);
				calls = display_clock_calls; operations = qb.operations;
				display_retry(EBUSY);
			}
		}
	}
	/* Independent actual two-pipeline firmware words; keep all unrelated bits. */
	native_initialize(false); qb_initialize(0x54,3);
	qb.regs[0][0] = 0x00002454; qb.regs[0][8] = 0x802000f0;
	qb.regs[0][0x100/4] = 0x877f0437; qb.regs[0][0x118/4] = 0x00b841eb;
	qb.regs[0][0x140/4] = 0x827f01df; qb.regs[0][0x158/4] = 0x00b8409f;
	qb.regs[0][0x180/4] = 0; qb.regs[0][0x198/4] = 0x00400000;
	for (unsigned i = 1; i <= 2; i++) { qb.regs[i][0] = 0x07174001; qb.regs[i][1] = 3; }
	native_finish(); CHECK(gpu_sc.sc_bootfb.display.complete && qb.writes == 10);
	CHECK(qb.regs[0][8] == 0x802000f0 && qb.regs[0][0x180/4] == 0);
	for (unsigned i = 0; i < 2; i++) {
		CHECK(gpu_sc.sc_bootfb.display.stop.pipe[i].pv_video == 2);
		CHECK((gpu_sc.sc_bootfb.display.stop.pipe[i].control & 0x40000000U) != 0);
	}
	display_retry(EBUSY);
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
		native_initialize(false); qb_initialize(0x53,3); qb.fault_at = fault; native_finish();
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
	for (unsigned port = 0; port < 2; port++) {
		for (unsigned stuck = 0; stuck < 3; stuck++) {
			native_initialize(false); qb_initialize(0x54,3);
			qb.vid_stuck[port] = stuck == 0; qb.pv_stuck[port] = stuck == 1; qb.hvs_stuck[port] = stuck == 2;
			native_finish(); CHECK(!gpu_sc.sc_bootfb.display.complete);
			CHECK(gpu_sc.sc_bootfb.display.fault == (stuck == 1 ? EIO : ETIMEDOUT));
			CHECK(gpu_sc.sc_bootfb.display.stop.pipe[port].error == gpu_sc.sc_bootfb.display.fault);
			CHECK(qb.writes == (stuck == 0 ? port+1 : stuck == 1 ? 3+2*port : 8+2*port));
			if (port == 1 && stuck == 2) CHECK(gpu_sc.sc_bootfb.display.stop.pipe[0].complete);
			display_retry(gpu_sc.sc_bootfb.display.fault);
		}
	}
	/* Unknown version, active HVS2, and mismatched PV clocks stop before writes. */
	for (unsigned gate = 0; gate < 5; gate++) {
		native_initialize(false); qb_initialize(0x54,true);
		if (gate == 0) qb.regs[0][0] = 0x55;
		if (gate == 1) qb.regs[2][0] |= 4;
		if (gate == 2) qb.regs[0][(qb.ctrl+0x80)/4] |= 0x80000000U;
		if (gate == 3) qb.regs[0][(qb.stat+0x80)/4] |= 0x2000;
		if (gate == 4) qb.regs[1][0] |= 4;
		native_finish(); CHECK(gpu_sc.sc_bootfb.display.fault == (gate == 0 ? EOPNOTSUPP : EBUSY));
		CHECK(qb.writes == 0 && qb.delays == 0); display_retry(gpu_sc.sc_bootfb.display.fault);
	}
	/* A failed renderer never invokes the continuation or creates a provider. */
	native_initialize(false); renderer_error = EIO; display_permitted = false; native_finish();
	CHECK(gpu_sc.sc_bootfb.renderer == NULL && !gpu_sc.sc_bootfb.display.attempted);
	CHECK(bcmv3d_bootfb_stop_display(&gpu_sc) == EPERM);
	CHECK(display_clock_calls == 0 && display_map_calls == 0 && qb.operations == 0);
	/* Raw CPU close cannot upgrade itself with copied firmware value facts. */
	native_initialize(false); cold = false;
	struct wsdisplay_bootfb_lease *raw;
	CHECK(simplefb_console_claim(&raw,&parsed) == 0 && simplefb_console_close(raw) == 0);
	gpu_sc.sc_bootfb.lease = raw; gpu_sc.sc_bootfb.phase = BCMV3D_BOOTFB_CLOSED;
	gpu_sc.sc_bootfb.firmware.attempted = gpu_sc.sc_bootfb.firmware.complete = true; gpu_sc.sc_bootfb.renderer = fixture_renderer;
	display_permitted = false;
	CHECK(bcmv3d_bootfb_stop_display(&gpu_sc) == ENXIO && display_clock_calls == 0 && display_map_calls == 0);
	native_finish(); CHECK(fallback_calls == 0);
}
static void mmu4_retry(int expected)
{
	struct bcmv3d_bootfb_mmu4 saved = gpu_sc.sc_bootfb.mmu4;
	unsigned maps = writeback_maps, reads = writeback_reads, calls = bootfb_mmu4_calls();
	int first = gpu_sc.sc_bootfb.first_error;

	CHECK(bcmv3d_bootfb_program_mmu4(&gpu_sc) == expected);
	bcmv3d_bootfb_claim_job(&gpu); CHECK(bcmv3d_finalize(&gpu) == 0);
	CHECK(memcmp(&saved,&gpu_sc.sc_bootfb.mmu4,sizeof(saved)) == 0);
	CHECK(writeback_maps == maps && writeback_reads == reads && bootfb_mmu4_calls() == calls);
	CHECK(gpu_sc.sc_bootfb.first_error == first && fallback_calls == 0 && serial_fixture_held());
}
static void tests_mmu4(void)
{
	struct bcmv3d_bootfb_mmu4 *m;
	unsigned i, port;

	native_initialize(false); mmu4_permitted = false;
	CHECK(bcmv3d_bootfb_program_mmu4(NULL) == EINVAL);
	CHECK(bcmv3d_bootfb_program_mmu4(&gpu_sc) == EWOULDBLOCK);
	cold = false; hard_irq = true;
	CHECK(bcmv3d_bootfb_program_mmu4(&gpu_sc) == EWOULDBLOCK); hard_irq = false;
	soft_irq = true; CHECK(bcmv3d_bootfb_program_mmu4(&gpu_sc) == EWOULDBLOCK); soft_irq = false;
	panicstr = "MMU4 context"; CHECK(bcmv3d_bootfb_program_mmu4(&gpu_sc) == EWOULDBLOCK); panicstr = NULL;
	int rs = pserialize_read_enter(); CHECK(bcmv3d_bootfb_program_mmu4(&gpu_sc) == EDEADLK); pserialize_read_exit(rs);
	CHECK(!gpu_sc.sc_bootfb.mmu4.attempted && bootfb_mmu4_calls() == 0 && writeback_maps == 0);
	mmu4_permitted = true; native_finish();
	for (i = 0; i < 2; i++) {
		native_initialize(i); qb_initialize(0x54,3); mmu4_reenter = true; native_finish();
		m = &gpu_sc.sc_bootfb.mmu4;
		CHECK(m->attempted && m->complete && m->fault == 0 && m->native != NULL);
		CHECK(m->status_valid && m->status.backing.prepared && m->status.backing.sealed);
		CHECK(m->status.sequence.sequence_complete && m->status.shared.flush_complete);
		CHECK(writeback_maps == 2 && writeback_reads == 2 && bootfb_mmu4_calls() == 4);
		CHECK(qb.writes == 10 && gpu_sc.sc_bootfb.first_error == gpu_sc.sc_bootfb.publish.fault);
		mmu4_retry(EBUSY);
	}
	for (i = 1; i <= 42; i++) {
		native_initialize(false); mmu4_gate = i; mmu4_permitted = false; native_finish();
		if (i == 40) gpu.private = &gpu_sc;
		m = &gpu_sc.sc_bootfb.mmu4;
		CHECK(!m->complete && !m->attempted && m->fault != 0);
		CHECK(writeback_maps == 0 && writeback_reads == 0 && bootfb_mmu4_calls() == 0);
		if (i != 9) mmu4_retry(m->fault); /* This test deliberately revoked its hold. */
	}
	native_initialize(false); mmu4_tuple_error = ENXIO; mmu4_permitted = false; native_finish();
	CHECK(!gpu_sc.sc_bootfb.mmu4.attempted && gpu_sc.sc_bootfb.mmu4.fault == ENXIO && writeback_maps == 0);
	mmu4_retry(ENXIO);
	for (port = 0; port < 2; port++) {
		for (i = 0; i < 4; i++) {
			native_initialize(false);
			if (i < 2) writeback_control[port] |= 1U << i;
			if (i == 2) writeback_fail_map = port+1;
			if (i == 3) writeback_fail_read = port+1;
			native_finish(); m = &gpu_sc.sc_bootfb.mmu4;
			CHECK(m->attempted && !m->complete && m->native == NULL);
			CHECK(m->fault == (i < 2 ? EBUSY : i == 2 ? ENOMEM : EIO));
			CHECK(bootfb_mmu4_calls() == 0 && writeback_maps == port+1);
			CHECK(m->writeback_mapped == (i == 2 ? port : port+1));
			CHECK(writeback_reads == (i == 2 ? port : port+1));
			CHECK(m->writeback_valid[port] == (i < 2));
			mmu4_retry(m->fault);
		}
	}
	for (i = 1; i <= 23; i++) {
		native_initialize(false); bootfb_mmu4_failure(i); native_finish();
		m = &gpu_sc.sc_bootfb.mmu4;
		CHECK(m->attempted && !m->complete && m->fault != 0);
		CHECK(m->native != NULL || i == 1);
		CHECK(gpu_sc.sc_bootfb.first_error == m->fault);
		CHECK(bootfb_mmu4_calls() == (i == 1 ? 1U : i == 2 || i == 5 || i == 8 ? 3U : 4U));
		if (i == 23) CHECK(m->fault == EIO && m->status_error == EFAULT);
		mmu4_retry(m->fault);
	}
	/* A prior failure is retained even if this stage has its own terminal errno. */
	native_initialize(false); mmu4_gate = 43; bootfb_mmu4_failure(1); native_finish();
	CHECK(gpu_sc.sc_bootfb.first_error == EPROTO); mmu4_retry(ENOMEM);
}
static void hvs_profile_boot(unsigned variant, bool first)
{
	native_initialize(first); qb_initialize(variant,3);
	qb.regs[0][8] = 0x802000f0;
	for (unsigned i = 1; i <= 2; i++) { qb.regs[i][0] = 0x07174001; qb.regs[i][1] = 3; }
}
static void hvs_retry(int error)
{
	struct bcmv3d_bootfb_publish saved = gpu_sc.sc_bootfb.publish;
	unsigned before = hvs_maps+hvs_writeback_reads+hvs_native_queries+pb.accesses;
	int first = gpu_sc.sc_bootfb.first_error;
	CHECK(bcmv3d_bootfb_publish(&gpu_sc) == error);
	bcmv3d_bootfb_claim_job(&gpu); CHECK(bcmv3d_finalize(&gpu) == 0);
	CHECK(memcmp(&saved,&gpu_sc.sc_bootfb.publish,sizeof(saved)) == 0);
	CHECK(hvs_maps+hvs_writeback_reads+hvs_native_queries+pb.accesses == before);
	CHECK(gpu_sc.sc_bootfb.first_error == first && fallback_calls == 0);
}
static void tests_hvs(void)
{
	struct bcmv3d_bootfb_publish *p;
	unsigned reads, writes;
	uint32_t value;

	native_initialize(false); hvs_permitted = false;
	CHECK(bcmv3d_bootfb_publish(NULL) == EINVAL);
	CHECK(bcmv3d_bootfb_publish(&gpu_sc) == EWOULDBLOCK);
	CHECK(bcmv3d_publish_read(&gpu_sc,BCMV3D_PUBLISH_HVS,0,&value) == EPERM);
	CHECK(bcmv3d_publish_write(&gpu_sc,BCMV3D_PUBLISH_HVS,0,0) == EPERM);
	cold = false; hard_irq = true; CHECK(bcmv3d_bootfb_publish(&gpu_sc) == EWOULDBLOCK); hard_irq = false;
	soft_irq = true; CHECK(bcmv3d_bootfb_publish(&gpu_sc) == EWOULDBLOCK); soft_irq = false;
	panicstr = "publish context"; CHECK(bcmv3d_bootfb_publish(&gpu_sc) == EWOULDBLOCK); panicstr = NULL;
	int rs = pserialize_read_enter(); CHECK(bcmv3d_bootfb_publish(&gpu_sc) == EDEADLK); pserialize_read_exit(rs);
	CHECK(!gpu_sc.sc_bootfb.publish.attempted && hvs_native_queries == 0 && hvs_maps == 0);
	hvs_permitted = true; native_finish();
	for (unsigned first = 0; first < 2; first++) {
		for (unsigned packing = 0; packing < 3; packing++) {
			for (unsigned variant = 0x53; variant <= 0x54; variant++) {
				hvs_profile_boot(variant,first);
				if (packing == 1) qb.regs[1][1] = 0x20000003;
				hvs_bypass = packing == 2;
				hvs_reenter = true; native_finish();
				p = &gpu_sc.sc_bootfb.publish;
				CHECK(p->attempted && p->complete && !p->executing && p->fault == 0);
				CHECK(range_check_calls == 1 && gpu_sc.sc_bootfb.range_check.complete);
				CHECK(vblank_calls == 1);
				CHECK(p->status.native_valid && p->status.native.backing.prepared && p->status.native.backing.sealed);
				CHECK(p->status.complete && p->status.active_list == 32 && (p->status.channel_status & 0x6000) == 0x4000);
				CHECK(p->mapped == 0x1ff && hvs_maps == 6 && hvs_writeback_reads == 2 && hvs_native_queries == 1);
				CHECK(p->dlist_first == 32 && p->dlist_words == 10 && p->frame.upm_offset == 0 && p->frame.upm_length == 15616 && p->frame.upm_slot == 0);
				CHECK(p->frame.dma_base == 0 && p->frame.dma_span == 8294400 && p->frame.variant == variant);
				CHECK(p->cxm_valid && p->cxm_words == 1024);
				CHECK(p->cob[0] == 0x87004b10 && p->cob[1] == 0x4b000f10 && p->cob[2] == 0x0f000000);
				CHECK(p->progress_valid[0] && p->progress_valid[1] && p->progress_error == 0);
				CHECK(((p->progress[1] >> 16) & 0x3f) == 1 && p->progress[0] == 0x4000);
				CHECK(pb.writes == 16 && pb.barriers == 16 && memcmp(pb.regs[0]+0x1000,pb.saved[0]+0x1000,128) == 0);
				CHECK(p->status.timing.words == 12);
				CHECK(p->status.timing.pv[0] == (packing != 1 ? 2U : 0x20000002U));
				CHECK(p->status.timing.pv[2] == (packing != 1 ? 0x004a0016U : 0x0094002cU));
				CHECK(p->status.timing.pv[3] == (packing != 1 ? 0x002c03c0U : 0x00580780U));
				snapshot_check();
				CHECK(gpu_sc.sc_bootfb.first_error == 0 && serial_fixture_held());
				reads = pb.reads; writes = pb.writes; hvs_retry(EBUSY);
				CHECK(bcmv3d_publish_read(&gpu_sc,BCMV3D_PUBLISH_HVS,0,&value) == EPERM && pb.reads == reads);
				CHECK(bcmv3d_publish_write(&gpu_sc,BCMV3D_PUBLISH_HVS,0,0) == EPERM && pb.writes == writes);
			}
		}
	}
/* A range failure does not rewrite the accepted fixed HVS result. */
for (unsigned failure = 0; failure < 2; failure++) {
	hvs_profile_boot(0x54, false);
	range_check_error = failure == 0 ? EIO : ETIMEDOUT;
	native_finish();
	CHECK(gpu_sc.sc_bootfb.publish.complete && gpu_sc.sc_bootfb.publish.fault == 0);
	CHECK(gpu_sc.sc_bootfb.first_error == 0 && range_check_calls == 1);
	CHECK(!gpu_sc.sc_bootfb.range_check.complete && gpu_sc.sc_bootfb.range_check.first_error == range_check_error);
	CHECK(vblank_calls == 0);
	hvs_retry(EBUSY);
	CHECK(range_check_calls == 1);
}
for (unsigned variant = 0x53; variant <= 0x54; variant++) {
	for (unsigned mix = 1; mix <= 11; mix++) {
			hvs_profile_boot(variant, false);
			hvs_bypass = true;
			hvs_bypass_mix = mix;
			native_finish();
			p = &gpu_sc.sc_bootfb.publish;
			CHECK(p->attempted && !p->complete && p->fault == EOPNOTSUPP);
			CHECK(range_check_calls == 0 && !gpu_sc.sc_bootfb.range_check.attempted);
			CHECK(!p->status.mutation_attempted && pb.writes == 0);
			CHECK(serial_fixture_held() && p->status.native.backing.sealed);
			hvs_retry(EOPNOTSUPP);
		}
	}
	for (unsigned gate = 1; gate <= 40; gate++) {
		hvs_profile_boot(0x54,false); hvs_gate = gate;
		hvs_permitted = gate == 37 || gate == 38; native_finish(); p = &gpu_sc.sc_bootfb.publish;
		CHECK(!p->complete && p->fault != 0 && !p->executing);
		CHECK(hvs_maps == 0 && pb.writes == 0);
		if (gate == 37 || gate == 38) CHECK(hvs_native_queries == 1 && hvs_writeback_reads == 2 && pb.reads == 1);
		else CHECK(hvs_native_queries == 0 && hvs_writeback_reads == 0 && pb.reads == 0);
		if (gate == 34) CHECK(gpu_sc.sc_bootfb.first_error == EPROTO);
		if (gate == 35) gpu.private = &gpu_sc; /* End the deliberate device fault before job replay. */
		hvs_retry(p->fault);
	}
	for (unsigned fault = 1; fault <= 30; fault++) {
		hvs_profile_boot(0x54,false); hvs_native_fault = fault; native_finish(); p = &gpu_sc.sc_bootfb.publish;
		CHECK(p->attempted && !p->complete && p->fault != 0 && p->status.native_valid);
		CHECK(hvs_native_queries == 1 && hvs_writeback_reads == 0 && hvs_maps == 0 && pb.accesses == 0);
		hvs_retry(p->fault);
	}
	hvs_profile_boot(0x54,false); hvs_native_fault = 100; native_finish(); p = &gpu_sc.sc_bootfb.publish;
	CHECK(p->fault == EAGAIN && !p->status.native_valid && hvs_maps == 0 && hvs_writeback_reads == 0); hvs_retry(EAGAIN);
	for (unsigned port = 0; port < 2; port++) {
		for (unsigned mode = 0; mode < 3; mode++) {
			hvs_profile_boot(0x54,false);
			if (mode < 2) hvs_busy[port] = 1U << mode; else hvs_fail_writeback = port+1;
			native_finish(); p = &gpu_sc.sc_bootfb.publish;
			CHECK(p->fault == (mode < 2 ? EBUSY : EIO) && hvs_writeback_reads == port+1 && pb.accesses == 0 && hvs_maps == 0);
			hvs_retry(p->fault);
		}
	}
	for (unsigned fault = 1; fault <= 6; fault++) {
		hvs_profile_boot(0x54,false); hvs_fail_map = fault; native_finish(); p = &gpu_sc.sc_bootfb.publish;
		CHECK(p->fault == ENOMEM && hvs_maps == fault && pb.writes == 0); hvs_retry(ENOMEM);
	}
	for (unsigned fault = 1; fault <= __arraycount(profile); fault++) {
		hvs_profile_boot(0x54,false); hvs_profile_fault = fault; native_finish(); p = &gpu_sc.sc_bootfb.publish;
		CHECK(p->fault == EOPNOTSUPP && p->status.raw_valid && pb.writes == 0);
		CHECK(p->status.window == profile[fault-1].window && p->status.offset == profile[fault-1].offset);
		hvs_retry(EOPNOTSUPP);
	}
	hvs_profile_boot(0x54,false); native_finish(); reads = pb.reads;
	for (unsigned fault = 1; fault <= reads; fault++) {
		hvs_profile_boot(0x54,false); hvs_read_fault = fault; native_finish(); p = &gpu_sc.sc_bootfb.publish;
		CHECK(p->fault == EIO && pb.reads == fault); hvs_retry(EIO);
	}
	for (unsigned fault = 1; fault <= 16; fault++) {
		for (unsigned partial = 0; partial < 2; partial++) {
			hvs_profile_boot(0x54,false); hvs_write_fault = fault; hvs_partial = partial;
			native_finish(); p = &gpu_sc.sc_bootfb.publish;
			CHECK(p->fault == EIO && pb.writes == fault && p->status.mutation_attempted); hvs_retry(EIO);
		}
	}
	for (unsigned fault = 1; fault <= 6; fault++) {
		hvs_profile_boot(0x54,false); hvs_callback_case = fault; native_finish(); p = &gpu_sc.sc_bootfb.publish;
		CHECK(p->fault == (fault == 6 ? EACCES : EINVAL) && pb.accesses == 0 && hvs_writeback_reads == 0 && hvs_maps == 0); hvs_retry(p->fault);
	}
	for (unsigned capacity = 0; capacity < 3; capacity++) {
		hvs_profile_boot(0x54,false); hvs_cxm = capacity == 0 ? 0 : capacity == 1 ? 41 : 42;
		native_finish(); p = &gpu_sc.sc_bootfb.publish;
		CHECK(p->cxm_valid && p->cxm_words == hvs_cxm);
		if (capacity < 2) { CHECK(p->fault == EOPNOTSUPP && hvs_maps == 0 && pb.writes == 0); hvs_retry(EOPNOTSUPP); }
		else { CHECK(p->complete); hvs_retry(EBUSY); }
	}
	for (unsigned mode = 0; mode < 2; mode++) {
		hvs_profile_boot(0x54,false); hvs_timeout = mode == 0; hvs_eof = mode == 1;
		native_finish(); p = &gpu_sc.sc_bootfb.publish;
		CHECK(p->fault == ETIMEDOUT && p->status.polls == 100 && pb.delays == 100 && pb.writes == 16); hvs_retry(ETIMEDOUT);
	}
	/* A lost readback cannot trigger recovery writes or a second attempt. */
	hvs_profile_boot(0x54,false); hvs_ignore_write = 1; native_finish(); p = &gpu_sc.sc_bootfb.publish;
	CHECK(p->fault == EIO && pb.writes == 10); hvs_retry(EIO);
	hvs_profile_boot(0x54,false); hvs_progress_stuck = true; native_finish();
	CHECK(gpu_sc.sc_bootfb.publish.complete && gpu_sc.sc_bootfb.publish.progress[0] == gpu_sc.sc_bootfb.publish.progress[1]);
	hvs_retry(EBUSY); /* No progress is diagnostic, never a permission or drain predicate. */
	hvs_profile_boot(0x54,false); hvs_query_error = EIO; native_finish(); p = &gpu_sc.sc_bootfb.publish;
	CHECK(p->fault == EIO && p->status.complete && p->progress_error == EIO && !p->progress_valid[1]); hvs_retry(EIO);
}
int main(void)
{
	pthread_mutexattr_t a;
	CHECK(pthread_mutexattr_init(&a) == 0 && pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE) == 0);
	CHECK(pthread_mutex_init(&giant_mutex, &a) == 0 && pthread_mutexattr_destroy(&a) == 0);
	tests_native(); tests_firmware(); tests_display(); tests_mmu4(); tests_hvs();
	CHECK(giant == 0 && read_depth == 0 && creates == destroys && init_started);
	(void)bootfb_mmu4_reboot();
	display_initialize();
	CHECK(pthread_mutex_destroy(&giant_mutex) == 0);
	printf("native serial boot handoff: %u actual-source checks PASS\n", atomic_load(&checks));
	return 0;
}
