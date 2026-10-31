/*	$NetBSD: fpu.c,v 1.3 2023/05/07 12:41:49 skrll Exp $	*/

/*-
 * Copyright (c) 2014 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Matt Thomas of 3am Software Foundry.
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

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: fpu.c,v 1.3 2023/05/07 12:41:49 skrll Exp $");

#include "opt_multiprocessor.h"
#include "opt_riscv_fpu.h"

#include "rv32-fpu.h"

#include <sys/param.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/cpu.h>
#include <sys/proc.h>
#include <sys/lwp.h>
#include <sys/kthread.h>
#include <sys/pcu.h>

#include <riscv/locore.h>
#include <riscv/frame.h>

static void fpu_state_save(lwp_t *);
static void fpu_state_load(lwp_t *, u_int);
static void fpu_state_release(lwp_t *);

static uint32_t
fpu_csr_read(void)
{
	uint32_t value;
	__asm volatile(".option push\n\t.option arch,+f\n\tfrcsr %0\n\t"
	    ".option pop" : "=r"(value) :: "memory");
	return value;
}

static void
fpu_csr_write(uint32_t value)
{
	__asm volatile(".option push\n\t.option arch,+f\n\tfscsr %0\n\t"
	    ".option pop" :: "r"(value) : "memory");
}

const pcu_ops_t pcu_fpu_ops = {
	.pcu_id = PCU_FPU,
	.pcu_state_save = fpu_state_save,
	.pcu_state_load = fpu_state_load,
	.pcu_state_release = fpu_state_release
};

static void
fpu_disable(void)
{
	csr_sstatus_clear(SR_FS);
}

static void
fpu_enable(void)
{
	csr_sstatus_clear(SR_FS);
	csr_sstatus_set(__SHIFTIN(SR_FS_CLEAN, SR_FS));
}

void
fpu_state_save(lwp_t *l)
{
//	struct trapframe * const tf = l->l_md.md_utf;
	struct pcb * const pcb = lwp_getpcb(l);
	struct fpreg * const fp = &pcb->pcb_fpregs;

	KASSERT(l->l_pcu_cpu[PCU_FPU] == curcpu());

	curcpu()->ci_ev_fpu_saves.ev_count++;

	// Enable FPU to save FP state
	fpu_enable();

	// Save FCSR
	fp->r_fcsr = fpu_csr_read();

	/* The public register slots remain 64 bits on an F-only CPU. */
#ifdef RISCV_FPU_SINGLE
	for (u_int i = 0; i < 32; i++)
		fp->r_fpreg[i].u_u64 = UINT64_C(0xffffffff00000000);
	__asm volatile(RISCV_FPU32_STORE :: "r"(fp->r_fpreg),
	    "i"(sizeof(fp->r_fpreg[0])) : "memory");
#else
	__asm volatile(RISCV_FPU64_STORE :: "r"(fp->r_fpreg),
	    "i"(sizeof(fp->r_fpreg[0])) : "memory");
#endif

	// Disable the FPU
	fpu_disable();
}

void
fpu_state_load(lwp_t *l, u_int flags)
{
	struct trapframe * const tf = l->l_md.md_utf;
	struct pcb * const pcb = lwp_getpcb(l);
	struct fpreg * const fp = &pcb->pcb_fpregs;

	KASSERT(l == curlwp);
//	KASSERT(l->l_pcu_cpu[PCU_FPU] == curcpu());

	// If this is the first time the state is being loaded, zero it first.
	if (__predict_false((flags & PCU_VALID) == 0)) {
		memset(fp, 0, sizeof(*fp));
	}

	/* Kernel threads have no user trapframe. */
	if ((l->l_flag & LW_SYSTEM) == 0) {
		tf->tf_sr &= ~SR_FS;
		tf->tf_sr |= __SHIFTIN(SR_FS_CLEAN, SR_FS);
	}

	// If this is a simple reenable, set the FPU enable flag and return
	if (flags & PCU_REENABLE) {
		curcpu()->ci_ev_fpu_reenables.ev_count++;
		if (l->l_flag & LW_SYSTEM_FPU)
			fpu_enable();
		return;
	}

	curcpu()->ci_ev_fpu_loads.ev_count++;


	// Enabling to load FP state.  Interrupts will remain on.
	fpu_enable();

	/* Restore the register width selected for this CPU family. */
#ifdef RISCV_FPU_SINGLE
	__asm volatile(RISCV_FPU32_LOAD :: "r"(fp->r_fpreg),
	    "i"(sizeof(fp->r_fpreg[0])) : "memory");
#else
	__asm volatile(RISCV_FPU64_LOAD :: "r"(fp->r_fpreg),
	    "i"(sizeof(fp->r_fpreg[0])) : "memory");
#endif

	// load FPCSR and disable FPU again
	fpu_csr_write(fp->r_fcsr);

	if (l->l_flag & LW_SYSTEM_FPU)
		fpu_enable();
	else
		fpu_disable();
}

void
fpu_state_release(lwp_t *l)
{
	if ((l->l_flag & LW_SYSTEM) == 0)
		l->l_md.md_utf->tf_sr &= ~SR_FS;
	fpu_disable();
}

void
kthread_fpu_enter_md(void)
{
	pcu_load(&pcu_fpu_ops);
}

void
kthread_fpu_exit_md(void)
{
	static const struct fpreg zero;
	struct cpu_info *ci;
	int s;

	/* The MI exit has dropped LW_SYSTEM_FPU and may have been preempted. */
	s = splhigh();
	ci = curcpu();
	if (curlwp->l_pcu_cpu[PCU_FPU] == ci &&
	    ci->ci_pcu_curlwp[PCU_FPU] == curlwp) {
		fpu_enable();
#ifdef RISCV_FPU_SINGLE
		__asm volatile(RISCV_FPU32_LOAD :: "r"(zero.r_fpreg),
		    "i"(sizeof(zero.r_fpreg[0])) : "memory");
#else
		__asm volatile(RISCV_FPU64_LOAD :: "r"(zero.r_fpreg),
		    "i"(sizeof(zero.r_fpreg[0])) : "memory");
#endif
		fpu_csr_write(0);
		fpu_disable();
	}
	splx(s);
	pcu_discard(&pcu_fpu_ops, curlwp, false);
}
