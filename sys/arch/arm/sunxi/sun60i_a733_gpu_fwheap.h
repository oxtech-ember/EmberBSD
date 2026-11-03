/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD; A733 firmware heap compatibility adapter. */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _SUN60I_A733_GPU_FWHEAP_H
#define _SUN60I_A733_GPU_FWHEAP_H
#include "sun60i_a733_gpu_fwmem.h"
#include "sun60i_a733_gpu_fwcompute.h"
#include "sun60i_a733_gpu_fwcompute_file.h"
#include "sun60i_a733_gpu_fwcompute_vm.h"
#include "../../../dev/powervr/pvr_fwheap.h"
#define A733_GPU_FWHEAP_BYTES PVR_FWHEAP_BYTES
#define A733_GPU_FWHEAP_CCCB_OFF PVR_FWHEAP_CCCB_OFF
#define A733_GPU_FWHEAP_CONTEXT_OFF PVR_FWHEAP_CONTEXT_OFF
#define A733_GPU_FWHEAP_CTL_OFF PVR_FWHEAP_CTL_OFF
#define A733_GPU_FWHEAP_H PVR_FWHEAP_H
#define A733_GPU_FWHEAP_PAGES PVR_FWHEAP_PAGES
#define A733_GPU_FWHEAP_STATE_OFF PVR_FWHEAP_STATE_OFF
#define A733_GPU_FWHEAP_TIMELINE_OFF PVR_FWHEAP_TIMELINE_OFF
#define A733_GPU_FWHEAP_VM_CONTEXT_OFF PVR_FWHEAP_VM_CONTEXT_OFF
#define sun60i_a733_gpu_fwheap pvr_fwheap
#define sun60i_a733_gpu_fwvm pvr_fwvm
int sun60i_a733_gpu_fwheap_prepare(bus_dma_tag_t,
    struct sun60i_a733_gpu_fwmem *,
    const struct sun60i_a733_gpu_fwcompute_image *,
    struct sun60i_a733_gpu_fwheap **);
int sun60i_a733_gpu_fwheap_map(struct sun60i_a733_gpu_fwheap *,
    struct sun60i_a733_gpu_fwmem *);
int sun60i_a733_gpu_fwheap_set_vm_context(struct sun60i_a733_gpu_fwheap *,
    const uint8_t[32]);
void sun60i_a733_gpu_fwheap_observe(const struct sun60i_a733_gpu_fwheap *,
    uint32_t *, uint32_t *, uint32_t *);
void sun60i_a733_gpu_fwheap_destroy(struct sun60i_a733_gpu_fwheap **);
int sun60i_a733_gpu_fwvm_prepare(bus_dma_tag_t,
    const struct a733_gpu_fwcompute_file *, const uint8_t *,
    struct sun60i_a733_gpu_fwheap *, struct sun60i_a733_gpu_fwvm **);
void sun60i_a733_gpu_fwvm_destroy(struct sun60i_a733_gpu_fwvm **);
#endif
