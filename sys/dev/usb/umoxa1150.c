/* Origin: EmberBSD independent MOXA UPort 1150 USB parent, 2026-10-13. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/kmem.h>
#include <sys/mutex.h>
#include <sys/termios.h>
#include <sys/tty.h>
#include <dev/firmload.h>
#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdivar.h>
#include <dev/usb/usbdi_util.h>
#include <dev/usb/ucomvar.h>
#include <dev/usb/umoxa1150io.h>
#include <dev/usb/umoxa1150_protocol.h>

struct umoxa1150_softc {
	device_t sc_dev, sc_subdev;
	struct usbd_device *sc_udev;
	struct usbd_interface *sc_iface;
	struct usbd_pipe *sc_intr_pipe, *sc_fw_pipe;
	struct usbd_xfer *sc_fw_xfer;
	uint8_t *sc_intr_buf;
	struct umoxa1150_layout sc_layout;
	struct umoxa1150_settings sc_settings;
	struct umoxa1150_io sc_io;
	kmutex_t sc_lock, sc_status_lock;
	bool sc_dying, sc_open;
	uint8_t sc_mcr, sc_msr, sc_lsr;
	int sc_fault;
};

static int umoxa1150_match(device_t, cfdata_t, void *);
static void umoxa1150_attach(device_t, device_t, void *);
static int umoxa1150_detach(device_t, int);
static void umoxa1150_childdet(device_t, device_t);
static void umoxa1150_firmware_hook(device_t);
static int umoxa1150_control(void *, uint8_t, uint16_t, uint16_t,
    const uint8_t *, size_t);
static int umoxa1150_bulk(void *, const uint8_t *, size_t, size_t *);
static int umoxa1150_open(void *, int);
static void umoxa1150_close(void *, int);
static int umoxa1150_param(void *, int, struct termios *);
static int umoxa1150_ioctl(void *, int, u_long, void *, int, proc_t *);
static void umoxa1150_set(void *, int, int, int);
static void umoxa1150_get_status(void *, int, u_char *, u_char *);
static void umoxa1150_intr(struct usbd_xfer *, void *, usbd_status);

CFATTACH_DECL2_NEW(umoxa, sizeof(struct umoxa1150_softc),
    umoxa1150_match, umoxa1150_attach, umoxa1150_detach, NULL, NULL,
    umoxa1150_childdet);

static const struct ucom_methods umoxa1150_methods = {
	.ucom_get_status = umoxa1150_get_status,
	.ucom_set = umoxa1150_set,
	.ucom_param = umoxa1150_param,
	.ucom_ioctl = umoxa1150_ioctl,
	.ucom_open = umoxa1150_open,
	.ucom_close = umoxa1150_close,
	.ucom_param_strict = true,
};

static int
umoxa1150_match(device_t parent, cfdata_t match, void *aux)
{
	struct usb_attach_arg *uaa = aux;

	return uaa->uaa_vendor == 0x110a && uaa->uaa_product == 0x1150 ?
	    UMATCH_VENDOR_PRODUCT : UMATCH_NONE;
}

static int
umoxa1150_control(void *arg, uint8_t command, uint16_t value, uint16_t index,
    const uint8_t *bytes, size_t size)
{
	struct umoxa1150_softc *sc = arg;
	usb_device_request_t req;
	usbd_status status;
	int actual = 0;

	if (sc->sc_dying || size > 65535)
		return EIO;
	memset(&req, 0, sizeof(req));
	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = command;
	USETW(req.wValue, value);
	USETW(req.wIndex, index);
	USETW(req.wLength, size);
	if (command == 12)
		usbd_delay_ms(sc->sc_udev, 100);
	status = usbd_do_request_flags(sc->sc_udev, &req, __UNCONST(bytes),
	    0, &actual, 1000);
	return status == USBD_NORMAL_COMPLETION && actual == (int)size ? 0 : EIO;
}

static int
umoxa1150_bulk(void *arg, const uint8_t *bytes, size_t size, size_t *actual)
{
	struct umoxa1150_softc *sc = arg;
	void *buffer = usbd_get_buffer(sc->sc_fw_xfer);
	uint32_t count = size;
	usbd_status status;

	if (sc->sc_dying || size > 64)
		return EIO;
	memcpy(buffer, bytes, size);
	status = usbd_bulk_transfer(sc->sc_fw_xfer, sc->sc_fw_pipe,
	    0, 1000, buffer, &count);
	*actual = count;
	return status == USBD_NORMAL_COMPLETION ? 0 : EIO;
}

static int
umoxa1150_layout(struct umoxa1150_softc *sc, int cold_layout)
{
	struct umoxa1150_endpoint ep[3];
	usb_endpoint_descriptor_t *ed;
	usb_interface_descriptor_t *id;
	usb_config_descriptor_t *cd;
	unsigned i;

	cd = usbd_get_config_descriptor(sc->sc_udev);
	if (cd == NULL || cd->bConfigurationValue != (cold_layout ? 1 : 2) ||
	    cd->bNumInterface != 1 || usbd_device2interface_handle(sc->sc_udev,
	    0, &sc->sc_iface) != USBD_NORMAL_COMPLETION)
		return EPROTO;
	id = usbd_get_interface_descriptor(sc->sc_iface);
	if (id == NULL || id->bNumEndpoints != (cold_layout ? 1 : 3))
		return EPROTO;
	for (i = 0; i < id->bNumEndpoints; i++) {
		ed = usbd_interface2endpoint_descriptor(sc->sc_iface, i);
		if (ed == NULL)
			return EPROTO;
		ep[i].address = ed->bEndpointAddress;
		ep[i].type = UE_GET_XFERTYPE(ed->bmAttributes);
		ep[i].packet = UGETW(ed->wMaxPacketSize);
	}
	return umoxa1150_endpoints(ep, id->bNumEndpoints, cold_layout, &sc->sc_layout);
}

static void
umoxa1150_attach(device_t parent, device_t self, void *aux)
{
	struct usb_attach_arg *uaa = aux;
	struct umoxa1150_softc *sc = device_private(self);
	struct ucom_attach_args ucaa;
	usb_device_descriptor_t *dd;
	int cold_layout, error;

	aprint_naive("\n");
	aprint_normal("\n");
	sc->sc_dev = self;
	sc->sc_udev = uaa->uaa_device;
	mutex_init(&sc->sc_lock, MUTEX_DEFAULT, IPL_NONE);
	mutex_init(&sc->sc_status_lock, MUTEX_DEFAULT, IPL_SOFTUSB);
	sc->sc_settings = (struct umoxa1150_settings){
	    9600, 8, 0, 1, 0, 0, 0, 0, 0, 0x11, 0x13};
	sc->sc_mcr = 0x30;
	sc->sc_io = (struct umoxa1150_io){sc, umoxa1150_control, umoxa1150_bulk};
	dd = usbd_get_device_descriptor(sc->sc_udev);
	if (dd->bNumConfigurations != 1 && dd->bNumConfigurations != 2)
		goto bad;
	cold_layout = dd->bNumConfigurations == 1;
	error = usbd_set_config_no(sc->sc_udev, cold_layout ? 1 : 2, 1);
	if (error != USBD_NORMAL_COMPLETION || umoxa1150_layout(sc, cold_layout) != 0)
		goto bad;
	if (cold_layout) {
		/* Attach itself pins softc; do not queue an unowned root hook. */
		if (!root_is_mounted)
			aprint_error_dev(self, "reconnect after root mount to load firmware\n");
		else
			umoxa1150_firmware_hook(self);
		return;
	}
	memset(&ucaa, 0, sizeof(ucaa));
	ucaa.ucaa_portno = 0;
	ucaa.ucaa_bulkin = sc->sc_layout.in;
	ucaa.ucaa_bulkout = sc->sc_layout.out;
	ucaa.ucaa_ibufsize = ucaa.ucaa_ibufsizepad = ucaa.ucaa_obufsize = 256;
	ucaa.ucaa_device = sc->sc_udev;
	ucaa.ucaa_iface = sc->sc_iface;
	ucaa.ucaa_methods = &umoxa1150_methods;
	ucaa.ucaa_arg = sc;
	ucaa.ucaa_info = "UPort 1150 (physical validation pending)";
	sc->sc_subdev = config_found(self, &ucaa, ucomprint, CFARGS(.submatch = ucomsubmatch));
	if (sc->sc_subdev == NULL)
		goto bad;
	usbd_add_drv_event(USB_EVENT_DRIVER_ATTACH, sc->sc_udev, self);
	return;
bad:
	sc->sc_dying = true;
	aprint_error_dev(self, "unsupported or unavailable UPort 1150 USB layout\n");
}

static void
umoxa1150_firmware_hook(device_t self)
{
	struct umoxa1150_softc *sc = device_private(self);
	firmware_handle_t fw;
	uint8_t *raw = NULL, *image = NULL;
	off_t size;
	int error;

	error = firmware_open("moxa", "moxa-1150.fw", &fw);
	if (error)
		goto done;
	size = firmware_get_size(fw);
	if (size < 3 || size > UMOXA1150_IMAGE_SIZE) {
		error = EINVAL;
		goto close;
	}
	raw = kmem_alloc((size_t)size, KM_SLEEP);
	image = kmem_alloc(UMOXA1150_IMAGE_SIZE, KM_SLEEP);
	error = firmware_read(fw, 0, raw, (size_t)size);
	if (error)
		goto free;
	error = umoxa1150_firmware(raw, size, image, UMOXA1150_IMAGE_SIZE);
	if (error)
		goto free;
	error = usbd_open_pipe(sc->sc_iface, sc->sc_layout.out,
	    USBD_EXCLUSIVE_USE, &sc->sc_fw_pipe);
	if (error)
		goto free;
	error = usbd_create_xfer(sc->sc_fw_pipe, 64, 0, 0, &sc->sc_fw_xfer);
	if (error == USBD_NORMAL_COMPLETION) {
		error = umoxa1150_upload(&sc->sc_io, image, UMOXA1150_IMAGE_SIZE);
		usbd_destroy_xfer(sc->sc_fw_xfer);
		sc->sc_fw_xfer = NULL;
	}
	usbd_close_pipe(sc->sc_fw_pipe);
	sc->sc_fw_pipe = NULL;
free:
	kmem_free(image, UMOXA1150_IMAGE_SIZE);
	kmem_free(raw, (size_t)size);
close:
	firmware_close(fw);
done:
	if (error)
		aprint_error_dev(self, "firmware load failed (%d); no tty attached\n", error);
	else
		aprint_normal_dev(self, "firmware transferred; waiting for USB re-enumeration\n");
}

static void
umoxa1150_close_intr(struct umoxa1150_softc *sc)
{
	if (sc->sc_intr_pipe != NULL) {
		usbd_abort_pipe(sc->sc_intr_pipe);
		usbd_close_pipe(sc->sc_intr_pipe);
		sc->sc_intr_pipe = NULL;
	}
	if (sc->sc_intr_buf != NULL) {
		kmem_free(sc->sc_intr_buf, sc->sc_layout.interrupt_size);
		sc->sc_intr_buf = NULL;
	}
}

static int
umoxa1150_open(void *arg, int port)
{
	struct umoxa1150_softc *sc = arg;
	int error;

	mutex_enter(&sc->sc_lock);
	if (sc->sc_dying || port != 0) {
		error = EIO;
		goto done;
	}
	if (sc->sc_open) {
		error = EBUSY;
		goto done;
	}
	sc->sc_intr_buf = kmem_alloc(sc->sc_layout.interrupt_size, KM_SLEEP);
	error = usbd_open_pipe_intr(sc->sc_iface, sc->sc_layout.interrupt,
	    USBD_SHORT_XFER_OK, &sc->sc_intr_pipe, sc, sc->sc_intr_buf,
	    sc->sc_layout.interrupt_size, umoxa1150_intr, USBD_DEFAULT_INTERVAL);
	if (error == USBD_NORMAL_COMPLETION)
		error = umoxa1150_start(&sc->sc_io, &sc->sc_settings, sc->sc_mcr);
	if (error) {
		umoxa1150_close_intr(sc);
		sc->sc_fault = EIO;
		error = EIO;
	} else {
		sc->sc_fault = 0;
		sc->sc_open = true;
	}
done:
	mutex_exit(&sc->sc_lock);
	return error;
}

static void
umoxa1150_close(void *arg, int port)
{
	struct umoxa1150_softc *sc = arg;

	mutex_enter(&sc->sc_lock);
	if (sc->sc_open && !sc->sc_dying && umoxa1150_stop(&sc->sc_io) != 0)
		aprint_error_dev(sc->sc_dev, "port stop/close failed\n");
	umoxa1150_close_intr(sc);
	sc->sc_open = false;
	mutex_exit(&sc->sc_lock);
}

static int
umoxa1150_param(void *arg, int port, struct termios *t)
{
	struct umoxa1150_softc *sc = arg;
	struct umoxa1150_settings s;
	uint8_t probe[10], mcr;
	int error;

	mutex_enter(&sc->sc_lock);
	s = sc->sc_settings;
	if (sc->sc_dying || port != 0) {
		error = EIO;
		goto done;
	}
	if ((t->c_ispeed != 0 && t->c_ispeed != t->c_ospeed) ||
	    (t->c_cflag & (CDTRCTS | MDMBUF)) != 0) {
		error = EINVAL;
		goto done;
	}
	s.baud = t->c_ospeed ? t->c_ospeed : 9600;
	s.bits = (t->c_cflag & CSIZE) == CS5 ? 5 :
	    (t->c_cflag & CSIZE) == CS6 ? 6 : (t->c_cflag & CSIZE) == CS7 ? 7 : 8;
	s.parity = !(t->c_cflag & PARENB) ? 0 : (t->c_cflag & PARODD) ? 1 : 2;
	s.stops = (t->c_cflag & CSTOPB) ? 2 : 1;
	s.rtscts = (t->c_cflag & (CRTS_IFLOW | CCTS_OFLOW)) != 0;
	/* Legacy ucom only supplies speed/cflag; TTY owns software flow. */
	s.ixon = s.ixoff = 0;
	mcr = t->c_ospeed ? sc->sc_mcr : 0;
	error = umoxa1150_config(&s, probe);
	if (error == 0)
		error = umoxa1150_apply(&sc->sc_io, &s, mcr);
	if (error == 0) {
		sc->sc_settings = s;
		sc->sc_mcr = mcr;
	} else {
		sc->sc_fault = error;
		aprint_error_dev(sc->sc_dev, "serial configuration failed (%d)\n", error);
	}
done:
	mutex_exit(&sc->sc_lock);
	return error;
}

static int
umoxa1150_ioctl(void *arg, int port, u_long cmd, void *data, int flag, proc_t *p)
{
	struct umoxa1150_softc *sc = arg;
	struct umoxa1150_settings s;
	int error = EPASSTHROUGH, mode;

	mutex_enter(&sc->sc_lock);
	if (cmd == UMOXA1150_GET_FAULT) {
		*(int *)data = sc->sc_fault;
		error = 0;
	} else if (cmd == UMOXA1150_SET_MODE) {
		mode = *(int *)data;
		if (mode < 0 || mode > 2)
			error = EINVAL;
		else if (sc->sc_dying)
			error = EIO;
		else {
			s = sc->sc_settings;
			s.mode = mode;
			error = umoxa1150_apply(&sc->sc_io, &s, sc->sc_mcr);
			if (error == 0)
				sc->sc_settings = s;
			else
				sc->sc_fault = error;
		}
	}
	mutex_exit(&sc->sc_lock);
	return error;
}

static void
umoxa1150_set(void *arg, int port, int which, int on)
{
	struct umoxa1150_softc *sc = arg;
	struct umoxa1150_settings s;
	uint8_t mcr;
	int error;

	mutex_enter(&sc->sc_lock);
	if (sc->sc_dying)
		goto done;
	s = sc->sc_settings;
	mcr = sc->sc_mcr;
	if (which == UCOM_SET_DTR)
		mcr = on ? mcr | 0x10 : mcr & ~0x10;
	else if (which == UCOM_SET_RTS)
		mcr = on ? mcr | 0x20 : mcr & ~0x20;
	else if (which == UCOM_SET_BREAK)
		s.send_break = !!on;
	else
		goto done;
	error = umoxa1150_apply(&sc->sc_io, &s, mcr);
	if (error == 0) {
		sc->sc_settings = s;
		sc->sc_mcr = mcr;
	} else {
		sc->sc_fault = error;
		aprint_error_dev(sc->sc_dev, "modem control failed (%d)\n", error);
	}
done:
	mutex_exit(&sc->sc_lock);
}

static void
umoxa1150_get_status(void *arg, int port, u_char *lsr, u_char *msr)
{
	struct umoxa1150_softc *sc = arg;

	mutex_enter(&sc->sc_status_lock);
	*lsr = sc->sc_lsr; *msr = sc->sc_msr;
	mutex_exit(&sc->sc_status_lock);
}

static void
umoxa1150_intr(struct usbd_xfer *xfer, void *arg, usbd_status status)
{
	struct umoxa1150_softc *sc = arg;
	struct umoxa1150_event e;
	uint32_t size;
	int error;

	if (sc->sc_dying || status == USBD_CANCELLED || status == USBD_NOT_STARTED)
		return;
	if (status != USBD_NORMAL_COMPLETION) {
		aprint_error_dev(sc->sc_dev, "interrupt transfer failed\n");
		return;
	}
	usbd_get_xfer_status(xfer, NULL, NULL, &size, NULL);
	error = umoxa1150_event(sc->sc_intr_buf, size, &e);
	if (error) {
		aprint_error_dev(sc->sc_dev, "invalid device status (%d)\n", error);
		return;
	}
	mutex_enter(&sc->sc_status_lock);
	if (e.kind == 4)
		sc->sc_msr = e.msr;
	else
		sc->sc_lsr = e.lsr;
	mutex_exit(&sc->sc_status_lock);
	if (sc->sc_subdev != NULL)
		ucom_status_change(device_private(sc->sc_subdev));
}

static void
umoxa1150_childdet(device_t self, device_t child)
{
	struct umoxa1150_softc *sc = device_private(self);

	sc->sc_subdev = NULL;
}

static int
umoxa1150_detach(device_t self, int flags)
{
	struct umoxa1150_softc *sc = device_private(self);
	int error = 0;

	mutex_enter(&sc->sc_lock);
	sc->sc_dying = true;
	umoxa1150_close_intr(sc);
	mutex_exit(&sc->sc_lock);
	if (sc->sc_subdev != NULL) {
		error = config_detach(sc->sc_subdev, flags);
		if (error)
			return error;
		sc->sc_subdev = NULL;
	}
	mutex_destroy(&sc->sc_lock);
	mutex_destroy(&sc->sc_status_lock);
	usbd_add_drv_event(USB_EVENT_DRIVER_DETACH, sc->sc_udev, self);
	return 0;
}
