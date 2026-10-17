/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "queue.h"
typedef uint64_t gpaddr_t;
typedef uint64_t nvmm_prot_t;
typedef uint32_t nvmm_machid_t;
@MACHINE@
@AREA@
struct nvmm_ioc_machine_create { nvmm_machid_t machid; };
static struct { uint32_t max_vcpus; } __capability = {4};
static int nvmm_fd = 7;
#define NVMM_IOC_MACHINE_CREATE 1
static void *blocks[16];
static size_t block_sizes[16], allocation_count, free_count, live_blocks;
static int fail_allocation, fail_ioctl, ioctl_count;
static void *
checked_calloc(size_t count, size_t size)
{
	void *p;

	allocation_count++;
	if ((int)allocation_count == fail_allocation) {
		errno = ENOMEM;
		return NULL;
	}
	assert(allocation_count <= 16);
	p = calloc(count, size);
	assert(p != NULL);
	blocks[allocation_count - 1] = p;
	block_sizes[allocation_count - 1] = count * size;
	live_blocks++;
	return p;
}
static void
checked_free(void *p)
{
	size_t i;

	assert(p != NULL);
	for (i = 0; i < allocation_count; i++) {
		if (blocks[i] != p)
			continue;
		blocks[i] = NULL;
		free_count++;
		live_blocks--;
		free(p);
		return;
	}
	assert(!"unknown or already freed pointer");
}
static int
checked_ioctl(int fd, unsigned long request, struct nvmm_ioc_machine_create *args)
{
	assert(fd == 7 && request == NVMM_IOC_MACHINE_CREATE);
	ioctl_count++;
	if (fail_ioctl) {
		errno = EIO;
		return -1;
	}
	args->machid = 9;
	return 0;
}
#define calloc checked_calloc
#define free checked_free
#define ioctl checked_ioctl
@PRODUCTION@
#undef calloc
#undef free
#undef ioctl
static void
reset(int allocation_failure, int ioctl_failure)
{
	assert(live_blocks == 0);
	memset(blocks, 0, sizeof(blocks));
	memset(block_sizes, 0, sizeof(block_sizes));
	allocation_count = free_count = 0;
	fail_allocation = allocation_failure;
	fail_ioctl = ioctl_failure;
	ioctl_count = 0;
	errno = 0;
}
int
main(void)
{
	struct nvmm_machine mach;
	unsigned char unchanged[sizeof(mach)];
	area_list_t *areas;
	area_t *ent;
	size_t i, j;
	int result, mode;

	for (mode = 1; mode <= 3; mode++) {
		reset(mode <= 2 ? mode : 0, mode == 3);
		memset(&mach, 0xa5, sizeof(mach));
		memcpy(unchanged, &mach, sizeof(mach));
		result = nvmm_machine_create(&mach);
		assert(result == -1);
		assert(errno == (mode == 3 ? EIO : ENOMEM));
		assert(memcmp(unchanged, &mach, sizeof(mach)) == 0);
		assert(allocation_count == (mode == 1 ? 1 : 2));
		assert(ioctl_count == (mode == 3 ? 1 : 0));
		assert(free_count == (mode == 1 ? 0 : mode == 2 ? 1 : 2));
		assert(live_blocks == 0);
	}
	for (i = 0; i <= 3; i++) {
		reset(0, 0);
		memset(&mach, 0xa5, sizeof(mach));
		result = nvmm_machine_create(&mach);
		assert(result == 0 && errno == 0);
		assert(ioctl_count == 1 && mach.machid == 9);
		assert(mach.areas == blocks[0] && mach.pages == blocks[1]);
		assert(block_sizes[0] == sizeof(area_list_t));
		assert(block_sizes[1] == 4 * sizeof(*mach.pages));
		assert(live_blocks == 2 && free_count == 0);
		for (j = 0; j < 4; j++)
			assert(mach.pages[j] == NULL);
		areas = mach.areas;
		assert(LIST_EMPTY(areas));
		for (j = 0; j < i; j++) {
			ent = checked_calloc(1, sizeof(*ent));
			LIST_INSERT_HEAD(areas, ent, list);
		}
		__area_remove_all(&mach);
		assert(live_blocks == 1 && free_count == i + 1);
		checked_free(mach.pages);
		assert(live_blocks == 0 && free_count == i + 2);
	}
	puts("3 creation failures + success/list sizes0..3: ownership, errno, output publication and exact-once frees PASS");
	return 0;
}
