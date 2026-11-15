/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: dist618/drm/i915/gt/intel_rps.c (the functions it defines for the
 * IPS driver and the symbol_get of ips_link_to_i915_driver); the GPL original was not read.
 *
 * The interface between the Gen5 render standby code and the Intel IPS
 * platform driver, which does not exist here: symbol_get finds nothing.
 */
#ifndef _DRM618_PLATFORM_X86_INTEL_IPS_H_
#define _DRM618_PLATFORM_X86_INTEL_IPS_H_

#include <linux/types.h>

unsigned long i915_read_mch_val(void);
bool i915_gpu_raise(void);
bool i915_gpu_lower(void);
bool i915_gpu_busy(void);
bool i915_gpu_turbo_disable(void);
void ips_link_to_i915_driver(void);

/* there is no module symbol table: a symbol is never found */
#define	symbol_get(sym)		((__typeof__(&(sym)))NULL)
#define	symbol_put(sym)		((void)0)

#endif
