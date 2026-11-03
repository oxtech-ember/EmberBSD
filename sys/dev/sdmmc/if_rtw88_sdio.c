/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD RTL8723D; FDT policy adapted from ef145f701, AI-assisted. */
/* Native RTL8723DS frontend. Board power remains with the admitted host;
 * one executor owns identity, chip/HCI lifecycle, control and FIFO I/O. */
#ifdef _KERNEL_OPT
#include "opt_fdt.h"
#endif
#include <sys/param.h>
#include <sys/device.h>
#include <sys/endian.h>
#include <sys/mutex.h>
#include <sys/callout.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <dev/firmload.h>
#ifdef FDT
#include <dev/fdt/fdtvar.h>
#include <dev/ofw/openfirm.h>
#endif
#include <dev/ic/rtw88_io.h>
#include <dev/ic/rtw88_executor.h>
#include <dev/ic/rtw88_net.h>
#include <dev/ic/rtw88_profile.h>
#include <dev/ic/rtw8723d_chip.h>
#include <dev/sdmmc/sdmmcchip.h>
#include <dev/sdmmc/sdmmc_ioreg.h>
#include <dev/sdmmc/sdmmcreg.h>
#include <dev/sdmmc/sdmmcvar.h>
#include <dev/sdmmc/rtw88_sdio_io.h>
#include <dev/sdmmc/rtw88_sdio_hci.h>

#define RTWSDIO_BLOCK_SIZE	512U
#define RTWSDIO_FW_MAX		(32U + 32768U)
#define RTWSDIO_HIMR		(RTW8723D_SDIO_LOCAL_BASE + 0x14)
#define RTWSDIO_HISR		(RTW8723D_SDIO_LOCAL_BASE + 0x18)
#define RTWSDIO_RETRY_MS		10U
#define RTWSDIO_RETRY_LIMIT	100U

enum rtwsdio_state {
	RTWSDIO_ATTACHED, RTWSDIO_LOADING, RTWSDIO_NET_READY, RTWSDIO_BOOT_READY,
	RTWSDIO_FAILED,
	RTWSDIO_STOPPING, RTWSDIO_DETACHED
};

struct rtwsdio_softc {
	device_t sc_dev;
	struct sdmmc_function *sc_sf;
	/* Interlock for admission, task submission and detach; never bus I/O. */
	kmutex_t sc_lock;
	enum rtwsdio_state sc_state;
	bool sc_stopping;
	int sc_error;
	/* A consumed handler cannot be retried after uncertain master release.
	 * Retain the card owner until a parent-owned reset/reinit resolves it. */
	int sc_intr_release_error;
	/* Protected by parent sc_mtx. A raw host failure does not prove that
	 * IDMA/reset completed; only parent-owned recovery may clear this. */
	int sc_host_error;
	void *sc_ih;
	struct rtw88_sdio_ops sc_ops;
	struct rtw88_sdio_bus sc_bus;
	struct rtw88_sdio_function_state sc_function;
	struct rtw88_softc sc_core;
	struct rtw8723d_runtime sc_chip;
	struct rtw88_sdio_hci sc_hci_ctx;
	uint32_t sc_scratch[RTW88_SDIO_MAX_TRANSFER / sizeof(uint32_t)];
	struct rtw88_sdio_tx_context sc_tx;
	uint32_t sc_tx_packet[RTW88_SDIO_MAX_TRANSFER / sizeof(uint32_t)];
	struct rtw88_sdio_rx_context sc_rx;
	struct rtw88_sdio_rx_batch sc_rx_batch;
	uint32_t sc_rx_packet[RTW88_SDIO_RX_BATCH_MAX / sizeof(uint32_t)];
	uint32_t sc_rx_scratch[RTW88_SDIO_RX_BATCH_MAX / sizeof(uint32_t)];
	callout_t sc_retry;
	bool sc_retry_armed, sc_retry_due, sc_core_attached;
	unsigned sc_retry_count;
	u_int sc_old_blklen;
	uint8_t sc_old_fbr[2];
	bool sc_block_owned, sc_ien_owned;
	void *sc_fwbuf;
	size_t sc_fwsize;
};

static int rtwsdio_match(device_t, cfdata_t, void *);
static void rtwsdio_attach(device_t, device_t, void *);
static int rtwsdio_detach(device_t, int);
static void rtwsdio_mountroot(device_t);
static void rtwsdio_core_service(struct rtw88_softc *, uint32_t);
static int rtwsdio_intr(void *);
static int rtwsdio_stop(struct rtwsdio_softc *);
static void rtwsdio_retry_reset(void *);
static void rtwsdio_fault(void *, int);

CFATTACH_DECL_NEW(rtwsdio, sizeof(struct rtwsdio_softc),
    rtwsdio_match, rtwsdio_attach, rtwsdio_detach, NULL);

#ifdef FDT
static int
rtwsdio_host_node(device_t parent)
{
	device_t host = device_parent(parent);
	const char *path;

	if (host == NULL || !prop_dictionary_get_string(device_properties(host),
	    "fdt-path", &path))
		return -1;
	return OF_finddevice(path);
}
#endif

/* Standard host pwrseq owns board power. */
static bool
rtwsdio_admitted(device_t parent)
{
#ifdef FDT
	int node, root;

	node = rtwsdio_host_node(parent);
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
	const struct sdmmc_function *sf, *fn0;

	if (aa == NULL || (sf = aa->sf) == NULL || sf->number != 1)
		return 0;
	if (sf->cis.manufacturer == 0x024c && sf->cis.product == 0xd723)
		return rtwsdio_admitted(parent);
	/* Use common CIS only when F1 omits both identity fields. */
	if (sf->cis.manufacturer != 0 || sf->cis.product != 0 ||
	    sf->sc == NULL || (fn0 = sf->sc->sc_fn0) == NULL)
		return 0;
	return fn0->cis.manufacturer == 0x024c &&
	    fn0->cis.product == 0xd723 && rtwsdio_admitted(parent);
}

/* Parent SDMMC IRQ work shares card selection and the host bounce buffer.
 * Keep select+command atomic, also when a caller owns a complete CCCR RMW. */
static bool
rtwsdio_host_enter(struct rtwsdio_softc *sc)
{
	bool held = mutex_owned(&sc->sc_sf->sc->sc_mtx);

	if (!held)
		mutex_enter(&sc->sc_sf->sc->sc_mtx);
	return held;
}

static void
rtwsdio_host_exit(struct rtwsdio_softc *sc, bool held)
{

	if (!held)
		mutex_exit(&sc->sc_sf->sc->sc_mtx);
}

static int
rtwsdio_host_error(struct rtwsdio_softc *sc)
{
	bool held = rtwsdio_host_enter(sc);
	int error = sc->sc_host_error;

	rtwsdio_host_exit(sc, held);
	return error;
}

/* Checked parent helpers do not expose transport versus R5 failure. Treat
 * their errors conservatively; do not assume a failed helper reset IDMA. */
static void
rtwsdio_host_failed(struct rtwsdio_softc *sc, int error)
{
	bool held = rtwsdio_host_enter(sc);

	if (sc->sc_host_error == 0)
		sc->sc_host_error = error;
	rtwsdio_host_exit(sc, held);
}

/* These callbacks consume an already encoded request without narrowing it.
 * Latch transport errors only: card-reported R5 errors are decoded above us. */
static int
rtwsdio_command52(void *cookie, uint32_t arg, uint32_t *response)
{
	struct rtwsdio_softc *sc = cookie;
	struct sdmmc_command cmd;
	bool held;
	int error;

	if (sc == NULL || sc->sc_sf == NULL || sc->sc_sf->sc == NULL ||
	    response == NULL)
		return EINVAL;
	held = rtwsdio_host_enter(sc);
	if ((error = sc->sc_host_error) != 0)
		goto out;
	error = sdmmc_select_card(sc->sc_sf->sc, sc->sc_sf);
	if (error != 0)
		goto failed;
	memset(&cmd, 0, sizeof(cmd));
	cmd.c_opcode = SD_IO_RW_DIRECT;
	cmd.c_arg = arg;
	cmd.c_flags = SCF_CMD_AC | SCF_RSP_R5;
	error = sdmmc_mmc_command(sc->sc_sf->sc, &cmd);
	if (error == 0)
		*response = cmd.c_resp[0];
failed:
	if (error != 0)
		sc->sc_host_error = error;
out:
	rtwsdio_host_exit(sc, held);
	return error;
}

static int
rtwsdio_command53(void *cookie, uint32_t arg, void *data, size_t length,
    uint32_t *response)
{
	struct rtwsdio_softc *sc = cookie;
	struct sdmmc_command cmd;
	unsigned count = arg & SD_ARG_CMD53_LENGTH_MASK;
	bool held;
	int error;

	if (sc == NULL || sc->sc_sf == NULL || sc->sc_sf->sc == NULL ||
	    response == NULL ||
	    data == NULL || length == 0 || length > RTW88_SDIO_MAX_TRANSFER ||
	    (arg & SD_ARG_CMD53_INCREMENT) == 0 ||
	    ((arg >> SD_ARG_CMD53_FUNC_SHIFT) & SD_ARG_CMD53_FUNC_MASK) != 1)
		return EINVAL;
	if ((arg & SD_ARG_CMD53_BLOCK_MODE) != 0) {
		if (count == 0 || count > 8 ||
		    length != count * RTWSDIO_BLOCK_SIZE)
			return EINVAL;
	} else if (length != (count == 0 ? 512 : count)) {
		return EINVAL;
	}
	held = rtwsdio_host_enter(sc);
	if (sc->sc_sf->blklen != RTWSDIO_BLOCK_SIZE) {
		error = EINVAL;
		goto out;
	}
	if ((error = sc->sc_host_error) != 0)
		goto out;
	error = sdmmc_select_card(sc->sc_sf->sc, sc->sc_sf);
	if (error != 0)
		goto failed;
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
failed:
	if (error != 0)
		sc->sc_host_error = error;
out:
	rtwsdio_host_exit(sc, held);
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

/* A load publishes a complete immutable image, with its file already closed.
 * The selected chip owns the matching release on every parse/start outcome. */
static int
rtwsdio_firmware_load(void *cookie, const char *driver, const char *image,
    const uint8_t **bytes, size_t *size)
{
	struct rtwsdio_softc *sc = cookie;
	firmware_handle_t handle = NULL;
	void *buffer = NULL;
	size_t length = 0;
	int error, close_error;

	if (bytes == NULL || size == NULL || sc->sc_fwbuf != NULL ||
	    !rtw88_executor_owned(&sc->sc_core))
		return EINVAL;
	error = firmware_open(driver, image, &handle);
	if (error == 0) {
		error = firmware_read_alloc(handle, RTWSDIO_FW_MAX,
		    &buffer, &length);
		close_error = firmware_close(handle);
		if (error == 0)
			error = close_error;
	}
	if (error != 0) {
		if (buffer != NULL)
			firmware_free(buffer, length);
		return error;
	}
	sc->sc_fwbuf = buffer;
	sc->sc_fwsize = length;
	*bytes = buffer;
	*size = length;
	return 0;
}

static void
rtwsdio_firmware_release(void *cookie)
{
	struct rtwsdio_softc *sc = cookie;

	if (sc->sc_fwbuf != NULL) {
		firmware_free(sc->sc_fwbuf, sc->sc_fwsize);
		sc->sc_fwbuf = NULL;
		sc->sc_fwsize = 0;
	}
}

static const struct rtw88_bus_ops rtwsdio_bus_ops = {
	.read_1 = rtwsdio_read_1,
	.read_2 = rtwsdio_read_2,
	.read_4 = rtwsdio_read_4,
	.write_1 = rtwsdio_write_1,
	.write_2 = rtwsdio_write_2,
	.write_4 = rtwsdio_write_4,
	.delay_us = rtwsdio_delay,
	.firmware_load = rtwsdio_firmware_load,
	.firmware_release = rtwsdio_firmware_release,
};

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
	if (error != 0) {
		rtwsdio_host_failed(sc, error);
		return error;
	}
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

/* Resource callbacks run only on the shared executor. The chip's existing
 * power owner confirms off before release; this frontend never powers it off
 * a second time and never treats a host timeout as confirmed reset. */
static int
rtwsdio_acquire(void *cookie)
{
	struct rtwsdio_softc *sc = cookie;
	bool held;
	int error;

	if (!rtw88_executor_owned(&sc->sc_core))
		return EPERM;
	if ((error = rtwsdio_host_error(sc)) != 0)
		return error;
	if (sc->sc_bus.function_claimed && sc->sc_bus.function_ready &&
	    sc->sc_block_owned)
		return 0;
	if (sc->sc_block_owned)
		return EBUSY; /* A partial FBR write requires release, not replay. */
	held = rtwsdio_host_enter(sc);
	error = rtw88_sdio_function_acquire(&sc->sc_bus, &sc->sc_function, false);
	if (error == 0)
		error = rtwsdio_block_setup(sc);
	rtwsdio_host_exit(sc, held);
	return error;
}

static int
rtwsdio_irq_start(void *cookie)
{
	struct rtwsdio_softc *sc = cookie;
	uint8_t inten = 0;
	bool held;
	int error;

	if (!rtw88_executor_owned(&sc->sc_core))
		return EPERM;
	if ((error = rtwsdio_host_error(sc)) != 0)
		return error;
	if (sc->sc_intr_release_error != 0)
		return sc->sc_intr_release_error;
	if (sc->sc_ih == NULL) {
		error = sdmmc_intr_establish_checked(sc->sc_sf->sc->sc_dev,
		    rtwsdio_intr, sc, device_xname(sc->sc_dev), &sc->sc_ih);
		if (error != 0) {
			rtwsdio_host_failed(sc, error);
			return error;
		}
	}
	held = rtwsdio_host_enter(sc);
	error = rtw88_sdio_cmd52(&sc->sc_ops, 0, SD_IO_CCCR_FN_INTEN,
	    false, &inten);
	if (error == 0 && (inten & 2) == 0) {
		sc->sc_ien_owned = true;
		error = sdmmc_intr_enable_checked(sc->sc_sf);
		if (error != 0)
			rtwsdio_host_failed(sc, error);
	}
	rtwsdio_host_exit(sc, held);
	return error;
}

static int
rtwsdio_status(void *cookie)
{
	struct rtwsdio_softc *sc = cookie;
	int error = rtwsdio_host_error(sc);

	return error != 0 ? error : sc->sc_intr_release_error;
}

static int
rtwsdio_release(void *cookie)
{
	struct rtwsdio_softc *sc = cookie;
	unsigned reg = SD_IO_FBR(1) + SD_IO_FBR_BLOCKLEN;
	int error = 0, result;
	bool held;

	if (!rtw88_executor_owned(&sc->sc_core))
		return EPERM;
	if (sc->sc_chip.boot.owned || sc->sc_core.sc_powered ||
	    sc->sc_core.sc_power_dirty || sc->sc_hci_ctx.tx_active)
		return EBUSY;
	rtwsdio_retry_reset(sc);
	/* No child lock: drain a racing MPSAFE callback before releasing card
	 * ownership. Its only action is to post this same still-pinned actor. */
	callout_halt(&sc->sc_retry, NULL);
	(void)rtw88_sdio_set_cmd53(&sc->sc_bus, false);
	if ((result = rtwsdio_status(sc)) != 0)
		return result;
	if (sc->sc_ien_owned) {
		result = sdmmc_intr_disable_checked(sc->sc_sf);
		if (result != 0) {
			rtwsdio_host_failed(sc, result);
			return result;
		}
		sc->sc_ien_owned = false;
	}
	if (sc->sc_ih != NULL) {
		/* Parent sc_mtx serializes this removal with invocation of its
		 * callback list. The checked API consumes the cookie on error. */
		result = sdmmc_intr_disestablish_checked(sc->sc_ih);
		sc->sc_ih = NULL;
		rtwsdio_first_error(&sc->sc_intr_release_error, result);
		if (result != 0) {
			rtwsdio_host_failed(sc, result);
			return result;
		}
	}
	if (sc->sc_block_owned) {
		held = rtwsdio_host_enter(sc);
		result = rtw88_sdio_cmd52(&sc->sc_ops, 0, reg, true,
		    &sc->sc_old_fbr[0]);
		rtwsdio_first_error(&error, result);
		/* A raw host failure forbids another command; an R5 failure
		 * retains both old bytes and permits the other restore attempt. */
		if (rtwsdio_host_error(sc) == 0) {
			result = rtw88_sdio_cmd52(&sc->sc_ops, 0, reg + 1,
			    true, &sc->sc_old_fbr[1]);
			rtwsdio_first_error(&error, result);
		}
		if (error == 0) {
			sc->sc_sf->blklen = sc->sc_old_blklen;
			sc->sc_block_owned = false;
		}
		rtwsdio_host_exit(sc, held);
	}
	if (error != 0)
		return error;
	held = rtwsdio_host_enter(sc);
	error = rtw88_sdio_function_release(&sc->sc_bus,
	    &sc->sc_function, RTW88_SDIO_RESTORE_OWNED_BIT);
	rtwsdio_host_exit(sc, held);
	return error;
}

static void
rtwsdio_retry(void *cookie)
{
	struct rtwsdio_softc *sc = cookie;

	mutex_enter(&sc->sc_lock);
	if (sc->sc_retry_armed && !sc->sc_stopping) {
		sc->sc_retry_armed = false;
		sc->sc_retry_due = true;
		rtw88_executor_schedule(&sc->sc_core, RTW88_EXEC_RX);
	}
	mutex_exit(&sc->sc_lock);
}

static void
rtwsdio_retry_reset(void *cookie)
{
	struct rtwsdio_softc *sc = cookie;

	mutex_enter(&sc->sc_lock);
	sc->sc_retry_armed = sc->sc_retry_due = false;
	sc->sc_retry_count = 0;
	callout_stop(&sc->sc_retry);
	mutex_exit(&sc->sc_lock);
}

static bool
rtwsdio_retry_ready(void *cookie)
{
	struct rtwsdio_softc *sc = cookie;
	bool ready;

	mutex_enter(&sc->sc_lock);
	ready = !sc->sc_stopping && !sc->sc_retry_armed && sc->sc_retry_due;
	if (ready)
		sc->sc_retry_due = false;
	mutex_exit(&sc->sc_lock);
	return ready;
}

static void
rtwsdio_again(void *cookie, bool delayed)
{
	struct rtwsdio_softc *sc = cookie;
	bool exhausted = false;

	mutex_enter(&sc->sc_lock);
	if (!sc->sc_stopping) {
		if (!delayed) {
			rtw88_executor_schedule(&sc->sc_core, RTW88_EXEC_RX);
		} else if (!sc->sc_retry_armed) {
			if (sc->sc_retry_count == RTWSDIO_RETRY_LIMIT) {
				exhausted = true;
			} else {
				sc->sc_retry_count++;
				sc->sc_retry_due = false;
				sc->sc_retry_armed = true;
				callout_reset(&sc->sc_retry,
				    MAX(1, mstohz(RTWSDIO_RETRY_MS)),
				    rtwsdio_retry, sc);
			}
		}
	}
	mutex_exit(&sc->sc_lock);
	if (exhausted)
		rtwsdio_fault(sc, ETIMEDOUT);
}

static void
rtwsdio_fault(void *cookie, int error)
{
	struct rtwsdio_softc *sc = cookie;

	/* This is also safe after io_end grants Giant preparation: fault only
	 * touches the locked network queue, never core generation/registers. */
	mutex_enter(&sc->sc_lock);
	rtwsdio_first_error(&sc->sc_error, error);
	if (sc->sc_core.sc_net != NULL)
		rtw88_net_fault(sc->sc_core.sc_net, error);
	mutex_exit(&sc->sc_lock);
}

static int
rtwsdio_rx_epoch(void *cookie, struct rtw88_async_epoch *id)
{
	struct rtwsdio_softc *sc = cookie;
	int error;

	mutex_enter(&sc->sc_lock);
	error = rtw88_net_rx_epoch(sc->sc_core.sc_net,
	    sc->sc_core.sc_generation, id);
	mutex_exit(&sc->sc_lock);
	return error;
}

static int
rtwsdio_rx_current(void *cookie, const struct rtw88_async_epoch *id)
{
	struct rtw88_async_epoch current;
	int error = rtwsdio_rx_epoch(cookie, &current);

	if (error == 0 && (current.generation != id->generation ||
	    current.epoch != id->epoch || current.key_epoch != id->key_epoch))
		error = ESTALE;
	return error;
}

static void
rtwsdio_defer_irq(struct rtw88_softc *core)
{
	struct rtwsdio_softc *sc = core->sc_bus;

	(void)rtwsdio_intr(sc);
}

/* With no radio profile, retain the existing bounded firmware-only route.
 * No netif, RF policy or transmit capability is published by this path. */
static int
rtwsdio_bootstrap_only(struct rtwsdio_softc *sc)
{
	struct rtw88_softc *core = &sc->sc_core;
	struct rtw8723d_identity identity;
	struct rtw8723d_fw parsed;
	struct rtw88_io io;
	const uint8_t *firmware;
	size_t size;
	int error;

	error = rtwsdio_firmware_load(sc, "rtw88", "rtw8723d_fw.bin",
	    &firmware, &size);
	if (error != 0)
		return error;
	error = rtw8723d_fw_parse(firmware, size, &parsed);
	if (error == 0)
		error = core->sc_hci->prepare(core);
	if (error == 0) {
		rtw88_io_init(&io, core->sc_ops, core->sc_bus);
		error = rtw8723d_identify(&io, &identity);
	}
	if (error == 0)
		error = rtw8723d_core_power_on(&sc->sc_chip.boot);
	if (error == 0)
		error = rtw8723d_core_firmware(&sc->sc_chip.boot, firmware, size);
	if (error == 0)
		error = rtwsdio_irq_start(sc);
	rtwsdio_firmware_release(sc);
	return error;
}

/* A partial explicit policy is not absence: it must fail closed. */
static bool
rtwsdio_has_radio_profile(struct rtwsdio_softc *sc)
{
	prop_dictionary_t p = device_properties(sc->sc_dev);
#ifdef FDT
	int node;
#endif

	if (p != NULL && (prop_dictionary_get(p, "rtw88-regulatory") != NULL ||
	    prop_dictionary_get(p, "rtw8723d-phy-package") != NULL))
		return true;
#ifdef FDT
	node = rtwsdio_host_node(device_parent(sc->sc_dev));

	return node > 0 &&
	    (OF_getproplen(node, "ember,rtw88-regdomain") >= 0 ||
	     OF_getproplen(node, "ember,rtw88-channels-2ghz") >= 0 ||
	     OF_getproplen(node, "ember,rtw8723d-phy-package") >= 0);
#else
	return false;
#endif
}

#ifdef FDT
/* Read only explicit host policy. A partial or malformed policy never falls
 * back to a default. Commit output only after the shared admission succeeds. */
static int
rtwsdio_fdt_profile(struct rtwsdio_softc *sc, struct rtw88_profile *profile,
    uint32_t *package)
{
	struct rtw88_softc *core = &sc->sc_core;
	struct rtw88_profile_input input;
	struct rtw88_profile value;
	prop_dictionary_t properties = device_properties(sc->sc_dev);
	prop_object_t mac;
	const void *bytes;
	char domain[sizeof("world-min")];
	uint8_t cells[4];
	uint32_t channels, selector;
	int error, node = rtwsdio_host_node(device_parent(sc->sc_dev));

	if (node <= 0 ||
	    OF_getproplen(node, "ember,rtw88-regdomain") != sizeof(domain) ||
	    OF_getprop(node, "ember,rtw88-regdomain", domain,
	    sizeof(domain)) != sizeof(domain) ||
	    memcmp(domain, "world-min", sizeof(domain)) != 0 ||
	    OF_getproplen(node, "ember,rtw88-channels-2ghz") != 4 ||
	    OF_getprop(node, "ember,rtw88-channels-2ghz", cells, 4) != 4)
		return EACCES;
	channels = be32dec(cells);
	if (channels == 0 || (channels & ~0x1fffU) != 0 ||
	    OF_getproplen(node, "ember,rtw8723d-phy-package") != 4 ||
	    OF_getprop(node, "ember,rtw8723d-phy-package", cells, 4) != 4)
		return EINVAL;
	selector = be32dec(cells);
	if (selector > 15)
		return EINVAL;
	memset(&input, 0, sizeof(input));
	memcpy(input.efuse_mac, core->sc_mac, sizeof(input.efuse_mac));
	input.has_regulatory = true;
	input.domain = RTW88_REG_WORLD_MIN;
	input.channels_2g = channels;
	mac = properties == NULL ? NULL :
	    prop_dictionary_get(properties, "mac-address");
	if (mac != NULL) {
		if (prop_object_type(mac) != PROP_TYPE_DATA ||
		    prop_data_size((prop_data_t)mac) != sizeof(input.platform_mac) ||
		    (bytes = prop_data_value((prop_data_t)mac)) == NULL)
			return EINVAL;
		memcpy(input.platform_mac, bytes, sizeof(input.platform_mac));
		input.has_platform_mac = true;
	}
	error = rtw88_profile_decode(&input, &core->sc_chip->caps, &value);
	if (error != 0)
		return error;
	*profile = value;
	*package = selector;
	return 0;
}
#endif

/* Explicit child properties take precedence over host FDT policy. A malformed
 * child policy never falls back to the host. Neither source is mutated. */
static int
rtwsdio_profile(struct rtwsdio_softc *sc, struct rtw88_profile *profile,
    uint32_t *package)
{
	prop_dictionary_t properties = device_properties(sc->sc_dev);
	struct rtw88_softc *core = &sc->sc_core;

	if (properties != NULL &&
	    (prop_dictionary_get(properties, "rtw88-regulatory") != NULL ||
	     prop_dictionary_get(properties, "rtw8723d-phy-package") != NULL)) {
		if (!prop_dictionary_get_uint32(properties,
		    "rtw8723d-phy-package", package) || *package > 15)
			return EACCES;
		return rtw88_profile_from_properties(sc->sc_dev, core->sc_mac,
		    &core->sc_chip->caps, profile);
	}
#ifdef FDT
	return rtwsdio_fdt_profile(sc, profile, package);
#else
	return EACCES;
#endif
}

/* Identity and profile setup runs once, before publishing a netif. The board
 * must supply explicit regulatory and package inputs. Neither is inferred from
 * vendor GPIO bytes, EFUSE regulatory hints or missing FDT properties. */
static int
rtwsdio_network_attach(struct rtwsdio_softc *sc)
{
	struct rtw88_softc *core = &sc->sc_core;
	struct rtw88_profile profile;
	struct rtw88_net *net = NULL;
	uint32_t package;
	int error;

	if (!rtwsdio_has_radio_profile(sc))
		return rtwsdio_bootstrap_only(sc);
	error = core->sc_hci->prepare(core);
	if (error != 0) {
		aprint_error_dev(sc->sc_dev, "bootstrap prepare failed: %d\n", error);
		return error;
	}
	error = core->sc_chip->identify(core);
	if (error != 0) {
		aprint_error_dev(sc->sc_dev, "bootstrap identify failed: %d\n", error);
		return error;
	}
	error = rtwsdio_profile(sc, &profile, &package);
	if (error != 0) {
		aprint_error_dev(sc->sc_dev, "bootstrap profile failed: %d\n", error);
		return error;
	}
	if (rtwsdio_cancelled(sc))
		return ECANCELED;
	sc->sc_chip.config.profile = profile;
	sc->sc_chip.config.condition.package = package;
	sc->sc_chip.config.power.rf_paths = 1;
	core->sc_profile_channels_2g = profile.channels_2g;
	/* No child/parent lock across Giant/stack attach. The initial branch
	 * has no net_io_begin window and must not call io_end on this new net. */
	KERNEL_LOCK(1, NULL);
	error = rtw88_net_attach(core, sc->sc_dev, &sc->sc_lock,
	    profile.mac, &net);
	KERNEL_UNLOCK_ONE(NULL);
	if (error != 0)
		aprint_error_dev(sc->sc_dev, "bootstrap net attach failed: %d\n", error);
	return error;
}

static void
rtwsdio_core_service(struct rtw88_softc *core, uint32_t reasons)
{
	struct rtwsdio_softc *sc = core->sc_bus;
	bool bootstrap, stopping, boot_ready;
	int error = 0, cleanup_error, reopen, status;

	mutex_enter(&sc->sc_lock);
	bootstrap = !sc->sc_stopping && sc->sc_state == RTWSDIO_ATTACHED &&
	    (reasons & RTW88_EXEC_BOOTSTRAP) != 0;
	if (bootstrap)
		sc->sc_state = RTWSDIO_LOADING;
	stopping = sc->sc_stopping;
	boot_ready = sc->sc_state == RTWSDIO_BOOT_READY;
	mutex_exit(&sc->sc_lock);
	if (bootstrap) {
		error = rtwsdio_network_attach(sc);
		if (error != 0) {
			/* No wrapper was published on an attach failure. Preserve the
			 * first cause; unwind on this same actor through chip + HCI. */
			cleanup_error = rtw88_stop(core);
			if (cleanup_error != 0)
				aprint_error_dev(sc->sc_dev,
				    "attach cleanup retained ownership: %d\n",
				    cleanup_error);
		}
		mutex_enter(&sc->sc_lock);
		rtwsdio_first_error(&sc->sc_error, error);
		if (!sc->sc_stopping)
			sc->sc_state = error != 0 ? RTWSDIO_FAILED :
			    core->sc_net != NULL ? RTWSDIO_NET_READY : RTWSDIO_BOOT_READY;
		mutex_exit(&sc->sc_lock);
		if (error != 0)
			aprint_error_dev(sc->sc_dev, "bootstrap or netif attach failed: %d\n", error);
		else if (core->sc_net != NULL)
			aprint_normal_dev(sc->sc_dev,
			    "native 2GHz netif attached; RF remains off until init\n");
		else
			aprint_normal_dev(sc->sc_dev, "firmware ready (bootstrap only)\n");
		return;
	}
	/* Partial attach/detach still retires resources on the actor. An error
	 * must never suppress STOP and leave an indefinitely powered owner. */
	if (core->sc_net == NULL) {
		if (!stopping && (reasons & RTW88_EXEC_RX) != 0 &&
		    boot_ready) {
			struct rtw88_io io;

			rtw88_io_init(&io, &rtwsdio_bus_ops, sc);
			rtw88_io_wr(&io, RTWSDIO_HIMR, 4, 0);
			(void)rtw88_io_rd(&io, RTWSDIO_HISR, 4);
			if (io.error != 0) {
				rtwsdio_fault(sc, io.error);
				mutex_enter(&sc->sc_lock);
				if (!sc->sc_stopping)
					sc->sc_state = RTWSDIO_FAILED;
				mutex_exit(&sc->sc_lock);
				reasons |= RTW88_EXEC_STOP;
			}
		}
		if (stopping || (reasons & RTW88_EXEC_STOP) != 0) {
			error = rtw88_stop(core);
			mutex_enter(&sc->sc_lock);
			rtwsdio_first_error(&sc->sc_error, error);
			mutex_exit(&sc->sc_lock);
		}
		return;
	}
	/* Parent lock -> IRQ callback -> child lock exists in SDMMC. Never
	 * hold the child lock across a CMD52/53 or parent IRQ operation. Only
	 * this owner can grant Giant preparation; mask before that grant. */
	if (sc->sc_hci_ctx.acquired && sc->sc_bus.function_ready)
		error = rtw88_sdio_irq_mask(&sc->sc_bus);
	if (error != 0)
		rtwsdio_fault(sc, error);
	status = rtwsdio_status(sc);
	mutex_enter(&sc->sc_lock);
	if (sc->sc_stopping || status != 0)
		reasons |= RTW88_EXEC_STOP;
	if (!rtw88_net_io_begin(core->sc_net)) {
		mutex_exit(&sc->sc_lock);
		return;
	}
	mutex_exit(&sc->sc_lock);
	if ((reasons & RTW88_EXEC_STOP) == 0) {
		rtw88_net_hw_update(core->sc_net);
		if (core->sc_running && core->sc_started) {
			error = rtw88_sdio_hci_poll(core);
			if (error != 0) {
				rtwsdio_fault(sc, error);
				reasons |= RTW88_EXEC_STOP;
			}
		}
	}
	rtw88_async_service(core, reasons);
	if ((reasons & RTW88_EXEC_STOP) == 0 && core->sc_running &&
	    core->sc_started && !core->sc_stop_requested) {
		error = rtw88_sdio_hci_poll(core);
		if (error != 0)
			rtwsdio_fault(sc, error);
	}
	rtw88_net_hw_update(core->sc_net);
	mutex_enter(&sc->sc_lock);
	reopen = rtw88_net_io_end(core->sc_net);
	mutex_exit(&sc->sc_lock);
	if (reopen && (reasons & RTW88_EXEC_STOP) == 0 &&
	    core->sc_running && core->sc_started && !core->sc_stop_requested) {
		error = rtw88_sdio_hci_reopen(core, true);
		if (error != 0)
			rtwsdio_fault(sc, error);
	}
}

static int
rtwsdio_intr(void *cookie)
{
	struct rtwsdio_softc *sc = cookie;

	/* No core reads or card I/O in the parent callback. It runs under the
	 * parent mutex; disestablish removes/drains it under that same mutex. */
	mutex_enter(&sc->sc_lock);
	if (!sc->sc_stopping)
		rtw88_executor_schedule(&sc->sc_core, RTW88_EXEC_RX);
	mutex_exit(&sc->sc_lock);
	return 1;
}

static void
rtwsdio_mountroot(device_t self)
{
	struct rtwsdio_softc *sc = device_private(self);

	mutex_enter(&sc->sc_lock);
	if (!sc->sc_stopping)
		rtw88_executor_schedule(&sc->sc_core, RTW88_EXEC_BOOTSTRAP);
	mutex_exit(&sc->sc_lock);
}

static void
rtwsdio_attach(device_t parent, device_t self, void *aux)
{
	struct rtwsdio_softc *sc = device_private(self);
	const struct sdmmc_attach_args *aa = aux;
	const struct rtw8723d_core_hooks hooks = {
		.wait_ms = rtwsdio_wait, .revoked = rtwsdio_cancelled, .cookie = sc
	};
	const struct rtw88_sdio_hci_parent hci_parent = {
		.acquire = rtwsdio_acquire, .irq_start = rtwsdio_irq_start,
		.release = rtwsdio_release, .status = rtwsdio_status,
		.revoked = rtwsdio_cancelled, .rx_epoch = rtwsdio_rx_epoch,
		.rx_current = rtwsdio_rx_current, .retry_ready = rtwsdio_retry_ready,
		.retry_reset = rtwsdio_retry_reset, .again = rtwsdio_again,
		.fault = rtwsdio_fault, .cookie = sc
	};
	int error;

	sc->sc_dev = self;
	sc->sc_sf = aa->sf;
	sc->sc_state = RTWSDIO_ATTACHED;
	/* The retry callout and network softint use this interlock. */
	mutex_init(&sc->sc_lock, MUTEX_DEFAULT, IPL_NET);
	callout_init(&sc->sc_retry, CALLOUT_MPSAFE);
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
	sc->sc_tx = (struct rtw88_sdio_tx_context) {
		.bus = &sc->sc_bus, .packet = (uint8_t *)sc->sc_tx_packet,
		.packet_cap = sizeof(sc->sc_tx_packet)
	};
	sc->sc_rx = (struct rtw88_sdio_rx_context) {
		.bus = &sc->sc_bus, .packet = (uint8_t *)sc->sc_rx_packet,
		.packet_cap = sizeof(sc->sc_rx_packet),
		.scratch = (uint8_t *)sc->sc_rx_scratch,
		.scratch_cap = sizeof(sc->sc_rx_scratch), .batch = &sc->sc_rx_batch
	};
	sc->sc_core.sc_ops = &rtwsdio_bus_ops;
	sc->sc_core.sc_bus = sc;
	sc->sc_core.sc_defer_irq = rtwsdio_defer_irq;
	aprint_normal(": RTL8723DS native SDIO\n");
	if (!rtwsdio_admitted(parent)) {
		error = ENOTSUP;
		goto fail;
	}
	error = rtw88_sdio_hci_bind(&sc->sc_hci_ctx, &sc->sc_core,
	    &sc->sc_bus, &sc->sc_tx, &sc->sc_rx, &hci_parent);
	if (error == 0)
		error = rtw8723d_chip_bind(&sc->sc_core, &sc->sc_chip, &hooks);
	if (error == 0)
		error = rtw88_core_attach(&sc->sc_core);
	if (error != 0)
		goto fail;
	sc->sc_core_attached = true;
	/* All callbacks/storage exist before the sole actor or producers. */
	error = rtw88_executor_create(&sc->sc_core, rtwsdio_core_service);
	if (error != 0)
		goto fail;
	config_mountroot(self, rtwsdio_mountroot);
	return;
fail:
	sc->sc_error = error;
	sc->sc_state = RTWSDIO_FAILED;
	aprint_error_dev(self, "native attach unavailable: %d\n", error);
}

static int
rtwsdio_stop(struct rtwsdio_softc *sc)
{
	struct rtw88_net *net;
	int error;

	if (rtw88_executor_owned(&sc->sc_core))
		return EDEADLK;
	mutex_enter(&sc->sc_lock);
	if (sc->sc_state == RTWSDIO_DETACHED) {
		mutex_exit(&sc->sc_lock);
		return 0;
	}
	/* LOADING covers all bootstrap work through net publication. Do not
	 * mutate admission or cancellation until its owner has completed. */
	if (sc->sc_state == RTWSDIO_LOADING) {
		mutex_exit(&sc->sc_lock);
		return EBUSY;
	}
	net = sc->sc_core.sc_net;
	sc->sc_stopping = true;
	sc->sc_state = RTWSDIO_STOPPING;
	sc->sc_retry_armed = sc->sc_retry_due = false;
	mutex_exit(&sc->sc_lock);
	error = config_mountroot_cancel(sc->sc_dev);
	if (error != 0)
		return error;
	callout_halt(&sc->sc_retry, NULL);
	if (sc->sc_core.sc_executor != NULL) {
		/* The stable published net closes its own external admission before
		 * scheduling and draining the hardware STOP. */
		if (net != NULL) {
			error = rtw88_net_detach(net);
			if (error != 0)
				return error;
		} else {
			rtw88_executor_schedule(&sc->sc_core, RTW88_EXEC_STOP);
			error = rtw88_executor_drain(&sc->sc_core);
			if (error != 0)
				return error;
		}
	}
	/* HCI callbacks are now pure offline checks. No process-context bus
	 * cleanup is allowed after a failed/uncertain actor STOP. */
	if (sc->sc_core_attached) {
		error = rtw88_core_detach(&sc->sc_core);
		if (error != 0)
			return error;
		sc->sc_core_attached = false;
	}
	if (sc->sc_core.sc_executor != NULL) {
		error = rtw88_executor_destroy(&sc->sc_core);
		if (error != 0)
			return error;
	}
	mutex_enter(&sc->sc_lock);
	sc->sc_state = RTWSDIO_DETACHED;
	mutex_exit(&sc->sc_lock);
	return 0;
}

static int
rtwsdio_detach(device_t self, int flags __unused)
{
	struct rtwsdio_softc *sc = device_private(self);
	int error = rtwsdio_stop(sc);

	if (error == 0) {
		callout_destroy(&sc->sc_retry);
		mutex_destroy(&sc->sc_lock);
	}
	return error;
}
