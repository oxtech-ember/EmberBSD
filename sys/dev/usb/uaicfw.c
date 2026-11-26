/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * AIC8800DC USB firmware identification. The vendor WLAN interface uses
 * the second bulk pipe in each direction for firmware commands.
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

#include <dev/ic/aicwf_command.h>
#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdevs.h>

#define	UAICFW_IDENT_REG	0x40500000
#define	UAICFW_SUBID_REG	0x00000020
#define	UAICFW_PARAM_MAX	1032	/* includes a 1024-byte block write */
#define	UAICFW_TX_SIZE		(AICWF_TX_MSG_OFFSET + AICWF_MSG_HDR_LEN + \
				 UAICFW_PARAM_MAX)
#define	UAICFW_RX_SIZE		2048
#define	UAICFW_TIMEOUT_MS	500

struct uaicfw_pipe {
	struct usbd_pipe *pipe;
	struct usbd_xfer *xfer;
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

struct uaicfw_softc {
	kmutex_t sc_cmd_lock;	/* serialize command writers */
	kmutex_t sc_rx_lock;	/* synchronize the sender with both IN pipes */
	kcondvar_t sc_cv;
	struct uaicfw_pipe sc_tx;
	struct uaicfw_pipe sc_rx[2];
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

/* Both IN pipes can carry firmware messages. The transfer owns the bytes. */
static usbd_status
uaicfw_receive(struct uaicfw_softc *sc, const uint8_t *p, size_t len)
{
	struct uaicfw_request *req = sc->sc_request;
	struct aicwf_rx_frame frame;
	const uint8_t *answer = NULL;
	int step;

	KASSERT(mutex_owned(&sc->sc_rx_lock));
	while ((step = aicwf_frame_decode(p, len, &frame)) > 0) {
		const uint8_t *msg = p + AICWF_HDR_LEN;
		uint16_t id, param_len;

		p += step;
		len -= step;
		if (frame.type != AICWF_TYPE_MSG)
			continue;
		if (!aicwf_reply_decode(msg, frame.len, &id, &param_len))
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
	if (step < 0)
		return USBD_INVAL;
	if (answer != NULL) {
		if (req->len != 0)
			memcpy(sc->sc_reply, answer, req->len);
		req->status = USBD_NORMAL_COMPLETION;
		cv_broadcast(&sc->sc_cv);
	}
	return USBD_NORMAL_COMPLETION;
}

static void
uaicfw_rxeof(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct uaicfw_softc * const sc = priv;
	uint32_t actual;

	mutex_enter(&sc->sc_rx_lock);
	if (sc->sc_stopping || sc->sc_error != USBD_NORMAL_COMPLETION) {
		mutex_exit(&sc->sc_rx_lock);
		return;
	}
	if (status == USBD_NORMAL_COMPLETION) {
		usbd_get_xfer_status(xfer, NULL, NULL, &actual, NULL);
		status = actual > UAICFW_RX_SIZE ? USBD_INVAL :
		    uaicfw_receive(sc, usbd_get_buffer(xfer), actual);
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

	/* Suspend waits for callbacks and prevents a resubmit during detach. */
	for (size_t i = 0; i < __arraycount(sc->sc_rx); i++)
		if (sc->sc_rx[i].pipe != NULL)
			usbd_suspend_pipe(sc->sc_rx[i].pipe);
	if (sc->sc_tx.pipe != NULL)
		usbd_suspend_pipe(sc->sc_tx.pipe);
	mutex_enter(&sc->sc_cmd_lock);
	uaicfw_close(&sc->sc_tx);
	for (size_t i = 0; i < __arraycount(sc->sc_rx); i++)
		uaicfw_close(&sc->sc_rx[i]);
	mutex_exit(&sc->sc_cmd_lock);
}

static void
uaicfw_attach(device_t parent, device_t self, void *aux)
{
	const struct usbif_attach_arg * const uiaa = aux;
	struct usbd_interface * const iface = uiaa->uiaa_iface;
	struct uaicfw_softc * const sc = device_private(self);
	struct uaicfw_endpoints eps;
	uint32_t chip, sub;
	struct aicwf_version fw_version;
	uint8_t cfm[AICWF_VERSION_LEN];
	usbd_status status;

	aprint_naive("\n");
	aprint_normal(": AIC8800DC USB WLAN interface\n");
	mutex_init(&sc->sc_cmd_lock, MUTEX_DEFAULT, IPL_NONE);
	mutex_init(&sc->sc_rx_lock, MUTEX_DEFAULT, IPL_SOFTUSB);
	cv_init(&sc->sc_cv, "uaicfw");
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
		aicwf_version_decode(cfm, &fw_version);
		aprint_normal_dev(self, "FullMAC version 0x%08x, features 0x%08x\n",
		    fw_version.lmac, fw_version.features);
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

	uaicfw_stop(sc);
	cv_destroy(&sc->sc_cv);
	mutex_destroy(&sc->sc_rx_lock);
	mutex_destroy(&sc->sc_cmd_lock);
	return 0;
}
