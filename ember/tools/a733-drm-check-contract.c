/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD, AI-assisted. Actual consumer with native UAPI and fake I/O.
 */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <poll.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <setjmp.h>
#include <stdarg.h>
/* The host uses native UAPI layouts without claiming native ioctl execution. */
#if !defined(__NetBSD__) && !defined(__linux__)
#define __user
typedef int8_t __s8;
typedef uint8_t __u8;
typedef int16_t __s16;
typedef uint16_t __u16;
typedef int32_t __s32;
typedef uint32_t __u32;
typedef int64_t __s64;
typedef uint64_t __u64;
typedef size_t __kernel_size_t;
typedef unsigned long drm_handle_t;
#endif
#include <drm/drm.h>
#include <drm/drm_fourcc.h>

static unsigned checks, fault, calls, created_frames, flips, tests, masters, closed;
static unsigned retries, connector_calls;
static bool modes_populated;
static uint64_t tick, pending;
static void *allocation;
static size_t allocation_size;
static jmp_buf failed;
#define VERIFY(x)                                                              \
	do {                                                                   \
		checks++;                                                      \
		if (!(x)) {                                                    \
			fprintf(stderr, "fixture:%d %s\n", __LINE__, #x);      \
			abort();                                               \
		}                                                              \
	} while (0)
static int fake_open(const char *, int, ...);
static int fake_close(int);
static unsigned fake_alarm(unsigned);
static int fake_clock_gettime(clockid_t, struct timespec *);
static int fake_poll(struct pollfd *, nfds_t, int);
static ssize_t fake_read(int, void *, size_t);
static int fake_ioctl(int, unsigned long, ...);
static void *fake_mmap(const void *, size_t, int, int, int, off_t);
static int fake_munmap(void *, size_t);
static _Noreturn void fake_exit(int);
#define open fake_open
#define close fake_close
#define alarm fake_alarm
#define clock_gettime fake_clock_gettime
#define poll fake_poll
#define read fake_read
#define ioctl fake_ioctl
#define mmap fake_mmap
#define munmap fake_munmap
#define exit fake_exit
#define main consumer_main
#include "a733-drm-check.c"
#undef main
#undef open
#undef close
#undef alarm
#undef clock_gettime
#undef poll
#undef read
#undef ioctl
#undef mmap
#undef munmap
#undef exit

static int
fake_open(const char *path, int flags, ...)
{
	VERIFY(strcmp(path, "/dev/dri/card7") == 0);
	VERIFY(flags == (O_RDWR | O_CLOEXEC));
	return 3;
}
static int
fake_close(int fd)
{
	VERIFY(fd == 3);
	closed++;
	return 0;
}
static unsigned
fake_alarm(unsigned seconds)
{
	VERIFY(seconds == 30);
	return 0;
}
static _Noreturn void
fake_exit(int status)
{
	VERIFY(status == 1);
	longjmp(failed, 1);
}
static int
fake_clock_gettime(clockid_t id, struct timespec *ts)
{
	VERIFY(id == CLOCK_MONOTONIC);
	tick++;
	*ts = (struct timespec){
	    .tv_sec = tick / 1000, .tv_nsec = (long)(tick % 1000) * 1000000};
	return 0;
}
static int
fake_poll(struct pollfd *p, nfds_t n, int timeout)
{
	VERIFY(timeout > 0 && timeout <= 5000);
	if (!p) {
		VERIFY(n == 0);
		return 0;
	}
	VERIFY(n == 1 && p->fd == 3 && p->events == POLLIN);
	if (fault == 26 && retries++ == 0) {
		errno = EINTR;
		return -1;
	}
	if (fault == 12)
		return 0;
	if (fault == 13) {
		p->revents = POLLERR;
		return 1;
	}
	p->revents = POLLIN;
	return 1;
}
static ssize_t
fake_read(int fd, void *buf, size_t size)
{
	struct drm_event_vblank e = {
	    .base = {DRM_EVENT_FLIP_COMPLETE, sizeof(e)},
	    .user_data = pending,
	    .tv_sec = 10 + flips,
	    .tv_usec = 123,
	    .sequence = 0,
	    .crtc_id = 10};
	VERIFY(fd == 3 && size == sizeof(e) && pending > 0);
	if (fault == 14)
		e.user_data++;
	if (fault == 15)
		e.sequence = 1;
	if (fault == 16)
		e.crtc_id = 11;
	if (fault == 17)
		e.base.length--;
	if (fault == 18)
		e.tv_usec = 1000000;
	if (fault == 19)
		e.tv_sec = 0, e.tv_usec = 0;
	if (fault == 20)
		e.base.type = DRM_EVENT_VBLANK;
	memcpy(buf, &e, sizeof(e));
	return fault == 21 ? (ssize_t)sizeof(e) - 1 : (ssize_t)sizeof(e);
}
static void *
fake_mmap(const void *addr, size_t length, int prot, int flags, int fd, off_t off)
{
	VERIFY(addr == NULL && length == 640 * 480 * 4 &&
	       prot == (PROT_READ | PROT_WRITE));
	VERIFY(flags == MAP_SHARED && fd == 3 && off == (off_t)(4096 * created_frames));
	VERIFY(allocation == NULL);
	allocation_size = length;
	allocation = calloc(1, length);
	VERIFY(allocation != NULL);
	return allocation;
}
static int
fake_munmap(void *addr, size_t length)
{
	static const uint32_t colors[4] = {
	    0xff0000, 0x00ff00, 0x0000ff, 0xffffff};
	VERIFY(addr == allocation && length == allocation_size && created_frames >= 1 &&
	       created_frames <= 4);
	for (size_t i = 0; i < length / 4; i++)
		VERIFY(((uint32_t *)addr)[i] == colors[created_frames - 1]);
	free(allocation);
	allocation = NULL;
	return 0;
}
static const char *const plane_names[] = {"type", "FB_ID", "CRTC_ID", "SRC_X",
    "SRC_Y", "SRC_W", "SRC_H", "CRTC_X", "CRTC_Y", "CRTC_W", "CRTC_H"};
static int
fake_ioctl(int fd, unsigned long op, ...)
{
	void *arg;
	va_list ap;
	VERIFY(fd == 3);
	calls++;
	va_start(ap, op);
	arg = va_arg(ap, void *);
	va_end(ap);
	if (op == DRM_IOCTL_VERSION) {
		struct drm_version *v = arg;
		const char *name = fault == 1 ? "powervr" : "sun60idrm";
		VERIFY(v->name_len >= strlen(name));
		strcpy(v->name, name);
		v->name_len = strlen(name);
	} else if (op == DRM_IOCTL_GET_CAP) {
		struct drm_get_cap *c = arg;
		VERIFY(c->capability == DRM_CAP_DUMB_BUFFER);
		c->value = fault == 2 ? 0 : 1;
	} else if (op == DRM_IOCTL_SET_CLIENT_CAP) {
		const struct drm_set_client_cap *c = arg;
		VERIFY(c->capability == DRM_CLIENT_CAP_ATOMIC && c->value == 1);
	} else if (op == DRM_IOCTL_MODE_GETRESOURCES) {
		struct drm_mode_card_res *r = arg;
		VERIFY(r->count_crtcs == 1 && r->count_connectors == 1 &&
		       r->count_encoders == 1);
		*(uint32_t *)(uintptr_t)r->crtc_id_ptr = 10;
		*(uint32_t *)(uintptr_t)r->connector_id_ptr = 20;
		*(uint32_t *)(uintptr_t)r->encoder_id_ptr = 40;
		if (fault == 3)
			r->count_crtcs = 2;
	} else if (op == DRM_IOCTL_MODE_GETCONNECTOR) {
		struct drm_mode_get_connector *c = arg;
		uint32_t mode_count, encoder_count = 1;
		bool probe = c->count_modes == 0;

		VERIFY(c->connector_id == 20 && ++connector_calls <= 2);
		if (fault == 37 || (fault == 38 && !probe)) {
			errno = EIO;
			return -1;
		}
		/* Match drm_mode_getconnector: only a zero count probes modes. */
		if (probe)
			modes_populated = true;
		mode_count = modes_populated ? 1 : 0;
		if ((fault == 30 && probe) || (fault == 33 && !probe))
			mode_count = 0;
		if ((fault == 31 && probe) || (fault == 34 && !probe))
			mode_count = 2;
		if ((fault == 32 && probe) || (fault == 35 && !probe))
			encoder_count = 2;
		if (c->count_modes >= mode_count && mode_count != 0) {
			VERIFY(mode_count == 1 && c->modes_ptr != 0);
			*(struct drm_mode_modeinfo *)(uintptr_t)c->modes_ptr =
			    (struct drm_mode_modeinfo){
				.clock = 25175, .hdisplay = 640, .vdisplay = 480};
		}
		if (c->count_encoders >= encoder_count) {
			VERIFY(encoder_count == 1 && c->encoders_ptr != 0);
			*(uint32_t *)(uintptr_t)c->encoders_ptr = 40;
		}
		c->count_modes = mode_count;
		c->count_encoders = encoder_count;
		c->connection = fault == 4 || (fault == 36 && !probe) ? 2 :
		    (modes_populated ? 1 : 3);
		c->connector_type = DRM_MODE_CONNECTOR_HDMIA;
		/* Discovery returns counts without copying properties. */
		if (c->count_props != 0 &&
		    (c->props_ptr == 0 || c->prop_values_ptr == 0)) {
			errno = EFAULT;
			return -1;
		}
		c->count_props = 1;
	} else if (op == DRM_IOCTL_MODE_GETENCODER) {
		struct drm_mode_get_encoder *e = arg;
		VERIFY(e->encoder_id == 40);
		e->encoder_type = DRM_MODE_ENCODER_TMDS;
		e->possible_crtcs = 1;
	} else if (op == DRM_IOCTL_MODE_GETPLANERESOURCES) {
		const struct drm_mode_get_plane_res *r = arg;
		VERIFY(r->count_planes == 1);
		*(uint32_t *)(uintptr_t)r->plane_id_ptr = 30;
	} else if (op == DRM_IOCTL_MODE_GETPLANE) {
		struct drm_mode_get_plane *p = arg;
		VERIFY(p->plane_id == 30 && p->count_format_types == 1);
		*(uint32_t *)(uintptr_t)p->format_type_ptr =
		    fault == 5 ? DRM_FORMAT_ARGB8888 : DRM_FORMAT_XRGB8888;
		p->possible_crtcs = 1;
	} else if (op == DRM_IOCTL_MODE_OBJ_GETPROPERTIES) {
		struct drm_mode_obj_get_properties *p = arg;
		uint32_t *ids = (void *)(uintptr_t)p->props_ptr;
		uint64_t *vals = (void *)(uintptr_t)p->prop_values_ptr;
		VERIFY(p->count_props == 32);
		if (p->obj_id == 10) {
			VERIFY(p->obj_type == DRM_MODE_OBJECT_CRTC);
			p->count_props = 2;
			ids[0] = 100;
			ids[1] = 101;
			vals[0] = vals[1] = 0;
		} else if (p->obj_id == 20) {
			VERIFY(p->obj_type == DRM_MODE_OBJECT_CONNECTOR);
			p->count_props = 1;
			ids[0] = 200;
			vals[0] = 0;
		} else {
			VERIFY(p->obj_id == 30 &&
			       p->obj_type == DRM_MODE_OBJECT_PLANE);
			p->count_props = 11;
			for (unsigned i = 0; i < 11; i++) {
				ids[i] = 300 + i;
				vals[i] = i == 0 ? (fault == 6 ? 0 : 1) : 0;
			}
		}
		if (fault == 7)
			p->count_props = 33;
	} else if (op == DRM_IOCTL_MODE_GETPROPERTY) {
		struct drm_mode_get_property *p = arg;
		const char *name;
		if (p->prop_id == 100)
			name = "MODE_ID";
		else if (p->prop_id == 101)
			name = "ACTIVE";
		else if (p->prop_id == 200)
			name = "CRTC_ID";
		else {
			VERIFY(p->prop_id >= 300 && p->prop_id < 311);
			name = plane_names[p->prop_id - 300];
		}
		if (fault == 27 && p->prop_id == 101)
			name = "ABSENT";
		if (fault == 8)
			memset(p->name, 'x', sizeof(p->name));
		else
			strcpy(p->name, name);
	} else if (op == DRM_IOCTL_SET_MASTER) {
		VERIFY(arg == NULL);
		masters++;
	} else if (op == DRM_IOCTL_MODE_CREATEPROPBLOB) {
		struct drm_mode_create_blob *b = arg;
		VERIFY(b->length == sizeof(struct drm_mode_modeinfo));
		b->blob_id = 50;
	} else if (op == DRM_IOCTL_MODE_CREATE_DUMB) {
		struct drm_mode_create_dumb *d = arg;
		VERIFY(d->width == 640 && d->height == 480 && d->bpp == 32 &&
		       !d->flags);
		created_frames++;
		d->handle = 1000 + created_frames;
		d->pitch = fault == 9 ? 1 : 2560;
		d->size = fault == 10 ? 1 : 2560 * 480;
	} else if (op == DRM_IOCTL_MODE_MAP_DUMB) {
		struct drm_mode_map_dumb *m = arg;
		VERIFY(m->handle == 1000 + created_frames);
		m->offset = UINT64_C(4096) * created_frames;
	} else if (op == DRM_IOCTL_MODE_ADDFB2) {
		struct drm_mode_fb_cmd2 *f = arg;
		VERIFY(f->handles[0] == 1000 + created_frames &&
		       f->pitches[0] == 2560 && f->width == 640 &&
		       f->height == 480 &&
		       f->pixel_format == DRM_FORMAT_XRGB8888 &&
		       !f->offsets[0] && !f->flags);
		f->fb_id = 2000 + created_frames;
	} else if (op == DRM_IOCTL_MODE_ATOMIC) {
		const struct drm_mode_atomic *a = arg;
		const uint32_t *objs = (void *)(uintptr_t)a->objs_ptr,
			 *count = (void *)(uintptr_t)a->count_props_ptr,
			 *props = (void *)(uintptr_t)a->props_ptr;
		const uint64_t *v = (void *)(uintptr_t)a->prop_values_ptr;
		static const uint32_t wantprops[] = {100, 101, 200, 301, 302,
		    303, 304, 305, 306, 307, 308, 309, 310};
		VERIFY(a->count_objs == 3 && objs[0] == 10 && objs[1] == 20 &&
		       objs[2] == 30 && count[0] == 2 && count[1] == 1 &&
		       count[2] == 10);
		VERIFY(memcmp(props, wantprops, sizeof(wantprops)) == 0);
		VERIFY(v[0] == 50 && v[1] == 1 && v[2] == 10 &&
		       v[3] == 2000 + created_frames && v[4] == 10 && v[5] == 0 &&
		       v[6] == 0 && v[7] == (640U << 16) &&
		       v[8] == (480U << 16) && v[9] == 0 && v[10] == 0 &&
		       v[11] == 640 && v[12] == 480);
		if (fault == 11) {
			errno = EIO;
			return -1;
		}
		if (fault == 24 || (fault == 25 && flips == 1 &&
				       (a->flags & DRM_MODE_ATOMIC_NONBLOCK) &&
				       retries++ == 0)) {
			errno = EBUSY;
			return -1;
		}
		if (fault == 22 && retries++ == 0) {
			errno = EBUSY;
			return -1;
		}
		if (a->flags & DRM_MODE_ATOMIC_TEST_ONLY) {
			VERIFY(a->flags == (DRM_MODE_ATOMIC_TEST_ONLY |
					       DRM_MODE_ATOMIC_ALLOW_MODESET) &&
			       a->user_data == 0);
			tests++;
		} else {
			VERIFY(a->flags == (DRM_MODE_ATOMIC_NONBLOCK |
					       DRM_MODE_ATOMIC_ALLOW_MODESET |
					       DRM_MODE_PAGE_FLIP_EVENT) &&
			       a->user_data == created_frames);
			flips++;
			pending = a->user_data;
		}
	} else
		VERIFY(false);
	return 0;
}
int
main(int argc, char **argv)
{
	volatile unsigned first = 0, last = 38;
	char *query_args[] = {"check", "--query", "/dev/dri/card7", NULL};
	char *commit_args[] = {"check", "--commit", "/dev/dri/card7", NULL};
	VERIFY(argc == 1 || argc == 2);
	if (argc == 2) {
		char *end;
		unsigned long selected = strtoul(argv[1], &end, 10);
		VERIFY(argv[1][0] != '\0' && *end == '\0' && selected <= last);
		first = last = (unsigned)selected;
	}
	for (volatile unsigned scenario = first; scenario <= last; scenario++) {
		errno = 0;
		fault = scenario == 23 || scenario == 28 || scenario == 29 ?
		    0 : scenario;
		modes_populated = scenario == 28 || scenario == 29;
		connector_calls = 0;
		calls = created_frames = flips = tests = masters = closed = retries = 0;
		tick = pending = 0;
		int failed_check = setjmp(failed);
		if (!failed_check)
			VERIFY(consumer_main(3,
				   scenario == 23 || scenario == 29 ?
				       query_args : commit_args) ==
			       0);
		if (scenario == 0 || scenario == 22 || scenario == 25 ||
		    scenario == 26 || scenario == 28) {
			VERIFY(!failed_check && created_frames == 4 && flips == 4 &&
			       tests == 4 && masters == 1 && closed == 1);
		} else if (scenario == 23 || scenario == 29) {
			VERIFY(!failed_check && created_frames == 0 && flips == 0 &&
			       tests == 0 && masters == 0 && closed == 1);
		} else {
			VERIFY(failed_check);
			VERIFY(closed == 0);
			if (scenario >= 30)
				VERIFY(created_frames == 0 && masters == 0);
		}
		free(allocation);
		allocation = NULL;
	}
	printf(
	    "PASS %u native-UAPI consumer checks, %u success/fault scenarios\n",
	    checks, last - first + 1);
	return 0;
}
