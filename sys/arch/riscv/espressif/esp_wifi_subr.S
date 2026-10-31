/*-
 * Copyright (c) 2026 The NetBSD Foundation, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Floating point and stack helpers for the ESP32-S31 radio code.
 *
 * The kernel is built without floating point and never touches the F
 * registers; the vendor's radio libraries use them.  Whoever enters that
 * code from a foreign context, or leaves it to sleep, saves and restores
 * the registers around the excursion with the two routines below.
 */

#include <machine/asm.h>

	.option	arch, +f

#define	SR_FS_MASK	0x6000

/*
 * void espwifi_fp_save(struct espwifi_fp *)
 *
 * Record whether floating point was enabled, enable it, save f0-f31 and
 * fcsr.
 */
ENTRY(espwifi_fp_save)
	csrr	t0, sstatus
	li	t1, SR_FS_MASK
	and	t2, t0, t1
	sw	t2, 132(a0)
	csrs	sstatus, t1
	fsw	f0, 0(a0)
	fsw	f1, 4(a0)
	fsw	f2, 8(a0)
	fsw	f3, 12(a0)
	fsw	f4, 16(a0)
	fsw	f5, 20(a0)
	fsw	f6, 24(a0)
	fsw	f7, 28(a0)
	fsw	f8, 32(a0)
	fsw	f9, 36(a0)
	fsw	f10, 40(a0)
	fsw	f11, 44(a0)
	fsw	f12, 48(a0)
	fsw	f13, 52(a0)
	fsw	f14, 56(a0)
	fsw	f15, 60(a0)
	fsw	f16, 64(a0)
	fsw	f17, 68(a0)
	fsw	f18, 72(a0)
	fsw	f19, 76(a0)
	fsw	f20, 80(a0)
	fsw	f21, 84(a0)
	fsw	f22, 88(a0)
	fsw	f23, 92(a0)
	fsw	f24, 96(a0)
	fsw	f25, 100(a0)
	fsw	f26, 104(a0)
	fsw	f27, 108(a0)
	fsw	f28, 112(a0)
	fsw	f29, 116(a0)
	fsw	f30, 120(a0)
	fsw	f31, 124(a0)
	frcsr	t0
	sw	t0, 128(a0)
	ret
END(espwifi_fp_save)

/*
 * void espwifi_fp_restore(const struct espwifi_fp *)
 *
 * Restore the registers and put the enable bits back as they were.
 */
ENTRY(espwifi_fp_restore)
	li	t1, SR_FS_MASK
	csrs	sstatus, t1
	flw	f0, 0(a0)
	flw	f1, 4(a0)
	flw	f2, 8(a0)
	flw	f3, 12(a0)
	flw	f4, 16(a0)
	flw	f5, 20(a0)
	flw	f6, 24(a0)
	flw	f7, 28(a0)
	flw	f8, 32(a0)
	flw	f9, 36(a0)
	flw	f10, 40(a0)
	flw	f11, 44(a0)
	flw	f12, 48(a0)
	flw	f13, 52(a0)
	flw	f14, 56(a0)
	flw	f15, 60(a0)
	flw	f16, 64(a0)
	flw	f17, 68(a0)
	flw	f18, 72(a0)
	flw	f19, 76(a0)
	flw	f20, 80(a0)
	flw	f21, 84(a0)
	flw	f22, 88(a0)
	flw	f23, 92(a0)
	flw	f24, 96(a0)
	flw	f25, 100(a0)
	flw	f26, 104(a0)
	flw	f27, 108(a0)
	flw	f28, 112(a0)
	flw	f29, 116(a0)
	flw	f30, 120(a0)
	flw	f31, 124(a0)
	lw	t0, 128(a0)
	fscsr	t0
	lw	t2, 132(a0)
	csrc	sstatus, t1
	csrs	sstatus, t2
	ret
END(espwifi_fp_restore)

/*
 * void espwifi_call_on_stack(void (*func)(void *), void *arg, void *top)
 *
 * Call func(arg) on another stack.  The radio code wants more stack than
 * a kernel thread has.
 */
ENTRY(espwifi_call_on_stack)
	addi	sp, sp, -16
	sw	ra, 12(sp)
	sw	s0, 8(sp)
	mv	s0, sp
	mv	sp, a2
	mv	t0, a0
	mv	a0, a1
	jalr	t0
	mv	sp, s0
	lw	ra, 12(sp)
	lw	s0, 8(sp)
	addi	sp, sp, 16
	ret
END(espwifi_call_on_stack)
