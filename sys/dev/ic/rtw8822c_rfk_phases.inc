/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: FreeBSD d2145e9b96448da34d3cfede24c509af6d830c92 rtw8822c.c.
 * Native private phase bodies; included only by the complete owning consumer.
 * Errors, freshness and cancellation are enforced by its typed IO services.
 * This native adaptation does not establish physical RFK acceptance. */
static void rtw8822c_rfk_power_save(struct rtw8822c_rfk *rtwdev,
				    bool is_power_save)
{
	u8 path;

	for (path = 0; path < rtwdev->hal.rf_path_num; path++) {
		rtw_write32_mask(rtwdev, REG_NCTL0, BIT_SEL_PATH, path);
		rtw_write32_mask(rtwdev, REG_DPD_CTL1_S0, BIT_PS_EN,
				 is_power_save ? 0 : 1);
	}
}

static void rtw8822c_txgapk_backup_bb_reg(struct rtw8822c_rfk *rtwdev, const u32 reg[],
					  u32 reg_backup[], u32 reg_num)
{
	u32 i;

	for (i = 0; i < reg_num; i++) {
		reg_backup[i] = rtw_read32(rtwdev, reg[i]);
		rfk_save(rtwdev, reg[i], reg_backup[i], 0, 0);
	}
}

static void rtw8822c_txgapk_reload_bb_reg(struct rtw8822c_rfk *rtwdev,
					  const u32 reg[], u32 reg_backup[],
					  u32 reg_num)
{
	u32 i;

	for (i = 0; i < reg_num; i++) {
		if (reg[i] != REG_TX_PTCL_CTRL && reg[i] != REG_TX_FIFO)
			rtw_write32(rtwdev, reg[i], reg_backup[i]);
	}
}

static void rtw8822c_txgapk_tx_pause(struct rtw8822c_rfk *rtwdev)
{
	rtw_write8(rtwdev, REG_TXPAUSE, BIT_AC_QUEUE);
	rtw_write32_mask(rtwdev, REG_TX_FIFO, BIT_STOP_TX, 0x2);
	(void)rfk_rf_idle(rtwdev, 0);
}

static void rtw8822c_txgapk_bb_dpk(struct rtw8822c_rfk *rtwdev, u8 path)
{
	(void)rtwdev;

	rtw_write32_mask(rtwdev, REG_ENFN, BIT_IQK_DPK_EN, 0x1);
	rtw_write32_mask(rtwdev, REG_CH_DELAY_EXTR2,
			 BIT_IQK_DPK_CLOCK_SRC, 0x1);
	rtw_write32_mask(rtwdev, REG_CH_DELAY_EXTR2,
			 BIT_IQK_DPK_RESET_SRC, 0x1);
	rtw_write32_mask(rtwdev, REG_CH_DELAY_EXTR2, BIT_EN_IOQ_IQK_DPK, 0x1);
	rtw_write32_mask(rtwdev, REG_CH_DELAY_EXTR2, BIT_TST_IQK2SET_SRC, 0x0);
	rtw_write32_mask(rtwdev, REG_CCA_OFF, BIT_CCA_ON_BY_PW, 0x1ff);

	if (path == RF_PATH_A) {
		rtw_write32_mask(rtwdev, REG_RFTXEN_GCK_A,
				 BIT_RFTXEN_GCK_FORCE_ON, 0x1);
		rtw_write32_mask(rtwdev, REG_3WIRE, BIT_DIS_SHARERX_TXGAT, 0x1);
		rtw_write32_mask(rtwdev, REG_DIS_SHARE_RX_A,
				 BIT_TX_SCALE_0DB, 0x1);
		rtw_write32_mask(rtwdev, REG_3WIRE, BIT_3WIRE_EN, 0x0);
	} else if (path == RF_PATH_B) {
		rtw_write32_mask(rtwdev, REG_RFTXEN_GCK_B,
				 BIT_RFTXEN_GCK_FORCE_ON, 0x1);
		rtw_write32_mask(rtwdev, REG_3WIRE2,
				 BIT_DIS_SHARERX_TXGAT, 0x1);
		rtw_write32_mask(rtwdev, REG_DIS_SHARE_RX_B,
				 BIT_TX_SCALE_0DB, 0x1);
		rtw_write32_mask(rtwdev, REG_3WIRE2, BIT_3WIRE_EN, 0x0);
	}
	rtw_write32_mask(rtwdev, REG_CCKSB, BIT_BBMODE, 0x2);
}

static void rtw8822c_txgapk_afe_dpk(struct rtw8822c_rfk *rtwdev, u8 path)
{
	u32 reg;

	(void)rtwdev;

	if (path == RF_PATH_A) {
		reg = REG_ANAPAR_A;
	} else if (path == RF_PATH_B) {
		reg = REG_ANAPAR_B;
	} else {
		(void)rtwdev;
		return;
	}

	rtw_write32_mask(rtwdev, REG_IQK_CTRL, MASKDWORD, MASKDWORD);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x700f0001);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x700f0001);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x701f0001);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x702f0001);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x703f0001);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x704f0001);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x705f0001);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x706f0001);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x707f0001);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x708f0001);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x709f0001);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x70af0001);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x70bf0001);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x70cf0001);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x70df0001);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x70ef0001);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x70ff0001);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x70ff0001);
}

static void rtw8822c_txgapk_afe_dpk_restore(struct rtw8822c_rfk *rtwdev, u8 path)
{
	u32 reg;

	(void)rtwdev;

	if (path == RF_PATH_A) {
		reg = REG_ANAPAR_A;
	} else if (path == RF_PATH_B) {
		reg = REG_ANAPAR_B;
	} else {
		(void)rtwdev;
		return;
	}
	rtw_write32_mask(rtwdev, REG_IQK_CTRL, MASKDWORD, 0xffa1005e);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x700b8041);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x70144041);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x70244041);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x70344041);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x70444041);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x705b8041);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x70644041);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x707b8041);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x708b8041);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x709b8041);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x70ab8041);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x70bb8041);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x70cb8041);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x70db8041);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x70eb8041);
	rtw_write32_mask(rtwdev, reg, MASKDWORD, 0x70fb8041);
}

static void rtw8822c_txgapk_bb_dpk_restore(struct rtw8822c_rfk *rtwdev, u8 path)
{
	(void)rtwdev;

	rtw_write_rf(rtwdev, path, RF_DEBUG, BIT_DE_TX_GAIN, 0x0);
	rtw_write_rf(rtwdev, path, RF_DIS_BYPASS_TXBB, BIT_TIA_BYPASS, 0x0);
	rtw_write_rf(rtwdev, path, RF_DIS_BYPASS_TXBB, BIT_TXBB, 0x0);

	rtw_write32_mask(rtwdev, REG_NCTL0, BIT_SEL_PATH, 0x0);
	rtw_write32_mask(rtwdev, REG_IQK_CTL1, BIT_TX_CFIR, 0x0);
	rtw_write32_mask(rtwdev, REG_SINGLE_TONE_SW, BIT_IRQ_TEST_MODE, 0x0);
	rtw_write32_mask(rtwdev, REG_R_CONFIG, MASKBYTE0, 0x00);
	rtw_write32_mask(rtwdev, REG_NCTL0, BIT_SEL_PATH, 0x1);
	rtw_write32_mask(rtwdev, REG_IQK_CTL1, BIT_TX_CFIR, 0x0);
	rtw_write32_mask(rtwdev, REG_SINGLE_TONE_SW, BIT_IRQ_TEST_MODE, 0x0);
	rtw_write32_mask(rtwdev, REG_R_CONFIG, MASKBYTE0, 0x00);
	rtw_write32_mask(rtwdev, REG_NCTL0, BIT_SEL_PATH, 0x0);
	rtw_write32_mask(rtwdev, REG_CCA_OFF, BIT_CCA_ON_BY_PW, 0x0);

	if (path == RF_PATH_A) {
		rtw_write32_mask(rtwdev, REG_RFTXEN_GCK_A,
				 BIT_RFTXEN_GCK_FORCE_ON, 0x0);
		rtw_write32_mask(rtwdev, REG_3WIRE, BIT_DIS_SHARERX_TXGAT, 0x0);
		rtw_write32_mask(rtwdev, REG_DIS_SHARE_RX_A,
				 BIT_TX_SCALE_0DB, 0x0);
		rtw_write32_mask(rtwdev, REG_3WIRE, BIT_3WIRE_EN, 0x3);
	} else if (path == RF_PATH_B) {
		rtw_write32_mask(rtwdev, REG_RFTXEN_GCK_B,
				 BIT_RFTXEN_GCK_FORCE_ON, 0x0);
		rtw_write32_mask(rtwdev, REG_3WIRE2,
				 BIT_DIS_SHARERX_TXGAT, 0x0);
		rtw_write32_mask(rtwdev, REG_DIS_SHARE_RX_B,
				 BIT_TX_SCALE_0DB, 0x0);
		rtw_write32_mask(rtwdev, REG_3WIRE2, BIT_3WIRE_EN, 0x3);
	}

	rtw_write32_mask(rtwdev, REG_CCKSB, BIT_BBMODE, 0x0);
	rtw_write32_mask(rtwdev, REG_IQK_CTL1, BIT_CFIR_EN, 0x5);
}

static bool _rtw8822c_txgapk_gain_valid(struct rtw8822c_rfk *rtwdev, u32 gain)
{

	(void)rtwdev;
	if ((FIELD_GET(BIT_GAIN_TX_PAD_H, gain) >= 0xc) &&
	    (FIELD_GET(BIT_GAIN_TX_PAD_L, gain) >= 0xe))
		return true;

	return false;
}

static void _rtw8822c_txgapk_write_gain_bb_table(struct rtw8822c_rfk *rtwdev,
						 u8 band, u8 path)
{
	struct rtw_gapk_info *txgapk = &rtwdev->dm_info.gapk;
	u32 v, tmp_3f = 0;
	u8 gain, check_txgain;

	rtw_write32_mask(rtwdev, REG_NCTL0, BIT_SEL_PATH, path);

	switch (band) {
	case RF_BAND_2G_OFDM:
		rtw_write32_mask(rtwdev, REG_TABLE_SEL, BIT_Q_GAIN_SEL, 0x0);
		break;
	case RF_BAND_5G_L:
		rtw_write32_mask(rtwdev, REG_TABLE_SEL, BIT_Q_GAIN_SEL, 0x2);
		break;
	case RF_BAND_5G_M:
		rtw_write32_mask(rtwdev, REG_TABLE_SEL, BIT_Q_GAIN_SEL, 0x3);
		break;
	case RF_BAND_5G_H:
		rtw_write32_mask(rtwdev, REG_TABLE_SEL, BIT_Q_GAIN_SEL, 0x4);
		break;
	default:
		break;
	}

	rtw_write32_mask(rtwdev, REG_TX_GAIN_SET, MASKBYTE0, 0x88);

	check_txgain = 0;
	for (gain = 0; gain < RF_GAIN_NUM; gain++) {
		v = txgapk->rf3f_bp[band][gain][path];
		if (_rtw8822c_txgapk_gain_valid(rtwdev, v)) {
			if (!check_txgain) {
				tmp_3f = txgapk->rf3f_bp[band][gain][path];
				check_txgain = 1;
			}
			(void)rtwdev;
		} else {
			tmp_3f = txgapk->rf3f_bp[band][gain][path];
		}

		rtw_write32_mask(rtwdev, REG_TABLE_SEL, BIT_Q_GAIN, tmp_3f);
		rtw_write32_mask(rtwdev, REG_TABLE_SEL, BIT_I_GAIN, gain);
		rtw_write32_mask(rtwdev, REG_TABLE_SEL, BIT_GAIN_RST, 0x1);
		rtw_write32_mask(rtwdev, REG_TABLE_SEL, BIT_GAIN_RST, 0x0);

		(void)rtwdev;
	}
}

static void rtw8822c_txgapk_write_gain_bb_table(struct rtw8822c_rfk *rtwdev)
{
	u8 path, band;

	(void)rtwdev;

	for (band = 0; band < RF_BAND_MAX; band++) {
		for (path = 0; path < rtwdev->hal.rf_path_num; path++) {
			_rtw8822c_txgapk_write_gain_bb_table(rtwdev,
							     band, path);
		}
	}
}

static void rtw8822c_txgapk_read_offset(struct rtw8822c_rfk *rtwdev, u8 path)
{
	static const u32 cfg1_1b00[2] = {0x00000d18, 0x00000d2a};
	static const u32 cfg2_1b00[2] = {0x00000d19, 0x00000d2b};
	static const u32 set_pi[2] = {REG_RSV_CTRL, REG_WLRF1};
	static const u32 path_setting[2] = {REG_ORITXCODE, REG_ORITXCODE2};
	struct rtw_gapk_info *txgapk = &rtwdev->dm_info.gapk;
	u8 channel = txgapk->channel;
	u32 val;
	int i;

	if (path >= ARRAY_SIZE(cfg1_1b00) ||
	    path >= ARRAY_SIZE(cfg2_1b00) ||
	    path >= ARRAY_SIZE(set_pi) ||
	    path >= ARRAY_SIZE(path_setting)) {
		(void)rtwdev;
		return;
	}

	rtw_write32_mask(rtwdev, REG_ANTMAP0, BIT_ANT_PATH, path + 1);
	rtw_write32_mask(rtwdev, REG_TXLGMAP, MASKDWORD, 0xe4e40000);
	rtw_write32_mask(rtwdev, REG_TXANTSEG, BIT_ANTSEG, 0x3);
	rtw_write32_mask(rtwdev, path_setting[path], MASK20BITS, 0x33312);
	rtw_write32_mask(rtwdev, path_setting[path], BIT_PATH_EN, 0x1);
	rtw_write32_mask(rtwdev, set_pi[path], BITS_RFC_DIRECT, 0x0);
	rtw_write_rf(rtwdev, path, RF_LUTDBG, BIT_TXA_TANK, 0x1);
	rtw_write_rf(rtwdev, path, RF_IDAC, BIT_TX_MODE, 0x820);
	rtw_write32_mask(rtwdev, REG_NCTL0, BIT_SEL_PATH, path);
	rtw_write32_mask(rtwdev, REG_IQKSTAT, MASKBYTE0, 0x0);

	rtw_write32_mask(rtwdev, REG_TX_TONE_IDX, MASKBYTE0, 0x018);
	fsleep(1000);
	if (channel >= 1 && channel <= 14)
		rtw_write32_mask(rtwdev, REG_R_CONFIG, MASKBYTE0, BIT_2G_SWING);
	else
		rtw_write32_mask(rtwdev, REG_R_CONFIG, MASKBYTE0, BIT_5G_SWING);
	fsleep(1000);

	/* The two primary writes form one shot. Arm before either can land;
	 * only an observed POST-trigger non-target then target accepts it. */
	rfk_arm(rtwdev, REG_RPT_CIP, 4, BIT_RPT_CIP_STATUS, 0x55,
	    100000, 0, RFK_GAPK);
	rtw_write32_mask(rtwdev, REG_NCTL0, MASKDWORD, cfg1_1b00[path]);
	rtw_write32_mask(rtwdev, REG_NCTL0, MASKDWORD, cfg2_1b00[path]);
	rfk_capture(rtwdev);
	if (!rfk_report(rtwdev, 1000))
		return;

	rtw_write32_mask(rtwdev, set_pi[path], BITS_RFC_DIRECT, 0x2);
	rtw_write32_mask(rtwdev, REG_NCTL0, BIT_SEL_PATH, path);
	rtw_write32_mask(rtwdev, REG_RXSRAM_CTL, BIT_RPT_EN, 0x1);
	rtw_write32_mask(rtwdev, REG_RXSRAM_CTL, BIT_RPT_SEL, 0x12);
	rtw_write32_mask(rtwdev, REG_TX_GAIN_SET, BIT_GAPK_RPT_IDX, 0x3);
	val = rtw_read32(rtwdev, REG_STAT_RPT);

	txgapk->offset[0][path] = (s8)FIELD_GET(BIT_GAPK_RPT0, val);
	txgapk->offset[1][path] = (s8)FIELD_GET(BIT_GAPK_RPT1, val);
	txgapk->offset[2][path] = (s8)FIELD_GET(BIT_GAPK_RPT2, val);
	txgapk->offset[3][path] = (s8)FIELD_GET(BIT_GAPK_RPT3, val);
	txgapk->offset[4][path] = (s8)FIELD_GET(BIT_GAPK_RPT4, val);
	txgapk->offset[5][path] = (s8)FIELD_GET(BIT_GAPK_RPT5, val);
	txgapk->offset[6][path] = (s8)FIELD_GET(BIT_GAPK_RPT6, val);
	txgapk->offset[7][path] = (s8)FIELD_GET(BIT_GAPK_RPT7, val);

	rtw_write32_mask(rtwdev, REG_TX_GAIN_SET, BIT_GAPK_RPT_IDX, 0x4);
	val = rtw_read32(rtwdev, REG_STAT_RPT);

	txgapk->offset[8][path] = (s8)FIELD_GET(BIT_GAPK_RPT0, val);
	txgapk->offset[9][path] = (s8)FIELD_GET(BIT_GAPK_RPT1, val);

	for (i = 0; i < RF_HW_OFFSET_NUM; i++)
		if (txgapk->offset[i][path] & BIT(3))
			txgapk->offset[i][path] = txgapk->offset[i][path] |
						  0xf0;
	for (i = 0; i < RF_HW_OFFSET_NUM; i++)
		(void)rtwdev;
}

static void rtw8822c_txgapk_calculate_offset(struct rtw8822c_rfk *rtwdev, u8 path)
{
	static const u32 bb_reg[] = {REG_ANTMAP0, REG_TXLGMAP, REG_TXANTSEG,
				     REG_ORITXCODE, REG_ORITXCODE2};
	struct rtw_gapk_info *txgapk = &rtwdev->dm_info.gapk;
	u8 channel = txgapk->channel;
	u32 reg_backup[ARRAY_SIZE(bb_reg)] = {0};

	(void)rtwdev;

	rtw8822c_txgapk_backup_bb_reg(rtwdev, bb_reg,
				      reg_backup, ARRAY_SIZE(bb_reg));

	if (channel >= 1 && channel <= 14) {
		rtw_write32_mask(rtwdev,
				 REG_SINGLE_TONE_SW, BIT_IRQ_TEST_MODE, 0x0);
		rtw_write32_mask(rtwdev, REG_NCTL0, BIT_SEL_PATH, path);
		rtw_write32_mask(rtwdev, REG_R_CONFIG, BIT_IQ_SWITCH, 0x3f);
		rtw_write32_mask(rtwdev, REG_IQK_CTL1, BIT_TX_CFIR, 0x0);
		rtw_write_rf(rtwdev, path, RF_DEBUG, BIT_DE_TX_GAIN, 0x1);
		rtw_write_rf(rtwdev, path, RF_MODE_TRXAGC, RFREG_MASK, 0x5000f);
		rtw_write_rf(rtwdev, path, RF_TX_GAIN_OFFSET, BIT_RF_GAIN, 0x0);
		rtw_write_rf(rtwdev, path, RF_RXG_GAIN, BIT_RXG_GAIN, 0x1);
		rtw_write_rf(rtwdev, path, RF_MODE_TRXAGC, BIT_RXAGC, 0x0f);
		rtw_write_rf(rtwdev, path, RF_DEBUG, BIT_DE_TRXBW, 0x1);
		rtw_write_rf(rtwdev, path, RF_BW_TRXBB, BIT_BW_TXBB, 0x1);
		rtw_write_rf(rtwdev, path, RF_BW_TRXBB, BIT_BW_RXBB, 0x0);
		rtw_write_rf(rtwdev, path, RF_EXT_TIA_BW, BIT_PW_EXT_TIA, 0x1);

		rtw_write32_mask(rtwdev, REG_IQKSTAT, MASKBYTE0, 0x00);
		rtw_write32_mask(rtwdev, REG_TABLE_SEL, BIT_Q_GAIN_SEL, 0x0);

		rtw8822c_txgapk_read_offset(rtwdev, path);
		(void)rtwdev;

	} else {
		rtw_write32_mask(rtwdev,
				 REG_SINGLE_TONE_SW, BIT_IRQ_TEST_MODE, 0x0);
		rtw_write32_mask(rtwdev, REG_NCTL0, BIT_SEL_PATH, path);
		rtw_write32_mask(rtwdev, REG_R_CONFIG, BIT_IQ_SWITCH, 0x3f);
		rtw_write32_mask(rtwdev, REG_IQK_CTL1, BIT_TX_CFIR, 0x0);
		rtw_write_rf(rtwdev, path, RF_DEBUG, BIT_DE_TX_GAIN, 0x1);
		rtw_write_rf(rtwdev, path, RF_MODE_TRXAGC, RFREG_MASK, 0x50011);
		rtw_write_rf(rtwdev, path, RF_TXA_LB_SW, BIT_TXA_LB_ATT, 0x3);
		rtw_write_rf(rtwdev, path, RF_TXA_LB_SW, BIT_LB_ATT, 0x3);
		rtw_write_rf(rtwdev, path, RF_TXA_LB_SW, BIT_LB_SW, 0x1);
		rtw_write_rf(rtwdev, path,
			     RF_RXA_MIX_GAIN, BIT_RXA_MIX_GAIN, 0x2);
		rtw_write_rf(rtwdev, path, RF_MODE_TRXAGC, BIT_RXAGC, 0x12);
		rtw_write_rf(rtwdev, path, RF_DEBUG, BIT_DE_TRXBW, 0x1);
		rtw_write_rf(rtwdev, path, RF_BW_TRXBB, BIT_BW_RXBB, 0x0);
		rtw_write_rf(rtwdev, path, RF_EXT_TIA_BW, BIT_PW_EXT_TIA, 0x1);
		rtw_write_rf(rtwdev, path, RF_MODE_TRXAGC, BIT_RF_MODE, 0x5);

		rtw_write32_mask(rtwdev, REG_IQKSTAT, MASKBYTE0, 0x0);

		if (channel >= 36 && channel <= 64)
			rtw_write32_mask(rtwdev,
					 REG_TABLE_SEL, BIT_Q_GAIN_SEL, 0x2);
		else if (channel >= 100 && channel <= 144)
			rtw_write32_mask(rtwdev,
					 REG_TABLE_SEL, BIT_Q_GAIN_SEL, 0x3);
		else if (channel >= 149 && channel <= 177)
			rtw_write32_mask(rtwdev,
					 REG_TABLE_SEL, BIT_Q_GAIN_SEL, 0x4);

		rtw8822c_txgapk_read_offset(rtwdev, path);
		(void)rtwdev;
	}
	rtw8822c_txgapk_reload_bb_reg(rtwdev, bb_reg,
				      reg_backup, ARRAY_SIZE(bb_reg));
}

static void rtw8822c_txgapk_rf_restore(struct rtw8822c_rfk *rtwdev, u8 path)
{
	(void)rtwdev;

	if (path >= rtwdev->hal.rf_path_num)
		return;

	rtw_write_rf(rtwdev, path, RF_MODE_TRXAGC, BIT_RF_MODE, 0x3);
	rtw_write_rf(rtwdev, path, RF_DEBUG, BIT_DE_TRXBW, 0x0);
	rtw_write_rf(rtwdev, path, RF_EXT_TIA_BW, BIT_PW_EXT_TIA, 0x0);
}

static u32 rtw8822c_txgapk_cal_gain(struct rtw8822c_rfk *rtwdev, u32 gain, s8 offset)
{
	int64_t gain_x2;
	u32 new_gain;

	(void)rtwdev;

	if (_rtw8822c_txgapk_gain_valid(rtwdev, gain)) {
		new_gain = gain;
		(void)rtwdev;
		return new_gain;
	}

	gain_x2 = (int64_t)gain * 2 + offset;
	if (gain_x2 < 0 || gain_x2 > (int64_t)BIT_GAIN_EXT * 2 - 1) {
		(void)rfk_error(rtwdev, ERANGE);
		return 0;
	}
	new_gain = (u32)(gain_x2 >> 1) |
	    (gain_x2 & 1 ? BIT_GAIN_EXT : 0);

	(void)rtwdev;

	return new_gain;
}

static void rtw8822c_txgapk_write_tx_gain(struct rtw8822c_rfk *rtwdev)
{
	struct rtw_gapk_info *txgapk = &rtwdev->dm_info.gapk;
	u32 i, j, tmp = 0x20, tmp_3f, v;
	s8 offset_tmp[RF_GAIN_NUM] = {0};
	u8 path, band = RF_BAND_2G_OFDM, channel = txgapk->channel;

	(void)rtwdev;

	if (channel >= 1 && channel <= 14) {
		tmp = 0x20;
		band = RF_BAND_2G_OFDM;
	} else if (channel >= 36 && channel <= 64) {
		tmp = 0x200;
		band = RF_BAND_5G_L;
	} else if (channel >= 100 && channel <= 144) {
		tmp = 0x280;
		band = RF_BAND_5G_M;
	} else if (channel >= 149 && channel <= 177) {
		tmp = 0x300;
		band = RF_BAND_5G_H;
	} else {
		(void)rtwdev;
		return;
	}

	for (path = 0; path < rtwdev->hal.rf_path_num; path++) {
		for (i = 0; i < RF_GAIN_NUM; i++) {
			offset_tmp[i] = 0;
			for (j = i; j < RF_GAIN_NUM; j++) {
				v = txgapk->rf3f_bp[band][j][path];
				if (_rtw8822c_txgapk_gain_valid(rtwdev, v))
					continue;

				offset_tmp[i] += txgapk->offset[j][path];
				txgapk->fianl_offset[i][path] = offset_tmp[i];
			}

			v = txgapk->rf3f_bp[band][i][path];
			if (_rtw8822c_txgapk_gain_valid(rtwdev, v)) {
				(void)rtwdev;
			} else {
				txgapk->rf3f_fs[path][i] = offset_tmp[i];
				(void)rtwdev;
			}
		}

		rtw_write_rf(rtwdev, path, RF_LUTWE2, RFREG_MASK, 0x10000);
		for (i = 0; i < RF_GAIN_NUM; i++) {
			rtw_write_rf(rtwdev, path,
				     RF_LUTWA, RFREG_MASK, tmp + i);

			tmp_3f = rtw8822c_txgapk_cal_gain(rtwdev,
							  txgapk->rf3f_bp[band][i][path],
							  offset_tmp[i]);
			rtw_write_rf(rtwdev, path, RF_LUTWD0,
				     BIT_GAIN_EXT | BIT_DATA_L, tmp_3f);

			(void)rtwdev;
		}
		rtw_write_rf(rtwdev, path, RF_LUTWE2, RFREG_MASK, 0x0);
	}
}

static void rtw8822c_txgapk_save_all_tx_gain_table(struct rtw8822c_rfk *rtwdev)
{
	struct rtw_gapk_info *txgapk = &rtwdev->dm_info.gapk;
	static const u32 three_wire[2] = {REG_3WIRE, REG_3WIRE2};
	static const u8 ch_num[RF_BAND_MAX] = {1, 1, 36, 100, 149};
	static const u8 band_num[RF_BAND_MAX] = {0x0, 0x0, 0x1, 0x3, 0x5};
	static const u8 cck[RF_BAND_MAX] = {0x1, 0x0, 0x0, 0x0, 0x0};
	u8 path, band, gain, rf0_idx;
	u32 rf18, v;

	/* This explicit native experiment has no dynamic feature-disable mask.
	 * The outer owner already rejected unsupported TSSI profiles. */

	(void)rtwdev;

	if (txgapk->read_txgain == 1) {
		(void)rtwdev;
		rtw8822c_txgapk_write_gain_bb_table(rtwdev);
		return;
	}

	for (band = 0; band < RF_BAND_MAX; band++) {
		for (path = 0; path < rtwdev->hal.rf_path_num; path++) {
			rf18 = rtw_read_rf(rtwdev, path, RF_CFGCH, RFREG_MASK);
			rfk_save(rtwdev, RF_CFGCH, rf18, path, 1);

			rtw_write32_mask(rtwdev,
					 three_wire[path], BIT_3WIRE_EN, 0x0);
			rtw_write_rf(rtwdev, path,
				     RF_CFGCH, MASKBYTE0, ch_num[band]);
			rtw_write_rf(rtwdev, path,
				     RF_CFGCH, BIT_BAND, band_num[band]);
			rtw_write_rf(rtwdev, path,
				     RF_BW_TRXBB, BIT_DBG_CCK_CCA, cck[band]);
			rtw_write_rf(rtwdev, path,
				     RF_BW_TRXBB, BIT_TX_CCK_IND, cck[band]);
			gain = 0;
			for (rf0_idx = 1; rf0_idx < 32; rf0_idx += 3) {
				rtw_write_rf(rtwdev, path, RF_MODE_TRXAGC,
					     MASKBYTE0, rf0_idx);
				v = rtw_read_rf(rtwdev, path,
						RF_TX_RESULT, RFREG_MASK);
				txgapk->rf3f_bp[band][gain][path] = v & BIT_DATA_L;

				(void)rtwdev;
				gain++;
			}
			rtw_write_rf(rtwdev, path, RF_CFGCH, RFREG_MASK, rf18);
			rtw_write32_mask(rtwdev,
					 three_wire[path], BIT_3WIRE_EN, 0x3);
		}
	}
	rtw8822c_txgapk_write_gain_bb_table(rtwdev);
	if (rtwdev->error == 0)
		txgapk->read_txgain = 1;
}

static void rtw8822c_txgapk(struct rtw8822c_rfk *rtwdev)
{
	struct rtw_gapk_info *txgapk = &rtwdev->dm_info.gapk;
	u8 path;

	(void)rtwdev;

	/* The owning full transaction already captured 520/1e70 before
	 * the early pause. Do not relabel an internally paused word. */
	if (rtwdev->outer_valid != 3) {
		(void)rfk_error(rtwdev, EPROTO);
		return;
	}
	rtw8822c_txgapk_tx_pause(rtwdev);
	if (rtwdev->error != 0)
		return;
	rtw8822c_txgapk_save_all_tx_gain_table(rtwdev);

	if (txgapk->read_txgain == 0) {
		(void)rtwdev;
		return;
	}

	if (rtwdev->efuse.power_track_type >= 4 &&
	    rtwdev->efuse.power_track_type <= 7) {
		(void)rtwdev;
		return;
	}

	for (path = 0; path < rtwdev->hal.rf_path_num; path++) {
		txgapk->channel = rtw_read_rf(rtwdev, path,
					      RF_CFGCH, RFREG_MASK) & MASKBYTE0;
		rtw8822c_txgapk_bb_dpk(rtwdev, path);
		rtw8822c_txgapk_afe_dpk(rtwdev, path);
		rtw8822c_txgapk_calculate_offset(rtwdev, path);
		rtw8822c_txgapk_rf_restore(rtwdev, path);
		rtw8822c_txgapk_afe_dpk_restore(rtwdev, path);
		rtw8822c_txgapk_bb_dpk_restore(rtwdev, path);
	}
	rtw8822c_txgapk_write_tx_gain(rtwdev);
	/* The outer finalizer restores source originals after IQK and DPK. */
}


static void rtw8822c_dpk_set_gnt_wl(struct rtw8822c_rfk *rtwdev, bool is_before_k)
{
	struct rtw_dpk_info *dpk_info = &rtwdev->dm_info.dpk_info;

	if (is_before_k) {
		dpk_info->gnt_control = rtw_read32(rtwdev, 0x70);
		dpk_info->gnt_value = rtw_coex_read_indirect_reg(rtwdev, 0x38);
		if (rtwdev->error != 0)
			return;
		rtwdev->gnt_valid = 1;
		rtw_write32_mask(rtwdev, 0x70, BIT(26), 0x1);
		rtw_coex_write_indirect_reg(rtwdev, 0x38, MASKBYTE1, 0x77);
	} else {
		rtw_coex_write_indirect_reg(rtwdev, 0x38, MASKDWORD,
					    dpk_info->gnt_value);
		rtw_write32(rtwdev, 0x70, dpk_info->gnt_control);
		if (rtwdev->error == 0)
			rtwdev->gnt_valid = 0;
	}
}

static void
rtw8822c_dpk_restore_registers(struct rtw8822c_rfk *rtwdev, u32 reg_num,
			       struct rtw_backup_info *bckp)
{
	rtw_restore_reg(rtwdev, bckp, reg_num);
	rtw_write32_mask(rtwdev, REG_NCTL0, BIT_SUBPAGE, 0xc);
	rtw_write32_mask(rtwdev, REG_RXSRAM_CTL, BIT_DPD_CLK, 0x4);
}

static void
rtw8822c_dpk_backup_registers(struct rtw8822c_rfk *rtwdev, u32 *reg,
			      u32 reg_num, struct rtw_backup_info *bckp)
{
	u32 i;

	for (i = 0; i < reg_num; i++) {
		bckp[i].len = 4;
		bckp[i].reg = reg[i];
		bckp[i].val = rtw_read32(rtwdev, reg[i]);
		rfk_save(rtwdev, reg[i], bckp[i].val, 0, 0);
	}
}

static void rtw8822c_dpk_backup_rf_registers(struct rtw8822c_rfk *rtwdev,
					     u32 *rf_reg,
					     u32 rf_reg_bak[][2])
{
	u32 i;

	for (i = 0; i < DPK_RF_REG_NUM; i++) {
		rf_reg_bak[i][RF_PATH_A] = rtw_read_rf(rtwdev, RF_PATH_A,
						       rf_reg[i], RFREG_MASK);
		rf_reg_bak[i][RF_PATH_B] = rtw_read_rf(rtwdev, RF_PATH_B,
						       rf_reg[i], RFREG_MASK);
		rfk_save(rtwdev, rf_reg[i], rf_reg_bak[i][RF_PATH_A], 0, 1);
		rfk_save(rtwdev, rf_reg[i], rf_reg_bak[i][RF_PATH_B], 1, 1);
	}
}

static void rtw8822c_dpk_reload_rf_registers(struct rtw8822c_rfk *rtwdev,
					     u32 *rf_reg,
					     u32 rf_reg_bak[][2])
{
	u32 i;

	for (i = 0; i < DPK_RF_REG_NUM; i++) {
		rtw_write_rf(rtwdev, RF_PATH_A, rf_reg[i], RFREG_MASK,
			     rf_reg_bak[i][RF_PATH_A]);
		rtw_write_rf(rtwdev, RF_PATH_B, rf_reg[i], RFREG_MASK,
			     rf_reg_bak[i][RF_PATH_B]);
	}
}

static void rtw8822c_dpk_information(struct rtw8822c_rfk *rtwdev)
{
	struct rtw_dpk_info *dpk_info = &rtwdev->dm_info.dpk_info;
	u32  reg;
	u8 band_shift;

	reg = rtw_read_rf(rtwdev, RF_PATH_A, RF_CFGCH, RFREG_MASK);


	band_shift = FIELD_GET(BIT(16), reg);
	dpk_info->dpk_band = 1 << band_shift;
	dpk_info->dpk_ch = FIELD_GET(0xff, reg);
	dpk_info->dpk_bw = FIELD_GET(0x3000, reg);
}

static void rtw8822c_dpk_rxbb_dc_cal(struct rtw8822c_rfk *rtwdev, u8 path)
{
	if (rtwdev->error != 0)
		return;
	/* The source uses a fixed-delay RXBB sequence, not a report poll.
	 * Retain uncertainty before even its first possibly-landed write;
	 * cancellation must not disguise another RF write as cleanup. */
	rtwdev->engine_pending = 1;
	rtw_write_rf(rtwdev, path, 0x92, RFREG_MASK, 0x84800);
	udelay(5);
	rtw_write_rf(rtwdev, path, 0x92, RFREG_MASK, 0x84801);
	if (rtwdev->error == 0) {
		/* Preserve the source usleep_range upper minimum and yield Giant. */
		rtwdev->delay_us += 610;
		(void)rfk_wait(rtwdev, 610);
	}
	rtw_write_rf(rtwdev, path, 0x92, RFREG_MASK, 0x84800);
	if (rtwdev->error == 0)
		rtwdev->engine_pending = 0;
}

static u8 rtw8822c_dpk_dc_corr_check(struct rtw8822c_rfk *rtwdev, u8 path)
{
	u16 dc_i, dc_q;
	u8 corr_idx;

	(void)path;

	rtw_write32(rtwdev, REG_RXSRAM_CTL, 0x000900f0);
	dc_i = (u16)rtw_read32_mask(rtwdev, REG_STAT_RPT, GENMASK(27, 16));
	dc_q = (u16)rtw_read32_mask(rtwdev, REG_STAT_RPT, GENMASK(11, 0));

	if (dc_i & BIT(11))
		dc_i = 0x1000 - dc_i;
	if (dc_q & BIT(11))
		dc_q = 0x1000 - dc_q;

	rtw_write32(rtwdev, REG_RXSRAM_CTL, 0x000000f0);
	corr_idx = (u8)rtw_read32_mask(rtwdev, REG_STAT_RPT, GENMASK(7, 0));
	rtw_read32_mask(rtwdev, REG_STAT_RPT, GENMASK(15, 8));

	if (dc_i > 200 || dc_q > 200 || corr_idx < 40 || corr_idx > 65)
		return 1;
	else
		return 0;

}

static void rtw8822c_dpk_tx_pause(struct rtw8822c_rfk *rtwdev)
{
	rtw_write8(rtwdev, 0x522, 0xff);
	rtw_write32_mask(rtwdev, 0x1e70, 0xf, 0x2);
	(void)rfk_rf_idle(rtwdev, 1);
}

static void rtw8822c_dpk_mac_bb_setting(struct rtw8822c_rfk *rtwdev)
{
	rtw8822c_dpk_tx_pause(rtwdev);
	rtw_load_table(rtwdev, &rtw8822c_dpk_mac_bb_tbl);
}

static void rtw8822c_dpk_afe_setting(struct rtw8822c_rfk *rtwdev, bool is_do_dpk)
{
	if (is_do_dpk)
		rtw_load_table(rtwdev, &rtw8822c_dpk_afe_is_dpk_tbl);
	else
		rtw_load_table(rtwdev, &rtw8822c_dpk_afe_no_dpk_tbl);
}

static void rtw8822c_dpk_pre_setting(struct rtw8822c_rfk *rtwdev)
{
	u8 path;

	for (path = 0; path < rtwdev->hal.rf_path_num; path++) {
		rtw_write_rf(rtwdev, path, RF_RXAGC_OFFSET, RFREG_MASK, 0x0);
		rtw_write32(rtwdev, REG_NCTL0, 0x8 | (path << 1));
		if (rtwdev->dm_info.dpk_info.dpk_band == RFK_BAND_2G)
			rtw_write32(rtwdev, REG_DPD_CTL1_S1, 0x1f100000);
		else
			rtw_write32(rtwdev, REG_DPD_CTL1_S1, 0x1f0d0000);
		rtw_write32_mask(rtwdev, REG_DPD_LUT0, BIT_GLOSS_DB, 0x4);
		rtw_write32_mask(rtwdev, REG_IQK_CTL1, BIT_TX_CFIR, 0x3);
	}
	rtw_write32_mask(rtwdev, REG_NCTL0, BIT_SUBPAGE, 0xc);
	rtw_write32(rtwdev, REG_DPD_CTL11, 0x3b23170b);
	rtw_write32(rtwdev, REG_DPD_CTL12, 0x775f5347);
}

static u32 rtw8822c_dpk_rf_setting(struct rtw8822c_rfk *rtwdev, u8 path)
{
	u32 ori_txbb;

	rtw_write_rf(rtwdev, path, RF_MODE_TRXAGC, RFREG_MASK, 0x50017);
	ori_txbb = rtw_read_rf(rtwdev, path, RF_TX_GAIN, RFREG_MASK);

	rtw_write_rf(rtwdev, path, RF_DEBUG, BIT_DE_TX_GAIN, 0x1);
	rtw_write_rf(rtwdev, path, RF_DEBUG, BIT_DE_PWR_TRIM, 0x1);
	rtw_write_rf(rtwdev, path, RF_TX_GAIN_OFFSET, BIT_BB_GAIN, 0x0);
	rtw_write_rf(rtwdev, path, RF_TX_GAIN, RFREG_MASK, ori_txbb);

	if (rtwdev->dm_info.dpk_info.dpk_band == RFK_BAND_2G) {
		rtw_write_rf(rtwdev, path, RF_TX_GAIN_OFFSET, BIT_RF_GAIN, 0x1);
		rtw_write_rf(rtwdev, path, RF_RXG_GAIN, BIT_RXG_GAIN, 0x0);
	} else {
		rtw_write_rf(rtwdev, path, RF_TXA_LB_SW, BIT_TXA_LB_ATT, 0x0);
		rtw_write_rf(rtwdev, path, RF_TXA_LB_SW, BIT_LB_ATT, 0x6);
		rtw_write_rf(rtwdev, path, RF_TXA_LB_SW, BIT_LB_SW, 0x1);
		rtw_write_rf(rtwdev, path, RF_RXA_MIX_GAIN, BIT_RXA_MIX_GAIN, 0);
	}

	rtw_write_rf(rtwdev, path, RF_MODE_TRXAGC, BIT_RXAGC, 0xf);
	rtw_write_rf(rtwdev, path, RF_DEBUG, BIT_DE_TRXBW, 0x1);
	rtw_write_rf(rtwdev, path, RF_BW_TRXBB, BIT_BW_RXBB, 0x0);

	if (rtwdev->dm_info.dpk_info.dpk_bw == DPK_CHANNEL_WIDTH_80)
		rtw_write_rf(rtwdev, path, RF_BW_TRXBB, BIT_BW_TXBB, 0x2);
	else
		rtw_write_rf(rtwdev, path, RF_BW_TRXBB, BIT_BW_TXBB, 0x1);

	rtw_write_rf(rtwdev, path, RF_EXT_TIA_BW, BIT(1), 0x1);

	usleep_range(100, 110);

	return ori_txbb & 0x1f;
}

static u16 rtw8822c_dpk_get_cmd(struct rtw8822c_rfk *rtwdev, u8 action, u8 path)
{
	u16 cmd;
	u8 bw = rtwdev->dm_info.dpk_info.dpk_bw == DPK_CHANNEL_WIDTH_80 ? 2 : 0;

	switch (action) {
	case RTW_DPK_GAIN_LOSS:
		cmd = 0x14 + path;
		break;
	case RTW_DPK_DO_DPK:
		cmd = 0x16 + path + bw;
		break;
	case RTW_DPK_DPK_ON:
		cmd = 0x1a + path;
		break;
	case RTW_DPK_DAGC:
		cmd = 0x1c + path + bw;
		break;
	default:
		return 0;
	}

	return (cmd << 8) | 0x48;
}

static u8 rtw8822c_dpk_one_shot(struct rtw8822c_rfk *rtwdev, u8 path, u8 action)
{
	u16 dpk_cmd;
	u8 result = 0;

	rtw8822c_dpk_set_gnt_wl(rtwdev, true);

	if (path >= DPK_RF_PATH_NUM || action >= RTW_DPK_ACTION_MAX) {
		(void)rfk_error(rtwdev, EINVAL);
		return 1;
	}
	if (action == RTW_DPK_CAL_PWR) {
		rfk_arm(rtwdev, REG_STAT_RPT, 4, BIT(31), 1,
		    20000, 10000, RFK_CAL_PWR);
		rtw_write32_mask(rtwdev, REG_DPD_CTL0, BIT(12), 0x1);
		rtw_write32_mask(rtwdev, REG_DPD_CTL0, BIT(12), 0x0);
		rtw_write32_mask(rtwdev, REG_RXSRAM_CTL, BIT_RPT_SEL, 0x0);
		rfk_capture(rtwdev);
		msleep(10);
		if (!check_hw_ready(rtwdev, REG_STAT_RPT, BIT(31), 0x1)) {
			result = 1;
			(void)rtwdev;
		}
	} else {
		rtw_write32_mask(rtwdev, REG_NCTL0, BIT_SUBPAGE,
				 0x8 | (path << 1));
		rtw_write32_mask(rtwdev, REG_R_CONFIG, BIT_IQ_SWITCH, 0x9);

		dpk_cmd = rtw8822c_dpk_get_cmd(rtwdev, action, path);
		rfk_arm(rtwdev, REG_RPT_CIP, 4, MASKBYTE0, 0x55,
		    20000, 10000, RFK_DPK);
		rtw_write32(rtwdev, REG_NCTL0, dpk_cmd);
		rtw_write32(rtwdev, REG_NCTL0, dpk_cmd + 1);
		rfk_capture(rtwdev);
		msleep(10);
		if (!check_hw_ready(rtwdev, 0x2d9c, 0xff, 0x55)) {
			result = 1;
			(void)rtwdev;
		}
		rtw_write32_mask(rtwdev, REG_NCTL0, BIT_SUBPAGE,
				 0x8 | (path << 1));
		rtw_write32_mask(rtwdev, REG_R_CONFIG, BIT_IQ_SWITCH, 0x0);
	}

	rtw8822c_dpk_set_gnt_wl(rtwdev, false);

	rtw_write8(rtwdev, 0x1b10, 0x0);

	return result;
}

static u16 rtw8822c_dpk_dgain_read(struct rtw8822c_rfk *rtwdev, u8 path)
{
	u16 dgain;

	(void)path;

	rtw_write32_mask(rtwdev, REG_NCTL0, BIT_SUBPAGE, 0xc);
	rtw_write32_mask(rtwdev, REG_RXSRAM_CTL, 0x00ff0000, 0x0);

	dgain = (u16)rtw_read32_mask(rtwdev, REG_STAT_RPT, GENMASK(27, 16));

	return dgain;
}

static u8 rtw8822c_dpk_thermal_read(struct rtw8822c_rfk *rtwdev, u8 path)
{
	rtw_write_rf(rtwdev, path, RF_T_METER, BIT(19), 0x1);
	rtw_write_rf(rtwdev, path, RF_T_METER, BIT(19), 0x0);
	rtw_write_rf(rtwdev, path, RF_T_METER, BIT(19), 0x1);
	udelay(15);

	return (u8)rtw_read_rf(rtwdev, path, RF_T_METER, 0x0007e);
}

static u32 rtw8822c_dpk_pas_read(struct rtw8822c_rfk *rtwdev, u8 path)
{
	u32 i_val, q_val;

	rtw_write32(rtwdev, REG_NCTL0, 0x8 | (path << 1));
	rtw_write32_mask(rtwdev, 0x1b48, BIT(14), 0x0);
	rtw_write32(rtwdev, REG_RXSRAM_CTL, 0x00060001);
	rtw_write32(rtwdev, 0x1b4c, 0x00000000);
	rtw_write32(rtwdev, 0x1b4c, 0x00080000);

	q_val = rtw_read32_mask(rtwdev, REG_STAT_RPT, MASKHWORD);
	i_val = rtw_read32_mask(rtwdev, REG_STAT_RPT, MASKLWORD);

	if (i_val & BIT(15))
		i_val = 0x10000 - i_val;
	if (q_val & BIT(15))
		q_val = 0x10000 - q_val;

	rtw_write32(rtwdev, 0x1b4c, 0x00000000);

	return i_val * i_val + q_val * q_val;
}

static u32 rtw8822c_psd_log2base(u32 val)
{
	u32 tmp, val_integerd_b, tindex;
	u32 result, val_fractiond_b;
	u32 table_fraction[21] = {0, 432, 332, 274, 232, 200, 174,
				  151, 132, 115, 100, 86, 74, 62, 51,
				  42, 32, 23, 15, 7, 0};

	if (val == 0)
		return 0;

	val_integerd_b = __fls(val) + 1;

	/* val_integerd_b can be 32. Keep the source ratio without a C
	 * shift by the type width or overflowing its numerator. */
	tmp = (u32)(((uint64_t)val * 100) /
	    (UINT64_C(1) << val_integerd_b));
	tindex = tmp / 5;

	if (tindex >= ARRAY_SIZE(table_fraction))
		tindex = ARRAY_SIZE(table_fraction) - 1;

	val_fractiond_b = table_fraction[tindex];

	result = val_integerd_b * 100 - val_fractiond_b;

	return result;
}

static u8 rtw8822c_dpk_gainloss_result(struct rtw8822c_rfk *rtwdev, u8 path)
{
	u8 result;

	rtw_write32_mask(rtwdev, REG_NCTL0, BIT_SUBPAGE, 0x8 | (path << 1));
	rtw_write32_mask(rtwdev, 0x1b48, BIT(14), 0x1);
	rtw_write32(rtwdev, REG_RXSRAM_CTL, 0x00060000);

	result = (u8)rtw_read32_mask(rtwdev, REG_STAT_RPT, 0x000000f0);

	rtw_write32_mask(rtwdev, 0x1b48, BIT(14), 0x0);

	return result;
}

static u8 rtw8822c_dpk_agc_gain_chk(struct rtw8822c_rfk *rtwdev, u8 path,
				    u8 limited_pga)
{
	u8 result = 0;
	u16 dgain;

	rtw8822c_dpk_one_shot(rtwdev, path, RTW_DPK_DAGC);
	dgain = rtw8822c_dpk_dgain_read(rtwdev, path);

	if (dgain > 1535 && !limited_pga)
		return RTW_DPK_GAIN_LESS;
	else if (dgain < 768 && !limited_pga)
		return RTW_DPK_GAIN_LARGE;
	else
		return result;
}

static u8 rtw8822c_dpk_agc_loss_chk(struct rtw8822c_rfk *rtwdev, u8 path)
{
	u32 loss, loss_db;

	loss = rtw8822c_dpk_pas_read(rtwdev, path);
	if (loss < 0x4000000)
		return RTW_DPK_GL_LESS;
	loss_db = 3 * rtw8822c_psd_log2base(loss >> 13) - 3870;

	if (loss_db > 1000)
		return RTW_DPK_GL_LARGE;
	else if (loss_db < 250)
		return RTW_DPK_GL_LESS;
	else
		return RTW_DPK_AGC_OUT;
}

struct rtw8822c_dpk_data {
	u8 txbb;
	u8 pga;
	u8 limited_pga;
	u8 agc_cnt;
	bool loss_only;
	bool gain_only;
	u8 path;
};

static u8 rtw8822c_gain_check_state(struct rtw8822c_rfk *rtwdev,
				    struct rtw8822c_dpk_data *data)
{
	u8 state;

	data->txbb = (u8)rtw_read_rf(rtwdev, data->path, RF_TX_GAIN,
				     BIT_GAIN_TXBB);
	data->pga = (u8)rtw_read_rf(rtwdev, data->path, RF_MODE_TRXAGC,
				    BIT_RXAGC);

	if (data->loss_only) {
		state = RTW_DPK_LOSS_CHECK;
		goto check_end;
	}

	state = rtw8822c_dpk_agc_gain_chk(rtwdev, data->path,
					  data->limited_pga);
	if (state == RTW_DPK_GAIN_CHECK && data->gain_only)
		state = RTW_DPK_AGC_OUT;
	else if (state == RTW_DPK_GAIN_CHECK)
		state = RTW_DPK_LOSS_CHECK;

check_end:
	data->agc_cnt++;
	if (data->agc_cnt >= 6)
		state = RTW_DPK_AGC_OUT;

	return state;
}

static u8 rtw8822c_gain_large_state(struct rtw8822c_rfk *rtwdev,
				    struct rtw8822c_dpk_data *data)
{
	u8 pga = data->pga;

	if (pga > 0xe)
		rtw_write_rf(rtwdev, data->path, RF_MODE_TRXAGC, BIT_RXAGC, 0xc);
	else if (pga > 0xb && pga < 0xf)
		rtw_write_rf(rtwdev, data->path, RF_MODE_TRXAGC, BIT_RXAGC, 0x0);
	else if (pga < 0xc)
		data->limited_pga = 1;

	return RTW_DPK_GAIN_CHECK;
}

static u8 rtw8822c_gain_less_state(struct rtw8822c_rfk *rtwdev,
				   struct rtw8822c_dpk_data *data)
{
	u8 pga = data->pga;

	if (pga < 0xc)
		rtw_write_rf(rtwdev, data->path, RF_MODE_TRXAGC, BIT_RXAGC, 0xc);
	else if (pga > 0xb && pga < 0xf)
		rtw_write_rf(rtwdev, data->path, RF_MODE_TRXAGC, BIT_RXAGC, 0xf);
	else if (pga > 0xe)
		data->limited_pga = 1;

	return RTW_DPK_GAIN_CHECK;
}

static u8 rtw8822c_gl_state(struct rtw8822c_rfk *rtwdev,
			    struct rtw8822c_dpk_data *data, u8 is_large)
{
	u8 txbb_bound[] = {0x1f, 0};

	if (data->txbb == txbb_bound[is_large])
		return RTW_DPK_AGC_OUT;

	if ((is_large == 1 && data->txbb < 2) ||
	    (is_large == 0 && data->txbb > 0x1c)) {
		(void)rfk_error(rtwdev, ERANGE);
		return RTW_DPK_AGC_OUT;
	}
	if (is_large == 1)
		data->txbb -= 2;
	else
		data->txbb += 3;

	rtw_write_rf(rtwdev, data->path, RF_TX_GAIN, BIT_GAIN_TXBB, data->txbb);
	data->limited_pga = 0;

	return RTW_DPK_GAIN_CHECK;
}

static u8 rtw8822c_gl_large_state(struct rtw8822c_rfk *rtwdev,
				  struct rtw8822c_dpk_data *data)
{
	return rtw8822c_gl_state(rtwdev, data, 1);
}

static u8 rtw8822c_gl_less_state(struct rtw8822c_rfk *rtwdev,
				 struct rtw8822c_dpk_data *data)
{
	return rtw8822c_gl_state(rtwdev, data, 0);
}

static u8 rtw8822c_loss_check_state(struct rtw8822c_rfk *rtwdev,
				    struct rtw8822c_dpk_data *data)
{
	u8 path = data->path;
	u8 state;

	rtw8822c_dpk_one_shot(rtwdev, path, RTW_DPK_GAIN_LOSS);
	state = rtw8822c_dpk_agc_loss_chk(rtwdev, path);

	return state;
}

static u8 (*dpk_state[])(struct rtw8822c_rfk *rtwdev,
			  struct rtw8822c_dpk_data *data) = {
	rtw8822c_gain_check_state, rtw8822c_gain_large_state,
	rtw8822c_gain_less_state, rtw8822c_gl_large_state,
	rtw8822c_gl_less_state, rtw8822c_loss_check_state };

static u8 rtw8822c_dpk_pas_agc(struct rtw8822c_rfk *rtwdev, u8 path,
			       bool gain_only, bool loss_only)
{
	struct rtw8822c_dpk_data data = {0};
	u8 (*func)(struct rtw8822c_rfk *rtwdev, struct rtw8822c_dpk_data *data);
	u8 state = RTW_DPK_GAIN_CHECK;

	data.loss_only = loss_only;
	data.gain_only = gain_only;
	data.path = path;

	for (;;) {
		if (rtwdev->error != 0)
			return 0;
		if (state >= ARRAY_SIZE(dpk_state)) {
			(void)rfk_error(rtwdev, EPROTO);
			return 0;
		}
		func = dpk_state[state];
		state = func(rtwdev, &data);
		if (state == RTW_DPK_AGC_OUT)
			break;
	}

	return data.txbb;
}

static bool rtw8822c_dpk_coef_iq_check(struct rtw8822c_rfk *rtwdev,
				       u16 coef_i, u16 coef_q)
{

	(void)rtwdev;
	if (coef_i == 0x1000 || coef_i == 0x0fff ||
	    coef_q == 0x1000 || coef_q == 0x0fff)
		return true;

	return false;
}

static u32 rtw8822c_dpk_coef_transfer(struct rtw8822c_rfk *rtwdev)
{
	u32 reg = 0;
	u16 coef_i = 0, coef_q = 0;

	/* Keep the source debug read and its IO/error behavior. */
	(void)rtw_read32(rtwdev, REG_STAT_RPT);

	coef_i = (u16)rtw_read32_mask(rtwdev, REG_STAT_RPT, MASKHWORD) & 0x1fff;
	coef_q = (u16)rtw_read32_mask(rtwdev, REG_STAT_RPT, MASKLWORD) & 0x1fff;

	coef_q = ((0x2000 - coef_q) & 0x1fff) - 1;

	reg = (coef_i << 16) | coef_q;

	return reg;
}

static const u32 rtw8822c_dpk_get_coef_tbl[] = {
	0x000400f0, 0x040400f0, 0x080400f0, 0x010400f0, 0x050400f0,
	0x090400f0, 0x020400f0, 0x060400f0, 0x0a0400f0, 0x030400f0,
	0x070400f0, 0x0b0400f0, 0x0c0400f0, 0x100400f0, 0x0d0400f0,
	0x110400f0, 0x0e0400f0, 0x120400f0, 0x0f0400f0, 0x130400f0,
};

static void rtw8822c_dpk_coef_tbl_apply(struct rtw8822c_rfk *rtwdev, u8 path)
{
	struct rtw_dpk_info *dpk_info = &rtwdev->dm_info.dpk_info;
	int i;

	for (i = 0; i < 20; i++) {
		rtw_write32(rtwdev, REG_RXSRAM_CTL,
			    rtw8822c_dpk_get_coef_tbl[i]);
		dpk_info->coef[path][i] = rtw8822c_dpk_coef_transfer(rtwdev);
	}
}

static void rtw8822c_dpk_get_coef(struct rtw8822c_rfk *rtwdev, u8 path)
{
	rtw_write32(rtwdev, REG_NCTL0, 0x0000000c);

	if (path == RF_PATH_A) {
		rtw_write32_mask(rtwdev, REG_DPD_CTL0, BIT(24), 0x0);
		rtw_write32(rtwdev, REG_DPD_CTL0_S0, 0x30000080);
	} else if (path == RF_PATH_B) {
		rtw_write32_mask(rtwdev, REG_DPD_CTL0, BIT(24), 0x1);
		rtw_write32(rtwdev, REG_DPD_CTL0_S1, 0x30000080);
	}

	rtw8822c_dpk_coef_tbl_apply(rtwdev, path);
}

static u8 rtw8822c_dpk_coef_read(struct rtw8822c_rfk *rtwdev, u8 path)
{
	struct rtw_dpk_info *dpk_info = &rtwdev->dm_info.dpk_info;
	u8 addr, result = 1;
	u16 coef_i, coef_q;

	for (addr = 0; addr < 20; addr++) {
		coef_i = FIELD_GET(0x1fff0000, dpk_info->coef[path][addr]);
		coef_q = FIELD_GET(0x1fff, dpk_info->coef[path][addr]);

		if (rtw8822c_dpk_coef_iq_check(rtwdev, coef_i, coef_q)) {
			result = 0;
			break;
		}
	}
	return result;
}

static void rtw8822c_dpk_coef_write(struct rtw8822c_rfk *rtwdev, u8 path, u8 result)
{
	struct rtw_dpk_info *dpk_info = &rtwdev->dm_info.dpk_info;
	u16 reg[DPK_RF_PATH_NUM] = {0x1b0c, 0x1b64};
	u32 coef;
	u8 addr;

	rtw_write32(rtwdev, REG_NCTL0, 0x0000000c);
	rtw_write32(rtwdev, REG_RXSRAM_CTL, 0x000000f0);

	for (addr = 0; addr < 20; addr++) {
		if (result == 0) {
			if (addr == 3)
				coef = 0x04001fff;
			else
				coef = 0x00001fff;
		} else {
			coef = dpk_info->coef[path][addr];
		}
		rtw_write32(rtwdev, reg[path] + addr * 4, coef);
	}
}

static void rtw8822c_dpk_fill_result(struct rtw8822c_rfk *rtwdev, u32 dpk_txagc,
				     u8 path, u8 result)
{
	struct rtw_dpk_info *dpk_info = &rtwdev->dm_info.dpk_info;

	rtw_write32_mask(rtwdev, REG_NCTL0, BIT_SUBPAGE, 0x8 | (path << 1));

	if (result && (dpk_txagc < 6 || dpk_txagc > 0x1f)) {
		(void)rfk_error(rtwdev, ERANGE);
		return;
	}
	if (result)
		rtw_write8(rtwdev, REG_DPD_AGC, (u8)(dpk_txagc - 6));
	else
		rtw_write8(rtwdev, REG_DPD_AGC, 0x00);

	dpk_info->result[path] = result;
	dpk_info->dpk_txagc[path] = rtw_read8(rtwdev, REG_DPD_AGC);

	rtw8822c_dpk_coef_write(rtwdev, path, result);
}

static u32 rtw8822c_dpk_gainloss(struct rtw8822c_rfk *rtwdev, u8 path)
{
	struct rtw_dpk_info *dpk_info = &rtwdev->dm_info.dpk_info;
	u8 tx_agc, tx_bb, ori_txbb, ori_txagc, tx_agc_search, t1, t2;

	ori_txbb = rtw8822c_dpk_rf_setting(rtwdev, path);
	ori_txagc = (u8)rtw_read_rf(rtwdev, path, RF_MODE_TRXAGC, BIT_TXAGC);

	rtw8822c_dpk_rxbb_dc_cal(rtwdev, path);
	rtw8822c_dpk_one_shot(rtwdev, path, RTW_DPK_DAGC);
	rtw8822c_dpk_dgain_read(rtwdev, path);

	if (rtw8822c_dpk_dc_corr_check(rtwdev, path)) {
		rtw8822c_dpk_rxbb_dc_cal(rtwdev, path);
		rtw8822c_dpk_one_shot(rtwdev, path, RTW_DPK_DAGC);
		rtw8822c_dpk_dc_corr_check(rtwdev, path);
	}

	t1 = rtw8822c_dpk_thermal_read(rtwdev, path);
	tx_bb = rtw8822c_dpk_pas_agc(rtwdev, path, false, true);
	tx_agc_search = rtw8822c_dpk_gainloss_result(rtwdev, path);

	if (tx_bb < tx_agc_search)
		tx_bb = 0;
	else
		tx_bb = tx_bb - tx_agc_search;

	rtw_write_rf(rtwdev, path, RF_TX_GAIN, BIT_GAIN_TXBB, tx_bb);

	{
		int value = (int)ori_txagc - ((int)ori_txbb - (int)tx_bb);
		if (value < 0 || value > 0x1f) {
			(void)rfk_error(rtwdev, ERANGE);
			return 0;
		}
		tx_agc = (u8)value;
	}

	t2 = rtw8822c_dpk_thermal_read(rtwdev, path);

	dpk_info->thermal_dpk_delta[path] = abs(t2 - t1);

	return tx_agc;
}

static u8 rtw8822c_dpk_by_path(struct rtw8822c_rfk *rtwdev, u32 tx_agc, u8 path)
{
	u8 result;

	(void)tx_agc;

	result = rtw8822c_dpk_one_shot(rtwdev, path, RTW_DPK_DO_DPK);

	rtw_write32_mask(rtwdev, REG_NCTL0, BIT_SUBPAGE, 0x8 | (path << 1));

	result = result | (u8)rtw_read32_mask(rtwdev, REG_DPD_CTL1_S0, BIT(26));

	rtw_write_rf(rtwdev, path, RF_MODE_TRXAGC, RFREG_MASK, 0x33e14);

	rtw8822c_dpk_get_coef(rtwdev, path);

	return result;
}

static void rtw8822c_dpk_cal_gs(struct rtw8822c_rfk *rtwdev, u8 path)
{
	struct rtw_dpk_info *dpk_info = &rtwdev->dm_info.dpk_info;
	u32 tmp_gs = 0;

	rtw_write32_mask(rtwdev, REG_NCTL0, BIT_SUBPAGE, 0x8 | (path << 1));
	rtw_write32_mask(rtwdev, REG_IQK_CTL1, BIT_BYPASS_DPD, 0x0);
	rtw_write32_mask(rtwdev, REG_IQK_CTL1, BIT_TX_CFIR, 0x0);
	rtw_write32_mask(rtwdev, REG_R_CONFIG, BIT_IQ_SWITCH, 0x9);
	rtw_write32_mask(rtwdev, REG_R_CONFIG, BIT_INNER_LB, 0x1);
	rtw_write32_mask(rtwdev, REG_NCTL0, BIT_SUBPAGE, 0xc);
	rtw_write32_mask(rtwdev, REG_RXSRAM_CTL, BIT_DPD_CLK, 0xf);

	if (path == RF_PATH_A) {
		rtw_write32_mask(rtwdev, REG_DPD_CTL0_S0, BIT_GS_PWSF,
				 0x1066680);
		rtw_write32_mask(rtwdev, REG_DPD_CTL1_S0, BIT_DPD_EN, 0x1);
	} else {
		rtw_write32_mask(rtwdev, REG_DPD_CTL0_S1, BIT_GS_PWSF,
				 0x1066680);
		rtw_write32_mask(rtwdev, REG_DPD_CTL1_S1, BIT_DPD_EN, 0x1);
	}

	if (dpk_info->dpk_bw == DPK_CHANNEL_WIDTH_80) {
		rtw_write32(rtwdev, REG_DPD_CTL16, 0x80001310);
		rtw_write32(rtwdev, REG_DPD_CTL16, 0x00001310);
		rtw_write32(rtwdev, REG_DPD_CTL16, 0x810000db);
		rtw_write32(rtwdev, REG_DPD_CTL16, 0x010000db);
		rtw_write32(rtwdev, REG_DPD_CTL16, 0x0000b428);
		rtw_write32(rtwdev, REG_DPD_CTL15,
			    0x05020000 | (BIT(path) << 28));
	} else {
		rtw_write32(rtwdev, REG_DPD_CTL16, 0x8200190c);
		rtw_write32(rtwdev, REG_DPD_CTL16, 0x0200190c);
		rtw_write32(rtwdev, REG_DPD_CTL16, 0x8301ee14);
		rtw_write32(rtwdev, REG_DPD_CTL16, 0x0301ee14);
		rtw_write32(rtwdev, REG_DPD_CTL16, 0x0000b428);
		rtw_write32(rtwdev, REG_DPD_CTL15,
			    0x05020008 | (BIT(path) << 28));
	}

	rtw_write32_mask(rtwdev, REG_DPD_CTL0, MASKBYTE3, 0x8 | path);

	rtw8822c_dpk_one_shot(rtwdev, path, RTW_DPK_CAL_PWR);

	rtw_write32_mask(rtwdev, REG_DPD_CTL15, MASKBYTE3, 0x0);
	rtw_write32_mask(rtwdev, REG_NCTL0, BIT_SUBPAGE, 0x8 | (path << 1));
	rtw_write32_mask(rtwdev, REG_R_CONFIG, BIT_IQ_SWITCH, 0x0);
	rtw_write32_mask(rtwdev, REG_R_CONFIG, BIT_INNER_LB, 0x0);
	rtw_write32_mask(rtwdev, REG_NCTL0, BIT_SUBPAGE, 0xc);

	if (path == RF_PATH_A)
		rtw_write32_mask(rtwdev, REG_DPD_CTL0_S0, BIT_GS_PWSF, 0x5b);
	else
		rtw_write32_mask(rtwdev, REG_DPD_CTL0_S1, BIT_GS_PWSF, 0x5b);

	rtw_write32_mask(rtwdev, REG_RXSRAM_CTL, BIT_RPT_SEL, 0x0);

	tmp_gs = (u16)rtw_read32_mask(rtwdev, REG_STAT_RPT, BIT_RPT_DGAIN);
	tmp_gs = (tmp_gs * 910) >> 10;
	tmp_gs = DIV_ROUND_CLOSEST(tmp_gs, 10);

	if (path == RF_PATH_A)
		rtw_write32_mask(rtwdev, REG_DPD_CTL0_S0, BIT_GS_PWSF, tmp_gs);
	else
		rtw_write32_mask(rtwdev, REG_DPD_CTL0_S1, BIT_GS_PWSF, tmp_gs);

	dpk_info->dpk_gs[path] = tmp_gs;
}

static void rtw8822c_dpk_cal_coef1(struct rtw8822c_rfk *rtwdev)
{
	struct rtw_dpk_info *dpk_info = &rtwdev->dm_info.dpk_info;
	u32 offset[DPK_RF_PATH_NUM] = {0, 0x58};
	u32 i_scaling;
	u8 path;

	rtw_write32_mask(rtwdev, REG_NCTL0, BIT_SUBPAGE, 0x0000000c);
	rtw_write32(rtwdev, REG_RXSRAM_CTL, 0x000000f0);
	rfk_arm(rtwdev, REG_RPT_CIP, 4, MASKBYTE0, 0x55,
	    10000, 0, RFK_COEF);
	rtw_write32(rtwdev, REG_NCTL0, 0x00001148);
	rtw_write32(rtwdev, REG_NCTL0, 0x00001149);
	rfk_capture(rtwdev);

	if (!check_hw_ready(rtwdev, 0x2d9c, MASKBYTE0, 0x55))
		return;

	rtw_write8(rtwdev, 0x1b10, 0x0);
	rtw_write32_mask(rtwdev, REG_NCTL0, BIT_SUBPAGE, 0x0000000c);

	for (path = 0; path < rtwdev->hal.rf_path_num; path++) {
		if (dpk_info->dpk_gs[path] == 0) {
			(void)rfk_error(rtwdev, ERANGE);
			return;
		}
		i_scaling = 0x16c00 / dpk_info->dpk_gs[path];

		rtw_write32_mask(rtwdev, 0x1b18 + offset[path], MASKHWORD,
				 i_scaling);
		rtw_write32_mask(rtwdev, REG_DPD_CTL0_S0 + offset[path],
				 GENMASK(31, 28), 0x9);
		rtw_write32_mask(rtwdev, REG_DPD_CTL0_S0 + offset[path],
				 GENMASK(31, 28), 0x1);
		rtw_write32_mask(rtwdev, REG_DPD_CTL0_S0 + offset[path],
				 GENMASK(31, 28), 0x0);
		rtw_write32_mask(rtwdev, REG_DPD_CTL1_S0 + offset[path],
				 BIT(14), 0x0);
	}
}

static void rtw8822c_dpk_on(struct rtw8822c_rfk *rtwdev, u8 path)
{
	struct rtw_dpk_info *dpk_info = &rtwdev->dm_info.dpk_info;

	rtw8822c_dpk_one_shot(rtwdev, path, RTW_DPK_DPK_ON);

	rtw_write32_mask(rtwdev, REG_NCTL0, BIT_SUBPAGE, 0x8 | (path << 1));
	rtw_write32_mask(rtwdev, REG_IQK_CTL1, BIT_TX_CFIR, 0x0);

	if (test_bit(path, dpk_info->dpk_path_ok))
		rtw8822c_dpk_cal_gs(rtwdev, path);
}

static bool rtw8822c_dpk_check_pass(struct rtw8822c_rfk *rtwdev, bool is_fail,
				    u32 dpk_txagc, u8 path)
{
	bool result;

	if (!is_fail) {
		if (rtw8822c_dpk_coef_read(rtwdev, path))
			result = true;
		else
			result = false;
	} else {
		result = false;
	}

	if (!result) {
		/* The primary writes substitute coefficients and logs failure.
		 * This experiment must not publish them as a successful chain. */
		(void)rfk_error(rtwdev, EIO);
		return false;
	}
	rtw8822c_dpk_fill_result(rtwdev, dpk_txagc, path, result);

	return result;
}

static void rtw8822c_dpk_result_reset(struct rtw8822c_rfk *rtwdev)
{
	struct rtw_dpk_info *dpk_info = &rtwdev->dm_info.dpk_info;
	u8 path;

	for (path = 0; path < rtwdev->hal.rf_path_num; path++) {
		clear_bit(path, dpk_info->dpk_path_ok);
		rtw_write32_mask(rtwdev, REG_NCTL0, BIT_SUBPAGE,
				 0x8 | (path << 1));
		rtw_write32_mask(rtwdev, 0x1b58, 0x0000007f, 0x0);

		dpk_info->dpk_txagc[path] = 0;
		dpk_info->result[path] = 0;
		dpk_info->dpk_gs[path] = 0x5b;
		dpk_info->pre_pwsf[path] = 0;
		dpk_info->thermal_dpk[path] = rtw8822c_dpk_thermal_read(rtwdev,
									path);
	}
}

static void rtw8822c_dpk_calibrate(struct rtw8822c_rfk *rtwdev, u8 path)
{
	struct rtw_dpk_info *dpk_info = &rtwdev->dm_info.dpk_info;
	u32 dpk_txagc;
	u8 dpk_fail;

	(void)rtwdev;

	dpk_txagc = rtw8822c_dpk_gainloss(rtwdev, path);

	dpk_fail = rtw8822c_dpk_by_path(rtwdev, dpk_txagc, path);

	if (!rtw8822c_dpk_check_pass(rtwdev, dpk_fail, dpk_txagc, path))
		(void)rtwdev;

	(void)rtwdev;

	if (dpk_info->result[path])
		set_bit(path, dpk_info->dpk_path_ok);
}

static void rtw8822c_dpk_path_select(struct rtw8822c_rfk *rtwdev)
{
	rtw8822c_dpk_calibrate(rtwdev, RF_PATH_A);
	rtw8822c_dpk_calibrate(rtwdev, RF_PATH_B);
	rtw8822c_dpk_on(rtwdev, RF_PATH_A);
	rtw8822c_dpk_on(rtwdev, RF_PATH_B);
	rtw8822c_dpk_cal_coef1(rtwdev);
}

static void rtw8822c_dpk_enable_disable(struct rtw8822c_rfk *rtwdev)
{
	struct rtw_dpk_info *dpk_info = &rtwdev->dm_info.dpk_info;
	u32 mask = BIT(15) | BIT(14);

	rtw_write32_mask(rtwdev, REG_NCTL0, BIT_SUBPAGE, 0xc);

	rtw_write32_mask(rtwdev, REG_DPD_CTL1_S0, BIT_DPD_EN,
			 dpk_info->is_dpk_pwr_on);
	rtw_write32_mask(rtwdev, REG_DPD_CTL1_S1, BIT_DPD_EN,
			 dpk_info->is_dpk_pwr_on);

	if (test_bit(RF_PATH_A, dpk_info->dpk_path_ok)) {
		rtw_write32_mask(rtwdev, REG_DPD_CTL1_S0, mask, 0x0);
		rtw_write8(rtwdev, REG_DPD_CTL0_S0, dpk_info->dpk_gs[RF_PATH_A]);
	}
	if (test_bit(RF_PATH_B, dpk_info->dpk_path_ok)) {
		rtw_write32_mask(rtwdev, REG_DPD_CTL1_S1, mask, 0x0);
		rtw_write8(rtwdev, REG_DPD_CTL0_S1, dpk_info->dpk_gs[RF_PATH_B]);
	}
}
