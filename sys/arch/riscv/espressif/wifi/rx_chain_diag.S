/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Temporary integer-only RX entry counters for the pinned ESP32-S31 vendor
 * object.  The producer enables these wrappers only for --vendor-rx-chain-once.
 */
	.section .text.rx_chain_diag,"ax"
	.p2align 2

	.macro RX_CHAIN_WRAP name, counter
	.globl __wrap_\name
	.type __wrap_\name,@function
__wrap_\name:
	la t0, espwifi_rx_chain_diag_active
	lw t1, 0(t0)
	beqz t1, 1f
	la t0, \counter
	lw t1, 0(t0)
	addi t1, t1, 1
	sw t1, 0(t0)
1:
	tail __real_\name
	.size __wrap_\name, .-__wrap_\name
	.endm

	RX_CHAIN_WRAP sta_input, espwifi_rx_chain_sta_input_count
	RX_CHAIN_WRAP ieee80211_crypto_decap, espwifi_rx_chain_crypto_decap_count
	RX_CHAIN_WRAP ieee80211_decap, espwifi_rx_chain_decap_count
	RX_CHAIN_WRAP sta_rx_eapol, espwifi_rx_chain_eapol_count
