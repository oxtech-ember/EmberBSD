/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. AI-assisted native interface. */
#ifndef _PVR_FILE_H_
#define _PVR_FILE_H_
#include <sys/types.h>
struct drm_file;
struct drm_gem_object;
struct pvr_backend;
struct pvr_file_vm;
/* All calls require the device gate and, if present, the platform gate. */
struct pvr_file_vm *pvr_file_vm_get(struct drm_file *, uint32_t);
void pvr_file_vm_put(struct pvr_file_vm *);
int pvr_file_vm_publish(struct pvr_file_vm *, const struct pvr_backend *, uint32_t *);
/*
 * Snapshot all current mappings with one GEM reference per array element.
 * The output slot starts NULL. Failure leaves both outputs unchanged.
 * Drop each reference and kmem_free the original count-sized array after use.
 * Coverage requires an owned array sorted by reservation address. The caller
 * locks those reservations outside the device/platform gates, then reenters
 * both gates before checking the current VM set. A removed mapping is safe;
 * a newly mapped reservation absent from the snapshot requires a new attempt.
 */
int pvr_file_vm_objects(struct pvr_file_vm *, struct drm_gem_object ***, size_t *);
bool pvr_file_vm_objects_cover(struct pvr_file_vm *,
    struct drm_gem_object * const *, size_t);
int pvr_file_vm_prepare(struct pvr_file_vm *);
void pvr_file_vm_complete(struct pvr_file_vm *);
void pvr_file_vm_fail(struct pvr_file_vm *);
int pvr_file_vm_buffer(struct pvr_file_vm *, uint64_t, size_t, uint64_t, void **);
/* A pin requires a live VM reference and prevents removal of its mapping. */
struct pvr_file_pin {
	void *mapping;
	void *cpu;
	uint64_t physical;
};
int pvr_file_vm_pin(struct pvr_file_vm *, uint64_t, size_t, uint64_t, struct pvr_file_pin *);
void pvr_file_pin_sync(void *, int);
void pvr_file_unpin(struct pvr_file_pin *);
/*
 * A shared hold requires a live VM reference. It prevents mapping removal.
 * Several HWRT spans can share one BO. An exclusive free-list pin cannot.
 */
struct pvr_file_hold {
	void *mapping;
};
int pvr_file_vm_hold(struct pvr_file_vm *, uint64_t, size_t, uint64_t,
    struct pvr_file_hold *);
void pvr_file_unhold(struct pvr_file_hold *);
#endif /* _PVR_FILE_H_ */
