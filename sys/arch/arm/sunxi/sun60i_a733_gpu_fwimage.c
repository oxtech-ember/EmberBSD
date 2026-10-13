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

#include "sun60i_a733_gpu_fwimage.h"
#ifdef _KERNEL
#include <sys/errno.h>
#include <sys/systm.h>
#else
#include <errno.h>
#include <string.h>
#endif

#define FW_ELF_HEADER_SIZE	52
#define FW_ELF_PHDR_SIZE	32
#define FW_PT_NULL		0
#define FW_PT_LOAD		1

struct fwimage_segment {
	uint32_t source, vaddr, file_size, memory_size;
	uint32_t destination, type, id;
};

static uint32_t
image_u16(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8;
}

static uint32_t
image_u32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
	    (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int
image_spans(const void *bytes, size_t size, void *code, size_t code_size,
    void *data, size_t data_size, struct sun60i_a733_gpu_fwimage *out)
{
	const struct {
		uintptr_t start;
		size_t length;
	} span[4] = {
		{ (uintptr_t)bytes, size }, { (uintptr_t)code, code_size },
		{ (uintptr_t)data, data_size }, { (uintptr_t)out, sizeof(*out) }
	};

	for (unsigned int i = 0; i < 4; i++) {
		if (span[i].start == 0 || span[i].length == 0 ||
		    span[i].length > UINTPTR_MAX - span[i].start)
			return EINVAL;
	}
	for (unsigned int i = 0; i < 4; i++) {
		for (unsigned int j = 0; j < i; j++) {
			if (span[i].start < span[j].start + span[j].length &&
			    span[j].start < span[i].start + span[i].length)
				return EINVAL;
		}
	}
	return 0;
}

static int
image_segment(const uint8_t *p, const struct sun60i_a733_gpu_fwinfo *info,
    struct fwimage_segment *segment)
{
	struct fwimage_segment s = { 0 };
	uint32_t paddr, flags, align;
	unsigned int matches = 0;

	s.source = image_u32(p + 4);
	s.vaddr = image_u32(p + 8);
	paddr = image_u32(p + 12);
	s.file_size = image_u32(p + 16);
	s.memory_size = image_u32(p + 20);
	flags = image_u32(p + 24);
	align = image_u32(p + 28);
	if (s.memory_size == 0 || s.file_size > s.memory_size ||
	    s.memory_size > UINT32_MAX - s.vaddr ||
	    s.file_size > UINT32_MAX - s.source ||
	    s.source > info->payload_size ||
	    s.file_size > info->payload_size - s.source)
		return EINVAL;
	if (align > 1 && ((align & (align - 1)) != 0 ||
	    ((s.vaddr ^ s.source) & (align - 1)) != 0))
		return EINVAL;
	if (paddr != s.vaddr)
		return EOPNOTSUPP;
	for (unsigned int i = 0; i < info->layout_count; i++) {
		const struct sun60i_a733_gpu_fw_layout *l = &info->layout[i];
		uint32_t delta;

		if (s.vaddr < l->base_addr)
			continue;
		delta = s.vaddr - l->base_addr;
		if (delta > l->alloc_size ||
		    s.memory_size > l->alloc_size - delta)
			continue;
		s.destination = l->alloc_offset + delta;
		s.type = l->type;
		s.id = l->id;
		matches++;
	}
	if (matches != 1)
		return EINVAL;
	/* These permissions define this initial loader scope, not all ELF. */
	if ((s.type == A733_GPU_FW_CODE && flags != 5) ||
	    (s.type == A733_GPU_FW_DATA && flags != 6))
		return EOPNOTSUPP;
	*segment = s;
	return 0;
}

int
sun60i_a733_gpu_fwimage_prepare(const void *bytes, size_t size,
    uint64_t expected_bvnc, void *code, size_t code_capacity,
    void *data, size_t data_capacity, struct sun60i_a733_gpu_fwimage *out)
{
	struct sun60i_a733_gpu_fwimage result = { 0 };
	struct fwimage_segment plan[A733_GPU_FWIMAGE_MAX_PHDRS];
	const uint8_t *input = bytes;
	uint32_t phoff, phnum, seen = 0;
	int error, have_entry = 0;

	error = image_spans(bytes, size, code, code_capacity, data,
	    data_capacity, out);
	if (error != 0)
		return error;
	error = sun60i_a733_gpu_fwinfo_parse(bytes, size, expected_bvnc,
	    &result.info);
	if (error != 0)
		return error;
	for (unsigned int i = 0; i < result.info.layout_count; i++) {
		const struct sun60i_a733_gpu_fw_layout *l = &result.info.layout[i];
		unsigned int slot = l->id - A733_GPU_FW_MIPS_CODE;

		if (slot >= A733_GPU_FWIMAGE_SECTIONS)
			return EOPNOTSUPP;
		result.section[slot] = *l;
		seen |= 1U << slot;
	}
	if (seen != (1U << A733_GPU_FWIMAGE_SECTIONS) - 1)
		return EINVAL;
	if (code_capacity < result.info.allocation_size[A733_GPU_FW_CODE] ||
	    data_capacity < result.info.allocation_size[A733_GPU_FW_DATA])
		return ENOSPC;
	if (result.info.payload_size < FW_ELF_HEADER_SIZE ||
	    input[0] != 0x7f || input[1] != 'E' || input[2] != 'L' ||
	    input[3] != 'F')
		return EINVAL;
	if (input[4] != 1 || input[5] != 1 || input[6] != 1 ||
	    image_u16(input + 16) != 2 || image_u16(input + 18) != 8 ||
	    image_u32(input + 20) != 1)
		return EOPNOTSUPP;
	if (image_u16(input + 40) != FW_ELF_HEADER_SIZE ||
	    image_u16(input + 42) != FW_ELF_PHDR_SIZE)
		return EINVAL;
	phoff = image_u32(input + 28);
	phnum = image_u16(input + 44);
	if (phnum == 0)
		return EINVAL;
	if (phnum > A733_GPU_FWIMAGE_MAX_PHDRS)
		return EOPNOTSUPP;
	if (phoff < FW_ELF_HEADER_SIZE || phoff > result.info.payload_size ||
	    phnum > (UINT32_MAX - phoff) / FW_ELF_PHDR_SIZE ||
	    phnum > (result.info.payload_size - phoff) / FW_ELF_PHDR_SIZE)
		return EINVAL;
	result.entry = image_u32(input + 24);
	result.entry_addr = result.entry & ~UINT32_C(1);
	for (unsigned int i = 0; i < phnum; i++) {
		const uint8_t *p = input + phoff + i * FW_ELF_PHDR_SIZE;
		struct fwimage_segment *s = &plan[result.load_count];
		uint32_t type = image_u32(p);

		if (type == FW_PT_NULL)
			continue;
		if (type != FW_PT_LOAD)
			return EOPNOTSUPP;
		error = image_segment(p, &result.info, s);
		if (error != 0)
			return error;
		for (unsigned int j = 0; j < result.load_count; j++) {
			const struct fwimage_segment *old = &plan[j];

			if (s->vaddr < old->vaddr + old->memory_size &&
			    old->vaddr < s->vaddr + s->memory_size)
				return EINVAL;
			if (s->type == old->type &&
			    s->destination < old->destination + old->memory_size &&
			    old->destination < s->destination + s->memory_size)
				return EINVAL;
		}
		if (s->id == A733_GPU_FW_MIPS_BOOT_CODE &&
		    result.entry_addr >= s->vaddr &&
		    result.entry_addr - s->vaddr < s->file_size) {
			have_entry = 1;
			result.entry_offset = s->destination +
			    (result.entry_addr - s->vaddr);
		}
		result.load_count++;
	}
	if (!have_entry)
		return EINVAL;

	/* Commit only the fully validated plan. Spare capacity stays untouched. */
	memset(code, 0, result.info.allocation_size[A733_GPU_FW_CODE]);
	memset(data, 0, result.info.allocation_size[A733_GPU_FW_DATA]);
	for (unsigned int i = 0; i < result.load_count; i++) {
		const struct fwimage_segment *s = &plan[i];
		uint8_t *destination = s->type == A733_GPU_FW_CODE ? code : data;

		if (s->file_size != 0)
			memcpy(destination + s->destination, input + s->source,
			    s->file_size);
	}
	*out = result;
	return 0;
}
