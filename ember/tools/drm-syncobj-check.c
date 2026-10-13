/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD native DRM binary/timeline API acceptance. */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include "drm.h"

static unsigned checks;
static void
check(bool pass, const char *name)
{
	checks++;
	if (!pass) {
		fprintf(stderr, "FAIL: %s (errno=%d)\n", name, errno);
		exit(1);
	}
}
static int64_t
now(void)
{
	struct timespec t;
	check(clock_gettime(CLOCK_MONOTONIC, &t) == 0, "monotonic clock");
	return (int64_t)t.tv_sec * 1000000000 + t.tv_nsec;
}
static void
pause_ms(unsigned ms)
{
	struct timespec t = { .tv_nsec = ms * 1000000 };
	while (nanosleep(&t, &t) < 0)
		check(errno == EINTR, "pause");
}
static uint32_t
create(int fd, unsigned flags)
{
	struct drm_syncobj_create a = { .flags = flags };
	check(ioctl(fd, DRM_IOCTL_SYNCOBJ_CREATE, &a) == 0, "create syncobj");
	return a.handle;
}
static void
destroy(int fd, uint32_t handle)
{
	struct drm_syncobj_destroy a = { .handle = handle };
	check(ioctl(fd, DRM_IOCTL_SYNCOBJ_DESTROY, &a) == 0, "destroy syncobj");
}
static void
array(int fd, unsigned long request, uint32_t *handles, unsigned count)
{
	struct drm_syncobj_array a = {
		.handles = (uintptr_t)handles, .count_handles = count
	};
	check(ioctl(fd, request, &a) == 0, "binary array operation");
}
static void
signal_point(int fd, uint32_t handle, uint64_t point)
{
	struct drm_syncobj_timeline_array a = {
		.handles = (uintptr_t)&handle, .points = (uintptr_t)&point,
		.count_handles = 1
	};
	check(ioctl(fd, DRM_IOCTL_SYNCOBJ_TIMELINE_SIGNAL, &a) == 0,
	    "signal timeline point");
}
static void
wait_point(int fd, uint32_t handle, uint64_t point)
{
	struct drm_syncobj_timeline_wait a = {
		.handles = (uintptr_t)&handle, .points = (uintptr_t)&point,
		.count_handles = 1
	};
	check(ioctl(fd, DRM_IOCTL_SYNCOBJ_TIMELINE_WAIT, &a) == 0,
	    "completed timeline point");
}
struct transfer_wait {
	int fd, result, error;
	struct drm_syncobj_transfer args;
	atomic_bool entered;
};
static void *
transfer_thread(void *data)
{
	struct transfer_wait *w = data;
	atomic_store(&w->entered, true);
	w->result = ioctl(w->fd, DRM_IOCTL_SYNCOBJ_TRANSFER, &w->args);
	w->error = errno;
	return NULL;
}
static void
transfer_publication(int fd)
{
	for (unsigned round = 0; round < 32; round++) {
		uint32_t src = create(fd, 0), dst = create(fd, 0);
		struct transfer_wait w = { .fd = fd, .args = {
			.src_handle = src, .dst_handle = dst,
			.src_point = round % 2 ? 9 : 0,
			.dst_point = round % 3 ? 11 : 0,
			.flags = DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT
		} };
		pthread_t thread;
		atomic_init(&w.entered, false);
		int64_t start = now();
		check(pthread_create(&thread, NULL, transfer_thread, &w) == 0,
		    "start transfer waiter");
		while (!atomic_load(&w.entered))
			sched_yield();
		pause_ms(10);
		if (w.args.src_point)
			signal_point(fd, src, w.args.src_point);
		else
			array(fd, DRM_IOCTL_SYNCOBJ_SIGNAL, &src, 1);
		check(pthread_join(thread, NULL) == 0, "join transfer waiter");
		if (w.result != 0)
			fprintf(stderr, "TRANSFER round=%u result=%d errno=%d\n",
			    round, w.result, w.error);
		check(w.result == 0 && now() - start < 2000000000,
		    "publication completes without five-second timeout");
		wait_point(fd, dst, w.args.dst_point);
		destroy(fd, src);
		wait_point(fd, dst, w.args.dst_point);
		destroy(fd, dst);
	}
}
static void
timeline(int fd)
{
	uint32_t src = create(fd, 0), dst = create(fd, 0);
	struct drm_syncobj_transfer x = { .src_handle = src, .dst_handle = dst };
	for (unsigned n = 0; n < 32; n++) {
		if (n == 1)
			continue;
		x.flags = UINT32_C(1) << n;
		errno = 0;
		check(ioctl(fd, DRM_IOCTL_SYNCOBJ_TRANSFER, &x) < 0 && errno == EINVAL,
		    "reject unknown transfer flags");
	}
	x.flags = 0; x.pad = 1; errno = 0;
	check(ioctl(fd, DRM_IOCTL_SYNCOBJ_TRANSFER, &x) < 0 && errno == EINVAL,
	    "reject transfer padding");
	x.pad = 0; errno = 0;
	check(ioctl(fd, DRM_IOCTL_SYNCOBJ_TRANSFER, &x) < 0 && errno == EINVAL,
	    "reject empty source without publication wait");
	for (uint64_t p = 1; p <= 256; p++) {
		signal_point(fd, src, p);
		wait_point(fd, src, p);
		wait_point(fd, src, 1);
		uint64_t got = UINT64_MAX;
		struct drm_syncobj_timeline_array q = {
			.handles = (uintptr_t)&src, .points = (uintptr_t)&got,
			.count_handles = 1
		};
		check(ioctl(fd, DRM_IOCTL_SYNCOBJ_QUERY, &q) == 0 && got == p,
		    "completed timeline sequence");
		q.flags = DRM_SYNCOBJ_QUERY_FLAGS_LAST_SUBMITTED;
		check(ioctl(fd, DRM_IOCTL_SYNCOBJ_QUERY, &q) == 0 && got == p,
		    "submitted timeline sequence");
	}
	x.src_point = 256; x.dst_point = 5;
	check(ioctl(fd, DRM_IOCTL_SYNCOBJ_TRANSFER, &x) == 0,
	    "transfer timeline point");
	destroy(fd, src); wait_point(fd, dst, 5);
	struct drm_syncobj_handle exported = { .handle = dst };
	check(ioctl(fd, DRM_IOCTL_SYNCOBJ_HANDLE_TO_FD, &exported) == 0,
	    "export opaque syncobj fd");
	destroy(fd, dst);
	struct drm_syncobj_handle imported = { .fd = exported.fd };
	check(ioctl(fd, DRM_IOCTL_SYNCOBJ_FD_TO_HANDLE, &imported) == 0,
	    "import retained timeline");
	check(close(exported.fd) == 0, "close opaque fd");
	wait_point(fd, imported.handle, 5); destroy(fd, imported.handle);
}
int
main(int argc, char **argv)
{
	if (argc != 2) {
		fprintf(stderr, "usage: %s /dev/dri/renderD128\n", argv[0]);
		return 2;
	}
	setvbuf(stdout, NULL, _IONBF, 0);
	alarm(30);
	int fd = open(argv[1], O_RDWR | O_CLOEXEC);
	check(fd >= 0, "open render node");
	const uint64_t caps[] = { DRM_CAP_SYNCOBJ, DRM_CAP_SYNCOBJ_TIMELINE };
	for (unsigned i = 0; i < 2; i++) {
		struct drm_get_cap a = { .capability = caps[i] };
		check(ioctl(fd, DRM_IOCTL_GET_CAP, &a) == 0 && a.value == 1,
		    "syncobj capability");
	}
	timeline(fd);
	transfer_publication(fd);
	check(close(fd) == 0, "close DRM file");
	printf("PASS: %u native DRM syncobj checks; no GPU submission\n", checks);
	return 0;
}
