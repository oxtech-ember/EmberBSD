/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD native PowerVR render-node identity acceptance. */
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
	for (i = 1; i <= 6; i++) {
		args = (struct drm_pvr_ioctl_dev_query_args){ i, 0, 0 };
		errno = 0;
		check(ioctl(fd, DRM_IOCTL_PVR_DEV_QUERY, &args) == -1 &&
		    errno == EOPNOTSUPP, "unsupported device query");
	}
	cap = (struct drm_get_cap){ .capability = DRM_CAP_SYNCOBJ };
	check(ioctl(fd, DRM_IOCTL_GET_CAP, &cap) == 0 && cap.value == 0,
	    "no syncobj capability");
	cap = (struct drm_get_cap){ .capability = DRM_CAP_DUMB_BUFFER };
	errno = 0;
	check(ioctl(fd, DRM_IOCTL_GET_CAP, &cap) == -1 &&
	    errno == EOPNOTSUPP, "reject display capability without modesetting");
	check(close(fd) == 0, "close render node");
	printf("PASS: %u PowerVR identity checks; BVNC=0x%016" PRIx64
	    "; GEM/VM/submission remain unsupported\n", checks, info.gpu_id);
	return 0;
}
