/* SPDX-License-Identifier: MIT */
/* Origin: EmberBSD experimental GE8300 metadata policy, AI-assisted. */
#ifndef _PVR_GE8300_H
#define _PVR_GE8300_H
#include "pvr_fwimage.h"
#include "pvr_mips.h"
#define PVR_GE8300_BVNC UINT64_C(0x0016006600360026)
/* Exact pinned firmware only. No firmware execution or graphics support. */
int pvr_ge8300_fwinfo_parse(const void *, size_t, uint64_t,
    struct pvr_fwinfo *);
int pvr_ge8300_fwimage_prepare(const void *, size_t, uint64_t, void *, size_t,
    void *, size_t, struct pvr_fwimage *);
int pvr_ge8300_mips_boot_data(void *, size_t, uint64_t, uint64_t,
    const uint64_t [PVR_MIPS_PT_PAGES]);
#endif
