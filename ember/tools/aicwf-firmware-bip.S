/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2026 EmberBSD contributors.
 * All rights reserved.
 *
 * Independently authored, AI-assisted interoperability code.
 * The external U02 image is an input. This file contains no vendor payload.
 */

.syntax unified
.cpu cortex-m4
.thumb

/* Fixed external Thumb entry points in the admitted U02 image. */
.equ AICWF_FILL, 0x1204cd
.equ AICWF_RANDOM, 0x153d85
.equ AICWF_CLASSIFY, 0x165db5
.equ AICWF_MMIE, 0x1541e5
.equ AICWF_CMAC, 0x16651d

.section .aicwf_bip_key,"ax",%progbits
.balign 4
.global aicwf_bip_key
.type aicwf_bip_key,%function
.thumb_func
aicwf_bip_key:
	/* r4 is the selected key context. r6 is its VIF. */
	push	{r4-r10, lr}
	mov	r5, r0
	mov	r8, r1
	ldrb	r2, [r5, #41]
	movw	r3, #0x528
	muls	r2, r3
	ldr	r6, =0x18fb08
	add	r6, r2
	ldrb	r2, [r5]
	movs	r3, #0x78
	mla	r4, r3, r2, r6
	addw	r4, r4, #0x1e0
	ldrb	r7, [r5, #40]
	cmp	r7, #5
	bne	.Lkey_metadata
	/* The generic path does not change PRIMASK. */
	mrs	r9, primask
	cpsid	i
	ldrb	r0, [r4, #0x63]
	cbz	r0, .Lkey_new
	ldrb	r0, [r4, #0x60]
	cmp	r0, #5
	bne	.Lkey_new
	ldrb	r0, [r4, #0x74]
	cmp	r0, #16
	bne	.Lkey_new
	addw	r0, r4, #0x64
	adds	r1, r5, #8
	movs	r2, #4
.Lkey_equal:
	/* All four backup words must match before a same-key update. */
	ldr	r3, [r0], #4
	ldr	r10, [r1], #4
	cmp	r3, r10
	bne	.Lkey_new
	subs	r2, #1
	bne	.Lkey_equal
	/* Match the current hardware index returned by this caller. */
	strb	r8, [r4, #0x62]
	ldrd	r0, r1, [r4]
	ldr	r2, [r5, #24]
	ldrh	r3, [r5, #28]
	cmp	r1, r3
	it	eq
	cmpeq	r0, r2
	bhs	.Lkey_return
	strd	r2, r3, [r4]
	b	.Lkey_return
.Lkey_new:
	/* Invalidate before changing a live BIP context. */
	movs	r0, #0
	strb	r0, [r4, #0x63]
.Lkey_metadata:
	strb	r7, [r4, #0x60]
	ldrb	r0, [r5]
	strb	r0, [r4, #0x61]
	strb	r8, [r4, #0x62]
	mov	r0, r4
	movs	r1, #0
	cmp	r7, #5
	beq	.Lkey_local_zero
	movs	r2, #0x48
	ldr	r3, =AICWF_FILL
	blx	r3
	movs	r1, #0
	movs	r2, #0
	b	.Lkey_zero_done
.Lkey_local_zero:
	/* The BIP critical path makes no external clear/copy call. */
	movs	r2, #18
.Lkey_zero:
	str	r1, [r0], #4
	subs	r2, #1
	bne	.Lkey_zero
.Lkey_zero_done:
	strd	r1, r2, [r4, #0x48]
	cmp	r7, #5
	beq	.Lkey_bip
	cmp	r7, #1
	beq	.Lkey_tail
	cmp	r7, #4
	beq	.Lkey_constant
	cmp	r7, #0
	beq	.Lkey_random
	cmp	r7, #3
	bne	.Lkey_publish
.Lkey_random:
	ldr	r3, =AICWF_RANDOM
	blx	r3
	ubfx	r0, r0, #0, #24
	movs	r1, #0
	strd	r0, r1, [r4, #0x48]
	b	.Lkey_publish
.Lkey_constant:
	ldr	r0, =0x5c365c36
	mov	r1, r0
	strd	r0, r1, [r4, #0x48]
	b	.Lkey_publish
.Lkey_tail:
	adds	r1, r5, #24
	b	.Lkey_crypto
.Lkey_bip:
	ldr	r2, [r5, #24]
	ldrh	r3, [r5, #28]
	strd	r2, r3, [r4]
	adds	r1, r5, #8
.Lkey_crypto:
	ldm	r1, {r0-r3}
	str	r0, [r4, #0x50]
	str	r1, [r4, #0x54]
	str	r2, [r4, #0x58]
	str	r3, [r4, #0x5c]
.Lkey_publish:
	adds	r1, r5, #8
	ldm	r1, {r0-r3}
	str	r0, [r4, #0x64]
	str	r1, [r4, #0x68]
	str	r2, [r4, #0x6c]
	str	r3, [r4, #0x70]
	ldrb	r0, [r5, #4]
	strb	r0, [r4, #0x74]
	dmb
	addw	r0, r6, #0x4b0
	cmp	r7, #5
	ite	eq
	streq	r4, [r0, #4]
	strne	r4, [r0]
	movs	r0, #1
	strb	r0, [r4, #0x63]
.Lkey_return:
	/* Restore exactly the state sampled at the BIP transaction entry. */
	dmb
	cmp	r7, #5
	bne	.Lkey_pop
	msr	primask, r9
.Lkey_pop:
	pop	{r4-r10, pc}
.global aicwf_bip_key_code_end
aicwf_bip_key_code_end:
	.ltorg
.size aicwf_bip_key, .-aicwf_bip_key

.section .aicwf_bip_rx,"ax",%progbits
.balign 4
.global aicwf_bip_rx
.type aicwf_bip_rx,%function
.thumb_func
aicwf_bip_rx:
	/* Save the C ABI before classification and bounded MMIE lookup. */
	push	{r4-r11, lr}
	sub	sp, #20
	mov	r4, r1
	mov	r7, r2
	mov	r5, r3
	mov	r10, r0
	ldrb	r6, [r0, #8]
	ldrh	r11, [r0]
	mov	r0, r11
	ldrb	r1, [r4, r6]
	ldr	r3, =AICWF_CLASSIFY
	blx	r3
	cmp	r0, #0
	beq	.Lrx_ok
	ldrb	r3, [r10, #9]
	cmp	r3, #255
	beq	.Lrx_fail
	ldrb	r8, [r10, #10]
	cmp	r8, #255
	beq	.Lrx_fail
	ldr	r3, =0x18fb08
	movw	r0, #0x528
	mla	r0, r0, r8, r3
	ldr	r2, [r0, #0x4b0]
	cmp	r2, #0
	beq	.Lrx_ok
	ldrb	r2, [r10, #0x30]
	lsls	r2, #28
	bpl	.Lrx_unicast
	and	r3, r11, #0xfc
	cmp	r3, #0xd0
	bne	.Lrx_group
	ldrb	r2, [r4, r6]
	subs	r2, #13
	cmp	r2, #1
	bhi	.Lrx_group
	eor	r0, r11, #0x4000
	ubfx	r0, r0, #14, #1
	b	.Lrx_return
.Lrx_group:
	ldr	r2, [r0, #0x4b4]
	cmp	r2, #0
	beq	.Lrx_ok
	adds	r3, r6, #19
	cmp	r7, r3
	ble	.Lrx_fail
	cmp	r7, #42
	blo	.Lrx_fail
	adds	r0, r4, r6
	adds	r0, #2
	subs	r1, r7, #2
	subs	r1, r6
	uxth	r1, r1
	ldr	r3, =AICWF_MMIE
	blx	r3
	cmp	r0, #0
	beq	.Lrx_fail
	ldrh	r2, [r0, #2]
	subs	r2, #4
	cmp	r2, #1
	bhi	.Lrx_fail
	adds	r2, #4
	ldr	r3, =0x18fb08
	movw	r1, #0x528
	mla	r3, r1, r8, r3
	movs	r1, #0x78
	mla	r12, r1, r2, r3
	addw	r12, r12, #0x1e0
	/* Every fresh context read and the MIC commit share this mask. */
	mrs	r11, primask
	cpsid	i
	ldrb	r1, [r12, #0x63]
	cbz	r1, .Lrx_locked_fail
	ldrb	r1, [r12, #0x60]
	cmp	r1, #5
	bne	.Lrx_locked_fail
	ldrb	r1, [r12, #0x74]
	cmp	r1, #16
	bne	.Lrx_locked_fail
	/* Read all six little-endian packet-number bytes. */
	ldr	r2, [r0, #4]
	ldrh	r3, [r0, #8]
	strd	r2, r3, [sp, #8]
	ldrd	r9, r10, [r12]
	cmp	r10, r3
	it	eq
	cmpeq	r9, r2
	bhs	.Lrx_locked_fail
	ldr	r9, [r0, #10]
	ldr	r10, [r0, #14]
	movs	r3, #0
	str	r3, [r0, #10]
	str	r3, [r0, #14]
	str	r3, [sp]
	mov	r1, r4
	mov	r4, r12
	mov	r0, r4
	sub	r2, r7, #18
	mov	r3, r6
	ldr	r12, =AICWF_CMAC
	blx	r12
	cmp	r1, r10
	it	eq
	cmpeq	r0, r9
	bne	.Lrx_locked_fail
	/* A matching MIC is the only route to the receive-floor store. */
	ldrd	r2, r3, [sp, #8]
	strd	r2, r3, [r4]
	dmb
	msr	primask, r11
.Lrx_ok:
	movs	r0, #0
.Lrx_return:
	add	sp, #20
	pop	{r4-r11, pc}
.Lrx_locked_fail:
	msr	primask, r11
.Lrx_fail:
	movs	r0, #1
	movs	r3, #0
	strb	r3, [r5]
	b	.Lrx_return
.Lrx_unicast:
	ldrb	r3, [r10, #9]
	ldr	r2, =0x1895a8
	movw	r0, #0x2d0
	mla	r3, r0, r3, r2
	ldr	r3, [r3, #4]
	lsls	r3, #28
	bpl	.Lrx_ok
	tst	r11, #0x4000
	bne	.Lrx_ok
	and	r11, r11, #0xfc
	cmp	r11, #0xc0
	beq	.Lrx_special
	cmp	r11, #0xa0
	bne	.Lrx_fail
.Lrx_special:
	ldrb	r1, [r4, r6]
	subs	r1, #6
	cmp	r1, #1
	bhi	.Lrx_fail
	movs	r0, #1
	strb	r0, [r5]
	b	.Lrx_return
.global aicwf_bip_rx_code_end
aicwf_bip_rx_code_end:
	.ltorg
.size aicwf_bip_rx, .-aicwf_bip_rx

.section .note.GNU-stack,"",%progbits
