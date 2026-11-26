/* SPDX-License-Identifier: BSD-2-Clause */
/* Test simultaneous bulk transfers, command completion and teardown.
 */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned u_int;
typedef int device_t, cfdata_t;

typedef enum {
	USBD_NORMAL_COMPLETION,
	USBD_INVAL,
	USBD_SHORT_XFER,
	USBD_TIMEOUT,
	USBD_NOMEM,
	USBD_IOERROR,
	USBD_IN_PROGRESS,
	USBD_STALLED,
	USBD_CANCELLED
} usbd_status;

typedef struct {
	uint8_t bAlternateSetting, bNumEndpoints;
} usb_interface_descriptor_t;

typedef struct {
	uint8_t bEndpointAddress, bmAttributes;
	uint16_t wMaxPacketSize;
} usb_endpoint_descriptor_t;

struct usbd_interface {
	usb_interface_descriptor_t desc;
	usb_endpoint_descriptor_t eps[4];
};

typedef struct { unsigned bNumInterface; } usb_config_descriptor_t;
static usb_config_descriptor_t config = { 3 };
struct usbif_attach_arg {
	unsigned uiaa_vendor, uiaa_product, uiaa_configno;
	unsigned uiaa_class, uiaa_subclass, uiaa_proto;
	struct usbd_interface *uiaa_iface;
	void *uiaa_device;
};

struct usbd_pipe {
	unsigned addr;
};

struct usbd_device;
struct usb_task { void (*fn)(void *); void *arg; bool pending; };
#define USB_TASKQ_MPSAFE 1
#define USB_TASKQ_DRIVER 1
static unsigned clears;
static bool clear_error;
static void usb_init_task(struct usb_task *t, void (*fn)(void *), void *arg, int flags)
{ t->fn = fn; t->arg = arg; t->pending = false; }
static void usb_add_task(struct usbd_device *d, struct usb_task *t, int q)
{ t->pending = true; }
static void usb_rem_task_wait(struct usbd_device *d, struct usb_task *t, int q, void *l)
{ t->pending = false; }
static usbd_status usbd_clear_endpoint_stall(struct usbd_pipe *p)
{ assert(p); clears++; return clear_error ? USBD_IOERROR : USBD_NORMAL_COMPLETION; }

struct usbd_xfer;
typedef void (*usbd_callback)(struct usbd_xfer *, void *, usbd_status);
struct usbd_xfer {
	usbd_callback cb;
	void *arg;
	bool pending;
	uint8_t buf[2048];
	struct usbd_pipe *pipe;
	uint32_t len, actual;
};

#include "usbdevs.h"
#define UICLASS_VENDOR 0xff
#define UMATCH_VENDOR_PRODUCT_CONF_IFACE 8
#define UMATCH_NONE 0
#define UE_GET_XFERTYPE(x) ((x) & 3)
#define UE_GET_DIR(x) ((x) & 0x80)
#define UE_DIR_IN 0x80
#define UE_GET_ADDR(x) ((x) & 15)
#define UE_BULK 2
#define UGETW(x) (x)
#define USBD_EXCLUSIVE_USE 1
#define USBD_SHORT_XFER_OK 2
#define USBD_FORCE_SHORT_XFER 4
#define USBD_NO_TIMEOUT 0
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#define MUTEX_DEFAULT 0
#define IPL_NONE 0
#define IPL_SOFTUSB 1
#define mstohz(n) (n)
typedef int kmutex_t, kcondvar_t;
static void mutex_init(kmutex_t *m, int kind, int ipl) { assert(!*m); }
static void mutex_enter(kmutex_t *m) { assert(!*m); *m = 1; }
static void mutex_exit(kmutex_t *m) { assert(*m); *m = 0; }
static bool mutex_owned(const kmutex_t *m) { return *m; }
static void mutex_destroy(kmutex_t *m) { assert(!*m); }
static void cv_init(kcondvar_t *cv, const char *name) { assert(!*cv); }
static void cv_destroy(kcondvar_t *cv) { assert(!*cv); }
static void cv_broadcast(kcondvar_t *cv) { (void)cv; }
static int cv_timedwait(kcondvar_t *, kmutex_t *, int);
static void *device_private(device_t);
#define explicit_memset memset
#define KASSERT assert
#define roundup(x, y) (((x) + (y) - 1) / (y) * (y))
#define CFATTACH_DECL_NEW(...)
#define aprint_naive(...) ((void)0)
#define aprint_normal(...) ((void)0)
#define aprint_error_dev(dev, ...) (errors++)
#define aprint_normal_dev(dev, fmt, first, second) \
	report((fmt), (first), (second))
#define aprint_verbose_dev(dev, ...) (unavailable++)

static unsigned identified, versions, unavailable;

static void
report(const char *fmt, uint32_t first, uint32_t second)
{
	if (strncmp(fmt, "FullMAC", 7) == 0) {
		assert(first == 0x06040301 && second == 0xb011fa5f);
		versions++;
	} else {
		assert(first == 7 && second == 1);
		identified++;
	}
}

static uint16_t
le16dec(const void *p)
{
	const uint8_t *b = p;
	return b[0] | ((uint16_t)b[1] << 8);
}


static uint32_t
le32dec(const void *p)
{
	const uint8_t *b = p;
	return le16dec(b) | ((uint32_t)le16dec(b + 2) << 16);
}

static void
le16enc(void *p, uint16_t v)
{
	uint8_t *b = p;
	b[0] = v;
	b[1] = v >> 8;
}

static void
le32enc(void *p, uint32_t v)
{
	uint8_t *b = p;
	le16enc(b, v);
	le16enc(b + 2, v >> 16);
}

#include "aicwf_command.h"
#include "uaicfwvar.h"
#include "aicwf_sta.h"

enum fault {
	NONE,
	OPEN_RX,
	OPEN_TX,
	ALLOC_RX,
	ALLOC_TX,
	TX_ERROR,
	SHORT_TX,
	TIMEOUT,
	EMPTY_RX,
	SHORT_BODY,
	BAD_ECHO,
	BAD_PARAM_LEN,
	OTHER,
	VERSION_TIMEOUT,
	VERSION_SHORT,
	SHORT_DATA,
	SHORT_TAIL,
	DUPLICATE,
	OPEN_DATA,
	ALLOC_DATA,
	SUBMIT_DATA,
	SUBMIT_CMD,
	ON_DATA,
	EARLY_REPLY,
	FW_ERROR,
	POST_ERROR,
	WRONG_FAMILY
};
static enum fault fault;
static unsigned opens, allocs, writes, reads, errors;
static uint32_t requested;
static unsigned children;
static struct uaicfw_attach_args radio;
static void (*mountroot_hook)(device_t);
static usb_config_descriptor_t *usbd_get_config_descriptor(void *device)
{ return &config; }
static void config_mountroot(device_t dev, void (*fn)(device_t))
{ assert(!mountroot_hook); mountroot_hook = fn; }
static int config_mountroot_cancel(device_t dev)
{ mountroot_hook = NULL; return 0; }
static int config_detach_children(device_t dev, int flags) { return 0; }
#define CFARGS(...) 0
static device_t config_found(device_t dev, struct uaicfw_attach_args *a, void *pr, int cf)
{ children++; radio = *a; return 1; }
static struct usbd_xfer *inputs[2];

static usb_interface_descriptor_t *
usbd_get_interface_descriptor(struct usbd_interface *i)
{
	return &i->desc;
}

static usb_endpoint_descriptor_t *
usbd_interface2endpoint_descriptor(struct usbd_interface *i, unsigned n)
{
	assert(n < 4);
	return &i->eps[n];
}

static usbd_status
usbd_open_pipe(
    struct usbd_interface *i, unsigned addr, int flags, struct usbd_pipe **out)
{
	assert(flags == USBD_EXCLUSIVE_USE && (addr == 0x81 || addr == 0x82 || addr == 2 || addr == 1));
	if ((addr == 0x81 && fault == OPEN_DATA) ||
	    (addr == 0x82 && fault == OPEN_RX) ||
	    (addr == 2 && fault == OPEN_TX))
		return USBD_INVAL;
	*out = calloc(1, sizeof(**out));
	assert(*out);
	(*out)->addr = addr;
	opens++;
	return USBD_NORMAL_COMPLETION;
}

static void
usbd_close_pipe(struct usbd_pipe *p)
{
	assert(opens);
	opens--;
	free(p);
}

static int
usbd_create_xfer(struct usbd_pipe *p, size_t n, int flags, int frames,
    struct usbd_xfer **out)
{
	assert(flags == (p->addr < 0x80 ? USBD_FORCE_SHORT_XFER : USBD_SHORT_XFER_OK));
	if ((p->addr == 0x81 && fault == ALLOC_DATA) ||
	    (p->addr == 2 && fault == ALLOC_TX) ||
	    (p->addr == 0x82 && fault == ALLOC_RX))
		return ENOMEM;
	*out = calloc(1, sizeof(**out));
	assert(*out && n <= sizeof((*out)->buf));
	(*out)->pipe = p;
	allocs++;
	return 0;
}

static void
usbd_destroy_xfer(struct usbd_xfer *x)
{
	assert(allocs && !x->pending);
	if (x->pipe->addr >= 0x81)
		inputs[x->pipe->addr - 0x81] = NULL;
	allocs--;
	free(x);
}

static void *
usbd_get_buffer(struct usbd_xfer *x)
{
	return x->buf;
}

static void
usbd_setup_xfer(struct usbd_xfer *x, void *p, void *b, uint32_t size,
    unsigned flags, unsigned timeout, usbd_callback cb)
{
	assert(!x->pending && b == x->buf && size <= sizeof(x->buf));
	assert(cb ? (flags == USBD_SHORT_XFER_OK && timeout == 0) :
	    (flags == USBD_FORCE_SHORT_XFER && timeout == 500));
	x->len = size;
	x->arg = p;
	x->cb = cb;
}

static usbd_status
usbd_transfer(struct usbd_xfer *x)
{
	assert(!x->pending && x->cb && x->pipe->addr >= 0x81);
	if ((x->pipe->addr == 0x81 && fault == SUBMIT_DATA) ||
	    (x->pipe->addr == 0x82 && fault == SUBMIT_CMD) ||
	    (reads && fault == POST_ERROR))
		return USBD_IOERROR;
	inputs[x->pipe->addr - 0x81] = x;
	x->pending = true;
	return USBD_IN_PROGRESS;
}

static void
usbd_suspend_pipe(struct usbd_pipe *p)
{
	if (p->addr < 0x81)
		return;
	struct usbd_xfer *x = inputs[p->addr - 0x81];
	if (x && x->pending) {
		x->pending = false;
		x->cb(x, x->arg, USBD_CANCELLED);
	}
}

static void deliver(void);
static usbd_status
emulate_transfer(struct usbd_xfer *x)
{
	uint8_t *b = x->buf;
	if (x->pipe->addr == 2) {
		static const uint8_t hdr[16] = {
		    16, 0, 0x11, 0, 0, 0, 0, 0, 0, 4, 1, 0, 100, 0, 4, 0};
		static const uint8_t version_hdr[16] = {
		    12, 0, 0x11, 0, 0, 0, 0, 0, 4, 0, 0, 0, 100, 0, 0, 0 };
		if (writes < 2) {
			assert(x->len == 20 && memcmp(b, hdr, sizeof(hdr)) == 0);
			requested = le32dec(b + 16);
			assert(requested == (writes == 0 ? 0x40500000 : 0x20));
		} else if (writes == 2) {
			assert(x->len == sizeof(version_hdr));
			assert(memcmp(b, version_hdr, sizeof(version_hdr)) == 0);
		} else {
			assert(writes == 3 && x->len == 20 && le16dec(b + 8) == AICWF_MM_GET_MAC_ADDR_REQ);
			assert(b[16] == 1 && !b[17] && !b[18] && !b[19]);
		}
		assert(inputs[0] && inputs[0]->pending && inputs[1] && inputs[1]->pending);
		writes++;
		if (fault == EARLY_REPLY && writes <= 3)
			deliver();
		if (fault == TX_ERROR)
			return USBD_INVAL;
		x->actual = fault == SHORT_TX ? 19 : x->len;
	} else {
		assert(writes && x->len == 2048);
		reads++;
		if (fault == TIMEOUT || (writes == 3 && fault == VERSION_TIMEOUT))
			return USBD_TIMEOUT;
		memset(b, 0, x->len);
		/* Data can arrive on the command endpoint. The last bytes
		 * of its descriptor and frame must not be read as messages. */
		le16enc(b, fault == SHORT_DATA ? 2048 : 3);
		b[2] = 0;
		memset(b + 4, 0xbd, 59);
		b += 64;
		/* Put an unrelated indication before the confirmation. */
		le16enc(b, 12);
		b[2] = 0x11;
		le16enc(b + 4, 0x1004);
		b += 16;

		if (writes == 3) {
			le16enc(b, 12 + 28);
			b[2] = 0x11;
			le16enc(b + 4, 5);
			le16enc(b + 10, fault == VERSION_SHORT ? 27 : 28);
			le32enc(b + 16, 0x06040301);
			le32enc(b + 36, 0xb011fa5f);
			x->actual = 64 + 60;
			return USBD_NORMAL_COMPLETION;
		}
		le16enc(b, fault == SHORT_BODY ? 200 : 20);
		b[2] = 0x11;
		le16enc(b + 4, fault == OTHER ? 6 : 0x401);
		le16enc(b + 10, fault == BAD_PARAM_LEN ? 7 : 8);
		le32enc(b + 16, fault == BAD_ECHO ? 0xdead : requested);
		le32enc(b + 20, requested == 0x20 ? 1 : 0x00070000);
		x->actual = fault == EMPTY_RX ? 0 : 64 + 40;
		if (fault == SHORT_TAIL || fault == DUPLICATE) {
			memcpy(b + 24, b, 24);
			x->actual += fault == DUPLICATE ? 24 : 23;
		}
	}
	return USBD_NORMAL_COMPLETION;
}

static usbd_status
usbd_sync_transfer(struct usbd_xfer *x)
{
	assert(x->pipe->addr < 0x80 && x->cb == NULL);
	return emulate_transfer(x);
}

static void
deliver(void)
{
	struct usbd_xfer *x = inputs[fault != ON_DATA];
	assert(x != NULL && x->pending);
	x->pending = false;
	usbd_status st = emulate_transfer(x);
	if (fault == FW_ERROR)
		le16enc(x->buf + 68, 0x0408);
	x->cb(x, x->arg, st);
}

static int
cv_timedwait(kcondvar_t *cv, kmutex_t *m, int ticks)
{
	assert(ticks == 500 && *m);
	mutex_exit(m);
	deliver();
	mutex_enter(m);
	return fault == OTHER || fault == EMPTY_RX ? EWOULDBLOCK : 0;
}

static void
usbd_get_xfer_status(
    struct usbd_xfer *x, void *p, void *b, uint32_t *length, void *s)
{
	*length = x->actual;
}

#include "probe.h"

static struct uaicfw_softc device;
static void *device_private(device_t self) { return &device; }

static void
ignore_input(void *cookie, const uint8_t *p, size_t len)
{
}

static void
stall_input(struct usbd_xfer *x)
{
	assert(x->pending);
	x->pending = false;
	x->cb(x, x->arg, USBD_STALLED);
}

static void
run_usb_task(struct usb_task *t)
{
	assert(t->pending);
	t->pending = false;
	t->fn(t->arg);
}

int
main(void)
{
	struct usbd_interface iface = {
	    {0, 4}, {{1, 2, 512}, {0x81, 2, 512}, {2, 2, 512}, {0x82, 2, 512}}};
	struct usbif_attach_arg a = {
	    0xa69c, 0x88dc, 1, 0xff, 0xff, 0xff, &iface, NULL};
	assert(uaicfw_match(0, 0, &a) == UMATCH_VENDOR_PRODUCT_CONF_IFACE);
	iface.desc.bAlternateSetting = 1;
	assert(uaicfw_match(0, 0, &a) == UMATCH_NONE);
	iface.desc.bAlternateSetting = 0;
	/* The two Bluetooth interfaces use the wireless-controller class. */
	a.uiaa_class = 0xe0;
	a.uiaa_subclass = a.uiaa_proto = 1;
	assert(uaicfw_match(0, 0, &a) == UMATCH_NONE);
	a.uiaa_class = a.uiaa_subclass = a.uiaa_proto = 0xff;
	a.uiaa_product++;
	assert(uaicfw_match(0, 0, &a) == UMATCH_NONE);
	a.uiaa_product--;
	for (fault = NONE; fault <= WRONG_FAMILY; fault++) {
		opens = allocs = writes = reads = errors = identified = 0;
		versions = unavailable = children = 0;
		config.bNumInterface = fault == WRONG_FAMILY ? 1 : 3;
		memset(&device, 0, sizeof(device));
		uaicfw_attach(0, 0, &a);
		bool have_id = fault == NONE || fault == VERSION_TIMEOUT ||
		    fault == VERSION_SHORT || fault == ON_DATA || fault == EARLY_REPLY || fault >= WRONG_FAMILY;
		assert(identified == have_id && errors == !have_id);
		bool running = fault == NONE || fault == ON_DATA || fault == EARLY_REPLY || fault >= WRONG_FAMILY;
		assert(versions == running);
		assert(unavailable == (have_id && !running));
		assert(!have_id || (writes == 3 && reads == 3));
		if (fault == OTHER || fault == EMPTY_RX)
			assert(reads == 1 && writes == 1);
		assert((mountroot_hook != NULL) == (running && fault != WRONG_FAMILY));
		if (mountroot_hook != NULL) {
			mountroot_hook(0);
			assert(children == 1);
			uint8_t packet[512], req[AICWF_STA_SCAN_LEN], mac[4] = { 1 };
			bool attempted = true;
			size_t n = aicwf_command_encode(packet, sizeof(packet), 0x1800, mac, 4);
			assert(radio.write(radio.cookie, packet, n, &attempted) == EOPNOTSUPP);
			assert(!attempted && writes == 3);
			packet[2] = 1;
			assert(radio.write(radio.cookie, packet, n, &attempted) == EOPNOTSUPP);
			assert(!attempted && writes == 3);
			n = aicwf_command_encode(packet, sizeof(packet), AICWF_MM_GET_MAC_ADDR_REQ, mac, 4);
			assert(radio.write(radio.cookie, packet, n, &attempted) == 0 && attempted);
			uint16_t freqs[] = { 2412, 2437, 2462 };
			assert(aicwf_sta_scan_build(req, freqs, 3, 2, 0));
			n = aicwf_command_encode(packet, sizeof(packet), AICWF_SCANU_START_REQ, req, sizeof(req));
			assert(uaicfw_survey_command(packet, n));
			packet[16 + 3] = 0; /* Active scan. */
			assert(!uaicfw_survey_command(packet, n));
			packet[16 + 3] = 1; packet[16 + 4] = 12;
			assert(!uaicfw_survey_command(packet, n));
			packet[16 + 4] = 0; packet[16 + 368] = 1;
			assert(!uaicfw_survey_command(packet, n));
			packet[16 + 368] = 0; le16enc(packet + 16 + 12, 2467);
			assert(!uaicfw_survey_command(packet, n));
		}
		if (children && fault == NONE) {
			radio.listen(radio.cookie, ignore_input, NULL);
			stall_input(inputs[0]);
			stall_input(inputs[1]);
			run_usb_task(&device.sc_recover_task);
			assert(clears == 2 && inputs[0]->pending && inputs[1]->pending);
			assert(device.sc_error == USBD_NORMAL_COMPLETION);
			inputs[0]->pending = false;
			inputs[0]->actual = 0;
			inputs[0]->cb(inputs[0], inputs[0]->arg, USBD_NORMAL_COMPLETION);
			stall_input(inputs[0]);
			run_usb_task(&device.sc_recover_task);
			assert(clears == 3 && inputs[0]->pending);
			stall_input(inputs[1]);
			assert(!device.sc_recover_task.pending && device.sc_error == USBD_STALLED);
		} else if (children && fault == EARLY_REPLY) {
			radio.listen(radio.cookie, ignore_input, NULL);
			stall_input(inputs[1]);
			clear_error = true;
			run_usb_task(&device.sc_recover_task);
			clear_error = false;
			assert(device.sc_error == USBD_IOERROR && !inputs[1]->pending);
		} else if (children && fault == ON_DATA) {
			radio.listen(radio.cookie, ignore_input, NULL);
			stall_input(inputs[1]);
			assert(device.sc_recover_task.pending);
		}
		assert(uaicfw_detach(0, 0) == 0 && !opens && !allocs);
	}
	writes = errors = 0;
	iface.eps[2].bEndpointAddress = 1;
	assert(uaicfw_match(0, 0, &a) == UMATCH_NONE);
	memset(&device, 0, sizeof(device));
	uaicfw_attach(0, 0, &a);
	assert(uaicfw_detach(0, 0) == 0);
	assert(!writes && errors == 1);
	puts("PASS: both bulk-IN endpoints, early and late FullMAC replies, "
	     "bounded endpoint recovery, passive command admission and cleanup");
	return 0;
}
