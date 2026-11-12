/* SPDX-License-Identifier: BSD-2-Clause */
/* efidrm: a minimal DRM/KMS driver over the UEFI GOP framebuffer. */
/*
 * efidrm: KMS on top of the linear framebuffer that the EFI boot loader
 * hands to the kernel in btinfo_framebuffer.
 *
 *  - loadable module; the console stays with genfb/wsdisplay, efidrm is a
 *    pseudo-device that only publishes /dev/dri/card0 (+ renderD128);
 *  - one primary plane (XRGB8888, ARGB8888 as an alias, alpha is ignored),
 *    one CRTC, one virtual encoder, one eDP connector, one fixed mode that
 *    equals the GOP mode;
 *  - dumb buffers live in kernel memory (drm_gem_shmem on UVM anon pages),
 *    atomic_update copies the damaged rectangle into the GOP buffer;
 *  - the vblank is emulated by a callout (about 50 Hz).
 *
 * No hardware acceleration, no fbdev/drmfb emulation, no hot unload.
 */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/module.h>
#include <sys/callout.h>
#include <sys/kmem.h>
#include <sys/bus.h>
#include <sys/sysctl.h>
#include <sys/atomic.h>
#include <sys/time.h>

#include <prop/proplib.h>

#include <machine/bootinfo.h>

#include <linux/err.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_encoder.h>
#include <drm/drm_file.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_modes.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_prime.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>
#include <drm/drm_native_identity.h>

/*
 * Optional build guard.  A loadable module is accepted by any kernel of the
 * same __NetBSD_Version__, even a different build, so the build script may
 * write efidrm_kver.h with the version line of the kernel it was built for
 * (EFIDRM_KERNEL_VERSION); the module then refuses to attach to any other
 * kernel instead of risking a struct-layout mismatch.
 */
#if defined(__has_include)
#if __has_include("efidrm_kver.h")
#include "efidrm_kver.h"
#endif
#endif

#define EFIDRM_NAME	"efidrm"
#define EFIDRM_DESC	"KMS over the UEFI GOP framebuffer"
#define EFIDRM_MAX_DIM	8192
#define EFIDRM_MAX_DUMB	(256u * 1024 * 1024)
#define EFIDRM_PHYS_W_MM 344		/* 16" 2560x1600 panel of the MSI Vector */
#define EFIDRM_PHYS_H_MM 215

extern int drm_guarantee_initialized(void);
extern struct x86_bus_dma_tag pci_bus_dma64_tag;

struct efidrm_softc {
	device_t		sc_dev;
	struct drm_device	*sc_drm;
	bus_space_tag_t		sc_memt;
	bus_space_handle_t	sc_memh;
	uint8_t			*sc_fb;		/* write-combining GOP mapping */
	uint64_t		sc_pa;
	size_t			sc_fbsize;
	uint32_t		sc_width, sc_height, sc_stride;
	struct drm_plane	sc_plane;
	struct drm_crtc		sc_crtc;
	struct drm_encoder	sc_enc;
	struct drm_connector	sc_conn;
	struct callout		sc_vbl_co;
	volatile bool		sc_vbl_on;
	bool			sc_warned_vmap;

	/* shadow of what is on the screen (ordinary kernel memory) */
	uint8_t			*sc_shadow;
	bool			sc_shadow_valid;
	volatile unsigned	sc_shadow_inval;	/* set from other contexts */

	/* software vblank, nanoseconds */
	uint64_t		sc_vbl_period;
	uint64_t		sc_vbl_next;
	uint32_t		sc_evseq;

	struct sysctllog	*sc_log;
};

/*
 * Run-time tunables (sysctl hw.efidrm.*) and counters.  Counters are
 * updated only from the commit path; they are read racily on purpose.
 */
static struct efidrm_tun {
	int	shadow;		/* 1: compare against the shadow, copy only the diff */
	int	evmode;		/* 0: flip event on the next vblank tick; 1: right after the copy */
	int	sync;		/* 1: blocking atomic commit (copy inside the ioctl) */
	int	invalidate;	/* write 1: next frame is copied in full */
	int	refresh;	/* ro: advertised refresh rate, Hz */
	int	shadow_ok;	/* ro: shadow buffer allocated */
	int	nocopy;		/* debug: skip the copy (measures the floor) */
} efidrm_tun = { .shadow = 0, .evmode = 1, .sync = 0 };
/*
 * Defaults, chosen by measurement in a VM with Plasma (see NOTES.md): the flip
 * event goes out right after the copy (evmode=1); the shadow frame is off
 * (modload -b shadow=true allocates it, then sysctl hw.efidrm.shadow=1 or
 * modload -i use_shadow=1 turns it on); commits stay non-blocking.
 */

static struct efidrm_cnt {
	uint64_t commits;	/* plane updates that reached the copy */
	uint64_t copy_full;
	uint64_t copy_part;
	uint64_t copy_none;	/* nothing changed */
	uint64_t bytes;		/* bytes written to the GOP */
	uint64_t copy_us;	/* time spent in the update (compare + copy) */
	uint64_t copy_us_max;
	uint64_t cmp_bytes;	/* bytes compared against the shadow */
	uint64_t ev_immediate;
	uint64_t ev_tick;
} efidrm_cnt;

/* A dumb buffer: shmem object plus "permanently vmapped" marker. */
struct efidrm_bo {
	struct drm_gem_shmem_object	shmem;
	bool				mapped;
};
#define to_bo(obj) container_of(obj, struct efidrm_bo, shmem.base)

static struct efidrm_softc *efidrm_sc;	/* single instance */
/*
 * No render node by default, like Linux simpledrm: dumb-buffer ioctls are not
 * allowed on render nodes, so renderD128 would be useless to kms_swrast/GBM
 * and would only mislead compositors.  modload -b render=true enables it.
 */
static bool efidrm_want_render = false;
static int efidrm_want_refresh = 0;		/* Hz, 0: derive from hz */
static bool efidrm_want_shadow = false;		/* allocate the shadow (modload -b shadow=true) */
static bool efidrm_shadow_fail = false;		/* test: simulate no memory */
static int efidrm_attach_status;	/* 0 ok, else errno from attach */

/* ---------------------------------------------------------------- GEM */

static void
efidrm_gem_free(struct drm_gem_object *obj)
{
	struct efidrm_bo *bo = to_bo(obj);

	/* Drop the permanent mapping taken by efidrm_bo_vaddr. */
	if (bo->mapped) {
		drm_gem_shmem_vunmap(obj, bo->shmem.vaddr);
		bo->mapped = false;
	}
	/*
	 * drm_gem_shmem_free_object does kfree(&bo->shmem), and shmem is
	 * the first member of efidrm_bo, so the whole object is released.
	 */
	drm_gem_shmem_free_object(obj);
}

static const struct drm_gem_object_funcs efidrm_gem_funcs = {
	.free		= efidrm_gem_free,
	.print_info	= drm_gem_shmem_print_info,
	.pin		= drm_gem_shmem_pin,
	.unpin		= drm_gem_shmem_unpin,
	.get_sg_table	= drm_gem_shmem_get_sg_table,
	.vmap		= drm_gem_shmem_vmap,
	.vunmap		= drm_gem_shmem_vunmap,
};

static struct drm_gem_object *
efidrm_gem_create_object(struct drm_device *dev, size_t size)
{
	struct efidrm_bo *bo;

	bo = kzalloc(sizeof(*bo), GFP_KERNEL);
	if (bo == NULL)
		return NULL;
	bo->shmem.base.funcs = &efidrm_gem_funcs;
	return &bo->shmem.base;
}

/* Kernel VA of the buffer, mapped once and kept until the object dies. */
static void *
efidrm_bo_vaddr(struct drm_gem_object *obj)
{
	struct efidrm_bo *bo = to_bo(obj);
	void *va;

	if (bo->mapped)
		return bo->shmem.vaddr;
	va = drm_gem_shmem_vmap(obj);
	if (IS_ERR_OR_NULL(va))
		return NULL;
	bo->mapped = true;
	return va;
}

static int
efidrm_dumb_create(struct drm_file *file, struct drm_device *dev,
    struct drm_mode_create_dumb *args)
{
	struct drm_gem_shmem_object *shmem;
	uint64_t pitch, size;
	uint32_t handle;
	int ret;

	if (args->bpp != 32 || args->flags != 0)
		return -EINVAL;
	if (args->width == 0 || args->height == 0 ||
	    args->width > EFIDRM_MAX_DIM || args->height > EFIDRM_MAX_DIM)
		return -EINVAL;
	pitch = (uint64_t)args->width * 4;
	size = pitch * args->height;
	if (size > EFIDRM_MAX_DUMB)
		return -EINVAL;
	size = roundup(size, PAGE_SIZE);

	shmem = drm_gem_shmem_create(dev, size);
	if (IS_ERR(shmem))
		return PTR_ERR(shmem);
	ret = drm_gem_handle_create(file, &shmem->base, &handle);
	/* The handle (if any) holds the reference now. */
	drm_gem_object_put_unlocked(&shmem->base);
	if (ret)
		return ret;
	args->pitch = (uint32_t)pitch;
	args->size = size;
	args->handle = handle;
	return 0;
}

/* ------------------------------------------------------------- plane */

static const uint32_t efidrm_formats[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
};

static const struct drm_plane_funcs efidrm_plane_funcs = {
	.update_plane		= drm_atomic_helper_update_plane,
	.disable_plane		= drm_atomic_helper_disable_plane,
	.destroy		= drm_plane_cleanup,
	.reset			= drm_atomic_helper_plane_reset,
	.atomic_duplicate_state	= drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_plane_destroy_state,
};

static int
efidrm_plane_atomic_check(struct drm_plane *plane,
    struct drm_plane_state *state)
{
	struct drm_crtc_state *crtc_state;

	if (state->fb == NULL || state->crtc == NULL)
		return 0;
	crtc_state = drm_atomic_get_crtc_state(state->state, state->crtc);
	if (IS_ERR(crtc_state))
		return PTR_ERR(crtc_state);
	return drm_atomic_helper_check_plane_state(state, crtc_state,
	    DRM_PLANE_HELPER_NO_SCALING, DRM_PLANE_HELPER_NO_SCALING,
	    false, true);
}

#include "efidrm_shadow.h"

static inline uint64_t
efidrm_now_ns(void)
{
	struct timespec ts;

	nanouptime(&ts);
	return (uint64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}

/*
 * Copy a rectangle of the source buffer to the GOP.  With a valid shadow only
 * the changed parts are written (to the GOP and to the shadow); returns the
 * number of bytes written to the GOP.
 */
static size_t
efidrm_copy_rect(struct efidrm_softc *sc, struct drm_framebuffer *fb,
    const uint8_t *src, int sx, int sy, int dx, int dy, int w, int h,
    bool use_shadow, size_t *cmp_bytes)
{
	const int gw = (int)sc->sc_width, gh = (int)sc->sc_height;
	size_t total;

	/* Clip to the GOP buffer; the plane check makes this a no-op. */
	if (dx < 0 || dy < 0 || w <= 0 || h <= 0)
		return 0;
	if (dx >= gw || dy >= gh)
		return 0;
	if (dx + w > gw)
		w = gw - dx;
	if (dy + h > gh)
		h = gh - dy;
	total = efidrm_copy_rows(sc->sc_fb + (size_t)dy * sc->sc_stride +
	    (size_t)dx * 4, sc->sc_stride,
	    sc->sc_shadow != NULL ? sc->sc_shadow + (size_t)dy * sc->sc_stride +
	    (size_t)dx * 4 : NULL,
	    src + (size_t)sy * fb->pitches[0] + fb->offsets[0] + (size_t)sx * 4,
	    fb->pitches[0], (size_t)w * 4, h, use_shadow,
	    sc->sc_shadow != NULL && !sc->sc_shadow_valid, cmp_bytes);
	return total;
}

static void
efidrm_plane_atomic_update(struct drm_plane *plane,
    struct drm_plane_state *old_state)
{
	struct efidrm_softc *sc = plane->dev->dev_private;
	struct drm_plane_state *state = plane->state;
	struct drm_framebuffer *fb = state->fb;
	struct drm_rect rect;
	const uint8_t *src;
	int sx, sy, dx, dy, w, h;
	bool full, use_shadow = false, cover_all;
	size_t written, cmp_bytes = 0, area;
	uint64_t t0, us;

	if (fb == NULL || state->crtc == NULL || !state->visible)
		return;
	src = efidrm_bo_vaddr(fb->obj[0]);
	if (src == NULL) {
		if (!sc->sc_warned_vmap) {
			sc->sc_warned_vmap = true;
			device_printf(sc->sc_dev,
			    "cannot map the dumb buffer, frame dropped\n");
		}
		return;
	}

	/* Source origin in fb pixels and the matching GOP position. */
	sx = state->src.x1 >> 16;
	sy = state->src.y1 >> 16;
	dx = state->dst.x1;
	dy = state->dst.y1;

	full = old_state->fb == NULL || old_state->crtc != state->crtc ||
	    !drm_rect_equals(&old_state->src, &state->src) ||
	    !drm_rect_equals(&old_state->dst, &state->dst);
	if (full) {
		rect.x1 = sx;
		rect.y1 = sy;
		rect.x2 = state->src.x2 >> 16;
		rect.y2 = state->src.y2 >> 16;
	} else if (!drm_atomic_helper_damage_merged(old_state, state, &rect))
		return;

	t0 = efidrm_now_ns();
	w = rect.x2 - rect.x1;
	h = rect.y2 - rect.y1;
	cover_all = dx + (rect.x1 - sx) == 0 && dy + (rect.y1 - sy) == 0 &&
	    w >= (int)sc->sc_width && h >= (int)sc->sc_height;

	use_shadow = efidrm_shadow_plan(sc->sc_shadow != NULL,
	    efidrm_tun.shadow != 0,
	    atomic_swap_uint(&sc->sc_shadow_inval, 0) != 0, full,
	    efidrm_tun.nocopy != 0, &sc->sc_shadow_valid);
	if (efidrm_tun.nocopy)
		written = 0;
	else
		written = efidrm_copy_rect(sc, fb, src, rect.x1, rect.y1,
		    dx + (rect.x1 - sx), dy + (rect.y1 - sy), w, h,
		    use_shadow, &cmp_bytes);
	/* The shadow is trustworthy once a whole-screen copy has filled it. */
	efidrm_shadow_done(sc->sc_shadow != NULL, efidrm_tun.nocopy != 0,
	    cover_all, &sc->sc_shadow_valid);

	us = (efidrm_now_ns() - t0) / 1000;
	area = (size_t)w * h * 4;
	efidrm_cnt.commits++;
	efidrm_cnt.bytes += written;
	efidrm_cnt.cmp_bytes += cmp_bytes;
	efidrm_cnt.copy_us += us;
	if (us > efidrm_cnt.copy_us_max)
		efidrm_cnt.copy_us_max = us;
	if (written == 0)
		efidrm_cnt.copy_none++;
	else if (written >= area)
		efidrm_cnt.copy_full++;
	else
		efidrm_cnt.copy_part++;
}

static const struct drm_plane_helper_funcs efidrm_plane_helper_funcs = {
	.atomic_check	= efidrm_plane_atomic_check,
	.atomic_update	= efidrm_plane_atomic_update,
};

/* -------------------------------------------------------------- CRTC */

/*
 * Software vblank.  The callout runs on the clock tick grid; the vblank
 * boundaries are computed in nanoseconds so that any refresh rate averages
 * out exactly (60 Hz at hz=1000 alternates 16/17 ms; at hz=100 the tick
 * grid makes the intervals 10/20 ms with the right average).
 */
static uint64_t
efidrm_vbl_default_period(void)
{
	int t;

	/*
	 * With a fine clock tick (hz >= 600) the period is computed in
	 * nanoseconds: exactly 60 Hz, ticks alternate 16/17 ms at hz=1000.
	 * With a coarse tick the period is a whole number of ticks nearest
	 * to 60 Hz (2 ticks = 50 Hz at hz=100), and the mode says so.
	 */
	if (hz >= 600)
		return 1000000000 / 60;
	t = (hz + 30) / 60;
	if (t < 1)
		t = 1;
	return (uint64_t)t * (1000000000 / hz);
}

static void
efidrm_vbl_schedule(struct efidrm_softc *sc, uint64_t now)
{
	uint64_t tickns = 1000000000 / hz, dt;
	int t;

	dt = sc->sc_vbl_next > now ? sc->sc_vbl_next - now : 0;
	t = (int)((dt + tickns - 1) / tickns);
	callout_schedule(&sc->sc_vbl_co, t < 1 ? 1 : t);
}

static void
efidrm_vbl_tick(void *arg)
{
	struct efidrm_softc *sc = arg;
	uint64_t now, tickns = 1000000000 / hz;

	if (!sc->sc_vbl_on)
		return;
	now = efidrm_now_ns();
	if (now + tickns / 2 >= sc->sc_vbl_next) {
		drm_crtc_handle_vblank(&sc->sc_crtc);
		sc->sc_vbl_next += sc->sc_vbl_period;
		if (sc->sc_vbl_next + sc->sc_vbl_period < now)
			sc->sc_vbl_next = now + sc->sc_vbl_period;
	}
	if (sc->sc_vbl_on)
		efidrm_vbl_schedule(sc, now);
}

static int
efidrm_enable_vblank(struct drm_crtc *crtc)
{
	struct efidrm_softc *sc = crtc->dev->dev_private;
	uint64_t now = efidrm_now_ns();

	sc->sc_vbl_next = now + sc->sc_vbl_period;
	sc->sc_vbl_on = true;
	efidrm_vbl_schedule(sc, now);
	return 0;
}

static void
efidrm_disable_vblank(struct drm_crtc *crtc)
{
	struct efidrm_softc *sc = crtc->dev->dev_private;

	/* No callout_halt here: we are under vbl_lock; the tick re-checks. */
	sc->sc_vbl_on = false;
}

static const struct drm_crtc_funcs efidrm_crtc_funcs = {
	.set_config		= drm_atomic_helper_set_config,
	.destroy		= drm_crtc_cleanup,
	.page_flip		= drm_atomic_helper_page_flip,
	.reset			= drm_atomic_helper_crtc_reset,
	.atomic_duplicate_state	= drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_crtc_destroy_state,
	.enable_vblank		= efidrm_enable_vblank,
	.disable_vblank		= efidrm_disable_vblank,
};

static enum drm_mode_status
efidrm_crtc_mode_valid(struct drm_crtc *crtc,
    const struct drm_display_mode *mode)
{
	struct efidrm_softc *sc = crtc->dev->dev_private;

	if (mode->hdisplay != (int)sc->sc_width ||
	    mode->vdisplay != (int)sc->sc_height)
		return MODE_BAD;
	return MODE_OK;
}

static int
efidrm_crtc_atomic_check(struct drm_crtc *crtc, struct drm_crtc_state *state)
{
	struct efidrm_softc *sc = crtc->dev->dev_private;

	if (state->enable &&
	    (state->mode.hdisplay != (int)sc->sc_width ||
	     state->mode.vdisplay != (int)sc->sc_height))
		return -EINVAL;
	return 0;
}

static void
efidrm_crtc_atomic_enable(struct drm_crtc *crtc,
    struct drm_crtc_state *old_state)
{
	struct efidrm_softc *sc = crtc->dev->dev_private;

	/* Whoever drew on the GOP meanwhile (the console): redraw everything. */
	atomic_store_relaxed(&sc->sc_shadow_inval, 1);
	drm_crtc_vblank_on(crtc);
}

static void
efidrm_crtc_atomic_disable(struct drm_crtc *crtc,
    struct drm_crtc_state *old_state)
{
	struct efidrm_softc *sc = crtc->dev->dev_private;
	unsigned long flags;

	atomic_store_relaxed(&sc->sc_shadow_inval, 1);
	drm_crtc_vblank_off(crtc);
	spin_lock_irqsave(&crtc->dev->event_lock, flags);
	if (crtc->state->event && !crtc->state->active) {
		drm_crtc_send_vblank_event(crtc, crtc->state->event);
		crtc->state->event = NULL;
	}
	spin_unlock_irqrestore(&crtc->dev->event_lock, flags);
}

static void
efidrm_crtc_atomic_flush(struct drm_crtc *crtc,
    struct drm_crtc_state *old_state)
{
	struct efidrm_softc *sc = crtc->dev->dev_private;
	struct drm_pending_vblank_event *event;
	unsigned long flags;
	bool armed;

	if (crtc->state->event == NULL)
		return;
	if (efidrm_tun.evmode == 1 && crtc->state->active) {
		/*
		 * Report the flip right after the copy, stamped with the
		 * time it was done (as virtgpu does).  A tick-aligned event
		 * would add up to a whole refresh period, and the compositor
		 * would take every frame for late.
		 */
		uint64_t t = efidrm_now_ns();

		spin_lock_irqsave(&crtc->dev->event_lock, flags);
		event = crtc->state->event;
		crtc->state->event = NULL;
		event->pipe = drm_crtc_index(crtc);
		event->event.vbl.sequence = sc->sc_evseq++;
		event->event.vbl.tv_sec = t / 1000000000;
		event->event.vbl.tv_usec = (t % 1000000000) / 1000;
		drm_send_event_locked(crtc->dev, &event->base);
		spin_unlock_irqrestore(&crtc->dev->event_lock, flags);
		efidrm_cnt.ev_immediate++;
		return;
	}
	/*
	 * NetBSD's drm_vblank takes event_lock itself in
	 * drm_crtc_vblank_get, so call it before taking the lock.
	 */
	armed = crtc->state->active && drm_crtc_vblank_get(crtc) == 0;
	spin_lock_irqsave(&crtc->dev->event_lock, flags);
	if (armed)
		drm_crtc_arm_vblank_event(crtc, crtc->state->event);
	else
		drm_crtc_send_vblank_event(crtc, crtc->state->event);
	crtc->state->event = NULL;
	spin_unlock_irqrestore(&crtc->dev->event_lock, flags);
	efidrm_cnt.ev_tick++;
}

static const struct drm_crtc_helper_funcs efidrm_crtc_helper_funcs = {
	.mode_valid	= efidrm_crtc_mode_valid,
	.atomic_check	= efidrm_crtc_atomic_check,
	.atomic_enable	= efidrm_crtc_atomic_enable,
	.atomic_disable	= efidrm_crtc_atomic_disable,
	.atomic_flush	= efidrm_crtc_atomic_flush,
};

/* ----------------------------------------------- encoder, connector */

static const struct drm_encoder_funcs efidrm_enc_funcs = {
	.destroy = drm_encoder_cleanup,
};

static int
efidrm_conn_get_modes(struct drm_connector *connector)
{
	struct efidrm_softc *sc = connector->dev->dev_private;
	struct drm_display_mode *mode;

	mode = drm_cvt_mode(connector->dev, sc->sc_width, sc->sc_height,
	    (int)efidrm_tun.refresh, false, false, false);
	if (mode == NULL)
		return 0;
	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);
	connector->display_info.width_mm = EFIDRM_PHYS_W_MM;
	connector->display_info.height_mm = EFIDRM_PHYS_H_MM;
	return 1;
}

static enum drm_mode_status
efidrm_conn_mode_valid(struct drm_connector *connector,
    struct drm_display_mode *mode)
{
	struct efidrm_softc *sc = connector->dev->dev_private;

	if (mode->hdisplay != (int)sc->sc_width ||
	    mode->vdisplay != (int)sc->sc_height)
		return MODE_BAD;
	return MODE_OK;
}

static const struct drm_connector_helper_funcs efidrm_conn_helper_funcs = {
	.get_modes	= efidrm_conn_get_modes,
	.mode_valid	= efidrm_conn_mode_valid,
};

static enum drm_connector_status
efidrm_conn_detect(struct drm_connector *connector, bool force)
{
	return connector_status_connected;
}

static const struct drm_connector_funcs efidrm_conn_funcs = {
	.detect			= efidrm_conn_detect,
	.fill_modes		= drm_helper_probe_single_connector_modes,
	.destroy		= drm_connector_cleanup,
	.reset			= drm_atomic_helper_connector_reset,
	.atomic_duplicate_state	= drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_connector_destroy_state,
};

/* ------------------------------------------------------- mode config */

static const struct drm_framebuffer_funcs efidrm_fb_funcs = {
	.create_handle	= drm_gem_fb_create_handle,
	.destroy	= drm_gem_fb_destroy,
	.dirty		= drm_atomic_helper_dirtyfb,
};

static struct drm_framebuffer *
efidrm_fb_create(struct drm_device *dev, struct drm_file *file,
    const struct drm_mode_fb_cmd2 *cmd)
{
	if (cmd->pixel_format != DRM_FORMAT_XRGB8888 &&
	    cmd->pixel_format != DRM_FORMAT_ARGB8888)
		return ERR_PTR(-EINVAL);
	if (cmd->modifier[0] != 0 && (cmd->flags & DRM_MODE_FB_MODIFIERS))
		return ERR_PTR(-EINVAL);
	return drm_gem_fb_create_with_funcs(dev, file, cmd, &efidrm_fb_funcs);
}

/* sync=1: make every commit blocking, so the copy happens inside the ioctl. */
static int
efidrm_atomic_commit(struct drm_device *dev, struct drm_atomic_state *state,
    bool nonblock)
{
	return drm_atomic_helper_commit(dev, state,
	    nonblock && !efidrm_tun.sync);
}

static const struct drm_mode_config_funcs efidrm_mode_funcs = {
	.fb_create	= efidrm_fb_create,
	.atomic_check	= drm_atomic_helper_check,
	.atomic_commit	= efidrm_atomic_commit,
};

/* Nobody uses the device: whatever is on the screen is no longer ours. */
static void
efidrm_lastclose(struct drm_device *dev)
{
	struct efidrm_softc *sc = dev->dev_private;

	atomic_store_relaxed(&sc->sc_shadow_inval, 1);
}

/* ------------------------------------------------------- DRM driver */

static struct drm_driver efidrm_driver = {
	.driver_features = DRIVER_MODESET | DRIVER_GEM | DRIVER_RENDER |
	    DRIVER_ATOMIC,
	.lastclose		= efidrm_lastclose,
	.dumb_create		= efidrm_dumb_create,
	.dumb_map_offset	= drm_gem_dumb_map_offset,
	.dumb_destroy		= drm_gem_dumb_destroy,
	.prime_handle_to_fd	= drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle	= drm_gem_prime_fd_to_handle,
	.gem_prime_mmap		= drm_gem_shmem_prime_mmap,
	.gem_create_object	= efidrm_gem_create_object,
	.mmap_object		= drm_gem_mmap_object,
	.gem_uvm_ops		= &drm_gem_shmem_uvm_ops,
	.name	= EFIDRM_NAME,
	.desc	= EFIDRM_DESC,
	.date	= "20261112",
	.major	= 1,
	.minor	= 0,
	.patchlevel = 0,
};

static int
efidrm_drm_init(struct efidrm_softc *sc, device_t self)
{
	struct drm_device *dev;
	int ret;

	ret = -drm_guarantee_initialized();
	if (ret)
		return ret;
	if (!efidrm_want_render)
		efidrm_driver.driver_features &= ~DRIVER_RENDER;
	dev = drm_dev_alloc(&efidrm_driver, self);
	if (IS_ERR(dev))
		return PTR_ERR(dev);
	sc->sc_drm = dev;
	dev->dev_private = sc;
	dev->bus_dmat = dev->bus_dmat32 = dev->dmat = &pci_bus_dma64_tag;
	dev->dmat_subregion_p = false;

	dev->native_platform.version = DRM_NATIVE_PLATFORM_VERSION;
	dev->native_platform.length = sizeof(dev->native_platform);
	dev->native_platform.bus_type = DRM_NATIVE_BUS_PLATFORM;
	strlcpy(dev->native_platform.fullname, device_xname(self),
	    sizeof(dev->native_platform.fullname));
	strlcpy(dev->native_platform.compatible, "oxvision,efidrm",
	    sizeof(dev->native_platform.compatible));

	ret = drm_dev_set_unique(dev, device_xname(self));
	if (ret)
		goto put;

	drm_mode_config_init(dev);
	dev->mode_config.funcs = &efidrm_mode_funcs;
	dev->mode_config.min_width = 32;
	dev->mode_config.min_height = 32;
	dev->mode_config.max_width = EFIDRM_MAX_DIM;
	dev->mode_config.max_height = EFIDRM_MAX_DIM;
	dev->mode_config.preferred_depth = 24;
	dev->mode_config.quirk_addfb_prefer_host_byte_order = true;

	if (efidrm_want_refresh > 0 && efidrm_want_refresh <= 240)
		sc->sc_vbl_period = 1000000000 / efidrm_want_refresh;
	else
		sc->sc_vbl_period = efidrm_vbl_default_period();
	efidrm_tun.refresh = (int)((1000000000 + sc->sc_vbl_period / 2) /
	    sc->sc_vbl_period);
	callout_init(&sc->sc_vbl_co, CALLOUT_MPSAFE);
	callout_setfunc(&sc->sc_vbl_co, efidrm_vbl_tick, sc);

	ret = drm_universal_plane_init(dev, &sc->sc_plane, 1,
	    &efidrm_plane_funcs, efidrm_formats, __arraycount(efidrm_formats),
	    NULL, DRM_PLANE_TYPE_PRIMARY, NULL);
	if (ret)
		goto cleanup;
	drm_plane_helper_add(&sc->sc_plane, &efidrm_plane_helper_funcs);
	drm_plane_enable_fb_damage_clips(&sc->sc_plane);

	ret = drm_crtc_init_with_planes(dev, &sc->sc_crtc, &sc->sc_plane, NULL,
	    &efidrm_crtc_funcs, NULL);
	if (ret)
		goto cleanup;
	drm_crtc_helper_add(&sc->sc_crtc, &efidrm_crtc_helper_funcs);

	ret = drm_encoder_init(dev, &sc->sc_enc, &efidrm_enc_funcs,
	    DRM_MODE_ENCODER_VIRTUAL, NULL);
	if (ret)
		goto cleanup;
	sc->sc_enc.possible_crtcs = 1;

	ret = drm_connector_init(dev, &sc->sc_conn, &efidrm_conn_funcs,
	    DRM_MODE_CONNECTOR_eDP);
	if (ret)
		goto cleanup;
	drm_connector_helper_add(&sc->sc_conn, &efidrm_conn_helper_funcs);
	sc->sc_conn.display_info.width_mm = EFIDRM_PHYS_W_MM;
	sc->sc_conn.display_info.height_mm = EFIDRM_PHYS_H_MM;
	ret = drm_connector_attach_encoder(&sc->sc_conn, &sc->sc_enc);
	if (ret)
		goto cleanup;

	ret = drm_vblank_init(dev, 1);
	if (ret)
		goto cleanup;
	drm_mode_config_reset(dev);

	ret = drm_dev_register(dev, 0);
	if (ret)
		goto cleanup;
	return 0;

cleanup:
	drm_mode_config_cleanup(dev);
put:
	drm_dev_put(dev);
	sc->sc_drm = NULL;
	return ret;
}


/* ------------------------------------------------------------ sysctl */

static int
efidrm_sysctl_inval(SYSCTLFN_ARGS)
{
	struct sysctlnode node = *rnode;
	int v = 0, error;

	node.sysctl_data = &v;
	error = sysctl_lookup(SYSCTLFN_CALL(&node));
	if (error || newp == NULL)
		return error;
	if (v != 0 && efidrm_sc != NULL)
		atomic_store_relaxed(&efidrm_sc->sc_shadow_inval, 1);
	return 0;
}

/* Any change of hw.efidrm.shadow drops the copy: the screen may differ from it. */
static int
efidrm_sysctl_shadow(SYSCTLFN_ARGS)
{
	struct sysctlnode node = *rnode;
	int v = efidrm_tun.shadow, error;

	node.sysctl_data = &v;
	error = sysctl_lookup(SYSCTLFN_CALL(&node));
	if (error || newp == NULL)
		return error;
	efidrm_tun.shadow = v != 0;
	if (efidrm_sc != NULL)
		atomic_store_relaxed(&efidrm_sc->sc_shadow_inval, 1);
	return 0;
}

static void
efidrm_sysctl_setup(struct efidrm_softc *sc)
{
	const struct sysctlnode *root = NULL;
	int rv = 0;

#define EFI_INT(name, p, flags, descr) \
	(void)sysctl_createv(&sc->sc_log, 0, &root, NULL, flags, CTLTYPE_INT, \
	    name, SYSCTL_DESCR(descr), NULL, 0, p, 0, CTL_CREATE, CTL_EOL)
#define EFI_CNT(name, p, descr) \
	(void)sysctl_createv(&sc->sc_log, 0, &root, NULL, CTLFLAG_READONLY, \
	    CTLTYPE_QUAD, name, SYSCTL_DESCR(descr), NULL, 0, p, 0, \
	    CTL_CREATE, CTL_EOL)

	rv = sysctl_createv(&sc->sc_log, 0, NULL, &root, CTLFLAG_PERMANENT,
	    CTLTYPE_NODE, "efidrm", SYSCTL_DESCR("efidrm tunables and counters"),
	    NULL, 0, NULL, 0, CTL_HW, CTL_CREATE, CTL_EOL);
	if (rv != 0)
		return;
	(void)sysctl_createv(&sc->sc_log, 0, &root, NULL, CTLFLAG_READWRITE,
	    CTLTYPE_INT, "shadow", SYSCTL_DESCR(
	    "copy only the parts that differ from the shadow frame"),
	    efidrm_sysctl_shadow, 0, NULL, 0, CTL_CREATE, CTL_EOL);
	EFI_INT("evmode", &efidrm_tun.evmode, CTLFLAG_READWRITE,
	    "flip event: 0 on the next vblank tick, 1 right after the copy");
	EFI_INT("sync", &efidrm_tun.sync, CTLFLAG_READWRITE,
	    "blocking commits: the copy happens inside the ioctl");
	EFI_INT("nocopy", &efidrm_tun.nocopy, CTLFLAG_READWRITE,
	    "debug: do not copy frames to the GOP");
	EFI_INT("refresh", &efidrm_tun.refresh, CTLFLAG_READONLY,
	    "advertised refresh rate, Hz");
	EFI_INT("shadow_ok", &efidrm_tun.shadow_ok, CTLFLAG_READONLY,
	    "the shadow frame was allocated");
	(void)sysctl_createv(&sc->sc_log, 0, &root, NULL,
	    CTLFLAG_READWRITE, CTLTYPE_INT, "invalidate",
	    SYSCTL_DESCR("write 1: copy the next frame in full"),
	    efidrm_sysctl_inval, 0, NULL, 0, CTL_CREATE, CTL_EOL);
	EFI_CNT("commits", &efidrm_cnt.commits, "plane updates that copied");
	EFI_CNT("copy_full", &efidrm_cnt.copy_full, "updates that wrote the whole rectangle");
	EFI_CNT("copy_part", &efidrm_cnt.copy_part, "updates that wrote part of it");
	EFI_CNT("copy_none", &efidrm_cnt.copy_none, "updates with nothing changed");
	EFI_CNT("bytes", &efidrm_cnt.bytes, "bytes written to the GOP");
	EFI_CNT("cmp_bytes", &efidrm_cnt.cmp_bytes, "bytes compared against the shadow");
	EFI_CNT("copy_us", &efidrm_cnt.copy_us, "total time in compare+copy, us");
	EFI_CNT("copy_us_max", &efidrm_cnt.copy_us_max, "longest compare+copy, us");
	EFI_CNT("ev_immediate", &efidrm_cnt.ev_immediate, "flip events sent right after the copy");
	EFI_CNT("ev_tick", &efidrm_cnt.ev_tick, "flip events delivered on a vblank tick");
#undef EFI_INT
#undef EFI_CNT
}

/* ------------------------------------------------------- autoconf */

static int	efidrm_match(device_t, cfdata_t, void *);
static void	efidrm_attach(device_t, device_t, void *);
static int	efidrm_detach(device_t, int);

CFATTACH_DECL_NEW(efidrm, sizeof(struct efidrm_softc), efidrm_match,
    efidrm_attach, efidrm_detach, NULL);

static int
efidrm_match(device_t parent, cfdata_t cf, void *aux)
{
	return 1;
}

static void
efidrm_attach(device_t parent, device_t self, void *aux)
{
	struct efidrm_softc *sc = device_private(self);
	const struct btinfo_framebuffer *fbi;
	int error;

	sc->sc_dev = self;
	aprint_naive("\n");
	aprint_normal_dev(self, "UEFI GOP framebuffer KMS\n");

	fbi = lookup_bootinfo(BTINFO_FRAMEBUFFER);
	if (fbi == NULL || fbi->physaddr == 0) {
		aprint_error_dev(self, "no GOP framebuffer in bootinfo\n");
		efidrm_attach_status = ENODEV;
		return;
	}
	aprint_normal_dev(self, "GOP framebuffer at 0x%" PRIx64
	    ", %ux%u, depth %u, stride %u, r/g/b %u@%u/%u@%u/%u@%u\n",
	    fbi->physaddr, fbi->width, fbi->height, fbi->depth, fbi->stride,
	    fbi->rnum, fbi->rpos, fbi->gnum, fbi->gpos, fbi->bnum, fbi->bpos);
	/* Only B8G8R8X8 (blue in the low byte) matches DRM XRGB8888. */
	if (fbi->depth != 32 || fbi->rnum != 8 || fbi->gnum != 8 ||
	    fbi->bnum != 8 || fbi->rpos != 16 || fbi->gpos != 8 ||
	    fbi->bpos != 0) {
		aprint_error_dev(self, "unsupported pixel format "
		    "(need 32 bpp XRGB8888/BGRX)\n");
		efidrm_attach_status = ENODEV;
		return;
	}
	if (fbi->width == 0 || fbi->height == 0 ||
	    fbi->width > EFIDRM_MAX_DIM || fbi->height > EFIDRM_MAX_DIM ||
	    fbi->stride < fbi->width * 4 || (fbi->stride & 3) != 0) {
		aprint_error_dev(self, "bad framebuffer geometry\n");
		efidrm_attach_status = EINVAL;
		return;
	}
	sc->sc_pa = fbi->physaddr;
	sc->sc_width = fbi->width;
	sc->sc_height = fbi->height;
	sc->sc_stride = fbi->stride;
	sc->sc_fbsize = (size_t)fbi->stride * fbi->height;

	/* Own write-combining mapping, as genfb_machdep does for the console. */
	sc->sc_memt = x86_bus_space_mem;
	error = _x86_memio_map(sc->sc_memt, (bus_addr_t)sc->sc_pa,
	    sc->sc_fbsize, BUS_SPACE_MAP_LINEAR | BUS_SPACE_MAP_PREFETCHABLE,
	    &sc->sc_memh);
	if (error) {
		aprint_error_dev(self, "cannot map the framebuffer: %d\n",
		    error);
		efidrm_attach_status = error;
		return;
	}
	sc->sc_fb = bus_space_vaddr(sc->sc_memt, sc->sc_memh);
	if (sc->sc_fb == NULL) {
		aprint_error_dev(self, "no linear mapping\n");
		_x86_memio_unmap(sc->sc_memt, sc->sc_memh, sc->sc_fbsize,
		    NULL);
		efidrm_attach_status = ENOMEM;
		return;
	}

	if (efidrm_want_shadow) {
		if (!efidrm_shadow_fail)
			sc->sc_shadow = kmem_alloc(sc->sc_fbsize, KM_NOSLEEP);
		if (sc->sc_shadow == NULL) {
			aprint_error_dev(self, "no memory for the shadow frame "
			    "(%zu KB): working without it, every frame is "
			    "copied in full\n", sc->sc_fbsize / 1024);
			efidrm_tun.shadow = 0;
		}
	}
	efidrm_tun.shadow_ok = sc->sc_shadow != NULL;
	error = efidrm_drm_init(sc, self);
	if (error) {
		aprint_error_dev(self, "DRM initialization failed: %d\n",
		    error);
		_x86_memio_unmap(sc->sc_memt, sc->sc_memh, sc->sc_fbsize,
		    NULL);
		sc->sc_fb = NULL;
		if (sc->sc_shadow != NULL) {
			kmem_free(sc->sc_shadow, sc->sc_fbsize);
			sc->sc_shadow = NULL;
		}
		efidrm_attach_status = error < 0 ? -error : error;
		return;
	}
	efidrm_attach_status = 0;
	efidrm_sc = sc;
	efidrm_sysctl_setup(sc);
	aprint_normal_dev(self, "%ux%u XRGB8888 (stride %u), dumb buffers in "
	    "kernel memory, software vblank; /dev/dri/card%d%s\n",
	    sc->sc_width, sc->sc_height, sc->sc_stride,
	    sc->sc_drm->primary->index,
	    sc->sc_drm->render ? " + render node" : "");
}

static int
efidrm_detach(device_t self, int flags)
{
	struct efidrm_softc *sc = device_private(self);

	/* A failed attach leaves no DRM device: allow its cleanup. */
	if (sc->sc_drm == NULL)
		return 0;
	/* Hot removal is deliberately unsupported (as in virtiodrm). */
	return EBUSY;
}

/* ----------------------------------------------------------- module */

MODULE(MODULE_CLASS_DRIVER, efidrm, "drmkms");

CFDRIVER_DECL(efidrm, DV_DULL, NULL);
extern struct cfattach efidrm_ca;
static struct cfdata efidrm_cfdata[] = {
	{
		.cf_name = "efidrm",
		.cf_atname = "efidrm",
		.cf_unit = 0,
		.cf_fstate = FSTATE_STAR,
		.cf_loc = NULL,
		.cf_flags = 0,
		.cf_pspec = NULL,
	},
	{ NULL, NULL, 0, FSTATE_NOTFOUND, NULL, 0, NULL }
};

static int
efidrm_modcmd(modcmd_t cmd, void *arg)
{
	int error;

	switch (cmd) {
	case MODULE_CMD_INIT:
#ifdef EFIDRM_KERNEL_VERSION
		if (strncmp(version, EFIDRM_KERNEL_VERSION,
		    sizeof(EFIDRM_KERNEL_VERSION) - 1) != 0) {
			printf("efidrm: built for kernel \"%s\", refusing to "
			    "load into another kernel\n", EFIDRM_KERNEL_VERSION);
			return ENOEXEC;
		}
#endif
		if (arg != NULL) {
			prop_dictionary_t d = arg;
			prop_object_t o;

			o = prop_dictionary_get(d, "render");
			if (o != NULL && prop_object_type(o) == PROP_TYPE_BOOL)
				efidrm_want_render = prop_bool_true(
				    (prop_bool_t)o);
			o = prop_dictionary_get(d, "shadow");
			if (o != NULL && prop_object_type(o) == PROP_TYPE_BOOL)
				efidrm_want_shadow = prop_bool_true(
				    (prop_bool_t)o);
			o = prop_dictionary_get(d, "shadow_fail");
			if (o != NULL && prop_object_type(o) == PROP_TYPE_BOOL)
				efidrm_shadow_fail = prop_bool_true(
				    (prop_bool_t)o);
			o = prop_dictionary_get(d, "refresh");
			if (o != NULL && prop_object_type(o) == PROP_TYPE_NUMBER)
				efidrm_want_refresh = (int)prop_number_signed_value(
				    (prop_number_t)o);
			o = prop_dictionary_get(d, "evmode");
			if (o != NULL && prop_object_type(o) == PROP_TYPE_NUMBER)
				efidrm_tun.evmode = (int)prop_number_signed_value(
				    (prop_number_t)o);
			o = prop_dictionary_get(d, "sync");
			if (o != NULL && prop_object_type(o) == PROP_TYPE_NUMBER)
				efidrm_tun.sync = (int)prop_number_signed_value(
				    (prop_number_t)o);
			o = prop_dictionary_get(d, "use_shadow");
			if (o != NULL && prop_object_type(o) == PROP_TYPE_NUMBER)
				efidrm_tun.shadow = (int)prop_number_signed_value(
				    (prop_number_t)o);
		}
		error = config_cfdriver_attach(&efidrm_cd);
		if (error)
			return error;
		error = config_cfattach_attach(efidrm_cd.cd_name, &efidrm_ca);
		if (error) {
			config_cfdriver_detach(&efidrm_cd);
			return error;
		}
		error = config_cfdata_attach(efidrm_cfdata, 1);
		if (error) {
			config_cfattach_detach(efidrm_cd.cd_name, &efidrm_ca);
			config_cfdriver_detach(&efidrm_cd);
			return error;
		}
		efidrm_attach_status = ENXIO;
		(void)config_attach_pseudo(efidrm_cfdata);
		if (efidrm_sc == NULL) {
			/* attach refused: undo everything, stay unloaded */
			error = efidrm_attach_status ? efidrm_attach_status :
			    ENXIO;
			/* the failed pseudo-device instance is detached */
			if (config_cfdata_detach(efidrm_cfdata) == 0) {
				config_cfattach_detach(efidrm_cd.cd_name,
				    &efidrm_ca);
				config_cfdriver_detach(&efidrm_cd);
			}
			return error;
		}
		return 0;
	case MODULE_CMD_FINI:
		printf("efidrm: unload is not supported (DRM device is live)\n");
		return EBUSY;
	case MODULE_CMD_AUTOUNLOAD:
		return EBUSY;
	default:
		return ENOTTY;
	}
}
