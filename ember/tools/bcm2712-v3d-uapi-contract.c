/* Origin: EmberBSD V3D 7.1 UAPI compatibility checks, 2026-10-11. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef uint8_t __u8;
typedef uint16_t __u16;
typedef uint32_t __u32;
typedef uint64_t __u64;
typedef int32_t __s32;
typedef int64_t __s64;
#include "v3d-uapi-body.h"

_Static_assert(sizeof(struct drm_v3d_create_bo) == 16, "CREATE_BO ABI");
_Static_assert(sizeof(struct drm_v3d_mmap_bo) == 16, "MMAP_BO ABI");
_Static_assert(sizeof(struct drm_v3d_wait_bo) == 16, "WAIT_BO ABI");
_Static_assert(sizeof(struct drm_v3d_get_param) == 16, "GET_PARAM ABI");
_Static_assert(sizeof(struct drm_v3d_submit_tfu) == 88, "V3D 7.1 TFU ABI");
_Static_assert(offsetof(struct drm_v3d_submit_tfu, bo_handles) == 44,
    "legacy TFU handle offsets must not move");
_Static_assert(offsetof(struct drm_v3d_submit_tfu, in_sync) == 60,
    "legacy TFU sync offsets must not move");
_Static_assert(offsetof(struct drm_v3d_submit_tfu, flags) == 68,
    "TFU flags ABI");
_Static_assert(offsetof(struct drm_v3d_submit_tfu, extensions) == 72,
    "TFU extensions ABI");
_Static_assert(offsetof(struct drm_v3d_submit_tfu, v71.ioc) == 80,
    "V3D 7.1 IOC ABI");
_Static_assert(DRM_V3D_PARAM_SUPPORTS_TFU == 7, "capability ABI");
_Static_assert(DRM_V3D_PARAM_SUPPORTS_MULTISYNC_EXT == 11,
    "extension capability ABI");

int
main(void)
{

	puts("PASS: V3D 7.1 UAPI layout and legacy field offsets");
	return 0;
}
