/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Native bootstrap only. Networking and board power control are separate. */
#ifdef _KERNEL_OPT
#include "opt_fdt.h"
#endif
#include <sys/param.h>
#include <sys/device.h>
#include <sys/endian.h>
#include <sys/mutex.h>
#include <sys/systm.h>
#include <dev/firmload.h>
#ifdef FDT
#include <dev/fdt/fdtvar.h>
#include <dev/ofw/openfirm.h>
#endif
#include <dev/ic/rtw88_io.h>
#include <dev/ic/rtw8723d_boot.h>
#include <dev/sdmmc/sdmmcchip.h>
#include <dev/sdmmc/sdmmc_ioreg.h>
#include <dev/sdmmc/sdmmcreg.h>
#include <dev/sdmmc/sdmmcvar.h>
#include <dev/sdmmc/rtw88_sdio_io.h>

#define RTWSDIO_BLOCK_SIZE	512U
#define RTWSDIO_FW_MAX		(32U + 32768U)
#define RTWSDIO_HIMR		(RTW8723D_SDIO_LOCAL_BASE + 0x14)
#define RTWSDIO_HISR		(RTW8723D_SDIO_LOCAL_BASE + 0x18)

enum rtwsdio_state {
	RTWSDIO_ATTACHED, RTWSDIO_LOADING, RTWSDIO_FW_READY,
	RTWSDIO_STOPPING, RTWSDIO_DETACHED
};

struct rtwsdio_softc {
	device_t sc_dev;
	struct sdmmc_function *sc_sf;
	/* Interlock for admission, task submission and detach; never bus I/O. */
	kmutex_t sc_lock;
	enum rtwsdio_state sc_state;
	bool sc_stopping, sc_irq_pending;
	int sc_error;
	struct sdmmc_task sc_task;
	void *sc_ih;
	struct rtw88_sdio_ops sc_ops;
	struct rtw88_sdio_bus sc_bus;
	struct rtw88_sdio_function_state sc_function;
	struct rtw8723d_boot_ops sc_boot_ops;
	uint32_t sc_scratch[RTW88_SDIO_MAX_TRANSFER / sizeof(uint32_t)];
	u_int sc_old_blklen;
	uint8_t sc_old_fbr[2];
	bool sc_block_owned, sc_chip_owned, sc_ien_owned;
	firmware_handle_t sc_fwh;
	void *sc_fwbuf;
	size_t sc_fwsize;
};

static int rtwsdio_match(device_t, cfdata_t, void *);
static void rtwsdio_attach(device_t, device_t, void *);
static int rtwsdio_detach(device_t, int);
static void rtwsdio_mountroot(device_t);
static void rtwsdio_task(void *);
static int rtwsdio_intr(void *);
static int rtwsdio_stop(struct rtwsdio_softc *);

CFATTACH_DECL_NEW(rtwsdio, sizeof(struct rtwsdio_softc),
    rtwsdio_match, rtwsdio_attach, rtwsdio_detach, NULL);

/* A configured, enabled host owns standard board power sequencing. */
static bool
rtwsdio_admitted(device_t parent)
{
#ifdef FDT
	device_t host = device_parent(parent);
	const char *path;
	int node, root;

	if (host == NULL || !prop_dictionary_get_string(device_properties(host),
	    "fdt-path", &path))
		return false;
	node = OF_finddevice(path);
	root = OF_finddevice("/");
	if (node <= 0 || root <= 0 || !fdtbus_status_okay(node) ||
	    OF_getproplen(node, "non-removable") != 0 ||
	    OF_getproplen(node, "mmc-pwrseq") != 4 ||
	    OF_getproplen(node, "ember,ys-m33-sdio-host") >= 0 ||
	    OF_getproplen(root, "ember,ys-m33-sdio-probe") >= 0 ||
	    OF_getproplen(root, "ember,ys-m33-rtl8723ds-read-probe") >= 0)
		return false;
	return fdtbus_mmc_pwrseq_get(node) != NULL;
#else
	(void)parent;
	return false;
#endif
}

static int
rtwsdio_match(device_t parent, cfdata_t cf __unused, void *aux)
{
	const struct sdmmc_attach_args *aa = aux;
	const struct sdmmc_function *sf = aa->sf;

	return sf != NULL && sf->number == 1 &&
	    sf->cis.manufacturer == 0x024c && sf->cis.product == 0xd723 &&
	    rtwsdio_admitted(parent);
}

/* These callbacks consume an already encoded request without narrowing it. */
static int
rtwsdio_command52(void *cookie, uint32_t arg, uint32_t *response)
{
	struct rtwsdio_softc *sc = cookie;
	struct sdmmc_command cmd;
	int error;

	error = sdmmc_select_card(sc->sc_sf->sc, sc->sc_sf);
	if (error != 0)
		return error;
	memset(&cmd, 0, sizeof(cmd));
	cmd.c_opcode = SD_IO_RW_DIRECT;
	cmd.c_arg = arg;
	cmd.c_flags = SCF_CMD_AC | SCF_RSP_R5;
	error = sdmmc_mmc_command(sc->sc_sf->sc, &cmd);
	if (error == 0)
		*response = cmd.c_resp[0];
	return error;
}

static int
rtwsdio_command53(void *cookie, uint32_t arg, void *data, size_t length,
    uint32_t *response)
{
	struct rtwsdio_softc *sc = cookie;
	struct sdmmc_command cmd;
	unsigned count = arg & SD_ARG_CMD53_LENGTH_MASK;
	int error;

	if (data == NULL || length == 0 || length > RTW88_SDIO_MAX_REQUEST ||
	    ((arg >> SD_ARG_CMD53_FUNC_SHIFT) & SD_ARG_CMD53_FUNC_MASK) != 1 ||
	    sc->sc_sf->blklen != RTWSDIO_BLOCK_SIZE)
		return EINVAL;
	if ((arg & SD_ARG_CMD53_BLOCK_MODE) != 0) {
		if (count == 0 || length != count * RTWSDIO_BLOCK_SIZE)
			return EINVAL;
	} else if (length != (count == 0 ? 512 : count)) {
		return EINVAL;
	}
	error = sdmmc_select_card(sc->sc_sf->sc, sc->sc_sf);
	if (error != 0)
		return error;
	memset(&cmd, 0, sizeof(cmd));
	cmd.c_opcode = SD_IO_RW_EXTENDED;
	cmd.c_arg = arg;
	cmd.c_flags = SCF_CMD_ADTC | SCF_RSP_R5 | SCF_NO_STOP;
	if ((arg & SD_ARG_CMD53_WRITE) == 0)
		cmd.c_flags |= SCF_CMD_READ;
	cmd.c_data = data;
	cmd.c_datalen = length;
	cmd.c_blklen = MIN(length, RTWSDIO_BLOCK_SIZE);
	/* A null DMA map selects the host's normal SDIO bounce-buffer path. */
	error = sdmmc_mmc_command(sc->sc_sf->sc, &cmd);
	if (error == 0)
		*response = cmd.c_resp[0];
	return error;
}

static void
rtwsdio_wait(void *cookie __unused, unsigned ms)
{

	sdmmc_pause(ms * 1000, NULL);
}

static void
rtwsdio_delay(void *cookie __unused, unsigned us)
{

	delay(us);
}

static bool
rtwsdio_cancelled(void *cookie)
{
	struct rtwsdio_softc *sc = cookie;
	bool stopping;

	mutex_enter(&sc->sc_lock);
	stopping = sc->sc_stopping;
	mutex_exit(&sc->sc_lock);
	return stopping;
}

static int
rtwsdio_scalar(struct rtwsdio_softc *sc, uint32_t address, void *data,
    size_t length, bool write)
{
	struct rtw88_sdio_bus bus = sc->sc_bus;
	enum rtw88_sdio_domain domain = RTW88_SDIO_MAC;
	uint32_t offset = address;

	if ((address & 0xffff0000U) == RTW8723D_SDIO_LOCAL_BASE) {
		domain = RTW88_SDIO_LOCAL;
		offset = address & 0xffff;
	} else if (address > 0xffff) {
		return EINVAL;
	}
	/* Narrow and unaligned registers use CMD52, including after MAC-on. */
	if (length != 4 || (address & 3) != 0)
		bus.cmd53_enabled = false;
	return rtw88_sdio_transfer(&bus, domain, offset, data, length, write);
}

static int
rtwsdio_read_1(void *cookie, uint32_t address, uint8_t *value)
{

	return rtwsdio_scalar(cookie, address, value, 1, false);
}

static int
rtwsdio_read_2(void *cookie, uint32_t address, uint16_t *value)
{
	uint8_t bytes[2];
	int error = rtwsdio_scalar(cookie, address, bytes, sizeof(bytes), false);

	if (error == 0)
		*value = le16dec(bytes);
	return error;
}

static int
rtwsdio_read_4(void *cookie, uint32_t address, uint32_t *value)
{
	uint32_t bytes;
	int error = rtwsdio_scalar(cookie, address, &bytes, sizeof(bytes), false);

	if (error == 0)
		*value = le32dec(&bytes);
	return error;
}

static int
rtwsdio_write_1(void *cookie, uint32_t address, uint8_t value)
{

	return rtwsdio_scalar(cookie, address, &value, 1, true);
}

static int
rtwsdio_write_2(void *cookie, uint32_t address, uint16_t value)
{
	uint8_t bytes[2];

	le16enc(bytes, value);
	return rtwsdio_scalar(cookie, address, bytes, sizeof(bytes), true);
}

static int
rtwsdio_write_4(void *cookie, uint32_t address, uint32_t value)
{
	uint32_t bytes;

	le32enc(&bytes, value);
	return rtwsdio_scalar(cookie, address, &bytes, sizeof(bytes), true);
}

static const struct rtw88_bus_ops rtwsdio_bus_ops = {
	.read_1 = rtwsdio_read_1,
	.read_2 = rtwsdio_read_2,
	.read_4 = rtwsdio_read_4,
	.write_1 = rtwsdio_write_1,
	.write_2 = rtwsdio_write_2,
	.write_4 = rtwsdio_write_4,
	.delay_us = rtwsdio_delay,
};

static int
rtwsdio_window(void *cookie, uint32_t address, const uint8_t *data,
    size_t length)
{
	struct rtwsdio_softc *sc = cookie;

	if (rtwsdio_cancelled(sc))
		return ECANCELED;
	return rtw88_sdio_transfer(&sc->sc_bus, RTW88_SDIO_MAC, address,
	    __UNCONST(data), length, true);
}

static int
rtwsdio_block_setup(struct rtwsdio_softc *sc)
{
	u_int maximum;
	uint8_t value;
	unsigned reg = SD_IO_FBR(1) + SD_IO_FBR_BLOCKLEN;
	int error;

	if (sdmmc_chip_host_maxblklen(sc->sc_sf->sc->sc_sct,
	    sc->sc_sf->sc->sc_sch) < (int)RTWSDIO_BLOCK_SIZE)
		return ENOTSUP;
	error = sdmmc_io_maxblklen_checked(sc->sc_sf, &maximum);
	if (error != 0)
		return error;
	if (maximum < RTWSDIO_BLOCK_SIZE)
		return ENOTSUP;
	value = 0;
	error = rtw88_sdio_cmd52(&sc->sc_ops, 0, SD_IO_CCCR_CAPABILITY, false, &value);
	if (error != 0)
		return error;
	if ((value & CCCR_CAPS_SMB) == 0)
		return ENOTSUP;
	sc->sc_old_blklen = sc->sc_sf->blklen;
	error = rtw88_sdio_cmd52(&sc->sc_ops, 0, reg, false, &sc->sc_old_fbr[0]);
	if (error == 0)
		error = rtw88_sdio_cmd52(&sc->sc_ops, 0, reg + 1, false,
		    &sc->sc_old_fbr[1]);
	if (error != 0)
		return error;
	/* Save ownership before a write that may reach the card and then fail. */
	sc->sc_block_owned = true;
	value = 0;
	error = rtw88_sdio_cmd52(&sc->sc_ops, 0, reg, true, &value);
	value = 2;
	if (error == 0)
		error = rtw88_sdio_cmd52(&sc->sc_ops, 0, reg + 1, true, &value);
	value = 0xff;
	if (error == 0)
		error = rtw88_sdio_cmd52(&sc->sc_ops, 0, reg, false, &value);
	if (error == 0 && value != 0)
		error = EIO;
	value = 0xff;
	if (error == 0)
		error = rtw88_sdio_cmd52(&sc->sc_ops, 0, reg + 1, false, &value);
	if (error == 0 && value != 2)
		error = EIO;
	if (error == 0)
		sc->sc_sf->blklen = RTWSDIO_BLOCK_SIZE;
	return error;
}

static void
rtwsdio_first_error(int *first, int error)
{

	if (*first == 0)
		*first = error;
}

/* Only the host worker or stop-after-task-barrier calls this function. */
static int
rtwsdio_cleanup(struct rtwsdio_softc *sc)
{
	struct rtw88_io io;
	unsigned reg = SD_IO_FBR(1) + SD_IO_FBR_BLOCKLEN;
	int error = 0, result;

	(void)rtw88_sdio_set_cmd53(&sc->sc_bus, false);
	if (sc->sc_chip_owned) {
		rtw88_io_init(&io, &rtwsdio_bus_ops, sc);
		rtw88_io_wr(&io, RTWSDIO_HIMR, 4, 0);
		rtwsdio_first_error(&error, io.error);
		/* A failed mask must not suppress the bounded power-off attempt. */
		rtw88_io_init(&io, &rtwsdio_bus_ops, sc);
		rtwsdio_first_error(&error, rtw8723d_power_off(&io, &sc->sc_boot_ops));
		sc->sc_chip_owned = false;
	}
	if (sc->sc_ien_owned) {
		rtwsdio_first_error(&error, sdmmc_intr_disable_checked(sc->sc_sf));
		sc->sc_ien_owned = false;
	}
	if (sc->sc_ih != NULL) {
		rtwsdio_first_error(&error, sdmmc_intr_disestablish_checked(sc->sc_ih));
		sc->sc_ih = NULL;
	}
	if (sc->sc_block_owned) {
		result = rtw88_sdio_cmd52(&sc->sc_ops, 0, reg, true, &sc->sc_old_fbr[0]);
		rtwsdio_first_error(&error, result);
		rtwsdio_first_error(&result, rtw88_sdio_cmd52(&sc->sc_ops, 0, reg + 1,
		    true, &sc->sc_old_fbr[1]));
		rtwsdio_first_error(&error, result);
		if (result == 0)
			sc->sc_sf->blklen = sc->sc_old_blklen;
		sc->sc_block_owned = false;
	}
	rtwsdio_first_error(&error, rtw88_sdio_function_release(&sc->sc_bus,
	    &sc->sc_function, RTW88_SDIO_RESTORE_OWNED_BIT));
	if (sc->sc_fwbuf != NULL) {
		firmware_free(sc->sc_fwbuf, sc->sc_fwsize);
		sc->sc_fwbuf = NULL;
	}
	if (sc->sc_fwh != NULL) {
		rtwsdio_first_error(&error, firmware_close(sc->sc_fwh));
		sc->sc_fwh = NULL;
	}
	return error;
}

static int
rtwsdio_bootstrap(struct rtwsdio_softc *sc)
{
	struct rtw8723d_fw firmware;
	struct rtw8723d_identity identity;
	struct rtw88_io io;
	uint8_t inten = 0;
	int error;

	error = firmware_open("rtw88", "rtw8723d_fw.bin", &sc->sc_fwh);
	if (error == 0)
		error = firmware_read_alloc(sc->sc_fwh, RTWSDIO_FW_MAX,
		    &sc->sc_fwbuf, &sc->sc_fwsize);
	if (error == 0)
		error = rtw8723d_fw_parse(sc->sc_fwbuf, sc->sc_fwsize, &firmware);
	if (error != 0)
		return error;
	if (rtwsdio_cancelled(sc))
		return ECANCELED;
	error = rtw88_sdio_function_acquire(&sc->sc_bus, &sc->sc_function, false);
	if (error == 0)
		error = rtwsdio_block_setup(sc);
	if (error != 0)
		return error;
	if (rtwsdio_cancelled(sc))
		return ECANCELED;
	rtw88_io_init(&io, &rtwsdio_bus_ops, sc);
	error = rtw8723d_identify(&io, &identity);
	if (error != 0)
		return error;
	sc->sc_chip_owned = true;
	/* The chip helper restores its caller's mask across power transitions. */
	rtw88_io_wr(&io, RTWSDIO_HIMR, 4, 0);
	if (io.error != 0)
		return io.error;
	error = rtw8723d_power_on(&io, &sc->sc_boot_ops);
	if (error == 0)
		error = rtw88_sdio_set_cmd53(&sc->sc_bus, true);
	if (error != 0)
		return error;
	/* This bootstrap has no operational RX/TX interrupt handlers. */
	rtw88_io_wr(&io, RTWSDIO_HIMR, 4, 0);
	if (io.error != 0)
		return io.error;
	error = rtw8723d_fw_boot(&io, &firmware, &sc->sc_boot_ops);
	if (error != 0)
		return error;
	firmware_free(sc->sc_fwbuf, sc->sc_fwsize);
	sc->sc_fwbuf = NULL;
	error = firmware_close(sc->sc_fwh);
	sc->sc_fwh = NULL;
	if (error != 0)
		return error;
	if (rtwsdio_cancelled(sc))
		return ECANCELED;
	error = sdmmc_intr_establish_checked(sc->sc_sf->sc->sc_dev,
	    rtwsdio_intr, sc, device_xname(sc->sc_dev), &sc->sc_ih);
	if (error != 0)
		return error;
	mutex_enter(&sc->sc_sf->sc->sc_mtx);
	error = rtw88_sdio_cmd52(&sc->sc_ops, 0, SD_IO_CCCR_FN_INTEN, false, &inten);
	if (error == 0 && (inten & 2) == 0) {
		sc->sc_ien_owned = true;
		error = sdmmc_intr_enable_checked(sc->sc_sf);
	}
	mutex_exit(&sc->sc_sf->sc->sc_mtx);
	return error;
}

static void
rtwsdio_task(void *cookie)
{
	struct rtwsdio_softc *sc = cookie;
	struct rtw88_io io;
	bool bootstrap;
	int error = 0;

	mutex_enter(&sc->sc_lock);
	if (sc->sc_stopping) {
		mutex_exit(&sc->sc_lock);
		return;
	}
	bootstrap = sc->sc_state == RTWSDIO_ATTACHED && sc->sc_error == 0;
	if (bootstrap)
		sc->sc_state = RTWSDIO_LOADING;
	else if (!sc->sc_irq_pending) {
		mutex_exit(&sc->sc_lock);
		return;
	}
	sc->sc_irq_pending = false;
	mutex_exit(&sc->sc_lock);
	if (bootstrap) {
		error = rtwsdio_bootstrap(sc);
	} else {
		rtw88_io_init(&io, &rtwsdio_bus_ops, sc);
		rtw88_io_wr(&io, RTWSDIO_HIMR, 4, 0);
		(void)rtw88_io_rd(&io, RTWSDIO_HISR, 4);
		/* No operational bits are handled, so none can be W1C-cleared. */
		error = io.error;
	}
	if (error != 0)
		(void)rtwsdio_cleanup(sc);
	mutex_enter(&sc->sc_lock);
	if (!sc->sc_stopping)
		sc->sc_state = error == 0 ? RTWSDIO_FW_READY : RTWSDIO_ATTACHED;
	rtwsdio_first_error(&sc->sc_error, error);
	mutex_exit(&sc->sc_lock);
	if (error != 0)
		aprint_error_dev(sc->sc_dev, "bootstrap/IRQ failed: %d\n", error);
	else if (bootstrap)
		aprint_normal_dev(sc->sc_dev, "firmware ready (bootstrap only)\n");
}

static int
rtwsdio_intr(void *cookie)
{
	struct rtwsdio_softc *sc = cookie;

	/* The host masks/acks its shared source. Do not change it here. */
	mutex_enter(&sc->sc_lock);
	if (!sc->sc_stopping && sc->sc_error == 0) {
		sc->sc_irq_pending = true;
		sdmmc_add_task(sc->sc_sf->sc, &sc->sc_task);
	}
	mutex_exit(&sc->sc_lock);
	return 1;
}

static void
rtwsdio_mountroot(device_t self)
{
	struct rtwsdio_softc *sc = device_private(self);

	mutex_enter(&sc->sc_lock);
	if (!sc->sc_stopping)
		sdmmc_add_task(sc->sc_sf->sc, &sc->sc_task);
	mutex_exit(&sc->sc_lock);
}

static void
rtwsdio_attach(device_t parent, device_t self, void *aux)
{
	struct rtwsdio_softc *sc = device_private(self);
	const struct sdmmc_attach_args *aa = aux;

	sc->sc_dev = self;
	sc->sc_sf = aa->sf;
	sc->sc_state = RTWSDIO_ATTACHED;
	mutex_init(&sc->sc_lock, MUTEX_DEFAULT, IPL_NONE);
	sdmmc_init_task(&sc->sc_task, rtwsdio_task, sc);
	sc->sc_ops = (struct rtw88_sdio_ops) {
		.command52 = rtwsdio_command52, .command53 = rtwsdio_command53,
		.wait_ms = rtwsdio_wait, .cookie = sc,
	};
	sc->sc_bus = (struct rtw88_sdio_bus) {
		.ops = &sc->sc_ops, .block_size = RTWSDIO_BLOCK_SIZE,
		.max_request = RTW88_SDIO_MAX_REQUEST,
		.read_scratch = (uint8_t *)sc->sc_scratch,
		.read_scratch_len = sizeof(sc->sc_scratch),
	};
	sc->sc_boot_ops = (struct rtw8723d_boot_ops) {
		.write_window = rtwsdio_window, .wait_ms = rtwsdio_wait,
		.cancelled = rtwsdio_cancelled, .cookie = sc,
	};
	aprint_normal(": RTL8723DS SDIO bootstrap\n");
	if (!rtwsdio_admitted(parent)) {
		sc->sc_error = ENOTSUP;
		aprint_error_dev(self, "no configured native power profile\n");
		return;
	}
	config_mountroot(self, rtwsdio_mountroot);
}

static int
rtwsdio_stop(struct rtwsdio_softc *sc)
{
	int error;

	mutex_enter(&sc->sc_lock);
	if (sc->sc_state == RTWSDIO_DETACHED) {
		mutex_exit(&sc->sc_lock);
		return 0;
	}
	sc->sc_stopping = true;
	sc->sc_state = RTWSDIO_STOPPING;
	mutex_exit(&sc->sc_lock);
	/* Do not hold a lock needed by the root callback or host worker. */
	error = config_mountroot_cancel(sc->sc_dev);
	if (error != 0)
		return error;
	/* Removing a queued repeat alone does not drain a running instance. */
	while (sdmmc_del_task(sc->sc_sf->sc, &sc->sc_task, NULL))
		continue;
	error = rtwsdio_cleanup(sc);
	mutex_enter(&sc->sc_lock);
	rtwsdio_first_error(&sc->sc_error, error);
	sc->sc_state = RTWSDIO_DETACHED;
	mutex_exit(&sc->sc_lock);
	if (error != 0)
		aprint_error_dev(sc->sc_dev, "cleanup failed: %d\n", error);
	return 0;
}

static int
rtwsdio_detach(device_t self, int flags __unused)
{
	struct rtwsdio_softc *sc = device_private(self);
	int error = rtwsdio_stop(sc);

	if (error == 0)
		mutex_destroy(&sc->sc_lock);
	return error;
}
