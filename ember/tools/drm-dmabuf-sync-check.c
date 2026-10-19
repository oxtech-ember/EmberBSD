/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD, AI-assisted. Native DMA-BUF sync-file ioctl smoke test. */
#include <sys/types.h>
#include <sys/ioctl.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "pvr_drm.h"
#include "../../sys/sys/dmabuf.h"

_Static_assert(sizeof(struct dma_buf_export_sync_file) == 8, "export ABI");
_Static_assert(sizeof(struct dma_buf_import_sync_file) == 8, "import ABI");

#define CYCLES 128
#define REQUIRE(x) do {                                                \
	if (!(x)) {                                                    \
		fprintf(stderr, "FAIL:%d %s errno=%d\n", __LINE__, #x,   \
		    errno);                                            \
		goto out;                                               \
	}                                                               \
} while (0)

struct resources {
	int drm, dma, sync, exported;
	uint32_t bo, syncobj;
};

static bool
close_fd(int *fd)
{
	int value;

	if (*fd < 0)
		return true;
	value = *fd;
	*fd = -1;
	return close(value) == 0;
}

static bool
close_bo(struct resources *r)
{
	struct drm_gem_close args = { .handle = r->bo };

	if (r->bo == 0)
		return true;
	if (ioctl(r->drm, DRM_IOCTL_GEM_CLOSE, &args) != 0)
		return false;
	r->bo = 0;
	return true;
}

static bool
close_syncobj(struct resources *r)
{
	struct drm_syncobj_destroy args = { .handle = r->syncobj };

	if (r->syncobj == 0)
		return true;
	if (ioctl(r->drm, DRM_IOCTL_SYNCOBJ_DESTROY, &args) != 0)
		return false;
	r->syncobj = 0;
	return true;
}

static bool
cleanup(struct resources *r)
{
	bool ok = true;

	if (!close_fd(&r->exported)) ok = false;
	if (!close_fd(&r->sync)) ok = false;
	if (!close_fd(&r->dma)) ok = false;
	if (!close_syncobj(r)) ok = false;
	if (!close_bo(r)) ok = false;
	return ok;
}

/* This native command counts open descriptors without opening a directory. */
static int
fd_count(int drm)
{
	int maximum = fcntl(drm, F_MAXFD), count = 0;

	if (maximum < 0 || maximum > 1048576)
		return -1;
	for (int fd = 0; fd <= maximum; fd++) {
		errno = 0;
		if (fcntl(fd, F_GETFD) >= 0)
			count++;
		else if (errno != EBADF)
			return -1;
	}
	return count;
}

static bool
signaled(int fd)
{
	struct pollfd p = { .fd = fd, .events = POLLIN };

	/* These inputs are already complete. A timeout or any other event fails. */
	return poll(&p, 1, 1000) == 1 && p.revents == POLLIN;
}

static bool
export_ready(struct resources *r, uint32_t flags)
{
	struct dma_buf_export_sync_file args = { .flags = flags, .fd = -1 };
	int descriptor_flags;

	if (r->exported >= 0 ||
	    ioctl(r->dma, DMA_BUF_IOCTL_EXPORT_SYNC_FILE, &args) != 0)
		return false;
	r->exported = args.fd;
	descriptor_flags = fcntl(r->exported, F_GETFD);
	if (r->exported < 0 || descriptor_flags < 0 ||
	    (descriptor_flags & FD_CLOEXEC) != FD_CLOEXEC ||
	    !signaled(r->exported))
		return false;
	return close_fd(&r->exported);
}

static bool
reject_export(struct resources *r, uint32_t flags, int error)
{
	struct dma_buf_export_sync_file args = { .flags = flags, .fd = -1 };
	int result, saved;

	errno = 0;
	result = ioctl(r->dma, DMA_BUF_IOCTL_EXPORT_SYNC_FILE, &args);
	saved = errno;
	/* Retain an unexpected returned descriptor so the failure path closes it. */
	if (args.fd >= 0)
		r->exported = args.fd;
	return result == -1 && saved == error && args.fd == -1;
}

static bool
reject_import(const struct resources *r, uint32_t flags, int fd, int error)
{
	struct dma_buf_import_sync_file args = { .flags = flags, .fd = fd };
	int result;

	errno = 0;
	result = ioctl(r->dma, DMA_BUF_IOCTL_IMPORT_SYNC_FILE, &args);
	return result == -1 && errno == error;
}

static bool
cycle(int drm, bool readonly)
{
	struct resources r = { .drm = drm, .dma = -1, .sync = -1, .exported = -1 };
	struct drm_pvr_ioctl_create_bo_args bo = {
	    .size = 4096, .flags = DRM_PVR_BO_ALLOW_CPU_USERSPACE_ACCESS };
	struct drm_prime_handle prime = { .flags = DRM_CLOEXEC, .fd = -1 };
	struct drm_syncobj_create syncobj = { .flags = DRM_SYNCOBJ_CREATE_SIGNALED };
	struct drm_syncobj_handle exported_sync = {
	    .flags = DRM_SYNCOBJ_HANDLE_TO_FD_FLAGS_EXPORT_SYNC_FILE, .fd = -1 };
	const uint32_t access_flags[] = { DMA_BUF_SYNC_READ, DMA_BUF_SYNC_WRITE, DMA_BUF_SYNC_RW };
	const uint32_t invalid[] = { 0, 4, UINT32_MAX };
	bool ok = false;

	REQUIRE(ioctl(drm, DRM_IOCTL_PVR_CREATE_BO, &bo) == 0);
	r.bo = bo.handle;
	REQUIRE(r.bo != 0);
	prime.handle = r.bo;
	if (!readonly)
		prime.flags |= DRM_RDWR;
	REQUIRE(ioctl(drm, DRM_IOCTL_PRIME_HANDLE_TO_FD, &prime) == 0);
	r.dma = prime.fd;
	REQUIRE(r.dma >= 0);
	REQUIRE((fcntl(r.dma, F_GETFL) & O_ACCMODE) == (readonly ? O_RDONLY : O_RDWR));
	REQUIRE(close_bo(&r));
	REQUIRE(export_ready(&r, readonly ? DMA_BUF_SYNC_READ : DMA_BUF_SYNC_RW));
	REQUIRE(ioctl(drm, DRM_IOCTL_SYNCOBJ_CREATE, &syncobj) == 0);
	r.syncobj = syncobj.handle;
	REQUIRE(r.syncobj != 0);
	exported_sync.handle = r.syncobj;
	REQUIRE(ioctl(drm, DRM_IOCTL_SYNCOBJ_HANDLE_TO_FD, &exported_sync) == 0);
	r.sync = exported_sync.fd;
	REQUIRE(r.sync >= 0);
	REQUIRE(close_syncobj(&r));
	REQUIRE(signaled(r.sync));
	for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
		REQUIRE(reject_export(&r, invalid[i], EINVAL));
		REQUIRE(reject_import(&r, invalid[i], r.sync, EINVAL));
	}
	REQUIRE(reject_import(&r, DMA_BUF_SYNC_READ, -1, EINVAL));
	REQUIRE(reject_import(&r, DMA_BUF_SYNC_READ, r.dma, EINVAL));
	REQUIRE(reject_import(&r, DMA_BUF_SYNC_READ, drm, EINVAL));
	for (size_t i = 0; i < sizeof(access_flags) / sizeof(access_flags[0]); i++) {
		struct dma_buf_import_sync_file args = { .flags = access_flags[i], .fd = r.sync };
		if (readonly && (access_flags[i] & DMA_BUF_SYNC_WRITE) != 0) {
			REQUIRE(reject_export(&r, access_flags[i], EACCES));
			REQUIRE(reject_import(&r, access_flags[i], r.sync, EACCES));
		} else {
			REQUIRE(ioctl(r.dma, DMA_BUF_IOCTL_IMPORT_SYNC_FILE, &args) == 0);
			REQUIRE(export_ready(&r, access_flags[i]));
		}
	}
	ok = true;
out:
	if (!cleanup(&r)) {
		fprintf(stderr, "FAIL: resource cleanup errno=%d\n", errno);
		ok = false;
	}
	return ok;
}

int
main(int argc, char **argv)
{
	int drm = -1, baseline;
	bool ok = false;

	if (argc != 2) {
		fprintf(stderr, "usage: %s RENDER_NODE\n", argv[0]);
		return 2;
	}
	setvbuf(stdout, NULL, _IOLBF, 0);
	alarm(120);
	drm = open(argv[1], O_RDWR | O_CLOEXEC);
	REQUIRE(drm >= 0);
	baseline = fd_count(drm);
	REQUIRE(baseline >= 1);
	for (unsigned i = 0; i < CYCLES; i++) {
		REQUIRE(cycle(drm, false));
		REQUIRE(cycle(drm, true));
		REQUIRE(fd_count(drm) == baseline);
	}
	ok = true;
out:
	if (!close_fd(&drm))
		ok = false;
	if (ok)
		puts("PASS: 128 DMA-BUF sync-file cycles; completed fences, descriptor rights and no descriptor growth");
	return ok ? 0 : 1;
}
