/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD; strict A733 adapter for common compute decoder. */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifdef _KERNEL
#include <sys/errno.h>
#else
#include <errno.h>
#endif
#include "sun60i_a733_gpu_fwcompute_cmd.h"
int
sun60i_a733_gpu_fwcompute_cmd_decode(const void *data, size_t size,
    uint64_t bvnc, uint64_t stream, uint64_t resume, uint64_t border,
    struct rogue_fwif_cmd_compute *out)
{
	if (bvnc != A733_GPU_FWCOMPUTE_BVNC)
		return EOPNOTSUPP;
	return pvr_fwcompute_cmd_decode(data, size, bvnc, stream, resume, border, out);
}
