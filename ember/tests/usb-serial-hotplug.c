/* SPDX-License-Identifier: BSD-2-Clause */
/* Host contracts for hub notification delivery and USB serial attachment. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef __packed
#define __packed __attribute__((__packed__))
#endif
#ifndef __CTASSERT
#define __CTASSERT(e) _Static_assert(e, #e)
#endif
#include "usb.h"

#define UHUBHIST_FUNC() ((void)0)
#define UHUBHIST_CALLARGS(...) ((void)0)
#define SDT_PROBE2(...) ((void)0)
#define DPRINTF(...) ((void)0)

typedef enum {
	USBD_NORMAL_COMPLETION, USBD_STALLED, USBD_IN_PROGRESS, USBD_IOERROR
} usbd_status;
struct usbd_xfer;
typedef void (*usbd_callback)(struct usbd_xfer *, void *, usbd_status);
struct usbd_xfer {
	struct usbd_pipe *ux_pipe;
	uint32_t length;
	unsigned releases;
	void *priv;
	usbd_callback callback;
};
struct usbd_pipe {
	struct usbd_xfer notification;
	struct usbd_xfer *up_intrxfer;
	int up_repeat;
	unsigned closes;
	unsigned stall_clears;
};
struct uhub_softc {
	bool sc_lock, sc_rescan;
	uint8_t sc_status[2], sc_statuspend[2], sc_statusbuf[2];
	size_t sc_statuslen;
	struct uhub_softc *sc_hub;
	struct usbd_pipe *sc_ipipe;
	unsigned wakeups;
};

static void mutex_enter(bool *lock) { assert(!*lock); *lock = true; }
static void mutex_exit(bool *lock) { assert(*lock); *lock = false; }
static void usbd_clear_endpoint_stall_async(struct usbd_pipe *pipe)
{
	assert(pipe != NULL);
	pipe->stall_clears++;
}
static void usb_needs_explore(struct uhub_softc *sc)
{
	assert(!sc->sc_lock);
	sc->wakeups++;
}
static void usbd_get_xfer_status(struct usbd_xfer *xfer, void **priv,
    void **buf, uint32_t *count, usbd_status *status)
{
	*count = xfer->length;
}
#include "uhub_begin_explore.inc"
#include "uhub_intr.inc"

static void
hub_changes(void)
{
	struct uhub_softc sc = { .sc_statuslen = 2, .sc_rescan = true };
	struct usbd_xfer xfer = { .length = 2 };
	bool rescan;

	sc.sc_hub = &sc;
	memset(sc.sc_statuspend, 0xff, sizeof(sc.sc_statuspend));
	rescan = uhub_begin_explore(&sc);
	assert(rescan);
	/* Two reports arrive while the event thread is examining the hub. */
	sc.sc_statusbuf[0] = 0x02;
	sc.sc_statusbuf[1] = 0x04;
	uhub_intr(&xfer, &sc, USBD_NORMAL_COMPLETION);
	xfer.length = 1;
	sc.sc_statusbuf[0] = 0x10;
	sc.sc_statusbuf[1] = 0x80; /* outside the new report */
	uhub_intr(&xfer, &sc, USBD_NORMAL_COMPLETION);
	assert(sc.sc_status[0] == 0xff && sc.sc_status[1] == 0xff);
	assert(sc.wakeups != 0);
	rescan = uhub_begin_explore(&sc);
	assert(!rescan);
	assert(sc.sc_status[0] == 0x12 && sc.sc_status[1] == 0x04);
	rescan = uhub_begin_explore(&sc);
	assert(!rescan);
	assert(sc.sc_status[0] == 0 && sc.sc_status[1] == 0);
	puts("PASS hub delivers both port changes and respects report length");
}

typedef void *device_t;
typedef void *cfdata_t;
enum { UMATCH_NONE, UMATCH_IFACECLASS_IFACESUBCLASS_IFACEPROTO };
struct usbif_attach_arg {
	int uiaa_class, uiaa_subclass, uiaa_proto;
	usb_interface_descriptor_t *uiaa_iface;
	int *uiaa_device;
};
static usb_interface_descriptor_t *
usbd_get_interface_descriptor(usb_interface_descriptor_t *iface)
{
	return iface;
}
static int
umodem_get_caps(int *data_iface, int *cm, int *acm,
    usb_interface_descriptor_t *id)
{
	return *data_iface;
}
#include "umodem_match.inc"

#define UMSR_DCD 0x80
struct umodem_softc { u_char sc_lsr, sc_msr; int sc_ctl_notify; };
#include "umodem_get_status.inc"

static void
modem(void)
{
	int data_iface = 1;
	struct usbif_attach_arg iface = {
		.uiaa_class = UICLASS_CDC,
		.uiaa_subclass = UISUBCLASS_ABSTRACT_CONTROL_MODEL,
		.uiaa_device = &data_iface
	};
	struct umodem_softc sc = { .sc_ctl_notify = -1 };
	u_char lsr, msr;

	for (int proto = 0; proto <= 255; proto++) {
		iface.uiaa_proto = proto;
		assert((umodem_match(NULL, NULL, &iface) != UMATCH_NONE) ==
		    (proto <= 6));
	}
	iface.uiaa_proto = UIPROTO_CDC_AT_3GPP;
	data_iface = -1;
	assert(umodem_match(NULL, NULL, &iface) == UMATCH_NONE);
	data_iface = 1;
	iface.uiaa_subclass = UISUBCLASS_ETHERNET_NETWORKING_CONTROL_MODEL;
	assert(umodem_match(NULL, NULL, &iface) == UMATCH_NONE);

	umodem_get_status(&sc, 0, &lsr, &msr);
	assert(msr == UMSR_DCD);
	sc.sc_ctl_notify = 0x81;
	umodem_get_status(&sc, 0, &lsr, &msr);
	assert(msr == 0);
	sc.sc_msr = UMSR_DCD;
	umodem_get_status(&sc, 0, &lsr, &msr);
	assert(msr == UMSR_DCD);
	puts("PASS ACM protocol matching and carrier status");
}

#ifndef __arraycount
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#endif
struct ucom_buffer { struct usbd_xfer *ub_xfer; unsigned char *ub_data; };
struct ucom_softc {
	struct ucom_buffer sc_ibuff[2], sc_obuff[2];
	struct usbd_pipe *sc_bulkin_pipe, *sc_bulkout_pipe;
};
static void usbd_destroy_xfer(struct usbd_xfer *xfer) { xfer->releases++; }
static void usbd_close_pipe(struct usbd_pipe *pipe) { pipe->closes++; }
#include "ucom_free_xfers.inc"
#include "ucom_close_pipes.inc"

static void
bulk_lifetime(void)
{
	struct usbd_pipe in = {0}, out = {0};
	struct usbd_xfer xfers[3] = {0};
	struct ucom_softc sc = {
		.sc_bulkin_pipe = &in, .sc_bulkout_pipe = &out,
		.sc_ibuff = {{&xfers[0], NULL}, {&xfers[1], NULL}},
		.sc_obuff = {{&xfers[2], NULL}}
	};

	ucom_close_pipes(&sc);
	ucom_close_pipes(&sc);
	assert(in.closes == 1 && out.closes == 1);
	for (size_t i = 0; i < __arraycount(xfers); i++)
		assert(xfers[i].releases == 1);
	puts("PASS one release per acquired bulk transfer and pipe");
}

/* A notification pipe becomes visible to its caller only after submission. */
#define USBHIST_FUNC() ((void)0)
#define USBHIST_CALLARGS(...) ((void)0)
#define SDT_PROBE7(...) ((void)0)
#define USBD_EXCLUSIVE_USE 1
#define USBD_MPSAFE 2
#define USBD_NO_TIMEOUT 0
struct usbd_interface { struct usbd_pipe pipe; };
static usbd_status submit_status;

static usbd_status
usbd_open_pipe_ival(struct usbd_interface *iface, uint8_t addr, uint8_t flags,
    struct usbd_pipe **pipe, int interval)
{
	*pipe = &iface->pipe;
	return USBD_NORMAL_COMPLETION;
}

static int
usbd_create_xfer(struct usbd_pipe *pipe, size_t len, unsigned flags,
    unsigned frames, struct usbd_xfer **out)
{
	pipe->notification.ux_pipe = pipe;
	*out = &pipe->notification;
	return 0;
}

static void
usbd_setup_xfer(struct usbd_xfer *xfer, void *priv, void *buf, uint32_t len,
    uint16_t flags, uint32_t timeout, usbd_callback cb)
{
	xfer->length = len;
	xfer->priv = priv;
	xfer->callback = cb;
}

static usbd_status
usbd_transfer(struct usbd_xfer *xfer)
{
	if (submit_status == USBD_IN_PROGRESS)
		xfer->callback(xfer, xfer->priv, USBD_STALLED);
	return submit_status;
}
#include "usbd_open_pipe_intr.inc"

static void
notification_pipe(void)
{
	struct usbd_interface iface = {0};
	struct uhub_softc sc = {0};
	uint8_t buffer[8];
	usbd_status err;

	submit_status = USBD_IOERROR;
	err = usbd_open_pipe_intr(&iface, 0x81, 0, &sc.sc_ipipe, &sc, buffer,
	    sizeof(buffer), uhub_intr, 1);
	assert(err == USBD_IOERROR);
	assert(sc.sc_ipipe == NULL && iface.pipe.closes == 1);
	assert(iface.pipe.notification.releases == 1 && iface.pipe.stall_clears == 0);

	submit_status = USBD_IN_PROGRESS;
	err = usbd_open_pipe_intr(&iface, 0x81, 0, &sc.sc_ipipe, &sc, buffer,
	    sizeof(buffer), uhub_intr, 1);
	assert(err == USBD_NORMAL_COMPLETION);
	assert(sc.sc_ipipe == &iface.pipe);
	assert(iface.pipe.up_intrxfer == &iface.pipe.notification);
	assert(iface.pipe.stall_clears == 1);
	puts("PASS early interrupt uses its own pipe and failed open retains no pipe");
}

int
main(void)
{
	hub_changes();
	modem();
	bulk_lifetime();
	notification_pipe();
	return 0;
}
