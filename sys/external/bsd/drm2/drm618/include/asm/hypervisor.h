/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: dist618/drm/i915/i915_utils.h (i915_run_as_guest); NetBSD
 * sys/arch/x86/include/cpu.h (vm_guest); the GPL original was not read.
 */
#ifndef _DRM618_ASM_HYPERVISOR_H_
#define _DRM618_ASM_HYPERVISOR_H_

#include_next <asm/hypervisor.h>

#include <sys/cpu.h>

enum x86_hypervisor_type {
	X86_HYPER_NATIVE = 0,
	X86_HYPER_VMWARE,
	X86_HYPER_MS_HYPERV,
	X86_HYPER_XEN_PV,
	X86_HYPER_XEN_HVM,
	X86_HYPER_KVM,
	X86_HYPER_JAILHOUSE,
	X86_HYPER_ACRN,
	X86_HYPER_BHYVE,
};

/*
 * Only the native case is told apart (the kernel's detection says whether it
 * runs on a hypervisor, not which): X86_HYPER_NATIVE is true when there is
 * none, any other type is false.
 */
static inline bool
hypervisor_is_type(enum x86_hypervisor_type type)
{

	return type == X86_HYPER_NATIVE && vm_guest == VM_GUEST_NO;
}

#endif
