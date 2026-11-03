/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD GE8300 DRM userspace compute acceptance, AI-assisted. */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <sha2.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "pvr_drm.h"
#include "pvr_fwcompute_file.h"

#define GE_BVNC UINT64_C(0x0016006600360026)
#define IMAGE_BYTES 322452
#define IMAGE_SHA "b7026df94875097338243e6bc1532be70c461f73995f78c29cb172b04b989dc6"

struct object {
	uint32_t handle;
	uint64_t va;
	size_t size;
	void *cpu;
};

static void
must(bool condition, const char *what)
{
	if (!condition) {
		fprintf(stderr, "%s: %s\n", what, strerror(errno));
		exit(1);
	}
}

static uint64_t
probe_round_page(uint64_t n)
{
	return (n + 4095) & ~UINT64_C(4095);
}

static void
check_identity(int fd)
{
	struct drm_pvr_dev_query_gpu_info info = { 0 };
	struct drm_pvr_ioctl_dev_query_args query = {
		.type = DRM_PVR_DEV_QUERY_GPU_INFO_GET,
		.size = sizeof(info), .pointer = (uintptr_t)&info
	};

	struct drm_pvr_dev_query_quirks quirks = { 0 };
	uint32_t values[16] = { 0 };
	bool found = false;
	size_t i;

	must(ioctl(fd, DRM_IOCTL_PVR_DEV_QUERY, &query) == 0 &&
	    info.gpu_id == GE_BVNC, "GE device identity");
	query.type = DRM_PVR_DEV_QUERY_QUIRKS_GET;
	query.size = sizeof(quirks);
	query.pointer = (uintptr_t)&quirks;
	must(ioctl(fd, DRM_IOCTL_PVR_DEV_QUERY, &query) == 0 &&
	    quirks.count <= 16, "GE quirk count");
	quirks.quirks = (uintptr_t)values;
	quirks.count = 16;
	must(ioctl(fd, DRM_IOCTL_PVR_DEV_QUERY, &query) == 0,
	    "GE quirks");
	for (i = 0; i < quirks.count; i++)
		if (values[i] == 49927)
			found = true;
	must(found, "GE BRN49927");
}

static void
make_object(int fd, uint32_t vm, const struct pvr_fwcompute_file_entry *entry,
    const uint8_t *payload, struct object *obj)
{
	struct drm_pvr_ioctl_create_bo_args bo = { 0 };
	struct drm_pvr_ioctl_get_bo_mmap_offset_args off = { 0 };
	struct drm_pvr_ioctl_vm_map_args map = { 0 };

	obj->size = probe_round_page(entry->bytes);
	obj->va = entry->va;
	bo.size = obj->size;
	bo.flags = DRM_PVR_BO_ALLOW_CPU_USERSPACE_ACCESS;
	if ((entry->flags & PVR_FWCOMPUTE_FLAG_BYPASS) != 0)
		bo.flags |= DRM_PVR_BO_BYPASS_DEVICE_CACHE;
	must(ioctl(fd, DRM_IOCTL_PVR_CREATE_BO, &bo) == 0, "create BO");
	obj->handle = bo.handle;
	off.handle = bo.handle;
	must(ioctl(fd, DRM_IOCTL_PVR_GET_BO_MMAP_OFFSET, &off) == 0,
	    "BO mmap offset");
	obj->cpu = mmap(NULL, obj->size, PROT_READ | PROT_WRITE, MAP_SHARED,
	    fd, (off_t)off.offset);
	must(obj->cpu != MAP_FAILED, "BO mmap");
	memcpy(obj->cpu, payload + entry->offset, entry->bytes);
	map.vm_context_handle = vm;
	map.device_addr = entry->va;
	map.handle = bo.handle;
	map.size = obj->size;
	must(ioctl(fd, DRM_IOCTL_PVR_VM_MAP, &map) == 0, "VM map");
}

static int
queue_value(const char *name)
{
	int value;
	size_t size = sizeof(value);

	must(sysctlbyname(name, &value, &size, NULL, 0) == 0 &&
	    size == sizeof(value), "read cached queue status");
	return value;
}

static void
negative_controls(int fd, const char *node, uint32_t vm,
    const struct pvr_fwcompute_file *image, const uint8_t *payload)
{
	struct drm_pvr_ioctl_create_bo_args foreign = { .size = 4096 };
	struct drm_gem_close close_bo = { 0 };
	struct drm_pvr_ioctl_vm_map_args map = { 0 };
	struct drm_pvr_ioctl_create_context_args ctx = { 0 };
	struct drm_pvr_ioctl_create_hwrt_dataset_args hwrt = { 0 };
	struct drm_pvr_job job = { 0 };
	struct drm_pvr_ioctl_submit_jobs_args submit = { 0 };
	int other, read_before, write_before, submit_rc, submit_errno;

	read_before = queue_value("hw.sun50ia133gpu0.drm_queue_read");
	write_before = queue_value("hw.sun50ia133gpu0.drm_queue_write");
	other = open(node, O_RDWR);
	must(other >= 0, "open second render fd");
	must(ioctl(other, DRM_IOCTL_PVR_CREATE_BO, &foreign) == 0,
	    "create foreign BO");
	map.vm_context_handle = vm;
	map.device_addr = UINT64_C(0x8000100000);
	map.handle = foreign.handle;
	map.size = 4096;
	errno = 0;
	must(ioctl(fd, DRM_IOCTL_PVR_VM_MAP, &map) == -1 && errno == ENOENT,
	    "reject foreign BO handle");
	close_bo.handle = foreign.handle;
	must(ioctl(other, DRM_IOCTL_GEM_CLOSE, &close_bo) == 0 &&
	    close(other) == 0, "close foreign BO");
	ctx.type = DRM_PVR_CTX_TYPE_RENDER;
	ctx.vm_context_handle = vm;
	errno = 0;
	must(ioctl(fd, DRM_IOCTL_PVR_CREATE_CONTEXT, &ctx) == -1 &&
	    errno == EOPNOTSUPP, "reject render context");
	errno = 0;
	must(ioctl(fd, DRM_IOCTL_PVR_CREATE_HWRT_DATASET, &hwrt) == -1 &&
	    errno == EOPNOTSUPP, "reject render target");
	job.type = DRM_PVR_JOB_TYPE_COMPUTE;
	job.context_handle = UINT32_MAX;
	job.cmd_stream_len = 44;
	job.cmd_stream = (uintptr_t)(payload + image->command_offset);
	submit.jobs = (struct drm_pvr_obj_array) {
		.stride = sizeof(job), .count = 1, .array = (uintptr_t)&job
	};
	errno = 0;
	submit_rc = ioctl(fd, DRM_IOCTL_PVR_SUBMIT_JOBS, &submit);
	submit_errno = errno;
	/* NetBSD copies ioctl output to userspace only on success. */
	if (submit_rc != -1 || submit_errno != EINVAL ||
	    submit.jobs.count != 1) {
		fprintf(stderr, "reject unknown context: rc=%d errno=%d count=%u\n",
		    submit_rc, submit_errno, submit.jobs.count);
		exit(1);
	}
	must(queue_value("hw.sun50ia133gpu0.drm_queue_read") == read_before &&
	    queue_value("hw.sun50ia133gpu0.drm_queue_write") == write_before &&
	    queue_value("hw.sun50ia133gpu0.drm_queue_error") == 0,
	    "negative controls do not publish firmware commands");
}

static void
run_once(int fd, uint32_t vm, const struct pvr_fwcompute_file *image,
    const uint8_t *payload, struct object *scratch)
{
	uint8_t state[64] = { 0 };
	struct drm_pvr_ioctl_create_context_args ctx = { 0 };
	struct drm_pvr_ioctl_destroy_context_args destroy = { 0 };
	struct drm_syncobj_create sync = { 0 };
	struct drm_syncobj_destroy sync_destroy = { 0 };
	struct drm_syncobj_wait wait = { 0 };
	struct drm_pvr_sync_op signal = { 0 };
	struct drm_pvr_job job = { 0 };
	struct drm_pvr_ioctl_submit_jobs_args submit = { 0 };
	struct timespec now;
	uint32_t state_size = sizeof(state), flags, canary;
	int before_cleanup;

	memcpy(state, &state_size, sizeof(state_size));
	memcpy(state + 8, payload + image->static_offset, 56);
	ctx.type = DRM_PVR_CTX_TYPE_COMPUTE;
	ctx.priority = DRM_PVR_CTX_PRIORITY_NORMAL;
	ctx.vm_context_handle = vm;
	ctx.static_context_state = (uintptr_t)state;
	ctx.static_context_state_len = sizeof(state);
	must(ioctl(fd, DRM_IOCTL_PVR_CREATE_CONTEXT, &ctx) == 0,
	    "create compute context");
	must(ioctl(fd, DRM_IOCTL_SYNCOBJ_CREATE, &sync) == 0, "create syncobj");
	memcpy(&flags, payload + image->command_offset + 44, sizeof(flags));
	signal.handle = sync.handle;
	signal.flags = DRM_PVR_SYNC_OP_FLAG_SIGNAL;
	job.type = DRM_PVR_JOB_TYPE_COMPUTE;
	job.context_handle = ctx.handle;
	job.flags = flags;
	job.cmd_stream_len = 44;
	job.cmd_stream = (uintptr_t)(payload + image->command_offset);
	job.sync_ops = (struct drm_pvr_obj_array) {
		.stride = sizeof(signal), .count = 1, .array = (uintptr_t)&signal
	};
	submit.jobs = (struct drm_pvr_obj_array) {
		.stride = sizeof(job), .count = 1, .array = (uintptr_t)&job
	};
	memset(scratch->cpu, 0, sizeof(canary));
	must(ioctl(fd, DRM_IOCTL_PVR_SUBMIT_JOBS, &submit) == 0 &&
	    submit.jobs.count == 1, "submit compute");
	must(clock_gettime(CLOCK_MONOTONIC, &now) == 0, "monotonic time");
	wait.handles = (uintptr_t)&sync.handle;
	wait.count_handles = 1;
	wait.flags = DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL;
	wait.timeout_nsec = ((int64_t)now.tv_sec + 5) * INT64_C(1000000000) + now.tv_nsec;
	must(ioctl(fd, DRM_IOCTL_SYNCOBJ_WAIT, &wait) == 0, "syncobj wait");
	memcpy(&canary, scratch->cpu, sizeof(canary));
	if (canary != UINT32_C(0x13579bdf)) {
		fprintf(stderr, "GPU canary: got 0x%08x, expected 0x13579bdf\n",
		    canary);
		exit(1);
	}
	before_cleanup = queue_value("hw.sun50ia133gpu0.drm_queue_read");
	destroy.handle = ctx.handle;
	must(ioctl(fd, DRM_IOCTL_PVR_DESTROY_CONTEXT, &destroy) == 0,
	    "destroy context handle");
	must(queue_value("hw.sun50ia133gpu0.drm_failed") == 0 &&
	    queue_value("hw.sun50ia133gpu0.drm_queue_error") == 0 &&
	    queue_value("hw.sun50ia133gpu0.drm_queue_read") != before_cleanup &&
	    queue_value("hw.sun50ia133gpu0.drm_queue_read") ==
	    queue_value("hw.sun50ia133gpu0.drm_queue_write") &&
	    queue_value("hw.sun50ia133gpu0.drm_queue_command") == 0x2abc0066,
	    "acknowledged cleanup and MMUCACHE");
	sync_destroy.handle = sync.handle;
	must(ioctl(fd, DRM_IOCTL_SYNCOBJ_DESTROY, &sync_destroy) == 0,
	    "destroy syncobj");
}

int
main(int argc, char **argv)
{
	struct pvr_fwcompute_file image;
	struct drm_pvr_ioctl_create_vm_context_args vm = { 0 };
	struct drm_pvr_ioctl_destroy_vm_context_args vm_destroy = { 0 };
	struct object objects[PVR_FWCOMPUTE_RESOURCES] = { 0 }, *scratch = NULL;
	struct stat st;
	uint8_t *bytes, *payload;
	char digest[SHA256_DIGEST_STRING_LENGTH];
	FILE *file;
	int fd, before_unmap, final_read, final_write, final_command, final_error,
	    final_failed;
	size_t i;

	if (argc != 3) {
		fprintf(stderr, "usage: %s /dev/dri/renderD... GE-compute-image\n", argv[0]);
		return 2;
	}
	must(getpagesize() == 4096, "4K host pages");
	file = fopen(argv[2], "rb");
	must(file != NULL && fstat(fileno(file), &st) == 0 &&
	    st.st_size == IMAGE_BYTES, "open pinned image");
	bytes = malloc(IMAGE_BYTES);
	must(bytes != NULL && fread(bytes, 1, IMAGE_BYTES, file) == IMAGE_BYTES &&
	    fclose(file) == 0, "read image");
	must(SHA256_Data(bytes, IMAGE_BYTES, digest) != NULL &&
	    strcmp(digest, IMAGE_SHA) == 0, "image SHA256");
	must(pvr_fwcompute_file_parse(bytes, IMAGE_BYTES, GE_BVNC, &image) == 0,
	    "image structure");
	payload = bytes + image.header_bytes;
	fd = open(argv[1], O_RDWR);
	must(fd >= 0, "open render node");
	check_identity(fd);
	must(ioctl(fd, DRM_IOCTL_PVR_CREATE_VM_CONTEXT, &vm) == 0, "create VM");
	negative_controls(fd, argv[1], vm.handle, &image, payload);
	for (i = 0; i < PVR_FWCOMPUTE_RESOURCES; i++) {
		make_object(fd, vm.handle, &image.resource[i], payload, &objects[i]);
		if (image.resource[i].role == PVR_FWCOMPUTE_SCRATCH)
			scratch = &objects[i];
	}
	must(scratch != NULL, "scratch object");
	run_once(fd, vm.handle, &image, payload, scratch);
	run_once(fd, vm.handle, &image, payload, scratch);
	before_unmap = queue_value("hw.sun50ia133gpu0.drm_queue_read");
	for (i = 0; i < PVR_FWCOMPUTE_RESOURCES; i++) {
		struct drm_pvr_ioctl_vm_unmap_args unmap = {
			.vm_context_handle = vm.handle, .device_addr = objects[i].va,
			.size = objects[i].size
		};
		struct drm_gem_close close_bo = { .handle = objects[i].handle };
		must(ioctl(fd, DRM_IOCTL_PVR_VM_UNMAP, &unmap) == 0, "VM unmap");
		must(munmap(objects[i].cpu, objects[i].size) == 0, "BO munmap");
		must(ioctl(fd, DRM_IOCTL_GEM_CLOSE, &close_bo) == 0, "close BO");
	}
	vm_destroy.handle = vm.handle;
	must(ioctl(fd, DRM_IOCTL_PVR_DESTROY_VM_CONTEXT, &vm_destroy) == 0,
	    "destroy VM");
	final_failed = queue_value("hw.sun50ia133gpu0.drm_failed");
	final_error = queue_value("hw.sun50ia133gpu0.drm_queue_error");
	final_read = queue_value("hw.sun50ia133gpu0.drm_queue_read");
	final_write = queue_value("hw.sun50ia133gpu0.drm_queue_write");
	final_command = queue_value("hw.sun50ia133gpu0.drm_queue_command");
	if (final_failed != 0 || final_error != 0 ||
	    final_read == before_unmap || final_read != final_write ||
	    final_command != 0x2abc0066) {
		fprintf(stderr, "VM retirement: failed=%d error=%d read=%d "
		    "write=%d command=0x%x before=%d\n", final_failed,
		    final_error, final_read, final_write, (unsigned)final_command,
		    before_unmap);
		exit(1);
	}
	must(close(fd) == 0, "close render node");
	free(bytes);
	puts("PASS: GE8300 userspace compute, two contexts and canary checks");
	return 0;
}
