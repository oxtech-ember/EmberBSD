/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD, AI-assisted. Diagnostic BO copy, not scanout proof. */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <drm.h>
#include <drm_mode.h>
#include <drm_fourcc.h>
#include <v3d_drm.h>

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

_Static_assert(sizeof(off_t) >= 8, "64-bit mmap offsets required");

#define SNAP_WIDTH 1920U
#define SNAP_HEIGHT 1080U
#define SNAP_PITCH (SNAP_WIDTH * 4U)
#define SNAP_BYTES ((size_t)SNAP_PITCH * SNAP_HEIGHT)
#define SNAP_RGB_BYTES ((size_t)SNAP_WIDTH * SNAP_HEIGHT * 3U)

struct snapshot_plane {
	struct drm_mode_get_plane plane;
	uint32_t format;
};

static int
snapshot_plane(int fd, uint32_t id, struct snapshot_plane *p)
{

	memset(p, 0, sizeof(*p));
	p->plane.plane_id = id;
	p->plane.count_format_types = 1;
	p->plane.format_type_ptr = (uintptr_t)&p->format;
	if (ioctl(fd, DRM_IOCTL_MODE_GETPLANE, &p->plane) == -1)
		return errno;
	if (p->plane.plane_id != id || p->plane.fb_id == 0 ||
	    p->plane.crtc_id == 0)
		return ENOENT;
	if (p->plane.count_format_types != 1 || p->format != DRM_FORMAT_XRGB8888)
		return EINVAL;
	return 0;
}

static bool
snapshot_same_plane(const struct snapshot_plane *a,
    const struct snapshot_plane *b)
{

	return a->plane.plane_id == b->plane.plane_id &&
	    a->plane.fb_id == b->plane.fb_id &&
	    a->plane.crtc_id == b->plane.crtc_id && a->format == b->format;
}

static int
snapshot_sequence(int fd, uint32_t crtc, struct drm_crtc_get_sequence *seq)
{

	memset(seq, 0, sizeof(*seq));
	seq->crtc_id = crtc;
	if (ioctl(fd, DRM_IOCTL_CRTC_GET_SEQUENCE, seq) == -1)
		return errno;
	return seq->crtc_id == crtc && seq->active == 1 ? 0 : ENOENT;
}

static int
snapshot_write(int fd, const void *memory, size_t length)
{
	const unsigned char *p = memory;
	ssize_t n;

	while (length != 0) {
		n = write(fd, p, length);
		if (n == -1) {
			if (errno == EINTR)
				continue;
			return errno;
		}
		if (n == 0)
			return EIO;
		p += n;
		length -= (size_t)n;
	}
	return 0;
}

static int
snapshot(const char *node, uint32_t plane_id, const char *path)
{
	static const char header[] = "P6\n1920 1080\n255\n";
	struct snapshot_plane first, before, after;
	struct drm_crtc_get_sequence seq0, seq1;
	struct drm_mode_fb_cmd fb = {0};
	struct drm_mode_map_dumb mapping = {0};
	struct drm_v3d_wait_bo wait = {0};
	struct drm_version version = {0};
	struct drm_gem_close gem = {0};
	const unsigned char *map = MAP_FAILED;
	unsigned char *rgb = NULL;
	char name[8] = {0};
	const char *stage = "page-size";
	long page;
	size_t length = 0, i;
	uint64_t offset;
	int fd = -1, output = -1, error = 0, e;
	bool created = false;

	/* Qualify the two CPU page sizes covered by the native allocator tests. */
	page = sysconf(_SC_PAGESIZE);
	if (page != 4096 && page != 65536) {
		error = EINVAL;
		goto out;
	}
	length = (SNAP_BYTES + (size_t)page - 1) & ~((size_t)page - 1);
	stage = "allocate-rgb";
	rgb = malloc(SNAP_RGB_BYTES);
	if (rgb == NULL) {
		error = ENOMEM;
		goto out;
	}
	stage = "open-card";
	fd = open(node, O_RDONLY | O_CLOEXEC);
	if (fd == -1) {
		error = errno;
		goto out;
	}
	stage = "driver";
	version.name = name;
	version.name_len = sizeof(name) - 1;
	if (ioctl(fd, DRM_IOCTL_VERSION, &version) == -1) {
		error = errno;
		goto out;
	}
	if (version.name_len != 3 || memcmp(name, "v3d", 3) != 0) {
		error = ENODEV;
		goto out;
	}
	stage = "get-plane";
	if ((error = snapshot_plane(fd, plane_id, &first)) != 0)
		goto out;
	stage = "get-fb";
	fb.fb_id = first.plane.fb_id;
	if (ioctl(fd, DRM_IOCTL_MODE_GETFB, &fb) == -1) {
		error = errno;
		goto out;
	}
	gem.handle = fb.handle; /* Own every successfully returned handle. */
	if (fb.handle == 0) {
		error = EACCES;
		goto out;
	}
	if (fb.fb_id != first.plane.fb_id || fb.width != SNAP_WIDTH ||
	    fb.height != SNAP_HEIGHT || fb.pitch != SNAP_PITCH ||
	    fb.depth != 24 || fb.bpp != 32) {
		error = EINVAL;
		goto out;
	}
	stage = "wait-bo";
	wait.handle = fb.handle;
	wait.timeout_ns = UINT64_C(1000000000);
	if (ioctl(fd, DRM_IOCTL_V3D_WAIT_BO, &wait) == -1) {
		error = errno;
		goto out;
	}
	stage = "map-offset";
	mapping.handle = fb.handle;
	if (ioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mapping) == -1) {
		error = errno;
		goto out;
	}
	offset = mapping.offset;
	if (offset > INT64_MAX - length || (offset & ((uint64_t)page - 1))) {
		error = EOVERFLOW;
		goto out;
	}
	stage = "mmap";
	map = mmap(NULL, length, PROT_READ, MAP_SHARED, fd, (off_t)offset);
	if (map == MAP_FAILED) {
		error = errno;
		goto out;
	}
	stage = "sequence-before";
	if ((error = snapshot_sequence(fd, first.plane.crtc_id, &seq0)) != 0)
		goto out;
	stage = "plane-before";
	if ((error = snapshot_plane(fd, plane_id, &before)) != 0)
		goto out;
	if (!snapshot_same_plane(&first, &before)) {
		error = EAGAIN;
		goto out;
	}
	/* Native XR24 is B,G,R,X in memory. Never interpret X as alpha. */
	for (i = 0; i < (size_t)SNAP_WIDTH * SNAP_HEIGHT; i++) {
		rgb[3 * i] = map[4 * i + 2];
		rgb[3 * i + 1] = map[4 * i + 1];
		rgb[3 * i + 2] = map[4 * i];
	}
	stage = "plane-after";
	if ((error = snapshot_plane(fd, plane_id, &after)) != 0)
		goto out;
	if (!snapshot_same_plane(&first, &after)) {
		error = EAGAIN;
		goto out;
	}
	stage = "sequence-after";
	if ((error = snapshot_sequence(fd, first.plane.crtc_id, &seq1)) != 0)
		goto out;
	if (seq1.sequence < seq0.sequence || seq1.sequence_ns < seq0.sequence_ns) {
		error = EAGAIN;
		goto out;
	}
	printf("BO snapshot: plane=%" PRIu32 " fb=%" PRIu32 " crtc=%" PRIu32
	    " width=%u height=%u pitch=%u depth=%u bpp=%u page=%ld map-bytes=%zu\n",
	    plane_id, fb.fb_id, first.plane.crtc_id, fb.width, fb.height,
	    fb.pitch, fb.depth, fb.bpp, page, length);
	printf("bracket: sequence=%" PRIu64 "..%" PRIu64 " ns=%" PRId64 "..%" PRId64
	    "; LINEAR/offset0 inferred from the pinned native whitelist\n",
	    (uint64_t)seq0.sequence, (uint64_t)seq1.sequence,
	    (int64_t)seq0.sequence_ns, (int64_t)seq1.sequence_ns);
out:
	/* Finish device ownership before creating any output file. */
	if (map != MAP_FAILED && munmap((void *)map, length) == -1 && error == 0) {
		error = errno;
		stage = "munmap";
	}
	if (gem.handle != 0 && ioctl(fd, DRM_IOCTL_GEM_CLOSE, &gem) == -1 &&
	    error == 0) {
		error = errno;
		stage = "gem-close";
	}
	if (fd != -1 && close(fd) == -1 && error == 0) {
		error = errno;
		stage = "close-card";
	}
	if (error == 0) {
		stage = "create-output";
		output = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
		if (output == -1)
			error = errno;
		else {
			created = true;
			stage = "write-header";
			error = snapshot_write(output, header, sizeof(header) - 1);
			if (error == 0) {
				stage = "write-rgb";
				error = snapshot_write(output, rgb, SNAP_RGB_BYTES);
			}
			if (error == 0) {
				stage = "fsync-output";
				if (fsync(output) == -1)
					error = errno;
			}
			if (close(output) == -1 && error == 0) {
				error = errno;
				stage = "close-output";
			}
		}
	}
	free(rgb);
	if (error != 0) {
		if (created && unlink(path) == -1) {
			e = errno;
			fprintf(stderr, "cleanup: unlink errno=%d\n", e);
		}
		fprintf(stderr, "FAIL: stage=%s errno=%d\n", stage, error);
		return 1;
	}
	puts("OK: diagnostic P6 BO copy; not actual-PV-head, immutable-pixel "
	    "or ABA proof");
	return 0;
}

int
main(int argc, char **argv)
{
	unsigned long id;
	char *end;

	if (argc != 4) {
		fprintf(stderr, "usage: %s /dev/dri/cardN plane-id /new/image.ppm\n",
		    argv[0]);
		return 2;
	}
	errno = 0;
	id = strtoul(argv[2], &end, 10);
	if (errno != 0 || end == argv[2] || *end != '\0' || id == 0 ||
	    id > UINT32_MAX)
		return 2;
	return snapshot(argv[1], (uint32_t)id, argv[3]);
}
