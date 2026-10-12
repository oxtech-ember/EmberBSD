/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#include <sys/types.h>
#ifdef _KERNEL
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/systm.h>
#else
#include <errno.h>
#include <stddef.h>
#include <string.h>
#endif

#include "sun60i_a733_gpu_fwcompute_file.h"

_Static_assert(sizeof(struct a733_gpu_fwcompute_file_header) ==
    A733_GPU_FWCOMPUTE_FILE_PREFIX, "packed compute file prefix");
_Static_assert(offsetof(struct a733_gpu_fwcompute_file_header,
    payload_digest) == 64, "compute file digest offset");
_Static_assert(sizeof(struct a733_gpu_fwcompute_file_entry) ==
    A733_GPU_FWCOMPUTE_ENTRY, "packed compute file entry");
_Static_assert(A733_GPU_FWCOMPUTE_FILE_PREFIX +
    A733_GPU_FWCOMPUTE_RESOURCES * A733_GPU_FWCOMPUTE_ENTRY == 312,
    "nine-entry compute file header");

/* Inclusive ends avoid overflow for spans ending at the type limit. */
static bool
fwfile_overlap(uint64_t a, uint64_t asize, uint64_t b, uint64_t bsize)
{
	return a <= b + bsize - 1 && b <= a + asize - 1;
}

int
sun60i_a733_gpu_fwcompute_file_parse(const void *data, size_t size,
    uint64_t bvnc, struct a733_gpu_fwcompute_file *out)
{
	struct a733_gpu_fwcompute_file_header header;
	struct a733_gpu_fwcompute_file_entry
	    entry[A733_GPU_FWCOMPUTE_RESOURCES];
	uint32_t payload, base;
	bool seen_role[A733_GPU_FWCOMPUTE_RESOURCES] = { false };
	size_t i, j;
	int k;

	if (data == NULL || out == NULL || size < A733_GPU_FWCOMPUTE_FILE_PREFIX +
	    A733_GPU_FWCOMPUTE_RESOURCES * A733_GPU_FWCOMPUTE_ENTRY ||
	    size > A733_GPU_FWCOMPUTE_FILE_MAX)
		return EINVAL;
	memcpy(&header, data, sizeof(header));
	if (header.magic != A733_GPU_FWCOMPUTE_FILE_MAGIC ||
	    header.version != A733_GPU_FWCOMPUTE_FILE_VERSION ||
	    header.reserved != 0 || header.bvnc != bvnc ||
	    header.resources != A733_GPU_FWCOMPUTE_RESOURCES)
		return EINVAL;
	if (header.header_bytes != A733_GPU_FWCOMPUTE_FILE_PREFIX +
	    A733_GPU_FWCOMPUTE_RESOURCES * A733_GPU_FWCOMPUTE_ENTRY)
		return EINVAL;
	payload = header.payload_bytes;
	if ((size_t)payload != size - header.header_bytes ||
	    header.static_bytes != A733_GPU_FWCOMPUTE_STATE_BYTES ||
	    header.priority > 2)
		return EINVAL;
	base = header.header_bytes;
	if (header.static_offset % 4 != 0 ||
	    header.static_offset > payload ||
	    A733_GPU_FWCOMPUTE_STATE_BYTES > payload - header.static_offset)
		return EINVAL;
	if (header.context_state_va == 0 ||
	    header.context_state_va >= (UINT64_C(1) << 40) ||
	    (header.context_state_va & 15) != 0)
		return EINVAL;
	memcpy(entry, (const uint8_t *)data + sizeof(header), sizeof(entry));
	for (i = 0; i < A733_GPU_FWCOMPUTE_RESOURCES; i++) {
		const struct a733_gpu_fwcompute_file_entry *e = &entry[i];

		if (e->role >= A733_GPU_FWCOMPUTE_RESOURCES ||
		    seen_role[e->role] ||
		    (e->flags & ~(A733_GPU_FWCOMPUTE_FLAG_RO |
		    A733_GPU_FWCOMPUTE_FLAG_BYPASS)) != 0)
			return EINVAL;
		seen_role[e->role] = true;
		if (e->offset % 4 != 0 || e->bytes < 4 || e->bytes % 4 != 0 ||
		    e->offset > payload || e->bytes > payload ||
		    e->bytes > payload - e->offset)
			return EINVAL;
		if (e->va == 0 || e->va >= (UINT64_C(1) << 40) ||
		    (e->va & 4095) != 0 ||
		    e->bytes > (UINT64_C(1) << 40) - e->va)
			return EINVAL;
		/*
		 * Page-aligned virtual addresses make byte-range overlap
		 * equivalent to sharing a page: whole distinct pages only.
		 */
		for (j = 0; j < i; j++) {
			if (fwfile_overlap(e->va, e->bytes, entry[j].va,
			    entry[j].bytes) ||
			    fwfile_overlap(base + e->offset, e->bytes,
			    base + entry[j].offset, entry[j].bytes))
				return EINVAL;
		}
		if (fwfile_overlap(base + e->offset, e->bytes,
		    base + header.static_offset, header.static_bytes))
			return EINVAL;
	}
	for (k = 0; k < A733_GPU_FWCOMPUTE_RESOURCES; k++) {
		if (!seen_role[k])
			return EINVAL;
	}

	out->header_bytes = header.header_bytes;
	out->payload_bytes = payload;
	out->static_offset = header.static_offset;
	out->context_state_va = header.context_state_va;
	out->priority = header.priority;
	out->pid = header.pid;
	out->context_id = header.context_id;
	out->job_id = header.job_id;
	memcpy(out->payload_digest, header.payload_digest, 32);
	memcpy(out->resource, entry, sizeof(entry));
	return 0;
}

void
sun60i_a733_gpu_fwcompute_file_fwspans(
    struct sun60i_a733_gpu_fwcompute_fwspan *fw)
{
	static const uint32_t sizes[A733_GPU_FWCOMPUTE_OBJECTS] = {
		sizeof(struct rogue_fwif_fwcomputecontext),
		sizeof(struct rogue_fwif_cccb_ctl),
		A733_GPU_FWCOMPUTE_CCCB_SIZE,
		sizeof(struct rogue_fwif_compute_ctx_state),
		sizeof(uint32_t),
		sizeof(struct rogue_fwif_fwmemcontext)
	};
	uint32_t offset = 0;
	size_t i;

	for (i = 0; i < A733_GPU_FWCOMPUTE_OBJECTS; i++) {
		/* Pad each offset to keep every firmware address eight-aligned. */
		offset = (offset + 7U) & ~7U;
		fw[i].addr = A733_GPU_FWCOMPUTE_SPAN_BASE + offset;
		fw[i].size = sizes[i];
		offset += sizes[i];
	}
}
