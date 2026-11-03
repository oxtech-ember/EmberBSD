/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD; A733 compatibility adapter. */
/* Copyright (c) 2026 EmberBSD contributors. */
#include "sun60i_a733_gpu_mmu.h"
#ifdef _KERNEL
#include <sys/errno.h>
#include <sys/null.h>
#else
#include <errno.h>
#endif

int
sun60i_a733_gpu_mmu_indices(uint64_t va,
    struct sun60i_a733_gpu_mmu_indices *out)
{
	struct pvr_mmu_indices indices;
	int error;

	if (out == NULL || (uintptr_t)out %
	    _Alignof(struct sun60i_a733_gpu_mmu_indices) != 0 ||
	    sizeof(*out) - 1 > (uintptr_t)-1 - (uintptr_t)out)
		return EINVAL;
	error = pvr_mmu_indices(va, &indices);
	if (error == 0) {
		out->pc = indices.pc;
		out->pd = indices.pd;
		out->pt = indices.pt;
		out->offset = indices.offset;
	}
	return error;
}

int
sun60i_a733_gpu_mmu_pce(uint64_t pa, uint32_t *out)
{
	return pvr_mmu_pce(pa, out);
}

int
sun60i_a733_gpu_mmu_pde(uint64_t pa, uint64_t *out)
{
	return pvr_mmu_pde(pa, out);
}

int
sun60i_a733_gpu_mmu_pte(uint64_t pa, uint64_t flags, uint64_t *out)
{
	return pvr_mmu_pte(pa, flags, out);
}
