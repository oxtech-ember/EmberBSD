/*
 * AD9361 initialization defaults derived from Analog Devices no-OS
 * projects/ad9361/src/main.c, commit
 * ce7107f957d19858f2dffc85bf3d7f2c70367000.
 * Original author: DBogdan (dragos.bogdan@analog.com).
 * Copyright 2013(c) Analog Devices, Inc.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of Analog Devices, Inc. nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY ANALOG DEVICES, INC. "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL ANALOG DEVICES, INC. BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * The native FDT adapter, excluding the ADI-derived defaults, is licensed
 * as follows.
 * Copyright (c) 2026 EmberBSD contributors.
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/* Origin: FDT binding adapter; defaults derived from ADI no-OS
 * projects/ad9361/src/main.c at ce7107f957d19858f2dffc85bf3d7f2c70367000
 * (BSD-3-Clause; see LICENSE_BSD). */

#include <sys/param.h>
#include <sys/endian.h>
#include <sys/systm.h>
#include <dev/fdt/fdtvar.h>
#include "ad9361.h"
#include "ad9361_native.h"

static const AD9361_InitParam defaults = {
    .dev_sel = ID_AD9361,
    .reference_clk_rate = 40000000UL,
    .two_rx_two_tx_mode_enable = 1,
    .one_rx_one_tx_mode_use_rx_num = 1,
    .one_rx_one_tx_mode_use_tx_num = 1,
    .frequency_division_duplex_mode_enable = 1,
    .frequency_division_duplex_independent_mode_enable = 0,
    .tdd_use_dual_synth_mode_enable = 0,
    .tdd_skip_vco_cal_enable = 0,
    .tx_fastlock_delay_ns = 0,
    .rx_fastlock_delay_ns = 0,
    .rx_fastlock_pincontrol_enable = 0,
    .tx_fastlock_pincontrol_enable = 0,
    .external_rx_lo_enable = 0,
    .external_tx_lo_enable = 0,
    .dc_offset_tracking_update_event_mask = 5,
    .dc_offset_attenuation_high_range = 6,
    .dc_offset_attenuation_low_range = 5,
    .dc_offset_count_high_range = 0x28,
    .dc_offset_count_low_range = 0x32,
    .split_gain_table_mode_enable = 0,
    .trx_synthesizer_target_fref_overwrite_hz = MAX_SYNTH_FREF,
    .qec_tracking_slow_mode_enable = 0,
    .ensm_enable_pin_pulse_mode_enable = 0,
    .ensm_enable_txnrx_control_enable = 0,
    .rx_synthesizer_frequency_hz = 2400000000UL,
    .tx_synthesizer_frequency_hz = 2400000000UL,
    .tx_lo_powerdown_managed_enable = 1,
    .rx_path_clock_frequencies = {983040000, 245760000, 122880000, 61440000,
				  30720000, 30720000},
    .tx_path_clock_frequencies = {983040000, 122880000, 122880000, 61440000,
				  30720000, 30720000},
    .rf_rx_bandwidth_hz = 18000000,
    .rf_tx_bandwidth_hz = 18000000,
    .rx_rf_port_input_select = 0,
    .tx_rf_port_input_select = 0,
    .tx_attenuation_mdB = 10000,
    .update_tx_gain_in_alert_enable = 0,
    .xo_disable_use_ext_refclk_enable = 0,
    .dcxo_coarse_and_fine_tune = {8, 5920},
    .clk_output_mode_select = CLKOUT_DISABLE,
    .gc_rx1_mode = 2,
    .gc_rx2_mode = 2,
    .gc_adc_large_overload_thresh = 58,
    .gc_adc_ovr_sample_size = 4,
    .gc_adc_small_overload_thresh = 47,
    .gc_dec_pow_measurement_duration = 8192,
    .gc_dig_gain_enable = 0,
    .gc_lmt_overload_high_thresh = 800,
    .gc_lmt_overload_low_thresh = 704,
    .gc_low_power_thresh = 24,
    .gc_max_dig_gain = 15,
    .gc_use_rx_fir_out_for_dec_pwr_meas_enable = 0,
    .mgc_dec_gain_step = 2,
    .mgc_inc_gain_step = 2,
    .mgc_rx1_ctrl_inp_enable = 0,
    .mgc_rx2_ctrl_inp_enable = 0,
    .mgc_split_table_ctrl_inp_gain_mode = 0,
    .agc_adc_large_overload_exceed_counter = 10,
    .agc_adc_large_overload_inc_steps = 2,
    .agc_adc_lmt_small_overload_prevent_gain_inc_enable = 0,
    .agc_adc_small_overload_exceed_counter = 10,
    .agc_dig_gain_step_size = 4,
    .agc_dig_saturation_exceed_counter = 3,
    .agc_gain_update_interval_us = 1000,
    .agc_immed_gain_change_if_large_adc_overload_enable = 0,
    .agc_immed_gain_change_if_large_lmt_overload_enable = 0,
    .agc_inner_thresh_high = 10,
    .agc_inner_thresh_high_dec_steps = 1,
    .agc_inner_thresh_low = 12,
    .agc_inner_thresh_low_inc_steps = 1,
    .agc_lmt_overload_large_exceed_counter = 10,
    .agc_lmt_overload_large_inc_steps = 2,
    .agc_lmt_overload_small_exceed_counter = 10,
    .agc_outer_thresh_high = 5,
    .agc_outer_thresh_high_dec_steps = 2,
    .agc_outer_thresh_low = 18,
    .agc_outer_thresh_low_inc_steps = 2,
    .agc_attack_delay_extra_margin_us = 1,
    .agc_sync_for_gain_counter_enable = 0,
    .fagc_dec_pow_measuremnt_duration = 64,
    .fagc_state_wait_time_ns = 260,
    .fagc_allow_agc_gain_increase = 0,
    .fagc_lp_thresh_increment_time = 5,
    .fagc_lp_thresh_increment_steps = 1,
    .fagc_lock_level_lmt_gain_increase_en = 1,
    .fagc_lock_level_gain_increase_upper_limit = 5,
    .fagc_lpf_final_settling_steps = 1,
    .fagc_lmt_final_settling_steps = 1,
    .fagc_final_overrange_count = 3,
    .fagc_gain_increase_after_gain_lock_en = 0,
    .fagc_gain_index_type_after_exit_rx_mode = 0,
    .fagc_use_last_lock_level_for_set_gain_en = 1,
    .fagc_rst_gla_stronger_sig_thresh_exceeded_en = 1,
    .fagc_optimized_gain_offset = 5,
    .fagc_rst_gla_stronger_sig_thresh_above_ll = 10,
    .fagc_rst_gla_engergy_lost_sig_thresh_exceeded_en = 1,
    .fagc_rst_gla_engergy_lost_goto_optim_gain_en = 1,
    .fagc_rst_gla_engergy_lost_sig_thresh_below_ll = 10,
    .fagc_energy_lost_stronger_sig_gain_lock_exit_cnt = 8,
    .fagc_rst_gla_large_adc_overload_en = 1,
    .fagc_rst_gla_large_lmt_overload_en = 1,
    .fagc_rst_gla_en_agc_pulled_high_en = 0,
    .fagc_rst_gla_if_en_agc_pulled_high_mode = 0,
    .fagc_power_measurement_duration_in_state5 = 64,
    .fagc_large_overload_inc_steps = 2,
    .rssi_delay = 1,
    .rssi_duration = 1000,
    .rssi_restart_mode = 3,
    .rssi_unit_is_rx_samples_enable = 0,
    .rssi_wait = 1,
    .aux_adc_decimation = 256,
    .aux_adc_rate = 40000000UL,
    .aux_dac_manual_mode_enable = 1,
    .aux_dac1_default_value_mV = 0,
    .aux_dac1_active_in_rx_enable = 0,
    .aux_dac1_active_in_tx_enable = 0,
    .aux_dac1_active_in_alert_enable = 0,
    .aux_dac1_rx_delay_us = 0,
    .aux_dac1_tx_delay_us = 0,
    .aux_dac2_default_value_mV = 0,
    .aux_dac2_active_in_rx_enable = 0,
    .aux_dac2_active_in_tx_enable = 0,
    .aux_dac2_active_in_alert_enable = 0,
    .aux_dac2_rx_delay_us = 0,
    .aux_dac2_tx_delay_us = 0,
    .temp_sense_decimation = 256,
    .temp_sense_measurement_interval_ms = 1000,
    .temp_sense_offset_signed = 0xCE,
    .temp_sense_periodic_measurement_enable = 1,
    .ctrl_outs_enable_mask = 0xFF,
    .ctrl_outs_index = 0,
    .elna_settling_delay_ns = 0,
    .elna_gain_mdB = 0,
    .elna_bypass_loss_mdB = 0,
    .elna_rx1_gpo0_control_enable = 0,
    .elna_rx2_gpo1_control_enable = 0,
    .elna_gaintable_all_index_enable = 0,
    .digital_interface_tune_skip_mode = 0,
    .digital_interface_tune_fir_disable = 0,
    .pp_tx_swap_enable = 1,
    .pp_rx_swap_enable = 1,
    .tx_channel_swap_enable = 0,
    .rx_channel_swap_enable = 0,
    .rx_frame_pulse_mode_enable = 1,
    .two_t_two_r_timing_enable = 0,
    .invert_data_bus_enable = 0,
    .invert_data_clk_enable = 0,
    .fdd_alt_word_order_enable = 0,
    .invert_rx_frame_enable = 0,
    .fdd_rx_rate_2tx_enable = 0,
    .swap_ports_enable = 0,
    .single_data_rate_enable = 0,
    .lvds_mode_enable = 1,
    .half_duplex_mode_enable = 0,
    .single_port_mode_enable = 0,
    .full_port_enable = 0,
    .full_duplex_swap_bits_enable = 0,
    .delay_rx_data = 0,
    .rx_data_clock_delay = 0,
    .rx_data_delay = 4,
    .tx_fb_clock_delay = 7,
    .tx_data_delay = 0,
    .lvds_bias_mV = 150,
    .lvds_rx_onchip_termination_enable = 1,
    .rx1rx2_phase_inversion_en = 0,
    .lvds_invert1_control = 0xFF,
    .lvds_invert2_control = 0x0F,
    .gpo_manual_mode_enable = 0,
    .gpo_manual_mode_enable_mask = 0,
    .gpo0_inactive_state_high_enable = 0,
    .gpo1_inactive_state_high_enable = 0,
    .gpo2_inactive_state_high_enable = 0,
    .gpo3_inactive_state_high_enable = 0,
    .gpo0_slave_rx_enable = 0,
    .gpo0_slave_tx_enable = 0,
    .gpo1_slave_rx_enable = 0,
    .gpo1_slave_tx_enable = 0,
    .gpo2_slave_rx_enable = 0,
    .gpo2_slave_tx_enable = 0,
    .gpo3_slave_rx_enable = 0,
    .gpo3_slave_tx_enable = 0,
    .gpo0_rx_delay_us = 0,
    .gpo0_tx_delay_us = 0,
    .gpo1_rx_delay_us = 0,
    .gpo1_tx_delay_us = 0,
    .gpo2_rx_delay_us = 0,
    .gpo2_tx_delay_us = 0,
    .gpo3_rx_delay_us = 0,
    .gpo3_tx_delay_us = 0,
    .low_high_gain_threshold_mdB = 37000,
    .low_gain_dB = 0,
    .high_gain_dB = 24,
    .tx_mon_track_en = 0,
    .one_shot_mode_en = 0,
    .tx_mon_delay = 511,
    .tx_mon_duration = 8192,
    .tx1_mon_front_end_gain = 2,
    .tx2_mon_front_end_gain = 2,
    .tx1_mon_lo_cm = 48,
    .tx2_mon_lo_cm = 48,
    .rx_only = true,
};

struct ad9361_property {
	const char *name;
	size_t offset;
	unsigned int width, count;
	bool boolean, signed_value;
};

static const struct ad9361_property properties[] = {
    {"adi,2rx-2tx-mode-enable",
     offsetof(AD9361_InitParam, two_rx_two_tx_mode_enable), 1, 1, true, false},
    {"adi,1rx-1tx-mode-use-rx-num",
     offsetof(AD9361_InitParam, one_rx_one_tx_mode_use_rx_num), 1, 1, false,
     false},
    {"adi,1rx-1tx-mode-use-tx-num",
     offsetof(AD9361_InitParam, one_rx_one_tx_mode_use_tx_num), 1, 1, false,
     false},
    {"adi,frequency-division-duplex-mode-enable",
     offsetof(AD9361_InitParam, frequency_division_duplex_mode_enable), 1, 1,
     true, false},
    {"adi,frequency-division-duplex-independent-mode-enable",
     offsetof(AD9361_InitParam,
	      frequency_division_duplex_independent_mode_enable),
     1, 1, true, false},
    {"adi,tdd-use-dual-synth-mode-enable",
     offsetof(AD9361_InitParam, tdd_use_dual_synth_mode_enable), 1, 1, true,
     false},
    {"adi,tdd-skip-vco-cal-enable",
     offsetof(AD9361_InitParam, tdd_skip_vco_cal_enable), 1, 1, true, false},
    {"adi,tx-fastlock-delay-ns",
     offsetof(AD9361_InitParam, tx_fastlock_delay_ns), 4, 1, false, false},
    {"adi,rx-fastlock-delay-ns",
     offsetof(AD9361_InitParam, rx_fastlock_delay_ns), 4, 1, false, false},
    {"adi,rx-fastlock-pincontrol-enable",
     offsetof(AD9361_InitParam, rx_fastlock_pincontrol_enable), 1, 1, true,
     false},
    {"adi,tx-fastlock-pincontrol-enable",
     offsetof(AD9361_InitParam, tx_fastlock_pincontrol_enable), 1, 1, true,
     false},
    {"adi,external-rx-lo-enable",
     offsetof(AD9361_InitParam, external_rx_lo_enable), 1, 1, true, false},
    {"adi,external-tx-lo-enable",
     offsetof(AD9361_InitParam, external_tx_lo_enable), 1, 1, true, false},
    {"adi,dc-offset-tracking-update-event-mask",
     offsetof(AD9361_InitParam, dc_offset_tracking_update_event_mask), 1, 1,
     false, false},
    {"adi,dc-offset-attenuation-high-range",
     offsetof(AD9361_InitParam, dc_offset_attenuation_high_range), 1, 1, false,
     false},
    {"adi,dc-offset-attenuation-low-range",
     offsetof(AD9361_InitParam, dc_offset_attenuation_low_range), 1, 1, false,
     false},
    {"adi,dc-offset-count-high-range",
     offsetof(AD9361_InitParam, dc_offset_count_high_range), 1, 1, false,
     false},
    {"adi,dc-offset-count-low-range",
     offsetof(AD9361_InitParam, dc_offset_count_low_range), 1, 1, false, false},
    {"adi,split-gain-table-mode-enable",
     offsetof(AD9361_InitParam, split_gain_table_mode_enable), 1, 1, true,
     false},
    {"adi,trx-synthesizer-target-fref-overwrite-hz",
     offsetof(AD9361_InitParam, trx_synthesizer_target_fref_overwrite_hz), 4, 1,
     false, false},
    {"adi,qec-tracking-slow-mode-enable",
     offsetof(AD9361_InitParam, qec_tracking_slow_mode_enable), 1, 1, true,
     false},
    {"adi,ensm-enable-pin-pulse-mode-enable",
     offsetof(AD9361_InitParam, ensm_enable_pin_pulse_mode_enable), 1, 1, true,
     false},
    {"adi,ensm-enable-txnrx-control-enable",
     offsetof(AD9361_InitParam, ensm_enable_txnrx_control_enable), 1, 1, true,
     false},
    {"adi,rx-synthesizer-frequency-hz",
     offsetof(AD9361_InitParam, rx_synthesizer_frequency_hz), 8, 1, false,
     false},
    {"adi,tx-synthesizer-frequency-hz",
     offsetof(AD9361_InitParam, tx_synthesizer_frequency_hz), 8, 1, false,
     false},
    {"adi,tx-lo-powerdown-managed-enable",
     offsetof(AD9361_InitParam, tx_lo_powerdown_managed_enable), 1, 1, true,
     false},
    {"adi,rx-path-clock-frequencies",
     offsetof(AD9361_InitParam, rx_path_clock_frequencies), 4, 6, false, false},
    {"adi,tx-path-clock-frequencies",
     offsetof(AD9361_InitParam, tx_path_clock_frequencies), 4, 6, false, false},
    {"adi,rf-rx-bandwidth-hz", offsetof(AD9361_InitParam, rf_rx_bandwidth_hz),
     4, 1, false, false},
    {"adi,rf-tx-bandwidth-hz", offsetof(AD9361_InitParam, rf_tx_bandwidth_hz),
     4, 1, false, false},
    {"adi,rx-rf-port-input-select",
     offsetof(AD9361_InitParam, rx_rf_port_input_select), 4, 1, false, false},
    {"adi,tx-rf-port-input-select",
     offsetof(AD9361_InitParam, tx_rf_port_input_select), 4, 1, false, false},
    {"adi,tx-attenuation-mdB", offsetof(AD9361_InitParam, tx_attenuation_mdB),
     4, 1, false, true},
    {"adi,update-tx-gain-in-alert-enable",
     offsetof(AD9361_InitParam, update_tx_gain_in_alert_enable), 1, 1, true,
     false},
    {"adi,xo-disable-use-ext-refclk-enable",
     offsetof(AD9361_InitParam, xo_disable_use_ext_refclk_enable), 1, 1, true,
     false},
    {"adi,dcxo-coarse-and-fine-tune",
     offsetof(AD9361_InitParam, dcxo_coarse_and_fine_tune), 4, 2, false, false},
    {"adi,clk-output-mode-select",
     offsetof(AD9361_InitParam, clk_output_mode_select), 4, 1, false, false},
    {"adi,gc-rx1-mode", offsetof(AD9361_InitParam, gc_rx1_mode), 1, 1, false,
     false},
    {"adi,gc-rx2-mode", offsetof(AD9361_InitParam, gc_rx2_mode), 1, 1, false,
     false},
    {"adi,gc-adc-large-overload-thresh",
     offsetof(AD9361_InitParam, gc_adc_large_overload_thresh), 1, 1, false,
     false},
    {"adi,gc-adc-ovr-sample-size",
     offsetof(AD9361_InitParam, gc_adc_ovr_sample_size), 1, 1, false, false},
    {"adi,gc-adc-small-overload-thresh",
     offsetof(AD9361_InitParam, gc_adc_small_overload_thresh), 1, 1, false,
     false},
    {"adi,gc-dec-pow-measurement-duration",
     offsetof(AD9361_InitParam, gc_dec_pow_measurement_duration), 2, 1, false,
     false},
    {"adi,gc-dig-gain-enable", offsetof(AD9361_InitParam, gc_dig_gain_enable),
     1, 1, true, false},
    {"adi,gc-lmt-overload-high-thresh",
     offsetof(AD9361_InitParam, gc_lmt_overload_high_thresh), 2, 1, false,
     false},
    {"adi,gc-lmt-overload-low-thresh",
     offsetof(AD9361_InitParam, gc_lmt_overload_low_thresh), 2, 1, false,
     false},
    {"adi,gc-low-power-thresh", offsetof(AD9361_InitParam, gc_low_power_thresh),
     1, 1, false, false},
    {"adi,gc-max-dig-gain", offsetof(AD9361_InitParam, gc_max_dig_gain), 1, 1,
     false, false},
    {"adi,gc-use-rx-fir-out-for-dec-pwr-meas-enable",
     offsetof(AD9361_InitParam, gc_use_rx_fir_out_for_dec_pwr_meas_enable), 1,
     1, true, false},
    {"adi,mgc-dec-gain-step", offsetof(AD9361_InitParam, mgc_dec_gain_step), 1,
     1, false, false},
    {"adi,mgc-inc-gain-step", offsetof(AD9361_InitParam, mgc_inc_gain_step), 1,
     1, false, false},
    {"adi,mgc-rx1-ctrl-inp-enable",
     offsetof(AD9361_InitParam, mgc_rx1_ctrl_inp_enable), 1, 1, true, false},
    {"adi,mgc-rx2-ctrl-inp-enable",
     offsetof(AD9361_InitParam, mgc_rx2_ctrl_inp_enable), 1, 1, true, false},
    {"adi,mgc-split-table-ctrl-inp-gain-mode",
     offsetof(AD9361_InitParam, mgc_split_table_ctrl_inp_gain_mode), 1, 1,
     false, false},
    {"adi,agc-adc-large-overload-exceed-counter",
     offsetof(AD9361_InitParam, agc_adc_large_overload_exceed_counter), 1, 1,
     false, false},
    {"adi,agc-adc-large-overload-inc-steps",
     offsetof(AD9361_InitParam, agc_adc_large_overload_inc_steps), 1, 1, false,
     false},
    {"adi,agc-adc-lmt-small-overload-prevent-gain-inc-enable",
     offsetof(AD9361_InitParam,
	      agc_adc_lmt_small_overload_prevent_gain_inc_enable),
     1, 1, true, false},
    {"adi,agc-adc-small-overload-exceed-counter",
     offsetof(AD9361_InitParam, agc_adc_small_overload_exceed_counter), 1, 1,
     false, false},
    {"adi,agc-dig-gain-step-size",
     offsetof(AD9361_InitParam, agc_dig_gain_step_size), 1, 1, false, false},
    {"adi,agc-dig-saturation-exceed-counter",
     offsetof(AD9361_InitParam, agc_dig_saturation_exceed_counter), 1, 1, false,
     false},
    {"adi,agc-gain-update-interval-us",
     offsetof(AD9361_InitParam, agc_gain_update_interval_us), 4, 1, false,
     false},
    {"adi,agc-immed-gain-change-if-large-adc-overload-enable",
     offsetof(AD9361_InitParam,
	      agc_immed_gain_change_if_large_adc_overload_enable),
     1, 1, true, false},
    {"adi,agc-immed-gain-change-if-large-lmt-overload-enable",
     offsetof(AD9361_InitParam,
	      agc_immed_gain_change_if_large_lmt_overload_enable),
     1, 1, true, false},
    {"adi,agc-inner-thresh-high",
     offsetof(AD9361_InitParam, agc_inner_thresh_high), 1, 1, false, false},
    {"adi,agc-inner-thresh-high-dec-steps",
     offsetof(AD9361_InitParam, agc_inner_thresh_high_dec_steps), 1, 1, false,
     false},
    {"adi,agc-inner-thresh-low",
     offsetof(AD9361_InitParam, agc_inner_thresh_low), 1, 1, false, false},
    {"adi,agc-inner-thresh-low-inc-steps",
     offsetof(AD9361_InitParam, agc_inner_thresh_low_inc_steps), 1, 1, false,
     false},
    {"adi,agc-lmt-overload-large-exceed-counter",
     offsetof(AD9361_InitParam, agc_lmt_overload_large_exceed_counter), 1, 1,
     false, false},
    {"adi,agc-lmt-overload-large-inc-steps",
     offsetof(AD9361_InitParam, agc_lmt_overload_large_inc_steps), 1, 1, false,
     false},
    {"adi,agc-lmt-overload-small-exceed-counter",
     offsetof(AD9361_InitParam, agc_lmt_overload_small_exceed_counter), 1, 1,
     false, false},
    {"adi,agc-outer-thresh-high",
     offsetof(AD9361_InitParam, agc_outer_thresh_high), 1, 1, false, false},
    {"adi,agc-outer-thresh-high-dec-steps",
     offsetof(AD9361_InitParam, agc_outer_thresh_high_dec_steps), 1, 1, false,
     false},
    {"adi,agc-outer-thresh-low",
     offsetof(AD9361_InitParam, agc_outer_thresh_low), 1, 1, false, false},
    {"adi,agc-outer-thresh-low-inc-steps",
     offsetof(AD9361_InitParam, agc_outer_thresh_low_inc_steps), 1, 1, false,
     false},
    {"adi,agc-attack-delay-extra-margin-us",
     offsetof(AD9361_InitParam, agc_attack_delay_extra_margin_us), 4, 1, false,
     false},
    {"adi,agc-sync-for-gain-counter-enable",
     offsetof(AD9361_InitParam, agc_sync_for_gain_counter_enable), 1, 1, true,
     false},
    {"adi,fagc-dec-pow-measurement-duration",
     offsetof(AD9361_InitParam, fagc_dec_pow_measuremnt_duration), 4, 1, false,
     false},
    {"adi,fagc-state-wait-time-ns",
     offsetof(AD9361_InitParam, fagc_state_wait_time_ns), 4, 1, false, false},
    {"adi,fagc-allow-agc-gain-increase-enable",
     offsetof(AD9361_InitParam, fagc_allow_agc_gain_increase), 1, 1, true,
     false},
    {"adi,fagc-lp-thresh-increment-time",
     offsetof(AD9361_InitParam, fagc_lp_thresh_increment_time), 4, 1, false,
     false},
    {"adi,fagc-lp-thresh-increment-steps",
     offsetof(AD9361_InitParam, fagc_lp_thresh_increment_steps), 4, 1, false,
     false},
    {"adi,fagc-lock-level-lmt-gain-increase-enable",
     offsetof(AD9361_InitParam, fagc_lock_level_lmt_gain_increase_en), 1, 1,
     true, false},
    {"adi,fagc-lock-level-gain-increase-upper-limit",
     offsetof(AD9361_InitParam, fagc_lock_level_gain_increase_upper_limit), 4,
     1, false, false},
    {"adi,fagc-lpf-final-settling-steps",
     offsetof(AD9361_InitParam, fagc_lpf_final_settling_steps), 4, 1, false,
     false},
    {"adi,fagc-lmt-final-settling-steps",
     offsetof(AD9361_InitParam, fagc_lmt_final_settling_steps), 4, 1, false,
     false},
    {"adi,fagc-final-overrange-count",
     offsetof(AD9361_InitParam, fagc_final_overrange_count), 4, 1, false,
     false},
    {"adi,fagc-gain-increase-after-gain-lock-enable",
     offsetof(AD9361_InitParam, fagc_gain_increase_after_gain_lock_en), 1, 1,
     true, false},
    {"adi,fagc-gain-index-type-after-exit-rx-mode",
     offsetof(AD9361_InitParam, fagc_gain_index_type_after_exit_rx_mode), 4, 1,
     false, false},
    {"adi,fagc-use-last-lock-level-for-set-gain-enable",
     offsetof(AD9361_InitParam, fagc_use_last_lock_level_for_set_gain_en), 1, 1,
     true, false},
    {"adi,fagc-rst-gla-stronger-sig-thresh-exceeded-enable",
     offsetof(AD9361_InitParam, fagc_rst_gla_stronger_sig_thresh_exceeded_en),
     1, 1, true, false},
    {"adi,fagc-optimized-gain-offset",
     offsetof(AD9361_InitParam, fagc_optimized_gain_offset), 4, 1, false,
     false},
    {"adi,fagc-rst-gla-stronger-sig-thresh-above-ll",
     offsetof(AD9361_InitParam, fagc_rst_gla_stronger_sig_thresh_above_ll), 4,
     1, false, false},
    {"adi,fagc-rst-gla-engergy-lost-sig-thresh-exceeded-enable",
     offsetof(AD9361_InitParam,
	      fagc_rst_gla_engergy_lost_sig_thresh_exceeded_en),
     1, 1, true, false},
    {"adi,fagc-rst-gla-engergy-lost-goto-optim-gain-enable",
     offsetof(AD9361_InitParam, fagc_rst_gla_engergy_lost_goto_optim_gain_en),
     1, 1, true, false},
    {"adi,fagc-rst-gla-engergy-lost-sig-thresh-below-ll",
     offsetof(AD9361_InitParam, fagc_rst_gla_engergy_lost_sig_thresh_below_ll),
     4, 1, false, false},
    {"adi,fagc-energy-lost-stronger-sig-gain-lock-exit-cnt",
     offsetof(AD9361_InitParam,
	      fagc_energy_lost_stronger_sig_gain_lock_exit_cnt),
     4, 1, false, false},
    {"adi,fagc-rst-gla-large-adc-overload-enable",
     offsetof(AD9361_InitParam, fagc_rst_gla_large_adc_overload_en), 1, 1, true,
     false},
    {"adi,fagc-rst-gla-large-lmt-overload-enable",
     offsetof(AD9361_InitParam, fagc_rst_gla_large_lmt_overload_en), 1, 1, true,
     false},
    {"adi,fagc-rst-gla-en-agc-pulled-high-enable",
     offsetof(AD9361_InitParam, fagc_rst_gla_en_agc_pulled_high_en), 1, 1, true,
     false},
    {"adi,fagc-rst-gla-if-en-agc-pulled-high-mode",
     offsetof(AD9361_InitParam, fagc_rst_gla_if_en_agc_pulled_high_mode), 4, 1,
     false, false},
    {"adi,fagc-power-measurement-duration-in-state5",
     offsetof(AD9361_InitParam, fagc_power_measurement_duration_in_state5), 4,
     1, false, false},
    {"adi,fagc-adc-large-overload-inc-steps",
     offsetof(AD9361_InitParam, fagc_large_overload_inc_steps), 4, 1, false,
     false},
    {"adi,rssi-delay", offsetof(AD9361_InitParam, rssi_delay), 4, 1, false,
     false},
    {"adi,rssi-duration", offsetof(AD9361_InitParam, rssi_duration), 4, 1,
     false, false},
    {"adi,rssi-restart-mode", offsetof(AD9361_InitParam, rssi_restart_mode), 1,
     1, false, false},
    {"adi,rssi-unit-is-rx-samples-enable",
     offsetof(AD9361_InitParam, rssi_unit_is_rx_samples_enable), 1, 1, true,
     false},
    {"adi,rssi-wait", offsetof(AD9361_InitParam, rssi_wait), 4, 1, false,
     false},
    {"adi,aux-adc-decimation", offsetof(AD9361_InitParam, aux_adc_decimation),
     4, 1, false, false},
    {"adi,aux-adc-rate", offsetof(AD9361_InitParam, aux_adc_rate), 4, 1, false,
     false},
    {"adi,aux-dac-manual-mode-enable",
     offsetof(AD9361_InitParam, aux_dac_manual_mode_enable), 1, 1, true, false},
    {"adi,aux-dac1-default-value-mV",
     offsetof(AD9361_InitParam, aux_dac1_default_value_mV), 4, 1, false, false},
    {"adi,aux-dac1-active-in-rx-enable",
     offsetof(AD9361_InitParam, aux_dac1_active_in_rx_enable), 1, 1, true,
     false},
    {"adi,aux-dac1-active-in-tx-enable",
     offsetof(AD9361_InitParam, aux_dac1_active_in_tx_enable), 1, 1, true,
     false},
    {"adi,aux-dac1-active-in-alert-enable",
     offsetof(AD9361_InitParam, aux_dac1_active_in_alert_enable), 1, 1, true,
     false},
    {"adi,aux-dac1-rx-delay-us",
     offsetof(AD9361_InitParam, aux_dac1_rx_delay_us), 4, 1, false, false},
    {"adi,aux-dac1-tx-delay-us",
     offsetof(AD9361_InitParam, aux_dac1_tx_delay_us), 4, 1, false, false},
    {"adi,aux-dac2-default-value-mV",
     offsetof(AD9361_InitParam, aux_dac2_default_value_mV), 4, 1, false, false},
    {"adi,aux-dac2-active-in-rx-enable",
     offsetof(AD9361_InitParam, aux_dac2_active_in_rx_enable), 1, 1, true,
     false},
    {"adi,aux-dac2-active-in-tx-enable",
     offsetof(AD9361_InitParam, aux_dac2_active_in_tx_enable), 1, 1, true,
     false},
    {"adi,aux-dac2-active-in-alert-enable",
     offsetof(AD9361_InitParam, aux_dac2_active_in_alert_enable), 1, 1, true,
     false},
    {"adi,aux-dac2-rx-delay-us",
     offsetof(AD9361_InitParam, aux_dac2_rx_delay_us), 4, 1, false, false},
    {"adi,aux-dac2-tx-delay-us",
     offsetof(AD9361_InitParam, aux_dac2_tx_delay_us), 4, 1, false, false},
    {"adi,temp-sense-decimation",
     offsetof(AD9361_InitParam, temp_sense_decimation), 4, 1, false, false},
    {"adi,temp-sense-measurement-interval-ms",
     offsetof(AD9361_InitParam, temp_sense_measurement_interval_ms), 2, 1,
     false, false},
    {"adi,temp-sense-offset-signed",
     offsetof(AD9361_InitParam, temp_sense_offset_signed), 1, 1, false, true},
    {"adi,temp-sense-periodic-measurement-enable",
     offsetof(AD9361_InitParam, temp_sense_periodic_measurement_enable), 1, 1,
     true, false},
    {"adi,ctrl-outs-enable-mask",
     offsetof(AD9361_InitParam, ctrl_outs_enable_mask), 1, 1, false, false},
    {"adi,ctrl-outs-index", offsetof(AD9361_InitParam, ctrl_outs_index), 1, 1,
     false, false},
    {"adi,elna-settling-delay-ns",
     offsetof(AD9361_InitParam, elna_settling_delay_ns), 4, 1, false, false},
    {"adi,elna-gain-mdB", offsetof(AD9361_InitParam, elna_gain_mdB), 4, 1,
     false, false},
    {"adi,elna-bypass-loss-mdB",
     offsetof(AD9361_InitParam, elna_bypass_loss_mdB), 4, 1, false, false},
    {"adi,elna-rx1-gpo0-control-enable",
     offsetof(AD9361_InitParam, elna_rx1_gpo0_control_enable), 1, 1, true,
     false},
    {"adi,elna-rx2-gpo1-control-enable",
     offsetof(AD9361_InitParam, elna_rx2_gpo1_control_enable), 1, 1, true,
     false},
    {"adi,elna-gaintable-all-index-enable",
     offsetof(AD9361_InitParam, elna_gaintable_all_index_enable), 1, 1, true,
     false},
    {"adi,digital-interface-tune-skip-mode",
     offsetof(AD9361_InitParam, digital_interface_tune_skip_mode), 1, 1, false,
     false},
    {"adi,digital-interface-tune-fir-disable",
     offsetof(AD9361_InitParam, digital_interface_tune_fir_disable), 1, 1,
     true, false},
    {"adi,pp-tx-swap-enable", offsetof(AD9361_InitParam, pp_tx_swap_enable), 1,
     1, true, false},
    {"adi,pp-rx-swap-enable", offsetof(AD9361_InitParam, pp_rx_swap_enable), 1,
     1, true, false},
    {"adi,tx-channel-swap-enable",
     offsetof(AD9361_InitParam, tx_channel_swap_enable), 1, 1, true, false},
    {"adi,rx-channel-swap-enable",
     offsetof(AD9361_InitParam, rx_channel_swap_enable), 1, 1, true, false},
    {"adi,rx-frame-pulse-mode-enable",
     offsetof(AD9361_InitParam, rx_frame_pulse_mode_enable), 1, 1, true, false},
    {"adi,2t2r-timing-enable",
     offsetof(AD9361_InitParam, two_t_two_r_timing_enable), 1, 1, true, false},
    {"adi,invert-data-bus-enable",
     offsetof(AD9361_InitParam, invert_data_bus_enable), 1, 1, true, false},
    {"adi,invert-data-clk-enable",
     offsetof(AD9361_InitParam, invert_data_clk_enable), 1, 1, true, false},
    {"adi,fdd-alt-word-order-enable",
     offsetof(AD9361_InitParam, fdd_alt_word_order_enable), 1, 1, true, false},
    {"adi,invert-rx-frame-enable",
     offsetof(AD9361_InitParam, invert_rx_frame_enable), 1, 1, true, false},
    {"adi,fdd-rx-rate-2tx-enable",
     offsetof(AD9361_InitParam, fdd_rx_rate_2tx_enable), 1, 1, true, false},
    {"adi,swap-ports-enable", offsetof(AD9361_InitParam, swap_ports_enable), 1,
     1, true, false},
    {"adi,single-data-rate-enable",
     offsetof(AD9361_InitParam, single_data_rate_enable), 1, 1, true, false},
    {"adi,lvds-mode-enable", offsetof(AD9361_InitParam, lvds_mode_enable), 1, 1,
     true, false},
    {"adi,half-duplex-mode-enable",
     offsetof(AD9361_InitParam, half_duplex_mode_enable), 1, 1, true, false},
    {"adi,single-port-mode-enable",
     offsetof(AD9361_InitParam, single_port_mode_enable), 1, 1, true, false},
    {"adi,full-port-enable", offsetof(AD9361_InitParam, full_port_enable), 1, 1,
     true, false},
    {"adi,full-duplex-swap-bits-enable",
     offsetof(AD9361_InitParam, full_duplex_swap_bits_enable), 1, 1, true,
     false},
    {"adi,delay-rx-data", offsetof(AD9361_InitParam, delay_rx_data), 4, 1,
     false, false},
    {"adi,rx-data-clock-delay", offsetof(AD9361_InitParam, rx_data_clock_delay),
     4, 1, false, false},
    {"adi,rx-data-delay", offsetof(AD9361_InitParam, rx_data_delay), 4, 1,
     false, false},
    {"adi,tx-fb-clock-delay", offsetof(AD9361_InitParam, tx_fb_clock_delay), 4,
     1, false, false},
    {"adi,tx-data-delay", offsetof(AD9361_InitParam, tx_data_delay), 4, 1,
     false, false},
    {"adi,lvds-bias-mV", offsetof(AD9361_InitParam, lvds_bias_mV), 4, 1, false,
     false},
    {"adi,lvds-rx-onchip-termination-enable",
     offsetof(AD9361_InitParam, lvds_rx_onchip_termination_enable), 1, 1, true,
     false},
    {"adi,rx1-rx2-phase-inversion-enable",
     offsetof(AD9361_InitParam, rx1rx2_phase_inversion_en), 1, 1, true, false},
    {"adi,lvds-invert1-control",
     offsetof(AD9361_InitParam, lvds_invert1_control), 1, 1, false, false},
    {"adi,lvds-invert2-control",
     offsetof(AD9361_InitParam, lvds_invert2_control), 1, 1, false, false},
    {"adi,gpo-manual-mode-enable",
     offsetof(AD9361_InitParam, gpo_manual_mode_enable), 1, 1, true, false},
    {"adi,gpo-manual-mode-enable-mask",
     offsetof(AD9361_InitParam, gpo_manual_mode_enable_mask), 4, 1, false,
     false},
    {"adi,gpo0-inactive-state-high-enable",
     offsetof(AD9361_InitParam, gpo0_inactive_state_high_enable), 1, 1, true,
     false},
    {"adi,gpo1-inactive-state-high-enable",
     offsetof(AD9361_InitParam, gpo1_inactive_state_high_enable), 1, 1, true,
     false},
    {"adi,gpo2-inactive-state-high-enable",
     offsetof(AD9361_InitParam, gpo2_inactive_state_high_enable), 1, 1, true,
     false},
    {"adi,gpo3-inactive-state-high-enable",
     offsetof(AD9361_InitParam, gpo3_inactive_state_high_enable), 1, 1, true,
     false},
    {"adi,gpo0-slave-rx-enable",
     offsetof(AD9361_InitParam, gpo0_slave_rx_enable), 1, 1, true, false},
    {"adi,gpo0-slave-tx-enable",
     offsetof(AD9361_InitParam, gpo0_slave_tx_enable), 1, 1, true, false},
    {"adi,gpo1-slave-rx-enable",
     offsetof(AD9361_InitParam, gpo1_slave_rx_enable), 1, 1, true, false},
    {"adi,gpo1-slave-tx-enable",
     offsetof(AD9361_InitParam, gpo1_slave_tx_enable), 1, 1, true, false},
    {"adi,gpo2-slave-rx-enable",
     offsetof(AD9361_InitParam, gpo2_slave_rx_enable), 1, 1, true, false},
    {"adi,gpo2-slave-tx-enable",
     offsetof(AD9361_InitParam, gpo2_slave_tx_enable), 1, 1, true, false},
    {"adi,gpo3-slave-rx-enable",
     offsetof(AD9361_InitParam, gpo3_slave_rx_enable), 1, 1, true, false},
    {"adi,gpo3-slave-tx-enable",
     offsetof(AD9361_InitParam, gpo3_slave_tx_enable), 1, 1, true, false},
    {"adi,gpo0-rx-delay-us", offsetof(AD9361_InitParam, gpo0_rx_delay_us), 1, 1,
     false, false},
    {"adi,gpo0-tx-delay-us", offsetof(AD9361_InitParam, gpo0_tx_delay_us), 1, 1,
     false, false},
    {"adi,gpo1-rx-delay-us", offsetof(AD9361_InitParam, gpo1_rx_delay_us), 1, 1,
     false, false},
    {"adi,gpo1-tx-delay-us", offsetof(AD9361_InitParam, gpo1_tx_delay_us), 1, 1,
     false, false},
    {"adi,gpo2-rx-delay-us", offsetof(AD9361_InitParam, gpo2_rx_delay_us), 1, 1,
     false, false},
    {"adi,gpo2-tx-delay-us", offsetof(AD9361_InitParam, gpo2_tx_delay_us), 1, 1,
     false, false},
    {"adi,gpo3-rx-delay-us", offsetof(AD9361_InitParam, gpo3_rx_delay_us), 1, 1,
     false, false},
    {"adi,gpo3-tx-delay-us", offsetof(AD9361_InitParam, gpo3_tx_delay_us), 1, 1,
     false, false},
    {"adi,txmon-low-high-thresh",
     offsetof(AD9361_InitParam, low_high_gain_threshold_mdB), 4, 1, false,
     false},
    {"adi,txmon-low-gain", offsetof(AD9361_InitParam, low_gain_dB), 4, 1, false,
     false},
    {"adi,txmon-high-gain", offsetof(AD9361_InitParam, high_gain_dB), 4, 1,
     false, false},
    {"adi,txmon-dc-tracking-enable",
     offsetof(AD9361_InitParam, tx_mon_track_en), 1, 1, true, false},
    {"adi,txmon-one-shot-mode-enable",
     offsetof(AD9361_InitParam, one_shot_mode_en), 1, 1, true, false},
    {"adi,txmon-delay", offsetof(AD9361_InitParam, tx_mon_delay), 4, 1, false,
     false},
    {"adi,txmon-duration", offsetof(AD9361_InitParam, tx_mon_duration), 4, 1,
     false, false},
    {"adi,txmon-1-front-end-gain",
     offsetof(AD9361_InitParam, tx1_mon_front_end_gain), 4, 1, false, false},
    {"adi,txmon-2-front-end-gain",
     offsetof(AD9361_InitParam, tx2_mon_front_end_gain), 4, 1, false, false},
    {"adi,txmon-1-lo-cm", offsetof(AD9361_InitParam, tx1_mon_lo_cm), 4, 1,
     false, false},
    {"adi,txmon-2-lo-cm", offsetof(AD9361_InitParam, tx2_mon_lo_cm), 4, 1,
     false, false},
};

static int
ad9361_native_params_read(int phandle, uint32_t reference,
    AD9361_InitParam *params, bool tx)
{
	const struct ad9361_property *property;
	uint8_t raw[24], *dest, v8;
	uint16_t v16;
	uint32_t v32;
	uint64_t value;
	unsigned int p, index, cell;
	int length;

	if (params == NULL || phandle == 0 || reference == 0)
		return EINVAL;
	if (tx) {
		if (!of_hasprop(phandle, "adi,tx-enable") ||
		    of_hasprop(phandle, "adi,rx-only"))
			return EOPNOTSUPP;
	} else if (!of_hasprop(phandle, "adi,rx-only") ||
	    of_hasprop(phandle, "adi,tx-enable")) {
		return EOPNOTSUPP;
	}
	*params = defaults;
	{
		static const char * const ad9363a[] = { "adi,ad9363a", NULL };

		if (of_compatible(phandle, ad9363a))
			params->dev_sel = ID_AD9363A;
	}
	params->reference_clk_rate = reference;
	for (p = 0; p < __arraycount(properties); p++) {
		property = &properties[p];
		dest = (uint8_t *)params + property->offset;
		length = OF_getproplen(phandle, property->name);
		if (length < 0) {
			/* DT boolean properties are true by presence, false
			 * otherwise. */
			if (property->boolean)
				memset(dest, 0, property->width);
			continue;
		}
		if (length == 0 && property->boolean) {
			value = 1;
		} else {
			cell = property->width == 8 ? 8 : 4;
			if (length != (int)(cell * property->count) ||
			    length > (int)sizeof(raw) ||
			    OF_getprop(phandle, property->name, raw, length) !=
				length)
				return EINVAL;
			value = 0;
		}
		for (index = 0; index < property->count; index++) {
			if (length != 0)
				value = property->width == 8
					    ? be64dec(raw + index * 8)
					    : be32dec(raw + index * 4);
			if (property->boolean && value > 1)
				return EINVAL;
			if (property->width < 4 &&
			    value >= (1ULL << (property->width * 8)) &&
			    !(property->signed_value &&
			      value >= 0x100000000ULL -
					   (1ULL << (property->width * 8 - 1))))
				return EINVAL;
			switch (property->width) {
			case 1:
				v8 = value;
				memcpy(dest, &v8, sizeof(v8));
				break;
			case 2:
				v16 = value;
				memcpy(dest, &v16, sizeof(v16));
				break;
			case 4:
				v32 = value;
				memcpy(dest, &v32, sizeof(v32));
				break;
			case 8:
				memcpy(dest, &value, sizeof(value));
				break;
			default:
				return EINVAL;
			}
			dest += property->width;
		}
	}
	if (params->aux_adc_rate == 0 || params->rssi_duration == 0)
		return EINVAL;
	/* Native control cannot delegate state or transmitter control to pins. */
	if (!params->frequency_division_duplex_mode_enable ||
	    (!params->two_rx_two_tx_mode_enable &&
	    (tx || params->one_rx_one_tx_mode_use_rx_num != 1)) ||
	    params->frequency_division_duplex_independent_mode_enable ||
	    params->ensm_enable_txnrx_control_enable ||
	    params->external_tx_lo_enable || params->external_rx_lo_enable ||
	    params->tx_fastlock_pincontrol_enable)
		return EOPNOTSUPP;
	params->tx_attenuation_mdB = 89750;
	params->rx_only = !tx;
	params->native_tx_fixed_delays = tx;
	if (tx && (!params->lvds_mode_enable ||
	    params->single_data_rate_enable || params->single_port_mode_enable ||
	    params->half_duplex_mode_enable || params->fdd_rx_rate_2tx_enable ||
	    params->rx_fastlock_pincontrol_enable ||
	    !of_hasprop(phandle, "adi,rx-data-clock-delay") ||
	    !of_hasprop(phandle, "adi,rx-data-delay") ||
	    !of_hasprop(phandle, "adi,tx-fb-clock-delay") ||
	    !of_hasprop(phandle, "adi,tx-data-delay") ||
	    params->rx_data_clock_delay > 15 || params->rx_data_delay > 15 ||
	    params->tx_fb_clock_delay > 15 || params->tx_data_delay > 15))
		return EOPNOTSUPP;
	return 0;
}

int
ad9361_native_params_from_fdt(int phandle, uint32_t reference,
    AD9361_InitParam *params)
{

	return ad9361_native_params_read(phandle, reference, params, false);
}

int
ad9361_native_params_from_fdt_tx(int phandle, uint32_t reference,
    AD9361_InitParam *params)
{

	return ad9361_native_params_read(phandle, reference, params, true);
}
