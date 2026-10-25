/* SPDX-License-Identifier: BSD-2-Clause */
/* Production AMDGPU code is inserted by amdgpu-stable-pstate-contract.py. */
#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef int8_t __s8;
typedef uint8_t __u8;
typedef int16_t __s16;
typedef uint16_t __u16;
typedef int32_t __s32;
typedef uint32_t __u32;
typedef int64_t __s64;
typedef uint64_t __u64;
typedef uint32_t u32;
#define _DRM_H_
@UAPI_HEADER@

_Static_assert(AMDGPU_CTX_OP_GET_STABLE_PSTATE == 5, "GET opcode");
_Static_assert(AMDGPU_CTX_OP_SET_STABLE_PSTATE == 6, "SET opcode");
_Static_assert(AMDGPU_CTX_STABLE_PSTATE_FLAGS_MASK == 0xf, "flags mask");
_Static_assert(AMDGPU_CTX_STABLE_PSTATE_NONE == 0, "NONE value");
_Static_assert(AMDGPU_CTX_STABLE_PSTATE_STANDARD == 1, "STANDARD value");
_Static_assert(AMDGPU_CTX_STABLE_PSTATE_MIN_SCLK == 2, "MIN_SCLK value");
_Static_assert(AMDGPU_CTX_STABLE_PSTATE_MIN_MCLK == 3, "MIN_MCLK value");
_Static_assert(AMDGPU_CTX_STABLE_PSTATE_PEAK == 4, "PEAK value");
_Static_assert(sizeof(struct drm_amdgpu_ctx_in) == 16, "ctx input size");
_Static_assert(offsetof(struct drm_amdgpu_ctx_in, flags) == 4, "ctx flags offset");
_Static_assert(sizeof(((union drm_amdgpu_ctx_out *)0)->pstate) == 8,
    "pstate output size");
_Static_assert(offsetof(union drm_amdgpu_ctx_out, pstate._pad) == 4,
    "pstate pad offset");

@VALIDATOR@

int
main(void)
{
	u32 pstate = UINT32_MAX;
	u32 value;

	assert(amdgpu_ctx_stable_pstate_flags(AMDGPU_CTX_OP_GET_STABLE_PSTATE,
	    0, &pstate) == 0);
	assert(pstate == UINT32_MAX);
	assert(amdgpu_ctx_stable_pstate_flags(AMDGPU_CTX_OP_GET_STABLE_PSTATE,
	    1, &pstate) == -EINVAL);

	for (value = AMDGPU_CTX_STABLE_PSTATE_NONE;
	    value <= AMDGPU_CTX_STABLE_PSTATE_PEAK; value++) {
		pstate = UINT32_MAX;
		assert(amdgpu_ctx_stable_pstate_flags(
		    AMDGPU_CTX_OP_SET_STABLE_PSTATE, value, &pstate) == 0);
		assert(pstate == value);
	}
	for (value = AMDGPU_CTX_STABLE_PSTATE_PEAK + 1;
	    value <= AMDGPU_CTX_STABLE_PSTATE_FLAGS_MASK; value++)
		assert(amdgpu_ctx_stable_pstate_flags(
		    AMDGPU_CTX_OP_SET_STABLE_PSTATE, value, &pstate) == -EINVAL);
	assert(amdgpu_ctx_stable_pstate_flags(AMDGPU_CTX_OP_SET_STABLE_PSTATE,
	    AMDGPU_CTX_STABLE_PSTATE_FLAGS_MASK + 1, &pstate) == -EINVAL);
	assert(amdgpu_ctx_stable_pstate_flags(AMDGPU_CTX_OP_SET_STABLE_PSTATE,
	    UINT32_MAX, &pstate) == -EINVAL);
	assert(amdgpu_ctx_stable_pstate_flags(0, 0, &pstate) == -EINVAL);

	puts("AMDGPU stable-pstate UAPI contract: PASS");
	return 0;
}
