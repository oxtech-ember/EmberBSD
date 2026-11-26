/*-
 * Copyright (c) 2026 Anton and EmberBSD contributors
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * AICSemi AIC8800D80 FullMAC wireless adapter on SDIO.
 *
 * The chip starts in a boot ROM that takes debug messages: read and
 * write memory, start a program. The driver uploads the firmware with
 * them and then talks to it in the same framing.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/callout.h>
#include <sys/condvar.h>
#include <sys/device.h>
#include <sys/atomic.h>
#include <sys/endian.h>
#include <sys/evcnt.h>
#include <sys/kernel.h>
#include <sys/kauth.h>
#include <sys/kmem.h>
#include <sys/mbuf.h>
#include <sys/mutex.h>
#include <sys/sha2.h>
#include <sys/pool.h>
#include <sys/proc.h>
#include <sys/socket.h>
#include <sys/systm.h>
#include <sys/workqueue.h>

#include <net/bpf.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_ether.h>
#include <net/if_media.h>
#include <net/route.h>

#include <netinet/in.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_beacon_ies.h>
#include <net80211/ieee80211_amsdu.h>

#include <dev/firmload.h>
#include <net80211/ieee80211_rsn.h>
#include <net80211/ieee80211_sta.h>
#include <dev/ic/aicwf_command.h>
#include <dev/sdmmc/if_aicwf_fw_bip.h>
#include <dev/sdmmc/sdmmcchip.h>
#include <dev/sdmmc/sdmmcvar.h>

#include <dev/ic/aicwfvar.h>

#define	AICWF_SDIO_VENDOR		0xc8a1
#define	AICWF_SDIO_PRODUCT_D80		0x0082

/* Function 1 registers. */
#define	AICWF_INTR_ENABLE		0x00
#define	 AICWF_INTR_ENABLE_ALL		0x07
#define	AICWF_INTR_PENDING		0x01
#define	 AICWF_INTR_PENDING_SOFT	__BIT(0)
#define	AICWF_FLOW_CTRL			0x03	/* free transmit buffers */
#define	AICWF_INTR_STATUS		0x04
#define	 AICWF_INTR_STATUS_OTHER	__BIT(7)
#define	 AICWF_INTR_STATUS_BLOCKS	__BITS(6,0)
#define	  AICWF_INTR_STATUS_BYTEMODE	120
#define	AICWF_BYTEMODE_LEN		0x05	/* in words */
#define	AICWF_BYTEMODE_ENABLE		0x07
#define	 AICWF_BYTEMODE_DISABLED	1
#define	AICWF_RD_FIFO			0x0f
#define	AICWF_WR_FIFO			0x10

/* Function 0 register that selects the pad settings the vendor uses. */
#define	AICWF_FN0_PAD			0xf2
#define	 AICWF_FN0_PAD_DEFAULT		0x7f

#define	AICWF_BLOCK_SIZE		512
#define	AICWF_TX_BUFFER_SIZE		1536	/* one unit of flow control */
#define	AICWF_RX_MAX			(119 * AICWF_BLOCK_SIZE)

/* Debug task: what the boot ROM understands. */
#define	AICWF_DBG_MEM_WRITE_REQ		0x0402
#define	AICWF_DBG_MEM_WRITE_CFM		0x0403
#define	AICWF_DBG_MEM_BLOCK_WRITE_REQ	0x040b
#define	AICWF_DBG_MEM_BLOCK_WRITE_CFM	0x040c
#define	AICWF_DBG_START_APP_REQ		0x040d
#define	AICWF_DBG_START_APP_CFM		0x040e
#define	 AICWF_START_APP_AUTO		1

#define	AICWF_BLOCK_WRITE_MAX		1024


#define	AICWF_MM_GET_FW_VERSION_REQ	0x0080
#define	AICWF_MM_GET_FW_VERSION_CFM	0x0081
#define	AICWF_MM_SET_RF_CALIB_REQ	0x0069
#define	AICWF_MM_SET_RF_CALIB_CFM	0x006a

/* Radio calibration, the vendor's defaults for this chip. */
#define	AICWF_CAL_CFG_2G		0x0f8f
#define	AICWF_CAL_CFG_5G		0x0f0f
#define	AICWF_CAL_PARAM_ALPHA		0x0c34c008
#define	AICWF_CAL_BT_PARAM		0x00264203

#define	AICWF_CHIP_ID_ADDR		0x40500000
#define	 AICWF_CHIP_ID_REV		__BITS(21,16)
#define	 AICWF_CHIP_ID_H		__BITS(23,22)

/*
 * Firmware. It runs from RAM at AICWF_FW_ADDR and reads its options from
 * a table of (offset, value) pairs that the driver writes into it.
 */
#define	AICWF_FW_NAME			"fmacfw_8800d80_u02.bin"
#define	AICWF_FW_NAME_H			"fmacfw_8800d80_h_u02.bin"
#define	AICWF_FW_ADDR			0x00120000
#define	AICWF_FW_VERSION		(AICWF_FW_ADDR + 0x01c)
#define	AICWF_FW_CONFIG_BASE		(AICWF_FW_ADDR + 0x198)
#define	AICWF_FW_PATCH_DESC		(AICWF_FW_ADDR + 0x1a0)
#define	AICWF_FW_PATCH_BUF		(AICWF_FW_ADDR + 0x1a4)
#define	AICWF_FW_PATCH_BUF_OLD		0x0016f800
#define	AICWF_FW_VERSION_PATCH_BUF	0x06090100
/*
 * Bluetooth. The controller half of the chip talks HCI on its own UART,
 * but its patches go in through the boot ROM, before the wireless
 * firmware starts. The patch table is a tagged list of entries, each a
 * name, a type and (address, value) pairs.
 */
#define	AICWF_BT_TABLE_NAME		"fw_patch_table_8800d80_u02.bin"
#define	AICWF_BT_ADID_NAME		"fw_adid_8800d80_u02.bin"
#define	AICWF_BT_PATCH_NAME		"fw_patch_8800d80_u02.bin"
#define	AICWF_BT_EXT_NAME		"fw_patch_8800d80_u02_ext%u.bin"
#define	AICWF_BT_TABLE_TAG		"AICBT_PT_TAG"
#define	AICWF_BT_TABLE_TAG_LEN		16
#define	AICWF_BT_ENTRY_HDR		24
#define	AICWF_BT_ENTRY_TYPE_OFF		16
#define	AICWF_BT_ENTRY_LEN_OFF		20
#define	AICWF_BT_PT_INF			0
#define	AICWF_BT_PT_BTMODE		3
#define	AICWF_BT_PT_PWRON		4
#define	AICWF_BT_PT_VER			6
#define	AICWF_BT_PT_NODATA		1000
/* Pairs of the information entry. */
#define	AICWF_BT_INF_ADID		0
#define	AICWF_BT_INF_PATCH		1
#define	AICWF_BT_INF_WRITES		4	/* pairs the chip is given */
#define	AICWF_BT_INF_EXT_COUNT		4
#define	AICWF_BT_INF_EXT		5
#define	AICWF_BT_ADID_ADDR		0x00201940
#define	AICWF_BT_PATCH_ADDR		0x0020b43c
/* Values of the mode entry, in the order of its pairs. */
#define	AICWF_BT_MODE_NO_SWITCH		5	/* shares the antenna */
#define	AICWF_BT_PORT_UART		2
#define	AICWF_BT_BAUD			1500000
#define	AICWF_BT_TXPWR			0x00006f2f

#define	AICWF_PATCH_MAGIC		0x48435450	/* "PTCH" */
#define	AICWF_PATCH_MAGIC_2		0x50544348	/* "HCTP" */
/* Offsets in the patch descriptor. */
#define	AICWF_PATCH_MAGIC_OFF		0x00
#define	AICWF_PATCH_PAIRS_OFF		0x04
#define	AICWF_PATCH_MAGIC_2_OFF		0x08
#define	AICWF_PATCH_COUNT_OFF		0x0c
#define	AICWF_PATCH_BLOCK_SIZE_OFF	0x30	/* four words */

#define	AICWF_CMD_TIMEOUT_MS		2000

/* The options the vendor driver gives this firmware by default. */
static const uint32_t aicwf_fw_options[][2] = {
	{ 0x00b4, 0xf3010001 },		/* 2.4 and 5 GHz */
	{ 0x0188, 0x00000001 },		/* power offsets cover calibration */
};


struct aicwf_sdio_softc {
	struct aicwf_softc sc_common;
	struct sdmmc_function *sc_sf;
	void *sc_ih;
	kmutex_t sc_bus_lock;
	size_t sc_io_max;
	uint8_t *sc_rxbuf;
};

static int	aicwf_match(device_t, cfdata_t, void *);
static void	aicwf_attach(device_t, device_t, void *);
static void	aicwf_attachhook(device_t);
static int	aicwf_intr(void *);

static int aicwf_fw_bip_runtime_verify(struct aicwf_softc *);
CFATTACH_DECL_NEW(aicwf_sdio, sizeof(struct aicwf_sdio_softc),
    aicwf_match, aicwf_attach, NULL, NULL);

/* CRC-8, polynomial x^8 + x^2 + x + 1, as the chip checks it. */
static uint8_t
aicwf_crc8(const uint8_t *buf, size_t len)
{
	uint8_t crc = 0;
	u_int bit;

	while (len-- > 0) {
		for (bit = 0x80; bit != 0; bit >>= 1) {
			const bool carry = (crc & 0x80) != 0;

			crc <<= 1;
			if (carry)
				crc ^= 0x07;
			if ((*buf & bit) != 0)
				crc ^= 0x07;
		}
		buf++;
	}

	return crc;
}

/*
 * Move whole blocks between a buffer and the chip. A host controller
 * takes only so much in one transfer, so a long one goes in parts.
 */
static int
aicwf_fifo(struct aicwf_sdio_softc *sc, bool write, uint8_t *buf, size_t len)
{
	int error = 0;

	KASSERT(mutex_owned(&sc->sc_bus_lock));

	while (len > 0 && error == 0) {
		const size_t n = MIN(len, sc->sc_io_max);

		if (write)
			error = sdmmc_io_write_multi_1(sc->sc_sf,
			    AICWF_WR_FIFO, buf, n);
		else
			error = sdmmc_io_read_multi_1(sc->sc_sf,
			    AICWF_RD_FIFO, buf, n);
		buf += n;
		len -= n;
	}

	return error;
}

/* Wait until the chip has room for a frame of this many bytes. */
static int
aicwf_tx_wait(struct aicwf_sdio_softc *sc, size_t len, u_int reserve)
{
	u_int tries, buffers = 0;

	for (tries = 0; tries < 50; tries++) {
		mutex_enter(&sc->sc_bus_lock);
		/* D80 uses all eight bits, unlike the older 8801/DC/DW. */
		buffers = sdmmc_io_read_1(sc->sc_sf, AICWF_FLOW_CTRL);
		mutex_exit(&sc->sc_bus_lock);

		if (buffers > reserve &&
		    len <= (buffers - reserve) * AICWF_TX_BUFFER_SIZE)
			return 0;
		sdmmc_pause(tries < 30 ? 200 : 10000, NULL);
	}

	IEEE80211_DPRINTF(&sc->sc_common.sc_ic, IEEE80211_MSG_STATE,
	    "SDIO transmit busy: buffers %u reserve %u length %zu\n",
	    buffers, reserve, len);
	return EBUSY;
}

/*
 * Write a frame of len bytes from the transmit buffer: whole words, and
 * a frame that ends inside a block gets a zero word after it.
 */
static int
aicwf_sdio_write(struct aicwf_softc *common, size_t len, u_int reserve,
    bool *attempted)
{
	struct aicwf_sdio_softc * const sc = common->sc_bus;
	uint8_t * const buf = common->sc_txbuf;
	size_t total;
	int error;

	KASSERT(mutex_owned(&common->sc_lock));
	buf[3] = aicwf_crc8(buf, 3);

	total = roundup(len, 4);
	if (total % AICWF_BLOCK_SIZE != 0)
		total = roundup(total + AICWF_HDR_LEN, AICWF_BLOCK_SIZE);
	memset(buf + len, 0, total - len);

	if (attempted != NULL)
		*attempted = false;
	error = aicwf_tx_wait(sc, total, reserve);
	if (error == 0) {
		mutex_enter(&sc->sc_bus_lock);
		if (attempted != NULL)
			*attempted = true;
		error = aicwf_fifo(sc, true, buf, total);
		mutex_exit(&sc->sc_bus_lock);
	}

	return error;
}

static int
aicwf_intr(void *arg)
{
	struct aicwf_sdio_softc * const sc = arg;
	struct aicwf_softc * const common = &sc->sc_common;
	struct sdmmc_function * const sf = sc->sc_sf;
	uint8_t status, pending;
	uint64_t serial;
	size_t len;
	int error;

	mutex_enter(&sc->sc_bus_lock);
	status = sdmmc_io_read_1(sf, AICWF_INTR_STATUS);
	if (status == 0) {
		mutex_exit(&sc->sc_bus_lock);
		return 0;
	}

	if ((status & AICWF_INTR_STATUS_OTHER) != 0) {
		pending = sdmmc_io_read_1(sf, AICWF_INTR_PENDING);
		sdmmc_io_write_1(sf, AICWF_INTR_PENDING,
		    pending & ~AICWF_INTR_PENDING_SOFT);
	}

	status = __SHIFTOUT(status, AICWF_INTR_STATUS_BLOCKS);
	if (status == AICWF_INTR_STATUS_BYTEMODE)
		len = sdmmc_io_read_1(sf, AICWF_BYTEMODE_LEN) * 4;
	else
		len = status * AICWF_BLOCK_SIZE;
	if (len == 0 || len > AICWF_RX_MAX) {
		mutex_exit(&sc->sc_bus_lock);
		return 1;
	}
	/* An entire FIFO read belongs to this host lifetime. A connection/key
	 * event inside the batch can retire it, but cannot relabel its data. */
	mutex_spin_enter(&common->sc_epoch_lock);
	serial = common->sc_rx_serial;
	mutex_spin_exit(&common->sc_epoch_lock);
	error = aicwf_fifo(sc, false, sc->sc_rxbuf, len);
	mutex_exit(&sc->sc_bus_lock);

	if (error != 0) {
		device_printf(common->sc_dev, "read of %zu bytes failed\n", len);
		return 1;
	}
	aicwf_rx(common, sc->sc_rxbuf, len, serial);

	return 1;
}

static int
aicwf_mem_read(struct aicwf_softc *sc, uint32_t addr, uint32_t *data)
{
	uint8_t req[4], cfm[8];
	int error;

	le32enc(req, addr);
	error = aicwf_cmd(sc, AICWF_DBG_MEM_READ_REQ, req, sizeof(req),
	    AICWF_DBG_MEM_READ_CFM, cfm, sizeof(cfm));
	if (error == 0)
		*data = le32dec(cfm + 4);

	return error;
}

static int
aicwf_mem_write(struct aicwf_softc *sc, uint32_t addr, uint32_t data)
{
	uint8_t req[8], cfm[8];

	le32enc(req, addr);
	le32enc(req + 4, data);
	return aicwf_cmd(sc, AICWF_DBG_MEM_WRITE_REQ, req, sizeof(req),
	    AICWF_DBG_MEM_WRITE_CFM, cfm, sizeof(cfm));
}

static int
aicwf_fw_mem_read(struct aicwf_softc *sc, uint32_t addr, uint32_t *data)
{
	uint8_t req[4], cfm[8];
	int error;

	if (!sc->sc_fw_bip_pending)
		return aicwf_mem_read(sc, addr, data);
	le32enc(req, addr);
	error = aicwf_fw_cmd(sc, AICWF_DBG_MEM_READ_REQ, req, sizeof(req),
	    AICWF_DBG_MEM_READ_CFM, cfm, sizeof(cfm));
	if (error != 0)
		return error;
	if (!aicwf_read_confirm(cfm, sizeof(cfm), addr, data))
		return EPROTO;
	return 0;
}

static int
aicwf_fw_mem_write(struct aicwf_softc *sc, uint32_t addr, uint32_t data)
{
	uint8_t req[8], cfm[8];
	int error;

	if (!sc->sc_fw_bip_pending)
		return aicwf_mem_write(sc, addr, data);
	le32enc(req, addr);
	le32enc(req + 4, data);
	error = aicwf_fw_cmd(sc, AICWF_DBG_MEM_WRITE_REQ, req, sizeof(req),
	    AICWF_DBG_MEM_WRITE_CFM, cfm, sizeof(cfm));
	if (error != 0)
		return error;
	if (le32dec(cfm) != addr || le32dec(cfm + 4) != data)
		return EPROTO;
	return 0;
}

static int
aicwf_fw_read(struct aicwf_softc *sc, const char *name, uint8_t **datap,
    size_t *sizep)
{
	firmware_handle_t fh;
	void *data;
	size_t size;
	int error;

	error = firmware_open("if_aicwf", name, &fh);
	if (error != 0) {
		aprint_error_dev(sc->sc_dev, "couldn't open %s: %d\n",
		    name, error);
		return error;
	}
	error = firmware_read_alloc(fh, SIZE_MAX, &data, &size);
	firmware_close(fh);
	if (error != 0)
		return error;
	*datap = data;
	*sizep = size;

	return 0;
}

/* Copy a firmware file into the chip's memory. */
static int
aicwf_upload(struct aicwf_softc *sc, const char *name, uint32_t addr)
{
	uint8_t req[8 + AICWF_BLOCK_WRITE_MAX], cfm[4];
	uint8_t *data;
	size_t size, off, n;
	int error;

	if ((error = aicwf_fw_read(sc, name, &data, &size)) != 0)
		return error;

	for (off = 0; off < size && error == 0; off += n) {
		n = MIN(size - off, AICWF_BLOCK_WRITE_MAX);
		memset(req, 0, sizeof(req));
		le32enc(req, addr + off);
		le32enc(req + 4, n);
		memcpy(req + 8, data + off, n);
		error = aicwf_cmd(sc, AICWF_DBG_MEM_BLOCK_WRITE_REQ, req,
		    sizeof(req), AICWF_DBG_MEM_BLOCK_WRITE_CFM, cfm,
		    sizeof(cfm));
	}
	firmware_free(data, size);
	if (error == 0)
		aprint_verbose_dev(sc->sc_dev, "%s: %zu bytes\n", name, size);

	return error;
}

/* The addresses of the Bluetooth patches, then the patches themselves. */
static int
aicwf_bt_patches(struct aicwf_softc *sc, const uint8_t *pair, u_int npairs)
{
	uint32_t adid = AICWF_BT_ADID_ADDR, patch = AICWF_BT_PATCH_ADDR;
	char name[sizeof(AICWF_BT_EXT_NAME) + 10];
	u_int ext = 0, i;
	int error;

	if (npairs == 0)
		return EINVAL;
	adid = le32dec(pair + AICWF_BT_INF_ADID * 8 + 4);
	if (npairs > AICWF_BT_INF_PATCH)
		patch = le32dec(pair + AICWF_BT_INF_PATCH * 8 + 4);
	if (npairs > AICWF_BT_INF_EXT_COUNT)
		ext = le32dec(pair + AICWF_BT_INF_EXT_COUNT * 8 + 4);
	if (ext > npairs - MIN(npairs, AICWF_BT_INF_EXT))
		return EINVAL;

	if ((error = aicwf_upload(sc, AICWF_BT_ADID_NAME, adid)) != 0 ||
	    (error = aicwf_upload(sc, AICWF_BT_PATCH_NAME, patch)) != 0)
		return error;
	for (i = 0; i < ext; i++) {
		const uint8_t *p = pair + (AICWF_BT_INF_EXT + i) * 8;

		snprintf(name, sizeof(name), AICWF_BT_EXT_NAME, le32dec(p));
		if ((error = aicwf_upload(sc, name, le32dec(p + 4))) != 0)
			return error;
	}

	return 0;
}

static void
aicwf_fw_bip_hash(const uint8_t *data, size_t size, uint8_t digest[32])
{
	SHA256_CTX ctx;

	SHA256_Init(&ctx);
	SHA256_Update(&ctx, data, size);
	SHA256_Final(digest, &ctx);
	explicit_memset(&ctx, 0, sizeof(ctx));
}

/* WLAN owns this RAM copy. The general Bluetooth uploader stays unchanged. */
static int
aicwf_wlan_upload(struct aicwf_softc *sc, bool h)
{
	uint8_t req[8 + AICWF_BLOCK_WRITE_MAX], cfm[4];
	uint8_t *data;
	size_t size, off, n;
	int error;

	sc->sc_fw_bip_pending = false;
	sc->sc_fw_bip_verified = false;
	sc->sc_fw_bip_ready = false;
	sc->sc_fw_bip_wstatus_valid = false;
	sc->sc_fw_bip_bootstatus_valid = false;
	error = aicwf_fw_read(sc, h ? AICWF_FW_NAME_H : AICWF_FW_NAME,
	    &data, &size);
	if (error != 0)
		return error;
	error = aicwf_fw_bip_patch(data, size, h, &sc->sc_fw_bip_pending,
	    aicwf_fw_bip_hash);
	if (error != 0)
		goto out;
	for (off = 0; off < size; off += n) {
		n = MIN(size - off, AICWF_BLOCK_WRITE_MAX);
		memset(req, 0, sizeof(req));
		le32enc(req, AICWF_FW_ADDR + off);
		le32enc(req + 4, n);
		memcpy(req + 8, data + off, n);
		error = aicwf_fw_cmd(sc, AICWF_DBG_MEM_BLOCK_WRITE_REQ, req,
		    sizeof(req), AICWF_DBG_MEM_BLOCK_WRITE_CFM, cfm, sizeof(cfm));
		/* wstatus is opaque. The final code readback proves these bytes. */
		if (error != 0)
			goto out;
		if (sc->sc_fw_bip_pending) {
			sc->sc_fw_bip_wstatus = le32dec(cfm);
			sc->sc_fw_bip_wstatus_valid = true;
		}
	}
out:
	firmware_free(data, size);
	return error;
}

/*
 * Prepare the Bluetooth controller: upload its patches and write the
 * patch table, with the mode entry describing how the host reaches it.
 */
static int
aicwf_bt_load(struct aicwf_softc *sc)
{
	static const uint32_t mode[] = {
		1,			/* no hardware information */
		0xffffffff,
		0,
		AICWF_BT_MODE_NO_SWITCH,
		AICWF_BT_PORT_UART,
		AICWF_BT_BAUD,
		1,			/* RTS/CTS */
		0,			/* no low power mode */
		AICWF_BT_TXPWR,
	};
	uint8_t *tbl;
	size_t size, off;
	uint32_t type, value;
	u_int npairs, pass, i;
	bool found = false;
	int error;

	if ((error = aicwf_fw_read(sc, AICWF_BT_TABLE_NAME, &tbl, &size)) != 0)
		return error;
	if (size < AICWF_BT_TABLE_TAG_LEN ||
	    memcmp(tbl, AICWF_BT_TABLE_TAG, sizeof(AICWF_BT_TABLE_TAG)) != 0) {
		error = EINVAL;
		goto out;
	}

	/* The first pass uploads the patches, the second writes the pairs. */
	for (pass = 0; pass < 2; pass++) {
		off = AICWF_BT_TABLE_TAG_LEN;
		while (size - off >= AICWF_BT_ENTRY_HDR) {
			const uint8_t *pair;

			type = le32dec(tbl + off + AICWF_BT_ENTRY_TYPE_OFF);
			npairs = le32dec(tbl + off + AICWF_BT_ENTRY_LEN_OFF);
			off += AICWF_BT_ENTRY_HDR;
			if (type >= AICWF_BT_PT_NODATA)
				continue;
			if (npairs > (size - off) / 8) {
				error = EINVAL;
				goto out;
			}
			pair = tbl + off;
			off += npairs * 8;

			if (pass == 0) {
				if (type != AICWF_BT_PT_INF)
					continue;
				error = aicwf_bt_patches(sc, pair, npairs);
				if (error != 0)
					goto out;
				found = true;
				continue;
			}
			if (type == AICWF_BT_PT_VER)
				continue;
			if (type == AICWF_BT_PT_INF)
				npairs = MIN(npairs, AICWF_BT_INF_WRITES);
			for (i = 0; i < npairs; i++) {
				value = le32dec(pair + i * 8 + 4);
				if (type == AICWF_BT_PT_BTMODE &&
				    i < __arraycount(mode))
					value = mode[i];
				error = aicwf_mem_write(sc,
				    le32dec(pair + i * 8), value);
				if (error != 0)
					goto out;
			}
			if (type == AICWF_BT_PT_PWRON)
				kpause("aicwfbt", false, hz / 10, NULL);
		}
		if (!found) {
			error = ENOENT;
			goto out;
		}
	}
out:
	firmware_free(tbl, size);

	return error;
}

/* Hand the firmware its options through the patch table. */
static int
aicwf_fw_options_set(struct aicwf_softc *sc)
{
	uint32_t config_base, desc, fw_version, pairs;
	u_int n;
	int error;

	if ((error = aicwf_fw_mem_read(sc, AICWF_FW_CONFIG_BASE,
	    &config_base)) != 0 ||
	    (error = aicwf_fw_mem_read(sc, AICWF_FW_PATCH_DESC, &desc)) != 0 ||
	    (error = aicwf_fw_mem_read(sc, AICWF_FW_VERSION, &fw_version)) != 0)
		return error;
	pairs = AICWF_FW_PATCH_BUF_OLD;
	if (fw_version > AICWF_FW_VERSION_PATCH_BUF &&
	    (error = aicwf_fw_mem_read(sc, AICWF_FW_PATCH_BUF, &pairs)) != 0)
		return error;
	aprint_verbose_dev(sc->sc_dev, "firmware version 0x%08x\n",
	    fw_version);

	if (sc->sc_fw_bip_pending &&
	    !aicwf_fw_bip_map(config_base, desc, fw_version, pairs,
	    aicwf_fw_options, __arraycount(aicwf_fw_options)))
		return EIO;

	if ((error = aicwf_fw_mem_write(sc, desc + AICWF_PATCH_MAGIC_OFF,
	    AICWF_PATCH_MAGIC)) != 0 ||
	    (error = aicwf_fw_mem_write(sc, desc + AICWF_PATCH_MAGIC_2_OFF,
	    AICWF_PATCH_MAGIC_2)) != 0 ||
	    (error = aicwf_fw_mem_write(sc, desc + AICWF_PATCH_PAIRS_OFF,
	    pairs)) != 0 ||
	    (error = aicwf_fw_mem_write(sc, desc + AICWF_PATCH_COUNT_OFF,
	    __arraycount(aicwf_fw_options))) != 0)
		return error;
	for (n = 0; n < __arraycount(aicwf_fw_options); n++) {
		if ((error = aicwf_fw_mem_write(sc, pairs + 8 * n,
		    config_base + aicwf_fw_options[n][0])) != 0 ||
		    (error = aicwf_fw_mem_write(sc, pairs + 8 * n + 4,
		    aicwf_fw_options[n][1])) != 0)
			return error;
	}
	for (n = 0; n < 4; n++) {
		if ((error = aicwf_fw_mem_write(sc,
		    desc + AICWF_PATCH_BLOCK_SIZE_OFF + 4 * n, 0)) != 0)
			return error;
	}

	return 0;
}

/* This is the final memory operation before START_APP in the patched path. */
static int
aicwf_fw_bip_verify(struct aicwf_softc *sc)
{
	uint32_t config, desc, fw_version, pairs, addr, expected, actual;
	unsigned n;
	int error;

	sc->sc_fw_bip_verified = false;
	sc->sc_fw_bip_ready = false;
	if (!sc->sc_fw_bip_pending)
		return 0;
	if ((error = aicwf_fw_mem_read(sc, AICWF_FW_CONFIG_BASE, &config)) != 0 ||
	    (error = aicwf_fw_mem_read(sc, AICWF_FW_PATCH_DESC, &desc)) != 0 ||
	    (error = aicwf_fw_mem_read(sc, AICWF_FW_VERSION, &fw_version)) != 0 ||
	    (error = aicwf_fw_mem_read(sc, AICWF_FW_PATCH_BUF, &pairs)) != 0)
		return error;
	if (!aicwf_fw_bip_map(config, desc, fw_version, pairs,
	    aicwf_fw_options, __arraycount(aicwf_fw_options)))
		return EIO;
	for (n = 0; n < 12; n++) {
		if (!aicwf_fw_bip_meta_word(n, &addr, &expected))
			return EIO;
		if ((error = aicwf_fw_mem_read(sc, addr, &actual)) != 0)
			return error;
		if (actual != expected)
			return EIO;
	}
	for (n = 0; n < 181; n++) {
		if (!aicwf_fw_bip_word(n, &addr, &expected))
			return EIO;
		if ((error = aicwf_fw_mem_read(sc, addr, &actual)) != 0)
			return error;
		if (actual != expected)
			return EIO;
	}
	sc->sc_fw_bip_verified = true;
	return 0;
}

static int
aicwf_fw_start(struct aicwf_softc *sc)
{
	uint8_t req[8], cfm[4];
	int error;

	le32enc(req, AICWF_FW_ADDR);
	le32enc(req + 4, AICWF_START_APP_AUTO);
	error = aicwf_fw_cmd(sc, AICWF_DBG_START_APP_REQ, req, sizeof(req),
	    AICWF_DBG_START_APP_CFM, cfm, sizeof(cfm));
	if (error == 0 && sc->sc_fw_bip_pending) {
		sc->sc_fw_bip_bootstatus = le32dec(cfm);
		sc->sc_fw_bip_bootstatus_valid = true;
	}
	if (error == 0)
		aprint_verbose_dev(sc->sc_dev,
		    "firmware started, status 0x%x\n", le32dec(cfm));

	return error;
}

static int
aicwf_wlan_load(struct aicwf_softc *sc, bool h)
{
	int error;

	if ((error = aicwf_wlan_upload(sc, h)) != 0 ||
	    (error = aicwf_fw_options_set(sc)) != 0 ||
	    (error = aicwf_fw_bip_verify(sc)) != 0 ||
	    (error = aicwf_fw_start(sc)) != 0) {
		sc->sc_fw_bip_ready = false;
		return error;
	}
	return 0;
}

static int
aicwf_fw_bip_runtime_verify(struct aicwf_softc *sc)
{
	uint32_t addr, expected, actual;
	unsigned n;
	int error;

	for (n = 0; n < 181; n++) {
		if (!aicwf_fw_bip_word(n, &addr, &expected))
			return EPROTO;
		if ((error = aicwf_fw_mem_read(sc, addr, &actual)) != 0)
			return error;
		if (actual != expected)
			return EPROTO;
	}
	return 0;
}

static int
aicwf_fw_init(struct aicwf_softc *sc)
{
	uint8_t req[256], cfm[64];
	int error;

	sc->sc_fw_bip_ready = false;
	/* Start the stack: on, no efuse override, vendor option bit 5. */
	memset(req, 0, sizeof(req));
	req[0] = 1;
	req[2] = __BIT(5);
	error = aicwf_cmd(sc, AICWF_MM_SET_STACK_START_REQ, req, 4,
	    AICWF_MM_SET_STACK_START_CFM, cfm, 2);
	if (error != 0)
		return error;
	sc->sc_5ghz = cfm[0] != 0;

	error = aicwf_cmd(sc, AICWF_MM_GET_FW_VERSION_REQ, NULL, 0,
	    AICWF_MM_GET_FW_VERSION_CFM, cfm, 64);
	if (error != 0)
		return error;
	cfm[MIN(cfm[0] + 1, 63)] = '\0';
	aprint_normal_dev(sc->sc_dev, "firmware %s%s\n", cfm + 1,
	    sc->sc_5ghz ? ", 5 GHz" : "");

	memset(req, 0, sizeof(req));
	le32enc(req + 0, AICWF_CAL_CFG_2G);
	le32enc(req + 4, AICWF_CAL_CFG_5G);
	le32enc(req + 8, AICWF_CAL_PARAM_ALPHA);
	le32enc(req + 16, AICWF_CAL_BT_PARAM);
	error = aicwf_cmd(sc, AICWF_MM_SET_RF_CALIB_REQ, req, 24,
	    AICWF_MM_SET_RF_CALIB_CFM, cfm, 16);
	if (error != 0) {
		aprint_error_dev(sc->sc_dev, "radio calibration failed\n");
		return error;
	}

	return aicwf_station_setup(sc);
}

static int
aicwf_match(device_t parent, cfdata_t match, void *aux)
{
	struct sdmmc_attach_args * const saa = aux;
	struct sdmmc_function * const sf = saa->sf;

	/* Not SDIO. */
	if (sf == NULL)
		return 0;

	const struct sdmmc_cis * const cis = &sf->sc->sc_fn0->cis;
	if (cis->manufacturer != AICWF_SDIO_VENDOR ||
	    cis->product != AICWF_SDIO_PRODUCT_D80)
		return 0;

	/* Everything goes through function 1. */
	return sf->number == 1;
}

static void
aicwf_attach(device_t parent, device_t self, void *aux)
{
	struct aicwf_sdio_softc * const sc = device_private(self);
	static const struct aicwf_bus_ops ops = {
		.write = aicwf_sdio_write,
		.verify = aicwf_fw_bip_runtime_verify,
	};
	struct sdmmc_attach_args * const saa = aux;
	struct sdmmc_function * const sf = saa->sf;

	aicwf_prepare(&sc->sc_common, self, &ops, sc, false);
	sc->sc_sf = sf;
	mutex_init(&sc->sc_bus_lock, MUTEX_DEFAULT, IPL_NONE);
	sc->sc_rxbuf = kmem_alloc(AICWF_RX_MAX, KM_SLEEP);

	aprint_naive("\n");
	aprint_normal(": AIC8800D80\n");

	sc->sc_io_max = MAX(AICWF_BLOCK_SIZE, rounddown(
	    sdmmc_chip_host_maxblklen(sf->sc->sc_sct, sf->sc->sc_sch),
	    AICWF_BLOCK_SIZE));
	sdmmc_io_set_blocklen(sf, AICWF_BLOCK_SIZE);
	if (sdmmc_io_function_enable(sf) != 0) {
		aprint_error_dev(self, "couldn't enable function 1\n");
		return;
	}
	sdmmc_io_write_1(sf->sc->sc_fn0, AICWF_FN0_PAD,
	    AICWF_FN0_PAD_DEFAULT);
	sdmmc_io_write_1(sf, AICWF_BYTEMODE_ENABLE, AICWF_BYTEMODE_DISABLED);

	sc->sc_ih = sdmmc_intr_establish(parent, aicwf_intr, sc,
	    device_xname(self));
	if (sc->sc_ih == NULL) {
		aprint_error_dev(self, "couldn't establish interrupt\n");
		return;
	}
	sdmmc_intr_enable(sf);
	sdmmc_io_write_1(sf, AICWF_INTR_ENABLE, AICWF_INTR_ENABLE_ALL);

	/*
	 * The rest waits for answers that arrive through the interrupt
	 * task, which shares its thread with this attachment.
	 */
	config_mountroot(self, aicwf_attachhook);
}

static void
aicwf_attachhook(device_t self)
{
	struct aicwf_sdio_softc * const ssc = device_private(self);
	struct aicwf_softc * const sc = &ssc->sc_common;
	uint32_t id;
	int error;

	error = aicwf_mem_read(sc, AICWF_CHIP_ID_ADDR, &id);
	if (error != 0) {
		aprint_error_dev(self, "the boot ROM does not answer: %d\n",
		    error);
		return;
	}
	const bool h = __SHIFTOUT(id, AICWF_CHIP_ID_H) == 3;
	aprint_normal_dev(self, "chip ID 0x%08x, revision %u%s\n", id,
	    (u_int)__SHIFTOUT(id, AICWF_CHIP_ID_REV), h ? ", H variant" : "");

	/* The wireless network works without the Bluetooth half. */
	if ((error = aicwf_bt_load(sc)) != 0)
		aprint_error_dev(self, "Bluetooth is not set up: %d\n",
		    error);

	if ((error = aicwf_wlan_load(sc, h)) != 0) {
		aprint_error_dev(self, "firmware start failed: %d\n", error);
		return;
	}

	/* The firmware takes a moment to come up. */
	kpause("aicwfup", false, hz / 5, NULL);
	if ((error = aicwf_fw_init(sc)) != 0) {
		aprint_error_dev(self, "firmware setup failed: %d\n", error);
		return;
	}

	if ((error = aicwf_ifattach(sc)) != 0)
		aprint_error_dev(self, "network interface setup failed: %d\n", error);
}
