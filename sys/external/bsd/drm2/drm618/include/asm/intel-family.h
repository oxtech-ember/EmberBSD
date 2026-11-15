/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: dist618/drm/i915/intel_cpu_info.c (the names it uses); the model
 * numbers of the Intel family 6 CPUs from Intel's published CPUID model tables; the GPL original was not read.
 * Only the models the i915 driver asks for are here.
 */
#ifndef _DRM618_ASM_INTEL_FAMILY_H_
#define _DRM618_ASM_INTEL_FAMILY_H_

#include <asm/cpu_device_id.h>

#define	INTEL_KABYLAKE_L	IFM(6, 0x8E)
#define	INTEL_KABYLAKE		IFM(6, 0x9E)
#define	INTEL_COMETLAKE		IFM(6, 0xA5)
#define	INTEL_ROCKETLAKE	IFM(6, 0xA7)
#define	INTEL_ALDERLAKE		IFM(6, 0x97)
#define	INTEL_ALDERLAKE_L	IFM(6, 0x9A)
#define	INTEL_RAPTORLAKE	IFM(6, 0xB7)
#define	INTEL_RAPTORLAKE_P	IFM(6, 0xBA)
#define	INTEL_RAPTORLAKE_S	IFM(6, 0xBF)

#endif
