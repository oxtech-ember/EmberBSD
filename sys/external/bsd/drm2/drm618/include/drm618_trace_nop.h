/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: the way the trace points are called in
 * the MIT files of dist618/drm/i915 (arguments of any scalar or pointer type,
 * an optional *_enabled() test); the GPL original was not read.
 *
 * Trace points are macros that evaluate nothing at run time: the arguments
 * are type checked (sizeof does not run its operand) and a *_enabled() test
 * says no.  A tracing facility, when there is one, replaces the trace headers.
 */
#ifndef _DRM618_TRACE_NOP_H_
#define _DRM618_TRACE_NOP_H_

#define DRM618_TRACE_NOP(...)	((void)sizeof(drm618_trace_args(0, ##__VA_ARGS__)))
static inline int
drm618_trace_args(int unused, ...)
{

	return unused;
}

#endif
