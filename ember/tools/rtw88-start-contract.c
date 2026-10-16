/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD RTL8822CE firmware-bootstrap fake-bus tests, AI-assisted. */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rtw88-test-kernel.h"
#include <dev/ic/rtw88var.h>
#include <dev/ic/rtw8822c.h>
#include <dev/ic/rtw8822c_table.h>

static unsigned assertions, failures;

#define CHECK(x)                                                               \
	do {                                                                   \
		assertions++;                                                  \
		if (!(x)) {                                                    \
			failures++;                                            \
			fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__,     \
			    #x);                                               \
		}                                                              \
	} while (0)

static struct {
	uint8_t mmio[0x8000];
	uint8_t efuse[512];
	const uint8_t *fw;
	size_t fw_len;
	int fw_error;
	int master;
	int missing;
	int remove;
	int fail_syscfg;
	int fail_efuse;
	int busy_box;
	int fw_never_ready;
	int stop_at_delay;
	int bcn_ready;
	int bcn_dead;
	int ddma_bad_sum;
	unsigned rx_hw_idx;
	unsigned bcn_valid_sets;
	unsigned tx_hw_rp[2];	/* MGMT, BE read index reported by the chip. */
	unsigned barriers;
	uint32_t imr_at_barrier;
	unsigned fail_alloc_at;	/* 1-based allocation that fails, or 0. */
	unsigned alloc_calls;
	unsigned delays;
	unsigned writes;
	unsigned cfg_writes;
	unsigned dma_allocs;
	struct rtw88_softc *sc;
} f;

static const uint8_t fw_header[64] = {
	0x22, 0x88, 0x00, 0x00, 0x09, 0x00, 0x09, 0x0b,
	0x00, 0x00, 0x00, 0x00, 0xe7, 0x01, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x10, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x20, 0x80, 0x80, 0x4b, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x30, 0xd3, 0x00, 0x00, 0xd0, 0xf7, 0x01, 0x00,
	0x00, 0x00, 0x10, 0x80, 0x00, 0x00, 0x03, 0x80
};

static uint8_t fw_image[202456];

/* Pinned Task 2 exact-map fixture: valid MAC and RFE 5. */
static const uint8_t efuse_ok[] = {
	0x8f, 0x48, 0x02, 0x11, 0x22, 0x33, 0x44, 0x55,
	0x2e, 0x81, 0xf7,
	0xef, 0x20, 0x34, 0xfe, 0x09, 0x08, 0x07, 0x06, 0x05, 0x04,
	0x0f, 0x30, 0x01, 0x22, 0x03, 0x45, 0x06, 0x07, 0x08, 0xa9,
	0x2f, 0x38, 0xa9, 0x31, 0x05, 'U', 'S', 0x66,
	0x4f, 0x3e, 0x1a, 0x2b,
	0x8f, 0x4b, 0xaa, 0xbb,
	0xff
};

static int
is_cfg_reg(uint32_t reg)
{

	if (reg == 0x0030 || reg == 0x00cf || reg == 0x00f0)
		return 0;
	return 1;
}

static int
access_ok(uint32_t reg, size_t width)
{

	if (f.remove)
		return ENXIO;
	if (reg + width > sizeof(f.mmio) || (reg & (width - 1)) != 0)
		return EINVAL;
	return 0;
}

static int
read_1(void *cookie, uint32_t reg, uint8_t *out)
{
	int error;

	(void)cookie;
	error = access_ok(reg, 1);
	if (error != 0)
		return error;
	if (f.fail_syscfg && reg == 0x00f0)
		return EIO;
	*out = f.mmio[reg];
	if (reg == 0x06)
		*out |= 0x02;
	if (reg == 0x01cc && f.busy_box)
		*out = 0x01;
	return 0;
}

static int
read_2(void *cookie, uint32_t reg, uint16_t *out)
{
	int error;

	(void)cookie;
	error = access_ok(reg, 2);
	if (error != 0)
		return error;
	if (f.fail_syscfg && reg == 0x00f0)
		return EIO;
	*out = (uint16_t)f.mmio[reg] | (uint16_t)f.mmio[reg + 1] << 8;
	return 0;
}

static int
read_4(void *cookie, uint32_t reg, uint32_t *out)
{
	int error;
	uint32_t v;

	(void)cookie;
	error = access_ok(reg, 4);
	if (error != 0)
		return error;
	if (f.fail_syscfg && reg == 0x00f0)
		return EIO;
	if (f.fail_efuse && reg == 0x0030)
		return EIO;
	v = (uint32_t)f.mmio[reg] | (uint32_t)f.mmio[reg + 1] << 8 |
	    (uint32_t)f.mmio[reg + 2] << 16 | (uint32_t)f.mmio[reg + 3] << 24;
	if (reg == 0x0030) {
		uint32_t addr = (v >> 8) & 0x3ff;

		if (addr < sizeof(f.efuse))
			v = (v & ~0xffu) | f.efuse[addr];
		v |= UINT32_C(1) << 31;
	}
	/*
	 * Reference rtw88: the driver sets DW_OK and CHKSUM_OK per section
	 * and FW_DW_RDY at the end. Only then does the enabled CPU boot the
	 * image and raise FW_INIT_RDY.
	 */
	if (reg == 0x0080 && !f.fw_never_ready && (f.mmio[0x03] & 0x04) != 0 &&
	    (v & 0x4078) == 0x4078)
		v |= 0x8000;
	if (reg == 0x1208 && f.ddma_bad_sum)
		v |= UINT32_C(1) << 27;
	/* RXBD_IDX_MPDUQ: hardware write index in 27:16, host index low. */
	if (reg == 0x03b4)
		v = (v & 0xfff) | (uint32_t)(f.rx_hw_idx & 0xfff) << 16;
	/* TXBD_IDX MGMTQ/BEQ: hardware read index in 27:16. */
	if (reg == 0x03b0)
		v = (v & 0xfff) | (uint32_t)(f.tx_hw_rp[0] & 0xfff) << 16;
	if (reg == 0x03a8)
		v = (v & 0xfff) | (uint32_t)(f.tx_hw_rp[1] & 0xfff) << 16;
	/*
	 * Internal DDMA completes only after a beacon kick consumed the
	 * host chunk. Otherwise OWN stays set and the download times out.
	 */
	if (reg == 0x1208 && f.bcn_ready) {
		v &= ~(UINT32_C(1) << 31);
		f.bcn_ready = 0;
	}
	*out = v;
	return 0;
}

static int
write_1(void *cookie, uint32_t reg, uint8_t val)
{
	int error;

	(void)cookie;
	error = access_ok(reg, 1);
	if (error != 0)
		return error;
	f.writes++;
	if (is_cfg_reg(reg))
		f.cfg_writes++;
	f.mmio[reg] = val;
	/* AUTO_LLT_V1 completes immediately. */
	if (reg == 0x0208)
		f.mmio[reg] &= (uint8_t)~0x01;
	if (reg == 0x05)
		f.mmio[reg] &= (uint8_t)~0x03;
	if (reg == 0x383 && (val & 0x10) != 0 && f.sc != NULL &&
	    f.sc->sc_ring[4].vaddr != NULL && f.sc->sc_fw_dma.vaddr != NULL) {
		uint8_t *bd = f.sc->sc_ring[4].vaddr;
		uint32_t desa = (uint32_t)f.mmio[0x308] |
		    (uint32_t)f.mmio[0x309] << 8 |
		    (uint32_t)f.mmio[0x30a] << 16 |
		    (uint32_t)f.mmio[0x30b] << 24;
		uint32_t dma = (uint32_t)bd[4] | (uint32_t)bd[5] << 8 |
		    (uint32_t)bd[6] << 16 | (uint32_t)bd[7] << 24;
		uint16_t psb = (uint16_t)bd[2] | (uint16_t)bd[3] << 8;

		/*
		 * Reference rtw88: the beacon queue has no completion
		 * write-back. The chip leaves the host OWN bit set and
		 * reports the copy through BCN_VALID in FIFOPAGE_CTRL_2.
		 */
		if (!f.bcn_dead &&
		    desa == (uint32_t)f.sc->sc_ring[4].addr &&
		    dma == (uint32_t)f.sc->sc_fw_dma.addr &&
		    (psb & 0x8000) != 0) {
			f.mmio[0x205] |= 0x80;
			f.bcn_valid_sets++;
			f.bcn_ready = 1;
		}
	}
	return 0;
}

static int
write_2(void *cookie, uint32_t reg, uint16_t val)
{
	int error;

	(void)cookie;
	error = access_ok(reg, 2);
	if (error != 0)
		return error;
	f.writes++;
	if (is_cfg_reg(reg))
		f.cfg_writes++;
	f.mmio[reg] = (uint8_t)val;
	f.mmio[reg + 1] = (uint8_t)(val >> 8);
	/* BCN_VALID is write-one-to-clear. */
	if (reg == 0x204)
		f.mmio[0x205] &= (uint8_t)~0x80;
	return 0;
}

static int
write_4(void *cookie, uint32_t reg, uint32_t val)
{
	int error;

	(void)cookie;
	error = access_ok(reg, 4);
	if (error != 0)
		return error;
	f.writes++;
	if (is_cfg_reg(reg))
		f.cfg_writes++;
	/* HISR0/1/3 are write-one-to-clear. */
	if (reg == 0x00b4 || reg == 0x00bc || reg == 0x10bc) {
		unsigned i;

		for (i = 0; i < 4; i++)
			f.mmio[reg + i] &= (uint8_t)~(val >> (8 * i));
		return 0;
	}
	f.mmio[reg] = (uint8_t)val;
	f.mmio[reg + 1] = (uint8_t)(val >> 8);
	f.mmio[reg + 2] = (uint8_t)(val >> 16);
	f.mmio[reg + 3] = (uint8_t)(val >> 24);
	return 0;
}

static void
delay_us(void *cookie, unsigned us)
{

	(void)cookie;
	(void)us;
	f.delays++;
	if (f.stop_at_delay != 0 && f.sc != NULL &&
	    f.delays == (unsigned)f.stop_at_delay)
		rtw88_stop(f.sc);
}

static int
dma_alloc(void *cookie, struct rtw88_dma *ring, size_t size)
{

	(void)cookie;
	if (f.fail_alloc_at != 0 && ++f.alloc_calls == f.fail_alloc_at)
		return ENOMEM;
	ring->vaddr = calloc(1, size);
	if (ring->vaddr == NULL)
		return ENOMEM;
	ring->addr = 0x2000 + f.dma_allocs * 0x2000;
	ring->size = size;
	ring->cookie = ring;
	f.dma_allocs++;
	return 0;
}

static void
dma_free(void *cookie, struct rtw88_dma *ring)
{

	(void)cookie;
	free(ring->vaddr);
	memset(ring, 0, sizeof(*ring));
	if (f.dma_allocs > 0)
		f.dma_allocs--;
}

static void
dma_sync(void *cookie, struct rtw88_dma *ring, int ops)
{

	(void)cookie;
	(void)ring;
	(void)ops;
}

static int
intr_establish(void *cookie)
{

	(void)cookie;
	return 0;
}

static void
intr_disestablish(void *cookie)
{

	(void)cookie;
}

static uint32_t
mmio32(uint32_t reg)
{

	return (uint32_t)f.mmio[reg] | (uint32_t)f.mmio[reg + 1] << 8 |
	    (uint32_t)f.mmio[reg + 2] << 16 | (uint32_t)f.mmio[reg + 3] << 24;
}

static void
intr_barrier(void *cookie)
{

	(void)cookie;
	f.barriers++;
	f.imr_at_barrier = mmio32(0x00b0) | mmio32(0x00b8) | mmio32(0x10b8);
}

static int
firmware_load(void *cookie, const char *drv, const char *name,
    const uint8_t **data, size_t *len)
{

	(void)cookie;
	(void)drv;
	(void)name;
	if (f.fw_error != 0)
		return f.fw_error;
	if (f.fw == NULL)
		return ENOENT;
	*data = f.fw;
	*len = f.fw_len;
	return 0;
}

static void
firmware_release(void *cookie)
{

	(void)cookie;
}

static int
bus_master(void *cookie, int enable)
{

	(void)cookie;
	f.master = enable ? 1 : 0;
	return 0;
}

static const struct rtw88_bus_ops ops = {
	.read_1 = read_1,
	.read_2 = read_2,
	.read_4 = read_4,
	.write_1 = write_1,
	.write_2 = write_2,
	.write_4 = write_4,
	.delay_us = delay_us,
	.dma_alloc = dma_alloc,
	.dma_free = dma_free,
	.dma_sync = dma_sync,
	.intr_establish = intr_establish,
	.intr_disestablish = intr_disestablish,
	.intr_barrier = intr_barrier,
	.firmware_load = firmware_load,
	.firmware_release = firmware_release,
	.bus_master = bus_master
};

static void
reset_fw(void)
{

	memset(fw_image, 0, sizeof(fw_image));
	memcpy(fw_image, fw_header, sizeof(fw_header));
}

static void
reset_bus(struct rtw88_softc *sc)
{
	unsigned i;

	memset(&f, 0, sizeof(f));
	memset(sc, 0, sizeof(*sc));
	sc->sc_ops = &ops;
	sc->sc_bus = sc;
	sc->sc_vendor = 0x10ec;
	sc->sc_product = 0xc822;
	f.sc = sc;
	f.mmio[0x06] = 0x02;
	f.mmio[0xf0] = 0x00;
	f.mmio[0xf1] = 0x00;
	f.mmio[0xf2] = 0x00;
	f.mmio[0xf3] = 0x08;
	memcpy(f.efuse, efuse_ok, sizeof(efuse_ok));
	for (i = sizeof(efuse_ok); i < sizeof(f.efuse); i++)
		f.efuse[i] = 0xff;
	reset_fw();
	f.fw = fw_image;
	f.fw_len = sizeof(fw_image);
}

static int
attach_ok(struct rtw88_softc *sc)
{

	CHECK(rtw88_core_attach(sc) == 0);
	CHECK(sc->sc_resources_ready);
	CHECK(!sc->sc_rf_enabled && !sc->sc_dma_master);
	return sc->sc_resources_ready;
}

static void
test_missing_firmware(void)
{
	struct rtw88_softc sc;

	reset_bus(&sc);
	f.fw = NULL;
	if (!attach_ok(&sc))
		return;
	CHECK(rtw88_start(&sc) == ENOENT);
	CHECK(!sc.sc_rf_enabled && !sc.sc_dma_master && f.master == 0);
	CHECK(f.cfg_writes == 0);
	rtw88_core_detach(&sc);
	CHECK(f.dma_allocs == 0);
}

static void
test_corrupt_firmware(void)
{
	struct rtw88_softc sc;

	reset_bus(&sc);
	fw_image[0] = 0x00;
	if (!attach_ok(&sc))
		return;
	CHECK(rtw88_start(&sc) != 0);
	CHECK(!sc.sc_rf_enabled && !sc.sc_dma_master && f.master == 0);
	CHECK(f.cfg_writes == 0);
	rtw88_core_detach(&sc);
}

static void
test_failed_identity_read(void)
{
	struct rtw88_softc sc;

	reset_bus(&sc);
	f.fail_syscfg = 1;
	if (!attach_ok(&sc))
		return;
	CHECK(rtw88_start(&sc) == EIO);
	CHECK(!sc.sc_rf_enabled && f.cfg_writes == 0);
	CHECK(f.master == 0);
	rtw88_core_detach(&sc);
}

static void
test_failed_efuse_read(void)
{
	struct rtw88_softc sc;

	reset_bus(&sc);
	f.fail_efuse = 1;
	if (!attach_ok(&sc))
		return;
	CHECK(rtw88_start(&sc) == EIO);
	CHECK(!sc.sc_rf_enabled && f.master == 0);
	rtw88_core_detach(&sc);
}

static void
test_malformed_efuse(void)
{
	struct rtw88_softc sc;

	reset_bus(&sc);
	memset(f.efuse, 0xff, sizeof(f.efuse));
	if (!attach_ok(&sc))
		return;
	CHECK(rtw88_start(&sc) == EINVAL);
	CHECK(!sc.sc_rf_enabled && f.master == 0);
	rtw88_core_detach(&sc);
}

static void
test_unsupported_identity(void)
{
	struct rtw88_softc sc;

	reset_bus(&sc);
	sc.sc_product = 0xb822;
	if (!attach_ok(&sc))
		return;
	CHECK(rtw88_start(&sc) == ENOTSUP);
	CHECK(!sc.sc_rf_enabled && f.master == 0);
	rtw88_core_detach(&sc);
}

static void
test_command_length(void)
{
	struct rtw88_softc sc;
	uint8_t buf[8] = { 0 };

	reset_bus(&sc);
	if (!attach_ok(&sc))
		return;
	CHECK(rtw88_start(&sc) == 0);
	CHECK(sc.sc_rf_enabled);
	if (sc.sc_rf_enabled) {
		uint8_t *bd = sc.sc_ring[4].vaddr;
		uint8_t *pkt = sc.sc_fw_dma.vaddr;
		uint32_t dma, desa;
		uint16_t pkt_size, seg1;

		CHECK(bd != NULL && pkt != NULL);
		dma = (uint32_t)bd[4] | (uint32_t)bd[5] << 8 |
		    (uint32_t)bd[6] << 16 | (uint32_t)bd[7] << 24;
		desa = (uint32_t)f.mmio[0x308] | (uint32_t)f.mmio[0x309] << 8 |
		    (uint32_t)f.mmio[0x30a] << 16 |
		    (uint32_t)f.mmio[0x30b] << 24;
		pkt_size = (uint16_t)pkt[0] | (uint16_t)pkt[1] << 8;
		seg1 = (uint16_t)bd[8] | (uint16_t)bd[9] << 8;
		CHECK(dma == (uint32_t)sc.sc_fw_dma.addr);
		CHECK(desa == (uint32_t)sc.sc_ring[4].addr);
		CHECK(pkt_size == 2008 && seg1 == 2008);
		/* MAC init after download: MAC_TRX_ENABLE, reserved boundary. */
		CHECK(f.mmio[0x100] == 0xff);
		CHECK((f.mmio[0x204] | f.mmio[0x205] << 8) == 0x0792);
		/* No beacon write-back: OWN stays, the chip sets BCN_VALID. */
		CHECK((bd[3] & 0x80) != 0);
		CHECK(f.bcn_valid_sets > 0);
	}
	CHECK(rtw88_fw_command(&sc, 1, buf, 8) == EMSGSIZE);
	CHECK(rtw88_fw_command(&sc, 1, buf, 7) == 0);
	rtw88_stop(&sc);
	CHECK(!sc.sc_rf_enabled && f.master == 0);
	rtw88_core_detach(&sc);
}

static void
test_command_timeout(void)
{
	struct rtw88_softc sc;
	uint8_t buf[1] = { 0 };

	reset_bus(&sc);
	if (!attach_ok(&sc))
		return;
	CHECK(rtw88_start(&sc) == 0);
	f.busy_box = 1;
	CHECK(rtw88_fw_command(&sc, 1, buf, 1) == ETIMEDOUT);
	rtw88_stop(&sc);
	rtw88_core_detach(&sc);
}

/* DDMA reports a bad section checksum: no FW_DW_RDY, bounded failure. */
static void
test_firmware_checksum_bad(void)
{
	struct rtw88_softc sc;

	reset_bus(&sc);
	f.ddma_bad_sum = 1;
	if (!attach_ok(&sc))
		return;
	CHECK(rtw88_start(&sc) == EIO);
	CHECK(!sc.sc_rf_enabled && f.master == 0);
	CHECK((f.mmio[0x81] & 0x40) == 0);
	rtw88_core_detach(&sc);
}

/* The chip never reports BCN_VALID: bounded failure, mastering cleared. */
static void
test_beacon_never_valid(void)
{
	struct rtw88_softc sc;

	reset_bus(&sc);
	f.bcn_dead = 1;
	if (!attach_ok(&sc))
		return;
	CHECK(rtw88_start(&sc) == ETIMEDOUT);
	CHECK(!sc.sc_rf_enabled && f.master == 0);
	CHECK((f.mmio[0x205] & 0x80) == 0);
	rtw88_core_detach(&sc);
}

static void
test_stop_during_bootstrap(void)
{
	struct rtw88_softc sc;

	reset_bus(&sc);
	f.stop_at_delay = 1;
	if (!attach_ok(&sc))
		return;
	CHECK(rtw88_start(&sc) != 0);
	CHECK(!sc.sc_rf_enabled && f.master == 0);
	CHECK(!sc.sc_dma_master);
	rtw88_core_detach(&sc);
}

static void
test_stale_generation(void)
{
	struct rtw88_softc sc;
	uint8_t buf[1] = { 0 };
	uint32_t gen;

	reset_bus(&sc);
	if (!attach_ok(&sc))
		return;
	CHECK(rtw88_start(&sc) == 0);
	gen = sc.sc_generation;
	f.stop_at_delay = (int)(f.delays + 1);
	f.busy_box = 1;
	CHECK(rtw88_fw_command(&sc, 1, buf, 1) != 0);
	CHECK(sc.sc_generation != gen || !sc.sc_started);
	rtw88_core_detach(&sc);
}

static void
test_success_consumes_tables(void)
{
	struct rtw88_softc sc;
	static const uint32_t cond[] = {
		0x80000000, 0x00000000,
		0x40000000, 0x00000000,
		0x00001d90, 0x300001ff,
		0xb0000000, 0x00000000
	};
	static const struct rtw8822c_table tiny = {
		.data = cond,
		.count = 8,
		.kind = RTW8822C_TBL_AGC,
		.rf_path = 0
	};

	reset_bus(&sc);
	if (!attach_ok(&sc))
		return;
	CHECK(rtw88_start(&sc) == 0);
	CHECK(sc.sc_rf_enabled && sc.sc_dma_master && f.master == 1);
	CHECK(sc.sc_pg_rows > 0 && sc.sc_pwr_rows > 0);
	CHECK(f.cfg_writes > 0);
	CHECK(rtw8822c_apply_table(&sc, &tiny) == 0);
	rtw88_stop(&sc);
	CHECK(!sc.sc_rf_enabled && f.master == 0);
	CHECK(rtw88_fw_command(&sc, 1, "", 0) == ENXIO);
	rtw88_core_detach(&sc);
	CHECK(f.dma_allocs == 0);
}

/* Started device with RX armed and interrupts unmasked. */
static int
start_ok(struct rtw88_softc *sc)
{

	reset_bus(sc);
	if (!attach_ok(sc))
		return 0;
	CHECK(rtw88_start(sc) == 0);
	CHECK(sc->sc_running);
	return sc->sc_running;
}

/*
 * Put one received packet in RX buffer idx: 24-byte RX descriptor,
 * optional C2H flag, then the payload. tag goes to the buffer
 * descriptor's total_pkt_size, as the chip reports it with RX_TAG_EN.
 */
static void
rx_put(struct rtw88_softc *sc, unsigned idx, unsigned len, int c2h,
    const uint8_t *payload, uint16_t tag)
{
	uint8_t *buf = sc->sc_rxbuf[idx].vaddr;
	uint8_t *bd = (uint8_t *)sc->sc_ring[8].vaddr + 8 * idx;
	uint32_t w0 = len & 0x3fff, w2 = c2h ? UINT32_C(1) << 28 : 0;
	unsigned i;

	memset(buf, 0, 24);
	for (i = 0; i < 4; i++) {
		buf[i] = (uint8_t)(w0 >> (8 * i));
		buf[8 + i] = (uint8_t)(w2 >> (8 * i));
	}
	if (payload != NULL && len <= RTW88_RX_BUF_SIZE - 24)
		memcpy(buf + 24, payload, len);
	bd[2] = (uint8_t)tag;
	bd[3] = (uint8_t)(tag >> 8);
	/* The chip consumed the descriptor; a stale size must not survive. */
	bd[0] = bd[1] = 0;
}

static void
set_hisr0(uint32_t v)
{

	f.mmio[0xb4] = (uint8_t)v;
	f.mmio[0xb5] = (uint8_t)(v >> 8);
	f.mmio[0xb6] = (uint8_t)(v >> 16);
	f.mmio[0xb7] = (uint8_t)(v >> 24);
}

static unsigned
host_rx_idx(void)
{

	return (unsigned)(f.mmio[0x3b4] | f.mmio[0x3b5] << 8) & 0xfff;
}

static int
rx_armed(struct rtw88_softc *sc, unsigned idx)
{
	const uint8_t *bd = (const uint8_t *)sc->sc_ring[8].vaddr + 8 * idx;
	uint32_t dma = (uint32_t)bd[4] | (uint32_t)bd[5] << 8 |
	    (uint32_t)bd[6] << 16 | (uint32_t)bd[7] << 24;

	return (bd[0] | bd[1] << 8) == RTW88_RX_BUF_SIZE &&
	    dma == (uint32_t)sc->sc_rxbuf[idx].addr;
}

static void
test_hci_start_masks(void)
{
	struct rtw88_softc sc;

	if (!start_ok(&sc))
		return;
	CHECK(mmio32(0x00b0) == 0x44fd);	/* ROK, TX DOKs, C2HCMD... */
	CHECK(mmio32(0x00b8) == 0x200);		/* TXFOVW */
	CHECK(mmio32(0x10b8) == 0x10000);	/* H2CDOK */
	CHECK((f.mmio[0x382] | f.mmio[0x383] << 8) == RTW88_RX_BUFS);
	CHECK(mmio32(0x338) == (uint32_t)sc.sc_ring[8].addr);
	CHECK(rx_armed(&sc, 0) && rx_armed(&sc, RTW88_RX_BUFS - 1));
	rtw88_stop(&sc);
	rtw88_core_detach(&sc);
}

static void
test_rx_c2h_and_frame(void)
{
	static const uint8_t c2h[4] = { 0x09, 0x05, 0xaa, 0xbb };
	uint8_t frame[24] = { 0x80, 0x00 };	/* Beacon header. */
	struct rtw88_softc sc;

	if (!start_ok(&sc))
		return;
	rx_put(&sc, 0, sizeof(c2h), 1, c2h, 0);
	rx_put(&sc, 1, sizeof(frame), 0, frame, 1);
	f.rx_hw_idx = 2;
	set_hisr0(0x01);
	CHECK(rtw88_intr(&sc) == 1);
	CHECK(sc.sc_stats.rx_c2h == 1 && sc.sc_stats.last_c2h_id == 0x09 &&
	    sc.sc_stats.last_c2h_seq == 0x05);
	CHECK(sc.sc_stats.rx_frames == 1);
	CHECK(sc.sc_stats.rx_errors == 0 && sc.sc_stats.rx_tag_errors == 0);
	CHECK(host_rx_idx() == 2);
	CHECK(mmio32(0x00b4) == 0);
	CHECK(rx_armed(&sc, 0) && rx_armed(&sc, 1));
	rtw88_stop(&sc);
	rtw88_core_detach(&sc);
}

static void
test_rx_malformed_and_short(void)
{
	uint8_t frame[4] = { 0 };
	struct rtw88_softc sc;

	if (!start_ok(&sc))
		return;
	/* Length beyond the buffer: the decoder must refuse it. */
	rx_put(&sc, 0, 0x3fff, 0, NULL, 0);
	/* Shorter than the smallest 802.11 frame (ACK, 10 bytes). */
	rx_put(&sc, 1, sizeof(frame), 0, frame, 1);
	f.rx_hw_idx = 2;
	set_hisr0(0x01);
	CHECK(rtw88_intr(&sc) == 1);
	CHECK(sc.sc_stats.rx_errors == 1 && sc.sc_stats.rx_short == 1);
	CHECK(sc.sc_stats.rx_frames == 0 && sc.sc_stats.rx_c2h == 0);
	CHECK(host_rx_idx() == 2);
	CHECK(rx_armed(&sc, 0) && rx_armed(&sc, 1));
	rtw88_stop(&sc);
	rtw88_core_detach(&sc);
}

static void
test_rx_limit_and_wrap(void)
{
	uint8_t frame[24] = { 0x80 };
	struct rtw88_softc sc;
	unsigned i;

	if (!start_ok(&sc))
		return;
	for (i = 0; i < RTW88_RX_BUFS; i++)
		rx_put(&sc, i, sizeof(frame), 0, frame,
		    (uint16_t)(i % 8192));
	f.rx_hw_idx = 100;
	set_hisr0(0x01);
	CHECK(rtw88_intr(&sc) == 1);
	CHECK(sc.sc_stats.rx_frames == RTW88_RX_LIMIT);
	CHECK(host_rx_idx() == RTW88_RX_LIMIT);
	set_hisr0(0x01);
	CHECK(rtw88_intr(&sc) == 1);
	CHECK(sc.sc_stats.rx_frames == 100 && host_rx_idx() == 100);
	/* Wrap: 28 to the end, then 4 from the start. */
	for (i = 0; i < RTW88_RX_BUFS; i++)
		rx_put(&sc, i, sizeof(frame), 0, frame,
		    (uint16_t)((i < 100 ? i + RTW88_RX_BUFS : i) % 8192));
	f.rx_hw_idx = 4;
	set_hisr0(0x01);
	CHECK(rtw88_intr(&sc) == 1);
	CHECK(sc.sc_stats.rx_frames == 100 + RTW88_RX_BUFS - 100 + 4);
	CHECK(host_rx_idx() == 4);
	CHECK(sc.sc_stats.rx_tag_errors == 0);
	rtw88_stop(&sc);
	rtw88_core_detach(&sc);
}

static void
test_rx_bad_hw_index(void)
{
	struct rtw88_softc sc;

	if (!start_ok(&sc))
		return;
	f.rx_hw_idx = RTW88_RX_BUFS;	/* Outside the published ring. */
	set_hisr0(0x01);
	CHECK(rtw88_intr(&sc) == 1);
	CHECK(sc.sc_stats.rx_dma_errors == 1 && sc.sc_stats.rx_frames == 0);
	CHECK(host_rx_idx() == 0);
	rtw88_stop(&sc);
	rtw88_core_detach(&sc);
}

static void
test_rx_duplicate_interrupt(void)
{
	struct rtw88_softc sc;

	if (!start_ok(&sc))
		return;
	f.rx_hw_idx = 0;	/* Nothing new since the last read. */
	set_hisr0(0x01);
	CHECK(rtw88_intr(&sc) == 1);
	CHECK(sc.sc_stats.rx_frames == 0 && sc.sc_stats.rx_errors == 0 &&
	    sc.sc_stats.rx_dma_errors == 0);
	rtw88_stop(&sc);
	rtw88_core_detach(&sc);
}

static void
test_rx_tag_mismatch(void)
{
	uint8_t frame[24] = { 0x80 };
	struct rtw88_softc sc;

	if (!start_ok(&sc))
		return;
	rx_put(&sc, 0, sizeof(frame), 0, frame, 7);	/* Expected 0. */
	f.rx_hw_idx = 1;
	set_hisr0(0x01);
	CHECK(rtw88_intr(&sc) == 1);
	CHECK(sc.sc_stats.rx_tag_errors == 1 && sc.sc_stats.rx_frames == 1);
	rtw88_stop(&sc);
	rtw88_core_detach(&sc);
}

/* A shared INTx line: none of our unmasked causes is pending. */
static void
test_intr_not_ours(void)
{
	struct rtw88_softc sc;
	unsigned writes;

	if (!start_ok(&sc))
		return;
	set_hisr0(UINT32_C(0x80000000));	/* TIMER2: masked off. */
	writes = f.writes;
	CHECK(rtw88_intr(&sc) == 0);
	CHECK(f.writes == writes);
	rtw88_stop(&sc);
	rtw88_core_detach(&sc);
}

/* stop masks and drains before buffers go; a late handler is inert. */
static void
test_intr_after_stop(void)
{
	struct rtw88_softc sc;
	unsigned writes;

	if (!start_ok(&sc))
		return;
	rtw88_stop(&sc);
	CHECK(!sc.sc_running);
	CHECK(f.barriers == 1 && f.imr_at_barrier == 0);
	set_hisr0(0x01);
	f.rx_hw_idx = 3;
	writes = f.writes;
	CHECK(rtw88_intr(&sc) == 0);
	CHECK(f.writes == writes && sc.sc_stats.rx_frames == 0);
	rtw88_core_detach(&sc);
}

static void
test_rx_buffer_alloc_failure(void)
{
	struct rtw88_softc sc;

	reset_bus(&sc);
	f.fail_alloc_at = RTW88_NRING + 5;	/* Sixth RX buffer. */
	CHECK(rtw88_core_attach(&sc) == ENOMEM);
	CHECK(!sc.sc_resources_ready && f.dma_allocs == 0);
}

static void
test_diag_regs(void)
{
	struct rtw88_softc sc;
	uint32_t r[RTW88_DIAG_REGS];

	if (!start_ok(&sc))
		return;
	set_hisr0(0x01);
	f.rx_hw_idx = 5;
	CHECK(rtw88_diag(&sc, r) == 0);
	CHECK(r[0] == 0x01 && r[1] == 0x44fd);		/* HISR0, HIMR0 */
	CHECK(((r[4] >> 16) & 0xfff) == 5);		/* RXBD_IDX */
	CHECK(mmio32(0x00b4) == 0x01);			/* Reading is not acking. */
	rtw88_stop(&sc);
	rtw88_core_detach(&sc);
}

static const uint8_t bcast_frame[24] = {
	0xd0, 0x00, 0x00, 0x00,				/* Action, no duration. */
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff,		/* DA broadcast. */
	0x02, 0x00, 0x00, 0x00, 0x00, 0x01,		/* SA local. */
	0x02, 0x00, 0x00, 0x00, 0x00, 0x01,		/* BSSID. */
	0x00, 0x00
};

static unsigned
reg16(uint32_t reg)
{

	return (unsigned)(f.mmio[reg] | f.mmio[reg + 1] << 8);
}

static void
test_tx_descriptors_and_full_ring(void)
{
	struct rtw88_softc sc;
	const uint8_t *bd, *pkt;
	unsigned i;

	if (!start_ok(&sc))
		return;
	CHECK(reg16(0x0380) == RTW88_TX_SLOTS);		/* MGMTQ entries */
	CHECK(reg16(0x0388) == RTW88_TX_SLOTS);		/* BEQ entries */
	CHECK(rtw88_tx_raw(&sc, RTW88_TXQ_MGMT, bcast_frame,
	    sizeof(bcast_frame)) == 0);
	bd = sc.sc_ring[5].vaddr;
	pkt = sc.sc_txq[RTW88_TXQ_MGMT].buf[0].vaddr;
	CHECK((bd[0] | bd[1] << 8) == 48);			/* segment 0 */
	CHECK((bd[2] | bd[3] << 8) == 1);			/* 72 bytes: 1 PSB */
	CHECK((bd[8] | bd[9] << 8) == sizeof(bcast_frame));	/* segment 1 */
	CHECK((uint32_t)(bd[4] | bd[5] << 8 | bd[6] << 16 |
	    (uint32_t)bd[7] << 24) == (uint32_t)sc.sc_txq[0].buf[0].addr);
	CHECK((pkt[0] | pkt[1] << 8) == sizeof(bcast_frame));	/* TX desc size */
	CHECK(((pkt[5] >> 0) & 0x1f) == 18);		/* QSEL management */
	CHECK(memcmp(pkt + 48, bcast_frame, sizeof(bcast_frame)) == 0);
	CHECK((reg16(0x03b0) & 0xfff) == 1);		/* kicked */
	CHECK(sc.sc_stats.tx_queued == 1 && sc.sc_stats.tx_done == 0);
	for (i = 1; i < RTW88_TX_SLOTS - 1; i++)
		CHECK(rtw88_tx_raw(&sc, RTW88_TXQ_MGMT, bcast_frame,
		    sizeof(bcast_frame)) == 0);
	CHECK(rtw88_tx_raw(&sc, RTW88_TXQ_MGMT, bcast_frame,
	    sizeof(bcast_frame)) == ENOBUFS);
	CHECK(sc.sc_stats.tx_full == 1);
	CHECK(sc.sc_stats.tx_queued == RTW88_TX_SLOTS - 1);
	rtw88_stop(&sc);
	CHECK(sc.sc_stats.tx_dropped == RTW88_TX_SLOTS - 1);
	rtw88_core_detach(&sc);
}

static void
test_tx_completion(void)
{
	struct rtw88_softc sc;
	unsigned i;

	if (!start_ok(&sc))
		return;
	for (i = 0; i < 3; i++)
		CHECK(rtw88_tx_raw(&sc, RTW88_TXQ_BE, bcast_frame,
		    sizeof(bcast_frame)) == 0);
	CHECK((reg16(0x03a8) & 0xfff) == 3);
	f.tx_hw_rp[1] = 2;
	set_hisr0(0x10);				/* BEDOK */
	CHECK(rtw88_intr(&sc) == 1);
	CHECK(sc.sc_stats.tx_done == 2 && sc.sc_txq[RTW88_TXQ_BE].queued == 1);
	/* The same completion again is not counted twice. */
	set_hisr0(0x10);
	CHECK(rtw88_intr(&sc) == 1);
	CHECK(sc.sc_stats.tx_done == 2 && sc.sc_stats.tx_dma_errors == 0);
	/* A read index past the outstanding frames is a DMA fault. */
	f.tx_hw_rp[1] = 10;
	set_hisr0(0x10);
	CHECK(rtw88_intr(&sc) == 1);
	CHECK(sc.sc_stats.tx_dma_errors == 1 && sc.sc_stats.tx_done == 2);
	f.tx_hw_rp[1] = 3;
	set_hisr0(0x10);
	CHECK(rtw88_intr(&sc) == 1);
	CHECK(sc.sc_stats.tx_done == 3 && sc.sc_txq[RTW88_TXQ_BE].queued == 0);
	rtw88_stop(&sc);
	CHECK(sc.sc_stats.tx_dropped == 0);
	rtw88_core_detach(&sc);
}

static void
test_tx_refusals(void)
{
	static uint8_t big[RTW88_TX_BUF_SIZE];
	struct rtw88_softc sc;

	reset_bus(&sc);
	if (!attach_ok(&sc))
		return;
	CHECK(rtw88_tx_raw(&sc, RTW88_TXQ_MGMT, bcast_frame,
	    sizeof(bcast_frame)) == ENXIO);			/* not running */
	CHECK(rtw88_start(&sc) == 0);
	CHECK(rtw88_tx_raw(&sc, RTW88_TXQ_MGMT, big, sizeof(big)) == EMSGSIZE);
	CHECK(rtw88_tx_raw(&sc, RTW88_TXQ_MGMT, big, 0) == EMSGSIZE);
	CHECK(rtw88_tx_raw(&sc, 7, bcast_frame, sizeof(bcast_frame)) == EINVAL);
	CHECK(sc.sc_stats.tx_queued == 0);
	rtw88_stop(&sc);
	rtw88_core_detach(&sc);
}

static void
test_loopback_mode(void)
{
	struct rtw88_softc sc;

	if (!start_ok(&sc))
		return;
	CHECK(rtw88_loopback(&sc, 1) == 0 && f.mmio[0x103] == 0x0b);
	CHECK(rtw88_loopback(&sc, 0) == 0 && f.mmio[0x103] == 0x00);
	rtw88_stop(&sc);
	CHECK(rtw88_loopback(&sc, 1) == ENXIO);
	rtw88_core_detach(&sc);
}

int
main(void)
{

	test_missing_firmware();
	test_corrupt_firmware();
	test_failed_identity_read();
	test_failed_efuse_read();
	test_malformed_efuse();
	test_unsupported_identity();
	test_command_length();
	test_command_timeout();
	test_beacon_never_valid();
	test_firmware_checksum_bad();
	test_hci_start_masks();
	test_rx_c2h_and_frame();
	test_rx_malformed_and_short();
	test_rx_limit_and_wrap();
	test_rx_bad_hw_index();
	test_rx_duplicate_interrupt();
	test_rx_tag_mismatch();
	test_intr_not_ours();
	test_intr_after_stop();
	test_rx_buffer_alloc_failure();
	test_diag_regs();
	test_tx_descriptors_and_full_ring();
	test_tx_completion();
	test_tx_refusals();
	test_loopback_mode();
	test_stop_during_bootstrap();
	test_stale_generation();
	test_success_consumes_tables();
	printf("rtw88 start contract: %u assertions, %u failures\n",
	    assertions, failures);
	return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
