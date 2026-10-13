/* SPDX-License-Identifier: MIT */
/*
 * Copyright (c) 2024 Imagination Technologies Ltd.
 * Copyright (c) 2026 EmberBSD contributors.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * ELF loading reference: Linux PowerVR pvr_fw_util.c,
 * 7b63ef2d55f24519e7e9e5f4d15dbea03f126e40, MIT license option.
 * Native bounded image preparation: EmberBSD, AI-assisted.
 * See a733-gpu-fwimage.md.
 */

#ifndef _SUN60I_A733_GPU_FWIMAGE_H
#define _SUN60I_A733_GPU_FWIMAGE_H

#include "sun60i_a733_gpu_fwinfo.h"

#define A733_GPU_FWIMAGE_MAX_PHDRS	16
#define A733_GPU_FWIMAGE_SECTIONS	6

struct sun60i_a733_gpu_fwimage {
	struct sun60i_a733_gpu_fwinfo info;
	/* Canonical order: section[id - A733_GPU_FW_MIPS_CODE]. */
	struct sun60i_a733_gpu_fw_layout section[A733_GPU_FWIMAGE_SECTIONS];
	uint32_t entry, entry_addr, entry_offset;
	uint32_t load_count;
};

/*
 * Prepare CPU buffers only. This does not authenticate or execute firmware.
 * No allocation, MMIO, DMA, boot-data patching, or address-space setup.
 * Supply stable input, the expected live BVNC, and disjoint writable outputs.
 * code/data capacities must cover the metadata allocation sizes. Extra space
 * is untouched. All declared input/output spans must be disjoint and must
 * not wrap uintptr_t. The result owns no pointers; metadata spans refer to
 * the input. entry keeps the microMIPS bit; entry_addr clears it. entry_offset
 * is the normalized entry's offset in the CODE buffer.
 *
 * Accept ELF32 LE ET_EXEC/EM_MIPS v1, up to 16 PT_LOAD/PT_NULL headers, all
 * six MIPS metadata sections, equal physical/virtual load addresses, and
 * CODE=R|X / DATA=R|W permissions. Other variants return EOPNOTSUPP.
 * Return positive errno. EINVAL: malformed input or arguments; ENOSPC:
 * insufficient buffer capacity. Metadata errors are propagated unchanged.
 * On every validation error, leave out, code, data, and input unchanged.
 * The caller must supply valid memory for every declared span.
 */
int sun60i_a733_gpu_fwimage_prepare(const void *, size_t, uint64_t,
    void *, size_t, void *, size_t, struct sun60i_a733_gpu_fwimage *);

#endif /* _SUN60I_A733_GPU_FWIMAGE_H */
