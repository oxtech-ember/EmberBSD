/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD; strict A733 compute-image compatibility adapter. */
/* Copyright (c) 2026 EmberBSD contributors. */
#include "sun60i_a733_gpu_fwcompute_file.h"
#ifdef _KERNEL
#include <sys/errno.h>
#else
#include <errno.h>
#endif

int
sun60i_a733_gpu_fwcompute_file_parse(const void *data, size_t size,
    uint64_t bvnc, struct a733_gpu_fwcompute_file *out)
{
	struct a733_gpu_fwcompute_file parsed;
	int error;

	if (bvnc == A733_GPU_FWCOMPUTE_BVNC || out == NULL)
		return pvr_fwcompute_file_parse(data, size, bvnc, out);
	error = pvr_fwcompute_file_parse(data, size, bvnc, &parsed);
	return error == 0 ? EOPNOTSUPP : error;
}

void
sun60i_a733_gpu_fwcompute_file_fwspans(
    struct sun60i_a733_gpu_fwcompute_fwspan *fw)
{
	pvr_fwcompute_file_fwspans(fw);
}

void
sun60i_a733_gpu_fwcompute_file_relocate(uint8_t state[56], uint64_t base)
{
	pvr_fwcompute_file_relocate(state, base);
}

void
sun60i_a733_gpu_fwcompute_file_relocate_stream(uint32_t *words,
    size_t count, uint64_t base)
{
	pvr_fwcompute_file_relocate_stream(words, count, base);
}
