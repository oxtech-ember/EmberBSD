/* Origin: EmberBSD native V3D CL userspace acceptance, 2026-10-11. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Reuse the accepted GEM helpers and exact queue-probe packet builder. */
#define main tfu_acceptance_main
#include "bcm2712-v3d-render-check.c"
#undef main
#define KASSERT(c) check((c), #c)
#include "cl-packets.h"

#define CL_MAX_BOS 91

static uint32_t
cl_guard(unsigned int bo_index)
{

	return GUARD ^ (bo_index - 4) * UINT32_C(0x01020305);
}

static void
render(const char *node, unsigned int rounds, unsigned int count)
{
	struct buffer bo[CL_MAX_BOS];
	struct drm_v3d_submit_cl a;
	struct drm_syncobj_create sync = {.flags = DRM_SYNCOBJ_CREATE_SIGNALED};
	uint32_t handles[CL_MAX_BOS], command_copy[BO_SIZE / 4];
	struct qv3d_cl bcl, rcl;
	int fd = open(node, O_RDWR | O_CLOEXEC);

	check(count >= 5 && count <= CL_MAX_BOS, "CL BO count out of range");
	if (fd < 0) err(1, "open CL node");
	if (ioctl(fd, DRM_IOCTL_SYNCOBJ_CREATE, &sync) < 0)
		err(1, "CL SYNCOBJ_CREATE");
	for (unsigned int i = 0; i < count; i++) {
		bo[i] = allocate(fd);
		handles[i] = bo[i].handle;
		check((uint64_t)bo[i].gpu + BO_SIZE <= UINT64_C(0x100000000),
		    "CL BO address wraps");
		for (unsigned int j = 0; j < i; j++) {
			check(handles[i] != handles[j], "CL duplicate BO handle");
			check((uint64_t)bo[i].gpu + BO_SIZE <= bo[j].gpu ||
			    (uint64_t)bo[j].gpu + BO_SIZE <= bo[i].gpu,
			    "CL overlapping BO addresses");
		}
	}
	/* BCL and RCL share BO 0; tile allocation/state and output are separate. */
	for (unsigned int i = 0; i < BO_SIZE / 4; i++) bo[0].map[i] = GUARD;
	bcl = (struct qv3d_cl){.base = (uint8_t *)bo[0].map};
	rcl = (struct qv3d_cl){.base = (uint8_t *)bo[0].map + 4096};
	qv3d_build_bcl(&bcl);
	qv3d_build_rcl(&rcl, bo[0].gpu + 4096, bo[3].gpu, bo[1].gpu);
	check(qv3d_length(&bcl) <= 4096 && qv3d_length(&rcl) <= 4096,
	    "CL builder overflow");
	memcpy(command_copy, bo[0].map, sizeof(command_copy));
	a = (struct drm_v3d_submit_cl){
	    .bcl_start = bo[0].gpu, .bcl_end = bo[0].gpu + qv3d_length(&bcl),
	    .rcl_start = bo[0].gpu + 4096,
	    .rcl_end = bo[0].gpu + 4096 + qv3d_length(&rcl),
	    .qma = bo[1].gpu, .qms = QV3D_TILE_ALLOC_SIZE, .qts = bo[2].gpu,
	    .bo_handles = (uintptr_t)handles, .bo_handle_count = count,
	    .in_sync_bcl = sync.handle, .in_sync_rcl = sync.handle,
	    .out_sync = sync.handle};
	for (unsigned int n = 0; n < rounds; n++) {
		for (unsigned int i = 0; i < BO_SIZE / 4; i++) {
			bo[1].map[i] = i < QV3D_TILE_ALLOC_SIZE / 4 ? 0 : GUARD;
			bo[2].map[i] = i < 4096 / 4 ? 0 : GUARD;
			bo[3].map[i] = GUARD;
			for (unsigned int b = 4; b < count; b++)
				bo[b].map[i] = cl_guard(b);
		}
		if (ioctl(fd, DRM_IOCTL_V3D_SUBMIT_CL, &a) < 0)
			err(1, "SUBMIT_CL round %u", n);
		struct drm_syncobj_wait wait = {.handles = (uintptr_t)&sync.handle,
		    .count_handles = 1, .flags = DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL};
		if (ioctl(fd, DRM_IOCTL_SYNCOBJ_WAIT, &wait) < 0)
			err(1, "CL fence not retired");
		for (unsigned int i = 0; i < BO_SIZE / 4; i++) {
			uint32_t expected = i < WORDS ? QV3D_CLEAR_COLOR : GUARD;
			if (bo[3].map[i] != expected)
				errx(1, "CL round %u word %u: %#x expected %#x", n, i,
				    bo[3].map[i], expected);
			check(bo[0].map[i] == command_copy[i], "CL modified commands/guard");
			for (unsigned int b = 4; b < count; b++)
				check(bo[b].map[i] == cl_guard(b),
				    "CL wrote unrelated admitted BO");
			if (i >= QV3D_TILE_ALLOC_SIZE / 4)
				check(bo[1].map[i] == GUARD, "CL tile allocation guard");
			if (i >= 4096 / 4) check(bo[2].map[i] == GUARD, "CL tile state guard");
		}
	}
	for (unsigned int i = 0; i < count; i++) {
		close_handle(fd, bo[i].handle);
		check(munmap(bo[i].map, BO_SIZE) == 0, "CL munmap");
	}
	check(close(fd) == 0, "CL close");
}

int
main(int argc, char **argv)
{
	pid_t child[2];
	int result;

	if (argc != 2) errx(1, "usage: %s /dev/dri/renderD128", argv[0]);
	setvbuf(stdout, NULL, _IONBF, 0);
	puts("V3D 7.1 CL: guarded 64x64 RGBA8 clear/store through native DRM");
	render(argv[1], 260, 5);
	puts("PASS: 260 bin/render pairs, 1064960 pixels and all guards");
	for (unsigned int i = 0; i < 2; i++) {
		child[i] = fork();
		if (child[i] < 0) err(1, "fork");
		if (child[i] == 0) { render(argv[1], 32, 5); _exit(0); }
	}
	for (unsigned int i = 0; i < 2; i++) {
		check(waitpid(child[i], &result, 0) == child[i], "waitpid");
		check(WIFEXITED(result) && WEXITSTATUS(result) == 0, "parallel CL client failed");
	}
	puts("PASS: 324 bin/render pairs; testing CL/TFU/CL transitions");
	int fd = open(argv[1], O_RDWR | O_CLOEXEC);
	if (fd < 0) err(1, "open TFU node");
	copy_test(fd, 32);
	check(close(fd) == 0, "TFU close");
	render(argv[1], 1, 5);
	puts("PASS: 325 CL pairs, 1331200 pixels, 32 TFU copies, binary fences");
	render(argv[1], 4, 72);
	render(argv[1], 4, 91);
	puts("PASS: 333 CL pairs, 1363968 pixels, 72/91 distinct BO lists and guards");
	return 0;
}
