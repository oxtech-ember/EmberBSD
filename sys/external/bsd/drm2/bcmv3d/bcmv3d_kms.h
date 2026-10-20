/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _BCMV3D_KMS_H_
#define _BCMV3D_KMS_H_
#include <sys/types.h>
struct bcmv3d_device;
/* A read through the retained canonical HDMI owner. Positive BSD errno. */
typedef int (*bcmv3d_kms_detect_fn)(void *, bool *);
/* Create software objects before drm_dev_register. No display MMIO. */
int bcmv3d_kms_init(struct bcmv3d_device *);
/* Failed preregistration only. Never call after binding or registration. */
void bcmv3d_kms_fini(struct bcmv3d_device *);
/* Bind only the existing admitted HVS/PV0 owners. Lifetime ends at reboot. */
int bcmv3d_kms_bind(struct bcmv3d_device *, bcmv3d_kms_detect_fn, void *);
#endif
