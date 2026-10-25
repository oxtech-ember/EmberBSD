/* SPDX-License-Identifier: BSD-2-Clause */
@SOURCE_COMMENTS@
/* Production AMDGPU discovery condition is inserted by the contract script. */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>

#define CHIP_VEGA10 10
#define GC_HWID 1
#define SDMA0_HWID 2
#define UVD_HWID 3
#define VCE_HWID 4

enum amd_ip_block_type {
	AMD_IP_BLOCK_TYPE_GFX,
	AMD_IP_BLOCK_TYPE_SDMA,
	AMD_IP_BLOCK_TYPE_UVD,
	AMD_IP_BLOCK_TYPE_VCN,
	AMD_IP_BLOCK_TYPE_JPEG,
	AMD_IP_BLOCK_TYPE_VCE,
};

struct amdgpu_device {
	void *discovery;
	int asic_type;
};

static unsigned discovery_helper_calls;
static int discovery_helper_result;
static int discovery_helper_hw_id;
static struct amdgpu_device *discovery_helper_adev;

static int
amdgpu_discovery_get_ip_version(struct amdgpu_device *adev, int hw_id,
	int *major, int *minor, int *revision)
{
	discovery_helper_calls++;
	discovery_helper_hw_id = hw_id;
	discovery_helper_adev = adev;
	if (discovery_helper_result == 0) {
		*major = 3;
		*minor = 2;
		*revision = 1;
	}
	return discovery_helper_result;
}

static uint32_t
query_ip_discovery_version(struct amdgpu_device *adev,
	enum amd_ip_block_type type)
{
	uint32_t ip_discovery_version = 0;

	@DISCOVERY_BRANCH@

	return ip_discovery_version;
}

#define EXPECT(condition, message) do { \
	if (!(condition)) { \
		fprintf(stderr, "FAIL: %s\n", (message)); \
		return 1; \
	} \
} while (0)

static void
reset_helper(void)
{
	discovery_helper_calls = 0;
	discovery_helper_result = 0;
	discovery_helper_hw_id = -1;
	discovery_helper_adev = NULL;
}

int
main(void)
{
	struct amdgpu_device adev = {
		.discovery = NULL,
		.asic_type = CHIP_VEGA10,
	};
	int discovery_storage;

	reset_helper();
	EXPECT(query_ip_discovery_version(&adev, AMD_IP_BLOCK_TYPE_GFX) == 0,
	    "NULL discovery: fallback version must remain zero");
	EXPECT(discovery_helper_calls == 0,
	    "NULL discovery: helper must not be called");

	adev.discovery = &discovery_storage;
	reset_helper();
	EXPECT(query_ip_discovery_version(&adev, AMD_IP_BLOCK_TYPE_GFX) ==
	    ((3u << 16) | (2u << 8) | 1u),
	    "initialized discovery: version encoding mismatch");
	EXPECT(discovery_helper_calls == 1 && discovery_helper_hw_id == GC_HWID &&
	    discovery_helper_adev == &adev,
	    "initialized discovery: helper call mismatch");

	adev.asic_type = CHIP_VEGA10 - 1;
	reset_helper();
	EXPECT(query_ip_discovery_version(&adev, AMD_IP_BLOCK_TYPE_GFX) == 0,
	    "older ASIC: fallback version must remain zero");
	EXPECT(discovery_helper_calls == 0,
	    "older ASIC: helper must not be called");

	puts("AMDGPU discovery guard execution contract: PASS");
	return 0;
}
