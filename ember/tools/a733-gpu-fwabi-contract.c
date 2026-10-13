/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 EmberBSD contributors. AI-assisted ABI checks. */
#include "powervr/pvr_rogue_fwif.h"

/* Including the production headers evaluates all upstream ABI assertions. */
#ifndef PVR_FWABI_COMPILE_ONLY
#include <stdio.h>

#define SHOW(type) \
	printf(#type " size=%zu align=%zu\n", sizeof(struct type), \
	    _Alignof(struct type))

int
main(void)
{
	SHOW(rogue_fwif_connection_ctl);
	SHOW(rogue_fwif_osinit);
	SHOW(rogue_fwif_sysinit);
	SHOW(rogue_fwif_sysdata);
	SHOW(rogue_fwif_osdata);
	SHOW(rogue_fwif_runtime_cfg);
	SHOW(rogue_fwif_gpu_util_fwcb);
	SHOW(rogue_fwif_hwrinfobuf);
	SHOW(rogue_fwif_tracebuf);
	SHOW(rogue_fwif_ccb_ctl);
	SHOW(rogue_fwif_kccb_cmd);
	SHOW(rogue_fwif_fwccb_cmd);
	printf("firmware_started offset=%zu width=%zu\n",
	    offsetof(struct rogue_fwif_sysinit, firmware_started),
	    sizeof(((struct rogue_fwif_sysinit *)0)->firmware_started));
	return (0);
}
#endif
