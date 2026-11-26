/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * AIC8800DC USB firmware identification. The vendor WLAN interface uses
 * the second bulk pipe in each direction for firmware commands.
 * Hardware facts: Luckfox SDK 6dcbca0d8a51, aic8800_fdrv/aicwf_usb.c
 * and aicwf_compat_8800dc.c.
 */
#include <sys/param.h>
#include <sys/device.h>
#include <sys/endian.h>
#include <sys/systm.h>

#include <dev/ic/aicwf_command.h>
#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdevs.h>

#define	UAICFW_IDENT_REG	0x40500000
#define	UAICFW_SUBID_REG	0x00000020
#define	UAICFW_TX_SIZE		20
#define	UAICFW_RX_SIZE		2048
#define	UAICFW_TIMEOUT_MS	500

struct uaicfw_pipe {
	struct usbd_pipe *pipe;
	struct usbd_xfer *xfer;
};

static int uaicfw_match(device_t, cfdata_t, void *);
static void uaicfw_attach(device_t, device_t, void *);
static int uaicfw_detach(device_t, int);

CFATTACH_DECL_NEW(uaicfw, 0, uaicfw_match, uaicfw_attach, uaicfw_detach, NULL);

/* The first bulk endpoint per direction carries data, the second commands. */
static bool
uaicfw_endpoints(struct usbd_interface *iface, uint8_t *in, uint8_t *out)
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
	*out = epaddr[0][1];
	*in = epaddr[1][1];
	return true;
}

static int
uaicfw_match(device_t parent, cfdata_t cf, void *aux)
{
	const struct usbif_attach_arg * const uiaa = aux;
	uint8_t in, out;

	if (uiaa->uiaa_vendor == USB_VENDOR_AICSEMI &&
	    uiaa->uiaa_product == USB_PRODUCT_AICSEMI_AIC8800DC &&
	    uiaa->uiaa_configno == 1 && uiaa->uiaa_class == UICLASS_VENDOR &&
	    uiaa->uiaa_subclass == 0xff && uiaa->uiaa_proto == 0xff &&
	    uaicfw_endpoints(uiaa->uiaa_iface, &in, &out))
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
	error = usbd_create_xfer(p->pipe, len, 0, 0, &p->xfer);
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
}

/* One synchronous command owns both pipes through its confirmation. */
static usbd_status
uaicfw_command(struct uaicfw_pipe *tx, struct uaicfw_pipe *rx, uint16_t cmd,
    const void *param, size_t request_len, uint16_t cfm, void *reply,
    size_t reply_len)
{
	uint8_t *buf = usbd_get_buffer(tx->xfer);
	uint32_t actual;
	usbd_status status;
	size_t len;

	len = aicwf_command_encode(buf, UAICFW_TX_SIZE, cmd, param, request_len);
	if (len == 0)
		return USBD_INVAL;
	usbd_setup_xfer(tx->xfer, NULL, buf, len, 0, UAICFW_TIMEOUT_MS, NULL);
	status = usbd_sync_transfer(tx->xfer);
	if (status != USBD_NORMAL_COMPLETION)
		return status;
	usbd_get_xfer_status(tx->xfer, NULL, NULL, &actual, NULL);
	if (actual != len)
		return USBD_SHORT_XFER;

	buf = usbd_get_buffer(rx->xfer);
	/* Other firmware indications can precede the requested confirmation. */
	for (u_int i = 0; i < 4; i++) {
		usbd_setup_xfer(rx->xfer, NULL, buf, UAICFW_RX_SIZE,
		    USBD_SHORT_XFER_OK, UAICFW_TIMEOUT_MS, NULL);
		status = usbd_sync_transfer(rx->xfer);
		if (status != USBD_NORMAL_COMPLETION)
			return status;
		usbd_get_xfer_status(rx->xfer, NULL, NULL, &actual, NULL);
		if (actual > UAICFW_RX_SIZE)
			return USBD_INVAL;
		struct aicwf_rx_frame frame;
		const uint8_t *p = buf;
		size_t remaining = actual;
		int step;
		const uint8_t *answer = NULL;

		while ((step = aicwf_frame_decode(p, remaining, &frame)) > 0) {
			const uint8_t *msg = p + AICWF_HDR_LEN;
			uint16_t id, param_len;

			p += step;
			remaining -= step;
			if (frame.type != AICWF_TYPE_MSG)
				continue;
			if (!aicwf_reply_decode(msg, frame.len, &id, &param_len))
				return USBD_INVAL;
			if (id != cfm)
				continue;
			if (answer != NULL || param_len != reply_len)
				return USBD_INVAL;
			answer = msg + AICWF_RX_MSG_PARAM;
		}
		if (step < 0)
			return USBD_INVAL;
		if (answer != NULL) {
			if (reply_len != 0)
				memcpy(reply, answer, reply_len);
			return USBD_NORMAL_COMPLETION;
		}
	}
	return USBD_TIMEOUT;
}

static usbd_status
uaicfw_read_id(struct uaicfw_pipe *tx, struct uaicfw_pipe *rx,
    uint32_t reg, uint32_t *value)
{
	uint8_t req[4], cfm[8];
	usbd_status status;

	le32enc(req, reg);
	status = uaicfw_command(tx, rx, AICWF_DBG_MEM_READ_REQ, req,
	    sizeof(req), AICWF_DBG_MEM_READ_CFM, cfm, sizeof(cfm));
	if (status == USBD_NORMAL_COMPLETION &&
	    !aicwf_read_confirm(cfm, sizeof(cfm), reg, value))
		return USBD_INVAL;
	return status;
}

static void
uaicfw_attach(device_t parent, device_t self, void *aux)
{
	const struct usbif_attach_arg * const uiaa = aux;
	struct usbd_interface * const iface = uiaa->uiaa_iface;
	struct uaicfw_pipe rx = { 0 }, tx = { 0 };
	uint8_t in, out;
	uint32_t chip, sub;
	struct aicwf_version fw_version;
	uint8_t cfm[AICWF_VERSION_LEN];
	usbd_status status;

	aprint_naive("\n");
	aprint_normal(": AIC8800DC USB WLAN interface\n");
	if (!uaicfw_endpoints(iface, &in, &out)) {
		aprint_error_dev(self, "unsupported firmware endpoint layout\n");
		return;
	}
	status = uaicfw_open(iface, in, UAICFW_RX_SIZE, &rx);
	if (status == USBD_NORMAL_COMPLETION)
		status = uaicfw_open(iface, out, UAICFW_TX_SIZE, &tx);
	if (status == USBD_NORMAL_COMPLETION)
		status = uaicfw_read_id(&tx, &rx, UAICFW_IDENT_REG, &chip);
	if (status == USBD_NORMAL_COMPLETION)
		status = uaicfw_read_id(&tx, &rx, UAICFW_SUBID_REG, &sub);
	if (status != USBD_NORMAL_COMPLETION) {
		aprint_error_dev(self, "firmware identification failed: %s\n",
		    usbd_errstr(status));
		goto out;
	}
	aprint_normal_dev(self, "chip ID 0x%02x, sub-ID 0x%02x\n",
	    (unsigned int)((chip >> 16) & 0xff), (unsigned int)(sub & 0xff));

	/* A missing reply leaves the FullMAC state unknown. */
	status = uaicfw_command(&tx, &rx, AICWF_MM_VERSION_REQ, NULL, 0,
	    AICWF_MM_VERSION_CFM, cfm, sizeof(cfm));
	if (status == USBD_NORMAL_COMPLETION) {
		aicwf_version_decode(cfm, &fw_version);
		aprint_normal_dev(self, "FullMAC version 0x%08x, features 0x%08x\n",
		    fw_version.lmac, fw_version.features);
	} else {
		aprint_verbose_dev(self, "FullMAC version unavailable: %s\n",
		    usbd_errstr(status));
	}
out:
	uaicfw_close(&tx);
	uaicfw_close(&rx);
}

static int
uaicfw_detach(device_t self, int flags)
{
	/* The synchronous probe releases all pipes before attach returns. */
	return 0;
}
