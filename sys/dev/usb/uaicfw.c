/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * AIC8800DC USB WLAN transport. The second bulk OUT pipe carries commands.
 * Both bulk IN pipes can deliver firmware messages.
 * Hardware facts: Luckfox SDK 6dcbca0d8a51, aic8800_fdrv/aicwf_usb.c
 * and aicwf_compat_8800dc.c.
 */
#include <sys/param.h>
#include <sys/condvar.h>
#include <sys/device.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/mutex.h>
#include <sys/systm.h>

#include <net/if_ether.h>

#include <dev/ic/aicwf_command.h>
#include <dev/ic/aicwf_sta.h>
#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdevs.h>
#include <dev/usb/uaicfwvar.h>

#define	UAICFW_IDENT_REG	0x40500000
#define	UAICFW_SUBID_REG	0x00000020
#define	UAICFW_PARAM_MAX	1032	/* includes a 1024-byte block write */
#define	UAICFW_TX_SIZE		(AICWF_TX_MSG_OFFSET + AICWF_MSG_HDR_LEN + \
				 UAICFW_PARAM_MAX)
#define	UAICFW_RX_SIZE		2048
#define	UAICFW_PAYLOAD_MAX	0xfff	/* DC USB record length field */
#define	UAICFW_TIMEOUT_MS	500

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

struct uaicfw_request {
	uint16_t id;
	size_t len;
	usbd_status status;
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
uaicfw_ethernet_confirm(struct uaicfw_softc *sc, const uint8_t *rec,
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
		if (id == AICWF_DBG_ERROR_IND)
			return USBD_IOERROR;
		if (req == NULL || req->id != id)
			continue;
		if (answer != NULL || req->status != USBD_IN_PROGRESS ||
		    param_len != req->len)
			return USBD_INVAL;
		answer = msg + AICWF_RX_MSG_PARAM;
	}
	if (answer != NULL) {
		if (req->len != 0)
			memcpy(sc->sc_reply, answer, req->len);
		req->status = USBD_NORMAL_COMPLETION;
		cv_broadcast(&sc->sc_cv);
	}
	if (sc->sc_input != NULL || sc->sc_data_reply != NULL) {
		for (size_t pos = 0; pos < end;) {
			const bool continued = pos == 0 && stream->pending != 0;
			const uint8_t *rec = p + pos;
			size_t raw;
			pos += uaicfw_record_size(rec, le16dec(rec), &raw);
			if (uaicfw_ethernet_confirm(sc, rec, raw))
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

static usbd_status
uaicfw_command(struct uaicfw_softc *sc, uint16_t cmd, const void *param,
    size_t request_len, uint16_t cfm, void *reply, size_t reply_len)
{
	struct uaicfw_request req = { cfm, reply_len, USBD_IN_PROGRESS };
	struct usbd_xfer *xfer;
	uint32_t actual;
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
	status = usbd_sync_transfer(xfer);
	if (status == USBD_NORMAL_COMPLETION) {
		usbd_get_xfer_status(xfer, NULL, NULL, &actual, NULL);
		if (actual != len)
			status = USBD_SHORT_XFER;
	}
	mutex_enter(&sc->sc_rx_lock);
	while (status == USBD_NORMAL_COMPLETION && !sc->sc_stopping &&
	    sc->sc_error == USBD_NORMAL_COMPLETION &&
	    req.status == USBD_IN_PROGRESS) {
		if (cv_timedwait(&sc->sc_cv, &sc->sc_rx_lock,
		    mstohz(UAICFW_TIMEOUT_MS)) == EWOULDBLOCK)
			status = USBD_TIMEOUT;
	}
	if (sc->sc_stopping)
		status = USBD_CANCELLED;
	else if (sc->sc_error != USBD_NORMAL_COMPLETION)
		status = sc->sc_error;
	else if (status == USBD_NORMAL_COMPLETION)
		status = req.status;
	/* A late reply has no sequence number. Do not reuse its command ID. */
	if (status != USBD_NORMAL_COMPLETION)
		sc->sc_error = status;
	else if (reply_len != 0)
		memcpy(reply, sc->sc_reply, reply_len);
	sc->sc_request = NULL;
done:
	mutex_exit(&sc->sc_rx_lock);
	mutex_exit(&sc->sc_cmd_lock);
	return status;
}

static usbd_status
uaicfw_read_id(struct uaicfw_softc *sc, uint32_t reg, uint32_t *value)
{
	uint8_t req[4], cfm[8];
	usbd_status status;

	le32enc(req, reg);
	status = uaicfw_command(sc, AICWF_DBG_MEM_READ_REQ, req,
	    sizeof(req), AICWF_DBG_MEM_READ_CFM, cfm, sizeof(cfm));
	if (status == USBD_NORMAL_COMPLETION &&
	    !aicwf_read_confirm(cfm, sizeof(cfm), reg, value))
		return USBD_INVAL;
	return status;
}

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
		usbd_status clear = usbd_clear_endpoint_stall(p->pipe);
		if (clear != USBD_NORMAL_COMPLETION && p == &sc->sc_tx) {
			mutex_enter(&sc->sc_rx_lock);
			sc->sc_error = clear;
			cv_broadcast(&sc->sc_cv);
			mutex_exit(&sc->sc_rx_lock);
		}
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
	if (!uaicfw_survey_command(packet, len))
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

/* The common station supplies an Ethernet descriptor and its MSDU payload. */
static int
uaicfw_ethernet(void *arg, const uint8_t *packet, size_t len,
    const struct uaicfw_port *port, bool *attempted)
{
	struct uaicfw_softc * const sc = arg;
	struct uaicfw_data_reply reply = { .status = USBD_IN_PROGRESS };
	const size_t hdr = AICWF_HDR_LEN + AICWF_TXDESC_LEN;
	const uint8_t *desc;
	uint8_t *buf;
	size_t wire;
	uint32_t start, timeout;
	usbd_status status;

	if (attempted != NULL)
		*attempted = false;
	if (len < hdr || len - hdr > ETHERMTU)
		return EMSGSIZE;
	desc = packet + AICWF_HDR_LEN;
	if (packet[2] != AICWF_TYPE_DATA ||
	    le16dec(packet) != len - AICWF_HDR_LEN || le16dec(desc) != len - hdr ||
	    le16dec(desc + 2) != 0 || le32dec(desc + 4) != 0 ||
	    le16dec(desc + 26) != 0)
		return EINVAL;
	const bool eapol = be16dec(desc + 20) == ETHERTYPE_PAE;
	wire = eapol ? (len + 3) & ~(size_t)3 : len;
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
	if (port != NULL && port->current(port->cookie) != port->serial) {
		mutex_exit(&sc->sc_cmd_lock);
		return EACCES;
	}
	buf = usbd_get_buffer(sc->sc_data.xfer);
	memcpy(buf, packet, len);
	le16enc(buf, wire);
	buf[3] = 0;
	if (eapol) {
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
	timeout = mstohz(UAICFW_TIMEOUT_MS);
	start = getticks();
	status = uaicfw_send(sc, &sc->sc_data, wire, attempted);
	if (eapol) {
		mutex_enter(&sc->sc_rx_lock);
		while (status == USBD_NORMAL_COMPLETION && !sc->sc_stopping &&
		    sc->sc_error == USBD_NORMAL_COMPLETION &&
		    reply.status == USBD_IN_PROGRESS) {
			const uint32_t elapsed = (uint32_t)getticks() - start;
			if (elapsed >= timeout ||
			    (cv_timedwait(&sc->sc_cv, &sc->sc_rx_lock,
			    timeout - elapsed) == EWOULDBLOCK &&
			    reply.status == USBD_IN_PROGRESS && !sc->sc_stopping &&
			    sc->sc_error == USBD_NORMAL_COMPLETION))
				status = USBD_TIMEOUT;
		}
		if (status == USBD_NORMAL_COMPLETION) {
			status = sc->sc_stopping ? USBD_CANCELLED : sc->sc_error;
			if (status == USBD_NORMAL_COMPLETION)
				status = reply.status;
		}
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
		.cookie = sc, .write = uaicfw_write, .ethernet = uaicfw_ethernet,
		.listen = uaicfw_listen,
	};
	(void)config_found(self, &aa, NULL, CFARGS(.iattr = "aicwfusbbus"));
}

static void
uaicfw_attach(device_t parent, device_t self, void *aux)
{
	const struct usbif_attach_arg * const uiaa = aux;
	struct usbd_interface * const iface = uiaa->uiaa_iface;
	struct uaicfw_softc * const sc = device_private(self);
	struct uaicfw_endpoints eps;
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
	status = uaicfw_open(iface, eps.out[1], UAICFW_TX_SIZE, &sc->sc_tx);
	if (status == USBD_NORMAL_COMPLETION)
		status = uaicfw_read_id(sc, UAICFW_IDENT_REG, &chip);
	if (status == USBD_NORMAL_COMPLETION)
		status = uaicfw_read_id(sc, UAICFW_SUBID_REG, &sub);
	if (status != USBD_NORMAL_COMPLETION)
		goto fail;
	aprint_normal_dev(self, "chip ID 0x%02x, sub-ID 0x%02x\n",
	    (unsigned int)((chip >> 16) & 0xff), (unsigned int)(sub & 0xff));

	/* A missing reply leaves the FullMAC state unknown. */
	status = uaicfw_command(sc, AICWF_MM_VERSION_REQ, NULL, 0,
	    AICWF_MM_VERSION_CFM, cfm, sizeof(cfm));
	if (status == USBD_NORMAL_COMPLETION) {
		aicwf_version_decode(cfm, &sc->sc_fw_version);
		aprint_normal_dev(self, "FullMAC version 0x%08x, features 0x%08x\n",
		    sc->sc_fw_version.lmac, sc->sc_fw_version.features);
		const usb_config_descriptor_t *desc =
		    usbd_get_config_descriptor(uiaa->uiaa_device);
		if (((chip >> 16) & 0xff) == 7 && (sub & 0xff) == 1 &&
		    sc->sc_fw_version.lmac == 0x06040301 && desc != NULL &&
		    desc->bNumInterface == 3) {
			config_mountroot(self, uaicfw_mountroot);
		}
	} else {
		uaicfw_stop(sc);
		aprint_verbose_dev(self, "FullMAC version unavailable: %s\n",
		    usbd_errstr(status));
	}
	return;
fail:
	aprint_error_dev(self, "firmware identification failed: %s\n",
	    usbd_errstr(status));
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
