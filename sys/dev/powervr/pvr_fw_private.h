/* SPDX-License-Identifier: MIT */
/* Origin: EmberBSD common PowerVR firmware contracts, AI-assisted. */
#ifndef _PVR_FW_PRIVATE_H
#define _PVR_FW_PRIVATE_H
#include "pvr_fwimage.h"
/* Internal only: the GE policy caller must verify the pinned image digest first. */
int pvr_fwinfo_parse_profile(const void *, size_t, uint64_t,
    struct pvr_fwinfo *, int);
int pvr_fwimage_prepare_profile(const void *, size_t, uint64_t, void *, size_t,
    void *, size_t, struct pvr_fwimage *,
    int (*)(const void *, size_t, uint64_t, struct pvr_fwinfo *));
#endif
