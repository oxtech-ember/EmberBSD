/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: FreeBSD a143197801c680b958ab662c88cf086668a1958b rtw88 PHY. */
/*
 * Native adaptation of rtw8822c_phy_set_param (around the table load),
 * rtw_phy_init, rtw_bf_phy_init and rtw8822c_set_channel. Tuple geometry
 * supports 20/40/80 MHz; live admission remains 20 MHz in the current core.
 * ember/tools/rtw8822c-phy.trace holds the reference register writes.
 * Cold ADC/DAC calibration runs before x2, trims and PA bias. Failed
 * measurements stop startup; the caller performs a complete power off.
 * Trims and PA bias come from physical EFUSE. TX power is set separately.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <dev/ic/rtw88var.h>
#include <dev/ic/rtw8822c.h>
#include <dev/ic/rtw88_io.h>

#define rd(io, a, n)		rtw88_io_rd(io, a, n)
#define w8(io, a, v)		rtw88_io_wr(io, a, 1, v)
#define w16(io, a, v)		rtw88_io_wr(io, a, 2, v)
#define w32(io, a, v)		rtw88_io_wr(io, a, 4, v)
#define set32(io, a, b)		rtw88_io_set(io, a, 4, b)
#define clr32(io, a, b)		rtw88_io_clr(io, a, 4, b)
#define mask32(io, a, m, v)	rtw88_io_wr_mask(io, a, m, v)

#define RFREG_MASK		0xfffffU
#define RF_CFGCH		0x18
#define RF_LUTWA		0x33
#define RF_LUTWD0		0x3f
#define RF_LUTWE2		0xee
#define BB_PATH_A		1
#define BB_PATH_AB		3

static const uint32_t rf_base[2] = { 0x3c00, 0x4c00 };
static const uint32_t rf_sipi[2] = { 0x1808, 0x4108 };

/* rtw_phy_read_rf: direct map. */
static uint32_t
read_rf(struct rtw88_io *io, unsigned path, uint32_t addr, uint32_t mask)
{

	return rtw88_io_rd_mask(io, rf_base[path] + ((addr & 0xff) << 2),
	    mask & RFREG_MASK);
}

/* rtw_phy_write_rf_reg_mix: register 0x00 through SIPI, others direct. */
static void
write_rf(struct rtw88_io *io, unsigned path, uint32_t addr, uint32_t mask,
    uint32_t data)
{

	addr &= 0xff;
	mask &= RFREG_MASK;
	if (addr != 0) {
		mask32(io, rf_base[path] + (addr << 2), mask, data);
		rtw88_io_delay(io, 1);
		return;
	}
	if (mask != RFREG_MASK)
		data = (read_rf(io, path, 0, RFREG_MASK) & ~mask) |
		    (data << __builtin_ctz(mask));
	w32(io, rf_sipi[path], (data & RFREG_MASK) & UINT32_C(0x0fffffff));
	rtw88_io_delay(io, 13);
}

static void
header_file_init(struct rtw88_io *io, int pre)
{

	set32(io, 0x180c, 0x3);			/* 3WIRE: TX_EN, RX_EN */
	set32(io, 0x180c, UINT32_C(0x10000000));	/* 3WIRE: PI_ON */
	set32(io, 0x410c, 0x3);			/* 3WIRE2 */
	set32(io, 0x410c, UINT32_C(0x10000000));
	if (pre)
		clr32(io, 0x1c3c, 0x3);		/* ENCCK: CCK_OFDM_BLK_EN */
	else
		set32(io, 0x1c3c, 0x3);
}

static void
bb_reset(struct rtw88_io *io)
{

	rtw88_io_set(io, 0x0002, 2, 0x1);	/* SYS_FUNC_EN: FEN_BB_RSTB */
	rtw88_io_clr(io, 0x0002, 2, 0x1);
	rtw88_io_set(io, 0x0002, 2, 0x1);
}

static void
toggle_igi(struct rtw88_io *io)
{
	uint32_t igi = rtw88_io_rd_mask(io, 0x1d70, 0x7f);	/* RXIGI */

	mask32(io, 0x1d70, 0x7f, igi - 2);
	mask32(io, 0x1d70, 0x7f00, igi - 2);
	mask32(io, 0x1d70, 0x7f, igi);
	mask32(io, 0x1d70, 0x7f00, igi);
}

/* rtw8822c_config_trx_mode(BB_PATH_AB, BB_PATH_AB, false). */
static void
config_trx_ab(struct rtw88_io *io)
{

	mask32(io, 0x1800, 0xfffff, 0x33312);	/* ORITXCODE */
	mask32(io, 0x4100, 0xfffff, 0x33312);	/* ORITXCODE2 */
	/* CCK RX on A and B. */
	mask32(io, 0x1a2c, 0x00600000, 0x1);	/* CCANRX */
	mask32(io, 0x1a2c, 0x00060000, 0x1);
	mask32(io, 0x1a04, 0x0f000000, 0x1);	/* RXCCKSEL */
	/* OFDM RX on A and B. */
	mask32(io, 0x1d30, 0x300, 0x1);		/* RXFNCTL */
	mask32(io, 0x1d30, 0x600000, 0x1);
	mask32(io, 0x0c44, 1U << 17, 0x1);	/* AGCSWSH */
	mask32(io, 0x0c54, 1U << 20, 0x1);	/* ANTWTPD */
	mask32(io, 0x0c38, 1U << 24, 0x1);	/* MRCM */
	mask32(io, 0x0824, 0x0f000000, BB_PATH_AB);
	mask32(io, 0x0824, 0x000f0000, BB_PATH_AB);
	/* TX: OFDM on AB with 1SS on A, CCK on A. */
	mask32(io, 0x1a04, 0xf0000000, 0x8);	/* RXCCKSEL: CCK TX A */
	bb_reset(io);
	mask32(io, 0x0820, 0xff, 0x31);		/* ANTMAP0 */
	mask32(io, 0x1e2c, 0xffff, 0x0400);	/* TXLGMAP */
	bb_reset(io);
	bb_reset(io);
	toggle_igi(io);
}

int
rtw8822c_phy_pre(struct rtw88_softc *sc)
{
	struct rtw88_io io;

	rtw88_io_init(&io, sc->sc_ops, sc->sc_bus);
	/* Power on the BB and RF domains. */
	rtw88_io_set(&io, 0x0002, 1, 0x3);	/* SYS_FUNC_EN: BB_GLB_RST, RSTB */
	rtw88_io_set(&io, 0x001f, 1, 0x7);	/* RF_CTRL: EN, RSTB, SDM_RSTB */
	set32(&io, 0x00ec, UINT32_C(0x07000000));	/* WLRF1: BBRF_EN */
	mask32(&io, 0x0a70, 0x3ff, 0x3ff);	/* DIS_DPD: all rates */
	header_file_init(&io, 1);
	return io.error;
}

/* rtw8822c_rf_x2_check: restart the X2 clock if the RF reports busy. */
static void
rf_x2_check(struct rtw88_io *io)
{

	rtw88_io_delay(io, 1000);
	if (read_rf(io, 0, 0xb8, 1U << 15) == 1) {
		write_rf(io, 0, 0xb8, RFREG_MASK, 0xc4440);
		write_rf(io, 0, 0xba, RFREG_MASK, 0x6840d);
		write_rf(io, 0, 0xb8, RFREG_MASK, 0x80440);
		rtw88_io_delay(io, 1000);
	}
}

/* rtw8822c_thermal_trim: EFUSE bit 0 moves to bit 3, bits 3:1 shift down. */
static void
thermal_trim(struct rtw88_io *io, const uint8_t *efuse)
{
	static const uint16_t addr[2] = { 0x1ef, 0x1b0 };	/* PPG_THERMAL */
	unsigned path;

	for (path = 0; path < 2; path++) {
		uint8_t pg = efuse[addr[path]];

		if (pg == 0xff)
			return;
		write_rf(io, path, 0x43, 0xf0000,
		    ((pg >> 1) & 0x7) | ((pg & 0x1) << 3));
	}
}

/* rtw8822c_power_trim and rtw8822c_set_power_trim. */
static void
power_trim(struct rtw88_io *io, const uint8_t *efuse)
{
	static const uint16_t ppg_2g[3] = { 0x1d4, 0x1ee, 0x1d2 };
	static const uint16_t ppg_5g[2][5] = {
		{ 0x1ec, 0x1e8, 0x1e4, 0x1e0, 0x1dc },	/* path A: L1..H1 */
		{ 0x1eb, 0x1e7, 0x1e3, 0x1df, 0x1db },	/* path B */
	};
	/* RF LUT sequence 0..0xe to bb_gain index. */
	static const uint8_t idx[15] = {
		0, 1, 2, 2, 3, 4, 5, 6, 7, 3, 4, 5, 6, 7, 7
	};
	uint8_t gain[2][8] = { { 0 } };
	unsigned i, path;
	int set = 0;

	for (i = 0; i < 3; i++) {
		uint8_t pg = efuse[ppg_2g[i]];

		if (pg == 0xff)
			continue;
		set = 1;
		gain[0][i] = pg & 0x0f;
		gain[1][i] = (pg >> 4) & 0x0f;
	}
	for (i = 0; i < 5; i++) {
		for (path = 0; path < 2; path++) {
			uint8_t pg = efuse[ppg_5g[path][i]];

			if (pg == 0xff)
				continue;
			set = 1;
			gain[path][i + 3] = pg & 0x1f;
		}
	}
	if (set) {
		for (path = 0; path < 2; path++) {
			write_rf(io, path, 0xee, 1U << 19, 1);
			for (i = 0; i < 15; i++) {
				write_rf(io, path, 0x33, RFREG_MASK, i);
				write_rf(io, path, 0x3f, RFREG_MASK,
				    gain[path][idx[i]]);
			}
			write_rf(io, path, 0xee, 1U << 19, 0);
		}
	}
	mask32(io, 0x0a70, 0x3ff, 0x3ff);	/* DIS_DPD: all rates */
}

/* rtw8822c_pa_bias: 2.4 GHz stops at the first blank byte, 5 GHz not. */
static void
pa_bias(struct rtw88_io *io, const uint8_t *efuse)
{
	static const uint16_t ppg_2g[2] = { 0x1d6, 0x1d5 };
	static const uint16_t ppg_5g[2] = { 0x1d8, 0x1d7 };
	unsigned path;

	for (path = 0; path < 2; path++) {
		uint8_t pg = efuse[ppg_2g[path]];

		if (pg == 0xff)
			return;
		write_rf(io, path, 0x60, 0xf000, pg & 0x0f);	/* RF_PA 2G */
	}
	for (path = 0; path < 2; path++)
		write_rf(io, path, 0x60, 0xf0000,
		    efuse[ppg_5g[path]] & 0x0f);		/* RF_PA 5G */
}

int
rtw8822c_phy_post(struct rtw88_softc *sc)
{
	struct rtw88_io io;
	struct rtw8822c_dack_result dack;
	uint32_t v, cap = sc->sc_xtal & 0x7f;
	int error;

	rtw88_io_init(&io, sc->sc_ops, sc->sc_bus);
	mask32(&io, 0x1040, 0xfffc00, cap | (cap << 7));	/* ANAPAR_XTAL_0 */
	header_file_init(&io, 0);
	config_trx_ab(&io);
	/* rtw_phy_init: rtw8822c_adaptivity_init, EDCCA thresholds max. */
	mask32(&io, 0x084c, 0x00ff0000, 0x7f + 0x80);
	mask32(&io, 0x084c, 0xff000000, 0x7f + 0x80);
	clr32(&io, 0x0520, 0x8000);		/* TX_PTCL_CTRL: DIS_EDCCA */
	set32(&io, 0x0524, 0x800);		/* RD_CTRL: EDCCA_MSK_CNTDOWN */
	clr32(&io, 0x0844, UINT32_C(0x60000000));	/* EDCCA_DECISION */
	if (io.error != 0)
		return io.error;
	/* rtw8822c_rf_init: measured cold ADCK/DACK before any HCI start. */
	error = rtw8822c_dack(sc, &dack);
	printf("rtwce: cold ADCK/DACK error %d measured %#x/%#x "
	    "converged %#x/%#x completed %#x restored %u reset %u "
	    "ADC A %d/%d B %d/%d DAC A %d/%d B %d/%d "
	    "ops %u explicit_delay_us %u\n", error, (unsigned)dack.adc_measured,
	    (unsigned)dack.dac_measured, (unsigned)dack.adc_converged,
	    (unsigned)dack.dac_converged, (unsigned)dack.completed_paths,
	    (unsigned)dack.restored,
	    (unsigned)dack.reset_required, dack.adc_residual[0][0],
	    dack.adc_residual[0][1], dack.adc_residual[1][0],
	    dack.adc_residual[1][1], dack.dac_residual[0][0],
	    dack.dac_residual[0][1], dack.dac_residual[1][0],
	    dack.dac_residual[1][1], dack.operations, dack.delay_us);
	if (dack.adc_nonconverged != 0 || dack.dac_nonconverged != 0)
		printf("rtwce: WARNING: cold calibration degraded: "
		    "ADC nonconverged %#x DAC nonconverged %#x; "
		    "RF quality is unqualified\n", (unsigned)dack.adc_nonconverged,
		    (unsigned)dack.dac_nonconverged);
	if (error != 0) {
		printf("rtwce: DACK identity syscfg %#x cut %u RFE %u XTAL %u\n",
		    sc->sc_sys_cfg1, (unsigned)sc->sc_cut, (unsigned)sc->sc_rfe,
		    (unsigned)sc->sc_xtal);
		printf("rtwce: DACK stage %u path %u ADC attempts %u/%u "
		    "DAC attempts %u/%u measure %u/%u samples %u/%u "
		    "filter %u spread %u/%u "
		    "mean %d/%d valid %u\n", (unsigned)dack.stage,
		    (unsigned)dack.path, (unsigned)dack.adc_attempts[0],
		    (unsigned)dack.adc_attempts[1], (unsigned)dack.dac_attempts[0],
		    (unsigned)dack.dac_attempts[1], (unsigned)dack.sample_stage,
		    (unsigned)dack.sample_path, (unsigned)dack.sample_count,
		    (unsigned)dack.sample_reads, (unsigned)dack.filter_passes,
		    (unsigned)dack.spread[0], (unsigned)dack.spread[1],
		    dack.mean[0], dack.mean[1], (unsigned)dack.mean_valid);
		printf("rtwce: DACK ADC before A %d/%d B %d/%d "
		    "comp A %u/%u B %u/%u\n", dack.adc_before[0][0],
		    dack.adc_before[0][1], dack.adc_before[1][0],
		    dack.adc_before[1][1], (unsigned)dack.adc[0][0],
		    (unsigned)dack.adc[0][1], (unsigned)dack.adc[1][0],
		    (unsigned)dack.adc[1][1]);
		printf("rtwce: DACK ADC snapshot path %u valid %u error %d "
		    "comp %#x mux %#x sample %#x AFE %#x control %#x\n",
		    (unsigned)dack.adc_snapshot_path,
		    (unsigned)dack.adc_snapshot_valid, dack.adc_snapshot_error,
		    dack.adc_snapshot[0], dack.adc_snapshot[1],
		    dack.adc_snapshot[2], dack.adc_snapshot[3],
		    dack.adc_snapshot[4]);
		return error;
	}
	rf_x2_check(&io);
	thermal_trim(&io, sc->sc_efuse_phys);
	power_trim(&io, sc->sc_efuse_phys);
	pa_bias(&io, sc->sc_efuse_phys);
	/* rtw_bf_phy_init. */
	v = rd(&io, 0x14c0, 4);			/* MU_TX_CTL */
	v |= 0x10000;				/* MU_P1_WAIT_STATE_EN */
	v = (v & ~0xf000U) | (0xaU << 12);	/* MU retry limit 10 */
	v &= ~(0x80U | 0x3fU);			/* MU-MIMO off, no STAs */
	w32(&io, 0x14c0, v);
	w8(&io, 0x167c, 3 << 4 | 0x40);		/* WMAC_MU_BF_OPTION */
	w16(&io, 0x1680, 0);			/* WMAC_MU_BF_CTL */
	set32(&io, 0x042c, UINT32_C(0x40000000));	/* TXBF_CTRL: NDPA param */
	w8(&io, 0x045f, 0x10);			/* NDPA_OPT_CTRL: rate */
	mask32(&io, 0x06dc, 0x3f000000, 0x04);	/* BBPSF_CTRL: CSI 6M */
	return io.error;
}

static int
channel_band(unsigned ch)
{

	if (ch >= 1 && ch <= 14)
		return 2;
	if ((ch >= 36 && ch <= 48) || (ch >= 52 && ch <= 64) ||
	    (ch >= 100 && ch <= 144) || (ch >= 149 && ch <= 177))
		return 5;
	return 0;
}

static int
channel_constituent(unsigned ch)
{
	if ((ch >= 1 && ch <= 14) ||
	    (ch >= 36 && ch <= 64 && ch % 4 == 0) ||
	    (ch >= 100 && ch <= 144 && ch % 4 == 0) ||
	    (ch >= 149 && ch <= 177 && (ch - 149) % 4 == 0))
		return 1;
	return 0;
}

/* Validate the complete primary/center relationship before any bus access. */
int
rtw8822c_channel_geometry(const struct rtw88_channel_tuple *tuple,
    struct rtw8822c_channel_geometry *out)
{
	struct rtw8822c_channel_geometry g = { 0 };
	int delta;
	unsigned i, ch;

	if (tuple == NULL || out == NULL ||
	    !channel_constituent(tuple->primary))
		return EINVAL;
	g.band = channel_band(tuple->primary);
	if (g.band == 0 || channel_band(tuple->primary) !=
	    channel_band(tuple->center))
		return EINVAL;
	g.cch20 = tuple->primary;
	switch (tuple->width_mhz) {
	case 20:
		if (tuple->center != tuple->primary)
			return EINVAL;
		g.cch40 = g.cch80 = tuple->center;
		g.channel_index = 0;
		g.data_sc = 0;
		break;
	case 40:
		delta = (int)tuple->primary - (int)tuple->center;
		if (g.band != 5 || (delta != -2 && delta != 2))
			return EINVAL;
		g.cch40 = tuple->center;
		g.cch80 = tuple->center;
		g.channel_index = delta > 0 ? 1 : 2;
		g.data_sc = g.channel_index;
		break;
	case 80:
		delta = (int)tuple->primary - (int)tuple->center;
		if (g.band != 5 || (delta != -6 && delta != -2 &&
		    delta != 2 && delta != 6))
			return EINVAL;
		g.channel_index = delta == -6 ? 4 : delta == -2 ? 2 :
		    delta == 2 ? 1 : 3;
		g.cch40 = (unsigned)((int)tuple->center + (delta < 0 ? -4 : 4));
		g.cch80 = tuple->center;
		g.data_sc = g.channel_index |
		    ((g.channel_index == 1 || g.channel_index == 3 ?
		    9U : 10U) << 4);
		break;
	default:
		return EINVAL;
	}
	if (tuple->width_mhz == 40) {
		for (i = 0; i < 2; i++) {
			ch = (unsigned)((int)tuple->center + (i == 0 ? -2 : 2));
			if (!channel_constituent(ch))
				return EINVAL;
		}
	} else if (tuple->width_mhz == 80) {
		if (tuple->center != 42 && tuple->center != 58 &&
		    tuple->center != 106 && tuple->center != 122 &&
		    tuple->center != 138 && tuple->center != 155)
			return EINVAL;
		for (i = 0; i < 4; i++) {
			ch = (unsigned)((int)tuple->center + (i == 0 ? -6 :
			    i == 1 ? -2 : i == 2 ? 2 : 6));
			if (!channel_constituent(ch))
				return EINVAL;
		}
	}
	*out = g;
	return 0;
}

static void
channel_bb(struct rtw88_io *io, const struct rtw88_channel_tuple *tuple,
    const struct rtw8822c_channel_geometry *g)
{
	unsigned ch = tuple->center;

	if (channel_band(ch) == 2) {
		clr32(io, 0x1a14, 0x300);	/* BGCTRL: RX_IQ_WEIGHT */
		set32(io, 0x1a9c, 1U << 20);	/* TXF4 */
		clr32(io, 0x0454, 0x80);	/* CCK_CHECK */
		clr32(io, 0x1a80, 0x40000);	/* CCKTXONLY */
		mask32(io, 0x1c80, 0x3f000000, 0xf);	/* CCAMSK */
		mask32(io, 0x18ac, 0xf000, 0x5);	/* RXAGCCTL0 CCK */
		mask32(io, 0x41ac, 0xf000, 0x5);
		mask32(io, 0x18ac, 0x1f0, 0x6);		/* OFDM */
		mask32(io, 0x41ac, 0x1f0, 0x6);
		if (ch == 13 || ch == 14)
			mask32(io, 0x0c30, 0xfff, 0x969);	/* SCOTRK */
		else if (ch == 11 || ch == 12)
			mask32(io, 0x0c30, 0xfff, 0x96a);
		else
			mask32(io, 0x0c30, 0xfff, 0x9aa);
		if (ch == 14) {
			mask32(io, 0x1a20, 0xffff0000, 0x3da0);	/* TXF0..7 */
			mask32(io, 0x1a24, 0xffffffff, 0x4962c931);
			mask32(io, 0x1a28, 0x0000ffff, 0x6aa3);
			mask32(io, 0x1a98, 0xffff0000, 0xaa7b);
			mask32(io, 0x1a9c, 0x0000ffff, 0xf3d7);
			mask32(io, 0x1aa0, 0xffffffff, 0x0);
			mask32(io, 0x1aac, 0xffffffff, 0xff012455);
			mask32(io, 0x1ab0, 0xffffffff, 0xffff);
		} else {
			mask32(io, 0x1a20, 0xffff0000, 0x5284);
			mask32(io, 0x1a24, 0xffffffff, 0x3e18fec8);
			mask32(io, 0x1a28, 0x0000ffff, 0x0a88);
			mask32(io, 0x1a98, 0xffff0000, 0xacc4);
			mask32(io, 0x1a9c, 0x0000ffff, 0xc8b2);
			mask32(io, 0x1aa0, 0xffffffff, 0x00faf0de);
			mask32(io, 0x1aac, 0xffffffff, 0x00122344);
			mask32(io, 0x1ab0, 0xffffffff, 0x0fffffff);
		}
		mask32(io, 0x0808, 0x70, ch == 13 ? 0x3 : 0x1);	/* TXDFIR0 */
	} else {
		uint32_t agc;

		set32(io, 0x1a80, 0x40000);
		set32(io, 0x0454, 0x80);
		set32(io, 0x1a14, 0x300);
		clr32(io, 0x1a9c, 1U << 20);
		mask32(io, 0x1c80, 0x3f000000, 0x22);
		mask32(io, 0x0808, 0x70, 0x3);
		agc = ch <= 64 ? 0x1 : ch <= 144 ? 0x2 : 0x3;
		mask32(io, 0x18ac, 0x1f0, agc);
		mask32(io, 0x41ac, 0x1f0, agc);
		if (ch <= 51)
			mask32(io, 0x0c30, 0xfff, 0x494);
		else if (ch <= 55)
			mask32(io, 0x0c30, 0xfff, 0x493);
		else if (ch <= 111)
			mask32(io, 0x0c30, 0xfff, 0x453);
		else if (ch <= 119)
			mask32(io, 0x0c30, 0xfff, 0x452);
		else if (ch <= 172)
			mask32(io, 0x0c30, 0xfff, 0x412);
		else
			mask32(io, 0x0c30, 0xfff, 0x411);
	}
	switch (tuple->width_mhz) {
	case 20:
		mask32(io, 0x0810, 0x3ff0, 0x19b);	/* DFIRBW */
		mask32(io, 0x09b0, 0xf, 0x0);	/* TXBWCTL */
		mask32(io, 0x09b0, 0xffc0, 0x0);
		mask32(io, 0x09b4, 0x700, 0x7);	/* TXCLK */
		mask32(io, 0x09b4, 0x700000, 0x6);
		mask32(io, 0x1abc, UINT32_C(0x40000000), 0x0);
		mask32(io, 0x088c, 0xf000, 0x1);
		mask32(io, 0x0cbc, 0x200000, 0x0);
		break;
	case 40:
		mask32(io, 0x1a00, 0x10,
		    g->channel_index == 1 ? 1 : 0); /* CCKSB */
		mask32(io, 0x09b0, 0xf, 0x5);
		mask32(io, 0x09b0, 0xc0, 0x0);
		mask32(io, 0x09b0, 0xff00,
		    g->channel_index | (g->channel_index << 4));
		mask32(io, 0x1abc, UINT32_C(0x40000000), 0x1);
		mask32(io, 0x088c, 0xf000, 0x1);
		mask32(io, 0x0cbc, 0x200000, 0x1);
		break;
	case 80:
		mask32(io, 0x09b0, 0xf, 0xa);
		mask32(io, 0x09b0, 0xc0, 0x0);
		mask32(io, 0x09b0, 0xff00,
		    g->channel_index | (g->channel_index << 4));
		mask32(io, 0x088c, 0xf000, 0x6);
		mask32(io, 0x0cbc, 0x200000, 0x1);
		break;
	}
}

/* rtw_set_channel_mac, 20 MHz, primary channel index 0. */
static void
channel_mac(struct rtw88_io *io, const struct rtw88_channel_tuple *tuple,
    const struct rtw8822c_channel_geometry *g)
{
	uint32_t v;
	unsigned ch = tuple->primary;

	w8(io, 0x0483, g->data_sc);		/* DATA_SC */
	v = rd(io, 0x0668, 4);			/* WMAC_TRXPTCL_CTL: RFMOD */
	v &= ~0x180U;
	if (tuple->width_mhz == 40)
		v |= 0x80;
	else if (tuple->width_mhz == 80)
		v |= 0x100;
	w32(io, 0x0668, v);
	v = rd(io, 0x0024, 4);			/* AFE_CTRL1: 80M */
	if (tuple->width_mhz != 80)
		v &= ~UINT32_C(0x300000);
	w32(io, 0x0024, v);
	w8(io, 0x055c, 80);			/* USTIME_TSF */
	w8(io, 0x0638, 80);			/* USTIME_EDCA */
	v = rd(io, 0x0454, 1) & ~0x80U;		/* CCK_CHECK */
	if (channel_band(ch) == 5)
		v |= 0x80;
	w8(io, 0x0454, v);
}

static void
rstb_3wire(struct rtw88_io *io, int enable)
{

	mask32(io, 0x1c90, 0x100, enable ? 1 : 0);	/* RSTB: 3WIRE */
	if (enable) {
		mask32(io, 0x1830, UINT32_C(0x20000000), 1);	/* ANAPAR_A */
		mask32(io, 0x4130, UINT32_C(0x20000000), 1);	/* ANAPAR_B */
	}
}

static void
channel_rf(struct rtw88_io *io, const struct rtw88_channel_tuple *tuple)
{
	uint32_t rf18;
	unsigned path;
	unsigned ch = tuple->center;

	rf18 = read_rf(io, 0, RF_CFGCH, RFREG_MASK);
	rf18 &= ~(UINT32_C(0x10300) | 0xff | UINT32_C(0x60000) | 0x3000);
	if (channel_band(ch) == 5)
		rf18 |= 0x10100;			/* RF18_BAND_5G */
	rf18 |= ch & 0xff;
	if (ch >= 149 && ch <= 177)
		rf18 |= 0x40000;			/* RFSI_GT_CH140 */
	else if (ch >= 100 && ch <= 144)
		rf18 |= 0x20000;			/* RFSI_GE_CH80 */
	if (tuple->width_mhz == 20)
		rf18 |= 0x3000;			/* BW_20M */
	else if (tuple->width_mhz == 40)
		rf18 |= 0x2000;			/* BW_40M */
	else
		rf18 |= 0x1000;			/* BW_80M */
	rstb_3wire(io, 0);
	for (path = 0; path < 2; path++) {
		write_rf(io, path, RF_LUTWE2, 0x04, 0x01);
		write_rf(io, path, RF_LUTWA, 0x1f, 0x12);
		write_rf(io, path, RF_LUTWD0, 0xfffff,
		    tuple->width_mhz == 20 ? 0x18 :
		    tuple->width_mhz == 40 ? 0x10 : 0x08);
		write_rf(io, path, RF_LUTWE2, 0x04, 0x00);
	}
	write_rf(io, 0, RF_CFGCH, RFREG_MASK, rf18);
	write_rf(io, 1, RF_CFGCH, RFREG_MASK, rf18);
	rstb_3wire(io, 1);
}

int
rtw8822c_set_channel_tuple(struct rtw88_softc *sc,
    const struct rtw88_channel_tuple *tuple)
{
	struct rtw88_io io;
	struct rtw8822c_channel_geometry g;
	uint32_t gen = sc->sc_generation;
	int error;

	if ((error = rtw8822c_channel_geometry(tuple, &g)) != 0)
		return error;
	sc->sc_phy.channel_valid = 0;
	if (sc->sc_stop_requested)
		return EINTR;
	rtw88_io_init(&io, sc->sc_ops, sc->sc_bus);
	channel_bb(&io, tuple, &g);
	if (io.error == 0 && (sc->sc_stop_requested || sc->sc_generation != gen))
		io.error = EINTR;
	if (io.error == 0)
		channel_mac(&io, tuple, &g);
	if (io.error == 0 && (sc->sc_stop_requested || sc->sc_generation != gen))
		io.error = EINTR;
	if (io.error == 0)
		channel_rf(&io, tuple);
	if (io.error == 0 && (sc->sc_stop_requested || sc->sc_generation != gen))
		io.error = EINTR;
	if (io.error == 0)
		toggle_igi(&io);
	if (io.error == 0 && (sc->sc_stop_requested || sc->sc_generation != gen))
		io.error = EINTR;
	return io.error;
}

int
rtw8822c_set_channel(struct rtw88_softc *sc, unsigned ch)
{
	const struct rtw88_channel_tuple tuple = { ch, ch, 20 };

	return rtw8822c_set_channel_tuple(sc, &tuple);
}

/* query_phy_status_page0 gain bounds, pinned RTL8822C phy_set_param.
 * These readbacks are parser context, not an RF calibration success claim. */
int
rtw8822c_signal_cache(struct rtw88_softc *sc)
{
    struct rtw88_phy_state v = { 0 };
    uint32_t a, b, c;
    int error;

    v.generation = sc->sc_generation;
    v.paths = 3; /* Successful native config_trx_ab, before HCI start. */
    v.channel = sc->sc_phy.channel;
    error = sc->sc_ops->read_4(sc->sc_bus, 0x1a98, &a);
    if (error == 0)
        error = sc->sc_ops->read_4(sc->sc_bus, 0x1aa8, &b);
    if (error == 0)
        error = sc->sc_ops->read_4(sc->sc_bus, 0x1a70, &c);
    if (error == 0) {
        v.gain_high = (uint8_t)(((a >> 14) & 3) << 4 | ((b >> 16) & 15));
        v.gain_low = (uint8_t)(((a >> 6) & 3) << 4 | ((c >> 24) & 15));
        if (v.gain_low > v.gain_high)
            error = EINVAL;
    }
    v.gain_valid = error == 0;
    v.gain_error = error;
    sc->sc_phy = v; /* Replace old context on success AND failure. */
    return error;
}
