/* Origin: EmberBSD Realtek USB SD host adapter (AI-assisted). */
/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Realtek RTS5129 USB SD host controller.
 *
 * Register definitions and hardware sequencing were checked against the
 * BSD-licensed RealtekCardReader USB controller by FireWolf, revision
 * 1d8c7f3bd95cfa811ff277426e2b5c79178036bf. This driver uses the NetBSD
 * sdmmc host contract. The sdmmc bus owns card discovery and SD commands.
 *
 * Initial support uses 3.3 V, one data line and the 512-byte ping-pong
 * buffer. The sdmmc bus splits larger requests into single blocks.
 */
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/atomic.h>
#include <sys/device.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/mutex.h>
#include <sys/systm.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdivar.h>
#include <dev/usb/usbdi_util.h>
#include <dev/usb/urtsx_proto.h>
#include <dev/usb/urtsxreg.h>

#include <dev/sdmmc/sdmmcchip.h>
#include <dev/sdmmc/sdmmcreg.h>
#include <dev/sdmmc/sdmmcvar.h>

#define URTSX_TIMEOUT 4000

struct urtsx_reg {
	uint16_t address;
	uint8_t mask, value;
};

struct urtsx_softc {
	device_t sc_dev, sc_sdmmc;
	struct usbd_device *sc_udev;
	struct usbd_pipe *sc_in, *sc_out;
	struct usbd_xfer *sc_ixfer, *sc_oxfer;
	kmutex_t sc_lock;
	struct usb_task sc_poll;
	unsigned sc_dying, sc_status;
	unsigned sc_recovery_wait;
	bool sc_ready, sc_failed, sc_unready, sc_powered;
	bool sc_power_dirty;
	uint8_t sc_package;
	uint8_t sc_command[URTSX_CMD_SIZE];
	uint8_t sc_response[256];
};

static int urtsx_match(device_t, cfdata_t, void *);
static void urtsx_attach(device_t, device_t, void *);
static int urtsx_detach(device_t, int);
static void urtsx_childdetached(device_t, device_t);
static void urtsx_poll(void *);
static int urtsx_host_reset(sdmmc_chipset_handle_t);
static uint32_t urtsx_host_ocr(sdmmc_chipset_handle_t);
static int urtsx_maxblklen(sdmmc_chipset_handle_t);
static int urtsx_card_detect(sdmmc_chipset_handle_t);
static int urtsx_write_protect(sdmmc_chipset_handle_t);
static int urtsx_bus_power(sdmmc_chipset_handle_t, uint32_t);
static int urtsx_bus_clock(sdmmc_chipset_handle_t, int);
static int urtsx_bus_width(sdmmc_chipset_handle_t, int);
static int urtsx_bus_rod(sdmmc_chipset_handle_t, int);
static void urtsx_exec_command(sdmmc_chipset_handle_t, struct sdmmc_command *);
static void urtsx_card_intr(sdmmc_chipset_handle_t, int);
static void urtsx_ack_intr(sdmmc_chipset_handle_t);

CFATTACH_DECL2_NEW(urtsx, sizeof(struct urtsx_softc), urtsx_match,
    urtsx_attach, urtsx_detach, NULL, NULL, urtsx_childdetached);

static struct sdmmc_chip_functions urtsx_functions = {
	.host_reset = urtsx_host_reset,
	.host_ocr = urtsx_host_ocr,
	.host_maxblklen = urtsx_maxblklen,
	.card_detect = urtsx_card_detect,
	.write_protect = urtsx_write_protect,
	.bus_power = urtsx_bus_power,
	.bus_clock = urtsx_bus_clock,
	.bus_width = urtsx_bus_width,
	.bus_rod = urtsx_bus_rod,
	.exec_command = urtsx_exec_command,
	.card_enable_intr = urtsx_card_intr,
	.card_intr_ack = urtsx_ack_intr,
};

/* The host mutex owns both USB pipes and their reusable transfers. */
static int
urtsx_bulk(struct urtsx_softc *sc, bool input, void *data, size_t length)
{
	struct usbd_xfer *xfer = input ? sc->sc_ixfer : sc->sc_oxfer;
	usbd_status status;
	uint32_t actual;

	KASSERT(mutex_owned(&sc->sc_lock));
	if (atomic_load_relaxed(&sc->sc_dying))
		return ENXIO;
	usbd_setup_xfer(xfer, NULL, data, length, 0, URTSX_TIMEOUT, NULL);
	status = usbd_sync_transfer(xfer);
	usbd_get_xfer_status(xfer, NULL, NULL, &actual, NULL);
	if (status != USBD_NORMAL_COMPLETION || actual != length) {
		sc->sc_failed = true;
		if (status == USBD_CANCELLED || status == USBD_NOT_STARTED)
			sc->sc_unready = true;
		if (status == USBD_STALLED)
			(void)usbd_clear_endpoint_stall(input ? sc->sc_in : sc->sc_out);
		return status == USBD_TIMEOUT ? ETIMEDOUT : EIO;
	}
	return 0;
}

static int
urtsx_submit(struct urtsx_softc *sc, struct urtsx_cmd *packet)
{
	size_t command_length, response_length;
	int error;

	if (sc->sc_failed || sc->sc_unready)
		return EIO;
	error = urtsx_cmd_finish(packet, URTSX_STAGE_CR, &command_length);
	if (error)
		return error;
	error = urtsx_cmd_response_size(packet, &response_length);
	if (error)
		return error;
	error = urtsx_bulk(sc, false, packet->buffer, command_length);
	if (error || response_length == 0)
		return error;
	return urtsx_bulk(sc, true, sc->sc_response, response_length);
}

static int
urtsx_write(struct urtsx_softc *sc, const struct urtsx_reg *registers,
    size_t count)
{
	struct urtsx_cmd packet;
	int error;

	error = urtsx_cmd_init(&packet, sc->sc_command, sizeof(sc->sc_command));
	for (size_t i = 0; !error && i < count; i++)
		error = urtsx_cmd_add(&packet, 1, registers[i].address,
		    registers[i].mask, registers[i].value);
	return error ? error : urtsx_submit(sc, &packet);
}

static int
urtsx_read(struct urtsx_softc *sc, const uint16_t *registers, size_t count)
{
	struct urtsx_cmd packet;
	int error;

	error = urtsx_cmd_init(&packet, sc->sc_command, sizeof(sc->sc_command));
	for (size_t i = 0; !error && i < count; i++)
		error = urtsx_cmd_add(&packet, 0, registers[i], 0, 0);
	return error ? error : urtsx_submit(sc, &packet);
}

static int
urtsx_control_write(struct urtsx_softc *sc, const struct urtsx_reg *reg)
{
	usb_device_request_t request;
	usbd_status status;

	if (atomic_load_relaxed(&sc->sc_dying))
		return ENXIO;
	memset(&request, 0, sizeof(request));
	request.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	USETW(request.wValue, bswap16(reg->address | 0xc000));
	USETW(request.wIndex, reg->value << 8 | reg->mask);
	status = usbd_do_request_flags(sc->sc_udev, &request, NULL, 0, NULL,
	    URTSX_TIMEOUT);
	if (status != USBD_NORMAL_COMPLETION) {
		sc->sc_unready = true;
		return EIO;
	}
	return 0;
}

/* Control writes can stop a failed command engine without reusing its queue. */
static int
urtsx_recover(struct urtsx_softc *sc)
{
	static const struct urtsx_reg registers[] = {
		{URTSX_CARD_STOP, 0x44, 0x44}, /* stop SD and clear its error */
		{URTSX_SFSM, 0xf8, 0xf8}, /* command state machine */
		{URTSX_FIFO_CTL, 0x01, 0x01}, /* flush controller FIFO */
		{URTSX_DMA_RESET, 0x01, 0x01}, /* reset controller DMA */
	};
	int error;

	for (size_t i = 0; i < __arraycount(registers); i++) {
		error = urtsx_control_write(sc, &registers[i]);
		if (error)
			return error;
	}
	sc->sc_failed = false;
	sc->sc_unready = false;
	return 0;
}

static int
urtsx_legacy_setup_locked(struct urtsx_softc *sc)
{
	static const struct urtsx_reg registers[] = {
		{URTSX_SD_CFG1, 0x0f, 0}, {URTSX_CARD_CLK_SOURCE, 0xff, 0x24},
		{URTSX_SD_PUSH_CTL, 0xff, 0}, {URTSX_SD_SAMPLE_CTL, 0x08, 0},
	};

	return urtsx_write(sc, registers, __arraycount(registers));
}

/* OFF uses EP0 even when the bulk engine is unready. It never powers a card on. */
static int
urtsx_power_off_locked(struct urtsx_softc *sc)
{
	static const struct urtsx_reg off[] = {
		{URTSX_CARD_OUTPUT, 4, 0}, {URTSX_CARD_CLOCK, 4, 0},
		{URTSX_CARD_POWER, 3, 3}, {URTSX_CARD_POWER, 0x0f, 0x0b},
	};
	static const uint8_t qfn[] = {0x65, 0x55, 0x95, 0x55, 0x56, 0x59};
	static const uint8_t lqfp[] = {0x55, 0x55, 0x95, 0x55, 0x55, 0xa5};
	struct urtsx_reg pull;
	int error = 0, next;

	sc->sc_power_dirty = true;
	for (size_t i = 0; i < __arraycount(off); i++) {
		next = urtsx_control_write(sc, &off[i]);
		if (!error)
			error = next;
	}
	for (size_t i = 0; i < __arraycount(qfn); i++) {
		pull = (struct urtsx_reg){URTSX_CARD_PULL_CTL1 + i, 0xff,
		    sc->sc_package ? lqfp[i] : qfn[i]};
		next = urtsx_control_write(sc, &pull);
		if (!error)
			error = next;
	}
	if (!error) {
		sc->sc_powered = false;
		sc->sc_power_dirty = false;
	}
	return error;
}

static int
urtsx_initialize(struct urtsx_softc *sc)
{
	static const struct urtsx_reg power[] = {{URTSX_FPDCTL, 1, 0}};
	static const struct urtsx_reg clock[] = {{URTSX_CLK_DIV, 0x80, 0}};
	static const uint16_t identity[] = {
		URTSX_HW_VERSION, URTSX_CARD_SHARE, URTSX_CFG_MODE0, URTSX_CFG_MODE1,
	};
	static const struct urtsx_reg reset[] = {
		{URTSX_DUMMY0, 1, 1}, {URTSX_CD_DEGLITCH_WIDTH, 0xff, 8}, {URTSX_CD_DEGLITCH_CTL, 4, 0},
		{URTSX_SD30_DRIVE, 7, 1}, {URTSX_CARD_DRIVE, 3, 0}, {URTSX_LDO_POWER_CFG, 0xe0, 0},
		{URTSX_CARD_DMA1_CTL, 2, 2}, {URTSX_CARD_INT_PENDING, 0x1c, 0x1c},
	};
	static const struct urtsx_reg lqfp[] = {
		{URTSX_CARD_POWER, 0x0c, 8}, {URTSX_CARD_POWER, 0x60, 0x60},
		{URTSX_CARD_PULL_CTL1, 0x30, 0x10},
		{URTSX_CARD_PULL_CTL5, 3, 1}, {URTSX_CARD_PULL_CTL6, 0x0c, 4},
	};
	static const struct urtsx_reg rts5179[] = {{URTSX_CARD_PULL_CTL5, 3, 1}};
	static const struct urtsx_reg noncrystal[] = {
		{URTSX_HS_PHY_DATA, 0xff, 0x7c}, {URTSX_HS_PHY_CTL, 0xff, 2},
		{URTSX_HS_PHY_LOAD, 0xff, 0}, {URTSX_HS_PHY_LOAD, 0xff, 0},
		{URTSX_HS_PHY_LOAD, 0xff, 1},
		{URTSX_HS_PHY_CTL, 0xff, 0x0c},
		{URTSX_HS_PHY_LOAD, 0xff, 0}, {URTSX_HS_PHY_LOAD, 0xff, 0},
		{URTSX_HS_PHY_LOAD, 0xff, 1},
	};
	uint8_t revision, mode0, mode1;
	int error;

	if ((error = urtsx_recover(sc)) != 0 ||
	    (error = urtsx_write(sc, power, __arraycount(power))) != 0)
		return error;
	usbd_delay_ms(sc->sc_udev, 1);
	if ((error = urtsx_write(sc, clock, __arraycount(clock))) != 0 ||
	    (error = urtsx_read(sc, identity, __arraycount(identity))) != 0)
		return error;
	revision = sc->sc_response[0] & 15;
	sc->sc_package = sc->sc_response[1] & 4;
	mode0 = sc->sc_response[2];
	mode1 = sc->sc_response[3];
	aprint_normal_dev(sc->sc_dev, "revision %u, %s, mode %02x/%02x\n",
	    revision, sc->sc_package ? "LQFP48" : "QFN24", mode0, mode1);
	if (sc->sc_package &&
	    (error = urtsx_write(sc, lqfp, __arraycount(lqfp))) != 0)
		return error;
	if ((error = urtsx_write(sc, reset, __arraycount(reset))) != 0)
		return error;
	if ((mode1 & 2) &&
	    (error = urtsx_write(sc, rts5179, __arraycount(rts5179))) != 0)
		return error;
	if (((mode0 & 0x80) || (mode0 & 3) == 1) &&
	    (error = urtsx_write(sc, noncrystal, __arraycount(noncrystal))) != 0)
		return error;
	return urtsx_legacy_setup_locked(sc);
}

static int
urtsx_match(device_t parent, cfdata_t cf, void *aux)
{
	struct usbif_attach_arg *uiaa = aux;
	usb_interface_descriptor_t *id = usbd_get_interface_descriptor(uiaa->uiaa_iface);

	if (uiaa->uiaa_vendor == 0x0bda && uiaa->uiaa_product == 0x0129 &&
	    id->bInterfaceNumber == 0 && id->bInterfaceClass == 0xff &&
	    id->bInterfaceSubClass == 6 && id->bInterfaceProtocol == 0x50)
		return UMATCH_VENDOR_PRODUCT;
	return UMATCH_NONE;
}

static void
urtsx_attach(device_t parent, device_t self, void *aux)
{
	struct urtsx_softc *sc = device_private(self);
	struct usbif_attach_arg *uiaa = aux;
	struct sdmmcbus_attach_args saa;
	usb_interface_descriptor_t *id;
	usb_endpoint_descriptor_t *ed;
	uint8_t input = 0, output = 0;
	unsigned input_count = 0, output_count = 0;
	int error;

	sc->sc_dev = self;
	sc->sc_udev = uiaa->uiaa_device;
	mutex_init(&sc->sc_lock, MUTEX_DEFAULT, IPL_NONE);
	usb_init_task(&sc->sc_poll, urtsx_poll, sc, USB_TASKQ_MPSAFE);
	aprint_naive("\n");
	aprint_normal(": Realtek RTS5129 USB SD host\n");
	id = usbd_get_interface_descriptor(uiaa->uiaa_iface);
	for (unsigned i = 0; i < id->bNumEndpoints; i++) {
		ed = usbd_interface2endpoint_descriptor(uiaa->uiaa_iface, i);
		if (ed == NULL || UE_GET_XFERTYPE(ed->bmAttributes) != UE_BULK)
			continue;
		if (UE_GET_ADDR(ed->bEndpointAddress) == 0)
			goto fail;
		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN) {
			input = ed->bEndpointAddress;
			input_count++;
		} else {
			output = ed->bEndpointAddress;
			output_count++;
		}
	}
	if (input_count != 1 || output_count != 1)
		goto fail;
	if (usbd_open_pipe(uiaa->uiaa_iface, input,
	    USBD_EXCLUSIVE_USE | USBD_MPSAFE, &sc->sc_in) ||
	    usbd_open_pipe(uiaa->uiaa_iface, output,
	    USBD_EXCLUSIVE_USE | USBD_MPSAFE, &sc->sc_out))
		goto fail;
	if (usbd_create_xfer(sc->sc_in, URTSX_CMD_SIZE, 0, 0, &sc->sc_ixfer) ||
	    usbd_create_xfer(sc->sc_out, URTSX_CMD_SIZE, 0, 0, &sc->sc_oxfer))
		goto fail;
	mutex_enter(&sc->sc_lock);
	error = urtsx_initialize(sc);
	mutex_exit(&sc->sc_lock);
	if (error) {
		aprint_error_dev(self, "controller initialization failed: %d\n", error);
		goto fail;
	}
	sc->sc_ready = true;
	urtsx_poll(sc);
	memset(&saa, 0, sizeof(saa));
	saa.saa_busname = "sdmmc";
	saa.saa_sct = &urtsx_functions;
	saa.saa_sch = sc;
	saa.saa_clkmin = 400;
	saa.saa_clkmax = 25000;
	saa.saa_caps = SMC_CAPS_SINGLE_ONLY | SMC_CAPS_POLL_CARD_DET |
	    SMC_CAPS_NO_WRITE_RETRY;
	sc->sc_sdmmc = config_found(self, &saa, NULL, CFARGS_NONE);
	return;
fail:
	aprint_error_dev(self, "USB SD host is unavailable\n");
	if (sc->sc_ixfer) usbd_destroy_xfer(sc->sc_ixfer);
	if (sc->sc_oxfer) usbd_destroy_xfer(sc->sc_oxfer);
	if (sc->sc_in) usbd_close_pipe(sc->sc_in);
	if (sc->sc_out) usbd_close_pipe(sc->sc_out);
	sc->sc_ixfer = sc->sc_oxfer = NULL;
	sc->sc_in = sc->sc_out = NULL;
}

static void
urtsx_poll(void *arg)
{
	struct urtsx_softc *sc = arg;
	static const uint16_t registers[] = {URTSX_CARD_EXIST, URTSX_OCP_STATUS};
	uint16_t status;

	mutex_enter(&sc->sc_lock);
	if (!sc->sc_ready || atomic_load_relaxed(&sc->sc_dying)) {
		mutex_exit(&sc->sc_lock);
		return;
	}
	if (sc->sc_failed || sc->sc_unready) {
		if (sc->sc_recovery_wait != 0) {
			sc->sc_recovery_wait--;
			mutex_exit(&sc->sc_lock);
			return;
		}
		/* Reset only transport state. Never replay an SD command here. */
		if (urtsx_recover(sc)) {
			sc->sc_recovery_wait = 5;
			mutex_exit(&sc->sc_lock);
			return;
		}
	}
	if (urtsx_read(sc, registers, __arraycount(registers)) ||
	    urtsx_bulk_status_decode(sc->sc_response, 2, &status)) {
		mutex_exit(&sc->sc_lock);
		return;
	}
	atomic_store_relaxed(&sc->sc_status, status);
	mutex_exit(&sc->sc_lock);
	/* The sdmmc polling callout observes this cache on its next tick. */
}

static int
urtsx_card_detect(sdmmc_chipset_handle_t handle)
{
	struct urtsx_softc *sc = handle;

	/* sdmmc calls this from its polling callout. USB work must sleep elsewhere. */
	if (atomic_load_relaxed(&sc->sc_dying))
		return 0;
	usb_add_task(sc->sc_udev, &sc->sc_poll, USB_TASKQ_DRIVER);
	return (atomic_load_relaxed(&sc->sc_status) & 0x31) == URTSX_SD_CD;
}

static int
urtsx_write_protect(sdmmc_chipset_handle_t handle)
{
	struct urtsx_softc *sc = handle;

	return (atomic_load_relaxed(&sc->sc_status) & URTSX_SD_WP) != 0;
}

static uint32_t
urtsx_host_ocr(sdmmc_chipset_handle_t handle)
{
	(void)handle;
	return MMC_OCR_3_2V_3_3V | MMC_OCR_3_3V_3_4V;
}

static int
urtsx_maxblklen(sdmmc_chipset_handle_t handle)
{
	return 512;
}

static int
urtsx_host_reset(sdmmc_chipset_handle_t handle)
{
	struct urtsx_softc *sc = handle;
	int error;

	mutex_enter(&sc->sc_lock);
	sc->sc_unready = false;
	sc->sc_power_dirty = true;
	error = urtsx_initialize(sc);
	mutex_exit(&sc->sc_lock);
	return error;
}

static int
urtsx_bus_power(sdmmc_chipset_handle_t handle, uint32_t ocr)
{
	struct urtsx_softc *sc = handle;
	static const struct urtsx_reg select[] = {
		{URTSX_CARD_SELECT, 7, 2}, {URTSX_CARD_SHARE, 3, 1}, {URTSX_CARD_CLOCK, 4, 4},
		{URTSX_SD_PAD_CTL, 0x80, 0}, {URTSX_LDO_POWER_CFG, 0x1c, 0x1c},
	};
	static const uint8_t qfn[] = {0xa5, 0x9a, 0xa5, 0x9a, 0x65, 0x5a};
	static const uint8_t lqfp[] = {0xaa, 0xaa, 0xa9, 0x55, 0x55, 0xa5};
	static const struct urtsx_reg partial[] = {{URTSX_CARD_POWER, 3, 2}};
	static const struct urtsx_reg full[] = {{URTSX_CARD_POWER, 0x0f, 0}};
	static const struct urtsx_reg output[] = {{URTSX_CARD_OUTPUT, 4, 4}};
	struct urtsx_reg pulls[6];
	int error;

	if (ocr && !(ocr & urtsx_host_ocr(sc)))
		return EINVAL;
	mutex_enter(&sc->sc_lock);
	if (!ocr) {
		error = urtsx_power_off_locked(sc);
		goto out;
	}
	if (sc->sc_unready || (ocr && (atomic_load_relaxed(&sc->sc_status) & 0x30))) {
		error = EIO;
		goto out;
	}
	if (sc->sc_failed && (error = urtsx_recover(sc)) != 0)
		goto out;
	if (sc->sc_power_dirty && (error = urtsx_power_off_locked(sc)) != 0)
		goto out;
	if (sc->sc_powered) {
		error = 0;
		goto out;
	}
	for (unsigned i = 0; i < 6; i++) {
		pulls[i].address = URTSX_CARD_PULL_CTL1 + i;
		pulls[i].mask = 0xff;
		pulls[i].value = sc->sc_package ? lqfp[i] : qfn[i];
	}
	if ((error = urtsx_write(sc, select, __arraycount(select))) != 0 ||
	    (error = urtsx_legacy_setup_locked(sc)) != 0 ||
	    (error = urtsx_write(sc, pulls, __arraycount(pulls))) != 0)
		goto rollback;
	/* A timed-out power request can still have reached the controller. */
	sc->sc_powered = true;
	sc->sc_power_dirty = true;
	if ((error = urtsx_write(sc, partial, __arraycount(partial))) != 0)
		goto rollback;
	usbd_delay_ms(sc->sc_udev, 5);
	if ((error = urtsx_write(sc, full, __arraycount(full))) != 0 ||
	    (error = urtsx_write(sc, output, __arraycount(output))) != 0)
		goto rollback;
	sc->sc_powered = true;
	sc->sc_power_dirty = false;
	goto out;
rollback:
	(void)urtsx_power_off_locked(sc);
out:
	mutex_exit(&sc->sc_lock);
	return error;
}

static int
urtsx_bus_clock(sdmmc_chipset_handle_t handle, int frequency)
{
	struct urtsx_softc *sc = handle;
	struct urtsx_clock clock;
	struct urtsx_reg registers[8];
	static const struct urtsx_reg stable[] = {{URTSX_CLK_DIV, 0x80, 0}};
	static const struct urtsx_reg off[] = {{URTSX_CARD_CLOCK, 4, 0}};
	int error;

	if (frequency < 0)
		return EINVAL;
	if (frequency && (error = urtsx_clock_params(frequency, &clock)) != 0)
		return error;
	mutex_enter(&sc->sc_lock);
	if (!frequency) {
		error = urtsx_write(sc, off, __arraycount(off));
		goto out;
	}
	registers[0] = (struct urtsx_reg){URTSX_SD_CFG1, 0xc0, clock.sd_div};
	registers[1] = (struct urtsx_reg){URTSX_CLK_DIV, 0x80, 0x80};
	registers[2] = (struct urtsx_reg){URTSX_CLK_DIV, 0x3f, clock.clk_div};
	registers[3] = (struct urtsx_reg){URTSX_SSC_CTL1, 0x80, 0};
	registers[4] = (struct urtsx_reg){URTSX_SSC_CTL2, 3, clock.depth};
	registers[5] = (struct urtsx_reg){URTSX_SSC_DIV_N, 0xff, clock.n};
	registers[6] = (struct urtsx_reg){URTSX_SSC_CTL1, 0xff, 0xd0};
	registers[7] = (struct urtsx_reg){URTSX_CARD_CLOCK, 4, 4};
	error = urtsx_write(sc, registers, __arraycount(registers));
	if (!error) {
		usbd_delay_ms(sc->sc_udev, 1);
		error = urtsx_write(sc, stable, __arraycount(stable));
	}
out:
	mutex_exit(&sc->sc_lock);
	return error;
}

static int
urtsx_bus_width(sdmmc_chipset_handle_t handle, int width)
{
	struct urtsx_softc *sc = handle;
	int error;

	if (width != 1)
		return EINVAL;
	mutex_enter(&sc->sc_lock);
	error = urtsx_legacy_setup_locked(sc);
	mutex_exit(&sc->sc_lock);
	return error;
}

static int
urtsx_bus_rod(sdmmc_chipset_handle_t handle, int rod)
{
	/* Native SD command signalling is controlled by the controller. */
	return 0;
}

static void
urtsx_exec_command(sdmmc_chipset_handle_t handle, struct sdmmc_command *command)
{
	struct urtsx_softc *sc = handle;
	struct urtsx_cmd packet;
	uint16_t registers[128];
	uint8_t cfg2;
	size_t offset, length;
	bool write;
	int error;

	command->c_resid = command->c_datalen;
	write = command->c_opcode == 24 && command->c_data != NULL &&
	    !(command->c_flags & SCF_CMD_READ) && command->c_datalen == 512 &&
	    command->c_blklen == 512 &&
	    (command->c_flags & SCF_RSP_MASK) == SCF_RSP_R1 &&
	    (command->c_flags & SCF_CMD_MASK) == SCF_CMD_ADTC;
	if ((!urtsx_sd_readonly_opcode(command->c_opcode) && !write) ||
	    (command->c_data && !(command->c_flags & SCF_CMD_READ) && !write)) {
		error = ENOTSUP;
		goto done;
	}
	if (command->c_datalen < 0 || command->c_datalen > 512 ||
	    (command->c_datalen != 0 && command->c_data == NULL)) {
		error = EINVAL;
		goto done;
	}
	if (!(command->c_flags & SCF_RSP_PRESENT))
		cfg2 = 4;
	else if (command->c_flags & SCF_RSP_136)
		cfg2 = 2;
	else if (command->c_flags & SCF_RSP_BSY)
		cfg2 = 9;
	else
		cfg2 = command->c_flags & SCF_RSP_CRC ? 1 : 5;
	mutex_enter(&sc->sc_lock);
	if (write) {
		unsigned status = atomic_load_relaxed(&sc->sc_status);
		if (!(status & URTSX_SD_CD)) {
			error = ENXIO;
			goto unlock;
		}
		if (status & URTSX_SD_WP) {
			error = EROFS;
			goto unlock;
		}
		if (status & URTSX_OCP_MASK) {
			error = EIO;
			goto unlock;
		}
	}
	if (sc->sc_unready) {
		error = EIO;
		goto unlock;
	}
	if (sc->sc_failed && (error = urtsx_recover(sc)) != 0)
		goto unlock;
	error = urtsx_cmd_init(&packet, sc->sc_command, sizeof(sc->sc_command));
	if (!error)
		error = urtsx_sd_command(&packet, command->c_opcode, command->c_arg,
		    cfg2, write ? 0 : command->c_datalen);
	if (!error)
		error = urtsx_submit(sc, &packet);
	if (!error) {
		(void)urtsx_cmd_response_size(&packet, &length);
		error = urtsx_sd_response(sc->sc_response, length, cfg2, command->c_resp);
	}
	if (write) {
		static const struct urtsx_reg flush = {
			URTSX_FIFO_CTL, URTSX_FIFO_FLUSH, URTSX_FIFO_FLUSH,
		};
		/* Send CMD24 exactly once. A later error can mean partial success. */
		if (!error && SD_R1_WRITE_REJECTED(command->c_resp[0]))
			error = EIO;
		if (!error)
			error = urtsx_ppbuf_write_packet(sc->sc_command,
			    sizeof(sc->sc_command), command->c_data, 512, &length);
		if (!error)
			error = urtsx_bulk(sc, false, sc->sc_command, length);
		if (!error)
			error = urtsx_cmd_init(&packet, sc->sc_command,
			    sizeof(sc->sc_command));
		if (!error)
			error = urtsx_sd_write_data(&packet);
		if (!error)
			error = urtsx_submit(sc, &packet);
		if (!error)
			error = urtsx_sd_write_status(sc->sc_response, 4);
		if (!error)
			error = urtsx_control_write(sc, &flush);
		if (!error)
			command->c_resid = 0;
		goto transfer_done;
	}
	for (offset = 0; !error && offset < (size_t)command->c_datalen; offset += length) {
		length = MIN(sizeof(registers) / sizeof(registers[0]),
		    command->c_datalen - offset);
		for (size_t i = 0; i < length; i++)
			registers[i] = URTSX_PPBUF + offset + i;
		error = urtsx_read(sc, registers, length);
		if (!error) {
			memcpy((uint8_t *)command->c_data + offset, sc->sc_response, length);
			command->c_resid -= length;
		}
	}
transfer_done:
	if (error)
		sc->sc_failed = true;
unlock:
	mutex_exit(&sc->sc_lock);
done:
	command->c_error = error;
	command->c_flags |= SCF_ITSDONE;
}

static void
urtsx_card_intr(sdmmc_chipset_handle_t handle, int enable)
{
	/* SDIO interrupts are not supported. */
}

static void
urtsx_ack_intr(sdmmc_chipset_handle_t handle)
{
}

static void
urtsx_childdetached(device_t parent, device_t child)
{
	struct urtsx_softc *sc = device_private(parent);

	if (sc->sc_sdmmc == child)
		sc->sc_sdmmc = NULL;
}

static int
urtsx_detach(device_t self, int flags)
{
	struct urtsx_softc *sc = device_private(self);
	int error;

	atomic_store_relaxed(&sc->sc_dying, 1);
	if (sc->sc_in) usbd_abort_pipe(sc->sc_in);
	if (sc->sc_out) usbd_abort_pipe(sc->sc_out);
	error = config_detach_children(self, flags);
	if (error) {
		atomic_store_relaxed(&sc->sc_dying, 0);
		return error;
	}
	/* Child detach stops the callout which can enqueue our polling task. */
	(void)usb_rem_task(sc->sc_udev, &sc->sc_poll);
	usb_rem_task_wait(sc->sc_udev, &sc->sc_poll, USB_TASKQ_DRIVER, NULL);
	if (sc->sc_ixfer) usbd_destroy_xfer(sc->sc_ixfer);
	if (sc->sc_oxfer) usbd_destroy_xfer(sc->sc_oxfer);
	if (sc->sc_in) usbd_close_pipe(sc->sc_in);
	if (sc->sc_out) usbd_close_pipe(sc->sc_out);
	mutex_destroy(&sc->sc_lock);
	return 0;
}
