/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD native PowerVR identity and firmware-query acceptance. */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "pvr_drm.h"

static unsigned int checks;

static void
check(bool pass, const char *what)
{

	checks++;
	if (!pass) {
		fprintf(stderr, "FAIL: %s (errno=%d)\n", what, errno);
		exit(1);
	}
}

static void
check_device_queries(int fd)
{
	const struct drm_pvr_dev_query_runtime_info expected = {
		25, 524288, 11264, 6144, 4096, 4096
	};
	struct drm_pvr_ioctl_dev_query_args args;
	struct drm_pvr_dev_query_quirks quirks = { 0 };
	struct drm_pvr_dev_query_enhancements enhancements = { 0 };
	struct drm_pvr_dev_query_runtime_info runtime;
	uint32_t values[2] = { UINT32_MAX, UINT32_MAX };
	uint8_t bytes[64];
	unsigned int i;

	args = (struct drm_pvr_ioctl_dev_query_args){
	    DRM_PVR_DEV_QUERY_RUNTIME_INFO_GET, 0, 0 };
	check(ioctl(fd, DRM_IOCTL_PVR_DEV_QUERY, &args) == 0 &&
	    args.size == sizeof(runtime), "RUNTIME_INFO size discovery");
	memset(bytes, 0xa5, sizeof(bytes));
	args.pointer = (uintptr_t)bytes;
	args.size = sizeof(bytes);
	check(ioctl(fd, DRM_IOCTL_PVR_DEV_QUERY, &args) == 0 &&
	    args.size == sizeof(runtime), "RUNTIME_INFO extended output");
	memcpy(&runtime, bytes, sizeof(runtime));
	check(memcmp(&runtime, &expected, sizeof(runtime)) == 0,
	    "firmware-derived runtime limits");
	for (i = sizeof(runtime); i < sizeof(bytes); i++)
		check(bytes[i] == 0, "runtime extension cleared");
	args.size = sizeof(runtime) - 1;
	errno = 0;
	check(ioctl(fd, DRM_IOCTL_PVR_DEV_QUERY, &args) == -1 && errno == EINVAL,
	    "reject short runtime object");
	args = (struct drm_pvr_ioctl_dev_query_args){
	    DRM_PVR_DEV_QUERY_QUIRKS_GET, 0, 0 };
	check(ioctl(fd, DRM_IOCTL_PVR_DEV_QUERY, &args) == 0 &&
	    args.size == sizeof(quirks), "QUIRKS size discovery");
	args.pointer = (uintptr_t)&quirks;
	check(ioctl(fd, DRM_IOCTL_PVR_DEV_QUERY, &args) == 0 &&
	    quirks.count == 0 && quirks.musthave_count == 0, "QUIRKS count discovery");
	quirks.quirks = (uintptr_t)values;
	quirks.count = 2;
	check(ioctl(fd, DRM_IOCTL_PVR_DEV_QUERY, &args) == 0 &&
	    quirks.count == 0 && quirks.musthave_count == 0 && values[0] == UINT32_MAX,
	    "empty exported quirk set");
	quirks._padding_c = 1;
	errno = 0;
	check(ioctl(fd, DRM_IOCTL_PVR_DEV_QUERY, &args) == -1 && errno == EINVAL,
	    "reject quirk reserved fields");
	args = (struct drm_pvr_ioctl_dev_query_args){
	    DRM_PVR_DEV_QUERY_ENHANCEMENTS_GET, 0, 0 };
	check(ioctl(fd, DRM_IOCTL_PVR_DEV_QUERY, &args) == 0 &&
	    args.size == sizeof(enhancements), "ENHANCEMENTS size discovery");
	args.pointer = (uintptr_t)&enhancements;
	check(ioctl(fd, DRM_IOCTL_PVR_DEV_QUERY, &args) == 0 &&
	    enhancements.count == 1, "ENHANCEMENTS count discovery");
	enhancements.enhancements = (uintptr_t)values;
	enhancements.count = 0;
	errno = 0;
	check(ioctl(fd, DRM_IOCTL_PVR_DEV_QUERY, &args) == -1 && errno == E2BIG,
	    "reject short enhancement array");
	enhancements.count = 2;
	check(ioctl(fd, DRM_IOCTL_PVR_DEV_QUERY, &args) == 0 &&
	    enhancements.count == 1 && values[0] == 35421 && values[1] == UINT32_MAX,
	    "firmware enhancement ERN35421");
	memset(bytes, 0, sizeof(bytes));
	memcpy(bytes, &enhancements, sizeof(enhancements));
	bytes[sizeof(enhancements)] = 1;
	args.pointer = (uintptr_t)bytes;
	args.size = sizeof(bytes);
	errno = 0;
	check(ioctl(fd, DRM_IOCTL_PVR_DEV_QUERY, &args) == -1 && errno == E2BIG,
	    "reject nonzero unknown enhancement fields");
	printf("Runtime: PM pages=%" PRIu64 "..%" PRIu64
	    "; common store allocation=%u partition=%u; coefficients=%u local=%u dwords\n",
	    (uint64_t)runtime.free_list_min_pages, (uint64_t)runtime.free_list_max_pages,
	    runtime.common_store_alloc_region_size, runtime.common_store_partition_space_size,
	    runtime.max_coeffs, runtime.cdm_max_local_mem_size_regs);
}

int
main(int argc, char **argv)
{
	struct drm_pvr_ioctl_dev_query_args args;
	struct drm_pvr_dev_query_gpu_info info;
	struct drm_get_cap cap;
	struct drm_version version;
	struct stat sb;
	uint8_t bytes[64];
	uint64_t expected;
	char name[32], *end;
	unsigned int i;
	int fd;

	if (argc != 3) {
		fprintf(stderr, "usage: %s /dev/dri/renderD128 expected-bvnc\n", argv[0]);
		return 2;
	}
	errno = 0;
	expected = strtoull(argv[2], &end, 0);
	check(errno == 0 && argv[2][0] != 0 && argv[2][0] != '-' &&
	    *end == 0 && expected != 0, "expected BVNC");
	fd = open(argv[1], O_RDWR);
	check(fd >= 0, "open render node");
	check(fstat(fd, &sb) == 0 && S_ISCHR(sb.st_mode), "character device");
	memset(&version, 0, sizeof(version));
	memset(name, 0, sizeof(name));
	version.name = name;
	version.name_len = sizeof(name) - 1;
	check(ioctl(fd, DRM_IOCTL_VERSION, &version) == 0, "DRM version");
	check(version.name_len == strlen("powervr") &&
	    memcmp(name, "powervr", strlen("powervr")) == 0, "PowerVR driver name");
	args = (struct drm_pvr_ioctl_dev_query_args){ 0, 0, 0 };
	check(ioctl(fd, DRM_IOCTL_PVR_DEV_QUERY, &args) == 0 &&
	    args.size == sizeof(info), "GPU_INFO size discovery");
	memset(bytes, 0xa5, sizeof(bytes));
	args.size = sizeof(bytes);
	args.pointer = (uintptr_t)bytes;
	check(ioctl(fd, DRM_IOCTL_PVR_DEV_QUERY, &args) == 0 &&
	    args.size == sizeof(info), "GPU_INFO extended output");
	memcpy(&info, bytes, sizeof(info));
	check(info.gpu_id == expected && info.num_phantoms == 1,
	    "measured BVNC and supported profile");
	for (i = 12; i < sizeof(bytes); i++)
		check(bytes[i] == 0, "reserved and extension bytes");
	memset(bytes, 0xa5, sizeof(bytes));
	args.size = 12;
	check(ioctl(fd, DRM_IOCTL_PVR_DEV_QUERY, &args) == 0 &&
	    args.size == 12 && bytes[12] == 0xa5, "minimum GPU_INFO object");
	args.size = 11;
	errno = 0;
	check(ioctl(fd, DRM_IOCTL_PVR_DEV_QUERY, &args) == -1 &&
	    errno == EINVAL, "reject short object");
	args.size = sizeof(info);
	args.pointer = 1;
	errno = 0;
	check(ioctl(fd, DRM_IOCTL_PVR_DEV_QUERY, &args) == -1 &&
	    errno == EFAULT, "reject inaccessible output");
	check_device_queries(fd);
	for (i = 4; i <= 6; i++) {
		args = (struct drm_pvr_ioctl_dev_query_args){ i, 0, 0 };
		if (i <= 5) {
			check(ioctl(fd, DRM_IOCTL_PVR_DEV_QUERY, &args) == 0 &&
			    args.size == 16, "heap/static query discovery");
			continue;
		}
		errno = 0;
		check(ioctl(fd, DRM_IOCTL_PVR_DEV_QUERY, &args) == -1 &&
		    errno == EOPNOTSUPP, "unsupported device query");
	}
	cap = (struct drm_get_cap){ .capability = DRM_CAP_SYNCOBJ };
	check(ioctl(fd, DRM_IOCTL_GET_CAP, &cap) == 0 && cap.value == 1,
	    "native syncobj capability");
	cap = (struct drm_get_cap){ .capability = DRM_CAP_SYNCOBJ_TIMELINE };
	check(ioctl(fd, DRM_IOCTL_GET_CAP, &cap) == 0 && cap.value == 1,
	    "native timeline syncobj capability");
	cap = (struct drm_get_cap){ .capability = DRM_CAP_DUMB_BUFFER };

	errno = 0;
	check(ioctl(fd, DRM_IOCTL_GET_CAP, &cap) == -1 &&
	    errno == EOPNOTSUPP, "reject display capability without modesetting");
	check(close(fd) == 0, "close render node");
	printf("PASS: %u PowerVR identity/runtime checks; BVNC=0x%016" PRIx64
	    "; GEM/VM/submission are outside this check\n", checks, info.gpu_id);
	return 0;
}
