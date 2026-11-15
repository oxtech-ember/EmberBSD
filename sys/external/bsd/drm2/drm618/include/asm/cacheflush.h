/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: the includes of dist618/drm/i915/gt/intel_engine.h
 * and the cache calls of the i915 files (clflush, wbinvd_on_all_cpus); the GPL original was not read.
 *
 * x86 cache maintenance of the Linux sources: the instructions are in the
 * layer's asm/cpufeature.h (clflush), asm/processor.h (clflushopt) and asm/smp.h (wbinvd_on_all_cpus).
 */
#ifndef _DRM618_ASM_CACHEFLUSH_H_
#define _DRM618_ASM_CACHEFLUSH_H_

#include <asm/cpufeature.h>
#include <asm/processor.h>
#include <asm/smp.h>

#endif
