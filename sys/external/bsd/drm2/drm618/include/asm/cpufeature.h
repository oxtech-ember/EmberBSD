/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: dist618/drm/i915/i915_memcpy.c
 * (static_cpu_has(X86_FEATURE_XMM4_1), boot_cpu_has(X86_FEATURE_HYPERVISOR)); NetBSD
 * x86/specialreg.h and sys/cpu.h (cpu_info_primary.ci_feat_val); the GPL original was not read.
 *
 * Shadow of the shared <asm/cpufeature.h> for the 6.18 sources: two more features.  The
 * shared one answers false for a feature it does not know, and the answer for these two
 * has to be the real one (SSE4.1 for the cached read copy, the hypervisor bit for a guest).
 */
#ifndef _DRM618_ASM_CPUFEATURE_H_
#define _DRM618_ASM_CPUFEATURE_H_

#define	static_cpu_has	drm618_shared_static_cpu_has
#include_next <asm/cpufeature.h>
#undef	static_cpu_has
#undef	boot_cpu_has

#if defined(__i386__) || defined(__x86_64__)

#define	X86_FEATURE_XMM4_1	2
#define	X86_FEATURE_HYPERVISOR	3

static inline bool
static_cpu_has(int feature)
{

	switch (feature) {
	case X86_FEATURE_XMM4_1:
		return (cpu_info_primary.ci_feat_val[1] & CPUID2_SSE41) != 0;
	case X86_FEATURE_HYPERVISOR:
		return (cpu_info_primary.ci_feat_val[1] & CPUID2_RAZ) != 0;
	default:
		return drm618_shared_static_cpu_has(feature);
	}
}

#define	boot_cpu_has	static_cpu_has

#endif

#endif
