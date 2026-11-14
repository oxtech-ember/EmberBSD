/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Client for a KMS driver on the Linux 6.18 DRM core (efidrm618; any driver
 * with one CRTC, one primary XRGB8888 plane and one connector with one mode
 * will do), on raw ioctls, without libdrm.
 *
 * Checks: the resources, connector, encoder, CRTC and plane objects and their
 * properties; dumb buffers (create, map, mmap, destroy) and the errors for bad
 * parameters; ADDFB2, GETFB, RMFB; SETCRTC; PAGE_FLIP with events read from the
 * descriptor; DIRTYFB; the atomic commit (properties by name, test only, flags,
 * events, bad properties and objects); the vblank wait (WAIT_VBLANK,
 * CRTC_GET_SEQUENCE) or the explicit error when the driver has none; PRIME of
 * a dumb buffer; then the load: thousands of flips from several threads, a
 * churn of open and close, SIGKILL in the middle of flips.
 *
 * The output is one "ok"/"FAIL" line per check, no timings in the line, so the
 * output of different runs can be compared with diff(1); timings and
 * counters go to "note:" lines.
 *
 * With -m shot -d DIR the client draws the test pattern for the screen
 * comparison on the host: the pattern 0 by SETCRTC (DIR/ready0 appears, the
 * client waits for DIR/go0), the pattern 1 by PAGE_FLIP (ready1/go1), the
 * pattern 0 again by an atomic commit (ready2/go2).  pattern_px() is mirrored
 * in the host program that reads the screendump (kms-shot-compare.py).
 *
 * usage: drm2-kms-client [-s flips_per_thread] [-t threads] [-k kills]
 *                        [-m shot -d DIR] [-q]
 * exit status 0 if every check passed.
 */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <pthread.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>

#define DRM_FORMAT_XRGB8888_FOURCC	0x34325258u	/* 'XR24' */
#define DRM_FORMAT_ARGB8888_FOURCC	0x34325241u	/* 'AR24' */
#define DRM_PLANE_TYPE_PRIMARY	1
#define DRM_FORMAT_RGB565_FOURCC	0x36314752u	/* 'RG16' */

static int fd = -1;			/* the main descriptor, master */
static int nchecks, nfail;
static const char *shot_dir;

#define CHECK(cond, ...) do { \
	nchecks++; \
	if (cond) { printf("ok   "); } else { printf("FAIL "); nfail++; } \
	printf(__VA_ARGS__); printf("\n"); fflush(stdout); \
} while (0)

#define NOTE(...) do { printf("note: "); printf(__VA_ARGS__); printf("\n"); fflush(stdout); } while (0)

#define PTR(p)	((uint64_t)(uintptr_t)(p))

static int
ioc_fd(int f, unsigned long req, void *arg)
{
	return ioctl(f, req, arg) < 0 ? -errno : 0;
}

static int
ioc(unsigned long req, void *arg)
{
	return ioc_fd(fd, req, arg);
}

static double
now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

/* ------------------------------------------------------------ pattern */

#define PAT_MARK	16
static const uint8_t bars[8][3] = {
	{255, 255, 255}, {255, 255, 0}, {0, 255, 255}, {0, 255, 0},
	{255, 0, 255}, {255, 0, 0}, {0, 0, 255}, {0, 0, 0},
};
static const uint8_t corners[2][4][3] = {
	{ {255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255} },	/* TL TR BL BR */
	{ {0, 0, 255}, {255, 255, 0}, {255, 0, 255}, {0, 0, 0} },
};

/* The pattern of frame 0 or 1 at (x, y) of a W x H screen; mirrored in kms-shot-compare.py. */
static void
pattern_px(int frame, int x, int y, int W, int H, uint8_t rgb[3])
{
	int k, c;

	if ((x < PAT_MARK || x >= W - PAT_MARK) &&
	    (y < PAT_MARK || y >= H - PAT_MARK)) {
		c = (y >= H - PAT_MARK ? 2 : 0) + (x >= W - PAT_MARK ? 1 : 0);
		memcpy(rgb, corners[frame][c], 3);
		return;
	}
	if (y < (H * 3) / 4) {
		k = (x * 8) / W;
		if (frame)
			k = 7 - k;
		memcpy(rgb, bars[k], 3);
		return;
	}
	k = (x * 255) / (W - 1);
	if (frame)
		k = 255 - k;
	rgb[0] = rgb[1] = rgb[2] = (uint8_t)k;
}

static void
pattern_fill(void *map, uint32_t pitch, int frame, int W, int H)
{
	int x, y;
	uint8_t rgb[3];

	for (y = 0; y < H; y++) {
		uint32_t *row = (uint32_t *)((char *)map + (size_t)y * pitch);

		for (x = 0; x < W; x++) {
			pattern_px(frame, x, y, W, H, rgb);
			row[x] = ((uint32_t)rgb[0] << 16) |
			    ((uint32_t)rgb[1] << 8) | rgb[2];
		}
	}
}

/* ------------------------------------------------------------- helpers */

struct buf {
	uint32_t handle, pitch, fb_id;
	uint64_t size;
	uint32_t w, h;
	void *map;
};

static int
mkdumb_on(int f, uint32_t w, uint32_t h, uint32_t bpp, uint32_t flags,
    struct drm_mode_create_dumb *out)
{
	struct drm_mode_create_dumb cd;
	int r;

	memset(&cd, 0, sizeof cd);
	cd.width = w; cd.height = h; cd.bpp = bpp; cd.flags = flags;
	r = ioc_fd(f, DRM_IOCTL_MODE_CREATE_DUMB, &cd);
	if (out)
		*out = cd;
	return r;
}

static int
destroy_dumb_on(int f, uint32_t handle)
{
	struct drm_mode_destroy_dumb dd;

	memset(&dd, 0, sizeof dd);
	dd.handle = handle;
	return ioc_fd(f, DRM_IOCTL_MODE_DESTROY_DUMB, &dd);
}

static void *
map_dumb_on(int f, uint32_t handle, uint64_t size)
{
	struct drm_mode_map_dumb md;
	void *p;

	memset(&md, 0, sizeof md);
	md.handle = handle;
	if (ioc_fd(f, DRM_IOCTL_MODE_MAP_DUMB, &md) != 0)
		return NULL;
	p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, f, (off_t)md.offset);
	return p == MAP_FAILED ? NULL : p;
}

static int
addfb2_on(int f, uint32_t w, uint32_t h, uint32_t fmt, uint32_t handle,
    uint32_t pitch, uint32_t offset, uint32_t flags, uint32_t *fb_id)
{
	struct drm_mode_fb_cmd2 c;
	int r;

	memset(&c, 0, sizeof c);
	c.width = w; c.height = h; c.pixel_format = fmt; c.flags = flags;
	c.handles[0] = handle; c.pitches[0] = pitch; c.offsets[0] = offset;
	r = ioc_fd(f, DRM_IOCTL_MODE_ADDFB2, &c);
	if (r == 0 && fb_id)
		*fb_id = c.fb_id;
	return r;
}

static int
rmfb_on(int f, uint32_t id)
{

	return ioc_fd(f, DRM_IOCTL_MODE_RMFB, &id);
}

/* a dumb buffer with a framebuffer and a mapping */
static int
buf_new(struct buf *b, uint32_t w, uint32_t h)
{
	struct drm_mode_create_dumb cd;
	int r;

	memset(b, 0, sizeof *b);
	r = mkdumb_on(fd, w, h, 32, 0, &cd);
	if (r)
		return r;
	b->handle = cd.handle; b->pitch = cd.pitch; b->size = cd.size;
	b->w = w; b->h = h;
	b->map = map_dumb_on(fd, b->handle, b->size);
	if (b->map == NULL)
		return -ENOMEM;
	return addfb2_on(fd, w, h, DRM_FORMAT_XRGB8888_FOURCC, b->handle,
	    b->pitch, 0, 0, &b->fb_id);
}

static void
buf_free(struct buf *b)
{

	if (b->fb_id)
		rmfb_on(fd, b->fb_id);
	if (b->map)
		munmap(b->map, b->size);
	if (b->handle)
		destroy_dumb_on(fd, b->handle);
	memset(b, 0, sizeof *b);
}

/*
 * The event of a flip or a vblank: one event per read (the buffer holds one
 * struct drm_event_vblank, and the driver hands out whole events only).  The
 * descriptor is non-blocking: with several threads reading, the event that poll
 * reported may be gone when the read comes.
 */
static int
read_event(int f, int timeout_ms, struct drm_event_vblank *ev)
{
	struct pollfd p;
	double end = now_ms() + timeout_ms;
	ssize_t n;
	int r, left;

	for (;;) {
		left = (int)(end - now_ms());
		if (left < 0)
			left = 0;
		p.fd = f; p.events = POLLIN; p.revents = 0;
		r = poll(&p, 1, left);
		if (r < 0)
			return -errno;
		if (r == 0)
			return -ETIMEDOUT;
		memset(ev, 0, sizeof *ev);
		n = read(f, ev, sizeof *ev);
		if (n == (ssize_t)sizeof *ev)
			return 0;
		if (n < 0 && errno == EAGAIN)
			continue;
		return n < 0 ? -errno : -EIO;
	}
}

static int
drain_events(int f)
{
	struct drm_event_vblank ev;
	int n = 0;

	while (read_event(f, 0, &ev) == 0)
		n++;
	return n;
}

/* ------------------------------------------------------------ objects */

static uint32_t crtc_id, conn_id, enc_id, plane_id;
static struct drm_mode_modeinfo mode;
static int W, H;

struct propmap { uint32_t id; char name[DRM_PROP_NAME_LEN]; uint64_t val; };

static int
get_props(int f, uint32_t obj_id, uint32_t obj_type, struct propmap *pm, int max)
{
	struct drm_mode_obj_get_properties op;
	uint32_t ids[64];
	uint64_t vals[64];
	int i, n;

	memset(&op, 0, sizeof op);
	op.obj_id = obj_id; op.obj_type = obj_type;
	op.props_ptr = PTR(ids); op.prop_values_ptr = PTR(vals);
	op.count_props = 64;
	if (ioc_fd(f, DRM_IOCTL_MODE_OBJ_GETPROPERTIES, &op) != 0)
		return -1;
	n = (int)op.count_props < max ? (int)op.count_props : max;
	for (i = 0; i < n; i++) {
		struct drm_mode_get_property gp;

		memset(&gp, 0, sizeof gp);
		gp.prop_id = ids[i];
		if (ioc_fd(f, DRM_IOCTL_MODE_GETPROPERTY, &gp) != 0)
			return -1;
		pm[i].id = ids[i];
		pm[i].val = vals[i];
		memcpy(pm[i].name, gp.name, DRM_PROP_NAME_LEN);
		pm[i].name[DRM_PROP_NAME_LEN - 1] = 0;
	}
	return n;
}

static uint32_t
prop_id(const struct propmap *pm, int n, const char *name)
{
	int i;

	for (i = 0; i < n; i++)
		if (strcmp(pm[i].name, name) == 0)
			return pm[i].id;
	return 0;
}

static int
setcrtc_on(int f, uint32_t fb_id, int x, int y, int enable, const struct drm_mode_modeinfo *m)
{
	struct drm_mode_crtc c;
	uint32_t conns[1];

	memset(&c, 0, sizeof c);
	c.crtc_id = crtc_id;
	c.fb_id = fb_id;
	c.x = x; c.y = y;
	if (enable) {
		conns[0] = conn_id;
		c.set_connectors_ptr = PTR(conns);
		c.count_connectors = 1;
		c.mode = *m;
		c.mode_valid = 1;
	}
	return ioc_fd(f, DRM_IOCTL_MODE_SETCRTC, &c);
}

static int
flip_on(int f, uint32_t fb_id, uint32_t flags, uint64_t user)
{
	struct drm_mode_crtc_page_flip pf;

	memset(&pf, 0, sizeof pf);
	pf.crtc_id = crtc_id; pf.fb_id = fb_id; pf.flags = flags;
	pf.user_data = user;
	return ioc_fd(f, DRM_IOCTL_MODE_PAGE_FLIP, &pf);
}

/* ---------------------------------------------------------- atomic */

struct atom {
	uint32_t objs[8];
	uint32_t cnt[8];
	uint32_t props[64];
	uint64_t vals[64];
	int nobj, nprop;
};

static void
atom_add(struct atom *a, uint32_t obj, uint32_t prop, uint64_t val)
{
	int i;

	for (i = 0; i < a->nobj; i++)
		if (a->objs[i] == obj)
			break;
	if (i == a->nobj) {
		a->objs[i] = obj;
		a->cnt[i] = 0;
		a->nobj++;
	}
	/* properties of one object must be contiguous: callers add object by object */
	a->props[a->nprop] = prop;
	a->vals[a->nprop] = val;
	a->nprop++;
	a->cnt[i]++;
}

static int
atom_commit(int f, const struct atom *a, uint32_t flags, uint64_t user)
{
	struct drm_mode_atomic at;

	memset(&at, 0, sizeof at);
	at.flags = flags;
	at.count_objs = a->nobj;
	at.objs_ptr = PTR(a->objs);
	at.count_props_ptr = PTR(a->cnt);
	at.props_ptr = PTR(a->props);
	at.prop_values_ptr = PTR(a->vals);
	at.user_data = user;
	return ioc_fd(f, DRM_IOCTL_MODE_ATOMIC, &at);
}

static struct propmap ppl[32], pcrtc[32], pconn[32];
static int nppl, npcrtc, npconn;

/* modeset commit: plane -> crtc with fb, mode blob, active, connector -> crtc */
static void
atom_modeset(struct atom *a, uint32_t fb_id, uint32_t blob)
{

	atom_add(a, conn_id, prop_id(pconn, npconn, "CRTC_ID"), crtc_id);
	atom_add(a, crtc_id, prop_id(pcrtc, npcrtc, "MODE_ID"), blob);
	atom_add(a, crtc_id, prop_id(pcrtc, npcrtc, "ACTIVE"), 1);
	atom_add(a, plane_id, prop_id(ppl, nppl, "FB_ID"), fb_id);
	atom_add(a, plane_id, prop_id(ppl, nppl, "CRTC_ID"), crtc_id);
	atom_add(a, plane_id, prop_id(ppl, nppl, "SRC_X"), 0);
	atom_add(a, plane_id, prop_id(ppl, nppl, "SRC_Y"), 0);
	atom_add(a, plane_id, prop_id(ppl, nppl, "SRC_W"), (uint64_t)W << 16);
	atom_add(a, plane_id, prop_id(ppl, nppl, "SRC_H"), (uint64_t)H << 16);
	atom_add(a, plane_id, prop_id(ppl, nppl, "CRTC_X"), 0);
	atom_add(a, plane_id, prop_id(ppl, nppl, "CRTC_Y"), 0);
	atom_add(a, plane_id, prop_id(ppl, nppl, "CRTC_W"), W);
	atom_add(a, plane_id, prop_id(ppl, nppl, "CRTC_H"), H);
}

static void
atom_flip(struct atom *a, uint32_t fb_id)
{

	atom_add(a, plane_id, prop_id(ppl, nppl, "FB_ID"), fb_id);
}

static int
mkblob(int f, const void *data, uint32_t len, uint32_t *id)
{
	struct drm_mode_create_blob cb;
	int r;

	memset(&cb, 0, sizeof cb);
	cb.data = PTR(data); cb.length = len;
	r = ioc_fd(f, DRM_IOCTL_MODE_CREATEPROPBLOB, &cb);
	if (r == 0)
		*id = cb.blob_id;
	return r;
}

/* --------------------------------------------------------- the checks */

static int
open_card_mode(int nonblock)
{
	char path[64], name[32];
	int i, f;
	struct drm_version v;

	for (i = 0; i < 64; i++) {
		snprintf(path, sizeof path, "/dev/dri/card%d", i);
		f = open(path, O_RDWR | O_CLOEXEC);
		if (f < 0)
			continue;
		memset(&v, 0, sizeof v);
		memset(name, 0, sizeof name);
		v.name = name;
		v.name_len = sizeof name - 1;
		if (ioctl(f, DRM_IOCTL_VERSION, &v) == 0 &&
		    strcmp(name, "efidrm") == 0) {
			if (nonblock)
				fcntl(f, F_SETFL, fcntl(f, F_GETFL) | O_NONBLOCK);
			return f;
		}
		close(f);
	}
	return -1;
}

static int
open_card(void)
{

	return open_card_mode(1);
}

static void
test_basics(void)
{
	struct drm_version v;
	char name[32], desc[64];
	struct drm_get_cap gc;
	struct drm_set_client_cap sc;

	memset(&v, 0, sizeof v);
	v.name = name; v.name_len = sizeof name - 1;
	v.desc = desc; v.desc_len = sizeof desc - 1;
	memset(name, 0, sizeof name); memset(desc, 0, sizeof desc);
	CHECK(ioc(DRM_IOCTL_VERSION, &v) == 0 && strcmp(name, "efidrm") == 0,
	    "DRM_IOCTL_VERSION: driver name efidrm");

	memset(&gc, 0, sizeof gc); gc.capability = DRM_CAP_DUMB_BUFFER;
	CHECK(ioc(DRM_IOCTL_GET_CAP, &gc) == 0 && gc.value == 1, "cap DUMB_BUFFER = 1");
	memset(&gc, 0, sizeof gc); gc.capability = DRM_CAP_TIMESTAMP_MONOTONIC;
	CHECK(ioc(DRM_IOCTL_GET_CAP, &gc) == 0 && gc.value == 1, "cap TIMESTAMP_MONOTONIC = 1");
	memset(&gc, 0, sizeof gc); gc.capability = DRM_CAP_PRIME;
	CHECK(ioc(DRM_IOCTL_GET_CAP, &gc) == 0 && gc.value == (DRM_PRIME_CAP_IMPORT | DRM_PRIME_CAP_EXPORT),
	    "cap PRIME = import|export (%llu)", (unsigned long long)gc.value);
	memset(&gc, 0, sizeof gc); gc.capability = DRM_CAP_CRTC_IN_VBLANK_EVENT;
	CHECK(ioc(DRM_IOCTL_GET_CAP, &gc) == 0 && gc.value == 1, "cap CRTC_IN_VBLANK_EVENT = 1");
	memset(&gc, 0, sizeof gc); gc.capability = 0xdead;
	CHECK(ioc(DRM_IOCTL_GET_CAP, &gc) == -EINVAL, "unknown cap: EINVAL");

	memset(&sc, 0, sizeof sc); sc.capability = 0xdead; sc.value = 1;
	CHECK(ioc(DRM_IOCTL_SET_CLIENT_CAP, &sc) == -EINVAL, "unknown client cap: EINVAL");
	memset(&sc, 0, sizeof sc); sc.capability = DRM_CLIENT_CAP_UNIVERSAL_PLANES; sc.value = 2;
	CHECK(ioc(DRM_IOCTL_SET_CLIENT_CAP, &sc) == -EINVAL, "UNIVERSAL_PLANES value 2: EINVAL");
	memset(&sc, 0, sizeof sc); sc.capability = DRM_CLIENT_CAP_UNIVERSAL_PLANES; sc.value = 1;
	CHECK(ioc(DRM_IOCTL_SET_CLIENT_CAP, &sc) == 0, "UNIVERSAL_PLANES on");
}

static void
test_resources(void)
{
	struct drm_mode_card_res res;
	struct drm_mode_get_connector gc;
	struct drm_mode_get_encoder ge;
	struct drm_mode_crtc cr;
	struct drm_mode_get_plane_res pr;
	struct drm_mode_get_plane gp;
	uint32_t fbs[8], crtcs[8], conns[8], encs[8], planes[8], fmts[16];
	uint32_t cprops[32], encs2[8];
	uint64_t cvals[32];
	struct drm_mode_modeinfo modes[8];
	int i;

	memset(&res, 0, sizeof res);
	res.fb_id_ptr = PTR(fbs); res.crtc_id_ptr = PTR(crtcs);
	res.connector_id_ptr = PTR(conns); res.encoder_id_ptr = PTR(encs);
	res.count_fbs = res.count_crtcs = res.count_connectors = res.count_encoders = 8;
	CHECK(ioc(DRM_IOCTL_MODE_GETRESOURCES, &res) == 0, "GETRESOURCES");
	CHECK(res.count_crtcs == 1 && res.count_connectors == 1 && res.count_encoders == 1,
	    "1 crtc, 1 connector, 1 encoder (%u/%u/%u)", res.count_crtcs,
	    res.count_connectors, res.count_encoders);
	CHECK(res.min_width >= 1 && res.max_width >= 1024 && res.max_height >= 768,
	    "size limits %u..%u x %u..%u", res.min_width, res.max_width, res.min_height, res.max_height);
	crtc_id = crtcs[0]; conn_id = conns[0]; enc_id = encs[0];
	NOTE("crtc %u connector %u encoder %u", crtc_id, conn_id, enc_id);

	/* the count-only call, then a buffer that is too small */
	memset(&res, 0, sizeof res);
	CHECK(ioc(DRM_IOCTL_MODE_GETRESOURCES, &res) == 0 && res.count_crtcs == 1,
	    "GETRESOURCES with zero buffers only counts");

	/* the first call with no buffers makes the master probe, the second one fills the buffers */
	memset(&gc, 0, sizeof gc);
	gc.connector_id = conn_id;
	CHECK(ioc(DRM_IOCTL_MODE_GETCONNECTOR, &gc) == 0 && gc.count_modes == 1 && gc.connection == 1,
	    "GETCONNECTOR without buffers probes: %u mode, status %u", gc.count_modes, gc.connection);
	memset(&gc, 0, sizeof gc);
	gc.connector_id = conn_id;
	gc.modes_ptr = PTR(modes); gc.count_modes = 8;
	gc.props_ptr = PTR(cprops); gc.prop_values_ptr = PTR(cvals); gc.count_props = 32;
	gc.encoders_ptr = PTR(encs2); gc.count_encoders = 8;
	CHECK(ioc(DRM_IOCTL_MODE_GETCONNECTOR, &gc) == 0, "GETCONNECTOR");
	CHECK(gc.connection == 1, "connector is connected (%u)", gc.connection);
	CHECK(gc.connector_type == DRM_MODE_CONNECTOR_eDP, "connector type eDP (%u)", gc.connector_type);
	CHECK(gc.count_modes == 1, "one mode (%u)", gc.count_modes);
	CHECK(gc.count_encoders == 1 && encs2[0] == enc_id, "one encoder, the encoder of the resources");
	CHECK(gc.count_props >= 3, "connector properties: %u", gc.count_props);
	CHECK(gc.mm_width > 0 && gc.mm_height > 0, "physical size %ux%u mm", gc.mm_width, gc.mm_height);
	mode = modes[0];
	W = mode.hdisplay; H = mode.vdisplay;
	CHECK(W >= 640 && H >= 480, "mode %dx%d@%u", W, H, mode.vrefresh);
	CHECK((mode.type & DRM_MODE_TYPE_PREFERRED) != 0, "the mode is preferred");
	NOTE("mode %s %dx%d refresh %u clock %u", mode.name, W, H, mode.vrefresh, mode.clock);

	memset(&gc, 0, sizeof gc);
	gc.connector_id = 0xdeadbeef;
	CHECK(ioc(DRM_IOCTL_MODE_GETCONNECTOR, &gc) == -ENOENT, "GETCONNECTOR bad id: ENOENT");

	memset(&ge, 0, sizeof ge);
	ge.encoder_id = enc_id;
	CHECK(ioc(DRM_IOCTL_MODE_GETENCODER, &ge) == 0 && ge.encoder_type == DRM_MODE_ENCODER_VIRTUAL &&
	    (ge.possible_crtcs & 1) != 0, "GETENCODER: virtual, crtc 0 possible");
	memset(&ge, 0, sizeof ge);
	ge.encoder_id = 0xdeadbeef;
	CHECK(ioc(DRM_IOCTL_MODE_GETENCODER, &ge) == -ENOENT, "GETENCODER bad id: ENOENT");

	memset(&cr, 0, sizeof cr);
	cr.crtc_id = crtc_id;
	CHECK(ioc(DRM_IOCTL_MODE_GETCRTC, &cr) == 0, "GETCRTC");
	NOTE("crtc before: fb %u mode_valid %u", cr.fb_id, cr.mode_valid);
	memset(&cr, 0, sizeof cr);
	cr.crtc_id = 0xdeadbeef;
	CHECK(ioc(DRM_IOCTL_MODE_GETCRTC, &cr) == -ENOENT, "GETCRTC bad id: ENOENT");

	memset(&pr, 0, sizeof pr);
	pr.plane_id_ptr = PTR(planes); pr.count_planes = 8;
	CHECK(ioc(DRM_IOCTL_MODE_GETPLANERESOURCES, &pr) == 0 && pr.count_planes == 1,
	    "GETPLANERESOURCES: one plane (%u)", pr.count_planes);
	plane_id = planes[0];
	memset(&gp, 0, sizeof gp);
	gp.plane_id = plane_id;
	gp.format_type_ptr = PTR(fmts); gp.count_format_types = 16;
	CHECK(ioc(DRM_IOCTL_MODE_GETPLANE, &gp) == 0, "GETPLANE");
	CHECK(gp.count_format_types == 2, "plane formats: %u", gp.count_format_types);
	{
		int hx = 0, ha = 0;

		for (i = 0; i < (int)gp.count_format_types && i < 16; i++) {
			hx |= fmts[i] == DRM_FORMAT_XRGB8888_FOURCC;
			ha |= fmts[i] == DRM_FORMAT_ARGB8888_FOURCC;
		}
		CHECK(hx && ha, "plane formats are XRGB8888 and ARGB8888");
	}
	CHECK((gp.possible_crtcs & 1) != 0, "plane can use crtc 0");
	memset(&gp, 0, sizeof gp);
	gp.plane_id = 0xdeadbeef;
	CHECK(ioc(DRM_IOCTL_MODE_GETPLANE, &gp) == -ENOENT, "GETPLANE bad id: ENOENT");

	/* a legacy client sees only the properties that are not atomic-only */
	nppl = get_props(fd, plane_id, DRM_MODE_OBJECT_PLANE, ppl, 32);
	npcrtc = get_props(fd, crtc_id, DRM_MODE_OBJECT_CRTC, pcrtc, 32);
	npconn = get_props(fd, conn_id, DRM_MODE_OBJECT_CONNECTOR, pconn, 32);
	CHECK(nppl > 0 && npcrtc >= 0 && npconn > 0, "legacy properties read: plane %d crtc %d connector %d", nppl, npcrtc, npconn);
	CHECK(prop_id(ppl, nppl, "type") != 0, "plane has the property type");
	CHECK(prop_id(pconn, npconn, "DPMS") != 0, "connector has the property DPMS");
	CHECK(prop_id(ppl, nppl, "FB_ID") == 0 && prop_id(pcrtc, npcrtc, "ACTIVE") == 0,
	    "atomic-only properties are hidden from a legacy client");
	for (i = 0; i < nppl; i++)
		if (strcmp(ppl[i].name, "type") == 0)
			CHECK(ppl[i].val == DRM_PLANE_TYPE_PRIMARY, "plane type is primary (%llu)",
			    (unsigned long long)ppl[i].val);
}

static void
test_dumb(void)
{
	struct drm_mode_create_dumb cd;
	struct drm_mode_map_dumb md;
	uint32_t h1;
	uint64_t size;
	volatile uint32_t *p;
	void *m;
	int r;

	CHECK(mkdumb_on(fd, 64, 64, 32, 0, &cd) == 0, "CREATE_DUMB 64x64x32");
	CHECK(cd.pitch == 256 && cd.size >= 64 * 256 && cd.handle != 0, "pitch %u size %llu handle %u",
	    cd.pitch, (unsigned long long)cd.size, cd.handle);
	h1 = cd.handle; size = cd.size;
	m = map_dumb_on(fd, h1, size);
	CHECK(m != NULL, "MAP_DUMB + mmap");
	if (m) {
		p = m;
		p[0] = 0x11223344; p[64 * 63 + 63] = 0x55667788;
		CHECK(p[0] == 0x11223344 && p[64 * 63 + 63] == 0x55667788, "write and read back through the mapping");
		munmap(m, size);
	}
	/* the mapping is gone: the object stays, map it again, the data is still there */
	m = map_dumb_on(fd, h1, size);
	CHECK(m != NULL && ((volatile uint32_t *)m)[0] == 0x11223344, "second mapping sees the data");
	if (m)
		munmap(m, size);

	CHECK(mkdumb_on(fd, 0, 64, 32, 0, NULL) == -EINVAL, "CREATE_DUMB width 0: EINVAL");
	CHECK(mkdumb_on(fd, 64, 0, 32, 0, NULL) == -EINVAL, "CREATE_DUMB height 0: EINVAL");
	CHECK(mkdumb_on(fd, 64, 64, 16, 0, NULL) == -EINVAL, "CREATE_DUMB bpp 16: EINVAL");
	CHECK(mkdumb_on(fd, 64, 64, 0, 0, NULL) == -EINVAL, "CREATE_DUMB bpp 0: EINVAL");
	CHECK(mkdumb_on(fd, 64, 64, 32, 1, NULL) == -EINVAL, "CREATE_DUMB flags: EINVAL");
	CHECK(mkdumb_on(fd, 20000, 64, 32, 0, NULL) == -EINVAL, "CREATE_DUMB width 20000: EINVAL");
	CHECK(mkdumb_on(fd, 8193, 8, 32, 0, NULL) == -EINVAL, "CREATE_DUMB width 8193 (over the limit): EINVAL");
	CHECK(mkdumb_on(fd, 0xffffffffu, 0xffffffffu, 32, 0, NULL) == -EINVAL, "CREATE_DUMB 4G x 4G: EINVAL");

	memset(&md, 0, sizeof md);
	md.handle = 0xdeadbeef;
	CHECK(ioc(DRM_IOCTL_MODE_MAP_DUMB, &md) == -ENOENT, "MAP_DUMB bad handle: ENOENT");
	md.handle = 0;
	CHECK(ioc(DRM_IOCTL_MODE_MAP_DUMB, &md) == -ENOENT, "MAP_DUMB handle 0: ENOENT");
	{
		void *bad = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)0x7fffff000ULL);
		CHECK(bad == MAP_FAILED, "mmap of a bad offset fails (errno %d)", bad == MAP_FAILED ? errno : 0);
		if (bad != MAP_FAILED)
			munmap(bad, 4096);
	}
	r = destroy_dumb_on(fd, h1);
	CHECK(r == 0, "DESTROY_DUMB");
	CHECK(destroy_dumb_on(fd, h1) != 0, "DESTROY_DUMB twice fails");
	CHECK(destroy_dumb_on(fd, 0xdeadbeef) != 0, "DESTROY_DUMB bad handle fails");
}

static void
test_addfb(void)
{
	struct drm_mode_create_dumb cd, small;
	struct drm_mode_fb_cmd get;
	uint32_t fb1, fb2, fb3;
	struct drm_mode_fb_cmd2 c;

	CHECK(mkdumb_on(fd, 640, 480, 32, 0, &cd) == 0, "dumb 640x480 for the framebuffers");
	CHECK(addfb2_on(fd, 640, 480, DRM_FORMAT_XRGB8888_FOURCC, cd.handle, cd.pitch, 0, 0, &fb1) == 0,
	    "ADDFB2 XRGB8888");
	CHECK(addfb2_on(fd, 640, 480, DRM_FORMAT_ARGB8888_FOURCC, cd.handle, cd.pitch, 0, 0, &fb2) == 0,
	    "ADDFB2 ARGB8888 of the same buffer");
	CHECK(fb1 != 0 && fb2 != 0 && fb1 != fb2, "two different fb ids (%u %u)", fb1, fb2);
	memset(&get, 0, sizeof get);
	get.fb_id = fb1;
	CHECK(ioc(DRM_IOCTL_MODE_GETFB, &get) == 0 && get.width == 640 && get.height == 480 &&
	    get.pitch == cd.pitch && get.bpp == 32 && get.depth == 24,
	    "GETFB: 640x480 pitch %u bpp %u depth %u", get.pitch, get.bpp, get.depth);
	CHECK(addfb2_on(fd, 640, 480, DRM_FORMAT_XRGB8888_FOURCC, 0xdeadbeef, cd.pitch, 0, 0, &fb3) == -ENOENT,
	    "ADDFB2 bad handle: ENOENT");
	CHECK(addfb2_on(fd, 640, 480, DRM_FORMAT_RGB565_FOURCC, cd.handle, 640 * 2, 0, 0, &fb3) == -EINVAL,
	    "ADDFB2 RGB565 (not offered by the plane): EINVAL");
	CHECK(addfb2_on(fd, 640, 480, 0x12345678, cd.handle, cd.pitch, 0, 0, &fb3) == -EINVAL,
	    "ADDFB2 unknown format: EINVAL");
	CHECK(addfb2_on(fd, 640, 480, DRM_FORMAT_XRGB8888_FOURCC, cd.handle, 64, 0, 0, &fb3) == -EINVAL,
	    "ADDFB2 pitch too small: EINVAL");
	CHECK(addfb2_on(fd, 640, 480, DRM_FORMAT_XRGB8888_FOURCC, cd.handle, 641 * 4 + 1, 0, 0, &fb3) != 0,
	    "ADDFB2 unaligned/too large pitch fails");
	CHECK(addfb2_on(fd, 640, 480, DRM_FORMAT_XRGB8888_FOURCC, cd.handle, cd.pitch, 4096 * 1024, 0, &fb3) == -EINVAL,
	    "ADDFB2 offset past the object: EINVAL");
	CHECK(addfb2_on(fd, 640, 481, DRM_FORMAT_XRGB8888_FOURCC, cd.handle, cd.pitch, 0, 0, &fb3) == -EINVAL,
	    "ADDFB2 one row more than the object has: EINVAL");
	CHECK(addfb2_on(fd, 0, 480, DRM_FORMAT_XRGB8888_FOURCC, cd.handle, cd.pitch, 0, 0, &fb3) == -EINVAL,
	    "ADDFB2 width 0: EINVAL");
	CHECK(addfb2_on(fd, 640, 0, DRM_FORMAT_XRGB8888_FOURCC, cd.handle, cd.pitch, 0, 0, &fb3) == -EINVAL,
	    "ADDFB2 height 0: EINVAL");
	CHECK(addfb2_on(fd, 100000, 480, DRM_FORMAT_XRGB8888_FOURCC, cd.handle, cd.pitch, 0, 0, &fb3) == -EINVAL,
	    "ADDFB2 width past the limit: EINVAL");
	CHECK(addfb2_on(fd, 640, 480, DRM_FORMAT_XRGB8888_FOURCC, cd.handle, cd.pitch, 0, 0x80, &fb3) == -EINVAL,
	    "ADDFB2 unknown flags: EINVAL");
	memset(&c, 0, sizeof c);
	c.width = 640; c.height = 480; c.pixel_format = DRM_FORMAT_XRGB8888_FOURCC;
	c.handles[0] = cd.handle; c.pitches[0] = cd.pitch;
	c.flags = DRM_MODE_FB_MODIFIERS; c.modifier[0] = 1;	/* a modifier the plane does not know */
	CHECK(ioc(DRM_IOCTL_MODE_ADDFB2, &c) != 0, "ADDFB2 with an unknown modifier fails");
	/* a buffer that is smaller than the framebuffer */
	CHECK(mkdumb_on(fd, 64, 64, 32, 0, &small) == 0, "dumb 64x64");
	CHECK(addfb2_on(fd, 640, 480, DRM_FORMAT_XRGB8888_FOURCC, small.handle, 640 * 4, 0, 0, &fb3) == -EINVAL,
	    "ADDFB2 640x480 over a 64x64 buffer: EINVAL");
	CHECK(rmfb_on(fd, fb1) == 0, "RMFB");
	CHECK(rmfb_on(fd, fb1) != 0, "RMFB twice fails");
	CHECK(rmfb_on(fd, 0xdeadbeef) != 0, "RMFB bad id fails");
	memset(&get, 0, sizeof get);
	get.fb_id = fb1;
	CHECK(ioc(DRM_IOCTL_MODE_GETFB, &get) == -ENOENT, "GETFB after RMFB: ENOENT");
	rmfb_on(fd, fb2);
	destroy_dumb_on(fd, cd.handle);
	destroy_dumb_on(fd, small.handle);
}

static struct buf fbA, fbB;

static void
test_setcrtc(void)
{
	struct drm_mode_crtc cr;
	struct buf small;
	struct drm_mode_modeinfo bad;
	uint32_t conns[1] = { conn_id };
	int i;
	volatile uint8_t *px;

	CHECK(buf_new(&fbA, W, H) == 0, "frame buffer A %dx%d", W, H);
	CHECK(buf_new(&fbB, W, H) == 0, "frame buffer B %dx%d", W, H);
	if (fbA.map == NULL || fbB.map == NULL)
		return;
	pattern_fill(fbA.map, fbA.pitch, 0, W, H);
	pattern_fill(fbB.map, fbB.pitch, 1, W, H);
	px = fbA.map;
	CHECK(px[0 * 4 + 2] == 255 && px[0 * 4 + 1] == 0 && px[0 * 4 + 0] == 0, "pattern 0 corner (red) in the mapping");

	CHECK(setcrtc_on(fd, fbA.fb_id, 0, 0, 1, &mode) == 0, "SETCRTC fb A with the mode");
	memset(&cr, 0, sizeof cr);
	cr.crtc_id = crtc_id;
	CHECK(ioc(DRM_IOCTL_MODE_GETCRTC, &cr) == 0 && cr.fb_id == fbA.fb_id && cr.mode_valid == 1 &&
	    cr.mode.hdisplay == W && cr.mode.vdisplay == H, "GETCRTC: fb A, mode valid");

	bad = mode;
	bad.hdisplay = (uint16_t)(W - 8);
	CHECK(setcrtc_on(fd, fbA.fb_id, 0, 0, 1, &bad) == -EINVAL, "SETCRTC with another mode size: EINVAL");
	CHECK(setcrtc_on(fd, 0xdeadbeef, 0, 0, 1, &mode) == -ENOENT, "SETCRTC bad fb: ENOENT");
	{
		int r = setcrtc_on(fd, fbA.fb_id, 8, 0, 1, &mode);
		CHECK(r == -ENOSPC || r == -EINVAL, "SETCRTC x offset 8 over the edge: error (%d)", r);
		r = setcrtc_on(fd, fbA.fb_id, 0, -1, 1, &mode);
		CHECK(r != 0, "SETCRTC negative y: error (%d)", r);
	}
	{
		struct drm_mode_crtc c2;
		uint32_t badconn[1] = { 0xdeadbeef };

		memset(&c2, 0, sizeof c2);
		c2.crtc_id = crtc_id; c2.fb_id = fbA.fb_id; c2.mode = mode; c2.mode_valid = 1;
		c2.set_connectors_ptr = PTR(badconn); c2.count_connectors = 1;
		CHECK(ioc(DRM_IOCTL_MODE_SETCRTC, &c2) == -ENOENT, "SETCRTC bad connector id: ENOENT");
		c2.crtc_id = 0xdeadbeef;
		c2.set_connectors_ptr = PTR(conns);
		CHECK(ioc(DRM_IOCTL_MODE_SETCRTC, &c2) == -ENOENT, "SETCRTC bad crtc id: ENOENT");
		memset(&c2, 0, sizeof c2);
		c2.crtc_id = crtc_id; c2.fb_id = fbA.fb_id; c2.mode = mode; c2.mode_valid = 1;
		c2.count_connectors = 1;			/* the pointer is NULL */
		CHECK(ioc(DRM_IOCTL_MODE_SETCRTC, &c2) != 0, "SETCRTC connectors count without pointer: error");
		memset(&c2, 0, sizeof c2);
		c2.crtc_id = crtc_id; c2.fb_id = fbA.fb_id; c2.mode = mode; c2.mode_valid = 1;
		c2.count_connectors = 1000000;
		c2.set_connectors_ptr = PTR(conns);
		CHECK(ioc(DRM_IOCTL_MODE_SETCRTC, &c2) != 0, "SETCRTC a million connectors: error");
	}
	/* a framebuffer smaller than the mode */
	if (buf_new(&small, 64, 64) == 0) {
		CHECK(setcrtc_on(fd, small.fb_id, 0, 0, 1, &mode) == -ENOSPC ||
		    setcrtc_on(fd, small.fb_id, 0, 0, 1, &mode) == -EINVAL,
		    "SETCRTC a 64x64 framebuffer under a %dx%d mode: error", W, H);
		buf_free(&small);
	}
	/* still showing A after the failures */
	memset(&cr, 0, sizeof cr);
	cr.crtc_id = crtc_id;
	CHECK(ioc(DRM_IOCTL_MODE_GETCRTC, &cr) == 0 && cr.fb_id == fbA.fb_id && cr.mode_valid == 1,
	    "after the failed SETCRTCs the crtc still shows A");

	/* off and on again */
	CHECK(setcrtc_on(fd, 0, 0, 0, 0, NULL) == 0, "SETCRTC off");
	memset(&cr, 0, sizeof cr);
	cr.crtc_id = crtc_id;
	CHECK(ioc(DRM_IOCTL_MODE_GETCRTC, &cr) == 0 && cr.fb_id == 0 && cr.mode_valid == 0, "GETCRTC: off");
	CHECK(setcrtc_on(fd, fbA.fb_id, 0, 0, 1, &mode) == 0, "SETCRTC on again with A");
	(void)i;
}

static void
test_flip(void)
{
	struct drm_event_vblank ev;
	uint32_t last_seq = 0;
	int i, ok, got;
	double t0;

	drain_events(fd);
	CHECK(flip_on(fd, fbB.fb_id, DRM_MODE_PAGE_FLIP_EVENT, 0x1234abcd) == 0, "PAGE_FLIP to B with an event");
	got = read_event(fd, 2000, &ev);
	CHECK(got == 0 && ev.base.type == DRM_EVENT_FLIP_COMPLETE && ev.base.length == sizeof ev &&
	    ev.user_data == 0x1234abcd && ev.crtc_id == crtc_id,
	    "flip event: type %u length %u user_data %llx crtc %u", ev.base.type, ev.base.length,
	    (unsigned long long)ev.user_data, ev.crtc_id);
	last_seq = ev.sequence;
	{
		struct timespec ts;
		double now, evt;

		clock_gettime(CLOCK_MONOTONIC, &ts);
		now = ts.tv_sec + ts.tv_nsec / 1e9;
		evt = ev.tv_sec + ev.tv_usec / 1e6;
		CHECK(evt <= now + 0.001 && now - evt < 2.0, "event time is CLOCK_MONOTONIC (%.3f s ago)", now - evt);
	}
	{
		struct drm_mode_crtc cr;

		memset(&cr, 0, sizeof cr);
		cr.crtc_id = crtc_id;
		CHECK(ioc(DRM_IOCTL_MODE_GETCRTC, &cr) == 0 && cr.fb_id == fbB.fb_id, "GETCRTC shows B after the flip");
	}
	CHECK(flip_on(fd, fbA.fb_id, 0, 0) == 0, "PAGE_FLIP back to A without an event");
	CHECK(read_event(fd, 300, &ev) == -ETIMEDOUT, "no event without the flag");

	/* a run of flips, each waits for its event: sequence numbers and times go forward */
	ok = 1; t0 = now_ms();
	for (i = 0; i < 60; i++) {
		struct drm_event_vblank e;
		int r = flip_on(fd, (i & 1) ? fbA.fb_id : fbB.fb_id, DRM_MODE_PAGE_FLIP_EVENT, (uint64_t)i);

		if (r == -EBUSY) {
			read_event(fd, 100, &e);
			r = flip_on(fd, (i & 1) ? fbA.fb_id : fbB.fb_id, DRM_MODE_PAGE_FLIP_EVENT, (uint64_t)i);
		}
		if (r != 0 || read_event(fd, 1000, &e) != 0 || e.user_data != (uint64_t)i ||
		    (i > 0 && (int32_t)(e.sequence - last_seq) < 0)) {
			ok = 0;
			break;
		}
		last_seq = e.sequence;
	}
	CHECK(ok, "60 flips in a row, event after each, sequence goes forward");
	NOTE("60 flips with events: %.1f ms", now_ms() - t0);
	drain_events(fd);

	CHECK(flip_on(fd, 0xdeadbeef, 0, 0) == -ENOENT, "PAGE_FLIP bad fb: ENOENT");
	CHECK(flip_on(fd, fbA.fb_id, 0x80, 0) == -EINVAL, "PAGE_FLIP unknown flags: EINVAL");
	{
		struct drm_mode_crtc_page_flip pf;

		memset(&pf, 0, sizeof pf);
		pf.crtc_id = 0xdeadbeef; pf.fb_id = fbA.fb_id;
		CHECK(ioc(DRM_IOCTL_MODE_PAGE_FLIP, &pf) == -ENOENT, "PAGE_FLIP bad crtc: ENOENT");
		memset(&pf, 0, sizeof pf);
		pf.crtc_id = crtc_id; pf.fb_id = fbA.fb_id; pf.reserved = 1;
		CHECK(ioc(DRM_IOCTL_MODE_PAGE_FLIP, &pf) == -EINVAL, "PAGE_FLIP reserved field: EINVAL");
	}
	{
		int r = flip_on(fd, fbA.fb_id, DRM_MODE_PAGE_FLIP_ASYNC, 0);

		CHECK(r == -EINVAL, "PAGE_FLIP async (the driver has no async flip): EINVAL (%d)", r);
	}
	{
		struct buf small;
		int r;

		if (buf_new(&small, 64, 64) == 0) {
			r = flip_on(fd, small.fb_id, 0, 0);
			CHECK(r == -EINVAL || r == -ENOSPC, "PAGE_FLIP to a 64x64 framebuffer: EINVAL or ENOSPC (%d)", r);
			buf_free(&small);
		}
	}
	drain_events(fd);
	/* flips with the crtc off */
	CHECK(setcrtc_on(fd, 0, 0, 0, 0, NULL) == 0, "crtc off");
	CHECK(flip_on(fd, fbA.fb_id, 0, 0) != 0, "PAGE_FLIP on a disabled crtc fails");
	CHECK(setcrtc_on(fd, fbA.fb_id, 0, 0, 1, &mode) == 0, "crtc on");
}

static void
test_dirtyfb(void)
{
	struct drm_mode_fb_dirty_cmd d;
	struct drm_clip_rect clips[2];
	int r;

	memset(clips, 0, sizeof clips);
	clips[0].x1 = 8; clips[0].y1 = 8; clips[0].x2 = 72; clips[0].y2 = 40;
	clips[1].x1 = 0; clips[1].y1 = 0; clips[1].x2 = (uint16_t)W; clips[1].y2 = (uint16_t)H;
	memset(&d, 0, sizeof d);
	d.fb_id = fbA.fb_id;
	CHECK(ioc(DRM_IOCTL_MODE_DIRTYFB, &d) == 0, "DIRTYFB without clips (whole frame)");
	d.clips_ptr = PTR(clips); d.num_clips = 1;
	CHECK(ioc(DRM_IOCTL_MODE_DIRTYFB, &d) == 0, "DIRTYFB one clip");
	d.num_clips = 2;
	CHECK(ioc(DRM_IOCTL_MODE_DIRTYFB, &d) == 0, "DIRTYFB two clips");
	d.num_clips = 1000;
	CHECK(ioc(DRM_IOCTL_MODE_DIRTYFB, &d) == -EINVAL, "DIRTYFB 1000 clips: EINVAL");
	d.num_clips = 1; d.clips_ptr = 0;
	CHECK(ioc(DRM_IOCTL_MODE_DIRTYFB, &d) == -EINVAL || ioc(DRM_IOCTL_MODE_DIRTYFB, &d) == -EFAULT,
	    "DIRTYFB a clip without a pointer: error");
	memset(&d, 0, sizeof d);
	d.fb_id = 0xdeadbeef;
	CHECK(ioc(DRM_IOCTL_MODE_DIRTYFB, &d) == -ENOENT, "DIRTYFB bad fb: ENOENT");
	memset(&d, 0, sizeof d);
	d.fb_id = fbA.fb_id; d.flags = 0x80;
	r = ioc(DRM_IOCTL_MODE_DIRTYFB, &d);
	CHECK(r == 0 || r == -EINVAL, "DIRTYFB unknown flags: ignored or EINVAL, no panic (%d)", r);
	/* a clip outside of the framebuffer must not write outside of the screen */
	memset(clips, 0, sizeof clips);
	clips[0].x1 = (uint16_t)(W - 4); clips[0].y1 = (uint16_t)(H - 4);
	clips[0].x2 = (uint16_t)(W + 500); clips[0].y2 = (uint16_t)(H + 500);
	memset(&d, 0, sizeof d);
	d.fb_id = fbA.fb_id; d.clips_ptr = PTR(clips); d.num_clips = 1;
	r = ioc(DRM_IOCTL_MODE_DIRTYFB, &d);
	CHECK(r == 0 || r == -EINVAL, "DIRTYFB a clip past the framebuffer: no panic (%d)", r);
}

static void
test_atomic(void)
{
	struct drm_set_client_cap sc;
	struct drm_mode_crtc cr;
	struct drm_event_vblank ev;
	struct atom a;
	uint32_t blob = 0;
	int fa, r;
	struct propmap ap[32], ac[32], an[32], apl[32];
	int nap, nac, nan, napl;

	fa = open_card();
	CHECK(fa >= 0, "second descriptor for the atomic client");
	if (fa < 0)
		return;
	/* the first descriptor is master: drop it, so that the second one can be */
	CHECK(ioc(DRM_IOCTL_DROP_MASTER, NULL) == 0, "DROP_MASTER on the first descriptor");
	CHECK(ioc_fd(fa, DRM_IOCTL_SET_MASTER, NULL) == 0, "SET_MASTER on the second one");
	memset(&sc, 0, sizeof sc); sc.capability = DRM_CLIENT_CAP_ATOMIC; sc.value = 1;
	r = ioc_fd(fa, DRM_IOCTL_SET_CLIENT_CAP, &sc);
	CHECK(r == 0, "ATOMIC cap implies universal planes (%d)", r);
	memset(&sc, 0, sizeof sc); sc.capability = DRM_CLIENT_CAP_ATOMIC; sc.value = 3;
	CHECK(ioc_fd(fa, DRM_IOCTL_SET_CLIENT_CAP, &sc) == -EINVAL, "ATOMIC cap value 3: EINVAL");
	napl = get_props(fa, plane_id, DRM_MODE_OBJECT_PLANE, apl, 32);
	nac = get_props(fa, crtc_id, DRM_MODE_OBJECT_CRTC, ac, 32);
	nan = get_props(fa, conn_id, DRM_MODE_OBJECT_CONNECTOR, an, 32);
	nap = 0; (void)ap; (void)nap;
	CHECK(napl > 0 && nac > 0 && nan > 0, "atomic descriptor reads properties: plane %d crtc %d connector %d", napl, nac, nan);
	{
		static const char *pn[] = { "type", "FB_ID", "CRTC_ID", "SRC_X", "SRC_Y", "SRC_W", "SRC_H",
		    "CRTC_X", "CRTC_Y", "CRTC_W", "CRTC_H", NULL };
		int ok = 1, k;

		for (k = 0; pn[k]; k++)
			ok &= prop_id(apl, napl, pn[k]) != 0;
		CHECK(ok, "atomic view: plane has type FB_ID CRTC_ID SRC_* CRTC_*");
		CHECK(prop_id(ac, nac, "ACTIVE") != 0 && prop_id(ac, nac, "MODE_ID") != 0,
		    "atomic view: crtc has ACTIVE and MODE_ID");
		CHECK(prop_id(an, nan, "CRTC_ID") != 0 && prop_id(an, nan, "DPMS") != 0,
		    "atomic view: connector has CRTC_ID and DPMS");
	}
	memcpy(ppl, apl, sizeof ppl); nppl = napl;
	memcpy(pcrtc, ac, sizeof pcrtc); npcrtc = nac;
	memcpy(pconn, an, sizeof pconn); npconn = nan;

	CHECK(mkblob(fa, &mode, sizeof mode, &blob) == 0, "CREATEPROPBLOB with the mode");
	/* a blob of the wrong size is refused where it is used, not at creation */
	drain_events(fa);

	memset(&a, 0, sizeof a);
	atom_modeset(&a, fbA.fb_id, blob);
	CHECK(atom_commit(fa, &a, DRM_MODE_ATOMIC_TEST_ONLY | DRM_MODE_ATOMIC_ALLOW_MODESET, 0) == 0,
	    "atomic TEST_ONLY modeset");
	/* the crtc is on with this mode already: nothing changes, so no modeset is needed */
	CHECK(atom_commit(fa, &a, 0, 0) == 0, "atomic commit of the unchanged mode without ALLOW_MODESET: ok");
	CHECK(atom_commit(fa, &a, DRM_MODE_ATOMIC_ALLOW_MODESET | DRM_MODE_PAGE_FLIP_EVENT | DRM_MODE_ATOMIC_NONBLOCK, 0xa11ce) == 0,
	    "atomic modeset commit with event, non-blocking");
	r = read_event(fa, 2000, &ev);
	CHECK(r == 0 && ev.base.type == DRM_EVENT_FLIP_COMPLETE && ev.user_data == 0xa11ce && ev.crtc_id == crtc_id,
	    "atomic commit event: type %u user_data %llx crtc %u", ev.base.type, (unsigned long long)ev.user_data, ev.crtc_id);

	memset(&a, 0, sizeof a);
	atom_flip(&a, fbB.fb_id);
	CHECK(atom_commit(fa, &a, DRM_MODE_ATOMIC_TEST_ONLY, 0) == 0, "atomic TEST_ONLY flip");
	CHECK(atom_commit(fa, &a, DRM_MODE_ATOMIC_TEST_ONLY | DRM_MODE_PAGE_FLIP_EVENT, 0) == -EINVAL,
	    "atomic TEST_ONLY with the event flag: EINVAL");
	CHECK(atom_commit(fa, &a, DRM_MODE_PAGE_FLIP_EVENT, 77) == 0, "atomic flip to B, blocking, with event");
	r = read_event(fa, 2000, &ev);
	CHECK(r == 0 && ev.user_data == 77, "atomic flip event arrives (user_data %llu)", (unsigned long long)ev.user_data);
	memset(&cr, 0, sizeof cr);
	cr.crtc_id = crtc_id;
	CHECK(ioc_fd(fa, DRM_IOCTL_MODE_GETCRTC, &cr) == 0 && cr.fb_id == fbB.fb_id, "GETCRTC shows B after the atomic flip");

	/* errors */
	memset(&a, 0, sizeof a);
	atom_add(&a, plane_id, 0xdeadbeef, 1);
	r = atom_commit(fa, &a, 0, 0);
	CHECK(r == -ENOENT || r == -EINVAL, "atomic unknown property id: error (%d)", r);
	memset(&a, 0, sizeof a);
	atom_add(&a, 0xdeadbeef, prop_id(ppl, nppl, "FB_ID"), fbA.fb_id);
	r = atom_commit(fa, &a, 0, 0);
	CHECK(r == -ENOENT, "atomic unknown object id: ENOENT (%d)", r);
	memset(&a, 0, sizeof a);
	atom_flip(&a, fbA.fb_id);
	CHECK(atom_commit(fa, &a, 0x10000000, 0) == -EINVAL, "atomic unknown flags: EINVAL");
	CHECK(atom_commit(fa, &a, DRM_MODE_PAGE_FLIP_ASYNC, 0) == -EINVAL, "atomic async flip: EINVAL");
	memset(&a, 0, sizeof a);
	atom_add(&a, plane_id, prop_id(ppl, nppl, "FB_ID"), 0xdeadbeef);
	r = atom_commit(fa, &a, 0, 0);
	CHECK(r == -ENOENT || r == -EINVAL, "atomic bad FB_ID: error (%d)", r);
	{
		struct buf small;

		/* the buffers belong to the first descriptor: make one on the second */
		struct drm_mode_create_dumb cd;
		uint32_t fb;

		if (mkdumb_on(fa, 64, 64, 32, 0, &cd) == 0 &&
		    addfb2_on(fa, 64, 64, DRM_FORMAT_XRGB8888_FOURCC, cd.handle, cd.pitch, 0, 0, &fb) == 0) {
			memset(&a, 0, sizeof a);
			atom_flip(&a, fb);
			r = atom_commit(fa, &a, 0, 0);
			CHECK(r == -EINVAL || r == -ENOSPC, "atomic flip to a 64x64 framebuffer: error (%d)", r);
			rmfb_on(fa, fb);
			destroy_dumb_on(fa, cd.handle);
		}
		(void)small;
	}
	{
		struct drm_mode_atomic at;
		uint32_t o = plane_id, cnt = 1, p = prop_id(ppl, nppl, "FB_ID");
		uint64_t v = fbA.fb_id;

		memset(&at, 0, sizeof at);
		at.count_objs = 1; at.objs_ptr = PTR(&o); at.count_props_ptr = PTR(&cnt);
		at.props_ptr = PTR(&p); at.prop_values_ptr = 0;
		CHECK(ioc_fd(fa, DRM_IOCTL_MODE_ATOMIC, &at) != 0, "atomic with a NULL values pointer: error");
		at.prop_values_ptr = PTR(&v);
		at.reserved = 1;
		CHECK(ioc_fd(fa, DRM_IOCTL_MODE_ATOMIC, &at) == -EINVAL, "atomic reserved field: EINVAL");
		memset(&at, 0, sizeof at);
		at.count_objs = 0x7fffffff;
		CHECK(ioc_fd(fa, DRM_IOCTL_MODE_ATOMIC, &at) != 0, "atomic with 2G objects: error");
		memset(&at, 0, sizeof at);
		CHECK(ioc_fd(fa, DRM_IOCTL_MODE_ATOMIC, &at) == 0, "atomic with no objects: nothing to do");
	}
	/* CRTC active without a mode */
	memset(&a, 0, sizeof a);
	atom_add(&a, crtc_id, prop_id(pcrtc, npcrtc, "MODE_ID"), 0);
	atom_add(&a, crtc_id, prop_id(pcrtc, npcrtc, "ACTIVE"), 1);
	r = atom_commit(fa, &a, DRM_MODE_ATOMIC_ALLOW_MODESET, 0);
	CHECK(r == -EINVAL, "atomic active crtc without a mode: EINVAL (%d)", r);
	/* a mode with another size */
	{
		struct drm_mode_modeinfo bad = mode;
		uint32_t b2;

		bad.hdisplay = (uint16_t)(W - 8);
		if (mkblob(fa, &bad, sizeof bad, &b2) == 0) {
			memset(&a, 0, sizeof a);
			atom_modeset(&a, fbA.fb_id, b2);
			r = atom_commit(fa, &a, DRM_MODE_ATOMIC_ALLOW_MODESET | DRM_MODE_ATOMIC_TEST_ONLY, 0);
			CHECK(r != 0, "atomic modeset with another mode size: error (%d)", r);
		}
	}
	{
		uint32_t shortblob = 0;
		char junk[8] = { 0 };

		if (mkblob(fa, junk, sizeof junk, &shortblob) == 0) {
			memset(&a, 0, sizeof a);
			atom_modeset(&a, fbA.fb_id, shortblob);
			r = atom_commit(fa, &a, DRM_MODE_ATOMIC_ALLOW_MODESET | DRM_MODE_ATOMIC_TEST_ONLY, 0);
			CHECK(r == -EINVAL, "atomic modeset with an 8 byte mode blob: EINVAL (%d)", r);
		}
	}
	/* off by atomic, on again */
	memset(&a, 0, sizeof a);
	atom_add(&a, conn_id, prop_id(pconn, npconn, "CRTC_ID"), 0);
	atom_add(&a, crtc_id, prop_id(pcrtc, npcrtc, "ACTIVE"), 0);
	atom_add(&a, crtc_id, prop_id(pcrtc, npcrtc, "MODE_ID"), 0);
	atom_add(&a, plane_id, prop_id(ppl, nppl, "FB_ID"), 0);
	atom_add(&a, plane_id, prop_id(ppl, nppl, "CRTC_ID"), 0);
	CHECK(atom_commit(fa, &a, DRM_MODE_ATOMIC_ALLOW_MODESET, 0) == 0, "atomic: everything off");
	memset(&cr, 0, sizeof cr);
	cr.crtc_id = crtc_id;
	CHECK(ioc_fd(fa, DRM_IOCTL_MODE_GETCRTC, &cr) == 0 && cr.mode_valid == 0 && cr.fb_id == 0, "GETCRTC: off after the atomic commit");
	memset(&a, 0, sizeof a);
	atom_modeset(&a, fbA.fb_id, blob);
	CHECK(atom_commit(fa, &a, 0, 0) == -EINVAL, "atomic modeset from off without ALLOW_MODESET: EINVAL");
	CHECK(atom_commit(fa, &a, DRM_MODE_ATOMIC_ALLOW_MODESET, 0) == 0, "atomic: on again with A");
	/* the last flip of the atomic client shows B; leave A */
	drain_events(fa);
	/* give the master back to the main descriptor */
	CHECK(ioc_fd(fa, DRM_IOCTL_DROP_MASTER, NULL) == 0, "DROP_MASTER on the second descriptor");
	CHECK(ioc(DRM_IOCTL_SET_MASTER, NULL) == 0, "SET_MASTER on the first descriptor again");
	close(fa);
	/* the legacy descriptor still works after the atomic client is gone */
	CHECK(flip_on(fd, fbB.fb_id, DRM_MODE_PAGE_FLIP_EVENT, 5) == 0, "legacy flip after the atomic client closed");
	CHECK(read_event(fd, 2000, &ev) == 0 && ev.user_data == 5, "its event");
	CHECK(flip_on(fd, fbA.fb_id, 0, 0) == 0, "legacy flip back to A");
	(void)blob;
}

static void
test_vblank(void)
{
	union drm_wait_vblank vbl;
	struct drm_crtc_get_sequence gs;
	struct drm_event_vblank ev;
	double t[6];
	uint32_t seq0;
	int i, r, ok;

	memset(&vbl, 0, sizeof vbl);
	vbl.request.type = _DRM_VBLANK_RELATIVE;
	vbl.request.sequence = 1;
	r = ioc(DRM_IOCTL_WAIT_VBLANK, &vbl);
	if (r != 0) {
		CHECK(r == -EINVAL || r == -EOPNOTSUPP || r == -ENOTSUP || r == -ENODEV,
		    "WAIT_VBLANK is not supported and says so (errno %d)", -r);
		NOTE("WAIT_VBLANK error %d: the driver has no vblank wait", -r);
		return;
	}
	CHECK(r == 0 && vbl.reply.tval_sec > 0, "WAIT_VBLANK relative 1 returns (seq %u)", vbl.reply.sequence);
	seq0 = vbl.reply.sequence;
	ok = 1;
	for (i = 0; i < 6; i++) {
		memset(&vbl, 0, sizeof vbl);
		vbl.request.type = _DRM_VBLANK_RELATIVE;
		vbl.request.sequence = 1;
		t[i] = now_ms();
		if (ioc(DRM_IOCTL_WAIT_VBLANK, &vbl) != 0) {
			ok = 0;
			break;
		}
		if ((int32_t)(vbl.reply.sequence - seq0) <= 0)
			ok = 0;
		seq0 = vbl.reply.sequence;
	}
	CHECK(ok, "6 vblank waits in a row: the sequence grows");
	if (ok) {
		double iv = (t[5] - t[0]) / 5;

		NOTE("vblank interval %.2f ms (%.1f Hz)", iv, 1000.0 / iv);
		CHECK(iv > 5.0 && iv < 60.0, "vblank interval between 5 and 60 ms");
	}
	/* absolute target in the past returns at once, in the future waits */
	memset(&vbl, 0, sizeof vbl);
	vbl.request.type = _DRM_VBLANK_ABSOLUTE;
	vbl.request.sequence = seq0 - 5;
	t[0] = now_ms();
	r = ioc(DRM_IOCTL_WAIT_VBLANK, &vbl);
	CHECK(r == 0 && now_ms() - t[0] < 30, "WAIT_VBLANK absolute in the past returns at once");
	memset(&vbl, 0, sizeof vbl);
	vbl.request.type = _DRM_VBLANK_ABSOLUTE;
	vbl.request.sequence = seq0 + 3 + 1;
	t[0] = now_ms();
	r = ioc(DRM_IOCTL_WAIT_VBLANK, &vbl);
	CHECK(r == 0 && now_ms() - t[0] > 20, "WAIT_VBLANK absolute 4 ahead waits (%.1f ms)", now_ms() - t[0]);
	/* secondary crtc: no such pipe */
	memset(&vbl, 0, sizeof vbl);
	vbl.request.type = _DRM_VBLANK_RELATIVE | _DRM_VBLANK_SECONDARY;
	vbl.request.sequence = 1;
	CHECK(ioc(DRM_IOCTL_WAIT_VBLANK, &vbl) != 0, "WAIT_VBLANK on a second pipe: error");
	memset(&vbl, 0, sizeof vbl);
	vbl.request.type = 0x00800000;	/* unknown flag bits */
	CHECK(ioc(DRM_IOCTL_WAIT_VBLANK, &vbl) == -EINVAL, "WAIT_VBLANK unknown type bits: EINVAL");
	/* the event of a vblank */
	drain_events(fd);
	memset(&vbl, 0, sizeof vbl);
	vbl.request.type = _DRM_VBLANK_RELATIVE | _DRM_VBLANK_EVENT;
	vbl.request.sequence = 2;
	vbl.request.signal = 0xbeef;
	r = ioc(DRM_IOCTL_WAIT_VBLANK, &vbl);
	CHECK(r == 0, "WAIT_VBLANK with the event flag queues an event");
	r = read_event(fd, 1000, &ev);
	CHECK(r == 0 && ev.base.type == DRM_EVENT_VBLANK && ev.user_data == 0xbeef,
	    "vblank event: type %u user_data %llx", ev.base.type, (unsigned long long)ev.user_data);

	memset(&gs, 0, sizeof gs);
	gs.crtc_id = crtc_id;
	r = ioc(DRM_IOCTL_CRTC_GET_SEQUENCE, &gs);
	CHECK(r == 0 && gs.active == 1 && gs.sequence_ns > 0,
	    "CRTC_GET_SEQUENCE: active %u sequence %llu (%d)", gs.active, (unsigned long long)gs.sequence, r);
	memset(&gs, 0, sizeof gs);
	gs.crtc_id = 0xdeadbeef;
	CHECK(ioc(DRM_IOCTL_CRTC_GET_SEQUENCE, &gs) == -ENOENT, "CRTC_GET_SEQUENCE bad crtc: ENOENT");
	/* the crtc off: a wait is an error, not a hang */
	CHECK(setcrtc_on(fd, 0, 0, 0, 0, NULL) == 0, "crtc off for the vblank check");
	memset(&vbl, 0, sizeof vbl);
	vbl.request.type = _DRM_VBLANK_RELATIVE;
	vbl.request.sequence = 1;
	t[0] = now_ms();
	r = ioc(DRM_IOCTL_WAIT_VBLANK, &vbl);
	CHECK(r != 0 && now_ms() - t[0] < 500, "WAIT_VBLANK on a disabled crtc: error at once (%d)", r);
	CHECK(setcrtc_on(fd, fbA.fb_id, 0, 0, 1, &mode) == 0, "crtc on again");
	drain_events(fd);
}

static void
sigalrm_nop(int sig)
{

	(void)sig;
}

/*
 * A blocking read of events on an empty queue: a signal ends it (EINTR) and
 * SIGKILL ends the process that sleeps in it.
 */
static void
test_read_signal(void)
{
	struct sigaction sa;
	struct itimerval it;
	struct drm_event_vblank ev;
	ssize_t n;
	int f, e, i, st = 0, died = 0;
	pid_t p;

	f = open_card_mode(0);
	CHECK(f >= 0, "blocking descriptor for the signal checks");
	if (f < 0)
		return;
	drain_events(f);
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = sigalrm_nop;		/* no SA_RESTART */
	sigaction(SIGALRM, &sa, NULL);
	memset(&it, 0, sizeof it);
	it.it_value.tv_usec = 200000;
	setitimer(ITIMER_REAL, &it, NULL);
	n = read(f, &ev, sizeof ev);
	e = errno;
	memset(&it, 0, sizeof it);
	setitimer(ITIMER_REAL, &it, NULL);
	CHECK(n < 0 && e == EINTR, "a blocking read of events is ended by a signal: EINTR (n %zd errno %d)", n, e);
	close(f);

	p = fork();
	if (p == 0) {
		f = open_card_mode(0);
		if (f < 0)
			_exit(2);
		for (;;)
			(void)read(f, &ev, sizeof ev);
	}
	CHECK(p > 0, "child for the SIGKILL check");
	if (p > 0) {
		usleep(300000);
		kill(p, SIGKILL);
		for (i = 0; i < 60 && !died; i++) {
			if (waitpid(p, &st, WNOHANG) == p)
				died = 1;
			else
				usleep(50000);
		}
		CHECK(died && WIFSIGNALED(st) && WTERMSIG(st) == SIGKILL,
		    "SIGKILL ends a process sleeping in a blocking read of events");
	}
}

static void
test_prime(void)
{
	struct drm_prime_handle ph, ph2;
	struct drm_mode_create_dumb cd;
	int r;
	void *m;

	if (mkdumb_on(fd, 64, 64, 32, 0, &cd) != 0) {
		CHECK(0, "dumb for PRIME");
		return;
	}
	memset(&ph, 0, sizeof ph);
	ph.handle = cd.handle; ph.flags = DRM_CLOEXEC | DRM_RDWR;
	r = ioc(DRM_IOCTL_PRIME_HANDLE_TO_FD, &ph);
	CHECK(r == 0 && ph.fd >= 0, "PRIME handle to fd");
	if (r == 0) {
		memset(&ph2, 0, sizeof ph2);
		ph2.fd = ph.fd;
		r = ioc(DRM_IOCTL_PRIME_FD_TO_HANDLE, &ph2);
		CHECK(r == 0 && ph2.handle == cd.handle, "PRIME fd to handle gives the same object (handle %u)", ph2.handle);
		m = mmap(NULL, cd.size, PROT_READ | PROT_WRITE, MAP_SHARED, ph.fd, 0);
		CHECK(m != MAP_FAILED, "mmap of the dma-buf");
		if (m != MAP_FAILED)
			munmap(m, cd.size);
		close(ph.fd);
	}
	memset(&ph, 0, sizeof ph);
	ph.handle = 0xdeadbeef;
	CHECK(ioc(DRM_IOCTL_PRIME_HANDLE_TO_FD, &ph) != 0, "PRIME of a bad handle fails");
	destroy_dumb_on(fd, cd.handle);
}

/* ------------------------------------------------------------- stress */

static volatile int stop_all;
static int g_flips;			/* flips per thread */
static volatile long n_ok, n_busy, n_events, n_other, n_dirty, n_atomic, n_bad;
static pthread_mutex_t cnt_mu = PTHREAD_MUTEX_INITIALIZER;

#define ADD(v, n) do { pthread_mutex_lock(&cnt_mu); (v) += (n); pthread_mutex_unlock(&cnt_mu); } while (0)

static void
reap_events(int tmo)
{
	struct drm_event_vblank ev;
	int n = 0;

	while (read_event(fd, n == 0 ? tmo : 0, &ev) == 0) {
		if (ev.base.type == DRM_EVENT_FLIP_COMPLETE)
			ADD(n_events, 1);
		n++;
	}
}

static void *
flip_thread(void *arg)
{
	long id = (long)arg;
	int i;

	for (i = 0; i < g_flips && !stop_all; i++) {
		uint32_t fb = ((i + id) & 1) ? fbA.fb_id : fbB.fb_id;
		int r = flip_on(fd, fb, DRM_MODE_PAGE_FLIP_EVENT, (uint64_t)id);

		if (r == 0)
			ADD(n_ok, 1);
		else if (r == -EBUSY) {
			ADD(n_busy, 1);
			reap_events(20);
			i--;		/* try again: the count is of successful flips */
		} else {
			ADD(n_other, 1);
			break;
		}
		if ((i & 15) == 0)
			reap_events(0);
	}
	return NULL;
}

static void *
dirty_thread(void *arg)
{
	struct drm_mode_fb_dirty_cmd d;
	struct drm_clip_rect c = { 10, 10, 200, 100 };
	int i;

	(void)arg;
	for (i = 0; i < g_flips && !stop_all; i++) {
		memset(&d, 0, sizeof d);
		d.fb_id = (i & 1) ? fbA.fb_id : fbB.fb_id;
		d.clips_ptr = PTR(&c); d.num_clips = 1;
		if (ioc(DRM_IOCTL_MODE_DIRTYFB, &d) == 0)
			ADD(n_dirty, 1);
		else
			ADD(n_bad, 1);
	}
	return NULL;
}

static void *
getter_thread(void *arg)
{
	struct drm_mode_crtc cr;
	int i;

	(void)arg;
	for (i = 0; i < g_flips && !stop_all; i++) {
		memset(&cr, 0, sizeof cr);
		cr.crtc_id = crtc_id;
		if (ioc(DRM_IOCTL_MODE_GETCRTC, &cr) != 0)
			ADD(n_bad, 1);
	}
	return NULL;
}

static void
test_stress(int nthreads)
{
	pthread_t th[16];
	int i, nt = 0;
	double t0;
	long want;

	drain_events(fd);
	n_ok = n_busy = n_events = n_other = n_dirty = n_atomic = n_bad = 0;
	stop_all = 0;
	t0 = now_ms();
	for (i = 0; i < nthreads && nt < 12; i++)
		if (pthread_create(&th[nt], NULL, flip_thread, (void *)(long)i) == 0)
			nt++;
	if (pthread_create(&th[nt], NULL, dirty_thread, NULL) == 0)
		nt++;
	if (pthread_create(&th[nt], NULL, getter_thread, NULL) == 0)
		nt++;
	for (i = 0; i < nt; i++)
		pthread_join(th[i], NULL);
	want = n_ok;
	/* every successful flip owes one event */
	for (i = 0; i < 50 && n_events < want; i++)
		reap_events(100);
	NOTE("stress: %d threads, %ld flips ok, %ld busy retries, %ld events, %ld dirty, %.0f ms (%.0f flips/s)",
	    nthreads, n_ok, n_busy, n_events, n_dirty, now_ms() - t0, n_ok * 1000.0 / (now_ms() - t0));
	CHECK(n_other == 0, "stress: no flip failed other than EBUSY (%ld)", n_other);
	CHECK(n_ok == (long)g_flips * nthreads, "stress: all %ld flips done", (long)g_flips * nthreads);
	CHECK(n_events == want, "stress: events %ld = successful flips %ld", n_events, want);
	CHECK(n_bad == 0, "stress: no DIRTYFB or GETCRTC failed (%ld)", n_bad);
	CHECK(n_dirty == g_flips, "stress: %d DIRTYFB calls done (%ld)", g_flips, n_dirty);
	CHECK(setcrtc_on(fd, fbA.fb_id, 0, 0, 1, &mode) == 0, "SETCRTC works after the stress");
	CHECK(flip_on(fd, fbB.fb_id, DRM_MODE_PAGE_FLIP_EVENT, 9) == 0, "flip works after the stress");
	{
		struct drm_event_vblank ev;

		CHECK(read_event(fd, 2000, &ev) == 0 && ev.user_data == 9, "its event");
	}
	CHECK(flip_on(fd, fbA.fb_id, 0, 0) == 0, "flip back to A");
}

static void
test_churn(void)
{
	int i, ok = 1, f;
	struct drm_mode_create_dumb cd;
	uint32_t fb;

	for (i = 0; i < 300; i++) {
		f = open_card();
		if (f < 0) {
			ok = 0;
			break;
		}
		/* leave with a buffer, a framebuffer and a mapping that are never released */
		if (mkdumb_on(f, 128, 128, 32, 0, &cd) == 0) {
			void *m = map_dumb_on(f, cd.handle, cd.size);

			if (m)
				*(volatile uint32_t *)m = (uint32_t)i;
			if (addfb2_on(f, 128, 128, DRM_FORMAT_XRGB8888_FOURCC, cd.handle, cd.pitch, 0, 0, &fb) != 0)
				ok = 0;
			if ((i & 3) == 0 && m)
				munmap(m, cd.size);
		} else
			ok = 0;
		close(f);
	}
	CHECK(ok, "300 times: open, buffer, framebuffer, mapping, close without cleanup");
	CHECK(mkdumb_on(fd, 64, 64, 32, 0, &cd) == 0 && destroy_dumb_on(fd, cd.handle) == 0,
	    "the device still allocates after the churn");
	{
		struct drm_event_vblank ev;

		CHECK(flip_on(fd, fbB.fb_id, DRM_MODE_PAGE_FLIP_EVENT, 21) == 0 && read_event(fd, 2000, &ev) == 0 &&
		    flip_on(fd, fbA.fb_id, DRM_MODE_PAGE_FLIP_EVENT, 22) == 0 && read_event(fd, 2000, &ev) == 0 &&
		    ev.user_data == 22, "and flips, with events");
	}
}

static void
kill_child_on_fd(int rounds)
{
	int i, bad = 0, died = 0;

	for (i = 0; i < rounds; i++) {
		pid_t p = fork();

		if (p == 0) {
			int j;

			/* the child flips on the inherited descriptor until it is killed */
			for (j = 0; j < 100000; j++) {
				if (flip_on(fd, (j & 1) ? fbA.fb_id : fbB.fb_id, DRM_MODE_PAGE_FLIP_EVENT, 1) != 0)
					usleep(200);
				if (j % 3 == 0)
					drain_events(fd);
			}
			_exit(0);
		}
		if (p < 0) {
			bad++;
			continue;
		}
		usleep(2000 + (rand() % 40000));
		kill(p, SIGKILL);
		{
			int st;

			waitpid(p, &st, 0);
			if (WIFSIGNALED(st) && WTERMSIG(st) == SIGKILL)
				died++;
		}
		drain_events(fd);
		usleep(20000);
		if (flip_on(fd, fbA.fb_id, DRM_MODE_PAGE_FLIP_EVENT, 3) != 0) {
			/* a flip of the killed child may still be pending */
			usleep(100000);
			if (flip_on(fd, fbA.fb_id, DRM_MODE_PAGE_FLIP_EVENT, 3) != 0)
				bad++;
		}
		{
			struct drm_event_vblank ev;

			if (read_event(fd, 2000, &ev) != 0)
				bad++;
		}
	}
	CHECK(bad == 0, "%d times: child killed with SIGKILL during flips on the shared descriptor, the parent flips on (%d killed)", rounds, died);
}

static void
kill_child_own_fd(int rounds)
{
	int i, bad = 0, skipped = 0, died = 0;

	if (ioc(DRM_IOCTL_DROP_MASTER, NULL) != 0) {
		CHECK(0, "DROP_MASTER before the killed children");
		return;
	}
	for (i = 0; i < rounds; i++) {
		pid_t p = fork();

		if (p == 0) {
			int f = open_card(), j;
			struct drm_mode_create_dumb cd;
			uint32_t fb1, fb2;

			if (f < 0)
				_exit(2);
			if (ioc_fd(f, DRM_IOCTL_SET_MASTER, NULL) != 0)
				_exit(3);
			if (mkdumb_on(f, W, H, 32, 0, &cd) != 0 ||
			    addfb2_on(f, W, H, DRM_FORMAT_XRGB8888_FOURCC, cd.handle, cd.pitch, 0, 0, &fb1) != 0)
				_exit(4);
			fb2 = fb1;
			/* a mapping with writes in flight, a mode set, flips with events */
			{
				struct drm_mode_crtc c;
				uint32_t conns[1] = { conn_id };
				void *m = map_dumb_on(f, cd.handle, cd.size);

				memset(&c, 0, sizeof c);
				c.crtc_id = crtc_id; c.fb_id = fb1; c.mode = mode; c.mode_valid = 1;
				c.set_connectors_ptr = PTR(conns); c.count_connectors = 1;
				if (ioc_fd(f, DRM_IOCTL_MODE_SETCRTC, &c) != 0)
					_exit(5);
				for (j = 0; j < 1000000; j++) {
					struct drm_mode_crtc_page_flip pf;

					if (m)
						((volatile uint32_t *)m)[j % 1024] = (uint32_t)j;
					memset(&pf, 0, sizeof pf);
					pf.crtc_id = crtc_id; pf.fb_id = fb2; pf.flags = DRM_MODE_PAGE_FLIP_EVENT;
					if (ioctl(f, DRM_IOCTL_MODE_PAGE_FLIP, &pf) != 0)
						usleep(100);
					if (j % 4 == 0)
						drain_events(f);
				}
			}
			_exit(0);
		}
		if (p < 0) {
			bad++;
			continue;
		}
		usleep(30000 + (rand() % 60000));
		kill(p, SIGKILL);
		{
			int st;

			waitpid(p, &st, 0);
			if (WIFSIGNALED(st) && WTERMSIG(st) == SIGKILL)
				died++;
			else if (WIFEXITED(st) && WEXITSTATUS(st) == 3)
				skipped++;
			else
				bad++;
		}
		usleep(50000);
	}
	CHECK(bad == 0, "%d times: child with its own descriptor killed by SIGKILL during flips (%d killed, %d could not become master)",
	    rounds, died, skipped);
	CHECK(ioc(DRM_IOCTL_SET_MASTER, NULL) == 0, "SET_MASTER again after the children");
	CHECK(setcrtc_on(fd, fbA.fb_id, 0, 0, 1, &mode) == 0, "SETCRTC works after the killed children");
	CHECK(flip_on(fd, fbB.fb_id, DRM_MODE_PAGE_FLIP_EVENT, 11) == 0, "flip works after the killed children");
	{
		struct drm_event_vblank ev;

		CHECK(read_event(fd, 2000, &ev) == 0 && ev.user_data == 11, "its event");
	}
	CHECK(flip_on(fd, fbA.fb_id, 0, 0) == 0, "flip back to A");
}

/* ------------------------------------------------------------- shot */

static int
touch(const char *name)
{
	char path[512];
	int f;

	snprintf(path, sizeof path, "%s/%s", shot_dir, name);
	f = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (f < 0)
		return -1;
	close(f);
	return 0;
}

static int
wait_file(const char *name, int seconds)
{
	char path[512];
	struct stat st;
	int i;

	snprintf(path, sizeof path, "%s/%s", shot_dir, name);
	for (i = 0; i < seconds * 20; i++) {
		if (stat(path, &st) == 0)
			return 0;
		usleep(50000);
	}
	return -1;
}

static int
shot_mode(void)
{
	struct drm_event_vblank ev;
	struct atom a;
	uint32_t blob;

	CHECK(buf_new(&fbA, W, H) == 0 && buf_new(&fbB, W, H) == 0, "two %dx%d buffers for the screen comparison", W, H);
	pattern_fill(fbA.map, fbA.pitch, 0, W, H);
	pattern_fill(fbB.map, fbB.pitch, 1, W, H);
	CHECK(setcrtc_on(fd, fbA.fb_id, 0, 0, 1, &mode) == 0, "SETCRTC pattern 0");
	printf("SHOT %dx%d\n", W, H); fflush(stdout);
	touch("ready0");
	CHECK(wait_file("go0", 120) == 0, "host took the snapshot of pattern 0");
	CHECK(flip_on(fd, fbB.fb_id, DRM_MODE_PAGE_FLIP_EVENT, 1) == 0 && read_event(fd, 2000, &ev) == 0,
	    "PAGE_FLIP to pattern 1, event");
	touch("ready1");
	CHECK(wait_file("go1", 120) == 0, "host took the snapshot of pattern 1");
	{
		struct drm_set_client_cap sc;
		int fa;

		/* back to pattern 0 by an atomic commit, on this descriptor */
		memset(&sc, 0, sizeof sc); sc.capability = DRM_CLIENT_CAP_UNIVERSAL_PLANES; sc.value = 1;
		ioc(DRM_IOCTL_SET_CLIENT_CAP, &sc);
		sc.capability = DRM_CLIENT_CAP_ATOMIC;
		CHECK(ioc(DRM_IOCTL_SET_CLIENT_CAP, &sc) == 0, "ATOMIC cap");
		nppl = get_props(fd, plane_id, DRM_MODE_OBJECT_PLANE, ppl, 32);
		npcrtc = get_props(fd, crtc_id, DRM_MODE_OBJECT_CRTC, pcrtc, 32);
		npconn = get_props(fd, conn_id, DRM_MODE_OBJECT_CONNECTOR, pconn, 32);
		fa = fd;
		CHECK(mkblob(fa, &mode, sizeof mode, &blob) == 0, "mode blob");
		memset(&a, 0, sizeof a);
		atom_modeset(&a, fbA.fb_id, blob);
		CHECK(atom_commit(fa, &a, DRM_MODE_ATOMIC_ALLOW_MODESET | DRM_MODE_PAGE_FLIP_EVENT, 2) == 0 &&
		    read_event(fd, 2000, &ev) == 0 && ev.user_data == 2, "atomic commit back to pattern 0, event");
	}
	touch("ready2");
	CHECK(wait_file("go2", 120) == 0, "host took the snapshot after the atomic commit");
	return 0;
}

int
main(int argc, char **argv)
{
	int ch, nthreads = 4, kills = 6, shot = 0;

	g_flips = 2000;
	while ((ch = getopt(argc, argv, "s:t:k:m:d:q")) != -1) {
		switch (ch) {
		case 's': g_flips = atoi(optarg); break;
		case 't': nthreads = atoi(optarg); break;
		case 'k': kills = atoi(optarg); break;
		case 'm': shot = strcmp(optarg, "shot") == 0; break;
		case 'd': shot_dir = optarg; break;
		case 'q': break;
		default:
			fprintf(stderr, "usage: %s [-s flips] [-t threads] [-k kills] [-m shot -d dir] [-q]\n", argv[0]);
			return 2;
		}
	}
	if (nthreads < 1) nthreads = 1;
	if (nthreads > 12) nthreads = 12;
	fd = open_card();
	if (fd < 0) {
		printf("FAIL no efidrm device in /dev/dri\n");
		return 1;
	}
	test_basics();
	test_resources();
	if (W == 0 || H == 0) {
		printf("FAIL no mode: the rest is skipped\n");
		return 1;
	}
	if (shot) {
		if (shot_dir == NULL) {
			fprintf(stderr, "-m shot needs -d dir\n");
			return 2;
		}
		shot_mode();
		printf("%d checks, %d failed\n", nchecks, nfail);
		return nfail != 0;
	}
	test_dumb();
	test_addfb();
	test_setcrtc();
	if (fbA.map && fbB.map) {
		test_flip();
		test_dirtyfb();
		test_atomic();
		test_vblank();
		test_prime();
		test_read_signal();
		test_stress(nthreads);
		test_churn();
		kill_child_on_fd(kills);
		kill_child_own_fd(kills);
	}
	buf_free(&fbA);
	buf_free(&fbB);
	printf("%d checks, %d failed\n", nchecks, nfail);
	return nfail != 0;
}
