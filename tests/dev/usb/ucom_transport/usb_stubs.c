/* Origin: EmberBSD test-only USB stubs for external ucom, 2026-10-10. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <sys/param.h>
#include <sys/systm.h>
#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include "umock_test.h"

static unsigned calls;
unsigned rump_umock_usb_calls(void) { return calls; }

/* A single deliberate NULL-backend attach must still take the legacy path. */
usbd_status
usbd_open_pipe(struct usbd_interface *i, uint8_t a, uint8_t f, struct usbd_pipe **p)
{

	(void)i; (void)a; (void)f; (void)p;
	calls++;
	return USBD_IOERROR;
}

#define FAIL() panic("unexpected legacy USB operation in ucom fixture")
void usbd_close_pipe(struct usbd_pipe *p) { (void)p; FAIL(); }
void usbd_abort_pipe(struct usbd_pipe *p) { (void)p; FAIL(); }
void usbd_destroy_xfer(struct usbd_xfer *x) { (void)x; FAIL(); }
void *usbd_get_buffer(struct usbd_xfer *x) { (void)x; FAIL(); return NULL; }
usbd_status usbd_transfer(struct usbd_xfer *x) { (void)x; FAIL(); return USBD_IOERROR; }
int
usbd_create_xfer(struct usbd_pipe *p, size_t s, unsigned a, unsigned b,
    struct usbd_xfer **x)
{

	(void)p; (void)s; (void)a; (void)b; (void)x; FAIL(); return EIO;
}
void
usbd_setup_xfer(struct usbd_xfer *x, void *p, void *b, uint32_t l, uint16_t f,
    uint32_t t, usbd_callback c)
{

	(void)x; (void)p; (void)b; (void)l; (void)f; (void)t; (void)c; FAIL();
}
void
usbd_get_xfer_status(struct usbd_xfer *x, void **p, void **b, uint32_t *l,
    usbd_status *s)
{

	(void)x; (void)p; (void)b; (void)l; (void)s; FAIL();
}
void usbd_clear_endpoint_stall_async(struct usbd_pipe *p) { (void)p; FAIL(); }
const char *usbd_errstr(usbd_status s) { (void)s; return "mock USB refusal"; }
