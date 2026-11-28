/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * AIC8800DC USB WLAN transport. The second bulk OUT pipe carries commands.
 * Both bulk IN pipes can deliver firmware messages.
 * Hardware facts: Luckfox SDK 6dcbca0d8a51, aic8800_fdrv/aicwf_usb.c
 * and aicwf_compat_8800dc.c.
 */
#include "opt_fdt.h"

#include <sys/param.h>
#include <sys/atomic.h>
#include <sys/bus.h>
#include <sys/condvar.h>
#include <sys/device.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/kmem.h>
#include <sys/mutex.h>
#include <sys/systm.h>

#include <net/if_ether.h>

#ifdef FDT
#include <sys/sha2.h>
#include <dev/firmload.h>
#include <dev/fdt/fdtvar.h>
#include <dev/ofw/openfirm.h>
#endif

#include <dev/ic/aicwf_command.h>
#include <dev/ic/aicwf_sta.h>
#include <dev/ic/aicwf_sae.h>
#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdevs.h>
#include <dev/usb/uaicfwvar.h>
#ifdef FDT
#include <dev/usb/usbdivar.h>
#endif

#define	UAICFW_IDENT_REG	0x40500000
#define	UAICFW_SUBID_REG	0x00000020
#define	UAICFW_U02_RF_MISC	0x00120ebc	/* normal-mode RF status on Lyra */
#define	UAICFW_U02_LMAC_ADDR	0x00150000	/* RAM_LMAC_FW_ADDR */
#define	UAICFW_PARAM_MAX	1032	/* includes a 1024-byte block write */
#define	UAICFW_TX_SIZE		(AICWF_TX_MSG_OFFSET + AICWF_MSG_HDR_LEN + \
				 UAICFW_PARAM_MAX)
#define	UAICFW_RX_SIZE		2048
#define	UAICFW_PAYLOAD_MAX	0xfff	/* DC USB record length field */
#define	UAICFW_TIMEOUT_MS	500
#define	UAICFW_CALIB_TIMEOUT_MS	4000	/* RWNX_80211_CMD_TIMEOUT_MS */
#define	UAICFW_MM_SET_RF_CONFIG_REQ	0x0067
#define	UAICFW_MM_SET_RF_CALIB_REQ	0x0069
#define	UAICFW_MM_SET_TXPWR_LVL_REQ	0x0077

struct uaicfw_pipe {
	struct usbd_pipe *pipe;
	struct usbd_xfer *xfer;
};

/* Only a full IN transfer can leave a record for the next transfer. */
struct uaicfw_rx_stream {
	size_t pending;
	uint64_t serial;
	bool deliver;
	uint8_t buf[UAICFW_PAYLOAD_MAX + AICWF_RXHDR_LEN + UAICFW_RX_SIZE];
};

struct uaicfw_endpoints {
	uint8_t in[2];
	uint8_t out[2];
};

struct uaicfw_reply_size {
	uint16_t declared, wire;
};

struct uaicfw_request {
	uint16_t id;
	size_t len;
	usbd_status status;
	bool optional;
	uint16_t *got;	/* accept and report a length up to len */
	struct uaicfw_reply_size *size;
};

struct uaicfw_data_reply {
	uint32_t token;
	usbd_status status;
};

struct uaicfw_softc {
	kmutex_t sc_cmd_lock;	/* serialize command writers */
	kmutex_t sc_rx_lock;	/* synchronize the sender with both IN pipes */
	kcondvar_t sc_cv;
	struct usbd_device *sc_udev;
	struct usb_task sc_recover_task;
	unsigned int sc_stalled, sc_retried;
	uaicfw_input_fn sc_input;
	uaicfw_serial_fn sc_serial;
	void *sc_input_arg;
	struct aicwf_version sc_fw_version;
	struct uaicfw_pipe sc_tx;
	struct usbd_interface *sc_iface;
	uint8_t sc_data_ep;
	struct uaicfw_pipe sc_data;
	uint32_t sc_data_sequence;
	struct uaicfw_data_reply *sc_data_reply; /* protected by sc_rx_lock */
	struct uaicfw_pipe sc_rx[2];
	struct uaicfw_rx_stream sc_stream[2];
	struct uaicfw_request *sc_request;
	uint8_t sc_reply[UAICFW_PARAM_MAX];
	usbd_status sc_error;
	bool sc_stopping;
	bool sc_station_ready;	/* patch, DPD, stack and RF are prepared */
#ifdef FDT
	bool sc_lyra_power_confirmed;
#endif
};

static int uaicfw_match(device_t, cfdata_t, void *);
static void uaicfw_attach(device_t, device_t, void *);
static int uaicfw_detach(device_t, int);

CFATTACH_DECL_NEW(uaicfw, sizeof(struct uaicfw_softc), uaicfw_match,
    uaicfw_attach, uaicfw_detach, NULL);

/* The first bulk endpoint per direction carries data, the second commands. */
static bool
uaicfw_endpoints(struct usbd_interface *iface, struct uaicfw_endpoints *eps)
{
	const usb_interface_descriptor_t *desc =
	    usbd_get_interface_descriptor(iface);
	uint8_t epaddr[2][2];
	unsigned int count[2] = { 0, 0 };

	if (desc == NULL || desc->bAlternateSetting != 0)
		return false;
	for (u_int i = 0; i < desc->bNumEndpoints; i++) {
		const usb_endpoint_descriptor_t *ep =
		    usbd_interface2endpoint_descriptor(iface, i);
		if (ep == NULL)
			return false;
		if (UE_GET_XFERTYPE(ep->bmAttributes) != UE_BULK)
			continue;
		uint8_t addr = ep->bEndpointAddress;
		unsigned int dir = UE_GET_DIR(addr) == UE_DIR_IN;
		if (UE_GET_ADDR(addr) == 0 || UGETW(ep->wMaxPacketSize) == 0 ||
		    count[dir] == 2)
			return false;
		epaddr[dir][count[dir]++] = addr;
	}
	for (unsigned int dir = 0; dir < 2; dir++)
		if (count[dir] != 2 || epaddr[dir][0] == epaddr[dir][1])
			return false;
	memcpy(eps->out, epaddr[0], sizeof(eps->out));
	memcpy(eps->in, epaddr[1], sizeof(eps->in));
	return true;
}

static int
uaicfw_match(device_t parent, cfdata_t cf, void *aux)
{
	const struct usbif_attach_arg * const uiaa = aux;
	struct uaicfw_endpoints eps;

	if (uiaa->uiaa_vendor == USB_VENDOR_AICSEMI &&
	    uiaa->uiaa_product == USB_PRODUCT_AICSEMI_AIC8800DC &&
	    uiaa->uiaa_configno == 1 && uiaa->uiaa_class == UICLASS_VENDOR &&
	    uiaa->uiaa_subclass == 0xff && uiaa->uiaa_proto == 0xff &&
	    uaicfw_endpoints(uiaa->uiaa_iface, &eps))
		return UMATCH_VENDOR_PRODUCT_CONF_IFACE;
	return UMATCH_NONE;
}

static usbd_status
uaicfw_open(struct usbd_interface *iface, uint8_t addr, size_t len,
    struct uaicfw_pipe *p)
{
	usbd_status status;
	int error;

	status = usbd_open_pipe(iface, addr, USBD_EXCLUSIVE_USE, &p->pipe);
	if (status != USBD_NORMAL_COMPLETION)
		return status;
	const int flags = UE_GET_DIR(addr) == UE_DIR_IN ?
	    USBD_SHORT_XFER_OK : USBD_FORCE_SHORT_XFER;
	error = usbd_create_xfer(p->pipe, len, flags, 0, &p->xfer);
	if (error != 0) {
		usbd_close_pipe(p->pipe);
		p->pipe = NULL;
		return error == ENOMEM ? USBD_NOMEM : USBD_IOERROR;
	}
	return USBD_NORMAL_COMPLETION;
}

static void
uaicfw_close(struct uaicfw_pipe *p)
{
	if (p->xfer != NULL)
		usbd_destroy_xfer(p->xfer);
	if (p->pipe != NULL)
		usbd_close_pipe(p->pipe);
	p->pipe = NULL;
	p->xfer = NULL;
}

static void uaicfw_rxeof(struct usbd_xfer *, void *, usbd_status);

static usbd_status
uaicfw_rx_submit(struct uaicfw_softc *sc, struct usbd_xfer *xfer)
{
	usbd_setup_xfer(xfer, sc, usbd_get_buffer(xfer), UAICFW_RX_SIZE,
	    USBD_SHORT_XFER_OK, USBD_NO_TIMEOUT, uaicfw_rxeof);
	return usbd_transfer(xfer);
}

/*
 * CLEAR_FEATURE(ENDPOINT_HALT) needs a sleepable context. Rearm only the
 * affected WLAN pipe. A second stall before any successful IN transfer
 * is terminal. The host sends each OUT command at most once.
 */
static void
uaicfw_recover_rx(void *arg)
{
	struct uaicfw_softc * const sc = arg;
	usbd_status status;

	for (size_t i = 0; i < __arraycount(sc->sc_rx); i++) {
		const unsigned int bit = 1U << i;
		mutex_enter(&sc->sc_rx_lock);
		if (sc->sc_stopping || sc->sc_error != USBD_NORMAL_COMPLETION ||
		    (sc->sc_stalled & bit) == 0) {
			mutex_exit(&sc->sc_rx_lock);
			continue;
		}
		sc->sc_stalled &= ~bit;
		mutex_exit(&sc->sc_rx_lock);
		status = usbd_clear_endpoint_stall(sc->sc_rx[i].pipe);
		if (status == USBD_NORMAL_COMPLETION)
			status = uaicfw_rx_submit(sc, sc->sc_rx[i].xfer);
		if (status != USBD_IN_PROGRESS && status != USBD_NORMAL_COMPLETION) {
			mutex_enter(&sc->sc_rx_lock);
			sc->sc_error = status;
			cv_broadcast(&sc->sc_cv);
			mutex_exit(&sc->sc_rx_lock);
			return;
		}
	}
}

/* Retire only the current firmware token. USB completion is independent. */
static bool
uaicfw_data_confirm(struct uaicfw_softc *sc, const uint8_t *rec,
    size_t raw)
{
	struct uaicfw_data_reply * const reply = sc->sc_data_reply;
	uint32_t status, token;

	if ((rec[2] & 0x7f) != AICWF_TYPE_TX_CFM || raw != AICWF_HDR_LEN + 8 ||
	    !aicwf_sta_cfm_decode(rec + AICWF_HDR_LEN, 8, &status, &token) ||
	    (token & AICWF_DC_DATA_TOKEN) == 0)
		return false;
	if (reply != NULL && reply->token == token && status != 0 &&
	    reply->status == USBD_IN_PROGRESS) {
		reply->status = (status & AICWF_STA_CFM_ACK) != 0 ?
		    USBD_NORMAL_COMPLETION : USBD_IOERROR;
		cv_broadcast(&sc->sc_cv);
	}
	return true;
}

/* DC USB pads each configuration record to four bytes. DATA has no pad. */
static size_t
uaicfw_record_size(const uint8_t *p, size_t payload, size_t *raw)
{
	if (p[2] & AICWF_TYPE_CFG) {
		*raw = AICWF_HDR_LEN + payload;
		return roundup(*raw, 4);
	}
	*raw = AICWF_RXHDR_LEN + payload;
	return *raw;
}

/* Validate the complete records before notifying either consumer. */
static usbd_status
uaicfw_receive(struct uaicfw_softc *sc, struct uaicfw_rx_stream *stream,
    const uint8_t *p, size_t len)
{
	static const uint8_t zeros[AICWF_HDR_LEN];
	struct uaicfw_request *req = sc->sc_request;
	const uint8_t *answer = NULL;
	bool bad_sample = false;
	const bool full = len == UAICFW_RX_SIZE;
	size_t end = 0, tail = 0;

	KASSERT(mutex_owned(&sc->sc_rx_lock));
	if (len > UAICFW_RX_SIZE || stream->pending > sizeof(stream->buf) - len)
		return USBD_INVAL;
	const uint64_t serial = sc->sc_serial == NULL ? 0 :
	    sc->sc_serial(sc->sc_input_arg);
	if (stream->pending != 0) {
		memcpy(stream->buf + stream->pending, p, len);
		len += stream->pending;
		p = stream->buf;
	}
	while (end < len) {
		const uint8_t *rec = p + end;
		const size_t left = len - end;
		size_t raw, wire;

		if (left < AICWF_HDR_LEN) {
			if (full)
				tail = left;
			else if (memcmp(rec, zeros, left) != 0)
				return USBD_INVAL;
			break;
		}
		const size_t payload = le16dec(rec);
		if (payload == 0)
			break;		/* End of the transfer. */
		if (payload > UAICFW_PAYLOAD_MAX)
			return USBD_INVAL;
		wire = uaicfw_record_size(rec, payload, &raw);
		if (wire > left && full) {
			tail = left;
			break;
		}
		if (raw > left)
			return USBD_INVAL;
		end += wire < left ? wire : left;
		if ((rec[2] & 0x7f) != AICWF_TYPE_MSG)
			continue;
		const uint8_t *msg = rec + AICWF_HDR_LEN;
		uint16_t id, param_len;
		if (!aicwf_reply_decode(msg, payload, &id, &param_len))
			return USBD_INVAL;
		if (id == AICWF_DBG_ERROR_IND) {
			const uint8_t *e = msg + AICWF_RX_MSG_PARAM;
			printf("uaicfw: firmware error indication, %u bytes:"
			    " %08x %08x\n", param_len,
			    param_len >= 4 ? le32dec(e) : 0,
			    param_len >= 8 ? le32dec(e + 4) : 0);
			return USBD_IOERROR;
		}
		if (req == NULL || req->id != id)
			continue;
		if (req->size != NULL) {
			req->size->declared = param_len;
			req->size->wire = payload - AICWF_RX_MSG_PARAM;
			if (req->size->wire != req->len)
				return USBD_INVAL;
		}
		if (answer != NULL || req->status != USBD_IN_PROGRESS ||
		    (req->got != NULL ? param_len > req->len :
		    param_len != req->len)) {
			if (!req->optional)
				return USBD_INVAL;
			/* This complete record belongs to the optional sample. */
			bad_sample = true;
			continue;
		}
		answer = msg + AICWF_RX_MSG_PARAM;
	}
	if (bad_sample) {
		req->status = USBD_INVAL;
		cv_broadcast(&sc->sc_cv);
	} else if (answer != NULL) {
		const uint16_t got = req->got != NULL ?
		    le16dec(answer - AICWF_RX_MSG_PARAM + 6) : req->len;
		memset(sc->sc_reply, 0, req->len);
		if (got != 0)
			memcpy(sc->sc_reply, answer, got);
		if (req->got != NULL)
			*req->got = got;
		req->status = USBD_NORMAL_COMPLETION;
		cv_broadcast(&sc->sc_cv);
	}
	if (sc->sc_input != NULL || sc->sc_data_reply != NULL) {
		for (size_t pos = 0; pos < end;) {
			const bool continued = pos == 0 && stream->pending != 0;
			const uint8_t *rec = p + pos;
			size_t raw;
			pos += uaicfw_record_size(rec, le16dec(rec), &raw);
			if (uaicfw_data_confirm(sc, rec, raw))
				continue;
			if (sc->sc_input != NULL && (!continued || stream->deliver))
				sc->sc_input(sc->sc_input_arg, rec, raw,
				    continued ? stream->serial : serial);
		}
	}
	if (tail != 0) {
		if (end != 0 || stream->pending == 0) {
			stream->serial = serial;
			stream->deliver = sc->sc_input != NULL;
		}
		memmove(stream->buf, p + end, tail);
	}
	stream->pending = tail;
	return USBD_NORMAL_COMPLETION;
}

static void
uaicfw_rxeof(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct uaicfw_softc * const sc = priv;
	const unsigned int index = xfer == sc->sc_rx[0].xfer ? 0 : 1;
	const unsigned int bit = 1U << index;
	struct uaicfw_rx_stream *stream = &sc->sc_stream[index];
	uint32_t actual;

	mutex_enter(&sc->sc_rx_lock);
	if (sc->sc_stopping || sc->sc_error != USBD_NORMAL_COMPLETION) {
		mutex_exit(&sc->sc_rx_lock);
		return;
	}
	if (status == USBD_STALLED && sc->sc_input != NULL &&
	    stream->pending == 0 && (sc->sc_retried & bit) == 0) {
		sc->sc_retried |= bit;
		sc->sc_stalled |= bit;
		usb_add_task(sc->sc_udev, &sc->sc_recover_task, USB_TASKQ_DRIVER);
		mutex_exit(&sc->sc_rx_lock);
		return;
	}
	if (status == USBD_NORMAL_COMPLETION) {
		sc->sc_retried &= ~bit;
		usbd_get_xfer_status(xfer, NULL, NULL, &actual, NULL);
		status = uaicfw_receive(sc, stream, usbd_get_buffer(xfer), actual);
	}
	if (status != USBD_NORMAL_COMPLETION) {
		if (status != USBD_CANCELLED)
			printf("uaicfw: receive pipe %u stopped: %s\n", index,
			    usbd_errstr(status));
		sc->sc_error = status;
		cv_broadcast(&sc->sc_cv);
	}
	mutex_exit(&sc->sc_rx_lock);
	if (status != USBD_NORMAL_COMPLETION)
		return;
	status = uaicfw_rx_submit(sc, xfer);
	if (status != USBD_IN_PROGRESS && status != USBD_NORMAL_COMPLETION) {
		mutex_enter(&sc->sc_rx_lock);
		sc->sc_error = status;
		cv_broadcast(&sc->sc_cv);
		mutex_exit(&sc->sc_rx_lock);
	}
}

/* Deduct USB submission time from the confirmation wait. */
static usbd_status
uaicfw_wait_reply(struct uaicfw_softc *sc, const usbd_status *reply,
    uint32_t start, unsigned int timeout_ms)
{
	const uint32_t timeout = mstohz(timeout_ms);
	int error = 0;

	KASSERT(mutex_owned(&sc->sc_rx_lock));
	while (!sc->sc_stopping && sc->sc_error == USBD_NORMAL_COMPLETION &&
	    *reply == USBD_IN_PROGRESS) {
		const uint32_t elapsed = (uint32_t)getticks() - start;
		if (error == EWOULDBLOCK || elapsed >= timeout)
			return USBD_TIMEOUT;
		error = cv_timedwait(&sc->sc_cv, &sc->sc_rx_lock,
		    timeout - elapsed);
	}
	if (sc->sc_stopping)
		return USBD_CANCELLED;
	if (sc->sc_error != USBD_NORMAL_COMPLETION)
		return sc->sc_error;
	return *reply;
}

static void
uaicfw_clear_out_stall(struct uaicfw_softc *sc, struct uaicfw_pipe *pipe)
{
	usbd_status error = usbd_clear_endpoint_stall(pipe->pipe);

	if (error != USBD_NORMAL_COMPLETION && pipe == &sc->sc_tx) {
		mutex_enter(&sc->sc_rx_lock);
		sc->sc_error = error;
		cv_broadcast(&sc->sc_cv);
		mutex_exit(&sc->sc_rx_lock);
	}
}

static usbd_status
uaicfw_command_wait(struct uaicfw_softc *sc, uint16_t cmd, const void *param,
    size_t request_len, uint16_t cfm, void *reply, size_t reply_len,
    bool optional, struct uaicfw_reply_size *size, unsigned int timeout_ms,
    uint16_t *got)
{
	struct uaicfw_request req = { cfm, reply_len, USBD_IN_PROGRESS,
	    optional, got, size };
	struct usbd_xfer *xfer;
	uint32_t actual, start;
	uint8_t *buf;
	size_t len;
	usbd_status status;

	if (reply_len > sizeof(sc->sc_reply))
		return USBD_INVAL;
	mutex_enter(&sc->sc_cmd_lock);
	mutex_enter(&sc->sc_rx_lock);
	status = sc->sc_stopping ? USBD_CANCELLED : sc->sc_error;
	if (status != USBD_NORMAL_COMPLETION)
		goto done;
	xfer = sc->sc_tx.xfer;
	buf = usbd_get_buffer(xfer);
	len = aicwf_command_encode(buf, UAICFW_TX_SIZE, cmd, param, request_len);
	if (len == 0) {
		status = USBD_INVAL;
		goto done;
	}
	sc->sc_request = &req;
	mutex_exit(&sc->sc_rx_lock);

	usbd_setup_xfer(xfer, NULL, buf, len, USBD_FORCE_SHORT_XFER,
	    UAICFW_TIMEOUT_MS, NULL);
	start = getticks();
	status = usbd_sync_transfer(xfer);
	if (optional && status == USBD_STALLED)
		uaicfw_clear_out_stall(sc, &sc->sc_tx);
	if (status == USBD_NORMAL_COMPLETION) {
		usbd_get_xfer_status(xfer, NULL, NULL, &actual, NULL);
		if (actual != len)
			status = USBD_SHORT_XFER;
	}
	/* A failed OUT transaction differs from a missing read confirmation. */
	const bool out_failed = status != USBD_NORMAL_COMPLETION &&
	    status != USBD_STALLED;
	mutex_enter(&sc->sc_rx_lock);
	if (sc->sc_stopping)
		status = USBD_CANCELLED;
	else if (sc->sc_error != USBD_NORMAL_COMPLETION)
		status = sc->sc_error;
	else if (status == USBD_NORMAL_COMPLETION)
		status = uaicfw_wait_reply(sc, &req.status, start, timeout_ms);
	/*
	 * Optional reads stop at the first failure. The station has a
	 * different set of command IDs. Keep genuine RX pipe errors.
	 */
	if (status != USBD_NORMAL_COMPLETION) {
		if (!optional || out_failed)
			sc->sc_error = status;
	} else if (reply_len != 0)
		memcpy(reply, sc->sc_reply, reply_len);
	sc->sc_request = NULL;
done:
	mutex_exit(&sc->sc_rx_lock);
	mutex_exit(&sc->sc_cmd_lock);
	return status;
}

static usbd_status
uaicfw_command(struct uaicfw_softc *sc, uint16_t cmd, const void *param,
    size_t request_len, uint16_t cfm, void *reply, size_t reply_len,
    bool optional, struct uaicfw_reply_size *size)
{
	return uaicfw_command_wait(sc, cmd, param, request_len, cfm, reply,
	    reply_len, optional, size, UAICFW_TIMEOUT_MS, NULL);
}

/* These are the only readable fields. No caller supplies a device address. */
enum uaicfw_field {
	UAICFW_CHIP, UAICFW_SUBID, UAICFW_USB_CONFIG, UAICFW_XTAL, UAICFW_BBPLL,
	UAICFW_CLOCK_SELECT, UAICFW_U02_RF_MISC_PTR, UAICFW_DPD_MASK0,
	UAICFW_DPD_MASK1, UAICFW_DPD_MASK2, UAICFW_DPD_RESERVED,
	UAICFW_U02_WIFISETTING_PTR, UAICFW_U02_LDPC_PTR, UAICFW_U02_AGC_PTR,
	UAICFW_U02_TXGAIN_PTR, UAICFW_U02_LMAC_ENTRY
};

static usbd_status
uaicfw_read_field(struct uaicfw_softc *sc, enum uaicfw_field field,
    uint32_t *value)
{
	static const uint32_t regs[] = {
		[UAICFW_CHIP] = UAICFW_IDENT_REG,
		[UAICFW_SUBID] = UAICFW_SUBID_REG,
		[UAICFW_USB_CONFIG] = 0x40200024,
		[UAICFW_XTAL] = 0x40500148,
		[UAICFW_BBPLL] = 0x40505010,
		[UAICFW_CLOCK_SELECT] = 0x40500010,
		/* Normal-mode RF configuration, rkwifibt 223f2479. */
		[UAICFW_U02_RF_MISC_PTR] = 0x00010164 + 0x14,
		[UAICFW_DPD_MASK0] = UAICFW_U02_RF_MISC,
		[UAICFW_DPD_MASK1] = UAICFW_U02_RF_MISC + 4,
		[UAICFW_DPD_MASK2] = UAICFW_U02_RF_MISC + 8,
		[UAICFW_DPD_RESERVED] = UAICFW_U02_RF_MISC + 12,
		/* aicwf_patch_config_8800dc: normal-mode setting pointers. */
		[UAICFW_U02_WIFISETTING_PTR] = 0x00010164,
		[UAICFW_U02_LDPC_PTR] = 0x00010164 + 0x8,
		[UAICFW_U02_AGC_PTR] = 0x00010164 + 0xc,
		[UAICFW_U02_TXGAIN_PTR] = 0x00010164 + 0x10,
		/* start_from_bootrom: RAM_LMAC_FW_ADDR. */
		[UAICFW_U02_LMAC_ENTRY] = UAICFW_U02_LMAC_ADDR,
	};
	uint8_t req[4], cfm[8];
	usbd_status status;

	if ((unsigned)field >= __arraycount(regs))
		return USBD_INVAL;
	le32enc(req, regs[field]);
	status = uaicfw_command(sc, AICWF_DBG_MEM_READ_REQ, req, sizeof(req),
	    AICWF_DBG_MEM_READ_CFM, cfm, sizeof(cfm), field > UAICFW_SUBID, NULL);
	if (status == USBD_NORMAL_COMPLETION &&
	    !aicwf_read_confirm(cfm, sizeof(cfm), regs[field], value))
		return USBD_INVAL;
	return status;
}

#ifdef FDT
/* Sample only the normal-mode header reported by this USB U02 revision. */
static const char *
uaicfw_u02_dpd_marker(struct uaicfw_softc *sc)
{
	static const enum uaicfw_field fields[] = {
		UAICFW_DPD_MASK0, UAICFW_DPD_MASK1, UAICFW_DPD_MASK2,
		UAICFW_DPD_RESERVED
	};
	uint32_t base, words[__arraycount(fields)];

	if (uaicfw_read_field(sc, UAICFW_U02_RF_MISC_PTR, &base) !=
	    USBD_NORMAL_COMPLETION || base != UAICFW_U02_RF_MISC)
		return "unknown";
	for (size_t i = 0; i < __arraycount(fields); i++) {
		if (uaicfw_read_field(sc, fields[i], &words[i]) !=
		    USBD_NORMAL_COMPLETION)
			return "unknown";
	}
	/* rkwifibt: aicwf_misc_ram_valid_check_8800dc, normal station mode. */
	if (words[0] == 0 && (words[1] & 0xfff00000U) == 0x80000000U &&
	    words[2] == 0 && (words[3] & 0xffffff00U) == 0)
		return "present";
	return "absent";
}

enum uaicfw_asset {
#define U02_FILE(id, name)	U02_##id,
#define U02_RELEASE(label, digest)
#include "uaicfw_kit.def"
#undef U02_RELEASE
#undef U02_FILE
	U02_NASSETS
};

/* Summarize the release only after all file snapshots match. */
struct uaicfw_kit {
	size_t wlan_size;
	const char *release;
	unsigned rows;
	void *buf[U02_NASSETS];
	size_t size[U02_NASSETS];
};

static const char * const uaicfw_assets[] = {
#define U02_FILE(id, name)	[U02_##id] = name,
#define U02_RELEASE(label, digest)
#include "uaicfw_kit.def"
#undef U02_RELEASE
#undef U02_FILE
};

static const struct {
	const char *label, *digest;
} uaicfw_releases[] = {
#define U02_FILE(id, name)
#define U02_RELEASE(label, digest)	{ label, digest },
#include "uaicfw_kit.def"
#undef U02_RELEASE
#undef U02_FILE
};

static void
uaicfw_kit_release(struct uaicfw_kit *kit)
{
	for (size_t i = 0; i < __arraycount(kit->buf); i++) {
		if (kit->buf[i] != NULL)
			firmware_free(kit->buf[i], kit->size[i]);
		kit->buf[i] = NULL;
	}
}

static bool
uaicfw_kit_tables(const uint8_t *buf, size_t size)
{
	size_t pos = 12;

	if (size < pos || memcmp(buf, "AICDCU02", 8) != 0 ||
	    le16dec(buf + 8) != 1 || le16dec(buf + 10) != 11)
		return false;
	for (unsigned i = 0; i < 11; i++) {
		if (size - pos < 4)
			return false;
		const uint32_t len = le32dec(buf + pos);
		pos += 4;
		if (!len || len % 4 != 0 || len > size - pos)
			return false;
		pos += len;
	}
	return pos == size;
}

static int
uaicfw_kit_prepare(struct uaicfw_kit *kit)
{
	static const uint8_t domain[] = "AIC8800DC-U02-STATION-KIT\0\1";
	static const char digits[] = "0123456789abcdef";
	uint8_t digest[SHA256_DIGEST_LENGTH], length[8];
	char hex[2 * SHA256_DIGEST_LENGTH + 1];
	SHA256_CTX sum, file;
	struct uaicfw_kit result = { 0 };
	int error;

	SHA256_Init(&sum);
	SHA256_Update(&sum, domain, sizeof(domain) - 1);
	for (size_t i = 0; i < __arraycount(uaicfw_assets); i++) {
		const char *name = uaicfw_assets[i];
		firmware_handle_t fh;
		void *buf;
		size_t size;

		error = firmware_open("aic8800dc", name, &fh);
		if (error) {
			uaicfw_kit_release(&result);
			return error;
		}
		error = firmware_read_alloc(fh, 40284, &buf, &size);
		const int close_error = firmware_close(fh);
		if (error) {
			uaicfw_kit_release(&result);
			return error;
		}
		error = close_error;
		if (!error && i == U02_TABLES && !uaicfw_kit_tables(buf, size))
			error = EFTYPE;
		if (!error && i == U02_PATCH_TABLE) {
			/* The factory table starts with a 128-byte descriptor. */
			if (size <= 128 || (size - 128) % 8 != 0)
				error = EFTYPE;
			else
				result.rows = (size - 128) / 8;
		}
		if (error) {
			firmware_free(buf, size);
			uaicfw_kit_release(&result);
			return error;
		}
		result.buf[i] = buf;
		result.size[i] = size;

		SHA256_Init(&file);
		SHA256_Update(&file, buf, size);
		SHA256_Final(digest, &file);
		be64enc(length, size);
		SHA256_Update(&sum, (const uint8_t *)name, strlen(name) + 1);
		SHA256_Update(&sum, length, sizeof(length));
		SHA256_Update(&sum, digest, sizeof(digest));
		if (i == U02_WLAN)
			result.wlan_size = size;
	}
	SHA256_Final(digest, &sum);
	for (size_t i = 0; i < sizeof(digest); i++) {
		hex[2 * i] = digits[digest[i] >> 4];
		hex[2 * i + 1] = digits[digest[i] & 15];
	}
	hex[sizeof(hex) - 1] = '\0';
	for (size_t i = 0; i < __arraycount(uaicfw_releases); i++) {
		if (strcmp(hex, uaicfw_releases[i].digest) == 0) {
			result.release = uaicfw_releases[i].label;
			*kit = result;
			return 0;
		}
	}
	uaicfw_kit_release(&result);
	return EFTYPE;
}

/* The board has one soldered WLAN function behind its CH334F on OTG1. */
static bool
uaicfw_lyra_onboard(struct usbd_device *dev)
{
	if (dev == NULL || dev->ud_myhub == NULL || dev->ud_bus == NULL ||
	    !dev->ud_bus->ub_usbctl)
		return false;
	struct usbd_device *hub = dev->ud_myhub;
	const usb_device_descriptor_t *dd = usbd_get_device_descriptor(hub);
	if (hub->ud_myhub != dev->ud_bus->ub_roothub || hub->ud_myhub == NULL ||
	    hub->ud_powersrc == NULL || hub->ud_powersrc->up_portno != 1 ||
	    UGETW(dd->idVendor) != 0x1a86 || UGETW(dd->idProduct) != 0x8091)
		return false;
	const device_t hc = device_parent(dev->ud_bus->ub_usbctl);
	if (!hc || devhandle_type(device_handle(hc)) != DEVHANDLE_TYPE_OF)
		return false;
	return devhandle_to_of(device_handle(hc)) ==
	    OF_finddevice("/soc/usb@ff780000");
}

/*
 * One physical radio per matched board. Hold this interlock across detach
 * if a command failed: the device might already have applied the change.
 */
static unsigned int uaicfw_lyra_power_busy;

/* The non-H DCDC sequence in rkwifibt 223f2479, with the extended PMU mask. */
static usbd_status
uaicfw_lyra_power(device_t self, struct uaicfw_softc *sc)
{
	static const struct {
		uint32_t reg, mask, value, update;
	} steps[] = {
		{ 0x40500010, UINT32_MAX, 4, 0 },
		{ 0x40500010, UINT32_MAX, 6, 0 },
		{ 0x7000216c, 0x0000000c, 0x00000004, 0 },
		{ 0x700021bc, 0x0000000c, 0x00000004, 0 },
		{ 0x70002118, 0x000000f0, 0x000000a0, 0x80 },
		{ 0x70002104, 0x0000007f, 0x00000042, 0x40 },
		{ 0x7000210c, 0x0000007f, 0x00000042, 0x40 },
		{ 0x70002170, 0x0000000f, 0x00000001, 0 },
		{ 0x70002190, 0x0000003f, 0x00000018, 0 },
		{ 0x700021cc, 0x000000f0, 0x00000000, 0 },
		{ 0x700010a0, 0x00000800, 0x00000800, 0 },
		{ 0x70001034, 0x1c100000, 0x08000000, 0 },
		{ 0x70001038, 0x00000100, 0x00000100, 0 },
		{ 0x70001094, 0x0000000c, 0x00000000, 0 },
		{ 0x700021d0, 0x00000060, 0x00000060, 0 },
		{ 0x70001000, 0x00508101, 0x00108101, 0 }, /* extended MCU mask */
		{ 0x70001028, 0x0000003c, 0x00000004, 0 },
	};
	uint8_t req[12], reply[8];

	if (atomic_cas_uint(&uaicfw_lyra_power_busy, 0, 1) != 0)
		return USBD_IOERROR;
	for (unsigned int i = 0; i < __arraycount(steps); i++) {
		/* Linux lmac_msg.h: DBG_MEM_WRITE and DBG_MEM_MASK_WRITE. */
		const bool masked = steps[i].mask != UINT32_MAX;
		const uint16_t cmd = masked ? 0x0411 : 0x0402;
		const size_t len = masked ? 12 : 8;
		le32enc(req, steps[i].reg);
		if (masked)
			le32enc(req + 4, steps[i].mask);
		le32enc(req + len - 4, steps[i].value);
		struct uaicfw_reply_size observed = { UINT16_MAX, UINT16_MAX };
		usbd_status status = uaicfw_command(sc, cmd, req, len, cmd + 1,
		    reply, sizeof(reply), false, &observed);
		if (status == USBD_NORMAL_COMPLETION) {
			uint32_t value;
			if (!aicwf_read_confirm(reply, sizeof(reply), steps[i].reg,
			    &value)) {
				aprint_error_dev(self, "clock/PMU step %u: "
				    "register echo mismatch\n", i + 1);
				return USBD_INVAL;
			}
			/* DENOM_UPDATE requests a latch. Check the divider setting. */
			const uint32_t check = steps[i].mask & ~steps[i].update;
			const uint32_t different = (value ^ steps[i].value) & check;
			if (different != 0) {
				aprint_error_dev(self, "clock/PMU step %u: "
				    "selected bits differ by %#x\n", i + 1, different);
				return USBD_INVAL;
			}
		}
		if (status != USBD_NORMAL_COMPLETION) {
			if (observed.declared != UINT16_MAX &&
			    (observed.declared != sizeof(reply) ||
			    observed.wire != sizeof(reply)))
				aprint_error_dev(self, "clock/PMU step %u: "
				    "confirmation length %u/%u, expected 8/8\n",
				    i + 1, observed.declared, observed.wire);
			else
				aprint_error_dev(self, "clock/PMU step %u failed: %s\n",
				    i + 1, usbd_errstr(status));
			return status;
		}
	}
	atomic_store_relaxed(&uaicfw_lyra_power_busy, 0);
	sc->sc_lyra_power_confirmed = true;
	aprint_normal_dev(self, "clock/PMU sequence confirmed\n");
	return USBD_NORMAL_COMPLETION;
}

/*
 * Luckfox rkwifibt 223f2479: aicwf_usb_config and set_bbpll_config.
 * system_config_8800dc samples the clock selector before the PMU sequence.
 * Report the initial clock selection and factory PMU mask variant. Stop
 * these optional reads on the first missing confirmation.
 */
static usbd_status
uaicfw_lyra_conditions(device_t self, struct uaicfw_softc *sc,
    const struct usbif_attach_arg *a, const struct uaicfw_endpoints *eps,
    uint32_t chip, bool onboard)
{
	const char *usb = "unknown", *pll = "unknown";
	const char *clock_select = "unknown";
	bool usb_skip = false, local_crystal = false, initial = false;
	uint32_t val;

	if (a->uiaa_vendor != USB_VENDOR_AICSEMI ||
	    a->uiaa_product != USB_PRODUCT_AICSEMI_AIC8800DC ||
	    a->uiaa_configno != 1 || a->uiaa_ifaceno != 2 || a->uiaa_port != 1 ||
	    a->uiaa_class != UICLASS_VENDOR || a->uiaa_subclass != 0xff ||
	    a->uiaa_proto != 0xff || sc->sc_fw_version.features != 0xb011fa5f ||
	    eps->in[0] != 0x81 || eps->in[1] != 0x82 ||
	    eps->out[0] != 1 || eps->out[1] != 2)
		return USBD_NORMAL_COMPLETION;
	const usb_interface_descriptor_t *iface =
	    usbd_get_interface_descriptor(a->uiaa_iface);
	if (iface == NULL || iface->bNumEndpoints != 4)
		return USBD_NORMAL_COMPLETION;
	for (unsigned i = 0; i < iface->bNumEndpoints; i++) {
		const usb_endpoint_descriptor_t *ep =
		    usbd_interface2endpoint_descriptor(a->uiaa_iface, i);
		if (ep == NULL || UGETW(ep->wMaxPacketSize) != 512)
			return USBD_NORMAL_COMPLETION;
	}
	if (uaicfw_read_field(sc, UAICFW_USB_CONFIG, &val) !=
	    USBD_NORMAL_COMPLETION)
		goto done;
	usb_skip = (val & 0xffff) != 0x0119;
	usb = usb_skip ? "skip" : "needed";
	if (uaicfw_read_field(sc, UAICFW_XTAL, &val) !=
	    USBD_NORMAL_COMPLETION)
		goto done;
	if ((val & 1) == 0) {
		local_crystal = true;
		pll = "skip (no CPU crystal)";
	} else {
		pll = "unknown (CPU crystal)";
		if (uaicfw_read_field(sc, UAICFW_BBPLL, &val) !=
		    USBD_NORMAL_COMPLETION)
			goto done;
		pll = (val >> 29) == 3 ?
		    "skip (CPU crystal, setting present)" : "needed (CPU crystal)";
	}
	if (uaicfw_read_field(sc, UAICFW_CLOCK_SELECT, &val) ==
	    USBD_NORMAL_COMPLETION) {
		switch (val) {
		case 0:
			initial = true;
			clock_select = "zero";
			break;
		case 4:
			clock_select = "factory first";
			break;
		case 6:
			clock_select = "factory final";
			break;
		default:
			clock_select = "other";
			break;
		}
	}
done:
	/* Identity bit 25 selects the extended factory PMU mask. */
	aprint_normal_dev(self, "factory USB setup %s, BBPLL setup %s, "
	    "clock select %s, PMU mask %s\n", usb, pll, clock_select,
	    (chip & (1U << 25)) ? "extended" : "base");
	if (onboard && usb_skip && local_crystal && initial &&
	    (chip & (1U << 25)) &&
	    UGETW(usbd_get_device_descriptor(a->uiaa_device)->bcdDevice) == 0x0100)
		return uaicfw_lyra_power(self, sc);
	return USBD_NORMAL_COMPLETION;
}

/*
 * Normal-mode U02 ROM patch and DPD preparation before the first
 * MM_SET_STACK_START: rkwifibt 223f2479, rwnx_plat_patch_load with
 * CONFIG_FORCE_DPD_CALIB, aicwf_plat_patch_load_8800dc,
 * aicwf_patch_config_8800dc and aicwf_dpd_calib_8800dc.
 */
#define	UAICFW_DBG_MEM_WRITE_REQ	0x0402
#define	UAICFW_DBG_MEM_WRITE_CFM	0x0403
#define	UAICFW_DBG_MEM_BLOCK_WRITE_REQ	0x040b
#define	UAICFW_DBG_MEM_BLOCK_WRITE_CFM	0x040c
#define	UAICFW_DBG_START_APP_REQ	0x040d
#define	UAICFW_DBG_START_APP_CFM	0x040e
#define	UAICFW_START_APP_FNCALL		4
#define	UAICFW_START_APP_DUMMY		5
#define	UAICFW_DBG_MEM_MASK_WRITE_REQ	0x0411
#define	UAICFW_DBG_MEM_MASK_WRITE_CFM	0x0412
#define	UAICFW_U02_WDT_REG		0x4010300c	/* wdt_tbl_masked */
#define	UAICFW_BLOCK_MAX		512	/* dbg_mem_block_write_req */
#define	UAICFW_U02_PATCH_ADDR		0x00180000	/* ROM_FMAC_PATCH_ADDR */
#define	UAICFW_U02_CALIB_ADDR		0x00130000	/* ROM_FMAC_CALIB_ADDR */
#define	UAICFW_U02_CALIB_ENTRY		0x00130009
#define	UAICFW_U02_TBL_DESC		128	/* patch table descriptor */

enum uaicfw_u02_table {
	U02T_WIFISETTING = 3, U02T_LDPC = 4, U02T_AGC = 5, U02T_TXGAIN = 6
};

struct uaicfw_u02_load {
	uint8_t *blk;
	unsigned blocks, empty, rejected, rows;
	uint32_t first_error;
};

static usbd_status
uaicfw_mem_write(struct uaicfw_softc *sc, uint32_t addr, uint32_t value)
{
	uint8_t req[8], cfm[8];
	uint32_t echo;
	usbd_status status;

	le32enc(req, addr);
	le32enc(req + 4, value);
	status = uaicfw_command(sc, UAICFW_DBG_MEM_WRITE_REQ, req, sizeof(req),
	    UAICFW_DBG_MEM_WRITE_CFM, cfm, sizeof(cfm), false, NULL);
	if (status == USBD_NORMAL_COMPLETION &&
	    !aicwf_read_confirm(cfm, sizeof(cfm), addr, &echo))
		return USBD_INVAL;
	return status;
}

/* rwnx_plat_bin_fw_upload_2: 512-byte blocks in a zero-filled request. */
static usbd_status
uaicfw_mem_block_write(struct uaicfw_softc *sc, struct uaicfw_u02_load *ld,
    uint32_t addr, const uint8_t *data, size_t len)
{
	uint8_t cfm[4];
	uint16_t got;
	usbd_status status;

	for (size_t off = 0; off < len; off += UAICFW_BLOCK_MAX) {
		const size_t n = MIN(UAICFW_BLOCK_MAX, len - off);
		memset(ld->blk, 0, 8 + UAICFW_BLOCK_MAX);
		le32enc(ld->blk, addr + off);
		le32enc(ld->blk + 4, n);
		memcpy(ld->blk + 8, data + off, n);
		status = uaicfw_command_wait(sc, UAICFW_DBG_MEM_BLOCK_WRITE_REQ,
		    ld->blk, 8 + UAICFW_BLOCK_MAX, UAICFW_DBG_MEM_BLOCK_WRITE_CFM,
		    cfm, sizeof(cfm), false, NULL, UAICFW_TIMEOUT_MS, &got);
		if (status != USBD_NORMAL_COMPLETION)
			return status;
		ld->blocks++;
		/* dbg_mem_block_write_cfm carries wstatus. The factory ignores it. */
		if (got == 0) {
			ld->empty++;
		} else if (got != sizeof(cfm) || le32dec(cfm) != 0) {
			if (ld->rejected++ == 0)
				ld->first_error = addr + off;
		}
	}
	return USBD_NORMAL_COMPLETION;
}

static const uint8_t *
uaicfw_u02_table(const struct uaicfw_kit *kit, unsigned index, size_t *len)
{
	const uint8_t * const buf = kit->buf[U02_TABLES];
	size_t pos = 12;

	/* uaicfw_kit_tables already checked the layout. */
	for (unsigned i = 0; i < index; i++)
		pos += 4 + le32dec(buf + pos);
	*len = le32dec(buf + pos);
	return buf + pos + 4;
}

static bool
uaicfw_u02_ram(uint32_t addr)
{
	return addr >= 0x00100000 && addr < 0x00200000 && (addr & 3) == 0;
}

/*
 * aicwf_set_rf_config_8800dc in normal mode: TX power levels from the
 * board file, the four gain tables and the 2.4 GHz calibration. The kit
 * board file disables power offsets, so no MM_SET_TXPWR_OFST is sent.
 */
#define	UAICFW_MM_SET_RF_CONFIG_CFM	0x0068
#define	UAICFW_MM_SET_RF_CALIB_CFM	0x006a
#define	UAICFW_MM_SET_TXPWR_LVL_CFM	0x0078
#define	UAICFW_TXPWR_LEVELS		34
#define	UAICFW_TXPWR_LVL_LEN		95	/* mm_set_txpwr_lvl_req union */
#define	UAICFW_RF_CONFIG_LEN		260	/* mm_set_rf_config_req */

static int
uaicfw_u02_levels(const struct uaicfw_kit *kit,
    uint8_t levels[UAICFW_TXPWR_LVL_LEN])
{
	const uint8_t *p = kit->buf[U02_USER];
	const uint8_t * const end = p + kit->size[U02_USER];
	unsigned count = 0;
	bool enabled = false;

	memset(levels, 0, UAICFW_TXPWR_LVL_LEN);
	while (p < end) {
		const uint8_t *line = memchr(p, '\n', end - p);
		if (line == NULL)
			line = end;
		const size_t n = line - p;
		if (n >= 8 && memcmp(p, "enable=1", 8) == 0)
			enabled = true;
		if (n > 4 && memcmp(p, "lvl_", 4) == 0) {
			const uint8_t *eq = memchr(p, '=', n);
			unsigned value = 0;

			if (eq == NULL || eq + 1 == line ||
			    count == UAICFW_TXPWR_LEVELS)
				return EFTYPE;
			for (const uint8_t *d = eq + 1; d < line; d++) {
				if (*d == '\r' && d + 1 == line)
					break;
				if (*d < '0' || *d > '9' || value > 12)
					return EFTYPE;
				value = value * 10 + *d - '0';
			}
			if (value > 127)
				return EFTYPE;
			levels[++count] = value;
		}
		p = line == end ? end : line + 1;
	}
	if (!enabled || count != UAICFW_TXPWR_LEVELS)
		return EFTYPE;
	levels[0] = 1;
	return 0;
}

static usbd_status
uaicfw_u02_rf(device_t self, struct uaicfw_softc *sc,
    const struct uaicfw_kit *kit)
{
	static const struct {
		unsigned table;
		uint8_t select, offset;
	} gains[] = {
		{ 7, 1, 0 }, { 8, 1, 16 },	/* TX gain, two halves */
		{ 9, 0, 0 }, { 10, 0, 32 },	/* RX gain, 20 and 40 MHz */
	};
	uint8_t levels[UAICFW_TXPWR_LVL_LEN], cfm[16], calib[24];
	uint16_t got;
	uint8_t *req;
	usbd_status status;

	if (uaicfw_u02_levels(kit, levels) != 0) {
		aprint_error_dev(self, "U02 board file has no TX power levels\n");
		return USBD_INVAL;
	}
	status = uaicfw_command_wait(sc, UAICFW_MM_SET_TXPWR_LVL_REQ, levels,
	    sizeof(levels), UAICFW_MM_SET_TXPWR_LVL_CFM, cfm, sizeof(cfm), false,
	    NULL, UAICFW_TIMEOUT_MS, &got);
	if (status != USBD_NORMAL_COMPLETION)
		return status;
	req = kmem_alloc(UAICFW_RF_CONFIG_LEN, KM_SLEEP);
	for (size_t i = 0; i < __arraycount(gains); i++) {
		size_t len;
		const uint8_t * const t = uaicfw_u02_table(kit, gains[i].table,
		    &len);

		memset(req, 0, UAICFW_RF_CONFIG_LEN);
		req[0] = gains[i].select;
		req[1] = gains[i].offset;
		req[2] = 16;
		memcpy(req + 4, t, MIN(len, UAICFW_RF_CONFIG_LEN - 4));
		status = uaicfw_command_wait(sc, UAICFW_MM_SET_RF_CONFIG_REQ,
		    req, UAICFW_RF_CONFIG_LEN, UAICFW_MM_SET_RF_CONFIG_CFM, cfm,
		    sizeof(cfm), false, NULL, UAICFW_TIMEOUT_MS, &got);
		if (status != USBD_NORMAL_COMPLETION)
			break;
	}
	kmem_free(req, UAICFW_RF_CONFIG_LEN);
	if (status != USBD_NORMAL_COMPLETION)
		return status;
	/* Normal-mode 2.4 GHz calibration; the factory reply is empty. */
	memset(calib, 0, sizeof(calib));
	le32enc(calib, 0x0f8f);
	le32enc(calib + 8, 0x0c34c008);
	le32enc(calib + 16, 0x264203);
	status = uaicfw_command_wait(sc, UAICFW_MM_SET_RF_CALIB_REQ, calib,
	    sizeof(calib), UAICFW_MM_SET_RF_CALIB_CFM, cfm, sizeof(cfm), false,
	    NULL, UAICFW_CALIB_TIMEOUT_MS, &got);
	if (status == USBD_NORMAL_COMPLETION)
		aprint_normal_dev(self, "RF calibrated: %u TX power levels, "
		    "4 gain tables\n", UAICFW_TXPWR_LEVELS);
	return status;
}

/* An administrator keeps the passive survey with an aic8800dc/passive file. */
static bool
uaicfw_u02_passive_requested(void)
{
	firmware_handle_t fh;

	if (firmware_open("aic8800dc", "passive", &fh) != 0)
		return false;
	(void)firmware_close(fh);
	return true;
}

static usbd_status
uaicfw_u02_prepare(device_t self, struct uaicfw_softc *sc,
    const struct uaicfw_kit *kit)
{
	static const enum uaicfw_field ptrs[] = {
		UAICFW_U02_WIFISETTING_PTR, UAICFW_U02_LDPC_PTR,
		UAICFW_U02_AGC_PTR, UAICFW_U02_TXGAIN_PTR
	};
	struct uaicfw_u02_load ld = { 0 };
	const char *phase = "WLAN patch";
	uint32_t base[__arraycount(ptrs)];
	const uint8_t *p;
	size_t len;
	usbd_status status;

	ld.blk = kmem_alloc(8 + UAICFW_BLOCK_MAX, KM_SLEEP);
	status = uaicfw_mem_block_write(sc, &ld, UAICFW_U02_PATCH_ADDR,
	    kit->buf[U02_WLAN], kit->size[U02_WLAN]);
	if (status != USBD_NORMAL_COMPLETION)
		goto out;

	phase = "setting pointers";
	for (size_t i = 0; i < __arraycount(ptrs); i++) {
		status = uaicfw_read_field(sc, ptrs[i], &base[i]);
		if (status != USBD_NORMAL_COMPLETION)
			goto out;
		if (!uaicfw_u02_ram(base[i])) {
			aprint_error_dev(self, "U02 setting pointer %zu: %#x\n",
			    i, base[i]);
			status = USBD_INVAL;
			goto out;
		}
	}
	phase = "WLAN settings";
	p = uaicfw_u02_table(kit, U02T_WIFISETTING, &len);
	for (size_t i = 0; i + 8 <= len; i += 8) {
		status = uaicfw_mem_write(sc, base[0] + le32dec(p + i),
		    le32dec(p + i + 4));
		if (status != USBD_NORMAL_COMPLETION)
			goto out;
	}
	phase = "LDPC table";
	p = uaicfw_u02_table(kit, U02T_LDPC, &len);
	if ((status = uaicfw_mem_block_write(sc, &ld, base[1], p, len)) !=
	    USBD_NORMAL_COMPLETION)
		goto out;
	phase = "AGC table";
	p = uaicfw_u02_table(kit, U02T_AGC, &len);
	if ((status = uaicfw_mem_block_write(sc, &ld, base[2], p, len)) !=
	    USBD_NORMAL_COMPLETION)
		goto out;
	phase = "TX gain map";
	p = uaicfw_u02_table(kit, U02T_TXGAIN, &len);
	if ((status = uaicfw_mem_block_write(sc, &ld, base[3], p, len)) !=
	    USBD_NORMAL_COMPLETION)
		goto out;

	/* aicwf_patch_table_load: the descriptor, then address/value rows. */
	phase = "patch table";
	p = kit->buf[U02_PATCH_TABLE];
	len = kit->size[U02_PATCH_TABLE];
	if (!uaicfw_u02_ram(le32dec(p))) {
		status = USBD_INVAL;
		goto out;
	}
	if ((status = uaicfw_mem_block_write(sc, &ld, le32dec(p), p,
	    UAICFW_U02_TBL_DESC)) != USBD_NORMAL_COMPLETION)
		goto out;
	for (size_t i = UAICFW_U02_TBL_DESC; i + 8 <= len; i += 8) {
		status = uaicfw_mem_write(sc, le32dec(p + i), le32dec(p + i + 4));
		if (status != USBD_NORMAL_COMPLETION)
			goto out;
		ld.rows++;
	}
	aprint_normal_dev(self, "U02 patch applied: %u blocks, %u rejected, "
	    "%u/%u table rows\n", ld.blocks, ld.rejected, ld.rows, kit->rows);
	if (ld.empty != 0)
		aprint_normal_dev(self, "%u block writes had no status word\n",
		    ld.empty);
	if (ld.rejected != 0) {
		aprint_error_dev(self, "first rejected block at %#x\n",
		    ld.first_error);
		status = USBD_INVAL;
		goto out;
	}

	/* aicwf_dpd_calib_8800dc: calibrate unless the RF misc RAM is valid. */
	phase = "DPD calibration";
	const char *marker = uaicfw_u02_dpd_marker(sc);
	if (strcmp(marker, "absent") == 0) {
		uint8_t req[8], cfm[4];
		uint16_t got;

		ld.blocks = 0;
		if ((status = uaicfw_mem_block_write(sc, &ld,
		    UAICFW_U02_CALIB_ADDR, kit->buf[U02_CALIB],
		    kit->size[U02_CALIB])) != USBD_NORMAL_COMPLETION)
			goto out;
		if (ld.rejected != 0) {
			status = USBD_INVAL;
			goto out;
		}
		le32enc(req, UAICFW_U02_CALIB_ENTRY);
		le32enc(req + 4, UAICFW_START_APP_FNCALL);
		status = uaicfw_command_wait(sc, UAICFW_DBG_START_APP_REQ, req,
		    sizeof(req), UAICFW_DBG_START_APP_CFM, cfm, sizeof(cfm),
		    false, NULL, UAICFW_CALIB_TIMEOUT_MS, &got);
		if (status != USBD_NORMAL_COMPLETION)
			goto out;
		marker = uaicfw_u02_dpd_marker(sc);
		if (got == sizeof(cfm))
			aprint_normal_dev(self, "DPD calibration program "
			    "returned %u, marker %s\n", le32dec(cfm), marker);
		else
			aprint_normal_dev(self, "DPD calibration program "
			    "finished, marker %s\n", marker);
	} else {
		aprint_normal_dev(self, "DPD marker %s, calibration skipped\n",
		    marker);
	}
	if (strcmp(marker, "present") != 0) {
		status = USBD_INVAL;
		goto out;
	}

	/*
	 * CONFIG_START_FROM_BOOTROM: wdt_config, then start_from_bootrom
	 * hands the normal-mode boot ROM to the patched FullMAC.
	 */
	phase = "boot ROM handoff";
	{
		uint8_t req[12], cfm[8];
		uint32_t value;
		uint16_t got;

		le32enc(req, UAICFW_U02_WDT_REG);
		le32enc(req + 4, 1);
		le32enc(req + 8, 1);
		status = uaicfw_command(sc, UAICFW_DBG_MEM_MASK_WRITE_REQ, req,
		    sizeof(req), UAICFW_DBG_MEM_MASK_WRITE_CFM, cfm, sizeof(cfm),
		    false, NULL);
		if (status != USBD_NORMAL_COMPLETION)
			goto out;
		/* The watchdog control does not read back the written bit. */
		if (!aicwf_read_confirm(cfm, sizeof(cfm), UAICFW_U02_WDT_REG,
		    &value)) {
			aprint_error_dev(self, "watchdog echo %#x %#x\n",
			    le32dec(cfm), le32dec(cfm + 4));
			status = USBD_INVAL;
			goto out;
		}
		status = uaicfw_read_field(sc, UAICFW_U02_LMAC_ENTRY, &value);
		if (status != USBD_NORMAL_COMPLETION)
			goto out;
		le32enc(req, UAICFW_U02_LMAC_ADDR);
		le32enc(req + 4, UAICFW_START_APP_DUMMY);
		status = uaicfw_command_wait(sc, UAICFW_DBG_START_APP_REQ, req, 8,
		    UAICFW_DBG_START_APP_CFM, cfm, 4, false, NULL,
		    UAICFW_CALIB_TIMEOUT_MS, &got);
		if (status != USBD_NORMAL_COMPLETION)
			goto out;
		aprint_normal_dev(self, "boot ROM handed over: entry word %#x, "
		    "%s %u\n", value, got == 4 ? "status" : "reply bytes",
		    got == 4 ? le32dec(cfm) : got);
	}

	/* rwnx_main.c: start the FullMAC stack with defaults. */
	phase = "MM_SET_STACK_START";
	{
		const uint8_t req[4] = { 1, 0, 0, 0 };
		uint8_t cfm[2];

		status = uaicfw_command_wait(sc, AICWF_MM_SET_STACK_START_REQ,
		    req, sizeof(req), AICWF_MM_SET_STACK_START_CFM, cfm,
		    sizeof(cfm), false, NULL, UAICFW_CALIB_TIMEOUT_MS, NULL);
		if (status == USBD_NORMAL_COMPLETION)
			aprint_normal_dev(self, "FullMAC stack started: 5 GHz %u, "
			    "vendor 0x%02x\n", cfm[0], cfm[1]);
	}
	if (status == USBD_NORMAL_COMPLETION) {
		phase = "RF configuration";
		status = uaicfw_u02_rf(self, sc, kit);
	}
	if (status == USBD_NORMAL_COMPLETION)
		sc->sc_station_ready = true;
out:
	if (status != USBD_NORMAL_COMPLETION) {
		aprint_error_dev(self, "U02 %s failed: %s\n", phase,
		    usbd_errstr(status));
		/* A partly prepared radio cannot serve the passive survey. */
		if (ld.blocks != 0) {
			mutex_enter(&sc->sc_rx_lock);
			if (sc->sc_error == USBD_NORMAL_COMPLETION)
				sc->sc_error = status;
			mutex_exit(&sc->sc_rx_lock);
		}
	}
	kmem_free(ld.blk, 8 + UAICFW_BLOCK_MAX);
	return status;
}
#endif

static void
uaicfw_stop(struct uaicfw_softc *sc)
{
	mutex_enter(&sc->sc_rx_lock);
	sc->sc_stopping = true;
	cv_broadcast(&sc->sc_cv);
	mutex_exit(&sc->sc_rx_lock);

	/* Completion of the halt-clear task precedes destruction of its pipes. */
	usb_rem_task_wait(sc->sc_udev, &sc->sc_recover_task, USB_TASKQ_DRIVER, NULL);
	/* Suspend waits for callbacks and prevents a resubmit during detach. */
	for (size_t i = 0; i < __arraycount(sc->sc_rx); i++)
		if (sc->sc_rx[i].pipe != NULL)
			usbd_suspend_pipe(sc->sc_rx[i].pipe);
	if (sc->sc_tx.pipe != NULL)
		usbd_suspend_pipe(sc->sc_tx.pipe);
	mutex_enter(&sc->sc_cmd_lock);
	if (sc->sc_data.pipe != NULL)
		usbd_suspend_pipe(sc->sc_data.pipe);
	uaicfw_close(&sc->sc_data);
	uaicfw_close(&sc->sc_tx);
	for (size_t i = 0; i < __arraycount(sc->sc_rx); i++)
		uaicfw_close(&sc->sc_rx[i]);
	mutex_exit(&sc->sc_cmd_lock);
}

static int
uaicfw_errno(usbd_status status)
{
	switch (status) {
	case USBD_NORMAL_COMPLETION:
		return 0;
	case USBD_TIMEOUT:
		return ETIMEDOUT;
	case USBD_CANCELLED:
		return ENXIO;
	case USBD_NOMEM:
		return ENOMEM;
	case USBD_INVAL:
		return EPROTO;
	default:
		return EIO;
	}
}

static void
uaicfw_listen(void *arg, uaicfw_input_fn fn, uaicfw_serial_fn serial,
    void *cookie)
{
	struct uaicfw_softc * const sc = arg;
	mutex_enter(&sc->sc_rx_lock);
	/* Partial records belong to the listener that saw their first byte. */
	for (size_t i = 0; i < __arraycount(sc->sc_stream); i++)
		sc->sc_stream[i].deliver = false;
	sc->sc_input = fn;
	sc->sc_serial = serial;
	sc->sc_input_arg = cookie;
	mutex_exit(&sc->sc_rx_lock);
}

/* Accept the four station operations available before RF initialization. */
static bool
uaicfw_survey_command(const uint8_t *packet, size_t len)
{
	const size_t hdr = AICWF_TX_MSG_OFFSET + AICWF_MSG_HDR_LEN;
	uint16_t freqs[11];
	uint8_t expected[AICWF_STA_SCAN_LEN];

	if (len < hdr || packet[2] != AICWF_TYPE_MSG || packet[3] != 0 ||
	    le16dec(packet) != len - AICWF_HDR_LEN ||
	    le16dec(packet + AICWF_TX_MSG_OFFSET + 6) != len - hdr)
		return false;
	const uint16_t id = le16dec(packet + AICWF_TX_MSG_OFFSET);
	const uint8_t *p = packet + hdr;
	if (le16dec(packet + AICWF_TX_MSG_OFFSET + 2) != id >> 10 ||
	    le16dec(packet + AICWF_TX_MSG_OFFSET + 4) != AICWF_TASK_DRIVER)
		return false;
	switch (id) {
	case AICWF_MM_GET_MAC_ADDR_REQ:
		return len == hdr + 4 && le32dec(p) == 1;
	case AICWF_MM_ADD_IF_REQ:
		return len == hdr + 10 && le16dec(p) == 0 && le16dec(p + 8) == 0 &&
		    (p[2] & 1) == 0 && memcmp(p + 2, (uint8_t[6]){ 0 }, 6) != 0;
	case AICWF_MM_REMOVE_IF_REQ:
		return len == hdr + 1 && p[0] < 4;
	case AICWF_SCANU_START_REQ:
		if (len != hdr + AICWF_STA_SCAN_LEN || p[AICWF_STA_SCAN_VIF] >= 4)
			return false;
		unsigned count = p[AICWF_STA_SCAN_COUNT];
		if (count == 0 || count > __arraycount(freqs))
			return false;
		for (unsigned i = 0; i < count; i++) {
			freqs[i] = le16dec(p + i * 6);
			if (freqs[i] < 2412 || freqs[i] > 2462 ||
			    (freqs[i] - 2412) % 5 != 0 ||
			    (i > 0 && freqs[i] <= freqs[i - 1]))
				return false;
		}
		(void)aicwf_sta_scan_build(expected, freqs, count, p[AICWF_STA_SCAN_VIF], 0);
		return memcmp(p, expected, sizeof(expected)) == 0;
	default:
		return false;
	}
}

/*
 * A prepared radio accepts the station's MM, ME, SCANU and SM messages.
 * The debug task and the RF and stack setup stay with the transport.
 */
static bool
uaicfw_station_command(const uint8_t *packet, size_t len)
{
	const size_t hdr = AICWF_TX_MSG_OFFSET + AICWF_MSG_HDR_LEN;

	if (len < hdr || len > UAICFW_TX_SIZE || packet[2] != AICWF_TYPE_MSG ||
	    packet[3] != 0 || le16dec(packet) != len - AICWF_HDR_LEN ||
	    le16dec(packet + AICWF_TX_MSG_OFFSET + 6) != len - hdr)
		return false;
	const uint16_t id = le16dec(packet + AICWF_TX_MSG_OFFSET);
	if (le16dec(packet + AICWF_TX_MSG_OFFSET + 2) != id >> 10 ||
	    le16dec(packet + AICWF_TX_MSG_OFFSET + 4) != AICWF_TASK_DRIVER)
		return false;
	switch (id >> 10) {
	case 0:			/* MM */
		return id != UAICFW_MM_SET_RF_CONFIG_REQ &&
		    id != UAICFW_MM_SET_RF_CALIB_REQ &&
		    id != UAICFW_MM_SET_TXPWR_LVL_REQ &&
		    id != AICWF_MM_SET_STACK_START_REQ;
	case 4:			/* SCANU */
	case 5:			/* ME */
	case 6:			/* SM */
		return true;
	default:
		return false;
	}
}

/* Serialize the reusable OUT transfer with sc_cmd_lock. */
static usbd_status
uaicfw_send(struct uaicfw_softc *sc, struct uaicfw_pipe *p, size_t len,
    bool *attempted)
{
	struct usbd_xfer * const xfer = p->xfer;
	usbd_status status;
	uint32_t actual;

	usbd_setup_xfer(xfer, NULL, usbd_get_buffer(xfer), len,
	    USBD_FORCE_SHORT_XFER, UAICFW_TIMEOUT_MS, NULL);
	if (attempted != NULL)
		*attempted = true;
	status = usbd_sync_transfer(xfer);
	if (status == USBD_STALLED) {
		uaicfw_clear_out_stall(sc, p);
	} else if (status == USBD_NORMAL_COMPLETION) {
		usbd_get_xfer_status(xfer, NULL, NULL, &actual, NULL);
		if (actual != len)
			status = USBD_SHORT_XFER;
	}
	return status;
}

static usbd_status
uaicfw_out_state(struct uaicfw_softc *sc)
{
	usbd_status status;

	mutex_enter(&sc->sc_rx_lock);
	status = sc->sc_stopping ? USBD_CANCELLED : sc->sc_error;
	mutex_exit(&sc->sc_rx_lock);
	return status;
}

static int
uaicfw_write(void *arg, const uint8_t *packet, size_t len, bool *attempted)
{
	struct uaicfw_softc * const sc = arg;
	usbd_status status;

	if (attempted != NULL)
		*attempted = false;
	if (!(sc->sc_station_ready ? uaicfw_station_command(packet, len) :
	    uaicfw_survey_command(packet, len)))
		return EOPNOTSUPP;
	mutex_enter(&sc->sc_cmd_lock);
	status = uaicfw_out_state(sc);
	if (status == USBD_NORMAL_COMPLETION) {
		memcpy(usbd_get_buffer(sc->sc_tx.xfer), packet, len);
		status = uaicfw_send(sc, &sc->sc_tx, len, attempted);
	}
	mutex_exit(&sc->sc_cmd_lock);
	return uaicfw_errno(status);
}

/* The station supplies the bounded frame and its firmware descriptor. */
static int
uaicfw_data(void *arg, enum uaicfw_data_kind kind, const uint8_t *packet,
    size_t len, const struct uaicfw_port *port, bool *attempted)
{
	struct uaicfw_softc * const sc = arg;
	struct uaicfw_data_reply reply = { .status = USBD_IN_PROGRESS };
	const size_t hdr = AICWF_HDR_LEN + AICWF_TXDESC_LEN;
	const uint8_t *desc;
	uint8_t *buf;
	size_t wire;
	uint32_t start;
	usbd_status status;

	if (attempted != NULL)
		*attempted = false;
	if (len < hdr || len > UAICFW_RX_SIZE ||
	    (kind == UAICFW_ETHERNET && len - hdr > ETHERMTU))
		return EMSGSIZE;
	desc = packet + AICWF_HDR_LEN;
	if (packet[2] != AICWF_TYPE_DATA ||
	    le16dec(packet) != len - AICWF_HDR_LEN || le16dec(desc) != len - hdr ||
	    le16dec(desc + 2) != 0 || le32dec(desc + 4) != 0)
		return EINVAL;
	const uint16_t flags = le16dec(desc + 26);
	bool confirm;
	switch (kind) {
	case UAICFW_ETHERNET:
		if (flags != 0)
			return EINVAL;
		confirm = be16dec(desc + 20) == ETHERTYPE_PAE;
		break;
	case UAICFW_SAE_AUTH:
		if (flags != AICWF_TX_MGMT || le16dec(desc + 20) != 0 ||
		    desc[25] != 0xff ||
		    !aicwf_sae_frame_valid(packet + hdr, len - hdr, desc + 8,
		    desc + 14, desc + 8) || le16dec(packet + hdr + 26) < 1 ||
		    le16dec(packet + hdr + 26) > 2)
			return EINVAL;
		confirm = false;
		break;
	case UAICFW_SA_QUERY:
		if (flags != (AICWF_TX_MGMT | AICWF_TX_MGMT_ROBUST) ||
		    le16dec(desc + 20) != 0 || desc[25] == 0xff ||
		    !aicwf_pmf_query_valid(packet + hdr, len - hdr, desc + 8,
		    desc + 14, desc + 8))
			return EINVAL;
		confirm = true;
		break;
	default:
		return EOPNOTSUPP;
	}
	/* EAPOL can establish the first controlled-port generation. */
	if (port == NULL) {
		if (kind != UAICFW_ETHERNET || !confirm)
			return EACCES;
	} else if (port->current == NULL || port->serial == 0) {
		return EACCES;
	}
	wire = confirm ? (len + 3) & ~(size_t)3 : len;
	KASSERT(wire <= UAICFW_RX_SIZE);

	mutex_enter(&sc->sc_cmd_lock);
	status = uaicfw_out_state(sc);
	if (status != USBD_NORMAL_COMPLETION)
		goto out;
	if (sc->sc_data.xfer == NULL) {
		status = uaicfw_open(sc->sc_iface, sc->sc_data_ep,
		    UAICFW_RX_SIZE, &sc->sc_data);
		if (status == USBD_NORMAL_COMPLETION)
			status = uaicfw_out_state(sc);
		if (status != USBD_NORMAL_COMPLETION)
			goto out;
	}
	/* Already admitted OUT transfers can complete across a key change.
	 * Recheck after every sleep preceding submission of protected data. */
	if (port != NULL && port->current(port->cookie, kind) != port->serial) {
		mutex_exit(&sc->sc_cmd_lock);
		return EACCES;
	}
	buf = usbd_get_buffer(sc->sc_data.xfer);
	memcpy(buf, packet, len);
	le16enc(buf, wire);
	buf[3] = 0;
	if (confirm) {
		/* Never reuse a token after a timeout or uncertain submission. */
		if (sc->sc_data_sequence == AICWF_DC_DATA_TOKEN - 1) {
			mutex_exit(&sc->sc_cmd_lock);
			return EOVERFLOW;
		}
		reply.token = AICWF_DC_DATA_TOKEN | ++sc->sc_data_sequence;
		le32enc(buf + AICWF_HDR_LEN + 4,
		    AICWF_STA_CFM_REQUEST | reply.token);
		memset(buf + len, 0, wire - len);
		mutex_enter(&sc->sc_rx_lock);
		sc->sc_data_reply = &reply;
		mutex_exit(&sc->sc_rx_lock);
	}
	start = getticks();
	status = uaicfw_send(sc, &sc->sc_data, wire, attempted);
	if (confirm) {
		mutex_enter(&sc->sc_rx_lock);
		if (status == USBD_NORMAL_COMPLETION)
			status = uaicfw_wait_reply(sc, &reply.status, start,
			    UAICFW_TIMEOUT_MS);
		sc->sc_data_reply = NULL;
		mutex_exit(&sc->sc_rx_lock);
	}
out:
	mutex_exit(&sc->sc_cmd_lock);
	return uaicfw_errno(status);
}

static void
uaicfw_mountroot(device_t self)
{
	struct uaicfw_softc * const sc = device_private(self);
	struct uaicfw_attach_args aa = {
		.cookie = sc, .write = uaicfw_write, .data = uaicfw_data,
		.listen = uaicfw_listen,
	};
#ifdef FDT
	if (sc->sc_lyra_power_confirmed &&
	    uaicfw_out_state(sc) == USBD_NORMAL_COMPLETION) {
		struct uaicfw_kit kit = { 0 };
		const int error = uaicfw_kit_prepare(&kit);
		if (error) {
			aprint_normal_dev(self, "station files unavailable (error %d)\n",
			    error);
		} else {
			aprint_normal_dev(self, "station files verified: %s, "
			    "WLAN file %zu bytes, patch table %u rows\n",
			    kit.release, kit.wlan_size, kit.rows);
			aprint_normal_dev(self, "DPD marker before WLAN setup: %s\n",
			    uaicfw_u02_dpd_marker(sc));
			if (uaicfw_u02_passive_requested())
				aprint_normal_dev(self, "station start disabled "
				    "by aic8800dc/passive\n");
			else
				(void)uaicfw_u02_prepare(self, sc, &kit);
			uaicfw_kit_release(&kit);
		}
	}
#endif
	const usbd_status status = uaicfw_out_state(sc);

	aa.station = sc->sc_station_ready && status == USBD_NORMAL_COMPLETION;
	/* A terminal RX failure can occur while the root disk is mounting. */
	if (status != USBD_NORMAL_COMPLETION) {
		aprint_error_dev(self, "station transport unavailable: %s\n",
		    usbd_errstr(status));
	} else if (config_found(self, &aa, NULL,
	    CFARGS(.iattr = "aicwfusbbus")) == NULL) {
		aprint_error_dev(self, "station driver unavailable\n");
	}
	/* config_found also returns a device when its attach routine fails. */
	if (!aa.if_attached)
		uaicfw_stop(sc);
}

static void
uaicfw_attach(device_t parent, device_t self, void *aux)
{
	const struct usbif_attach_arg * const uiaa = aux;
	struct usbd_interface * const iface = uiaa->uiaa_iface;
	struct uaicfw_softc * const sc = device_private(self);
	struct uaicfw_endpoints eps;
	const usb_config_descriptor_t *desc;
	const char *phase = "bulk IN setup";
	uint32_t chip, sub;
	uint8_t cfm[AICWF_VERSION_LEN];
	usbd_status status;

	aprint_naive("\n");
	aprint_normal(": AIC8800DC USB WLAN interface\n");
	mutex_init(&sc->sc_cmd_lock, MUTEX_DEFAULT, IPL_NONE);
	mutex_init(&sc->sc_rx_lock, MUTEX_DEFAULT, IPL_SOFTUSB);
	cv_init(&sc->sc_cv, "uaicfw");
	sc->sc_udev = uiaa->uiaa_device;
	usb_init_task(&sc->sc_recover_task, uaicfw_recover_rx, sc, USB_TASKQ_MPSAFE);
#ifdef FDT
	static const char * const boards[] = { "luckfox,lyra-zero-w", NULL };
	const bool lyra = fdtbus_get_data() != NULL &&
	    of_compatible(OF_finddevice("/"), boards);
	const bool onboard = lyra && uiaa->uiaa_port == 1 &&
	    uaicfw_lyra_onboard(uiaa->uiaa_device);
	if (onboard && atomic_load_relaxed(&uaicfw_lyra_power_busy) != 0) {
		phase = "previous onboard power setup";
		status = USBD_IOERROR;
		goto fail;
	}
#endif
	if (!uaicfw_endpoints(iface, &eps)) {
		aprint_error_dev(self, "unsupported firmware endpoint layout\n");
		return;
	}
	for (size_t i = 0; i < __arraycount(sc->sc_rx); i++) {
		status = uaicfw_open(iface, eps.in[i], UAICFW_RX_SIZE,
		    &sc->sc_rx[i]);
		if (status != USBD_NORMAL_COMPLETION)
			goto fail;
		status = uaicfw_rx_submit(sc, sc->sc_rx[i].xfer);
		if (status != USBD_IN_PROGRESS && status != USBD_NORMAL_COMPLETION)
			goto fail;
	}
	sc->sc_iface = iface;
	sc->sc_data_ep = eps.out[0];
	phase = "command pipe setup";
	status = uaicfw_open(iface, eps.out[1], UAICFW_TX_SIZE, &sc->sc_tx);
	if (status != USBD_NORMAL_COMPLETION)
		goto fail;
	phase = "chip identification";
	status = uaicfw_read_field(sc, UAICFW_CHIP, &chip);
	if (status != USBD_NORMAL_COMPLETION)
		goto fail;
	phase = "subrevision identification";
	status = uaicfw_read_field(sc, UAICFW_SUBID, &sub);
	if (status != USBD_NORMAL_COMPLETION)
		goto fail;
	aprint_normal_dev(self, "chip ID 0x%02x, sub-ID 0x%02x\n",
	    (unsigned int)((chip >> 16) & 0xff), (unsigned int)(sub & 0xff));

	phase = "MM_VERSION";
	status = uaicfw_command(sc, AICWF_MM_VERSION_REQ, NULL, 0,
	    AICWF_MM_VERSION_CFM, cfm, sizeof(cfm), false, NULL);
	if (status != USBD_NORMAL_COMPLETION)
		goto fail;
	aicwf_version_decode(cfm, &sc->sc_fw_version);
	aprint_normal_dev(self, "FullMAC version 0x%08x, features 0x%08x\n",
	    sc->sc_fw_version.lmac, sc->sc_fw_version.features);
	desc = usbd_get_config_descriptor(uiaa->uiaa_device);
	if (((chip >> 16) & 0xff) != 7 || (sub & 0xff) != 1 ||
	    sc->sc_fw_version.lmac != 0x06040301 || desc == NULL ||
	    desc->bNumInterface != 3) {
		aprint_error_dev(self, "unsupported WLAN firmware or USB configuration\n");
		uaicfw_stop(sc);
		return;
	}
#ifdef FDT
	if (lyra) {
		phase = "onboard power setup";
		status = uaicfw_lyra_conditions(self, sc, uiaa, &eps, chip,
		    onboard);
		if (status != USBD_NORMAL_COMPLETION)
			goto fail;
	}
#endif
	config_mountroot(self, uaicfw_mountroot);
	return;
fail:
	aprint_error_dev(self, "%s failed: %s\n", phase, usbd_errstr(status));
	uaicfw_stop(sc);
}

static int
uaicfw_detach(device_t self, int flags)
{
	struct uaicfw_softc * const sc = device_private(self);

	(void)config_mountroot_cancel(self);
	int error = config_detach_children(self, flags);
	if (error != 0)
		return error;
	uaicfw_stop(sc);
	cv_destroy(&sc->sc_cv);
	mutex_destroy(&sc->sc_rx_lock);
	mutex_destroy(&sc->sc_cmd_lock);
	return 0;
}
