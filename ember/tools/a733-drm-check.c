/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD, AI-assisted. Bounded native A733 KMS consumer. */
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
#include <drm/drm.h>
#include <drm/drm_fourcc.h>

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "FAIL:%d %s errno=%d\n", __LINE__, #x, \
			    errno);                                            \
			exit(1);                                               \
		}                                                              \
	} while (0)
#define PTR(x) ((uint64_t)(uintptr_t)(x))
#define NPROP 32
#define NFRAME 4

struct output {
	uint32_t crtc, connector, plane;
	struct drm_mode_modeinfo mode;
	uint32_t crtc_props[2], connector_prop, plane_props[10];
};
struct frame {
	struct drm_mode_create_dumb dumb;
	uint32_t fb;
};

static uint64_t
milliseconds(void)
{
	struct timespec ts;
	CHECK(clock_gettime(CLOCK_MONOTONIC, &ts) == 0 && ts.tv_sec >= 0);
	return (uint64_t)ts.tv_sec * 1000 + (unsigned)ts.tv_nsec / 1000000;
}

static uint32_t
property(
    int fd, uint32_t object, uint32_t type, const char *name, uint64_t *value)
{
	uint32_t ids[NPROP];
	uint64_t values[NPROP];
	struct drm_mode_obj_get_properties p = {.obj_id = object,
	    .obj_type = type,
	    .count_props = NPROP,
	    .props_ptr = PTR(ids),
	    .prop_values_ptr = PTR(values)};
	uint32_t found = 0;

	CHECK(ioctl(fd, DRM_IOCTL_MODE_OBJ_GETPROPERTIES, &p) == 0);
	CHECK(p.count_props <= NPROP);
	for (uint32_t i = 0; i < p.count_props; i++) {
		struct drm_mode_get_property get = {.prop_id = ids[i]};
		CHECK(ioctl(fd, DRM_IOCTL_MODE_GETPROPERTY, &get) == 0);
		CHECK(memchr(get.name, 0, sizeof(get.name)) != NULL);
		if (strcmp(get.name, name) == 0) {
			CHECK(found == 0 && ids[i] != 0);
			found = ids[i];
			if (value != NULL)
				*value = values[i];
		}
	}
	CHECK(found != 0);
	return found;
}

static void
query(int fd, struct output *o)
{
	char name[32] = {0};
	struct drm_version version = {
	    .name = name, .name_len = sizeof(name) - 1};
	struct drm_set_client_cap cap = {
	    .capability = DRM_CLIENT_CAP_ATOMIC, .value = 1};
	struct drm_get_cap dumb = {.capability = DRM_CAP_DUMB_BUFFER};
	uint32_t encoder = 0, format = 0;
	struct drm_mode_card_res res = {.count_crtcs = 1,
	    .count_connectors = 1,
	    .count_encoders = 1,
	    .crtc_id_ptr = PTR(&o->crtc),
	    .connector_id_ptr = PTR(&o->connector),
	    .encoder_id_ptr = PTR(&encoder)};
	struct drm_mode_get_connector connector = {0};
	struct drm_mode_get_encoder enc = {0};
	struct drm_mode_get_plane_res planes = {
	    .count_planes = 1, .plane_id_ptr = PTR(&o->plane)};
	struct drm_mode_get_plane plane = {0};
	uint64_t plane_type = 0;
	static const char *const props[10] = {"FB_ID", "CRTC_ID", "SRC_X",
	    "SRC_Y", "SRC_W", "SRC_H", "CRTC_X", "CRTC_Y", "CRTC_W", "CRTC_H"};

	CHECK(ioctl(fd, DRM_IOCTL_VERSION, &version) == 0);
	CHECK(version.name_len == strlen("sun60idrm") &&
	      strcmp(name, "sun60idrm") == 0);
	CHECK(ioctl(fd, DRM_IOCTL_GET_CAP, &dumb) == 0 && dumb.value == 1);
	CHECK(ioctl(fd, DRM_IOCTL_SET_CLIENT_CAP, &cap) == 0);
	CHECK(ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) == 0);
	CHECK(res.count_crtcs == 1 && res.count_connectors == 1 &&
	      res.count_encoders == 1 && o->crtc && o->connector && encoder);
	connector.connector_id = o->connector;
	connector.count_modes = 1;
	connector.modes_ptr = PTR(&o->mode);
	connector.count_encoders = 1;
	connector.encoders_ptr = PTR(&encoder);
	CHECK(ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &connector) == 0);
	/* Native DRM connector_status_connected is 1. */
	CHECK(connector.connection == 1 && connector.count_modes == 1 &&
	      connector.count_encoders == 1 &&
	      connector.connector_type == DRM_MODE_CONNECTOR_HDMIA);
	CHECK(o->mode.clock && o->mode.hdisplay && o->mode.hdisplay <= 1920 &&
	      o->mode.vdisplay && o->mode.vdisplay <= 1080 &&
	      (o->mode.flags &
		  (DRM_MODE_FLAG_INTERLACE | DRM_MODE_FLAG_DBLSCAN)) == 0);
	enc.encoder_id = encoder;
	CHECK(ioctl(fd, DRM_IOCTL_MODE_GETENCODER, &enc) == 0 &&
	      enc.encoder_type == DRM_MODE_ENCODER_TMDS &&
	      enc.possible_crtcs == 1);
	CHECK(ioctl(fd, DRM_IOCTL_MODE_GETPLANERESOURCES, &planes) == 0 &&
	      planes.count_planes == 1 && o->plane != 0);
	plane.plane_id = o->plane;
	plane.count_format_types = 1;
	plane.format_type_ptr = PTR(&format);
	CHECK(ioctl(fd, DRM_IOCTL_MODE_GETPLANE, &plane) == 0 &&
	      plane.possible_crtcs == 1 && plane.count_format_types == 1 &&
	      format == DRM_FORMAT_XRGB8888);
	(void)property(
	    fd, o->plane, DRM_MODE_OBJECT_PLANE, "type", &plane_type);
	/* Native DRM_PLANE_TYPE_PRIMARY is 1. */
	CHECK(plane_type == 1);
	o->crtc_props[0] =
	    property(fd, o->crtc, DRM_MODE_OBJECT_CRTC, "MODE_ID", NULL);
	o->crtc_props[1] =
	    property(fd, o->crtc, DRM_MODE_OBJECT_CRTC, "ACTIVE", NULL);
	o->connector_prop = property(
	    fd, o->connector, DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID", NULL);
	for (unsigned i = 0; i < 10; i++)
		o->plane_props[i] = property(
		    fd, o->plane, DRM_MODE_OBJECT_PLANE, props[i], NULL);
	printf("QUERY sun60idrm connector=%u crtc=%u plane=%u mode=%ux%u "
	       "clock=%u kHz\n",
	    o->connector, o->crtc, o->plane, o->mode.hdisplay, o->mode.vdisplay,
	    o->mode.clock);
}

static void
create_frame(int fd, const struct output *o, struct frame *f, uint32_t color)
{
	struct drm_mode_map_dumb map;
	struct drm_mode_fb_cmd2 fb = {.width = o->mode.hdisplay,
	    .height = o->mode.vdisplay,
	    .pixel_format = DRM_FORMAT_XRGB8888};
	uint8_t *pixels;
	uint64_t needed;

	f->dumb = (struct drm_mode_create_dumb){
	    .width = o->mode.hdisplay, .height = o->mode.vdisplay, .bpp = 32};
	CHECK(ioctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &f->dumb) == 0);
	needed = (uint64_t)f->dumb.pitch * o->mode.vdisplay;
	CHECK(f->dumb.handle != 0 && f->dumb.pitch % 64 == 0 &&
	      f->dumb.pitch >= (uint32_t)o->mode.hdisplay * 4 &&
	      f->dumb.size >= needed && f->dumb.size <= 16 * 1024 * 1024 &&
	      f->dumb.size <= SIZE_MAX);
	map = (struct drm_mode_map_dumb){.handle = f->dumb.handle};
	CHECK(ioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map) == 0 &&
	      map.offset <= INT64_MAX);
	pixels = mmap(NULL, (size_t)f->dumb.size, PROT_READ | PROT_WRITE,
	    MAP_SHARED, fd, (off_t)map.offset);
	CHECK(pixels != MAP_FAILED);
	memset(pixels, 0, (size_t)f->dumb.size);
	for (unsigned y = 0; y < o->mode.vdisplay; y++) {
		uint32_t *row =
		    (uint32_t *)(void *)(pixels + (size_t)y * f->dumb.pitch);
		for (unsigned x = 0; x < o->mode.hdisplay; x++)
			row[x] = color;
	}
	/* DE owns PREWRITE after its producer wait. Never edit this map again.
	 */
	CHECK(munmap(pixels, (size_t)f->dumb.size) == 0);
	fb.handles[0] = f->dumb.handle;
	fb.pitches[0] = f->dumb.pitch;
	CHECK(ioctl(fd, DRM_IOCTL_MODE_ADDFB2, &fb) == 0 && fb.fb_id != 0);
	f->fb = fb.fb_id;
}

static void
commit(int fd, const struct output *o, uint32_t fb, uint32_t blob,
    uint32_t flags, uint64_t token)
{
	uint32_t objects[3] = {o->crtc, o->connector, o->plane};
	uint32_t counts[3] = {2, 1, 10}, props[13];
	uint64_t values[13] = {blob, 1, o->crtc, fb, o->crtc, 0, 0,
	    (uint64_t)o->mode.hdisplay << 16, (uint64_t)o->mode.vdisplay << 16,
	    0, 0, o->mode.hdisplay, o->mode.vdisplay};
	struct drm_mode_atomic atomic = {.flags = flags,
	    .count_objs = 3,
	    .objs_ptr = PTR(objects),
	    .count_props_ptr = PTR(counts),
	    .props_ptr = PTR(props),
	    .prop_values_ptr = PTR(values),
	    .user_data = token};

	memcpy(props, o->crtc_props, sizeof(o->crtc_props));
	props[2] = o->connector_prop;
	memcpy(props + 3, o->plane_props, sizeof(o->plane_props));
	uint64_t deadline = milliseconds() + 5000;

	for (;;) {
		if (ioctl(fd, DRM_IOCTL_MODE_ATOMIC, &atomic) == 0)
			break;
		/* A prior event can precede the final worker cleanup. */
		CHECK(errno == EBUSY && milliseconds() < deadline);
		CHECK(poll(NULL, 0, 1) == 0);
	}
}

static uint64_t
wait_flip(int fd, uint32_t crtc, uint64_t token, uint64_t previous)
{
	struct pollfd pfd = {.fd = fd, .events = POLLIN};
	struct drm_event_vblank event;
	uint64_t deadline = milliseconds() + 5000, stamp;

	for (;;) {
		uint64_t now = milliseconds();
		CHECK(now < deadline);
		int ready = poll(&pfd, 1, (int)(deadline - now));
		if (ready < 0 && errno == EINTR)
			continue;
		CHECK(ready == 1 && pfd.revents == POLLIN);
		break;
	}
	CHECK(read(fd, &event, sizeof(event)) == (ssize_t)sizeof(event));
	CHECK(event.base.type == DRM_EVENT_FLIP_COMPLETE &&
	      event.base.length == sizeof(event) && event.user_data == token &&
	      event.crtc_id == crtc && event.sequence == 0 &&
	      event.tv_usec < 1000000);
	stamp = (uint64_t)event.tv_sec * 1000000 + event.tv_usec;
	CHECK(stamp > previous);
	printf("FLIP token=%" PRIu64 " timestamp_us=%" PRIu64 " sequence=0\n",
	    token, stamp);
	return stamp;
}

int
main(int argc, char **argv)
{
	struct output output = {0};
	struct frame frames[NFRAME] = {0};
	static const uint32_t colors[NFRAME] = {
	    0x00ff0000, 0x0000ff00, 0x000000ff, 0x00ffffff};
	struct drm_mode_create_blob blob;
	uint64_t stamp = 0;
	bool run;
	int fd;

	if (argc != 3 || (strcmp(argv[1], "--query") != 0 &&
			     strcmp(argv[1], "--commit") != 0)) {
		fprintf(stderr, "usage: %s --query|--commit /dev/dri/cardN\n",
		    argv[0]);
		return 2;
	}
	run = strcmp(argv[1], "--commit") == 0;
	setvbuf(stdout, NULL, _IOLBF, 0);
	alarm(30);
	fd = open(argv[2], O_RDWR | O_CLOEXEC);
	CHECK(fd >= 0);
	query(fd, &output);
	if (!run) {
		CHECK(close(fd) == 0);
		return 0;
	}
	CHECK(ioctl(fd, DRM_IOCTL_SET_MASTER, (void *)NULL) == 0);
	blob = (struct drm_mode_create_blob){
	    .data = PTR(&output.mode), .length = sizeof(output.mode)};
	CHECK(ioctl(fd, DRM_IOCTL_MODE_CREATEPROPBLOB, &blob) == 0 &&
	      blob.blob_id != 0);
	for (unsigned i = 0; i < NFRAME; i++) {
		create_frame(fd, &output, &frames[i], colors[i]);
		commit(fd, &output, frames[i].fb, blob.blob_id,
		    DRM_MODE_ATOMIC_TEST_ONLY | DRM_MODE_ATOMIC_ALLOW_MODESET,
		    0);
		commit(fd, &output, frames[i].fb, blob.blob_id,
		    DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_ATOMIC_ALLOW_MODESET |
			DRM_MODE_PAGE_FLIP_EVENT,
		    i + 1);
		stamp = wait_flip(fd, output.crtc, i + 1, stamp);
	}
	/* Do not request disable or remove an active framebuffer. */
	CHECK(close(fd) == 0);
	puts("PASS: four atomic page flips; final native scanout remains "
	     "retained");
	return 0;
}
