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

	.macro RX_STA_RX_CB_WRAP
	.globl __wrap_sta_rx_cb
	.type __wrap_sta_rx_cb,@function
__wrap_sta_rx_cb:
	la t0, espwifi_rx_chain_diag_active
	lw t1, 0(t0)
	beqz t1, 2f
	la t0, espwifi_rx_chain_sta_rx_cb_count
	lw t1, 0(t0)
	addi t1, t1, 1
	sw t1, 0(t0)
	/*
	 * a0 is the vendor ESF, not the 802.11 header.  The pinned S31 ROM
	 * ppRxPkt reads the header through ESF+4 -> buffer+4, with eight
	 * extra bytes when ESF flags at +0x24 contain 0x2000 (0x2f832ec0
	 * through 0x2f832ed0).  It then passes that same ESF to this callback
	 * at 0x2f832f64.  sta_rx_cb passes it as a1 to ROM sta_input, whose
	 * 0x2f83cf26..0x2f83cf4c instructions use the same header layout.
	 * Count only FC type; keep arguments, ra, sp, and FP registers intact.
	 */
	beqz a0, 2f
	lw t0, 4(a0)
	beqz t0, 2f
	lw t0, 4(t0)
	beqz t0, 2f
	lhu t1, 0x24(a0)
	li t2, 0x2000
	and t1, t1, t2
	beqz t1, 3f
	addi t0, t0, 8
3:
	lbu t2, 0(t0)
	/* The type bits already give a byte offset into four uint32_t's. */
	andi t2, t2, 0x0c
	la t0, espwifi_rx_chain_sta_rx_cb_type_count
	add t0, t0, t2
	lw t1, 0(t0)
	addi t1, t1, 1
	sw t1, 0(t0)
2:
	tail __real_sta_rx_cb
	.size __wrap_sta_rx_cb, .-__wrap_sta_rx_cb
	.endm

	RX_CHAIN_WRAP sta_input, espwifi_rx_chain_sta_input_count
	RX_STA_RX_CB_WRAP
	RX_CHAIN_WRAP ieee80211_crypto_decap, espwifi_rx_chain_crypto_decap_count
	RX_CHAIN_WRAP ieee80211_decap, espwifi_rx_chain_decap_count
	RX_CHAIN_WRAP sta_rx_eapol, espwifi_rx_chain_eapol_count
