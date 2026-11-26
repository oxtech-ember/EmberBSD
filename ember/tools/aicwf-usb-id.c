/* SPDX-License-Identifier: BSD-2-Clause */
/* Test the actual synchronous probe with USB transfer and descriptor doubles.
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
	USBD_IOERROR
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

struct usbif_attach_arg {
	unsigned uiaa_vendor, uiaa_product, uiaa_configno;
	unsigned uiaa_class, uiaa_subclass, uiaa_proto;
	struct usbd_interface *uiaa_iface;
};

struct usbd_pipe {
	unsigned addr;
};

struct usbd_xfer {
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
		assert(first == 0x06010203 && second == 0x12345678);
		versions++;
	} else {
		assert(first == 0xc3 && second == 2);
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
	VERSION_SHORT
};
static enum fault fault;
static unsigned opens, allocs, writes, reads, errors;
static uint32_t requested;

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
	assert(flags == USBD_EXCLUSIVE_USE && (addr == 0x82 || addr == 2));
	if ((addr == 0x82 && fault == OPEN_RX) ||
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
	if ((p->addr == 2 && fault == ALLOC_TX) ||
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
	assert(allocs);
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
    unsigned flags, unsigned timeout, void *cb)
{
	assert(b == x->buf && size <= sizeof(x->buf) && timeout == 500);
	x->len = size;
}

static usbd_status
usbd_sync_transfer(struct usbd_xfer *x)
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
		} else {
			assert(writes == 2 && x->len == sizeof(version_hdr));
			assert(memcmp(b, version_hdr, sizeof(version_hdr)) == 0);
		}
		writes++;
		if (fault == TX_ERROR)
			return USBD_INVAL;
		x->actual = fault == SHORT_TX ? 19 : x->len;
	} else {
		assert(writes && x->len == 2048);
		reads++;
		if (fault == TIMEOUT || (writes == 3 && fault == VERSION_TIMEOUT))
			return USBD_TIMEOUT;
		memset(b, 0, x->len);
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
			le32enc(b + 16, 0x06010203);
			le32enc(b + 36, 0x12345678);
			x->actual = 60;
			return USBD_NORMAL_COMPLETION;
		}
		le16enc(b, fault == SHORT_BODY ? 200 : 20);
		b[2] = 0x11;
		le16enc(b + 4, fault == OTHER ? 6 : 0x401);
		le16enc(b + 10, fault == BAD_PARAM_LEN ? 7 : 8);
		le32enc(b + 16, fault == BAD_ECHO ? 0xdead : requested);
		le32enc(b + 20, requested == 0x20 ? 2 : 0x00c30000);
		x->actual = fault == EMPTY_RX ? 0 : 40;
	}
	return USBD_NORMAL_COMPLETION;
}

static void
usbd_get_xfer_status(
    struct usbd_xfer *x, void *p, void *b, uint32_t *length, void *s)
{
	*length = x->actual;
}

#include "probe.h"

int
main(void)
{
	struct usbd_interface iface = {
	    {0, 4}, {{1, 2, 512}, {0x81, 2, 512}, {2, 2, 512}, {0x82, 2, 512}}};
	struct usbif_attach_arg a = {
	    0xa69c, 0x88dc, 1, 0xff, 0xff, 0xff, &iface};
	assert(uaicfw_match(0, 0, &a) == UMATCH_VENDOR_PRODUCT_CONF_IFACE);
	iface.desc.bAlternateSetting = 1;
	assert(uaicfw_match(0, 0, &a) == UMATCH_NONE);
	iface.desc.bAlternateSetting = 0;
	a.uiaa_class = 0xe0;
	assert(uaicfw_match(0, 0, &a) == UMATCH_NONE);
	a.uiaa_class = 0xff;
	a.uiaa_product++;
	assert(uaicfw_match(0, 0, &a) == UMATCH_NONE);
	a.uiaa_product--;
	for (fault = NONE; fault <= VERSION_SHORT; fault++) {
		opens = allocs = writes = reads = errors = identified = 0;
		versions = unavailable = 0;
		uaicfw_attach(0, 0, &a);
		assert(!opens && !allocs && uaicfw_detach(0, 0) == 0);
		bool have_id = fault == NONE || fault == VERSION_TIMEOUT ||
		    fault == VERSION_SHORT;
		assert(identified == have_id && errors == !have_id);
		assert(versions == (fault == NONE));
		assert(unavailable == (have_id && fault != NONE));
		assert(!have_id || (writes == 3 && reads == 3));
		if (fault == OTHER || fault == EMPTY_RX)
			assert(reads == 4 && writes == 1);
	}
	writes = errors = 0;
	iface.eps[2].bEndpointAddress = 1;
	assert(uaicfw_match(0, 0, &a) == UMATCH_NONE);
	uaicfw_attach(0, 0, &a);
	assert(!writes && errors == 1);
	puts("PASS: silicon and FullMAC queries, interface selection, bounded "
	     "replies, timeouts and resource release");
	return 0;
}
