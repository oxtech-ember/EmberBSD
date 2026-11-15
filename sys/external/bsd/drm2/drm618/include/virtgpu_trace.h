/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: virtgpu_vq.c of dist618 (MIT), where the
 * two tracepoints are called; the GPL original was not read.
 *
 * The tracepoints of the virtio-gpu driver: there is no tracing here, the
 * calls do nothing.  The arguments are still evaluated by the caller.
 */

#ifndef _DRM618_VIRTGPU_TRACE_H_
#define _DRM618_VIRTGPU_TRACE_H_

#include <linux/types.h>
#include <linux/virtio_gpu.h>

struct linux_virtqueue;

static inline void
trace_virtio_gpu_cmd_queue(struct linux_virtqueue *vq __unused,
    struct virtio_gpu_ctrl_hdr *hdr __unused, u32 seqno __unused)
{
}

static inline void
trace_virtio_gpu_cmd_response(struct linux_virtqueue *vq __unused,
    struct virtio_gpu_ctrl_hdr *hdr __unused, u32 seqno __unused)
{
}

#endif	/* _DRM618_VIRTGPU_TRACE_H_ */
