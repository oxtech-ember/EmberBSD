/* SPDX-License-Identifier: MIT */
/* Origin: EmberBSD PowerVR adapter; MIT-option Linux reference, AI-assisted. */
/* Copyright (c) 2023 Imagination Technologies Ltd. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* See powervr/LICENSE for the MIT notice and a733-gpu-fwstart.md. */
#ifndef _SUN60I_A733_GPU_FWSTART_H
#define _SUN60I_A733_GPU_FWSTART_H

#include "sun60i_a733_gpu_fwinfo.h"
#include "../../../dev/powervr/pvr_fwstart.h"

#define A733_GPU_FWSTART_REG_SIZE PVR_FWSTART_REG_SIZE
#define sun60i_a733_gpu_fwstart_result pvr_fwstart_result

int sun60i_a733_gpu_fwstart(bus_space_tag_t, bus_space_handle_t,
    const struct sun60i_a733_gpu_fwinfo *, uint64_t, uint64_t, uint64_t,
    struct sun60i_a733_gpu_fwstart_result *);


#endif
