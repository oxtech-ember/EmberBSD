/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: dist618/drm/i915/intel_cpu_info.c (X86_MATCH_VFM, struct x86_cpu_id,
 * x86_match_cpu); NetBSD sys/arch/x86/include/cpu.h and specialreg.h (the cpuid signature); the GPL original was not read.
 */
#ifndef _DRM618_ASM_CPU_DEVICE_ID_H_
#define _DRM618_ASM_CPU_DEVICE_ID_H_

#include <sys/cpu.h>
#include <machine/cputypes.h>
#include <machine/cpuvar.h>
#include <x86/specialreg.h>

#include <linux/types.h>

/* a vendor, family and model in one number: vendor << 16 | family << 8 | model (Intel is vendor 0) */
#define	IFM(_family, _model)	(((_family) << 8) | (_model))
#define	VFM_MODEL(vfm)		((vfm) & 0xff)
#define	VFM_FAMILY(vfm)		(((vfm) >> 8) & 0xff)

struct x86_cpu_id {
	unsigned int	vfm;			/* 0: the end of a table */
	unsigned long	driver_data;
};

#define	X86_MATCH_VFM(_vfm, _data)					\
	{ .vfm = (_vfm), .driver_data = (unsigned long)(_data) }

/* the entry of the table that is the CPU of this machine, Intel only; NULL if none */
static inline const struct x86_cpu_id *
x86_match_cpu(const struct x86_cpu_id *table)
{
	uint32_t sig = curcpu()->ci_signature;
	unsigned int family = CPUID_TO_FAMILY(sig), model = CPUID_TO_MODEL(sig);

	if (cpu_vendor != CPUVENDOR_INTEL)
		return NULL;
	for (; table->vfm != 0; table++)
		if (VFM_FAMILY(table->vfm) == family &&
		    VFM_MODEL(table->vfm) == model)
			return table;
	return NULL;
}

#endif
