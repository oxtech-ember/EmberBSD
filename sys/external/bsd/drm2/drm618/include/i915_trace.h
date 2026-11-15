/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Own implementation. Sources read: the calls to trace_i915_* and trace_intel_context_* in the MIT files of dist618/drm/i915 (gem, gt, root); the GPL original was not read. */
#ifndef _I915_TRACE_H_618
#define _I915_TRACE_H_618

#include "drm618_trace_nop.h"

#ifndef trace_i915_request_in
#define trace_i915_request_in(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_i915_request_in_enabled
#define trace_i915_request_in_enabled()	(0)
#endif
#ifndef trace_i915_request_add
#define trace_i915_request_add(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_i915_request_add_enabled
#define trace_i915_request_add_enabled()	(0)
#endif
#ifndef trace_intel_context_sched_enable
#define trace_intel_context_sched_enable(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_intel_context_sched_enable_enabled
#define trace_intel_context_sched_enable_enabled()	(0)
#endif
#ifndef trace_i915_gem_object_create
#define trace_i915_gem_object_create(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_i915_gem_object_create_enabled
#define trace_i915_gem_object_create_enabled()	(0)
#endif
#ifndef trace_intel_context_steal_guc_id
#define trace_intel_context_steal_guc_id(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_intel_context_steal_guc_id_enabled
#define trace_intel_context_steal_guc_id_enabled()	(0)
#endif
#ifndef trace_intel_context_set_prio
#define trace_intel_context_set_prio(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_intel_context_set_prio_enabled
#define trace_intel_context_set_prio_enabled()	(0)
#endif
#ifndef trace_intel_context_sched_done
#define trace_intel_context_sched_done(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_intel_context_sched_done_enabled
#define trace_intel_context_sched_done_enabled()	(0)
#endif
#ifndef trace_intel_context_sched_disable
#define trace_intel_context_sched_disable(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_intel_context_sched_disable_enabled
#define trace_intel_context_sched_disable_enabled()	(0)
#endif
#ifndef trace_intel_context_reset
#define trace_intel_context_reset(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_intel_context_reset_enabled
#define trace_intel_context_reset_enabled()	(0)
#endif
#ifndef trace_intel_context_register
#define trace_intel_context_register(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_intel_context_register_enabled
#define trace_intel_context_register_enabled()	(0)
#endif
#ifndef trace_intel_context_free
#define trace_intel_context_free(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_intel_context_free_enabled
#define trace_intel_context_free_enabled()	(0)
#endif
#ifndef trace_intel_context_fence_release
#define trace_intel_context_fence_release(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_intel_context_fence_release_enabled
#define trace_intel_context_fence_release_enabled()	(0)
#endif
#ifndef trace_intel_context_do_unpin
#define trace_intel_context_do_unpin(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_intel_context_do_unpin_enabled
#define trace_intel_context_do_unpin_enabled()	(0)
#endif
#ifndef trace_intel_context_do_pin
#define trace_intel_context_do_pin(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_intel_context_do_pin_enabled
#define trace_intel_context_do_pin_enabled()	(0)
#endif
#ifndef trace_intel_context_deregister_done
#define trace_intel_context_deregister_done(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_intel_context_deregister_done_enabled
#define trace_intel_context_deregister_done_enabled()	(0)
#endif
#ifndef trace_intel_context_deregister
#define trace_intel_context_deregister(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_intel_context_deregister_enabled
#define trace_intel_context_deregister_enabled()	(0)
#endif
#ifndef trace_intel_context_create
#define trace_intel_context_create(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_intel_context_create_enabled
#define trace_intel_context_create_enabled()	(0)
#endif
#ifndef trace_intel_context_ban
#define trace_intel_context_ban(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_intel_context_ban_enabled
#define trace_intel_context_ban_enabled()	(0)
#endif
#ifndef trace_i915_vma_unbind
#define trace_i915_vma_unbind(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_i915_vma_unbind_enabled
#define trace_i915_vma_unbind_enabled()	(0)
#endif
#ifndef trace_i915_vma_bind
#define trace_i915_vma_bind(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_i915_vma_bind_enabled
#define trace_i915_vma_bind_enabled()	(0)
#endif
#ifndef trace_i915_request_wait_end
#define trace_i915_request_wait_end(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_i915_request_wait_end_enabled
#define trace_i915_request_wait_end_enabled()	(0)
#endif
#ifndef trace_i915_request_wait_begin
#define trace_i915_request_wait_begin(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_i915_request_wait_begin_enabled
#define trace_i915_request_wait_begin_enabled()	(0)
#endif
#ifndef trace_i915_request_submit
#define trace_i915_request_submit(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_i915_request_submit_enabled
#define trace_i915_request_submit_enabled()	(0)
#endif
#ifndef trace_i915_request_retire
#define trace_i915_request_retire(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_i915_request_retire_enabled
#define trace_i915_request_retire_enabled()	(0)
#endif
#ifndef trace_i915_request_queue
#define trace_i915_request_queue(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_i915_request_queue_enabled
#define trace_i915_request_queue_enabled()	(0)
#endif
#ifndef trace_i915_request_out
#define trace_i915_request_out(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_i915_request_out_enabled
#define trace_i915_request_out_enabled()	(0)
#endif
#ifndef trace_i915_request_guc_submit
#define trace_i915_request_guc_submit(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_i915_request_guc_submit_enabled
#define trace_i915_request_guc_submit_enabled()	(0)
#endif
#ifndef trace_i915_request_execute
#define trace_i915_request_execute(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_i915_request_execute_enabled
#define trace_i915_request_execute_enabled()	(0)
#endif
#ifndef trace_i915_ppgtt_release
#define trace_i915_ppgtt_release(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_i915_ppgtt_release_enabled
#define trace_i915_ppgtt_release_enabled()	(0)
#endif
#ifndef trace_i915_ppgtt_create
#define trace_i915_ppgtt_create(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_i915_ppgtt_create_enabled
#define trace_i915_ppgtt_create_enabled()	(0)
#endif
#ifndef trace_i915_gem_shrink
#define trace_i915_gem_shrink(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_i915_gem_shrink_enabled
#define trace_i915_gem_shrink_enabled()	(0)
#endif
#ifndef trace_i915_gem_object_pwrite
#define trace_i915_gem_object_pwrite(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_i915_gem_object_pwrite_enabled
#define trace_i915_gem_object_pwrite_enabled()	(0)
#endif
#ifndef trace_i915_gem_object_pread
#define trace_i915_gem_object_pread(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_i915_gem_object_pread_enabled
#define trace_i915_gem_object_pread_enabled()	(0)
#endif
#ifndef trace_i915_gem_object_fault
#define trace_i915_gem_object_fault(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_i915_gem_object_fault_enabled
#define trace_i915_gem_object_fault_enabled()	(0)
#endif
#ifndef trace_i915_gem_object_destroy
#define trace_i915_gem_object_destroy(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_i915_gem_object_destroy_enabled
#define trace_i915_gem_object_destroy_enabled()	(0)
#endif
#ifndef trace_i915_gem_object_clflush
#define trace_i915_gem_object_clflush(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_i915_gem_object_clflush_enabled
#define trace_i915_gem_object_clflush_enabled()	(0)
#endif
#ifndef trace_i915_gem_evict_vm
#define trace_i915_gem_evict_vm(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_i915_gem_evict_vm_enabled
#define trace_i915_gem_evict_vm_enabled()	(0)
#endif
#ifndef trace_i915_gem_evict_node
#define trace_i915_gem_evict_node(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_i915_gem_evict_node_enabled
#define trace_i915_gem_evict_node_enabled()	(0)
#endif
#ifndef trace_i915_gem_evict
#define trace_i915_gem_evict(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_i915_gem_evict_enabled
#define trace_i915_gem_evict_enabled()	(0)
#endif
#ifndef trace_i915_context_free
#define trace_i915_context_free(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_i915_context_free_enabled
#define trace_i915_context_free_enabled()	(0)
#endif
#ifndef trace_i915_context_create
#define trace_i915_context_create(...)	DRM618_TRACE_NOP(__VA_ARGS__)
#endif
#ifndef trace_i915_context_create_enabled
#define trace_i915_context_create_enabled()	(0)
#endif

#endif
