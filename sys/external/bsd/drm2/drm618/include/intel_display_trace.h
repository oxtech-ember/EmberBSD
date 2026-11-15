/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Own implementation. Sources read: the calls to trace_intel_* and trace_*_wm in the MIT files of dist618/drm/i915/display; the GPL original was not read. */
#ifndef _INTEL_DISPLAY_TRACE_H_618
#define _INTEL_DISPLAY_TRACE_H_618

#include "drm618_trace_nop.h"

/* the sources rely on this header for the display structures (found by the errors of the files that include it) */
#include "intel_display_core.h"
#include "intel_display_types.h"
#include "intel_crtc.h"

#ifndef trace_intel_cpu_fifo_underrun
#define trace_intel_cpu_fifo_underrun(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_intel_cpu_fifo_underrun_enabled
#define trace_intel_cpu_fifo_underrun_enabled()	(0)
#endif
#ifndef trace_intel_pch_fifo_underrun
#define trace_intel_pch_fifo_underrun(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_intel_pch_fifo_underrun_enabled
#define trace_intel_pch_fifo_underrun_enabled()	(0)
#endif
#ifndef trace_vlv_wm
#define trace_vlv_wm(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_vlv_wm_enabled
#define trace_vlv_wm_enabled()	(0)
#endif
#ifndef trace_vlv_fifo_size
#define trace_vlv_fifo_size(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_vlv_fifo_size_enabled
#define trace_vlv_fifo_size_enabled()	(0)
#endif
#ifndef trace_intel_scaler_disable_arm
#define trace_intel_scaler_disable_arm(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_intel_scaler_disable_arm_enabled
#define trace_intel_scaler_disable_arm_enabled()	(0)
#endif
#ifndef trace_intel_plane_update_noarm
#define trace_intel_plane_update_noarm(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_intel_plane_update_noarm_enabled
#define trace_intel_plane_update_noarm_enabled()	(0)
#endif
#ifndef trace_intel_plane_update_arm
#define trace_intel_plane_update_arm(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_intel_plane_update_arm_enabled
#define trace_intel_plane_update_arm_enabled()	(0)
#endif
#ifndef trace_intel_plane_scaler_update_arm
#define trace_intel_plane_scaler_update_arm(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_intel_plane_scaler_update_arm_enabled
#define trace_intel_plane_scaler_update_arm_enabled()	(0)
#endif
#ifndef trace_intel_plane_disable_arm
#define trace_intel_plane_disable_arm(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_intel_plane_disable_arm_enabled
#define trace_intel_plane_disable_arm_enabled()	(0)
#endif
#ifndef trace_intel_plane_async_flip
#define trace_intel_plane_async_flip(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_intel_plane_async_flip_enabled
#define trace_intel_plane_async_flip_enabled()	(0)
#endif
#ifndef trace_intel_pipe_update_vblank_evaded
#define trace_intel_pipe_update_vblank_evaded(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_intel_pipe_update_vblank_evaded_enabled
#define trace_intel_pipe_update_vblank_evaded_enabled()	(0)
#endif
#ifndef trace_intel_pipe_update_start
#define trace_intel_pipe_update_start(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_intel_pipe_update_start_enabled
#define trace_intel_pipe_update_start_enabled()	(0)
#endif
#ifndef trace_intel_pipe_update_end
#define trace_intel_pipe_update_end(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_intel_pipe_update_end_enabled
#define trace_intel_pipe_update_end_enabled()	(0)
#endif
#ifndef trace_intel_pipe_scaler_update_arm
#define trace_intel_pipe_scaler_update_arm(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_intel_pipe_scaler_update_arm_enabled
#define trace_intel_pipe_scaler_update_arm_enabled()	(0)
#endif
#ifndef trace_intel_pipe_enable
#define trace_intel_pipe_enable(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_intel_pipe_enable_enabled
#define trace_intel_pipe_enable_enabled()	(0)
#endif
#ifndef trace_intel_pipe_disable
#define trace_intel_pipe_disable(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_intel_pipe_disable_enabled
#define trace_intel_pipe_disable_enabled()	(0)
#endif
#ifndef trace_intel_pipe_crc
#define trace_intel_pipe_crc(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_intel_pipe_crc_enabled
#define trace_intel_pipe_crc_enabled()	(0)
#endif
#ifndef trace_intel_memory_cxsr
#define trace_intel_memory_cxsr(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_intel_memory_cxsr_enabled
#define trace_intel_memory_cxsr_enabled()	(0)
#endif
#ifndef trace_intel_frontbuffer_invalidate
#define trace_intel_frontbuffer_invalidate(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_intel_frontbuffer_invalidate_enabled
#define trace_intel_frontbuffer_invalidate_enabled()	(0)
#endif
#ifndef trace_intel_frontbuffer_flush
#define trace_intel_frontbuffer_flush(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_intel_frontbuffer_flush_enabled
#define trace_intel_frontbuffer_flush_enabled()	(0)
#endif
#ifndef trace_intel_fbc_nuke
#define trace_intel_fbc_nuke(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_intel_fbc_nuke_enabled
#define trace_intel_fbc_nuke_enabled()	(0)
#endif
#ifndef trace_intel_fbc_deactivate
#define trace_intel_fbc_deactivate(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_intel_fbc_deactivate_enabled
#define trace_intel_fbc_deactivate_enabled()	(0)
#endif
#ifndef trace_intel_fbc_activate
#define trace_intel_fbc_activate(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_intel_fbc_activate_enabled
#define trace_intel_fbc_activate_enabled()	(0)
#endif
#ifndef trace_intel_crtc_vblank_work_start
#define trace_intel_crtc_vblank_work_start(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_intel_crtc_vblank_work_start_enabled
#define trace_intel_crtc_vblank_work_start_enabled()	(0)
#endif
#ifndef trace_intel_crtc_vblank_work_end
#define trace_intel_crtc_vblank_work_end(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_intel_crtc_vblank_work_end_enabled
#define trace_intel_crtc_vblank_work_end_enabled()	(0)
#endif
#ifndef trace_intel_crtc_flip_done
#define trace_intel_crtc_flip_done(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_intel_crtc_flip_done_enabled
#define trace_intel_crtc_flip_done_enabled()	(0)
#endif
#ifndef trace_g4x_wm
#define trace_g4x_wm(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_g4x_wm_enabled
#define trace_g4x_wm_enabled()	(0)
#endif

#endif
