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
#include "rtw8822c-dack-model.h"

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
	uint8_t mmio[0x10000];
	uint32_t lte[256];
	int coex_busy;
	int fail_power_stage, off_stuck, off_error, expect_stop;
	int enable_error, disable_error;
	unsigned off_attempts, master_enables, master_disables, firmware_loads;
	int power_pre_seen, power_table_seen, power_post_seen, bad_power_order;
	int coex_before_h2c;
	int fail_dack, quality_dack, stop_dack_delay, dack_seen, dack_failed;
	unsigned dack_samples, dack_polls, dack_after_error, hci_before_dack;
	int h2c_dead;
	int stop_coex_final;
	unsigned bcn_last_pkt_size, bcn_last_seg1;
	unsigned h2c_rp, h2c_sent;
	uint8_t efuse[512];
	const uint8_t *fw;
	size_t fw_len;
	int fw_error;
	int master;
	int missing;
	int remove;
	int fail_syscfg;
	int fail_efuse;
	int fail_rcr_final;
	unsigned rcr_final_attempts;
	int busy_box;
	int fw_never_ready;
	int stop_at_delay;
	int bcn_ready;
	int bcn_dead;
	int ddma_bad_sum;
	unsigned rx_hw_idx;
	unsigned rx_idx_reads;
	int rx_idx_error, rx_idx_change_generation, rx_idx_stop;
	unsigned bcn_valid_sets;
	unsigned tx_hw_rp[5];	/* MGMT, BE, BK, VI, VO read indices. */
	unsigned barriers;
	uint32_t imr_at_barrier;
	unsigned fail_alloc_at;	/* 1-based allocation that fails, or 0. */
	unsigned alloc_calls;
	unsigned delays;
	unsigned reads, writes, syncs;
	unsigned diag_reads;
	uint32_t fail_diag_reg;
	uint32_t fail_edca_reg;
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

/* Valid synthetic calibration, encoded as real physical EFUSE records. */
static void
efuse_calibrated(void)
{
	struct rtw8822c_efuse map;
	unsigned p, i, block, mask, word, cursor = 0;

	CHECK(rtw8822c_efuse_decode(efuse_ok, sizeof(efuse_ok), &map) == 0);
	for (p = 0; p < 4; p++) {
		for (i = 0; i < 42; i++)
			map.logical[0x10 + p * 42 + i] =
			    (i < 11 || (i >= 18 && i < 32)) ? 96 : 0;
	}
	memset(f.efuse, 0xff, sizeof(f.efuse));
	for (block = 0; block < sizeof(map.logical) / 8; block++) {
		mask = 15;
		for (word = 0; word < 4; word++) {
			i = block * 8 + word * 2;
			if (map.logical[i] != 0xff || map.logical[i + 1] != 0xff)
				mask &= ~(1U << word);
		}
		if (mask == 15)
			continue;
		if (block < 16)
			f.efuse[cursor++] = (uint8_t)(block << 4 | mask);
		else {
			f.efuse[cursor++] = (uint8_t)((block & 7) << 5 | 15);
			f.efuse[cursor++] = (uint8_t)((block & 0x78) << 1 | mask);
		}
		for (word = 0; word < 4; word++) {
			if ((mask & (1U << word)) != 0)
				continue;
			i = block * 8 + word * 2;
			CHECK(cursor + 2 < RTW8822C_EFUSE_PHYSICAL_SIZE -
			    RTW8822C_EFUSE_PROTECTED_SIZE);
			f.efuse[cursor++] = map.logical[i];
			f.efuse[cursor++] = map.logical[i + 1];
		}
	}
}

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

	if (f.dack_failed && !f.sc->sc_stop_requested)
		f.dack_after_error++;
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
	f.reads++;
	error = access_ok(reg, 1);
	if (error != 0)
		return error;
	if (f.expect_stop)
		CHECK(f.sc->sc_stop_requested == 1);
	if (reg == 0x93 && f.sc->sc_stop_requested) {
		f.off_attempts++;
		if (f.off_error)
			return EIO;
	}
	if (!f.sc->sc_stop_requested && f.fail_power_stage == 2 && reg == 0x75)
		return EIO;
	if (f.fail_syscfg && reg == 0x00f0)
		return EIO;
	*out = f.mmio[reg];
	if (f.off_stuck && f.sc->sc_stop_requested && reg == 0x05)
		*out |= 0x02;
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
	f.reads++;
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
	f.reads++;
	if (reg == 0x3c60 || reg == 0x4c60) {
		f.diag_reads++;
		if (reg == f.fail_diag_reg)
			return EIO;
	}
	error = access_ok(reg, 4);
	if (error != 0)
		return error;
	if (f.fail_syscfg && reg == 0x00f0)
		return EIO;
	if (f.fail_efuse && reg == 0x0030)
		return EIO;
	v = (uint32_t)f.mmio[reg] | (uint32_t)f.mmio[reg + 1] << 8 |
	    (uint32_t)f.mmio[reg + 2] << 16 | (uint32_t)f.mmio[reg + 3] << 24;
	if (f.dack_seen && !f.sc->sc_stop_requested) {
		if (reg == 0x2dbc) {
			if (f.dack_samples == 0) {
				/*
				 * Normal PHY load must not leave AFE in DPK mode.
				 * The pinned phy_load_tables graph has no DPK setup.
				 * Check the actual MMIO from native table decoding.
				 */
				CHECK((f.mmio[0x18a4] & 0x80) == 0);
				CHECK((f.mmio[0x41a4] & 0x80) == 0);
				CHECK((f.mmio[0x1867] & 0x80) == 0);
				CHECK((f.mmio[0x4167] & 0x80) == 0);
			}
			CHECK(f.sc->sc_powered && f.sc->sc_bootstrapping &&
			    !f.sc->sc_running && !f.sc->sc_started);
			CHECK(f.mmio[0xb0] == 0 && f.mmio[0xb8] == 0 &&
			    f.mmio[0x10b8] == 0);
			f.dack_samples++;
			if (f.fail_dack == 1) { f.dack_failed = 1; return EIO; }
			if (f.fail_dack == 2 && f.dack_samples == 10000)
				f.dack_failed = 1;
		}
		if (reg == 0x2dbc || reg == 0x2808 || reg == 0x2834 ||
		    reg == 0x4508 || reg == 0x4534 || reg == 0x2824 ||
		    reg == 0x2850 || reg == 0x4524 || reg == 0x4550)
			v = model_read(reg, 4);
		if (reg == 0x2808 && f.fail_dack == 3 && ++f.dack_polls == 1000)
			f.dack_failed = 1;
	}
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
	if (reg == 0x1700 && (!f.sc->sc_running || f.h2c_sent < 2))
		f.coex_before_h2c = 1;
	if (reg == 0x1700 && !f.coex_busy)
		v |= UINT32_C(0x20000000);
	/* RXBD_IDX_MPDUQ: hardware write index in 27:16, host index low. */
	if (reg == 0x03b4) {
		f.rx_idx_reads++;
		if (f.rx_idx_error)
			return EIO;
		if (f.rx_idx_change_generation)
			f.sc->sc_generation++;
		if (f.rx_idx_stop)
			f.sc->sc_stop_requested = 1;
		v = (v & 0xfff) | (uint32_t)(f.rx_hw_idx & 0xfff) << 16;
	}
	if (reg == 0x132c)
		v = (v & 0xfff) | (uint32_t)f.h2c_rp << 16;
	/* TXBD_IDX: independent hardware read indices in 27:16. */
	if (reg == 0x03b0)
		v = (v & 0xfff) | (uint32_t)(f.tx_hw_rp[0] & 0xfff) << 16;
	if (reg == 0x03a8)
		v = (v & 0xfff) | (uint32_t)(f.tx_hw_rp[1] & 0xfff) << 16;
	if (reg == 0x03ac)
		v = (v & 0xfff) | (uint32_t)(f.tx_hw_rp[2] & 0xfff) << 16;
	if (reg == 0x03a4)
		v = (v & 0xfff) | (uint32_t)(f.tx_hw_rp[3] & 0xfff) << 16;
	if (reg == 0x03a0)
		v = (v & 0xfff) | (uint32_t)(f.tx_hw_rp[4] & 0xfff) << 16;
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
	if (!f.sc->sc_stop_requested &&
	    ((f.fail_power_stage == 1 && reg == 0x1c) ||
	    (f.fail_power_stage == 3 && reg == 0x1103)))
		return EIO;
	if (!f.sc->sc_stop_requested && reg == 0x1c)
		f.power_pre_seen = 1;
	if (!f.sc->sc_stop_requested && reg == 0x2e) {
		f.bad_power_order |= !f.power_pre_seen;
		f.power_table_seen = 1;
	}
	if (!f.sc->sc_stop_requested && reg == 0x1103) {
		f.bad_power_order |= !f.power_table_seen;
		f.power_post_seen = 1;
	}
	f.writes++;
	if (is_cfg_reg(reg))
		f.cfg_writes++;
	f.mmio[reg] = val;
	if (reg == 0x73 && f.stop_coex_final && f.sc->sc_running &&
	    (f.lte[0x38] & 0xff00) == 0x7700) {
		if (f.stop_coex_final == 1)
			f.sc->sc_stop_requested = 1;
		else
			f.sc->sc_generation++;
	}
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
			const uint8_t *packet = f.sc->sc_fw_dma.vaddr;

			f.bcn_last_pkt_size = (unsigned)packet[0] |
			    (unsigned)packet[1] << 8;
			f.bcn_last_seg1 = (unsigned)bd[8] | (unsigned)bd[9] << 8;
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
	if (reg == 0x80 && (val & 1) && !f.sc->sc_stop_requested)
		f.bad_power_order |= !f.power_post_seen;
	f.writes++;
	if (is_cfg_reg(reg))
		f.cfg_writes++;
	f.mmio[reg] = (uint8_t)val;
	f.mmio[reg + 1] = (uint8_t)(val >> 8);
	if (reg == 0x132c && !f.h2c_dead) {
		uint8_t *bd = (uint8_t *)f.sc->sc_ring[7].vaddr + (val - 1) * 16;
		uint8_t *packet = (uint8_t *)f.sc->sc_fw_dma.vaddr;

		CHECK(val == 1 || val == 2);
		CHECK(bd[0] == 48 && bd[1] == 0 && bd[2] == 1 && bd[3] == 0);
		CHECK(bd[8] == 32 && bd[9] == 0);
		CHECK(packet[0] == 32 && packet[5] == 19);
		CHECK(packet[48] == 1 && packet[49] == 0xff);
		CHECK(packet[50] == (val == 1 ? 0x0d : 0x11));
		CHECK(packet[52] == (val == 1 ? 12 : 16));
		CHECK(packet[54] == val - 1);
		f.h2c_sent++;
		f.h2c_rp = val;
	}
	/* BCN_VALID is write-one-to-clear. */
	if (reg == 0x204)
		f.mmio[0x205] &= (uint8_t)~0x80;
	return 0;
}

static int
write_4(void *cookie, uint32_t reg, uint32_t val)
{
	int error;

	if (f.fail_edca_reg != 0 && reg == f.fail_edca_reg)
		return EIO;
	/* Final HAL RCR write, distinct from the earlier MAC RCR defaults. */
	if (reg == 0x0608 &&
	    (val & ~UINT32_C(0x04000000)) == UINT32_C(0xf010400e)) {
		f.rcr_final_attempts++;
		if (f.fail_rcr_final)
			return EIO;
	}

	(void)cookie;
	error = access_ok(reg, 4);
	if (error != 0)
		return error;
	model_write(reg, 4, val);
	if (reg == 0x1c3c && val == 0xa8003)
		f.dack_seen = 1;
	if ((reg == 0xb0 || reg == 0xb8 || reg == 0x10b8) && val != 0 &&
	    f.dack_samples < 800)
		f.hci_before_dack++;
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
	if (reg == 0x1330 && (val & 0x10100) != 0) {
		f.h2c_rp = 0;
		memset(f.mmio + 0x132c, 0, 4);
	}
	if (reg == 0x1700) {
		unsigned address = val & 0xff;
		uint32_t value;
		unsigned j;

		if ((val & 0xc0000000) == 0xc0000000) {
			value = (uint32_t)f.mmio[0x1704] |
			    (uint32_t)f.mmio[0x1705] << 8 |
			    (uint32_t)f.mmio[0x1706] << 16 |
			    (uint32_t)f.mmio[0x1707] << 24;
			f.lte[address] = value;
		} else {
			value = f.lte[address];
			for (j = 0; j < 4; j++)
				f.mmio[0x1708 + j] = (uint8_t)(value >> (8 * j));
		}
	}
	return 0;
}

static void
delay_us(void *cookie, unsigned us)
{

	(void)cookie;
	model_delay(us);
	if (f.dack_seen && f.stop_dack_delay && !f.sc->sc_stop_requested) {
		f.sc->sc_generation++;
		f.stop_dack_delay = 0;
		f.dack_failed = 1;
	}
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
	CHECK(!f.master); /* No host mapping may outlive unconfirmed DMA ownership. */
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
	f.syncs++;
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
set_test_channel(struct rtw88_softc *sc, unsigned channel)
{

	sc->sc_channel = channel;
	sc->sc_phy.channel = (struct rtw88_channel_tuple){ channel, channel, 20 };
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
	f.firmware_loads++;
	f.power_pre_seen = f.power_table_seen = f.power_post_seen = 0;
	f.bad_power_order = 0;
	f.dack_seen = f.dack_failed = 0;
	f.dack_samples = f.dack_polls = f.dack_after_error = f.hci_before_dack = 0;
	model_reset(0);
	model.recording = 0;
	model.profile = f.quality_dack == 1 ? 6 : f.quality_dack == 2 ? 7 : 0;
	model.invalid = f.fail_dack == 2;
	model.not_ready = f.fail_dack == 3;
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
	if (enable) {
		f.master_enables++;
		f.master = 1; /* A failing enable may already have changed hardware. */
		return f.enable_error ? EIO : 0;
	}
	f.master_disables++;
	if (f.disable_error)
		return EIO;
	f.master = 0;
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
	sc->sc_hci = &rtw88_pci_hci_ops;
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
	efuse_calibrated();
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
test_radio_dependencies(void)
{
	struct rtw88_softc sc;

	reset_bus(&sc);
	if (!attach_ok(&sc))
		return;
	CHECK(rtw88_start(&sc) == 0);
	CHECK(!f.coex_before_h2c); /* main.c rtw_power_on: H2C before coex. */
	CHECK(f.h2c_sent == 2); /* Both mandatory packet commands were consumed. */
	CHECK((f.lte[0x38] & 0xff00) == 0x7700); /* WONLY grants. */
	CHECK((f.mmio[0x73] & 4) != 0); /* WLAN owns the antenna path. */
	CHECK((f.mmio[0x18a2] & 0x7f) != 0); /* CCK reference programmed. */
	CHECK(rtw88_channel(&sc, 36) == 0);
	CHECK((f.mmio[0x18a2] & 0x7f) == 0); /* No CCK on 5 GHz. */
	CHECK(sc.sc_efuse_phys[0] == 0x20 && sc.sc_efuse_phys[1] == 96);
	sc.sc_efuse_phys[1] = 0; /* Invalid CCK calibration result for channel1. */
	CHECK(rtw88_channel(&sc, 1) == ERANGE);
	CHECK(f.mmio[0x522] == 0xff && sc.sc_channel == 36);
	rtw88_stop(&sc);
	rtw88_core_detach(&sc);
	for (unsigned cancel = 1; cancel <= 2; cancel++) {
		reset_bus(&sc);
		f.stop_coex_final = (int)cancel;
		if (!attach_ok(&sc))
			return;
		CHECK(rtw88_start(&sc) == EINTR);
		CHECK(!sc.sc_started && !sc.sc_running && !sc.sc_rf_enabled && !f.master);
		rtw88_core_detach(&sc);
	}
	reset_bus(&sc);
	f.h2c_dead = 1;
	if (!attach_ok(&sc))
		return;
	CHECK(rtw88_start(&sc) == ETIMEDOUT);
	CHECK(!sc.sc_started && !sc.sc_running && !sc.sc_rf_enabled && !f.master);
	rtw88_core_detach(&sc);
	reset_bus(&sc);
	f.coex_busy = 1;
	if (!attach_ok(&sc))
		return;
	CHECK(rtw88_start(&sc) == ETIMEDOUT);
	CHECK(!sc.sc_started && !sc.sc_running && !sc.sc_rf_enabled && !f.master);
	rtw88_core_detach(&sc);
}

/* Actual core+PHY+calibrator: no HCI start before measured cold success. */
static void
test_cold_dack_startup(void)
{
	struct rtw88_softc sc;
	unsigned loads;

	reset_bus(&sc);
	if (!attach_ok(&sc)) return;
	CHECK(rtw88_start(&sc) == 0);
	CHECK(f.dack_samples == 800 && !f.hci_before_dack);
	CHECK(model.visits[0][ADC_POST] == 1 && model.visits[1][DAC_PRE] == 1);
	CHECK(rtw88_core_detach(&sc) == 0);
	/* Advisory residual quality must be visible after the full A/B procedure. */
	for (int kind = 1; kind <= 2; kind++) {
		reset_bus(&sc);
		f.quality_dack = kind;
		if (!attach_ok(&sc)) return;
		CHECK(rtw88_start(&sc) == 0);
		CHECK(sc.sc_started && sc.sc_running && sc.sc_rf_enabled);
		CHECK(model.visits[0][ADC_POST] == (kind == 1 ? 10U : 1U));
		CHECK(model.visits[0][DAC_PRE] == (kind == 2 ? 10U : 1U));
		CHECK(model.visits[1][ADC_POST] == 1 && model.visits[1][DAC_PRE] == 1);
		CHECK(!f.hci_before_dack);
		CHECK(rtw88_core_detach(&sc) == 0);
	}
	for (int kind = 1; kind <= 3; kind++) {
		reset_bus(&sc);
		f.fail_dack = kind;
		if (!attach_ok(&sc)) return;
		CHECK(rtw88_start(&sc) == (kind == 1 ? EIO : kind == 2 ? ERANGE : ETIMEDOUT));
		CHECK(f.dack_seen && f.dack_samples > 0 && f.dack_after_error == 0);
		CHECK(!sc.sc_running && !sc.sc_started && !sc.sc_rf_enabled);
		CHECK(!f.master && !sc.sc_powered && !sc.sc_power_dirty);
		CHECK(f.off_attempts == 2 && f.mmio[0x92] == 0x20 && f.h2c_sent == 0);
		CHECK(rtw88_core_detach(&sc) == 0);
	}
	reset_bus(&sc);
	f.stop_dack_delay = 1;
	if (!attach_ok(&sc)) return;
	CHECK(rtw88_start(&sc) == EINTR);
	CHECK(f.dack_seen && f.h2c_sent == 0 && !sc.sc_running && !f.master);
	CHECK(f.dack_after_error == 0);
	CHECK(f.off_attempts == 2 && !sc.sc_power_dirty);
	CHECK(rtw88_core_detach(&sc) == 0);
	/* Preserve the calibration error when shutdown fails; block unsafe retry. */
	reset_bus(&sc);
	f.fail_dack = 2; f.off_error = 1;
	if (!attach_ok(&sc)) return;
	CHECK(rtw88_start(&sc) == ERANGE);
	CHECK(sc.sc_power_dirty && sc.sc_power_error == EIO && !sc.sc_running);
	loads = f.firmware_loads;
	CHECK(rtw88_start(&sc) == EBUSY && f.firmware_loads == loads);
	f.off_error = f.fail_dack = 0;
	CHECK(rtw88_stop(&sc) == 0);
	CHECK(rtw88_core_detach(&sc) == 0);
}

/* Shutdown must finish the off table without lowering its stop fence. */
static void
test_power_system_integration(void)
{
	struct rtw88_softc sc;
	uint32_t generation;

	reset_bus(&sc);
	if (!attach_ok(&sc))
		return;
	CHECK(rtw88_start(&sc) == 0);
	CHECK((f.mmio[0x03] & 0xd8) == 0xd8);
	CHECK((f.mmio[0x1103] & 0x0f) == 0x0c);
	CHECK(f.power_pre_seen && f.power_table_seen && f.power_post_seen &&
	    !f.bad_power_order);
	f.mmio[0x92] = 0;
	generation = sc.sc_generation;
	rtw88_stop(&sc);
	CHECK(sc.sc_stop_requested == 1 && sc.sc_generation == generation + 1);
	CHECK(f.mmio[0x92] == 0x20); /* Last card-disable stage, after off poll. */
	CHECK(!sc.sc_powered && !sc.sc_dma_master && !f.master);
	rtw88_core_detach(&sc);
}

/* Failed power setup must run off even before sc_powered becomes true. */
static void
test_power_setup_unwind(void)
{
	struct rtw88_softc sc;

	for (int stage = 1; stage <= 3; stage++) {
		reset_bus(&sc);
		if (!attach_ok(&sc))
			return;
		f.fail_power_stage = stage;
		CHECK(rtw88_start(&sc) == EIO);
		/* The two off-table stages each read 0x93, and finish at 0x92. */
		CHECK(f.off_attempts == 2 && f.mmio[0x92] == 0x20);
		CHECK(!sc.sc_power_dirty && !sc.sc_power_error && !sc.sc_dma_error);
		CHECK(!sc.sc_powered && !sc.sc_dma_master && !f.master);
		CHECK(!sc.sc_fw_dma_owned && !sc.sc_started);
		CHECK(rtw88_core_detach(&sc) == 0);
		CHECK(f.dma_allocs == 0);
	}
}

/* Off failures remain visible, keep the fence and forbid a fresh startup. */
static void
test_shutdown_failure_retention(void)
{
	struct rtw88_softc sc;
	unsigned allocations, loads, delays;

	for (int kind = 0; kind < 2; kind++) {
		reset_bus(&sc);
		if (!attach_ok(&sc))
			return;
		CHECK(rtw88_start(&sc) == 0);
		f.expect_stop = 1;
		f.off_stuck = kind == 0;
		f.off_error = kind == 1;
		delays = f.delays;
		CHECK(rtw88_stop(&sc) == (kind == 0 ? ETIMEDOUT : EIO));
		CHECK(sc.sc_power_dirty && sc.sc_power_error ==
		    (kind == 0 ? ETIMEDOUT : EIO));
		CHECK(sc.sc_stop_requested && !sc.sc_started && !sc.sc_running);
		CHECK(!sc.sc_dma_master && !f.master && !sc.sc_fw_dma_owned);
		if (kind == 0)
			CHECK(f.delays - delays == 20000); /* One bounded one-second poll. */
		allocations = f.dma_allocs;
		loads = f.firmware_loads;
		CHECK(rtw88_start(&sc) == EBUSY && f.firmware_loads == loads);
		CHECK(rtw88_core_detach(&sc) == (kind == 0 ? ETIMEDOUT : EIO));
		CHECK(sc.sc_resources_ready && f.dma_allocs == allocations);
		CHECK(sc.sc_intr_established);
		f.off_stuck = f.off_error = 0;
		CHECK(rtw88_stop(&sc) == 0);
		CHECK(!sc.sc_power_dirty && !sc.sc_power_error);
		CHECK(rtw88_core_detach(&sc) == 0 && f.dma_allocs == 0);
	}
}

/* Mastering errors can change hardware before the method reports failure. */
static void
test_master_failure_retention(void)
{
	struct rtw88_softc sc;
	unsigned allocations, loads;

	reset_bus(&sc);
	if (!attach_ok(&sc))
		return;
	f.enable_error = 1;
	CHECK(rtw88_start(&sc) == EIO);
	CHECK(f.master_enables == 1 && f.master_disables == 1 && !f.master);
	CHECK(!sc.sc_dma_master && !sc.sc_fw_dma_owned);
	CHECK(rtw88_core_detach(&sc) == 0 && f.dma_allocs == 0);
	reset_bus(&sc);
	if (!attach_ok(&sc))
		return;
	CHECK(rtw88_start(&sc) == 0);
	allocations = f.dma_allocs;
	f.disable_error = 1;
	CHECK(rtw88_stop(&sc) == EIO);
	CHECK(sc.sc_dma_master && sc.sc_dma_error == EIO && f.master);
	CHECK(sc.sc_fw_dma_owned && f.dma_allocs == allocations);
	CHECK(sc.sc_power_error == 0 && !sc.sc_power_dirty);
	loads = f.firmware_loads;
	CHECK(rtw88_start(&sc) == EBUSY && f.firmware_loads == loads);
	CHECK(rtw88_core_detach(&sc) == EIO);
	CHECK(sc.sc_resources_ready && sc.sc_intr_established);
	CHECK(f.dma_allocs == allocations);
	f.disable_error = 0;
	CHECK(rtw88_core_detach(&sc) == 0 && !f.master && f.dma_allocs == 0);
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

		assert(bd != NULL && pkt != NULL);
		dma = (uint32_t)bd[4] | (uint32_t)bd[5] << 8 |
		    (uint32_t)bd[6] << 16 | (uint32_t)bd[7] << 24;
		desa = (uint32_t)f.mmio[0x308] | (uint32_t)f.mmio[0x309] << 8 |
		    (uint32_t)f.mmio[0x30a] << 16 |
		    (uint32_t)f.mmio[0x30b] << 24;
		/* Snapshot at beacon consumption, before packet-H2C reuses the map. */
		pkt_size = (uint16_t)f.bcn_last_pkt_size;
		seg1 = (uint16_t)f.bcn_last_seg1;
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

/* A new firmware instance starts its host mailbox sequence at box 0. */
static void
test_command_after_restart(void)
{
	struct rtw88_softc sc;
	uint8_t payload = 0x31;

	reset_bus(&sc);
	if (!attach_ok(&sc))
		return;
	CHECK(rtw88_start(&sc) == 0);
	CHECK(rtw88_fw_command(&sc, 0x01, &payload, 1) == 0);
	CHECK(mmio32(0x1d0) == 0x3101);
	rtw88_stop(&sc);
	CHECK(!sc.sc_started && f.master == 0);
	CHECK(rtw88_start(&sc) == 0);
	payload = 0x72;
	CHECK(rtw88_fw_command(&sc, 0x2c, &payload, 1) == 0);
	CHECK(mmio32(0x1d0) == 0x722c);
	rtw88_stop(&sc);
	rtw88_core_detach(&sc);
	CHECK(f.dma_allocs == 0);
}

/* An uncertain runtime packet retains DMA until stop confirms quiescence. */
static void
test_packet_epoch_recovery(void)
{
	struct rtw88_softc sc;
	const uint8_t command[32] = {1, 0xff, 0x0d, 0, 12, 0, 0, 0, [10] = 56};
	unsigned loads, allocations, writes;

	reset_bus(&sc);
	if (!attach_ok(&sc))
		return;
	CHECK(rtw88_start(&sc) == 0);
	CHECK(sc.sc_packet_h2c.initialized && sc.sc_packet_h2c.sequence == 2);
	f.h2c_dead = 1;
	CHECK(rtw88_fw_packet(&sc, command, sizeof(command)) == ETIMEDOUT);
	CHECK(sc.sc_packet_h2c.dirty && sc.sc_packet_h2c.error == ETIMEDOUT);
	writes = f.writes;
	CHECK(rtw88_fw_packet(&sc, command, sizeof(command)) == EBUSY);
	CHECK(rtw88_fw_init(&sc) == EBUSY && f.writes == writes);
	f.disable_error = 1;
	allocations = f.dma_allocs;
	CHECK(rtw88_stop(&sc) == EIO);
	CHECK(!sc.sc_packet_h2c.active && sc.sc_packet_h2c.dirty);
	CHECK(sc.sc_fw_dma_owned && sc.sc_dma_master && f.dma_allocs == allocations);
	loads = f.firmware_loads;
	CHECK(rtw88_start(&sc) == EBUSY && f.firmware_loads == loads);
	f.disable_error = f.h2c_dead = 0;
	CHECK(rtw88_stop(&sc) == 0 && !sc.sc_fw_dma_owned);
	CHECK(sc.sc_packet_h2c.error == ETIMEDOUT && sc.sc_packet_h2c.dirty);
	CHECK(rtw88_start(&sc) == 0);
	CHECK(sc.sc_packet_h2c.active && sc.sc_packet_h2c.initialized);
	CHECK(sc.sc_packet_h2c.sequence == 2 && sc.sc_packet_h2c.wp == 2);
	CHECK(!sc.sc_packet_h2c.error && !sc.sc_packet_h2c.dirty);
	CHECK(rtw88_stop(&sc) == 0 && rtw88_core_detach(&sc) == 0);
	CHECK(f.dma_allocs == 0);
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
#ifdef RTW88_RFK_EXPERIMENT
    CHECK(!sc->sc_phy.channel_valid);
#else
    CHECK(sc->sc_phy.channel_valid);
#endif
    /* Full MAC initialization owns the existing legacy hardware defaults. */
	CHECK(mmio32(0x500) == UINT32_C(0x002fa226));
	CHECK(mmio32(0x504) == UINT32_C(0x005ea328));
	CHECK(mmio32(0x508) == UINT32_C(0x005ea42b));
	CHECK(mmio32(0x50c) == UINT32_C(0x0000a44f));
	return sc->sc_running;
}

static void
test_edca_quarantine_recovery(void)
{
	struct rtw88_softc sc;
	const struct rtw88_edca_params params[4] = {
		{3, 4, 10, 0}, {7, 4, 10, 0}, {2, 3, 4, 94}, {2, 2, 3, 47}
	};
	uint8_t frame[24] = {0x08, 0x02};

	if (!start_ok(&sc))
		return;
	f.fail_edca_reg = 0x504;
	CHECK(rtw88_edca_apply(&sc, params, 9) == EIO);
	CHECK(sc.sc_edca_error == EIO && f.mmio[0x522] == 0xff);
	const uint8_t bssid[6] = {2, 1, 2, 3, 4, 5};
	unsigned writes = f.writes;
	CHECK(rtw88_station_config(&sc, bssid, 1, 0xf, 9, 1) == EIO);
	CHECK(f.writes == writes && f.mmio[0x522] == 0xff);
	CHECK(rtw88_tx_available(&sc, RTW88_TXQ_BE) == 0);
	CHECK(rtw88_tx_raw(&sc, RTW88_TXQ_BE, frame, sizeof(frame)) == EIO);
	CHECK(rtw88_stop(&sc) == 0);
	CHECK(sc.sc_edca_error == EIO);
	f.fail_edca_reg = 0;
	f.fail_efuse = 1;
	CHECK(rtw88_start(&sc) == EIO);
	CHECK(sc.sc_edca_error == EIO);
	f.fail_efuse = 0;
	CHECK(rtw88_start(&sc) == 0);
	CHECK(sc.sc_edca_error == 0 && f.mmio[0x522] == 0);
	CHECK(rtw88_edca_apply(&sc, params, 9) == 0);
	CHECK(rtw88_tx_raw(&sc, RTW88_TXQ_BE, frame, sizeof(frame)) == 0);
	CHECK(rtw88_stop(&sc) == 0);
	CHECK(rtw88_core_detach(&sc) == 0);
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

static unsigned rx_delivered, tx_completed;
static uint8_t rx_copy[32];
static size_t rx_copy_len;

static void
network_rx(void *cookie, const uint8_t *p, size_t len,
    const struct rtw88_rx_info *info)
{
	CHECK(cookie == &rx_delivered);
	CHECK(info->packet_size == len && len <= sizeof(rx_copy));
	CHECK((info->flags & (RTW88_RX_CRC_ERROR | RTW88_RX_ICV_ERROR |
	    RTW88_RX_C2H)) == 0);
	rx_delivered++;
	rx_copy_len = len;
	memcpy(rx_copy, p, len);
}

static void
network_tx(void *cookie, unsigned count)
{
	CHECK(cookie == &rx_delivered && count != 0);
	tx_completed += count;
}

static void
test_network_delivery(void)
{
	struct rtw88_softc sc;
	uint8_t frame[28] = { 0x80, 0x00 };
	unsigned i;

	if (!start_ok(&sc))
		return;
	rx_delivered = tx_completed = 0;
	rx_copy_len = 0;
	sc.sc_net_cookie = &rx_delivered;
	sc.sc_rx_frame = network_rx;
	sc.sc_tx_complete = network_tx;
	frame[24] = 0xde; frame[25] = 0xad; frame[26] = 0xbe; frame[27] = 0xef;
	for (i = 0; i < 4; i++)
		rx_put(&sc, i, sizeof(frame), 0, frame, (uint16_t)i);
	/* CRC error, ICV error, and a C2H event must never reach the stack. */
	((uint8_t *)sc.sc_rxbuf[1].vaddr)[1] |= 0x40;
	((uint8_t *)sc.sc_rxbuf[2].vaddr)[1] |= 0x80;
	((uint8_t *)sc.sc_rxbuf[3].vaddr)[11] |= 0x10;
	/* A valid-looking frame with an unexpected DMA tag has no ownership. */
	rx_put(&sc, 4, sizeof(frame), 0, frame, 77);
	/* A bare ACK cannot be offered as a full net80211 header. */
	rx_put(&sc, 5, 14, 0, frame, 5);
	f.rx_hw_idx = 6;
	set_hisr0(0x01);
	CHECK(rtw88_intr(&sc) == 1);
	CHECK(rx_delivered == 1 && rx_copy_len == 24);
	CHECK(memcmp(rx_copy, frame, 24) == 0);
	CHECK(sc.sc_stats.rx_errors == 2 && sc.sc_stats.rx_tag_errors == 1);
	CHECK(rx_armed(&sc, 4) && rx_armed(&sc, 5));
	/* A compressed single-TID BAR is20 bytes plus APP_FCS. Other short
	 * control shapes and descriptor faults stay below the native callback. */
	uint8_t bar[24] = {0x84};bar[16]=4;
	for (i=6;i<13;i++)
		rx_put(&sc,i,sizeof(bar),0,bar,(uint16_t)i);
	((uint8_t *)sc.sc_rxbuf[7].vaddr)[RTW8822C_RX_PACKET_DESC_SIZE+16]=0;
	((uint8_t *)sc.sc_rxbuf[8].vaddr)[RTW8822C_RX_PACKET_DESC_SIZE+17]=0x80;
	((uint8_t *)sc.sc_rxbuf[9].vaddr)[RTW8822C_RX_PACKET_DESC_SIZE+18]=1;
	((uint8_t *)sc.sc_rxbuf[10].vaddr)[1]|=0x40;
	((uint8_t *)sc.sc_rxbuf[11].vaddr)[1]|=0x80;
	((uint8_t *)sc.sc_rxbuf[12].vaddr)[11]|=0x10;
	f.rx_hw_idx=13;set_hisr0(0x01);CHECK(rtw88_intr(&sc)==1);
	CHECK(rx_delivered==2 && rx_copy_len==20 && memcmp(rx_copy,bar,20)==0);
	CHECK(rtw88_tx_raw(&sc, RTW88_TXQ_MGMT, frame, 24) == 0);
	f.tx_hw_rp[0] = 1;
	set_hisr0(0x40); /* MGNTDOK. */
	CHECK(rtw88_intr(&sc) == 1 && tx_completed == 1);
	set_hisr0(0x40);
	CHECK(rtw88_intr(&sc) == 1 && tx_completed == 1);
	rtw88_stop(&sc);
	f.rx_hw_idx = 14;
	set_hisr0(0x41);
	CHECK(rtw88_intr(&sc) == 0 && rx_delivered == 2 && tx_completed == 1);
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

/* Capture metadata from the actual DMA decoder and core callback. */
static struct {
	uint64_t sequence[RTW88_RX_BUFS];
	uint32_t generation;
	unsigned count;
} rx_metadata;

static void
rx_metadata_capture(void *cookie, const uint8_t *p, size_t len,
    const struct rtw88_rx_info *info)
{

	CHECK(cookie == &rx_metadata && p[0] == 0x80 && len == 24);
	CHECK(info->packet_size == len);
	CHECK(info->generation == rx_metadata.generation);
	CHECK(rx_metadata.count < RTW88_RX_BUFS);
	if (rx_metadata.count < RTW88_RX_BUFS)
		rx_metadata.sequence[rx_metadata.count++] = info->sequence;
}

/* The frontier includes the completed tail that the 64-slot IRQ leaves. */
static void
test_rx_completed_frontier(void)
{
	struct rtw88_softc sc;
	uint8_t frame[28] = { 0x80 };
	uint64_t frontier;
	unsigned i, writes, syncs;

	if (!start_ok(&sc))
		return;
	memset(&rx_metadata, 0, sizeof(rx_metadata));
	rx_metadata.generation = sc.sc_generation;
	sc.sc_rx_frame = rx_metadata_capture;
	sc.sc_net_cookie = &rx_metadata;
	for (i = 0; i < 100; i++)
		rx_put(&sc, i, sizeof(frame), 0, frame, (uint16_t)i);
	f.rx_hw_idx = 100;
	writes = f.writes;
	syncs = f.syncs;
	CHECK(rtw88_rx_frontier(&sc, sc.sc_generation, &frontier) == 0);
	CHECK(frontier == 100 && sc.sc_rx_sequence == 0);
	CHECK(sc.sc_rx_rp == 0 && sc.sc_rx_tag == 0 && host_rx_idx() == 0);
	CHECK(f.writes == writes && f.syncs == syncs);
	set_hisr0(1);
	CHECK(rtw88_intr(&sc) == 1 && rx_metadata.count == 64);
	CHECK(sc.sc_rx_sequence == 64);
	CHECK(rtw88_rx_frontier(&sc, sc.sc_generation, &frontier) == 0);
	CHECK(frontier == 100);
	set_hisr0(1);
	CHECK(rtw88_intr(&sc) == 1 && rx_metadata.count == 100);
	for (i = 0; i < 100; i++)
		CHECK(rx_metadata.sequence[i] == i + 1);
	rx_put(&sc, 100, sizeof(frame), 0, frame, 100);
	f.rx_hw_idx = 101;
	CHECK(rtw88_rx_frontier(&sc, sc.sc_generation, &frontier) == 0);
	CHECK(frontier == 101);
	set_hisr0(1);
	CHECK(rtw88_intr(&sc) == 1 && rx_metadata.count == 101);
	CHECK(rx_metadata.sequence[100] == 101);
	/* Completed producer wrap includes 27 tail slots and four head slots. */
	rx_metadata.count = 0;
	for (i = 101; i < 132; i++)
		rx_put(&sc, i % RTW88_RX_BUFS, sizeof(frame), 0, frame,
		    (uint16_t)i);
	f.rx_hw_idx = 4;
	CHECK(rtw88_rx_frontier(&sc, sc.sc_generation, &frontier) == 0 &&
	    frontier == 132);
	set_hisr0(1);
	CHECK(rtw88_intr(&sc) == 1 && rx_metadata.count == 31);
	CHECK(rx_metadata.sequence[0] == 102 && rx_metadata.sequence[30] == 132);
	CHECK(sc.sc_rx_sequence == 132 && sc.sc_stats.rx_tag_errors == 0);
	rtw88_stop(&sc);
	rtw88_core_detach(&sc);
}

/* Invalid descriptors still consume their host sequence before validation. */
static void
test_rx_sequence_rejections(void)
{
	struct rtw88_softc sc;
	uint8_t frame[28] = { 0x80 }, c2h[4] = { 9, 5 };
	uint64_t frontier;
	unsigned i;

	if (!start_ok(&sc))
		return;
	memset(&rx_metadata, 0, sizeof(rx_metadata));
	rx_metadata.generation = sc.sc_generation;
	sc.sc_rx_frame = rx_metadata_capture;
	sc.sc_net_cookie = &rx_metadata;
	for (i = 0; i < 7; i++)
		rx_put(&sc, i, sizeof(frame), 0, frame, (uint16_t)i);
	rx_put(&sc, 1, sizeof(frame), 0, frame, 77);
	rx_put(&sc, 2, 0x3fff, 0, NULL, 2);
	rx_put(&sc, 3, sizeof(c2h), 1, c2h, 3);
	((uint8_t *)sc.sc_rxbuf[4].vaddr)[1] |= 0x40;
	rx_put(&sc, 5, 4, 0, frame, 5);
	f.rx_hw_idx = 7;
	set_hisr0(1);
	CHECK(rtw88_intr(&sc) == 1);
	CHECK(rx_metadata.count == 2 && rx_metadata.sequence[0] == 1 &&
	    rx_metadata.sequence[1] == 7);
	CHECK(sc.sc_rx_sequence == 7 && sc.sc_rx_rp == 7);
	CHECK(sc.sc_stats.rx_tag_errors == 1 && sc.sc_stats.rx_c2h == 1 &&
	    sc.sc_stats.rx_errors == 2 && sc.sc_stats.rx_short == 1);
	CHECK(rtw88_rx_frontier(&sc, sc.sc_generation, &frontier) == 0 &&
	    frontier == 7);
	rtw88_stop(&sc);
	rtw88_core_detach(&sc);
}

/* One full 8192-tag period and a ring wrap must not reset the sequence. */
static void
test_rx_sequence_wrap_restart(void)
{
	struct rtw88_softc sc;
	uint8_t frame[28] = { 0x80 };
	uint32_t generation;
	uint64_t frontier;
	unsigned n;

	if (!start_ok(&sc))
		return;
	memset(&rx_metadata, 0, sizeof(rx_metadata));
	rx_metadata.generation = sc.sc_generation;
	sc.sc_rx_frame = rx_metadata_capture;
	sc.sc_net_cookie = &rx_metadata;
	generation = sc.sc_generation;
	for (n = 0; n < 8194; n++) {
		rx_metadata.count = 0;
		rx_put(&sc, n % RTW88_RX_BUFS, sizeof(frame), 0, frame,
		    (uint16_t)(n % 8192));
		f.rx_hw_idx = (n + 1) % RTW88_RX_BUFS;
		set_hisr0(1);
		CHECK(rtw88_intr(&sc) == 1 && rx_metadata.count == 1);
		CHECK(rx_metadata.sequence[0] == n + 1);
	}
	CHECK(sc.sc_rx_sequence == 8194 && sc.sc_rx_tag == 2);
	CHECK(rtw88_rx_frontier(&sc, generation, &frontier) == 0 &&
	    frontier == 8194);
	CHECK(rtw88_stop(&sc) == 0);
	CHECK(rtw88_start(&sc) == 0 && sc.sc_generation != generation);
	CHECK(sc.sc_rx_sequence == 0 && sc.sc_rx_rp == 0 && sc.sc_rx_tag == 0);
	f.rx_hw_idx = 0;
	CHECK(rtw88_rx_frontier(&sc, generation, &frontier) == EINTR &&
	    frontier == 0);
	rx_metadata.generation = sc.sc_generation;
	rx_metadata.count = 0;
	rx_put(&sc, 0, sizeof(frame), 0, frame, 0);
	f.rx_hw_idx = 1;
	set_hisr0(1);
	CHECK(rtw88_intr(&sc) == 1 && rx_metadata.count == 1);
	CHECK(rx_metadata.sequence[0] == 1);
	rtw88_stop(&sc);
	rtw88_core_detach(&sc);
}

/* Each rejected snapshot clears its result without writes or DMA changes. */
static void
rx_frontier_reject(struct rtw88_softc *sc, uint32_t generation,
    int expected, unsigned reads)
{
	uint64_t frontier = 42, sequence = sc->sc_rx_sequence;
	unsigned before = f.rx_idx_reads, writes = f.writes, syncs = f.syncs;
	unsigned rp = sc->sc_rx_rp, tag = sc->sc_rx_tag;

	CHECK(rtw88_rx_frontier(sc, generation, &frontier) == expected);
	CHECK(frontier == 0 && f.rx_idx_reads == before + reads);
	CHECK(f.writes == writes && f.syncs == syncs);
	CHECK(sc->sc_rx_sequence == sequence && sc->sc_rx_rp == rp &&
	    sc->sc_rx_tag == tag);
}

static void
test_rx_frontier_errors(void)
{
	struct rtw88_softc sc;
	struct rtw88_dma ring;
	struct rtw88_bus_ops invalid_ops = ops;
	uint64_t frontier = 42;
	uint32_t generation;

	if (!start_ok(&sc))
		return;
	generation = sc.sc_generation;
	CHECK(rtw88_rx_frontier(NULL, generation, &frontier) == EINVAL &&
	    frontier == 0);
	CHECK(rtw88_rx_frontier(&sc, generation, NULL) == EINVAL);
	rx_frontier_reject(&sc, generation + 1, EINTR, 0);
	sc.sc_started = 0;
	rx_frontier_reject(&sc, generation, ENXIO, 0);
	sc.sc_started = 1;
	sc.sc_running = 0;
	rx_frontier_reject(&sc, generation, ENXIO, 0);
	sc.sc_running = 1;
	sc.sc_resources_ready = 0;
	rx_frontier_reject(&sc, generation, ENXIO, 0);
	sc.sc_resources_ready = 1;
	sc.sc_stop_requested = 1;
	rx_frontier_reject(&sc, generation, ENXIO, 0);
	sc.sc_stop_requested = 0;
	sc.sc_dma_master = 0;
	rx_frontier_reject(&sc, generation, ENXIO, 0);
	sc.sc_dma_master = 1;
	sc.sc_dma_error = EIO;
	rx_frontier_reject(&sc, generation, ENXIO, 0);
	sc.sc_dma_error = 0;
	sc.sc_ops = NULL;
	rx_frontier_reject(&sc, generation, EINVAL, 0);
	invalid_ops.read_4 = NULL;
	sc.sc_ops = &invalid_ops;
	rx_frontier_reject(&sc, generation, EINVAL, 0);
	sc.sc_ops = &ops;
	sc.sc_nrxbuf = 0;
	rx_frontier_reject(&sc, generation, EINVAL, 0);
	sc.sc_nrxbuf = RTW88_RX_BUFS + 1;
	rx_frontier_reject(&sc, generation, EINVAL, 0);
	sc.sc_nrxbuf = RTW88_RX_BUFS;
	sc.sc_rx_rp = RTW88_RX_BUFS;
	rx_frontier_reject(&sc, generation, EINVAL, 0);
	sc.sc_rx_rp = 0;
	ring = sc.sc_ring[RTW88_RXRING];
	sc.sc_ring[RTW88_RXRING].vaddr = NULL;
	rx_frontier_reject(&sc, generation, EINVAL, 0);
	sc.sc_ring[RTW88_RXRING] = ring;
	sc.sc_ring[RTW88_RXRING].size = RTW88_RX_BUFS * 8 - 1;
	rx_frontier_reject(&sc, generation, EINVAL, 0);
	sc.sc_ring[RTW88_RXRING].size = ring.size + 1;
	rx_frontier_reject(&sc, generation, EINVAL, 0);
	sc.sc_ring[RTW88_RXRING] = ring;
	f.rx_hw_idx = RTW88_RX_BUFS;
	rx_frontier_reject(&sc, generation, EINVAL, 1);
	f.rx_hw_idx = 0;
	f.rx_idx_error = 1;
	rx_frontier_reject(&sc, generation, EIO, 1);
	f.rx_idx_error = 0;
	f.rx_idx_change_generation = 1;
	rx_frontier_reject(&sc, generation, EINTR, 1);
	f.rx_idx_change_generation = 0;
	generation = sc.sc_generation;
	f.rx_idx_stop = 1;
	rx_frontier_reject(&sc, generation, ENXIO, 1);
	f.rx_idx_stop = 0;
	sc.sc_stop_requested = 0;
	rtw88_stop(&sc);
	rx_frontier_reject(&sc, sc.sc_generation, ENXIO, 0);
	rtw88_core_detach(&sc);
}

/* Exhaustion invalidates future metadata; normal packet delivery continues. */
static void
test_rx_sequence_exhaustion(void)
{
	struct rtw88_softc sc;
	uint8_t frame[28] = { 0x80 };
	uint64_t frontier;

	if (!start_ok(&sc))
		return;
	memset(&rx_metadata, 0, sizeof(rx_metadata));
	rx_metadata.generation = sc.sc_generation;
	sc.sc_rx_frame = rx_metadata_capture;
	sc.sc_net_cookie = &rx_metadata;
	sc.sc_rx_sequence = UINT64_MAX - 2;
	f.rx_hw_idx = 1;
	CHECK(rtw88_rx_frontier(&sc, sc.sc_generation, &frontier) == 0 &&
	    frontier == UINT64_MAX - 1);
	sc.sc_rx_sequence = UINT64_MAX - 1;
	rx_frontier_reject(&sc, sc.sc_generation, EOVERFLOW, 1);
	f.rx_hw_idx = 2;
	rx_frontier_reject(&sc, sc.sc_generation, EOVERFLOW, 1);
	rx_put(&sc, 0, sizeof(frame), 0, frame, 0);
	rx_put(&sc, 1, sizeof(frame), 0, frame, 1);
	set_hisr0(1);
	CHECK(rtw88_intr(&sc) == 1 && rx_metadata.count == 2);
	CHECK(rx_metadata.sequence[0] == UINT64_MAX &&
	    rx_metadata.sequence[1] == 0 && sc.sc_rx_sequence == UINT64_MAX);
	f.rx_hw_idx = 2;
	rx_frontier_reject(&sc, sc.sc_generation, EOVERFLOW, 0);
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
	CHECK(sc.sc_stats.rx_tag_errors == 1 && sc.sc_stats.rx_frames == 0);
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

/* Partial allocation on any producer must release every preceding mapping. */
static void
test_tx_buffer_alloc_failure(void)
{
	static const unsigned slots[3] = {0, 15, 31};
	struct rtw88_softc sc;
	unsigned q, j, i;

	for (q = 0; q < 5; q++) {
		for (j = 0; j < 3; j++) {
			reset_bus(&sc);
			f.fail_alloc_at = RTW88_NRING + RTW88_RX_BUFS +
			    q * 32 + slots[j] + 1;
			int error = rtw88_core_attach(&sc);
			CHECK(error == ENOMEM);
			if (error == 0) {
				rtw88_core_detach(&sc);
				continue;
			}
			CHECK(!sc.sc_resources_ready && !sc.sc_intr_established);
			CHECK(sc.sc_nring == 0 && sc.sc_nrxbuf == 0);
			for (i = 0; i < RTW88_NTXQ; i++)
				CHECK(sc.sc_txq[i].nbuf == 0);
			CHECK(f.dma_allocs == 0);
			f.fail_alloc_at = 0;
			CHECK(rtw88_core_attach(&sc) == 0);
			CHECK(rtw88_start(&sc) == 0 && sc.sc_started);
			CHECK(rtw88_core_detach(&sc) == 0 && f.dma_allocs == 0);
		}
	}
}

static void
test_diag_regs(void)
{
	struct rtw88_softc sc;
	uint32_t r[8] = { 0 };
	unsigned writes, reads;

	if (!start_ok(&sc))
		return;
	set_hisr0(0x01);
	f.rx_hw_idx = 5;
	memcpy(f.mmio + 0x3c60, "\x45\x23\x01\x00", 4);
	memcpy(f.mmio + 0x4c60, "\xde\xbc\x0a\x00", 4);
	f.diag_reads = 0;
	writes = f.writes;
	CHECK(rtw88_diag(&sc, r) == 0);
	CHECK(r[0] == 0x01 && r[1] == 0x44fd);		/* HISR0, HIMR0 */
	CHECK(((r[4] >> 16) & 0xfff) == 5);		/* RXBD_IDX */
	CHECK(mmio32(0x00b4) == 0x01);			/* Reading is not acking. */
	CHECK(r[6] == 0x12345 && r[7] == 0xabcde);
	CHECK(f.writes == writes && f.diag_reads == 2);
	f.fail_diag_reg = 0x3c60;
	CHECK(rtw88_diag(&sc, r) == EIO);
	CHECK(f.diag_reads == 3);			/* Do not read B after A fails. */
	f.fail_diag_reg = 0x4c60;
	CHECK(rtw88_diag(&sc, r) == EIO);
	CHECK(f.diag_reads == 5 && f.writes == writes);
	f.fail_diag_reg = 0;
	rtw88_stop(&sc);
	reads = f.diag_reads;
	CHECK(rtw88_diag(&sc, r) == ENXIO && f.diag_reads == reads);
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

/* Legacy station TX must use a legal basic rate for the selected band. */
static void
test_station_config(void)
{
	static const uint8_t mac[6] = { 0x02, 0x12, 0x23, 0x34, 0x45, 0x56 };
	static const uint8_t bssid[6] = { 0x02, 0x65, 0x54, 0x43, 0x32, 0x21 };
	struct rtw88_softc sc;
	unsigned writes;
	uint32_t rcr;

	if (!start_ok(&sc))
		return;
	memcpy(sc.sc_mac, mac, sizeof(mac));
	f.mmio[0x680] = 0xff;
	f.mmio[0x551] = 0x71;
	f.mmio[0x6ab] = 0x8a;
	rcr = mmio32(0x608);
	CHECK(rtw88_station_config(&sc, NULL, 0, 0xf, 20, 0) == 0);
	CHECK(memcmp(f.mmio + 0x610, mac, sizeof(mac)) == 0);
	CHECK((mmio32(0x100) & 0x30000) == 0);
	CHECK((f.mmio[0x680] & 0x0c) == 0 && (f.mmio[0x680] & 0xf3) == 0xf3);
	CHECK((f.mmio[0x550] & 0x18) == 0x18 && f.mmio[0x551] == 0x71);
	CHECK((mmio32(0x608) & 0xc0) == 0 && (mmio32(0x608) & 0x80000000) != 0);
	CHECK(mmio32(0x608) == (rcr & ~UINT32_C(0xc0)));
	CHECK((mmio32(0x608) & UINT32_C(0x04000000)) != 0);
	CHECK(reg16(0x6a4) == 0 && (f.mmio[0x522] & 0x0f) == 0x0f);
	CHECK(f.mmio[0x51b] == 20 && (mmio32(0x440) & 0xfff) == 0xf);
	writes = f.writes;
	CHECK(rtw88_station_config(&sc, bssid, 2008, 0x10, 9, 1) == EINVAL);
	CHECK(rtw88_station_config(&sc, bssid, 1, 0, 9, 1) == EINVAL);
	CHECK(rtw88_station_config(&sc, bssid, 1, 0x1000, 9, 1) == EINVAL);
	CHECK(rtw88_station_config(&sc, bssid, 1, 0x10, 10, 1) == EINVAL);
	CHECK(rtw88_station_config(&sc, NULL, 1, 0x10, 9, 1) == EINVAL);
	CHECK(f.writes == writes);
	CHECK(rtw88_station_config(&sc, bssid, 123, 0xff0, 9, 1) == 0);
	CHECK(mmio32(0x1d0) == 0x01 && mmio32(0x1d4) == 0x0101 &&
	    mmio32(0x1d8) == 0x2c); /* media off/on and default port0/MACID0. */
	CHECK(memcmp(f.mmio + 0x618, bssid, sizeof(bssid)) == 0);
	CHECK((mmio32(0x100) & 0x30000) == 0x20000);
	CHECK((mmio32(0x6a8) & 0x7ff) == 123 && f.mmio[0x6ab] == 0x8a);
	CHECK((f.mmio[0x550] & 0x18) == 0x08);
	CHECK((mmio32(0x608) & 0xc0) == 0xc0);
	CHECK(mmio32(0x608) == (rcr | UINT32_C(0xc0)));
	CHECK((mmio32(0x608) & UINT32_C(0x04000000)) != 0);
	CHECK(reg16(0x6a4) == 0xffff && (f.mmio[0x522] & 0x0f) == 0);
	CHECK(f.mmio[0x51b] == 9 && (mmio32(0x440) & 0xfff) == 0xff0);
	CHECK(rtw88_tx_raw(&sc, RTW88_TXQ_MGMT, bcast_frame,
	    sizeof(bcast_frame)) == 0);
	CHECK(((const uint8_t *)sc.sc_txq[0].buf[0].vaddr)[16] == 4);
	CHECK(rtw88_tx_available(&sc, RTW88_TXQ_BE) == RTW88_TX_SLOTS - 1);
	CHECK(rtw88_tx_available(&sc, 7) == 0);
	f.mmio[0x1cc] = 0x0f; /* All mailbox slots remain busy. */
	CHECK(rtw88_station_config(&sc, NULL, 0, 0xf, 20, 0) == ETIMEDOUT);
	rtw88_stop(&sc);
	CHECK(rtw88_tx_available(&sc, 0) == 0);
	CHECK(rtw88_station_config(&sc, NULL, 0, 0xf, 20, 0) == ENXIO);
	rtw88_core_detach(&sc);
}

static void
test_tx_band_and_sequence(void)
{
	struct rtw88_softc sc;
	uint8_t data[24];
	const uint8_t *pkt;

	if (!start_ok(&sc))
		return;
	set_test_channel(&sc, 36);
	CHECK(rtw88_tx_raw(&sc, RTW88_TXQ_MGMT, bcast_frame,
	    sizeof(bcast_frame)) == 0);
	pkt = sc.sc_txq[RTW88_TXQ_MGMT].buf[0].vaddr;
	CHECK(pkt[16] == 4); /* DESC_RATE6M; CCK cannot be sent on 5 GHz. */
	CHECK(pkt[6] == 7); /* RTW_RATEID_G, legacy OFDM. */
	memcpy(data, bcast_frame, sizeof(data));
	data[0] = 0x08; /* Non-QoS data with a software sequence. */
	data[22] = 0x30;
	data[23] = 0x12;
	CHECK(rtw88_tx_raw(&sc, RTW88_TXQ_BE, data, sizeof(data)) == 0);
	pkt = sc.sc_txq[RTW88_TXQ_BE].buf[0].vaddr;
	CHECK(pkt[5] == 0); /* BE TID 0, not the management QSEL. */
	CHECK((pkt[33] & 0x80) == 0); /* Preserve software/CCMP sequence. */
	CHECK((pkt[37] >> 4 | (unsigned)pkt[38] << 4) == 0x123);
	set_test_channel(&sc, 1);
	CHECK(rtw88_tx_raw(&sc, RTW88_TXQ_MGMT, bcast_frame,
	    sizeof(bcast_frame)) == 0);
	pkt = sc.sc_txq[RTW88_TXQ_MGMT].buf[1].vaddr;
	CHECK(pkt[16] == 0 && pkt[6] == 8); /* 1M, B_20M. */
	rtw88_stop(&sc);
	rtw88_core_detach(&sc);
}

static void
test_tx_data_rate_diagnostics(void)
{
	struct rtw88_softc sc;
	uint8_t data[24];
	const uint8_t *pkt;
	unsigned i;

	if (!start_ok(&sc))
		return;
	memcpy(data, bcast_frame, sizeof(data));
	data[0] = 0x08;
	data[4] = 0x02; /* Unicast DATA, without retaining an address. */
	for (i = 0; i < 12; i++) {
		set_test_channel(&sc, i < 4 ? 1 : 36);
		sc.sc_basic_rates = 1U << i;
		CHECK(rtw88_tx_raw(&sc, RTW88_TXQ_BE, data, sizeof(data)) == 0);
		pkt = sc.sc_txq[RTW88_TXQ_BE].buf[i].vaddr;
		CHECK(pkt[16] == i);
		CHECK((pkt[13] & 0x05) == 0x05); /* USE_RATE and DIS_RATE_FB unchanged. */
		CHECK(sc.sc_stats.tx_data_encoded == i + 1);
		CHECK(sc.sc_stats.tx_data_rate == (pkt[16] & 0x7f));
		CHECK(sc.sc_stats.tx_data_basic == (1U << i));
		CHECK(sc.sc_stats.tx_data_rates[i] == 1);
	}
	CHECK(sc.sc_stats.tx_data_other == 0);
	CHECK(rtw88_tx_raw(&sc, RTW88_TXQ_BE, data, 23) == EMSGSIZE);
	CHECK(sc.sc_stats.tx_data_encoded == 12);
	data[4] |= 1; /* A multicast DATA encoding is not this sample. */
	CHECK(rtw88_tx_raw(&sc, RTW88_TXQ_BE, data, sizeof(data)) == 0);
	CHECK(sc.sc_stats.tx_data_encoded == 12);
	CHECK(rtw88_tx_raw(&sc, RTW88_TXQ_MGMT, bcast_frame,
	    sizeof(bcast_frame)) == 0);
	CHECK(sc.sc_stats.tx_data_encoded == 12);
	CHECK(sc.sc_stats.tx_data_rate == 11 && sc.sc_stats.tx_data_basic == 0x800);
	data[4] = 0x02;
	sc.sc_basic_rates = 0;
	set_test_channel(&sc, 36);
	CHECK(rtw88_tx_raw(&sc, RTW88_TXQ_BE, data, sizeof(data)) == 0);
	CHECK(sc.sc_stats.tx_data_encoded == 13 && sc.sc_stats.tx_data_rate == 4);
	CHECK(sc.sc_stats.tx_data_basic == 0 && sc.sc_stats.tx_data_rates[4] == 2);
	set_test_channel(&sc, 1);
	CHECK(rtw88_tx_raw(&sc, RTW88_TXQ_BE, data, sizeof(data)) == 0);
	CHECK(sc.sc_stats.tx_data_encoded == 14 && sc.sc_stats.tx_data_rate == 0);
	CHECK(sc.sc_stats.tx_data_rates[0] == 2);
	f.remove = 1; /* Encoding succeeds even if the later doorbell write fails. */
	CHECK(rtw88_tx_raw(&sc, RTW88_TXQ_BE, data, sizeof(data)) == ENXIO);
	CHECK(sc.sc_stats.tx_data_encoded == 15 && sc.sc_stats.tx_data_rates[0] == 3);
	f.remove = 0;
	rtw88_stop(&sc);
	CHECK(sc.sc_stats.tx_data_encoded == 15); /* Cumulative across stop. */
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

/* Wrong ring/doorbell/DOK or a shared producer corrupts independent ACs. */
static void
test_all_ac_producers(void)
{
	static const unsigned rings[5] = {5, 1, 0, 2, 3};
	static const unsigned nums[5] = {0x380, 0x388, 0x38a, 0x386, 0x384};
	static const unsigned idxs[5] = {0x3b0, 0x3a8, 0x3ac, 0x3a4, 0x3a0};
	static const unsigned dok[5] = {0x40, 0x10, 0x20, 0x08, 0x04};
	static const uint8_t tids[5] = {0, 3, 1, 5, 6};
	struct rtw88_softc sc;
	uint8_t frame[26] = {0x88, 0x02};
	unsigned q, i, writes;

	if (!start_ok(&sc))
		return;
	tx_completed = 0;
	sc.sc_net_cookie = &rx_delivered;
	sc.sc_tx_complete = network_tx;
	for (q = 0; q < 5; q++) {
		frame[0] = q == 0 ? 0xb0 : 0x88;
		frame[24] = tids[q];
		CHECK(reg16(nums[q]) == 32);
		CHECK(rtw88_tx_raw(&sc, q, frame, sizeof(frame)) == 0);
		/* Keep the old two-producer baseline runnable for causal RED. */
		if (q >= RTW88_NTXQ)
			continue;
		CHECK(sc.sc_txq[q].nbuf == 32);
		CHECK(sc.sc_txq[q].queued == 1 && sc.sc_txq[q].wp == 1);
		CHECK(reg16(idxs[q]) == 1);
		const uint8_t *pkt = sc.sc_txq[q].buf[0].vaddr;
		const uint8_t *bd = sc.sc_ring[rings[q]].vaddr;
		uint32_t addr = (uint32_t)bd[4] | (uint32_t)bd[5] << 8 |
		    (uint32_t)bd[6] << 16 | (uint32_t)bd[7] << 24;
		CHECK(addr == sc.sc_txq[q].buf[0].addr);
		CHECK(pkt[5] == (q == 0 ? 18 : tids[q]));
		CHECK(memcmp(pkt + 48, frame, sizeof(frame)) == 0);
		/* Completion for another AC must not reclaim this producer. */
		f.tx_hw_rp[q] = 1;
		set_hisr0(dok[(q + 1) % 5]);
		CHECK(rtw88_intr(&sc) == 1);
		CHECK(sc.sc_txq[q].queued == 1);
		set_hisr0(dok[q]);
		CHECK(rtw88_intr(&sc) == 1 && sc.sc_txq[q].queued == 0);
		for (i = 0; i < 31; i++)
			CHECK(rtw88_tx_raw(&sc, q, frame, sizeof(frame)) == 0);
		CHECK(sc.sc_txq[q].wp == 0 && sc.sc_txq[q].rp == 1);
		writes = f.writes;
		CHECK(rtw88_tx_raw(&sc, q, frame, sizeof(frame)) == ENOBUFS);
		CHECK(f.writes == writes && rtw88_tx_available(&sc, q) == 0);
		/* Past outstanding and out-of-range indices retain ownership. */
		f.tx_hw_rp[q] = 32;
		set_hisr0(dok[q]);
		CHECK(rtw88_intr(&sc) == 1 && sc.sc_txq[q].queued == 31);
		f.tx_hw_rp[q] = 0;
		set_hisr0(dok[q]);
		CHECK(rtw88_intr(&sc) == 1 && sc.sc_txq[q].queued == 0);
		CHECK(rtw88_tx_available(&sc, q) == 31);
		/* Leave a frame on every producer for simultaneous IRQ/stop. */
		CHECK(rtw88_tx_raw(&sc, q, frame, sizeof(frame)) == 0);
	}
	if (RTW88_NTXQ == 5) {
		/* One bad index cannot block reclaim of the other four queues. */
		for (q = 0; q < 5; q++)
			f.tx_hw_rp[q] = 1;
		f.tx_hw_rp[2] = 2;
		set_hisr0(0x7c);
		CHECK(rtw88_intr(&sc) == 1);
		for (q = 0; q < 5; q++)
			CHECK(sc.sc_txq[q].queued == (q == 2 ? 1U : 0U));
		CHECK(sc.sc_stats.tx_done == 164 && tx_completed == 164);
		CHECK(sc.sc_stats.tx_dma_errors == 6);
		CHECK(rtw88_stop(&sc) == 0 && sc.sc_stats.tx_dropped == 1);
		memset(f.tx_hw_rp, 0, sizeof(f.tx_hw_rp));
		CHECK(rtw88_start(&sc) == 0);
		for (q = 0; q < 5; q++) {
			CHECK(sc.sc_txq[q].wp == 0 && sc.sc_txq[q].rp == 0 &&
			    sc.sc_txq[q].queued == 0 && reg16(nums[q]) == 32);
			frame[0] = q == 0 ? 0xb0 : 0x88;
			frame[24] = tids[q];
			CHECK(rtw88_tx_raw(&sc, q, frame, sizeof(frame)) == 0);
		}
		CHECK(rtw88_stop(&sc) == 0 && sc.sc_stats.tx_dropped == 6);
	}
	CHECK(rtw88_core_detach(&sc) == 0 && f.dma_allocs == 0);
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
	CHECK(rtw88_tx_raw(&sc, RTW88_TXQ_MGMT, big, 1) == EMSGSIZE);
	CHECK(rtw88_tx_raw(&sc, RTW88_TXQ_MGMT, big, 23) == EMSGSIZE);
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
	/* rtlwifi LBMODE: normal, PHY, MAC delay, DMA, immediately. */
	CHECK(rtw88_loopback(&sc, 0x0b) == 0 && f.mmio[0x103] == 0x0b);
	CHECK(rtw88_loopback(&sc, 0x03) == 0 && f.mmio[0x103] == 0x03);
	CHECK(rtw88_loopback(&sc, 0x07) == 0 && f.mmio[0x103] == 0x07);
	CHECK(rtw88_loopback(&sc, 0x01) == 0 && f.mmio[0x103] == 0x01);
	CHECK(rtw88_loopback(&sc, 0x02) == EINVAL && f.mmio[0x103] == 0x01);
	CHECK(rtw88_loopback(&sc, 0) == 0 && f.mmio[0x103] == 0x00);
	rtw88_stop(&sc);
	CHECK(rtw88_loopback(&sc, 0x0b) == ENXIO);
	rtw88_core_detach(&sc);
}

/*
 * Reference rtw_phy_write_rf_reg_mix: RF register 0x00 (mode) goes
 * through the SIPI port, every other register through the direct map.
 */
static void
test_rf_mode_register_uses_sipi(void)
{
	static const uint32_t rows[] = {
		0x00000000, 0x00030000,
		0x00000018, 0x00012345
	};
	struct rtw8822c_table tbl = {
		.data = rows, .count = 4, .kind = RTW8822C_TBL_RF
	};
	struct rtw88_softc sc;
	unsigned path;

	for (path = 0; path < 2; path++) {
		uint32_t base = path == 0 ? 0x3c00 : 0x4c00;
		uint32_t sipi = path == 0 ? 0x1808 : 0x4108;

		reset_bus(&sc);
		f.mmio[base] = 0xef;
		f.mmio[base + 1] = 0xbe;
		f.mmio[base + 2] = 0xad;
		f.mmio[base + 3] = 0xde;
		tbl.rf_path = (int)path;
		CHECK(rtw8822c_apply_table(&sc, &tbl) == 0);
		CHECK(mmio32(sipi) == 0x00030000);
		CHECK(mmio32(base) == 0xdeadbeef);
		CHECK((mmio32(base + 0x18 * 4) & 0xfffff) == 0x12345);
	}
}

static void
test_phy_and_channel_at_start(void)
{
	struct rtw8822c_efuse map;
	struct rtw88_softc sc;
	uint32_t x;

	if (!start_ok(&sc))
		return;
	CHECK((f.mmio[0x1f] & 0x7) == 0x7);		/* RF_CTRL powered */
	CHECK((mmio32(0x1c3c) & 0x3) == 0x3);		/* ENCCK after tables */
	/* Pinned main.c: base hal.rcr plus default-ACK BIT_VHT_DACK26. */
	CHECK(mmio32(0x0608) == UINT32_C(0xf410400e));
	CHECK((mmio32(0x0608) & ~UINT32_C(0x04000000)) ==
	    UINT32_C(0xf010400e));
	CHECK((mmio32(0x0608) & UINT32_C(0x08000000)) == 0); /* No APP_BASSN. */
	CHECK((f.mmio[0x426] & 0x20) == 0); /* BA_PARSER_EN stays off. */
	CHECK((f.mmio[0x680] & 0x0c) == 0); /* No hardware crypto enable. */
	CHECK(f.mmio[0x66e] == 0x05); /* Existing BAR_ACK_TYPE. */
	CHECK(rtw8822c_efuse_decode(f.efuse, sizeof(f.efuse), &map) == 0);
	CHECK(sc.sc_xtal == map.xtal);
	x = sc.sc_xtal;
	CHECK(((mmio32(0x1040) >> 10) & 0x3fff) == (x | x << 7));
	CHECK(sc.sc_channel == RTW88_DEFAULT_CHANNEL);
	CHECK(f.mmio[0x3c60] == RTW88_DEFAULT_CHANNEL);	/* RF 0x18 path A */
	rtw88_stop(&sc);
	rtw88_core_detach(&sc);
}

/* Same default-ACK policy after restart; a failed final write must unwind. */
static void
test_default_ack_restart_and_failure(void)
{
	struct rtw88_softc sc;
	unsigned attempts;

	if (!start_ok(&sc))
		return;
	attempts = f.rcr_final_attempts;
	CHECK(attempts == 1);
	CHECK(rtw88_stop(&sc) == 0 && rtw88_start(&sc) == 0);
	CHECK(f.rcr_final_attempts == attempts + 1);
	CHECK(mmio32(0x608) == UINT32_C(0xf410400e));
	CHECK(rtw88_stop(&sc) == 0 && rtw88_core_detach(&sc) == 0);
	CHECK(f.dma_allocs == 0);

	reset_bus(&sc);
	if (!attach_ok(&sc))
		return;
	f.fail_rcr_final = 1;
	CHECK(rtw88_start(&sc) == EIO);
	CHECK(f.rcr_final_attempts == 1);
	CHECK(!sc.sc_started && !sc.sc_running && !sc.sc_bootstrapping);
	CHECK(!sc.sc_powered && !sc.sc_dma_master && !f.master);
	CHECK(!sc.sc_fw_dma_owned && !sc.sc_power_dirty && !sc.sc_power_error);
	CHECK(sc.sc_stop_requested && sc.sc_fw_dma.vaddr == NULL);
	f.fail_rcr_final = 0;
	CHECK(rtw88_start(&sc) == 0);
	CHECK(mmio32(0x608) == UINT32_C(0xf410400e));
	CHECK(rtw88_stop(&sc) == 0 && rtw88_core_detach(&sc) == 0);
	CHECK(f.dma_allocs == 0);
}

static void
test_channel_switch(void)
{
	struct rtw88_softc sc;

	if (!start_ok(&sc))
		return;
	CHECK(rtw88_channel(&sc, 6) == 0 && sc.sc_channel == 6);
	CHECK(f.mmio[0x3c60] == 6 && f.mmio[0x4c60] == 6);
	CHECK(rtw88_channel(&sc, 36) == 0 && f.mmio[0x3c60] == 36);
	CHECK(rtw88_channel(&sc, 15) == EINVAL && sc.sc_channel == 36);
	rtw88_stop(&sc);
	CHECK(rtw88_channel(&sc, 1) == ENXIO);
	rtw88_core_detach(&sc);
}

static void
test_rx_beacon(void)
{
	uint8_t bcn[24 + 12 + 2 + 4 + 4] = {
		0x80, 0x00, 0x00, 0x00,
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		0x02, 0x11, 0x22, 0x33, 0x44, 0x55,	/* SA */
		0x02, 0x11, 0x22, 0x33, 0x44, 0x55,	/* BSSID */
		0x10, 0x00,
	};
	uint8_t bad[sizeof(bcn)];
	struct rtw88_softc sc;

	memcpy(bcn + 36, "\x00\x04Test", 6);	/* SSID IE, then FCS */
	memcpy(bad, bcn, sizeof(bcn));
	bad[37] = 40;				/* SSID IE past the frame */
	if (!start_ok(&sc))
		return;
	rx_put(&sc, 0, sizeof(bcn), 0, bcn, 0);
	rx_put(&sc, 1, sizeof(bad), 0, bad, 1);
	f.rx_hw_idx = 2;
	set_hisr0(0x01);
	CHECK(rtw88_intr(&sc) == 1);
	CHECK(sc.sc_stats.rx_frames == 2 && sc.sc_stats.rx_beacons == 2);
	CHECK(memcmp(sc.sc_stats.last_bssid, bcn + 16, 6) == 0);
	/* The second beacon's SSID is malformed: no stale or partial name. */
	CHECK(sc.sc_stats.last_ssid_len == 0 && sc.sc_stats.last_ssid[0] == 0);
	rx_put(&sc, 2, sizeof(bcn), 0, bcn, 2);
	f.rx_hw_idx = 3;
	set_hisr0(0x01);
	CHECK(rtw88_intr(&sc) == 1);
	CHECK(sc.sc_stats.last_ssid_len == 4 &&
	    strcmp(sc.sc_stats.last_ssid, "Test") == 0);
	rtw88_stop(&sc);
	rtw88_core_detach(&sc);
}

/* Successful producer writes are separate from DMA ownership and air ACKs. */
static uint32_t posted_fail_idx;
static int posted_fail_after;

static int
posted_write_2(void *cookie, uint32_t reg, uint16_t value)
{
	int error;

	if (reg != posted_fail_idx)
		return write_2(cookie, reg, value);
	if (!posted_fail_after)
		return EIO;
	error = write_2(cookie, reg, value);
	return error != 0 ? error : EIO;
}

static void
test_tx_posted_diagnostics(void)
{
	static const uint32_t idx[5] = { 0x3b0, 0x3a8, 0x3ac, 0x3a4, 0x3a0 };
	static const uint32_t dok[5] = { 0x40, 0x10, 0x20, 0x08, 0x04 };
	static const uint8_t tids[5] = { 0, 0, 1, 5, 6 };
	unsigned q, i, after;

	for (q = 0; q < RTW88_NTXQ; q++) {
		struct rtw88_softc sc;
		uint8_t frame[26] = { 0x88, 1 };

		if (!start_ok(&sc))
			return;
		frame[0] = q == RTW88_TXQ_MGMT ? 0xb0 : 0x88;
		frame[24] = tids[q];
		CHECK(rtw88_tx_raw(&sc, q, frame, sizeof(frame)) == 0);
		for (i = 0; i < RTW88_NTXQ; i++)
			CHECK(sc.sc_stats.tx_posted[i] == (i == q ? 1U : 0U));
		while (rtw88_tx_available(&sc, q) != 0)
			CHECK(rtw88_tx_raw(&sc, q, frame, sizeof(frame)) == 0);
		CHECK(sc.sc_stats.tx_posted[q] == 31);
		CHECK(rtw88_tx_raw(&sc, q, frame, sizeof(frame)) == ENOBUFS);
		CHECK(sc.sc_stats.tx_posted[q] == 31);
		CHECK(rtw88_tx_raw(&sc, RTW88_NTXQ, frame, sizeof(frame)) == EINVAL);
		CHECK(sc.sc_stats.tx_posted[q] == 31);
		f.tx_hw_rp[q] = sc.sc_txq[q].wp;
		set_hisr0(dok[q]);
		CHECK(rtw88_intr(&sc) == 1 && sc.sc_txq[q].queued == 0);
		CHECK(sc.sc_stats.tx_posted[q] == 31);
		CHECK(rtw88_stop(&sc) == 0 && rtw88_start(&sc) == 0);
		CHECK(sc.sc_stats.tx_posted[q] == 31);
		CHECK(rtw88_tx_raw(&sc, q, frame, sizeof(frame)) == 0);
		CHECK(sc.sc_stats.tx_posted[q] == 32);
		CHECK(rtw88_stop(&sc) == 0 && rtw88_core_detach(&sc) == 0);
		for (after = 0; after < 2; after++) {
			struct rtw88_bus_ops fault = ops;

			if (!start_ok(&sc))
				return;
			posted_fail_idx = idx[q];
			posted_fail_after = (int)after;
			fault.write_2 = posted_write_2;
			sc.sc_ops = &fault;
			CHECK(rtw88_tx_raw(&sc, q, frame, sizeof(frame)) == EIO);
			CHECK(sc.sc_stats.tx_posted[q] == 0);
			/* An uncertain doorbell keeps the existing DMA ownership. */
			CHECK(sc.sc_txq[q].queued == 1 && sc.sc_stats.tx_queued == 1);
			sc.sc_ops = &ops;
			CHECK(rtw88_stop(&sc) == 0 && rtw88_core_detach(&sc) == 0);
		}
	}
}

/* The fixed-rate caller has its own stack layout. Its explicit core-only
 * reuse excludes this state fixture. The normal start runner keeps it. */
/* Independent failures of each quiet control must leave host TX fenced. */
static unsigned quiet_fault, quiet_pause_writes, quiet_cr_writes, quiet_cr_reads;
static int
quiet_write1(void *cookie, uint32_t reg, uint8_t value)
{
	if (reg == 0x0522 || reg == 0x0100) {
		CHECK(!f.sc->sc_phy.channel_valid);
        CHECK(f.sc->sc_stop_requested && !f.sc->sc_ra.configured &&
		    !f.sc->sc_ra.active);
		if (reg == 0x0522) {
			quiet_pause_writes++;
			if (quiet_fault == 1) return EIO;
			if (quiet_fault == 10) return ETIMEDOUT;
		} else {
			quiet_cr_writes++;
			if (quiet_fault == 4) return EIO;
		}
	}
	int error = write_1(cookie, reg, value);
	if ((reg == 0x0522 && quiet_fault == 2) ||
	    (reg == 0x0100 && quiet_fault == 5)) return EIO;
	return error;
}
static int
quiet_read1(void *cookie, uint32_t reg, uint8_t *value)
{
	if (reg == 0x0522 && quiet_fault == 8) return ENXIO;
	if (reg == 0x0100) {
		quiet_cr_reads++;
		if (quiet_fault == 3) return EIO;
		if (quiet_cr_reads > 1 && (quiet_fault == 9 || quiet_fault == 10))
			return EINVAL;
	}
	int error = read_1(cookie, reg, value);
	if (error == 0 && reg == 0x0522 && quiet_fault == 6) *value = 0;
	if (error == 0 && reg == 0x0100 && quiet_fault == 7 &&
	    quiet_cr_reads > 1) *value |= 0x40;
	return error;
}
static void
test_tx_quiet_fence(void)
{
	struct rtw88_softc sc;
	reset_bus(&sc);
	CHECK(rtw88_tx_quiet(&sc) == ENXIO && sc.sc_stop_requested);
	CHECK(f.writes == 0 && f.delays == 0);
	for (unsigned fault = 0; fault < 11; fault++) {
		if (!start_ok(&sc)) return;
		struct rtw88_bus_ops spy = ops;
		spy.read_1 = quiet_read1; spy.write_1 = quiet_write1;
		sc.sc_ops = &spy;
		sc.sc_ra.configured = sc.sc_ra.active = 1;
		f.mmio[0x0100] = 0xff; f.mmio[0x0522] = 0;
		quiet_fault = fault;
		quiet_pause_writes = quiet_cr_writes = quiet_cr_reads = 0;
		unsigned delays = f.delays, syncs = f.syncs;
		int expected = fault == 10 ? ETIMEDOUT : fault == 9 ? EINVAL :
		    fault == 8 ? ENXIO : fault ? EIO : 0;
		CHECK(rtw88_tx_quiet(&sc) == expected);
		CHECK(sc.sc_stop_requested && !sc.sc_ra.configured && !sc.sc_ra.active);
		CHECK(sc.sc_running && sc.sc_started && sc.sc_dma_master);
		CHECK(f.delays == delays && f.syncs == syncs);
		CHECK(quiet_pause_writes == 1 && quiet_cr_writes == (fault == 3 ? 0U : 1U));
		if (fault == 0) CHECK(f.mmio[0x0522] == 0xff && f.mmio[0x0100] == 0xbf);
		if (fault == 1 || fault == 2) CHECK((f.mmio[0x0100] & 0x40) == 0);
		if (fault == 3 || fault == 4) CHECK(f.mmio[0x0522] == 0xff);
		uint8_t frame[24] = {0x80};
		unsigned writes = f.writes;
		for (unsigned q = 0; q < RTW88_NTXQ; q++) {
			unsigned wp = sc.sc_txq[q].wp;
			CHECK(rtw88_tx_available(&sc, q) == 0);
			CHECK(rtw88_tx_raw(&sc, q, frame, sizeof(frame)) == ENXIO);
			CHECK(sc.sc_txq[q].wp == wp);
		}
		CHECK(rtw88_channel(&sc, 36) == ENXIO);
		CHECK(rtw88_loopback(&sc, 0) == ENXIO);
		CHECK(rtw88_station_config(&sc, NULL, 0, 0x0f, 20, 0) == ENXIO);
		CHECK(rtw88_fw_command(&sc, 1, NULL, 0) == ENXIO);
		CHECK(f.writes == writes && f.syncs == syncs);
		sc.sc_ops = &ops; quiet_fault = 0;
		CHECK(rtw88_stop(&sc) == 0);
		CHECK(rtw88_start(&sc) == 0 && !sc.sc_stop_requested);
		rtw88_core_detach(&sc);
	}
}

#ifndef RTW88_START_CORE_ONLY
#include "rtw88-state-test.h"
#endif

int
main(void)
{

	test_tx_quiet_fence();
	test_cold_dack_startup();
	test_radio_dependencies();
	test_power_system_integration();
	test_power_setup_unwind();
	test_shutdown_failure_retention();
	test_master_failure_retention();
	test_missing_firmware();
	test_corrupt_firmware();
	test_failed_identity_read();
	test_failed_efuse_read();
	test_malformed_efuse();
	test_unsupported_identity();
	test_command_length();
	test_command_timeout();
	test_command_after_restart();
	test_packet_epoch_recovery();
	test_beacon_never_valid();
	test_firmware_checksum_bad();
	test_hci_start_masks();
	test_network_delivery();
	test_rx_c2h_and_frame();
	test_rx_malformed_and_short();
	test_rx_limit_and_wrap();
	test_rx_completed_frontier();
	test_rx_sequence_rejections();
	test_rx_sequence_wrap_restart();
	test_rx_frontier_errors();
	test_rx_sequence_exhaustion();
	test_rx_bad_hw_index();
	test_rx_duplicate_interrupt();
	test_rx_tag_mismatch();
	test_intr_not_ours();
	test_intr_after_stop();
	test_rx_buffer_alloc_failure();
	test_tx_buffer_alloc_failure();
	test_diag_regs();
	test_tx_descriptors_and_full_ring();
	test_station_config();
	test_tx_band_and_sequence();
	test_tx_data_rate_diagnostics();
	test_tx_completion();
	test_tx_refusals();
	test_all_ac_producers();
	test_tx_posted_diagnostics();
	test_edca_quarantine_recovery();
#ifndef RTW88_START_CORE_ONLY
	test_native_state_defaults();
	test_native_state_edca_failures();
	test_native_state_negotiated();
	test_native_state_reject_context();
	test_native_state_fixed_clear();
	test_native_state_epoch();
    test_native_state_retune_failure();
	test_native_state_width_restart();
	test_native_band_media();
	test_native_band_invalid_and_fault();
#endif
	test_loopback_mode();
	test_rf_mode_register_uses_sipi();
	test_phy_and_channel_at_start();
	test_default_ack_restart_and_failure();
	test_channel_switch();
	test_rx_beacon();
	test_stop_during_bootstrap();
	test_stale_generation();
	test_success_consumes_tables();
#ifdef RTW88_START_CORE_ONLY
	printf("rtw88 core-only start fixture: %u assertions, %u failures\n",
#else
	printf("rtw88 start contract: %u assertions, %u failures\n",
#endif
	    assertions, failures);
	return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
