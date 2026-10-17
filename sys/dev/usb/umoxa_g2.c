/* Origin: EmberBSD independent UPort1450-G2 USB parent, 2026-10-13. */
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
#include <dev/usb/umoxa_g2io.h>
#include <dev/usb/umoxa_g2var.h>

static int umoxag_match(device_t, cfdata_t, void *);
static void umoxag_attach(device_t, device_t, void *);
static int umoxag_detach(device_t, int);
static void umoxag_childdet(device_t, device_t);
static int umoxag_control(void *, int, uint8_t, uint16_t, uint16_t, uint8_t *, size_t);
static void umoxag_delay(void *, unsigned);
CFATTACH_DECL2_NEW(umoxag, sizeof(struct umoxa_g2_softc), umoxag_match,
    umoxag_attach, umoxag_detach, NULL, NULL, umoxag_childdet);

static int
umoxag_match(device_t parent, cfdata_t match, void *aux)
{
	struct usb_attach_arg *uaa = aux;

	return uaa->uaa_vendor == 0x110a && uaa->uaa_product == 0x1452 ?
	    UMATCH_VENDOR_PRODUCT : UMATCH_NONE;
}

static int
umoxag_control(void *arg, int input, uint8_t command, uint16_t value,
    uint16_t index, uint8_t *data, size_t size)
{
	struct umoxa_g2_softc *sc = arg;
	usb_device_request_t req;
	usbd_status status;
	int actual = 0;
	bool dying;

	mutex_enter(&sc->sc_lock);
	dying = sc->sc_dying;
	mutex_exit(&sc->sc_lock);
	if (dying)
		return ENXIO;
	if (size > 64)
		return EINVAL;
	memset(&req, 0, sizeof(req));
	req.bmRequestType = input ? UT_READ_VENDOR_DEVICE : UT_WRITE_VENDOR_DEVICE;
	req.bRequest = command;
	USETW(req.wValue, value);
	USETW(req.wIndex, index);
	USETW(req.wLength, size);
	status = usbd_do_request_flags(sc->sc_udev, &req, data, 0, &actual, 1000);
	return status == USBD_NORMAL_COMPLETION && actual == (int)size ? 0 : EIO;
}

static void
umoxag_delay(void *arg, unsigned ms)
{
	struct umoxa_g2_softc *sc = arg;

	usbd_delay_ms(sc->sc_udev, ms);
}

static int
umoxag_firmware(struct umoxa_g2_softc *sc)
{
	firmware_handle_t fw;
	uint8_t *image;
	uint32_t fw_version, expected;
	off_t size;
	int error, remap, stop_error, attempt;

	error = umoxa_g2_probe(&sc->sc_io, &fw_version, &remap);
	if (error)
		return error;
	if (remap == 2 && fw_version != 0 && fw_version != 0xffffff)
		return 0;
	if (!root_is_mounted)
		return EWOULDBLOCK;
	error = firmware_open("moxa", "moxa-1450-g2.fw", &fw);
	if (error)
		return error;
	size = firmware_get_size(fw);
	if (size < 0x8b || size > UMOXA_G2_FIRMWARE_MAX) {
		firmware_close(fw);
		return EINVAL;
	}
	image = kmem_alloc((size_t)size, KM_SLEEP);
	error = firmware_read(fw, 0, image, (size_t)size);
	if (error == 0)
		error = umoxa_g2_firmware_version(image, (size_t)size, &expected);
	if (error == 0)
		error = umoxa_g2_download(&sc->sc_io, image, (size_t)size, &stop_error);
	kmem_free(image, (size_t)size);
	firmware_close(fw);
	if (error)
		return error;
	for (attempt = 0; attempt < 5; attempt++) {
		usbd_delay_ms(sc->sc_udev, 100);
		error = umoxa_g2_probe(&sc->sc_io, &fw_version, &remap);
		if (error == EIO || error == ENXIO)
			return EAGAIN;
		if (error)
			return error;
		if (remap == 2 && fw_version == expected) {
			if (stop_error)
				aprint_normal_dev(sc->sc_dev,
				    "STOP status %d; firmware version/SRAM confirmed\n", stop_error);
			return 0;
		}
	}
	return ETIMEDOUT;
}

static int
umoxag_layout(struct umoxa_g2_softc *sc)
{
	usb_device_descriptor_t *dd;
	usb_config_descriptor_t *cd;
	usb_interface_descriptor_t *id;
	usb_endpoint_descriptor_t *ed;
	struct umoxa_g2_endpoint ep[3];
	unsigned i;

	dd = usbd_get_device_descriptor(sc->sc_udev);
	if (dd == NULL || dd->bNumConfigurations == 0 || dd->bNumConfigurations > 2)
		return EPROTO;
	cd = usbd_get_config_descriptor(sc->sc_udev);
	if (cd == NULL || cd->bConfigurationValue == 0) {
		if (usbd_set_config_index(sc->sc_udev, 0, 1) != USBD_NORMAL_COMPLETION)
			return EIO;
		cd = usbd_get_config_descriptor(sc->sc_udev);
	}
	if (cd == NULL || cd->bLength < USB_CONFIG_DESCRIPTOR_SIZE ||
	    cd->bDescriptorType != UDESC_CONFIG || UGETW(cd->wTotalLength) < 39 ||
	    cd->bNumInterface != 1 ||
	    usbd_device2interface_handle(sc->sc_udev, 0, &sc->sc_iface) != USBD_NORMAL_COMPLETION)
		return EPROTO;
	id = usbd_get_interface_descriptor(sc->sc_iface);
	if (id == NULL || id->bNumEndpoints != 3 || id->bInterfaceClass != UICLASS_VENDOR)
		return EPROTO;
	for (i = 0; i < 3; i++) {
		ed = usbd_interface2endpoint_descriptor(sc->sc_iface, i);
		if (ed == NULL)
			return EPROTO;
		ep[i] = (struct umoxa_g2_endpoint){ed->bEndpointAddress,
		    UE_GET_XFERTYPE(ed->bmAttributes), UGETW(ed->wMaxPacketSize)};
	}
	return umoxa_g2_endpoints(ep, 3, &sc->sc_layout);
}

static void
umoxag_attach(device_t parent, device_t self, void *aux)
{
	struct usb_attach_arg *uaa = aux;
	struct umoxa_g2_softc *sc = device_private(self);
	struct ucom_attach_args a;
	unsigned p;
	int error;

	aprint_naive("\n");
	aprint_normal("\n");
	sc->sc_dev = self;
	sc->sc_udev = uaa->uaa_device;
	mutex_init(&sc->sc_lock, MUTEX_DEFAULT, IPL_SOFTUSB);
	mutex_init(&sc->sc_ctl_lock, MUTEX_DEFAULT, IPL_NONE);
	sc->sc_locks_initialized = true;
	sc->sc_io = (struct umoxa_g2_io){sc, umoxag_control, umoxag_delay};
	error = umoxag_layout(sc);
	if (error)
		goto bad;
	error = umoxag_firmware(sc);
	/* EWOULDBLOCK and EAGAIN may have the same numeric value. */
	if (!root_is_mounted && error == EWOULDBLOCK) {
		aprint_error_dev(self, "reconnect after root mount to load G2 RAM firmware\n");
		return;
	}
	if (error == EAGAIN) {
		aprint_normal_dev(self, "RAM firmware transferred; waiting for fresh USB attach\n");
		return;
	}
	if (error)
		goto bad;
	error = umoxa_g2_txq_init(&sc->sc_txq, sc->sc_layout.packet, 1000);
	if (error == 0)
		error = umoxa_g2_io_init(sc);
	if (error)
		goto bad;
	for (p = 0; p < 4; p++) {
		sc->sc_port[p].sc_settings = (struct umoxa_g2_settings){9600, 8, 0, 1, 0, 0, 3};
		memset(&a, 0, sizeof(a));
		a.ucaa_portno = p;
		a.ucaa_ibufsize = a.ucaa_ibufsizepad = sc->sc_layout.packet;
		a.ucaa_obufsize = sc->sc_layout.packet - 4;
		a.ucaa_device = sc->sc_udev;
		a.ucaa_iface = sc->sc_iface;
		a.ucaa_methods = &umoxag_methods;
		a.ucaa_arg = sc;
		a.ucaa_info = "UPort 1450-G2 (experimental USB transport)";
		sc->sc_port[p].sc_subdev = config_found(self, &a, ucomprint, CFARGS(.submatch = ucomsubmatch));
		if (sc->sc_port[p].sc_subdev == NULL) {
			error = ENXIO;
			goto bad;
		}
	}
	mutex_enter(&sc->sc_lock);
	sc->sc_ready = true;
	mutex_exit(&sc->sc_lock);
	usbd_add_drv_event(USB_EVENT_DRIVER_ATTACH, sc->sc_udev, self);
	return;
bad:
	aprint_error_dev(self, "G2 attachment failed (%d); no usable TTY\n", error);
	(void)umoxag_detach(self, DETACH_FORCE);
}

static int
umoxag_detach(device_t self, int flags)
{
	struct umoxa_g2_softc *sc = device_private(self);
	unsigned p;
	int error, first = 0;

	if (!sc->sc_locks_initialized)
		return 0;
	umoxa_g2_io_fini(sc);
	for (p = 0; p < 4; p++) {
		if (sc->sc_port[p].sc_subdev == NULL)
			continue;
		error = config_detach(sc->sc_port[p].sc_subdev, flags);
		if (error == 0)
			sc->sc_port[p].sc_subdev = NULL;
		else if (first == 0)
			first = error;
	}
	if (first)
		return first;
	mutex_destroy(&sc->sc_ctl_lock);
	mutex_destroy(&sc->sc_lock);
	sc->sc_locks_initialized = false;
	return 0;
}

static void
umoxag_childdet(device_t self, device_t child)
{
	struct umoxa_g2_softc *sc = device_private(self);
	unsigned p;

	for (p = 0; p < 4; p++)
		if (sc->sc_port[p].sc_subdev == child)
			sc->sc_port[p].sc_subdev = NULL;
}

