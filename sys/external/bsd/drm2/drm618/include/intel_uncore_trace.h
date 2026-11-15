/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Own implementation. Sources read: the 11 calls to trace_i915_reg_rw in the MIT files of dist618/drm/i915; the GPL original was not read. */
#ifndef _INTEL_UNCORE_TRACE_H_618
#define _INTEL_UNCORE_TRACE_H_618

#include "drm618_trace_nop.h"

#ifndef trace_i915_reg_rw
#define trace_i915_reg_rw(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_i915_reg_rw_enabled
#define trace_i915_reg_rw_enabled()	(0)
#endif

#endif
