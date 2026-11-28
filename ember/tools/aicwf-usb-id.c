/* SPDX-License-Identifier: BSD-2-Clause */
/* Test simultaneous bulk transfers, command completion and teardown.
 */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "a733-sha256.h"
#define SHA256_CTX struct sha256_ctx
#define SHA256_DIGEST_LENGTH 32
#define SHA256_Init sha256_init
#define SHA256_Update(ctx, buf, len) sha256_update((ctx), (buf), (len))
#define SHA256_Final(out, ctx) sha256_final((ctx), (out))
#ifndef EFTYPE
#define EFTYPE EINVAL
#endif

typedef FILE *firmware_handle_t;
static const char *kit_dir;
static unsigned kit_opens, kit_allocations, kit_handles;

static void
be64enc(void *buf, uint64_t n)
{
	uint8_t *p = buf;
	for (unsigned i = 0; i < 8; i++)
		p[7 - i] = (uint8_t)(n >> (8 * i));
}

static int
firmware_open(const char *driver, const char *name, firmware_handle_t *fh)
{
	char path[1024];
	assert(strcmp(driver, "aic8800dc") == 0 && !kit_handles && !kit_allocations);
	kit_opens++;
	if (kit_dir == NULL)
		return ENOENT;
	assert((size_t)snprintf(path, sizeof(path), "%s/%s", kit_dir, name)
	    < sizeof(path));
	*fh = fopen(path, "rb");
	if (*fh == NULL)
		return errno;
	kit_handles++;
	return 0;
}

static int
firmware_close(firmware_handle_t fh)
{
	assert(kit_handles > 0);
	kit_handles--;
	return fclose(fh) == 0 ? 0 : EIO;
}

static int
firmware_read_alloc(firmware_handle_t fh, size_t limit, void **out, size_t *len)
{
	assert(fseek(fh, 0, SEEK_END) == 0);
	long size = ftell(fh);
	rewind(fh);
	if (size <= 0 || (size_t)size > limit)
		return size <= 0 ? EINVAL : EFBIG;
	*len = (size_t)size;
	*out = malloc(*len);
	assert(*out != NULL && fread(*out, 1, *len, fh) == *len);
	kit_allocations++;
	return 0;
}

static void
firmware_free(void *buf, size_t len)
{
	assert(kit_allocations && buf && len);
	kit_allocations--;
	free(buf);
}

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
	unsigned uiaa_port, uiaa_ifaceno;
};

struct usbd_pipe {
	unsigned addr;
};

typedef struct { uint16_t idVendor, idProduct, bcdDevice; } usb_device_descriptor_t;
struct usbd_bus { struct usbd_device *ub_roothub; device_t ub_usbctl; };
struct usbd_port { unsigned up_portno; };
struct usbd_device {
	struct usbd_device *ud_myhub;
	struct usbd_bus *ud_bus;
	struct usbd_port *ud_powersrc;
	usb_device_descriptor_t desc;
};
static usb_device_descriptor_t *usbd_get_device_descriptor(struct usbd_device *d)
{ return &d->desc; }
#define DEVHANDLE_TYPE_OF 1
static int device_parent(device_t d) { return d == 3 ? 2 : 0; }
static int device_handle(device_t d) { return d; }
static int devhandle_type(int h) { return h ? DEVHANDLE_TYPE_OF : 0; }
static int devhandle_to_of(int h) { return h; }
static unsigned atomic_cas_uint(unsigned *v, unsigned old, unsigned n)
{ unsigned before = *v; if (before == old) *v = n; return before; }
#define atomic_load_relaxed(v) (*(v))
#define atomic_store_relaxed(v,n) (*(v) = (n))
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
#define ETHER_ADDR_LEN 6
#define ETHERMTU 1500
#define ETHERTYPE_PAE 0x888e
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
#define aprint_error_dev(dev, ...) report_error(__VA_ARGS__)
static void report_error(const char *, ...);
static const char *usbd_errstr(usbd_status status)
{ return status == USBD_TIMEOUT ? "timeout" : "USB error"; }
#define aprint_normal_dev(dev, ...) report(__VA_ARGS__)

#define FDT 1
static bool target_board;
static int board_tree;
static const void *fdtbus_get_data(void) { return &board_tree; }
static int OF_finddevice(const char *name)
{
	if (strcmp(name, "/soc/usb@ff780000") == 0)
		return 2;
	assert(strcmp(name, "/") == 0);
	return 1;
}
static bool of_compatible(int node, const char * const *boards)
{
	assert(node == 1 && strcmp(boards[0], "luckfox,lyra-zero-w") == 0);
	return target_board;
}

static unsigned identified, versions, condition_reports;
static unsigned int chip_id = 7, sub_id = 1;
static uint32_t features = 0xb011fa5f;
static uint32_t chip_flags;
static char condition_line[128];
static unsigned power_confirmed;

static void
report(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	if (strcmp(fmt, "clock/PMU sequence confirmed\n") == 0) {
		power_confirmed++;
		va_end(ap);
		return;
	}
	if (strncmp(fmt, "station files ", 14) == 0) {
		va_end(ap);
		return;
	}
	if (strncmp(fmt, "factory", 7) == 0) {
		vsnprintf(condition_line, sizeof(condition_line), fmt, ap);
		condition_reports++;
		va_end(ap);
		return;
	}
	unsigned int first = va_arg(ap, unsigned int);
	unsigned int second = va_arg(ap, unsigned int);
	va_end(ap);
	if (strncmp(fmt, "FullMAC", 7) == 0) {
		assert(first == 0x06040301 && second == features);
		versions++;
	} else {
		assert(first == chip_id && second == sub_id);
		identified++;
	}
}

static uint16_t
be16dec(const void *p)
{
	const uint8_t *b = p;
	return ((uint16_t)b[0] << 8) | b[1];
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
#include "aicwf_sae.h"
#include "survey-rx.h"

#define IEEE80211_F_WPA 3
#define IEEE80211_S_AUTH 2
#define IEEE80211_S_RUN 4
struct ieee80211_node { uint8_t ni_bssid[6]; };
struct ieee80211com {
	unsigned ic_flags, ic_state;
	struct ieee80211_node *ic_bss;
	uint8_t ic_myaddr[6];
};
struct aicwf_softc {
	void *sc_bus;
	uint8_t *sc_txbuf;
	struct ieee80211com sc_ic;
	bool sc_sae_enabled, sc_sae_pending, sc_connecting, sc_sae_authenticated;
	uint8_t sc_vif, sc_ap, sc_sae_bssid[6];
	unsigned sc_sae_generation;
	bool sc_receive_only, sc_connected, sc_ptk_valid, sc_rx_active;
	kmutex_t sc_epoch_lock;
	uint64_t sc_port_serial, sc_rx_serial, sc_rx_assoc, sc_rx_join;
	bool sc_join_accepted, sc_join_open;
};
struct aicwf_usb_softc {
	struct uaicfw_attach_args bus;
	struct aicwf_softc wifi;
	kmutex_t lock;
	void *rxq;
	bool prepared;
};
struct aicwf_bus_ops {
	int (*write)(struct aicwf_softc *, size_t, unsigned int, bool *);
};
#define mutex_spin_enter mutex_enter
#define mutex_spin_exit mutex_exit
#include "station-write.h"
static struct aicwf_softc *revoke_on_open;

enum data_out_test {
	OUT_IDLE, OUT_ORDINARY, OUT_ACK, OUT_NACK, OUT_LATE, OUT_STALE,
	OUT_MISSING, OUT_SHORT, OUT_STALL, OUT_ALLOC_FAIL, OUT_WAKE
};
static enum data_out_test data_test;
static unsigned data_writes;
static uint32_t clock_ticks;
static enum { CMD_NORMAL, CMD_NO_CFM, CMD_CFM_AT_DEADLINE, CMD_OUT_EXPIRED }
    command_timing;
static unsigned command_waits;
static bool stopping_on_alloc;
static void cancel_data_open(void);
static int getticks(void) { return (int)clock_ticks; }
static uint8_t data_wire[2048];
static size_t data_len;
static uint32_t previous_token;


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
static const uint32_t condition_fields[] = { 0x40200024, 0x40500148, 0x40505010, 0x40500010 };
static uint32_t field_values[4];
static unsigned current_field;
static unsigned condition_reads, fail_at;
static unsigned power_writes, power_fail_at;
static uint32_t power_hash, power_reg, power_reply;
static uint16_t power_cfm;
static bool power_update_clear;
static uint32_t power_value_error;
enum power_fault { POWER_OK, POWER_TIMEOUT, POWER_LENGTH, POWER_ADDR,
    POWER_VALUE, POWER_OUT_ERROR, POWER_TRAILING };
static enum power_fault power_fault;
static char power_error[128];
enum condition_fault { CONDITION_OK, CONDITION_ECHO, CONDITION_LENGTH,
    CONDITION_TIMEOUT, CONDITION_OUT_STALL, CONDITION_PIPE, CONDITION_FW, CONDITION_FRAME,
    CONDITION_CLEAR_FAIL, CONDITION_OUT_IO, CONDITION_OUT_CANCEL,
    CONDITION_OUT_SHORT, CONDITION_OUT_TIMEOUT };
static enum condition_fault condition_fault;
static unsigned children;
static bool child_present = true;
static int station_error, ifattach_error;
static char last_error[128];
static void
report_error(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	vsnprintf(last_error, sizeof(last_error), format, args);
	if (strncmp(last_error, "clock/PMU step ", 15) == 0)
		snprintf(power_error, sizeof(power_error), "%s", last_error);
	va_end(args);
	errors++;
}
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
static void *config_found(device_t, struct uaicfw_attach_args *, void *, int);
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
	    (p->addr == 1 && data_test == OUT_ALLOC_FAIL) ||
	    (p->addr == 2 && fault == ALLOC_TX) ||
	    (p->addr == 0x82 && fault == ALLOC_RX))
		return ENOMEM;
	*out = calloc(1, sizeof(**out));
	assert(*out && n <= sizeof((*out)->buf));
	if (p->addr == 1 && revoke_on_open != NULL) {
		revoke_on_open->sc_port_serial = 0;
		revoke_on_open->sc_rx_serial++;
		revoke_on_open = NULL;
	}
	if (p->addr == 1 && stopping_on_alloc)
		cancel_data_open();
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

static void
data_confirm(unsigned pipe, uint32_t status, uint32_t token)
{
	struct usbd_xfer *x = inputs[pipe];
	assert(x->pending);
	x->pending = false;
	x->actual = 12;
	memset(x->buf, 0, 12);
	le16enc(x->buf, 8);
	x->buf[2] = AICWF_TYPE_TX_CFM;
	le32enc(x->buf + 4, status);
	le32enc(x->buf + 8, token);
	x->cb(x, x->arg, USBD_NORMAL_COMPLETION);
}

static void deliver(void);
static usbd_status
emulate_transfer(struct usbd_xfer *x)
{
	uint8_t *b = x->buf;
	if (x->pipe->addr == 1) {
		assert(data_test != OUT_IDLE && x->len <= sizeof(data_wire));
		data_writes++;
		data_len = x->len;
		memcpy(data_wire, b, data_len);
		uint32_t token = le32dec(b + 8) & AICWF_STA_TOKEN_MAX;
		assert(token < 0x40000000U);
		if (data_test == OUT_ACK || data_test == OUT_NACK)
			data_confirm(0, data_test == OUT_ACK ? 8 : 1, token);
		if (data_test == OUT_STALE) {
			assert(previous_token != token);
			data_confirm(1, 8, previous_token);
			data_confirm(0, 0, token);
		}
		previous_token = token;
		x->actual = data_test == OUT_SHORT ? x->len - 1 : x->len;
		return data_test == OUT_STALL ? USBD_STALLED : USBD_NORMAL_COMPLETION;
	} else if (x->pipe->addr == 2) {
		static const uint8_t hdr[16] = {
		    16, 0, 0x11, 0, 0, 0, 0, 0, 0, 4, 1, 0, 100, 0, 4, 0};
		static const uint8_t version_hdr[16] = {
		    12, 0, 0x11, 0, 0, 0, 0, 0, 4, 0, 0, 0, 100, 0, 0, 0 };
		const uint16_t cmd = le16dec(b + 8);
		power_cfm = 0;
		if (writes < 2) {
			assert(x->len == 20 && memcmp(b, hdr, sizeof(hdr)) == 0);
			requested = le32dec(b + 16);
			assert(requested == (writes == 0 ? 0x40500000 : 0x20));
		} else if (writes == 2) {
			assert(x->len == sizeof(version_hdr));
			assert(memcmp(b, version_hdr, sizeof(version_hdr)) == 0);
		} else if (le16dec(b + 8) == AICWF_DBG_MEM_READ_REQ) {
			const bool has_bbpll = (field_values[1] & 1) != 0;
			assert(target_board && condition_reads < (has_bbpll ? 4U : 3U));
			assert(x->len == 20 && memcmp(b, hdr, sizeof(hdr)) == 0);
			requested = le32dec(b + 16);
			current_field = condition_reads;
			if (current_field == 2 && !has_bbpll)
				current_field = 3; /* The clock selector follows XTAL. */
			assert(requested == condition_fields[current_field]);
			condition_reads++;
			if (fail_at == condition_reads) {
				switch (condition_fault) {
				case CONDITION_OUT_IO:
					writes++;
					return USBD_IOERROR;
				case CONDITION_OUT_CANCEL:
					writes++;
					return USBD_CANCELLED;
				case CONDITION_OUT_TIMEOUT:
					writes++;
					return USBD_TIMEOUT;
				default:
					break;
				}
			}
			if ((condition_fault == CONDITION_OUT_STALL ||
			    condition_fault == CONDITION_CLEAR_FAIL) &&
			    fail_at == condition_reads) {
				writes++;
				return USBD_STALLED;
			}
		} else if (cmd == 0x0402 || cmd == 0x0411) {
			power_writes++;
			assert(condition_reads == 3 && power_writes <= 17);
			const size_t param_len = power_writes <= 2 ? 8 : 12;
			assert(cmd == (param_len == 8 ? 0x0402 : 0x0411));
			assert(x->len == 16 + param_len && le16dec(b + 14) == param_len);
			power_reg = le32dec(b + 16);
			uint32_t mask = param_len == 8 ? UINT32_MAX : le32dec(b + 20);
			power_reply = le32dec(b + x->len - 4) | ~mask;
			if (power_update_clear && power_writes >= 5 && power_writes <= 7)
				power_reply &= ~(power_writes == 5 ? 0x80U : 0x40U);
			power_cfm = cmd + 1;
			for (size_t j = 16; j < x->len; j++)
				power_hash = (power_hash ^ b[j]) * UINT32_C(16777619);
			if (power_writes == power_fail_at && power_fault == POWER_OUT_ERROR) {
				writes++;
				return USBD_IOERROR;
			}
		} else {
			assert(writes == 3 + condition_reads + power_writes && x->len == 20 &&
			    le16dec(b + 8) == AICWF_MM_GET_MAC_ADDR_REQ);
			assert(b[16] == 1 && !b[17] && !b[18] && !b[19]);
		}
		assert(inputs[0] && inputs[0]->pending && inputs[1] && inputs[1]->pending);
		writes++;
		if (fault == EARLY_REPLY && writes <= 3)
			deliver();
		if (fault == TX_ERROR)
			return USBD_INVAL;
		x->actual = fault == SHORT_TX ||
		    (condition_fault == CONDITION_OUT_SHORT &&
		     condition_reads != 0 && condition_reads == fail_at) ?
		    x->len - 1 : x->len;
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
		b += 63;
		/* Put an unrelated indication before the confirmation. */
		le16enc(b, 12);
		b[2] = 0x11;
		le16enc(b + 4, 0x1004);
		b += 16;

		if (power_cfm != 0) {
			const bool fail = power_writes == power_fail_at;
			const unsigned len = fail && power_fault == POWER_LENGTH ? 0 : 8;
			const unsigned wire = len + (fail && power_fault == POWER_TRAILING ? 4 : 0);
			le16enc(b, 12 + wire);
			b[2] = AICWF_TYPE_MSG;
			le16enc(b + 4, power_cfm);
			le16enc(b + 10, len);
			le32enc(b + 16, power_reg ^ (fail && power_fault == POWER_ADDR ? 4U : 0U));
			le32enc(b + 20, power_reply ^ (fail && power_fault == POWER_VALUE ? power_value_error : 0U));
			x->actual = 63 + 16 + 16 + wire;
			return USBD_NORMAL_COMPLETION;
		}
		if (writes == 3) {
			le16enc(b, 12 + 28);
			b[2] = 0x11;
			le16enc(b + 4, 5);
			le16enc(b + 10, fault == VERSION_SHORT ? 27 : 28);
			le32enc(b + 16, 0x06040301);
			le32enc(b + 36, features);
			x->actual = 63 + 60;
			return USBD_NORMAL_COMPLETION;
		}
		le16enc(b, fault == SHORT_BODY ? 200 : 20);
		b[2] = 0x11;
		le16enc(b + 4, fault == OTHER ? 6 : 0x401);
		le16enc(b + 10, fault == BAD_PARAM_LEN ? 7 : 8);
		le32enc(b + 16, fault == BAD_ECHO ? 0xdead : requested);
		le32enc(b + 20, requested == 0x20 ? sub_id : (chip_id << 16) | chip_flags);
		if (condition_reads != 0) {
			le32enc(b + 20, field_values[current_field]);
			if (fail_at == condition_reads && condition_fault == CONDITION_ECHO)
				le32enc(b + 16, requested ^ 4);
			if (fail_at == condition_reads && condition_fault == CONDITION_LENGTH)
				le16enc(b + 10, 4);
		}
		x->actual = fault == EMPTY_RX ? 0 : 63 + 40;
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
	usbd_status status = emulate_transfer(x);
	if (command_timing != CMD_NORMAL && x->pipe->addr == 2 && writes == 3)
		clock_ticks += command_timing == CMD_OUT_EXPIRED ? 500 : 200;
	return status;
}

static void
deliver(void)
{
	struct usbd_xfer *x = inputs[fault != ON_DATA];
	assert(x != NULL && x->pending);
	x->pending = false;
	usbd_status st = emulate_transfer(x);
	if (condition_reads != 0 && condition_fault == CONDITION_PIPE &&
	    condition_reads == fail_at)
		st = USBD_IOERROR;
	if (fault == FW_ERROR || (condition_reads != 0 &&
	    condition_fault == CONDITION_FW && condition_reads == fail_at))
		le16enc(x->buf + 67, 0x0408);
	if (condition_reads != 0 && condition_fault == CONDITION_FRAME &&
	    condition_reads == fail_at)
		le16enc(x->buf + 67 + 6, 0xffff); /* Unrelated malformed record. */
	x->cb(x, x->arg, st);
}

static int
cv_timedwait(kcondvar_t *cv, kmutex_t *m, int ticks)
{
	assert(ticks > 0 && ticks <= 500 && *m);
	if (command_timing != CMD_NORMAL && writes == 3) {
		assert(command_timing != CMD_OUT_EXPIRED);
		assert(ticks == 300 - 100 * (int)command_waits);
		command_waits++;
		clock_ticks += 100;
		if (command_timing == CMD_CFM_AT_DEADLINE && command_waits == 3) {
			mutex_exit(m);
			deliver();
			mutex_enter(m);
			return EWOULDBLOCK;
		}
		return 0;
	}
	if (data_test == OUT_WAKE) {
		static unsigned wakes;
		assert(ticks == 500 - (int)(100 * wakes++));
		clock_ticks += 100;
		return 0; /* A different condition used this condition variable. */
	}
	assert(ticks == 500);
	if (power_cfm && power_writes == power_fail_at && power_fault == POWER_TIMEOUT)
		return EWOULDBLOCK;
	if (condition_fault == CONDITION_TIMEOUT && condition_reads != 0 &&
	    condition_reads == fail_at)
		return EWOULDBLOCK;
	if (data_test == OUT_MISSING || data_test == OUT_STALE)
		return EWOULDBLOCK;
	if (data_test == OUT_LATE) {
		mutex_exit(m);
		data_confirm(1, 8, previous_token);
		mutex_enter(m);
		return EWOULDBLOCK; /* CFM arrived at the deadline. */
	}
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
static struct aicwf_usb_softc child;
static void *device_private(device_t self)
{ return self == 0 ? (void *)&device : (void *)&child; }

/* Stop may mark the device while the sender sleeps in xfer allocation. */
static void
cancel_data_open(void)
{
	assert(mutex_owned(&device.sc_cmd_lock));
	mutex_enter(&device.sc_rx_lock);
	device.sc_stopping = true;
	cv_broadcast(&device.sc_cv);
	mutex_exit(&device.sc_rx_lock);
}

static void
ignore_input(void *cookie, const uint8_t *p, size_t len, uint64_t serial)
{
}

/* Run both sides of the synchronous station attachment. */
#define SIMPLEQ_INIT(q) ((void)0)
#define workqueue_create(q, ...) (*(q) = &child, 0)
#define aicwf_usb_input ignore_input
static uint64_t aicwf_usb_serial(void *cookie) { return 0; }
static void
aicwf_prepare(struct aicwf_softc *sc, device_t self,
    const struct aicwf_bus_ops *ops, void *cookie, bool dc)
{ assert(self == 1 && cookie == &child && dc && ops->write == aicwf_usb_write); }
static int aicwf_station_setup(struct aicwf_softc *sc)
{ assert(sc->sc_receive_only && device.sc_input != NULL); return station_error; }
static int aicwf_ifattach(struct aicwf_softc *sc) { return ifattach_error; }
static int
aicwf_usb_detach(device_t self, int flags)
{
	child.bus.listen(child.bus.cookie, NULL, NULL, NULL);
	child.prepared = false;
	return 0;
}
#include "station-attach.h"
static void *
config_found(device_t parent, struct uaicfw_attach_args *a, void *pr, int cf)
{
	assert(!a->if_attached);
	if (!child_present)
		return NULL;
	children++;
	memset(&child, 0, sizeof(child));
	aicwf_usb_attach(parent, 1, a);
	assert(a->if_attached == (station_error == 0 && ifattach_error == 0));
	radio = child.bus;
	return &child;
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

struct received_record {
	size_t len;
	unsigned limit;
	uint8_t type;
	uint16_t id;
	uint64_t serial;
};
static struct received_record seen[16];
static size_t nseen;
static uint64_t generation;
static uint64_t get_serial(void *cookie) { return generation; }

static void
capture_record(void *cookie, const uint8_t *p, size_t len, uint64_t serial)
{
	assert(nseen < __arraycount(seen));
	struct received_record *r = &seen[nseen++];
	r->len = len;
	r->serial = serial;
	r->type = p[2] & 0x7f;
	r->id = r->type == AICWF_TYPE_MSG ? le16dec(p + 4) : 0;
	r->limit = aicwf_usb_queue_limit(p, len, true);
	if ((r->type & AICWF_TYPE_CFG) == 0)
		assert(aicwf_usb_queue_limit(p, len, false) == 16);
}

static void
message(uint8_t *p, uint16_t id)
{
	memset(p, 0, 20);
	le16enc(p, 14);
	p[2] = AICWF_TYPE_MSG;
	le16enc(p + 4, id);
	le16enc(p + 10, 2);
	p[16] = 1;
	p[17] = 0xff;
	/* Padding belongs to this record regardless of its contents. */
	p[18] = 0xa5;
	p[19] = 0x5a;
}

static void
survey_queue_checks(void)
{
	struct uaicfw_softc sc = { 0 };
	struct uaicfw_request req = { AICWF_MM_ADD_IF_CFM, 2, USBD_IN_PROGRESS, false, NULL };
	uint8_t records[240] = { 0 };
	uint8_t first[UAICFW_RX_SIZE];
	uint8_t rest[24];
	struct uaicfw_rx_stream *s = &sc.sc_stream[0];

	sc.sc_input = capture_record;
	sc.sc_serial = get_serial;
	sc.sc_request = &req;
	mutex_enter(&sc.sc_rx_lock);
	le16enc(records, 133);	/* 60-byte descriptor plus 133-byte MPDU */
	message(records + 193, AICWF_SCANU_RESULT_IND);
	message(records + 213, AICWF_MM_ADD_IF_CFM);
	assert(uaicfw_receive(&sc, s, records, 233) == USBD_NORMAL_COMPLETION);
	assert(nseen == 3 && seen[0].len == 193 && seen[0].limit == 0);
	assert(seen[1].len == 18 && seen[1].limit == 24);
	assert(seen[2].id == AICWF_MM_ADD_IF_CFM && seen[2].limit == 32);
	assert(req.status == USBD_NORMAL_COMPLETION && sc.sc_reply[0] == 1 &&
	    sc.sc_reply[1] == 0xff);
	sc.sc_request = NULL;

	/* Split the header, parameters or alignment at a full URB boundary.
	 * A completion on the other pipe must not contribute to this record. */
	for (size_t split = 1; split < 20; split++) {
		size_t data = sizeof(first) - split;
		memset(first, 0, sizeof(first));
		le16enc(first, data - AICWF_RXHDR_LEN);
		memcpy(first + data, records + 193, split);
		nseen = 0;
		assert(uaicfw_receive(&sc, s, first, sizeof(first)) == USBD_NORMAL_COMPLETION);
		assert(nseen == 1 && seen[0].len == data && s->pending == split);
		assert(uaicfw_receive(&sc, &sc.sc_stream[1], records + 213, 20) ==
		    USBD_NORMAL_COMPLETION);
		assert(nseen == 2 && seen[1].limit == 32);
		assert(uaicfw_receive(&sc, s, records + 193 + split, 40 - split) ==
		    USBD_NORMAL_COMPLETION);
		assert(nseen == 4 && seen[2].limit == 24 && seen[3].limit == 32);
		assert(s->pending == 0);
	}
	/* A DATA record can itself span URBs. Its continuation has no header. */
	memset(first, 0, sizeof(first));
	le16enc(first, sizeof(first) + 1 - AICWF_RXHDR_LEN);
	rest[0] = 0xe1;
	message(rest + 1, AICWF_SCANU_START_CFM);
	nseen = 0;
	generation = 41;
	assert(uaicfw_receive(&sc, s, first, sizeof(first)) == USBD_NORMAL_COMPLETION);
	assert(s->pending == sizeof(first) && nseen == 0);
	generation++;
	assert(uaicfw_receive(&sc, s, rest, 21) == USBD_NORMAL_COMPLETION);
	assert(nseen == 2 && seen[0].len == 2049 && seen[1].limit == 32);
	assert(seen[0].serial == 41 && seen[1].serial == 42);
	assert(s->pending == 0);

	/* Replacing the listener retires its partial record only. */
	nseen = 0;
	assert(uaicfw_receive(&sc, s, first, sizeof(first)) == USBD_NORMAL_COMPLETION);
	mutex_exit(&sc.sc_rx_lock);
	uaicfw_listen(&sc, NULL, NULL, NULL);
	uaicfw_listen(&sc, capture_record, get_serial, NULL);
	mutex_enter(&sc.sc_rx_lock);
	assert(uaicfw_receive(&sc, s, rest, 21) == USBD_NORMAL_COMPLETION);
	assert(nseen == 1 && seen[0].limit == 32 && s->pending == 0);

	/* The largest DATA record spans three host transfers. */
	le16enc(first, UAICFW_PAYLOAD_MAX);
	nseen = 0;
	assert(uaicfw_receive(&sc, s, first, sizeof(first)) == USBD_NORMAL_COMPLETION);
	memset(first, 0, sizeof(first));
	assert(uaicfw_receive(&sc, s, first, sizeof(first)) == USBD_NORMAL_COMPLETION);
	assert(s->pending == 2 * sizeof(first) && nseen == 0);
	assert(uaicfw_receive(&sc, s, first,
	    UAICFW_PAYLOAD_MAX + AICWF_RXHDR_LEN - 2 * sizeof(first)) == USBD_NORMAL_COMPLETION);
	assert(nseen == 1 && seen[0].len == UAICFW_PAYLOAD_MAX + AICWF_RXHDR_LEN);

	/* A short transfer can omit final padding, never required message bytes. */
	nseen = 0;
	assert(uaicfw_receive(&sc, s, records + 193, 18) == USBD_NORMAL_COMPLETION);
	assert(uaicfw_receive(&sc, s, records + 213, 20) == USBD_NORMAL_COMPLETION);
	assert(nseen == 2 && seen[0].limit == 24 && seen[1].limit == 32);
	assert(uaicfw_receive(&sc, s, records + 193, 17) == USBD_INVAL);
	assert(nseen == 2 && s->pending == 0);
	message(rest, 0xffff);
	assert(aicwf_usb_queue_limit(rest, 18, true) == 0);
	assert(aicwf_usb_queue_limit(rest, 18, false) == 32);
	/* TX confirmation admission belongs to the normal station mode. */
	rest[2] = AICWF_TYPE_TX_CFM;
	le16enc(rest, 8);
	assert(aicwf_usb_queue_limit(rest, 12, false) == 32);
	assert(aicwf_usb_queue_limit(rest, 12, true) == 0);
	assert(aicwf_usb_queue_limit(rest, 11, false) == 0);
	le16enc(first, UAICFW_PAYLOAD_MAX + 1);
	assert(uaicfw_receive(&sc, s, first, sizeof(first)) == USBD_INVAL);
	le16enc(first, sizeof(first));
	assert(uaicfw_receive(&sc, s, first, sizeof(first)) == USBD_NORMAL_COMPLETION);
	mutex_exit(&sc.sc_rx_lock);

	/* A stall can lose bytes. Never parse a continuation after recovery. */
	struct usbd_xfer xfer = { 0 };
	sc.sc_rx[0].xfer = &xfer;
	uaicfw_rxeof(&xfer, &sc, USBD_STALLED);
	assert(sc.sc_error == USBD_STALLED && !sc.sc_recover_task.pending);
}

static void
ethernet_checks(void)
{
	uint8_t packet[AICWF_HDR_LEN + AICWF_TXDESC_LEN + ETHERMTU + 1] = {0};
	struct aicwf_usb_softc usc = { .bus = radio };
	struct aicwf_softc sc = { .sc_bus = &usc, .sc_txbuf = packet };
	uint8_t *desc = packet + AICWF_HDR_LEN;
	size_t hdr = AICWF_HDR_LEN + AICWF_TXDESC_LEN;
	bool attempted;
	const unsigned base = opens;

	packet[2] = AICWF_TYPE_DATA;
	packet[3] = 0xaa; /* Common bus byte is not part of the USB ABI. */
	desc[20] = 8; /* IPv4 */
	le16enc(packet, AICWF_TXDESC_LEN + 1);
	le16enc(desc, 1);
	desc[AICWF_TXDESC_LEN] = 0xa5;
	sc.sc_receive_only = sc.sc_connected = true;
	assert(aicwf_usb_write(&sc, hdr + 1, 0, &attempted) == ENETDOWN && !attempted);
	sc.sc_receive_only = sc.sc_connected = false;
	assert(aicwf_usb_write(&sc, hdr + 1, 0, &attempted) == ENETDOWN && !attempted);
	assert(opens == base && !data_writes);
	sc.sc_connected = true;
	sc.sc_rx_assoc = 2;
	sc.sc_join_accepted = true;
	sc.sc_ic.ic_flags = IEEE80211_F_WPA;
	assert(aicwf_usb_write(&sc, hdr + 1, 0, &attempted) == EACCES && !attempted);
	/* The saved association is secured. A profile edit does not reconnect. */
	sc.sc_ic.ic_flags = 0;
	data_test = OUT_ALLOC_FAIL;
	assert(aicwf_usb_write(&sc, hdr + 1, 0, &attempted) == EACCES && !attempted);
	assert(!data_writes && opens == base);
	sc.sc_ic.ic_flags = IEEE80211_F_WPA;
	sc.sc_ptk_valid = sc.sc_rx_active = true;
	sc.sc_rx_serial = 4;
	assert(aicwf_usb_write(&sc, hdr + 1, 0, &attempted) == EACCES && !attempted);
	sc.sc_port_serial = 3;
	assert(aicwf_usb_write(&sc, hdr + 1, 0, &attempted) == EACCES && !attempted);
	sc.sc_port_serial = 4;
	sc.sc_join_accepted = false; /* IND before CONNECT_CFM. */
	assert(aicwf_usb_write(&sc, hdr + 1, 0, &attempted) == EACCES && !attempted);
	sc.sc_join_accepted = true;
	sc.sc_rx_assoc = 0; /* CONNECT_CFM before IND or a retired join. */
	assert(aicwf_usb_write(&sc, hdr + 1, 0, &attempted) == EACCES && !attempted);
	sc.sc_rx_assoc = 2;
	data_test = OUT_ALLOC_FAIL;
	assert(aicwf_usb_write(&sc, hdr + 1, 0, &attempted) == ENOMEM && !attempted);
	assert(opens == base && !data_writes);

	data_test = OUT_ORDINARY;
	desc[26] = 1;
	assert(aicwf_usb_write(&sc, hdr + 1, 0, &attempted) == EINVAL && !attempted);
	desc[26] = 0;
	desc[4] = 1;
	assert(aicwf_usb_write(&sc, hdr + 1, 0, &attempted) == EINVAL && !attempted);
	desc[4] = 0;
	assert(aicwf_usb_write(&sc, hdr, 0, &attempted) == EINVAL && !attempted);
	assert(aicwf_usb_write(&sc, sizeof(packet), 0, &attempted) == EMSGSIZE && !attempted);
	assert(opens == base && !data_writes);
	revoke_on_open = &sc;
	assert(aicwf_usb_write(&sc, hdr + 1, 0, &attempted) == EACCES && !attempted);
	assert(opens == base + 1 && !data_writes && sc.sc_port_serial == 0);
	sc.sc_port_serial = sc.sc_rx_serial; /* A later port CFM. */
	assert(aicwf_usb_write(&sc, hdr + 1, 0, &attempted) == 0 && attempted);
	assert(opens == base + 1 && data_writes == 1 && data_len == hdr + 1);
	assert(le16dec(data_wire) == data_len && data_wire[2] == 1 && data_wire[3] == 0);
	assert(memcmp(desc, data_wire + AICWF_HDR_LEN, AICWF_TXDESC_LEN + 1) == 0);
	/* A confirmed open join keeps its policy through a pending profile edit. */
	sc.sc_join_open = true;
	sc.sc_ptk_valid = false;
	sc.sc_port_serial = 0;
	sc.sc_ic.ic_flags = IEEE80211_F_WPA;
	le16enc(packet, AICWF_TXDESC_LEN + ETHERMTU);
	le16enc(desc, ETHERMTU);
	assert(aicwf_usb_write(&sc, hdr + ETHERMTU, 0, &attempted) == 0 && attempted);
	assert(data_len == hdr + ETHERMTU);

	le16enc(packet, AICWF_TXDESC_LEN + 1);
	le16enc(desc, 1);
	desc[20] = 0x88; desc[21] = 0x8e;
	sc.sc_ptk_valid = false; /* EAPOL establishes the pairwise key. */
	data_test = OUT_ACK;
	assert(aicwf_usb_write(&sc, hdr + 1, 0, &attempted) == 0 && attempted);
	assert(data_len == hdr + 4 && le16dec(data_wire) == data_len);
	assert((le32dec(data_wire + 8) & AICWF_STA_CFM_REQUEST) != 0);
	assert((previous_token & AICWF_DC_DATA_TOKEN) != 0);
	assert(data_wire[hdr] == 0xa5 && data_wire[hdr + 1] == 0 &&
	    data_wire[hdr + 2] == 0 && data_wire[hdr + 3] == 0);
	data_test = OUT_NACK;
	assert(aicwf_usb_write(&sc, hdr + 1, 0, &attempted) == EIO && attempted);
	data_test = OUT_LATE;
	assert(aicwf_usb_write(&sc, hdr + 1, 0, &attempted) == 0 && attempted);
	clock_ticks = UINT32_MAX - 200;
	data_test = OUT_WAKE;
	assert(aicwf_usb_write(&sc, hdr + 1, 0, &attempted) == ETIMEDOUT && attempted);
	data_test = OUT_MISSING;
	assert(aicwf_usb_write(&sc, hdr + 1, 0, &attempted) == ETIMEDOUT && attempted);
	data_test = OUT_STALE;
	assert(aicwf_usb_write(&sc, hdr + 1, 0, &attempted) == ETIMEDOUT && attempted);
	data_test = OUT_SHORT;
	assert(aicwf_usb_write(&sc, hdr + 1, 0, &attempted) == EIO && attempted);
	data_test = OUT_STALL;
	unsigned saved_clears = clears;
	assert(aicwf_usb_write(&sc, hdr + 1, 0, &attempted) == EIO && attempted);
	assert(clears == saved_clears + 1 && data_writes == 10 && opens == base + 1);
	clears = saved_clears;
	device.sc_data_sequence = AICWF_DC_DATA_TOKEN - 1;
	assert(aicwf_usb_write(&sc, hdr + 1, 0, &attempted) == EOVERFLOW && !attempted);
	assert(device.sc_data_reply == NULL && data_writes == 10 && writes == 4);
	data_test = OUT_IDLE;
}

static void
security_checks(void)
{
	uint8_t packet[AICWF_HDR_LEN + AICWF_TXDESC_LEN + 40] = {0};
	uint8_t *desc = packet + AICWF_HDR_LEN;
	uint8_t *frame = desc + AICWF_TXDESC_LEN;
	const uint8_t addr[6] = {2,0,0,0,0,1};
	struct ieee80211_node peer = { .ni_bssid = {2,0,0,0,0,2} };
	struct aicwf_usb_softc usc = { .bus = radio };
	struct aicwf_softc sc = { .sc_bus = &usc, .sc_txbuf = packet };
	const unsigned previous = data_writes;
	size_t hdr = AICWF_HDR_LEN + AICWF_TXDESC_LEN;
	bool attempted;

	device.sc_data_sequence = 100;
	sc.sc_vif = desc[24] = 2;
	sc.sc_ap = 4;
	sc.sc_ic.ic_bss = &peer;
	sc.sc_ic.ic_state = IEEE80211_S_AUTH;
	sc.sc_sae_generation = 7;
	sc.sc_rx_serial = sc.sc_rx_join = 18;
	sc.sc_sae_enabled = sc.sc_connecting = sc.sc_sae_pending = true;
	memcpy(sc.sc_sae_bssid, peer.ni_bssid, 6);
	memcpy(sc.sc_ic.ic_myaddr, addr, 6);
	memcpy(desc + 8, peer.ni_bssid, 6); memcpy(desc + 14, addr, 6);
	memcpy(frame + 4, peer.ni_bssid, 6); memcpy(frame + 10, addr, 6);
	memcpy(frame + 16, peer.ni_bssid, 6);
	packet[2] = AICWF_TYPE_DATA;
	desc[25] = 0xff;
	frame[0] = 0xb0; frame[24] = 3; frame[26] = 1;
	le16enc(desc + 26, AICWF_TX_MGMT);
	le16enc(packet, AICWF_TXDESC_LEN + 35); le16enc(desc, 35);

	assert(radio.data(radio.cookie, UAICFW_SAE_AUTH, packet, hdr + 35,
	    NULL, &attempted) == EACCES && !attempted);
	sc.sc_receive_only = true;
	assert(aicwf_usb_write(&sc, hdr + 35, 0, &attempted) == ENETDOWN && !attempted);
	sc.sc_receive_only = false; sc.sc_sae_pending = false;
	assert(aicwf_usb_write(&sc, hdr + 35, 0, &attempted) == ESTALE && !attempted);
	sc.sc_sae_pending = true;
	assert(aicwf_usb_write(&sc, hdr + 35, 0, &attempted) == EACCES && !attempted);
	sc.sc_join_accepted = sc.sc_join_open = true;
	assert(aicwf_usb_write(&sc, hdr + 35, 0, &attempted) == EACCES && !attempted);
	sc.sc_join_open = false; sc.sc_rx_join--;
	assert(aicwf_usb_write(&sc, hdr + 35, 0, &attempted) == EACCES && !attempted);
	sc.sc_rx_join = sc.sc_rx_serial; frame[16] ^= 1;
	assert(aicwf_usb_write(&sc, hdr + 35, 0, &attempted) == EINVAL && !attempted);
	frame[16] ^= 1; frame[26] = 3;
	assert(aicwf_usb_write(&sc, hdr + 35, 0, &attempted) == EINVAL && !attempted);
	frame[26] = 1; data_test = OUT_ORDINARY;
	uaicfw_close(&device.sc_data);
	revoke_on_open = &sc;
	assert(aicwf_usb_write(&sc, hdr + 35, 0, &attempted) == EACCES && !attempted);
	assert(!revoke_on_open);
	sc.sc_rx_join = sc.sc_rx_serial;
	assert(aicwf_usb_write(&sc, hdr + 35, 0, &attempted) == 0 && attempted);
	assert(data_len == hdr + 35 && le32dec(data_wire + 8) == 0);
	assert(data_writes == previous + 1);

	frame[0] = 0xd0; frame[24] = 8; frame[25] = 1;
	desc[25] = sc.sc_ap; le16enc(desc + 26, AICWF_TX_MGMT | AICWF_TX_MGMT_ROBUST);
	le16enc(packet, AICWF_TXDESC_LEN + 28); le16enc(desc, 28);
	assert(radio.data(radio.cookie, UAICFW_SA_QUERY, packet, hdr + 28,
	    NULL, &attempted) == EACCES && !attempted);
	assert(aicwf_usb_write(&sc, hdr + 28, 0, &attempted) == ESTALE && !attempted);
	sc.sc_ic.ic_state = IEEE80211_S_RUN; sc.sc_sae_pending = sc.sc_connecting = false;
	sc.sc_join_accepted = sc.sc_rx_active = sc.sc_connected = sc.sc_sae_authenticated = true;
	sc.sc_ptk_valid = true; sc.sc_rx_assoc = 20; sc.sc_rx_serial = 23;
	assert(aicwf_usb_write(&sc, hdr + 28, 0, &attempted) == EACCES && !attempted);
	sc.sc_port_serial = sc.sc_rx_serial;
	sc.sc_join_open = true;
	assert(aicwf_usb_write(&sc, hdr + 28, 0, &attempted) == EACCES && !attempted);
	sc.sc_join_open = false; frame[24] = 10;
	assert(aicwf_usb_write(&sc, hdr + 28, 0, &attempted) == EINVAL && !attempted);
	frame[24] = 8; data_test = OUT_ACK;
	uaicfw_close(&device.sc_data);
	revoke_on_open = &sc;
	assert(aicwf_usb_write(&sc, hdr + 28, 0, &attempted) == EACCES && !attempted);
	assert(!revoke_on_open);
	sc.sc_port_serial = sc.sc_rx_serial;
	assert(aicwf_usb_write(&sc, hdr + 28, 0, &attempted) == 0 && attempted);
	assert((le32dec(data_wire + 8) & AICWF_STA_CFM_REQUEST) != 0);
	data_test = OUT_STALE;
	assert(aicwf_usb_write(&sc, hdr + 28, 0, &attempted) == ETIMEDOUT && attempted);
	assert(data_writes == previous + 3);
	data_test = OUT_IDLE;
}


/* A successful version reply can precede a failed mountroot handoff. */
static void
mountroot_failure_checks(struct usbif_attach_arg *a)
{
	for (unsigned int i = 0; i < 5; i++) {
		fault = NONE;
		writes = reads = errors = children = 0;
		config.bNumInterface = 3;
		memset(&device, 0, sizeof(device));
		uaicfw_attach(0, 0, a);
		assert(mountroot_hook && opens == 3 && allocs == 3 && !errors);
		if (i < 2) {
			struct usbd_xfer *x = inputs[i];
			assert(x->pending);
			x->pending = false;
			x->cb(x, x->arg, USBD_IOERROR);
		}
		child_present = i != 2;
		station_error = i == 3 ? EIO : 0;
		ifattach_error = i == 4 ? ENOMEM : 0;
		mountroot_hook(0);
		assert(children == (i >= 3) && device.sc_stopping);
		assert(!opens && !allocs && writes == 3);
		if (i < 2)
			assert(errors == 1 && strstr(last_error, "transport unavailable"));
		if (i == 2)
			assert(errors == 1 && strstr(last_error, "driver unavailable"));
		if (i >= 3)
			assert(errors == 1 && strstr(last_error, "station setup failed") &&
			    !child.prepared && device.sc_input == NULL);
		assert(uaicfw_detach(0, 0) == 0);
		child_present = true;
		station_error = ifattach_error = 0;
	}
}

static void
command_deadline_checks(struct usbif_attach_arg *a)
{
	fault = NONE;
	for (command_timing = CMD_NO_CFM; command_timing <= CMD_OUT_EXPIRED;
	    command_timing++) {
		config.bNumInterface = 3;
		command_waits = writes = reads = 0;
		clock_ticks = UINT32_MAX - 250;
		memset(&device, 0, sizeof(device));
		uaicfw_attach(0, 0, a);
		assert(writes == 3 && device.sc_request == NULL);
		assert(command_waits == (command_timing == CMD_OUT_EXPIRED ? 0 : 3));
		assert((mountroot_hook != NULL) == (command_timing == CMD_CFM_AT_DEADLINE));
		assert(uaicfw_detach(0, 0) == 0 && !opens && !allocs);
	}
	command_timing = CMD_NORMAL;
}

static void
condition_checks(struct usbif_attach_arg *a)
{
	static const struct {
		uint32_t usb, crystal, pll, clock;
		enum condition_fault fault;
		unsigned at, expected;
		usbd_status terminal;
		const char *text;
	} cases[] = {
		{ 0xffff0119, 0, 0, 0, CONDITION_OK, 0, 3, USBD_NORMAL_COMPLETION, "needed, BBPLL setup skip (no CPU crystal), clock select zero" },
		{ 0, 1, 3U << 29, 4, CONDITION_OK, 0, 4, USBD_NORMAL_COMPLETION, "skip, BBPLL setup skip (CPU crystal, setting present), clock select factory first" },
		{ 0, 1, 7U << 29, 6, CONDITION_OK, 0, 4, USBD_NORMAL_COMPLETION, "skip, BBPLL setup needed (CPU crystal), clock select factory final" },
		{ 0, 0, 0, 0x10006, CONDITION_OK, 0, 3, USBD_NORMAL_COMPLETION, "clock select other" },
		{ 0, 0, 0, 0, CONDITION_ECHO, 3, 3, USBD_NORMAL_COMPLETION, "clock select unknown" },
		{ 0, 1, 0, 0, CONDITION_TIMEOUT, 4, 4, USBD_NORMAL_COMPLETION, "clock select unknown" },
		{ 0, 1, 0, 0, CONDITION_ECHO, 1, 1, USBD_NORMAL_COMPLETION, "unknown, BBPLL setup unknown" },
		{ 0, 1, 0, 0, CONDITION_LENGTH, 2, 2, USBD_NORMAL_COMPLETION, "skip, BBPLL setup unknown" },
		{ 0, 1, 0, 0, CONDITION_TIMEOUT, 3, 3, USBD_NORMAL_COMPLETION, "skip, BBPLL setup unknown (CPU crystal)" },
		{ 0, 1, 0, 0, CONDITION_OUT_STALL, 2, 2, USBD_NORMAL_COMPLETION, "skip, BBPLL setup unknown" },
		{ 0, 1, 0, 0, CONDITION_PIPE, 2, 2, USBD_IOERROR, "skip, BBPLL setup unknown" },
		{ 0, 1, 0, 0, CONDITION_FW, 2, 2, USBD_IOERROR, "skip, BBPLL setup unknown" },
		{ 0, 1, 0, 0, CONDITION_FRAME, 2, 2, USBD_INVAL, "skip, BBPLL setup unknown" },
		{ 0, 1, 0, 0, CONDITION_CLEAR_FAIL, 2, 2, USBD_IOERROR, "skip, BBPLL setup unknown" },
		{ 0, 1, 0, 0, CONDITION_OUT_IO, 2, 2, USBD_IOERROR, "skip, BBPLL setup unknown" },
		{ 0, 1, 0, 0, CONDITION_OUT_CANCEL, 2, 2, USBD_CANCELLED, "skip, BBPLL setup unknown" },
		{ 0, 1, 0, 0, CONDITION_OUT_SHORT, 2, 2, USBD_SHORT_XFER, "skip, BBPLL setup unknown" },
		{ 0, 1, 0, 0, CONDITION_OUT_TIMEOUT, 2, 2, USBD_TIMEOUT, "skip, BBPLL setup unknown" },
	};
	target_board = true;
	for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		memset(&device, 0, sizeof(device));
		opens = allocs = reads = writes = errors = children = condition_reports = 0;
		condition_reads = 0;
		condition_fault = cases[i].fault;
		clear_error = condition_fault == CONDITION_CLEAR_FAIL;
		fail_at = cases[i].at;
		chip_flags = i % 2 ? 1U << 25 : 0;
		field_values[0] = cases[i].usb;
		field_values[1] = cases[i].crystal;
		field_values[2] = cases[i].pll;
		field_values[3] = cases[i].clock;
		fault = NONE;
		uaicfw_attach(0, 0, a);
		assert(condition_reads == cases[i].expected && condition_reports == 1);
		assert(strstr(condition_line, cases[i].text) != NULL);
		if (condition_fault != CONDITION_OK)
			assert(strstr(condition_line, "clock select unknown") != NULL);
		assert(strstr(condition_line, chip_flags ? "PMU mask extended" :
		    "PMU mask base") != NULL);
		assert(mountroot_hook != NULL);
		if (cases[i].terminal != USBD_NORMAL_COMPLETION) {
			assert(device.sc_error == cases[i].terminal);
			mountroot_hook(0);
			assert(children == 0 && device.sc_stopping);
			assert(uaicfw_detach(0, 0) == 0 && !opens && !allocs);
			continue;
		}
		assert(device.sc_error == USBD_NORMAL_COMPLETION);
		mountroot_hook(0);
		if (condition_fault == CONDITION_TIMEOUT)
			deliver(); /* The station discards the late DBG CFM. */
		assert(children == 1 && device.sc_input != NULL && !errors);
		uint8_t ether_query[20];
		uint8_t query[4] = { 1, 0, 0, 0 };
		size_t n = aicwf_command_encode(ether_query, sizeof(ether_query),
		    AICWF_MM_GET_MAC_ADDR_REQ, query, sizeof(query));
		bool attempted = false;
		assert(n != 0 && radio.write(radio.cookie, ether_query, n, &attempted) == 0 && attempted);
		assert(uaicfw_detach(0, 0) == 0 && !opens && !allocs);
	}
	condition_fault = CONDITION_OK;
	chip_flags = 0;
	clear_error = false;
	for (int i = 0; i < 6; i++) {
		struct usbif_attach_arg other = *a;
		target_board = i != 0;
		if (i == 1)
			other.uiaa_port++;
		if (i == 2)
			other.uiaa_ifaceno++;
		chip_id = i == 3 ? 0xc7 : 7;
		sub_id = i == 4 ? 2 : 1;
		features = i == 5 ? 0xb011fa5e : 0xb011fa5f;
		memset(&device, 0, sizeof(device));
		opens = allocs = reads = writes = errors = children = condition_reports = 0;
		condition_reads = 0;
		uaicfw_attach(0, 0, &other);
		assert(condition_reads == 0 && condition_reports == 0);
		assert(writes == 3);
		assert((mountroot_hook == NULL) == (i == 3 || i == 4));
		assert(uaicfw_detach(0, 0) == 0 && !opens && !allocs);
	}
	chip_id = 7;
	sub_id = 1;
	features = 0xb011fa5f;
	target_board = false;
}

static void
power_checks(struct usbif_attach_arg a)
{
	struct usbd_device root = { 0 };
	struct usbd_bus bus = { &root, 3 };
	struct usbd_port hubport = { 1 };
	struct usbd_device hub = { &root, &bus, &hubport, { 0x1a86, 0x8091, 0 } };
	struct usbd_device wlan = { &hub, &bus, NULL, { 0xa69c, 0x88dc, 0x0100 } };
	a.uiaa_device = &wlan;
	target_board = true;
	chip_flags = 1U << 25;
	condition_fault = CONDITION_OK;
	fault = NONE;
	fail_at = 0;
	config.bNumInterface = 3;
	memset(field_values, 0, sizeof(field_values));
	/* Verified against the full and masked tables in both factory ELFs. */
	for (unsigned int i = 0; i < 36; i++) {
		uaicfw_lyra_power_busy = 0; /* A separate boot for each case. */
		memset(&device, 0, sizeof(device));
		power_writes = power_confirmed = condition_reads = writes = errors = children = 0;
		kit_opens = 0;
		power_hash = UINT32_C(2166136261);
		power_error[0] = '\0';
		power_update_clear = i >= 32;
		power_value_error = 4;
		power_fail_at = i >= 1 && i <= 17 ? i : 0;
		power_fault = power_fail_at ? POWER_TIMEOUT : POWER_OK;
		switch (i) {
		case 18: power_fail_at = 2; power_fault = POWER_VALUE; break;
		case 19: power_fail_at = 3; power_fault = POWER_ADDR; break;
		case 20: power_fail_at = 3; power_fault = POWER_LENGTH; break;
		case 21: power_fail_at = 15; power_fault = POWER_OUT_ERROR; break;
		case 22: chip_flags = 0; break;
		case 23: field_values[0] = 0x119; break;
		case 24: field_values[1] = 1; break;
		case 25: field_values[3] = 6; break;
		case 26: hub.desc.idProduct++; break;
		case 27: hubport.up_portno++; break;
		case 28: wlan.desc.bcdDevice++; break;
		case 29: bus.ub_usbctl++; break;
		case 30: hub.ud_myhub = NULL; break;
		case 31: power_fail_at = 16; power_fault = POWER_TRAILING; break;
		case 33: case 34: case 35:
			power_fail_at = i - 28;
			power_value_error = power_fail_at == 5 ? 0x10 : 1;
			power_fault = POWER_VALUE;
			break;
		}
		uaicfw_attach(0, 0, &a);
		if (i == 0 || i == 32) {
			assert(power_writes == 17 && power_confirmed == 1);
			assert(power_hash == UINT32_C(0xfd3968ad));
			assert(uaicfw_lyra_power_busy == 0);
		} else if (power_fail_at) {
			assert(power_writes == power_fail_at && power_confirmed == 0);
			assert(mountroot_hook == NULL && device.sc_stopping);
			assert(!opens && !allocs && !children && power_error[0]);
			if (power_fault == POWER_LENGTH)
				assert(strstr(power_error, "length 0/0, expected 8/8"));
			if (power_fault == POWER_TRAILING)
				assert(strstr(power_error, "length 8/12, expected 8/8"));
			if (power_fault == POWER_ADDR)
				assert(strstr(power_error, "register echo mismatch"));
			if (power_fault == POWER_VALUE) {
				char detail[48];
				snprintf(detail, sizeof(detail), "selected bits differ by %#x",
				    power_value_error);
				assert(strstr(power_error, detail));
			}
			assert(uaicfw_detach(0, 0) == 0);
			memset(&device, 0, sizeof(device));
			writes = condition_reads = 0;
			/* A re-enumeration can change the reported firmware and USB version. */
			features ^= 1;
			wlan.desc.bcdDevice++;
			uaicfw_attach(0, 0, &a);
			assert(writes == 0 && condition_reads == 0 && !opens && !allocs);
			assert(device.sc_stopping && !mountroot_hook && !children);
			features ^= 1;
		} else {
			assert(power_writes == 0 && power_confirmed == 0);
		}
		assert(!kit_opens);
		if (!power_fail_at) {
			assert(mountroot_hook);
			mountroot_hook(0);
			assert(kit_opens == (power_confirmed != 0));
			assert(children == 1 && !errors && device.sc_input);
			if (i == 0) {
				const uint16_t cmds[] = { 0x0402, 0x0411 };
				uint8_t packet[64], args[12] = { 0 };
				for (size_t j = 0; j < __arraycount(cmds); j++) {
					size_t n = aicwf_command_encode(packet, sizeof(packet),
					    cmds[j], args, j == 0 ? 8 : 12);
					bool attempted = true;
					assert(radio.write(radio.cookie, packet, n,
					    &attempted) == EOPNOTSUPP && !attempted);
				}
			}
		}
		assert(uaicfw_detach(0, 0) == 0 && !opens && !allocs);
		hub.ud_myhub = &root;
		hub.desc.idProduct = 0x8091;
		hubport.up_portno = 1;
		wlan.desc.bcdDevice = 0x0100;
		bus.ub_usbctl = 3;
		chip_flags = 1U << 25;
		memset(field_values, 0, sizeof(field_values));
	}
	power_writes = power_fail_at = condition_reads = chip_flags = 0;
	target_board = false;
}

/* Each table has a little-endian byte length followed by whole words. */
static void
kit_table_checks(void)
{
	uint8_t packed[12 + 11 * 8] = "AICDCU02";
	le16enc(packed + 8, 1);
	le16enc(packed + 10, 11);
	for (size_t pos = 12; pos < sizeof(packed); pos += 8)
		le32enc(packed + pos, 4);
	assert(uaicfw_kit_tables(packed, sizeof(packed)));
	for (size_t size = 0; size < sizeof(packed); size++)
		assert(!uaicfw_kit_tables(packed, size));
	le32enc(packed + 12, 0);
	assert(!uaicfw_kit_tables(packed, sizeof(packed)));
	le32enc(packed + 12, UINT32_MAX - 3);
	assert(!uaicfw_kit_tables(packed, sizeof(packed)));
	le32enc(packed + 12, 3);
	assert(!uaicfw_kit_tables(packed, sizeof(packed)));
}

int
main(int argc, char **argv)
{
	kit_table_checks();
	assert(argc == 1 || argc == 2);
	if (argc == 2) {
		struct uaicfw_kit kit = { 0 };
		kit_dir = argv[1];
		int error = uaicfw_kit_prepare(&kit);
		assert(!kit_handles && !kit_allocations);
		if (error) {
			assert(!kit.release && !kit.rows && !kit.wlan_size);
			fprintf(stderr, "kit rejected: error %d after %u opens\n",
			    error, kit_opens);
			return 1;
		}
		assert(kit.wlan_size && kit_opens == 6);
		printf("%s %zu %u\n", kit.release, kit.wlan_size, kit.rows);
		return 0;
	}
	survey_queue_checks();
	struct usbd_interface iface = {
	    {0, 4}, {{1, 2, 512}, {0x81, 2, 512}, {2, 2, 512}, {0x82, 2, 512}}};
	struct usbif_attach_arg a = {
	    0xa69c, 0x88dc, 1, 0xff, 0xff, 0xff, &iface, NULL, 1, 2};
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
		versions = children = 0;
		config.bNumInterface = fault == WRONG_FAMILY ? 1 : 3;
		memset(&device, 0, sizeof(device));
		uaicfw_attach(0, 0, &a);
		bool have_id = fault == NONE || fault == VERSION_TIMEOUT ||
		    fault == VERSION_SHORT || fault == ON_DATA || fault == EARLY_REPLY || fault >= WRONG_FAMILY;
		assert(identified == have_id);
		bool running = fault == NONE || fault == ON_DATA || fault == EARLY_REPLY || fault >= WRONG_FAMILY;
		assert(versions == running);
		assert(errors == (!running || fault == WRONG_FAMILY));
		if (have_id && !running)
			assert(strstr(last_error, "MM_VERSION failed"));
		if (fault == VERSION_TIMEOUT)
			assert(strstr(last_error, "timeout"));
		if (fault == WRONG_FAMILY)
			assert(strstr(last_error, "unsupported WLAN"));
		if (!running || fault == WRONG_FAMILY)
			assert(device.sc_stopping && !opens && !allocs);
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
			ethernet_checks();
			security_checks();
			radio.listen(radio.cookie, ignore_input, NULL, NULL);
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
			uint8_t p[AICWF_HDR_LEN + AICWF_TXDESC_LEN] = { 0 };
			bool attempted = true;
			p[AICWF_HDR_LEN + 20] = 0x88; /* EAPOL can open the first pipe. */
			p[AICWF_HDR_LEN + 21] = 0x8e;
			le16enc(p, AICWF_TXDESC_LEN);
			p[2] = AICWF_TYPE_DATA;
			stopping_on_alloc = true;
			unsigned before = data_writes;
			assert(radio.data(radio.cookie, UAICFW_ETHERNET, p, sizeof(p), NULL, &attempted)
			    == ENXIO && !attempted && data_writes == before);
			stopping_on_alloc = false;
			assert(device.sc_data.xfer != NULL && !device.sc_data_reply);
			/* The disconnect interleave ended. Exercise IN recovery too. */
			device.sc_stopping = false;
			radio.listen(radio.cookie, ignore_input, NULL, NULL);
			stall_input(inputs[1]);
			clear_error = true;
			run_usb_task(&device.sc_recover_task);
			clear_error = false;
			assert(device.sc_error == USBD_IOERROR && !inputs[1]->pending);
		} else if (children && fault == ON_DATA) {
			radio.listen(radio.cookie, ignore_input, NULL, NULL);
			stall_input(inputs[1]);
			assert(device.sc_recover_task.pending);
		}
		assert(uaicfw_detach(0, 0) == 0 && !opens && !allocs);
	}
	mountroot_failure_checks(&a);
	condition_checks(&a);
	power_checks(a);
	command_deadline_checks(&a);
	writes = errors = 0;
	iface.eps[2].bEndpointAddress = 1;
	assert(uaicfw_match(0, 0, &a) == UMATCH_NONE);
	memset(&device, 0, sizeof(device));
	uaicfw_attach(0, 0, &a);
	assert(uaicfw_detach(0, 0) == 0);
	assert(!writes && errors == 1);
	puts("PASS: both bulk-IN endpoints, early and late FullMAC replies, "
	     "DC clock/PMU setup, records, Ethernet, SAE, protected SA Query, "
	     "TX tokens and cleanup");
	return 0;
}
