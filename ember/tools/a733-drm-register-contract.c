/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD, AI-assisted. Actual registration cleanup controls. */
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x);abort();} } while(0)
#define __arraycount(a) (sizeof(a)/sizeof((a)[0]))
#define IS_ERR(p) ((uintptr_t)(p)>=(uintptr_t)-4095)
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define DRM_FORMAT_XRGB8888 1
#define DRM_FORMAT_MOD_LINEAR 0
#define DRM_FORMAT_MOD_INVALID UINT64_MAX
#define DRM_NATIVE_PLATFORM_VERSION 1
#define DRM_NATIVE_BUS_PLATFORM 2
#define DRM_PLANE_TYPE_PRIMARY 1
#define DRM_MODE_ENCODER_TMDS 2
#define DRM_MODE_CONNECTOR_HDMIA 3
#define DRM_CONNECTOR_POLL_CONNECT 1
#define DRM_CONNECTOR_POLL_DISCONNECT 2
#define PRI_NONE 0
#define IPL_NONE 0
#define WQ_MPSAFE 1
struct sun60i_a733_output_mode {
	uint32_t pixel_clock_hz, hdisplay, vdisplay;
};
struct sun60i_a733_de352_state {
	void *dmat;
	uint64_t max_dma_address;
	struct sun60i_a733_output_mode mode;
};
struct sun60i_a733_tcon_timing {
	unsigned unused;
};
struct drm_device {
	void *dev_private, *bus_dmat, *bus_dmat32, *dmat, *cma_pool;
	bool dmat_subregion_p, irq_enabled;
	uint32_t max_vblank_count;
	unsigned num_crtcs;
	struct {
		unsigned version, length, bus_type;
		char fullname[32], compatible[64];
	}      native_platform;
	struct {
		const void *funcs;
		unsigned min_width, max_width, min_height, max_height;
	}      mode_config;
};
struct plane {
	void *state;
	const void *helper;
};
struct crtc {
	void *state;
};
struct encoder {
	unsigned possible_crtcs;
};
struct connector {
	unsigned polled;
	void *state;
	const void *helper;
};
struct a733_drm {
	struct sun60i_a733_de352_state native;
	int phandle;
	void *client, *queue, *device;
	bool registration_started, vblank_irq;
	void *pool;
	struct drm_device *dev;
	struct plane plane;
	struct crtc crtc;
	struct encoder encoder;
	struct connector connector;
};
static unsigned vblank_storage, vblank_bound, vblank_unbinds;
static int vblank_bind_error;
typedef void (*sun60i_a733_vblank_fn)(void *, uint64_t);
static void a733_vblank(void *arg, uint64_t sequence){
	(void)arg;
	(void)sequence;
}
static unsigned calls, failed_step, acquired, queues, devices, modes, registered,
         releases, put_calls, cleanups;
static int release_error, state_error;
static unsigned invalid_state, reset_failure;
static struct a733_drm *current;
static const unsigned a733_driver = 1, a733_mode_funcs = 2, a733_plane_funcs = 3,
         a733_plane_helpers = 4, a733_crtc_funcs = 5, a733_encoder_funcs = 6,
         a733_connector_funcs = 7, a733_connector_helpers = 8;
static int
operation(void)
{
	calls++;
	return calls == failed_step ? ENOMEM : 0;
}
static int sun60i_a733_de352_get_state(int p, struct sun60i_a733_de352_state *state){
	CHECK(p == 7);
	int e = operation();
	if (e || state_error)
		return e ? e : state_error;
	*state = (struct sun60i_a733_de352_state){
		current, UINT32_MAX, {
			27000000, 640, 480
		}
	};
	if (invalid_state == 1)
		state->dmat = NULL;
	if (invalid_state == 2)
		state->max_dma_address = UINT64_MAX;
	if (invalid_state == 3)
		state->mode.pixel_clock_hz++;
	return 0;
}
static int sun60i_a733_tcon_timing_encode(const struct sun60i_a733_output_mode *m, struct sun60i_a733_tcon_timing *t){
	CHECK(m->hdisplay == 640 && t);
	return invalid_state == 4 ? EINVAL : 0;
}
static int sun60i_a733_de352_acquire(int p, void *cookie, void **client){
	CHECK(p == 7 && cookie == current && !acquired);
	int e = operation();
	if (!e) {
		*client = current;
		acquired++;
	} return e;
}
static int sun60i_a733_de352_release(void *client){
	CHECK(client == current && acquired == 1 && !queues && !devices && !modes && !registered);
	releases++;
	if (!release_error)
		acquired--;
	return release_error;
}
int drm_guarantee_initialized(void){
	CHECK(acquired == 1);
	return operation();
}
static void
a733_drm_work(void *w, void *c)
{
	(void)w;
	(void)c;
}
static int workqueue_create(void **q, const char *name, void (*fn) (void *, void *), void *cookie, int pri, int ipl, int flags){
	CHECK(acquired == 1 && !queues && !strcmp(name, "a733kms") && fn == a733_drm_work && cookie == current && pri == PRI_NONE && ipl == IPL_NONE && flags == WQ_MPSAFE);
	int e = operation();
	if (!e) {
		*q = current;
		queues++;
	} return e;
}
static void workqueue_destroy(void *q){
	CHECK(q == current && queues == 1 && !devices && !modes);
	queues--;
}
static struct drm_device *drm_dev_alloc(const void *driver, void *device){
	CHECK(driver == &a733_driver && device == current && queues == 1);
	int e = operation();
	if (e)
		return ERR_PTR(-e);
	struct drm_device *d = calloc(1, sizeof(*d));
	CHECK(d);
	devices++;
	return d;
}
static void drm_dev_put(struct drm_device *dev){
	CHECK(dev == current->dev && devices == 1 && !modes && !registered);
	/* drm_dev_fini frees the vblank storage after the callback removal. */
	CHECK(!vblank_bound);
	vblank_storage = 0;
	devices--;
	put_calls++;
	free(dev);
}
static const char *device_xname(void *d){
	CHECK(d == current);
	return "sun60idrm0";
}
#define strlcpy fixture_strlcpy
static void fixture_strlcpy(char *out, const char *in, size_t n){
	CHECK(strlen(in) < n);
	memcpy(out, in, strlen(in) + 1);
}
static void drm_mode_config_init(struct drm_device *d){
	CHECK(d == current->dev && devices == 1 && !modes);
	modes++;
}
static void drm_mode_config_cleanup(struct drm_device *d){
	CHECK(d == current->dev && modes == 1);
	modes--;
	cleanups++;
}
static int
drm_universal_plane_init(struct drm_device *d, struct plane *p, unsigned possible, const void *funcs, const uint32_t * formats, size_t n, const uint64_t * modifiers, unsigned type, const char *name)
{
	CHECK(d == current->dev && p == &current->plane && possible == 1 && funcs == &a733_plane_funcs && n == 1 && formats[0] == DRM_FORMAT_XRGB8888 && modifiers[0] == 0 && modifiers[1] == UINT64_MAX && type == DRM_PLANE_TYPE_PRIMARY && !name && modes == 1);
	return -operation();
}
static void drm_plane_helper_add(struct plane *p, const void *h){
	CHECK(p == &current->plane && h == &a733_plane_helpers);
	p->helper = h;
}
static int drm_crtc_init_with_planes(struct drm_device *d, struct crtc *c, struct plane *p, void *cursor, const void *f, const char *name){
	CHECK(d == current->dev && c == &current->crtc && p == &current->plane && !cursor && f == &a733_crtc_funcs && !name);
	return -operation();
}
static int drm_encoder_init(struct drm_device *d, struct encoder *e, const void *f, unsigned type, const char *name){
	CHECK(d == current->dev && e == &current->encoder && f == &a733_encoder_funcs && type == DRM_MODE_ENCODER_TMDS && !name);
	return -operation();
}
static unsigned drm_crtc_mask(struct crtc *c){
	CHECK(c == &current->crtc);
	return 1;
}
static int drm_connector_init(struct drm_device *d, struct connector *c, const void *f, unsigned type){
	CHECK(d == current->dev && c == &current->connector && f == &a733_connector_funcs && type == DRM_MODE_CONNECTOR_HDMIA);
	return -operation();
}
static void drm_connector_helper_add(struct connector *c, const void *h){
	CHECK(c == &current->connector && h == &a733_connector_helpers);
	c->helper = h;
}
static int drm_connector_attach_encoder(struct connector *c, struct encoder *e){
	CHECK(c == &current->connector && e == &current->encoder && e->possible_crtcs == 1);
	return -operation();
}
static void drm_mode_config_reset(struct drm_device *d){
	CHECK(d == current->dev);
	current->plane.state = reset_failure == 1 ? NULL : d;
	current->crtc.state = reset_failure == 2 ? NULL : d;
	current->connector.state = reset_failure == 3 ? NULL : d;
}
static int drm_vblank_init(struct drm_device *d, unsigned n){
	CHECK(d == current->dev && n == 1 && !vblank_storage && current->crtc.state);
	int e = operation();
	if (!e) {
		vblank_storage = 1;
		d->num_crtcs = n;
	}
	return -e;
}
static int sun60i_a733_de352_set_vblank_handler(void *client, sun60i_a733_vblank_fn fn, void *arg){
	CHECK(client == current && acquired == 1 && devices == 1);
	if (fn == NULL) {
		CHECK(arg == NULL && vblank_bound);
		vblank_bound = 0;
		vblank_unbinds++;
		return 0;
	}
	CHECK(fn == a733_vblank && arg == current && vblank_storage && !vblank_bound);
	CHECK(current->dev->max_vblank_count == UINT32_MAX);
	int e = operation();
	if (!e && vblank_bind_error)
		e = vblank_bind_error;
	if (!e)
		vblank_bound = 1;
	return e;
}
static int drm_dev_register(struct drm_device *d, unsigned flags){
	CHECK(vblank_storage && d->num_crtcs == 1);
	CHECK(d->irq_enabled == (vblank_bound != 0) && current->vblank_irq == (vblank_bound != 0));
	CHECK(d->cma_pool == current->pool && d->cma_pool != NULL);
	CHECK(!flags && d == current->dev && d->dmat == current->native.dmat && d->bus_dmat == d->dmat && d->bus_dmat32 == d->dmat && !d->dmat_subregion_p);
	CHECK(d->native_platform.version == 1 && d->native_platform.bus_type == 2 && !strcmp(d->native_platform.compatible, "allwinner,sun60i-a733-display"));
	CHECK(d->mode_config.funcs == &a733_mode_funcs && d->mode_config.min_width == 640 && d->mode_config.max_width == 640 && d->mode_config.min_height == 480 && d->mode_config.max_height == 480);
	CHECK(current->plane.helper == &a733_plane_helpers && current->connector.helper == &a733_connector_helpers && current->plane.state && current->crtc.state && current->connector.state);
	int e = operation();
	if (!e)
		registered++;
	return -e;
}
static void drm_kms_helper_poll_init(struct drm_device *dev)
{
	CHECK(dev == current->dev && registered == 1);
	CHECK(current->connector.polled ==
	    (DRM_CONNECTOR_POLL_CONNECT | DRM_CONNECTOR_POLL_DISCONNECT));
}
#include "registration.c"
static void reset(struct a733_drm *sc){
	CHECK(!devices && !queues && !modes && !acquired && !registered);
	memset(sc, 0, sizeof(*sc));
	current = sc;
	sc->phandle = 7;
	sc->device = sc;
	sc->pool = sc;
	calls = failed_step = releases = put_calls = cleanups = invalid_state = reset_failure = 0;
	vblank_storage = vblank_bound = vblank_unbinds = 0;
	vblank_bind_error = 0;
	release_error = state_error = 0;
}
int
main(void)
{
	struct a733_drm sc;
	for (unsigned fail = 1; fail <= 13; fail++)
		for (unsigned rel = 0; rel < 2; rel++) {
			reset(&sc);
			failed_step = fail;
			release_error = rel ? EBUSY : 0;
			CHECK(a733_register(&sc) == ENOMEM);
			CHECK(calls == fail && !devices && !queues && !modes && !registered);
			CHECK(sc.dev == NULL && sc.queue == NULL);
			CHECK(releases == (fail > 2 ? 1u : 0u));
			CHECK(!vblank_bound && vblank_unbinds == (fail == 13 ? 1u : 0u));
			CHECK(!sc.vblank_irq);
			CHECK(sc.registration_started == (fail > 2));
			CHECK(acquired == ((fail > 2 && rel) ? 1u : 0u));
			CHECK((sc.client != NULL) == (acquired != 0));
			acquired = 0;
		}
	for (unsigned invalid = 1; invalid <= 4; invalid++) {
		reset(&sc);
		invalid_state = invalid;
		CHECK(a733_register(&sc) == EINVAL && calls == 1 && !acquired && !sc.registration_started);
	}
	for (unsigned failed = 1; failed <= 3; failed++) {
		reset(&sc);
		reset_failure = failed;
		CHECK(a733_register(&sc) == ENOMEM && calls == 10 && !devices && !queues && !modes && !acquired && releases == 1 && put_calls == 1 && cleanups == 1);
	}
	for (unsigned i = 0; i < 2; i++) {
		reset(&sc);
		state_error = i ? ENXIO : EAGAIN;
		CHECK(a733_register(&sc) == state_error && !sc.registration_started && !acquired && calls == 1);
	}
	/* A missing boot reservation fails before DE client acquisition. */
	reset(&sc);
	sc.pool = NULL;
	CHECK(a733_register(&sc) == ENOMEM && calls == 1 && !acquired && !sc.registration_started && !queues && !devices);
	/* A provider without the TV0 interrupt keeps polled observation. */
	reset(&sc);
	vblank_bind_error = EOPNOTSUPP;
	CHECK(a733_register(&sc) == 0 && registered == 1 && calls == 13 && !vblank_bound && !sc.vblank_irq && !sc.dev->irq_enabled);
	CHECK(sc.dev->max_vblank_count == UINT32_MAX);
	free(sc.dev);
	registered = devices = queues = modes = acquired = 0;
	reset(&sc);
	vblank_bind_error = EBUSY;
	CHECK(a733_register(&sc) == EBUSY && calls == 12 && !devices && !acquired && !vblank_bound && vblank_unbinds == 0);
	reset(&sc);
	CHECK(a733_register(&sc) == 0 && registered == 1 && devices == 1 && queues == 1 && modes == 1 && acquired == 1 && calls == 13 && sc.registration_started);
	CHECK(releases == 0 && cleanups == 0 && put_calls == 0);
	CHECK(vblank_bound && sc.vblank_irq && sc.dev->irq_enabled && sc.dev->max_vblank_count == UINT32_MAX);
	/*
	 * Test teardown releases only fixture objects. Production has no
	 * detach.
	 */
	free(sc.dev);
	printf("PASS %u native registration boundary checks\n", checks);
	return 0;
}
