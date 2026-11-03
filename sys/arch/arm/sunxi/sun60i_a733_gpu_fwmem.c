/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2026 EmberBSD contributors.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Origin: EmberBSD, AI-assisted native bus_dma preparation. The MIPS heap
 * placement follows the MIT-option PowerVR pvr_fw.c and pvr_fw_mips.c at
 * Linux 7b63ef2d55f24519e7e9e5f4d15dbea03f126e40. No firmware is executed.
 */
#include "sun60i_a733_gpu_fwmem.h"
#ifdef _KERNEL
#include <sys/null.h>
#else
#include <stddef.h>
#endif

int
sun60i_a733_gpu_fwmem_prepare(bus_dma_tag_t dmat, const void *bytes,
    size_t size, uint64_t bvnc, struct sun60i_a733_gpu_fwmem **out)
{
	return pvr_fwmem_prepare(dmat, bytes, size, bvnc, PVR_FW_PROFILE_A733,
	    UINT64_C(0x01800000), UINT64_C(0x10000), 0, 0, out);
}
int
sun60i_a733_gpu_fwmem_interface(struct sun60i_a733_gpu_fwmem *mem,
    uint64_t hz, uint32_t clusters, uint32_t slc)
{
	return pvr_fwmem_interface(mem, hz, clusters, slc);
}
void
sun60i_a733_gpu_fwmem_destroy(struct sun60i_a733_gpu_fwmem *mem)
{
	(void)pvr_fwmem_destroy(mem);
}
