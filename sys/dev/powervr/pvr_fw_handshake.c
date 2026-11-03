/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD common FWIF compatibility check, factored from A733. */
#include "pvr_fw_handshake.h"
#include "fwif/pvr_rogue_fwif.h"
#ifdef _KERNEL
#include <sys/errno.h>
#else
#include <errno.h>
#endif

int
pvr_fw_handshake_check(uint8_t started, uint8_t updated, uint32_t hw_layout,
    uint32_t fw_layout, uint64_t hw_bvnc, uint64_t fw_bvnc, uint64_t expected)
{
	if (started != 1 || updated > 1)
		return EPROTO;
	if (updated == 0)
		return EAGAIN;
	if (hw_layout != ROGUE_FWIF_COMPCHECKS_LAYOUT_VERSION ||
	    fw_layout != ROGUE_FWIF_COMPCHECKS_LAYOUT_VERSION)
		return EOPNOTSUPP;
	if (hw_bvnc != expected || fw_bvnc != expected)
		return ENODEV;
	return 0;
}
