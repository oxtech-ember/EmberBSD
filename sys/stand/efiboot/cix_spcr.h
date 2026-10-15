/* Origin: EmberBSD opt-in CIX SPCR table correction, 2026-10-15. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _CIX_SPCR_H_
#define _CIX_SPCR_H_

#include <efi.h>

#define CIX_SPCR_MAP_MAX 65536

/* The buffer belongs to the caller and contains a live EFI memory map. */
struct cix_spcr_map {
	const void *data;
	UINTN size;
	UINTN stride;
	UINT32 version;
};

/* Only metadata from validated tables; no firmware address is writable. */
struct cix_spcr_plan {
	const uint8_t *rsdp, *xsdt, *rsdt, *spcr;
	uint32_t xlength, rlength;
};

const char *cix_spcr_prepare(uint64_t, const struct cix_spcr_map *,
    struct cix_spcr_plan *);
/* Destination must be one page owned by this loader, from AllocatePages. */
const char *cix_spcr_build(uint64_t, const struct cix_spcr_map *, uint64_t);

#endif /* _CIX_SPCR_H_ */
