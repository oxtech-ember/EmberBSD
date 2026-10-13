/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. AI-assisted native interface. */
#ifndef _PVR_EXEC_H_
#define _PVR_EXEC_H_
#include <sys/types.h>
struct drm_device;
struct drm_file;
struct pvr_context;
struct pvr_file_vm;
struct pvr_exec_file;
struct pvr_exec_context;
struct pvr_exec_file *pvr_exec_open(void);
void pvr_exec_close(struct drm_device *, struct pvr_exec_file *);
/* All accessors and reference operations require the device/backend gate. */
struct pvr_exec_file *pvr_file_execution(struct drm_file *);
struct pvr_exec_context *pvr_exec_context_get(struct drm_file *, uint32_t);
void pvr_exec_context_put(struct drm_device *, struct pvr_exec_context *);
struct pvr_file_vm *pvr_exec_context_vm(struct pvr_exec_context *);
struct pvr_context *pvr_exec_context_firmware(struct pvr_exec_context *);
int pvr_create_context(struct drm_device *, void *, struct drm_file *);
int pvr_destroy_context(struct drm_device *, void *, struct drm_file *);
int pvr_create_free_list(struct drm_device *, void *, struct drm_file *);
int pvr_destroy_free_list(struct drm_device *, void *, struct drm_file *);
int pvr_submit_jobs(struct drm_device *, void *, struct drm_file *);
#endif /* _PVR_EXEC_H_ */
