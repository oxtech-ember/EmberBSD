/* SPDX-License-Identifier: BSD-2-Clause */
/* Production AMDGPU code is inserted by amdgpu-reset-contract.py. */
@LICENSES@
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define atomic_load_relaxed(pointer) __atomic_load_n((pointer), __ATOMIC_RELAXED)
#define atomic_store_relaxed(pointer, value) \
	__atomic_store_n((pointer), (value), __ATOMIC_RELAXED)

struct amdgpu_device {
	bool in_gpu_reset;
};

@UAPI_FLAG@
@HELPERS@

static uint64_t
query_reset_flags(struct amdgpu_device *adev)
{
	struct {
		struct {
			uint64_t flags;
		} state;
	} result = { 0 }, *out = &result;

	@QUERY_BRANCH@
	return out->state.flags;
}

int
main(void)
{
	struct amdgpu_device adev = { .in_gpu_reset = false };
	uint64_t flag = AMDGPU_CTX_QUERY2_FLAGS_RESET_IN_PROGRESS;

	amdgpu_set_in_reset(&adev, true);
	assert(query_reset_flags(&adev) == flag);
	amdgpu_set_in_reset(&adev, false);
	assert(query_reset_flags(&adev) == 0);
	puts("AMDGPU reset flag contract: PASS");
	return 0;
}
