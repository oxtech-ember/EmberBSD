/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD, AI-assisted. Native fixed-mode A733 KMS frontend. */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");
#include <sys/param.h>
#include <sys/atomic.h>
#include <sys/container_of.h>
#include <sys/cpu.h>
#include <sys/device.h>
#include <sys/intr.h>
#include <sys/kmem.h>
#include <sys/mman.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/vmem.h>
#include <dev/fdt/fdtvar.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem.h>
#include <drm/drm_legacy.h>
#include <drm/drm_modes.h>
#include <drm/drm_modeset_helper.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_prime.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>
#include <drm/drm_file.h>
#include "sun60i_a733_drm.h"

bool
a733_drm_mode_matches(const struct sun60i_a733_output_mode *n,
    const struct drm_display_mode *m)
{
	uint32_t flags;

	flags = n->hsync_positive ? DRM_MODE_FLAG_PHSYNC : DRM_MODE_FLAG_NHSYNC;
	flags |= n->vsync_positive ? DRM_MODE_FLAG_PVSYNC : DRM_MODE_FLAG_NVSYNC;

	return n->pixel_clock_hz % 1000 == 0 && m->clock > 0 &&
	    (uint64_t) m->clock * 1000 == n->pixel_clock_hz &&
	    m->hdisplay == (int64_t) n->hdisplay &&
	    m->hsync_start == (int64_t) n->hsync_start &&
	    m->hsync_end == (int64_t) n->hsync_end &&
	    m->htotal == (int64_t) n->htotal &&
	    m->vdisplay == (int64_t) n->vdisplay &&
	    m->vsync_start == (int64_t) n->vsync_start &&
	    m->vsync_end == (int64_t) n->vsync_end &&
	    m->vtotal == (int64_t) n->vtotal &&
	    m->hskew == 0 && m->vscan == 0 && m->flags == flags;
}

static void
a733_fb_destroy(struct drm_framebuffer *base)
{
	struct a733_drm_fb *fb = container_of(base, struct a733_drm_fb, base);

	drm_framebuffer_cleanup(base);
	drm_gem_object_put_unlocked(&fb->object->base);
	kmem_free(fb, sizeof(*fb));
}

static int
a733_fb_handle(struct drm_framebuffer *base, struct drm_file *file,
    unsigned int *handle)
{
	struct a733_drm_fb *fb = container_of(base, struct a733_drm_fb, base);

	return drm_gem_handle_create(file, &fb->object->base, handle);
}

static const struct drm_framebuffer_funcs a733_fb_funcs = {
	.destroy = a733_fb_destroy,
	.create_handle = a733_fb_handle,
};

static int
a733_file_open(struct drm_device *dev, struct drm_file *file)
{
	struct a733_drm *sc = dev->dev_private;
	struct a733_drm_file *owner = kmem_alloc(sizeof(*owner), KM_SLEEP);
	int error = 0;

	mutex_enter(&sc->lock);
	if (sc->next_file_id == UINT64_MAX)
		error = -EOVERFLOW;
	else
		owner->id = ++sc->next_file_id;
	mutex_exit(&sc->lock);
	if (error != 0) {
		kmem_free(owner, sizeof(*owner));
		return error;
	}
	file->driver_priv = owner;
	return 0;
}

static void
a733_file_close(struct drm_device *dev, struct drm_file *file)
{
	struct a733_drm_file *owner = file->driver_priv;

	(void)dev;
	file->driver_priv = NULL;
	if (owner != NULL)
		kmem_free(owner, sizeof(*owner));
}

static struct drm_framebuffer *
a733_fb_create(struct drm_device *dev, struct drm_file *file,
    const struct drm_mode_fb_cmd2 *cmd)
{
	const struct a733_drm *sc = dev->dev_private;
	struct drm_gem_object *gem;
	struct drm_gem_cma_object *obj;
	struct a733_drm_fb *fb;
	uint64_t bytes, address;
	int error;

	if (cmd->pixel_format != DRM_FORMAT_XRGB8888 ||
	    cmd->width != sc->native.mode.hdisplay ||
	    cmd->height != sc->native.mode.vdisplay ||
	    cmd->offsets[0] != 0 || cmd->modifier[0] != DRM_FORMAT_MOD_LINEAR ||
	    cmd->flags & ~DRM_MODE_FB_MODIFIERS ||
	    cmd->pitches[0] < (uint64_t) cmd->width * 4 || (cmd->pitches[0] & 63) != 0)
		return ERR_PTR(-EINVAL);
	for (u_int i = 1; i < 4; i++)
		if (cmd->handles[i] || cmd->pitches[i] ||
		    cmd->offsets[i] || cmd->modifier[i])
			return ERR_PTR(-EINVAL);
	gem = drm_gem_object_lookup(file, cmd->handles[0]);
	if (gem == NULL)
		return ERR_PTR(-ENOENT);
	error = -EINVAL;
	if (gem->dev != dev)
		goto fail;
	obj = to_drm_gem_cma_obj(gem);
	bytes = (uint64_t) cmd->pitches[0] * cmd->height;
	if (obj->dmat != sc->native.dmat || obj->dmamap == NULL ||
	    obj->dmamap->dm_nsegs != 1 || bytes == 0 || bytes > gem->size ||
	    bytes > obj->dmasize || bytes > obj->dmamap->dm_mapsize ||
	    bytes > obj->dmamap->dm_segs[0].ds_len)
		goto fail;
	address = obj->dmamap->dm_segs[0].ds_addr;
	if ((address & 63) != 0 || address > UINT32_MAX ||
	    bytes - 1 > UINT32_MAX - address)
		goto fail;
	fb = kmem_zalloc(sizeof(*fb), KM_SLEEP);
	fb->object = obj;
	if (file->driver_priv != NULL)
		fb->owner_id = ((const struct a733_drm_file *)file->driver_priv)->id;
	drm_helper_mode_fill_fb_struct(dev, &fb->base, cmd);
	error = drm_framebuffer_init(dev, &fb->base, &a733_fb_funcs);
	if (error == 0)
		return &fb->base;
	kmem_free(fb, sizeof(*fb));
fail:
	drm_gem_object_put_unlocked(gem);
	return ERR_PTR(error);
}

struct drm_gem_cma_object *
a733_drm_alloc_cma(struct a733_drm *sc, size_t size)
{
	struct drm_gem_cma_object *obj;
	vmem_addr_t probe;

	/* A fit proof avoids the helper's blocking fallback at exhaustion. */
	mutex_enter(&sc->pool_lock);
	if (sc->pool == NULL || sc->dev->cma_pool != sc->pool ||
	    vmem_xalloc(sc->pool, size, PAGE_SIZE, 0, 0, VMEM_ADDR_MIN,
	    VMEM_ADDR_MAX, VM_BESTFIT | VM_NOSLEEP, &probe) != 0) {
		mutex_exit(&sc->pool_lock);
		return NULL;
	}
	vmem_xfree(sc->pool, probe, size);
	obj = drm_gem_cma_create(sc->dev, size);
	mutex_exit(&sc->pool_lock);
	return obj;
}

static int
a733_dumb_create(struct drm_file *file, struct drm_device *dev,
    struct drm_mode_create_dumb *args)
{
	struct a733_drm *sc = dev->dev_private;
	struct drm_gem_cma_object *obj;
	uint64_t pitch, size;
	int error;

	if (args->bpp != 32 || args->width == 0 || args->width > 1920 ||
	    args->height == 0 || args->height > 1080 || args->flags != 0)
		return -EINVAL;
	pitch = ((uint64_t)args->width * 4 + 63) & ~UINT64_C(63);
	size = roundup(pitch * args->height, PAGE_SIZE);
	obj = a733_drm_alloc_cma(sc, size);
	if (obj == NULL)
		return -ENOMEM;
	if (file->driver_priv != NULL)
		obj->a733_allocation_id =
		    ((const struct a733_drm_file *)file->driver_priv)->id;
	error = drm_gem_handle_create(file, &obj->base, &args->handle);
	drm_gem_object_put_unlocked(&obj->base);
	if (error == 0) {
		args->pitch = pitch;
		args->size = size;
	}
	return error;
}

static bool
a733_format(struct drm_plane *plane, uint32_t format, uint64_t modifier)
{
	(void)plane;
	return format == DRM_FORMAT_XRGB8888 && modifier == DRM_FORMAT_MOD_LINEAR;
}

static const struct drm_plane_funcs a733_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = drm_plane_cleanup,
	.reset = drm_atomic_helper_plane_reset,
	.atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_plane_destroy_state,
	.format_mod_supported = a733_format,
};
/* The custom worker owns all plane preparation and hardware submission. */
static const struct drm_plane_helper_funcs a733_plane_helpers = {0};
/*
 * TV0 vblank interrupt callback. It runs in interrupt context under the
 * provider spin lock and counts every TCON_TV0 vblank since output start.
 */
static void
a733_vblank(void *arg, uint64_t sequence)
{
	struct a733_drm *sc = arg;
	ktime_t prior_time = 0, time = 0;
	uint64_t generation = 0, before = 0, count = 0;
	bool handled;
	mutex_spin_enter(&sc->vblank_history_lock);
	if (sc->vblank_history.active)
		generation = sc->vblank_history.generation;
	mutex_spin_exit(&sc->vblank_history_lock);

	/*
	 * The native IRQ lock serializes this callback. Do not hold the
	 * history lock across DRM event_lock or vblank_time_lock.
	 */
	if (generation != 0)
		before = drm_crtc_vblank_count_and_time(&sc->crtc, &prior_time);
	atomic_store_relaxed(&sc->hw_vblank, sequence);
	handled = drm_crtc_handle_vblank(&sc->crtc);
	if (generation == 0)
		return;
	if (handled)
		count = drm_crtc_vblank_count_and_time(&sc->crtc, &time);
	mutex_spin_enter(&sc->vblank_history_lock);
	if (generation != 0)
		a733_history_record(&sc->vblank_history, generation, sequence,
		    before, ktime_to_ns(prior_time), handled, count,
		    ktime_to_ns(time));
	mutex_spin_exit(&sc->vblank_history_lock);
}

void
a733_drm_vblank_history_reset(struct a733_drm *sc, bool enabled)
{
	mutex_spin_enter(&sc->vblank_history_lock);
	a733_history_reset(&sc->vblank_history, enabled);
	mutex_spin_exit(&sc->vblank_history_lock);
}

bool
a733_drm_vblank_stamp(struct a733_drm *sc, uint64_t raw,
    struct a733_vblank_stamp *stamp)
{
	bool found;

	mutex_spin_enter(&sc->vblank_history_lock);
	found = a733_history_lookup(&sc->vblank_history, raw, stamp);
	mutex_spin_exit(&sc->vblank_history_lock);
	return found;
}

static uint32_t
a733_vblank_counter(struct drm_crtc *crtc)
{
	struct a733_drm *sc = crtc->dev->dev_private;

	return (uint32_t)atomic_load_relaxed(&sc->hw_vblank);
}

/* DRM calls these hooks with its spin locks held. Do not take sc->lock. */
static int
a733_vblank_enable(struct drm_crtc *crtc)
{
	struct a733_drm *sc = crtc->dev->dev_private;
	const int fault = atomic_load_relaxed(&sc->fault);

	/* The provider interrupt stays armed while TV0 runs. */
	return fault != 0 ? -fault : sc->vblank_irq ? 0 : -EOPNOTSUPP;
}

static void
a733_vblank_disable(struct drm_crtc *crtc)
{
	(void)crtc;
}

static const struct drm_crtc_funcs a733_crtc_funcs = {
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.destroy = drm_crtc_cleanup,
	.reset = drm_atomic_helper_crtc_reset,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
	.get_vblank_counter = a733_vblank_counter,
	.enable_vblank = a733_vblank_enable,
	.disable_vblank = a733_vblank_disable,
};
static const struct drm_encoder_funcs a733_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static enum drm_connector_status
a733_detect(struct drm_connector *connector, bool force)
{
	struct a733_drm *sc = connector->dev->dev_private;
	bool connected;
	int error;

	(void)force;
	mutex_enter(&sc->lock);
	error = sc->fault;
	mutex_exit(&sc->lock);
	if (error == 0)
		error = sun60i_a733_de352_detect(sc->client, &connected);
	if (error != 0)
		return connector_status_unknown;
	return connected ? connector_status_connected : connector_status_disconnected;
}

static int
a733_dpms(struct drm_connector *connector, int mode)
{
	(void)connector;
	return mode == DRM_MODE_DPMS_ON ? 0 : -EOPNOTSUPP;
}

static void
a733_connector_destroy(struct drm_connector *connector)
{
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

static const struct drm_connector_funcs a733_connector_funcs = {
	.dpms = a733_dpms,
	.detect = a733_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = a733_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int
a733_get_modes(struct drm_connector *connector)
{
	struct a733_drm *sc = connector->dev->dev_private;
	const struct sun60i_a733_output_mode *n = &sc->native.mode;
	struct drm_display_mode *mode;

	mode = drm_mode_create(connector->dev);
	if (mode == NULL)
		return 0;
	mode->clock = n->pixel_clock_hz / 1000;
	mode->hdisplay = n->hdisplay;
	mode->hsync_start = n->hsync_start;
	mode->hsync_end = n->hsync_end;
	mode->htotal = n->htotal;
	mode->vdisplay = n->vdisplay;
	mode->vsync_start = n->vsync_start;
	mode->vsync_end = n->vsync_end;
	mode->vtotal = n->vtotal;
	mode->flags = (n->hsync_positive ? DRM_MODE_FLAG_PHSYNC : DRM_MODE_FLAG_NHSYNC) |
	    (n->vsync_positive ? DRM_MODE_FLAG_PVSYNC : DRM_MODE_FLAG_NVSYNC);
	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_set_name(mode);
	drm_mode_probed_add(connector, mode);
	return 1;
}

static enum drm_mode_status
a733_mode_valid(struct drm_connector *connector, struct drm_display_mode *mode)
{
	struct a733_drm *sc = connector->dev->dev_private;

	return a733_drm_mode_matches(&sc->native.mode, mode) ? MODE_OK : MODE_BAD;
}

static const struct drm_connector_helper_funcs a733_connector_helpers = {
	.get_modes = a733_get_modes,
	.mode_valid = a733_mode_valid,
};
static const struct drm_mode_config_funcs a733_mode_funcs = {
	.fb_create = a733_fb_create,
	.atomic_check = a733_drm_atomic_check,
	.atomic_commit = a733_drm_atomic_commit,
};
/*
 * A CPU alias disqualifies an imported object for private scanout.
 * Admission is serialized with its short source-copy lease. Existing
 * aliases continue to work with the ordinary scanout path.
 */
static bool
a733_expose_cpu(struct drm_gem_object *gem)
{
	struct a733_drm *sc = gem->dev->dev_private;
	struct drm_gem_cma_object *obj = to_drm_gem_cma_obj(gem);
	bool allowed;

	mutex_enter(&sc->lock);
	allowed = !obj->shadow_copying;
	if (allowed)
		obj->shadow_cpu_mapped = true;
	mutex_exit(&sc->lock);
	return allowed;
}

static int
a733_gem_mmap(struct drm_device *dev, off_t offset, size_t size, int prot,
    struct uvm_object **uobjp, voff_t *uoffsetp, struct file *file)
{
	struct drm_gem_object *gem;
	int error;

	error = drm_gem_mmap_object(dev, offset, size, prot, uobjp, uoffsetp,
	    file);
	if (error != 0)
		return error;
	if (*uobjp == NULL)
		return drm_legacy_mmap_object(dev, offset, size, prot, uobjp,
		    uoffsetp, file);
	gem = container_of(*uobjp, struct drm_gem_object, gemo_uvmobj);
	if (a733_expose_cpu(gem))
		return 0;
	drm_gem_object_put_unlocked(gem);
	*uobjp = NULL;
	return -EBUSY;
}

static void *
a733_prime_vmap(struct drm_gem_object *obj)
{

	if (!a733_expose_cpu(obj))
		return NULL;
	return drm_gem_cma_prime_vmap(obj);
}

/* The authorized PRIME offset is relative to this CMA object, in bytes. */
static int
a733_prime_mmap(struct drm_gem_object *obj, off_t * offp, size_t size,
    int prot, int *flagsp, int *advicep, struct uvm_object **uobjp,
    int *maxprotp)
{
	(void)advicep;
	if (*offp < 0 || ((uint64_t) * offp & (PAGE_SIZE - 1)) != 0 ||
	    size == 0 || (size & (PAGE_SIZE - 1)) != 0 ||
	    (uint64_t) * offp > obj->size || size > obj->size - (size_t) * offp ||
	    (prot & ~(PROT_READ | PROT_WRITE)) != 0 ||
	    (*flagsp & MAP_SHARED) == 0 || (*flagsp & MAP_PRIVATE) != 0)
		return -EINVAL;
	if (!a733_expose_cpu(obj))
		return -EBUSY;
	drm_gem_object_get(obj);
	*uobjp = &obj->gemo_uvmobj;
	*maxprotp = PROT_READ | PROT_WRITE;
	return 0;
}

/*
 * All first exports of local CMA storage pass here under the PRIME core's
 * object_name_lock. Remember exposure for the lifetime of the GEM object.
 */
static struct dma_buf *
a733_prime_export(struct drm_gem_object *gem, int flags)
{
	struct a733_drm *sc = gem->dev->dev_private;
	struct drm_gem_cma_object *obj = to_drm_gem_cma_obj(gem);

	mutex_enter(&sc->lock);
	if (obj->shadow_copying) {
		mutex_exit(&sc->lock);
		return ERR_PTR(-EBUSY);
	}
	obj->a733_prime_exposed = true;
	mutex_exit(&sc->lock);

	return drm_gem_prime_export(gem, flags);
}

static struct drm_driver a733_driver = {
	.driver_features = DRIVER_MODESET | DRIVER_ATOMIC | DRIVER_GEM,
	.open = a733_file_open,
	.postclose = a733_file_close,
	.gem_free_object = drm_gem_cma_free_object,
	.mmap_object = a733_gem_mmap,
	.gem_uvm_ops = &drm_gem_cma_uvm_ops,
	.dumb_create = a733_dumb_create,
	.dumb_destroy = drm_gem_dumb_destroy,
	.prime_handle_to_fd = drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle = drm_gem_prime_fd_to_handle,
	.gem_prime_export = a733_prime_export,
	.gem_prime_mmap = a733_prime_mmap,
	.gem_prime_get_sg_table = drm_gem_cma_prime_get_sg_table,
	.gem_prime_import_sg_table = drm_gem_cma_prime_import_sg_table,
	.gem_prime_vmap = a733_prime_vmap,
	.gem_prime_vunmap = drm_gem_cma_prime_vunmap,
	.name = "sun60idrm",
	.desc = "A733 fixed-mode native KMS",
	.date = "20261019",
	.major = 1,
	.minor = 0,
	.patchlevel = 0,
};

static int
a733_register(struct a733_drm *sc)
{
	static const uint32_t formats[] = {DRM_FORMAT_XRGB8888};
	static const uint64_t modifiers[] = {
		DRM_FORMAT_MOD_LINEAR, DRM_FORMAT_MOD_INVALID
	};
	struct drm_device *dev;
	struct sun60i_a733_tcon_timing timing;
	int error, release_error;
	extern int drm_guarantee_initialized(void);

	error = sun60i_a733_de352_get_state(sc->phandle, &sc->native);
	if (error != 0)
		return error;
	if (sc->native.dmat == NULL || sc->native.max_dma_address != UINT32_MAX ||
	    sc->native.mode.pixel_clock_hz % 1000 != 0 ||
	    sun60i_a733_tcon_timing_encode(&sc->native.mode, &timing) != 0)
		return EINVAL;
	/* Dumb buffers come only from the boot-time contiguous reservation. */
	if (sc->pool == NULL)
		return ENOMEM;
	error = sun60i_a733_de352_acquire(sc->phandle, sc, &sc->client);
	if (error != 0)
		return error;
	sc->registration_started = true;
	error = drm_guarantee_initialized();
	if (error != 0)
		goto release;
	dev = drm_dev_alloc(&a733_driver, sc->device);
	if (IS_ERR(dev)) {
		error = -PTR_ERR(dev);
		goto release;
	}
	sc->dev = dev;
	dev->dev_private = sc;
	dev->cma_pool = sc->pool;
	dev->bus_dmat = dev->bus_dmat32 = dev->dmat = sc->native.dmat;
	dev->dmat_subregion_p = false;
	dev->native_platform.version = DRM_NATIVE_PLATFORM_VERSION;
	dev->native_platform.length = sizeof(dev->native_platform);
	dev->native_platform.bus_type = DRM_NATIVE_BUS_PLATFORM;
	strlcpy(dev->native_platform.fullname, device_xname(sc->device),
	    sizeof(dev->native_platform.fullname));
	strlcpy(dev->native_platform.compatible, "allwinner,sun60i-a733-display",
	    sizeof(dev->native_platform.compatible));
	drm_mode_config_init(dev);
	dev->mode_config.funcs = &a733_mode_funcs;
	dev->mode_config.min_width = sc->native.mode.hdisplay;
	dev->mode_config.max_width = sc->native.mode.hdisplay;
	dev->mode_config.min_height = sc->native.mode.vdisplay;
	dev->mode_config.max_height = sc->native.mode.vdisplay;
	error = -drm_universal_plane_init(dev, &sc->plane, 1, &a733_plane_funcs,
	    formats, __arraycount(formats), modifiers, DRM_PLANE_TYPE_PRIMARY, NULL);
	if (error != 0)
		goto mode;
	drm_plane_helper_add(&sc->plane, &a733_plane_helpers);
	error = -drm_crtc_init_with_planes(dev, &sc->crtc, &sc->plane, NULL,
	    &a733_crtc_funcs, NULL);
	if (error != 0)
		goto mode;
	error = -drm_encoder_init(dev, &sc->encoder, &a733_encoder_funcs,
	    DRM_MODE_ENCODER_TMDS, NULL);
	if (error != 0)
		goto mode;
	sc->encoder.possible_crtcs = drm_crtc_mask(&sc->crtc);
	error = -drm_connector_init(dev, &sc->connector, &a733_connector_funcs,
	    DRM_MODE_CONNECTOR_HDMIA);
	if (error != 0)
		goto mode;
	sc->connector.polled = DRM_CONNECTOR_POLL_CONNECT |
	    DRM_CONNECTOR_POLL_DISCONNECT;
	drm_connector_helper_add(&sc->connector, &a733_connector_helpers);
	error = -drm_connector_attach_encoder(&sc->connector, &sc->encoder);
	if (error != 0)
		goto mode;
	drm_mode_config_reset(dev);
	if (sc->plane.state == NULL || sc->crtc.state == NULL ||
	    sc->connector.state == NULL) {
		error = ENOMEM;
		goto mode;
	}
	/* Vblank storage exists before the callback and drm_dev_register. */
	error = -drm_vblank_init(dev, 1);
	if (error != 0)
		goto mode;
	dev->max_vblank_count = UINT32_MAX;
	error = sun60i_a733_de352_set_vblank_handler(sc->client, a733_vblank, sc);
	if (error == 0) {
		sc->vblank_irq = true;
		dev->irq_enabled = true;
	} else if (error != EOPNOTSUPP)
		goto mode;
	error = -drm_dev_register(dev, 0);
	if (error == 0) {
		drm_kms_helper_poll_init(dev);
		return 0;
	}
mode:
	/* No callback can use the DRM vblank storage after this removal. */
	if (sc->vblank_irq) {
		a733_drm_vblank_history_reset(sc, false);
		(void)sun60i_a733_de352_set_vblank_handler(sc->client, NULL, NULL);
		sc->vblank_irq = false;
		dev->irq_enabled = false;
	}
	drm_mode_config_cleanup(dev);
	drm_dev_put(dev);
	sc->dev = NULL;
release:
	release_error = sun60i_a733_de352_release(sc->client);
	if (release_error == 0)
		sc->client = NULL;
	else if (error == 0)
		error = release_error;
	return error;
}

/* Registration and cold-start share one owner; never hold it across DRM. */
static int
a733_start(struct a733_drm *sc, bool *busy)
{
	int error;

	if (busy != NULL)
		*busy = false;
	mutex_enter(&sc->lock);
	error = sc->fault;
	if (error == 0 && sc->stage == 1) {
		mutex_exit(&sc->lock);
		return 0;
	}
	if (error == 0 && sc->registering) {
		if (busy != NULL)
			*busy = true;
		error = EBUSY;
	}
	if (error == 0)
		sc->registering = true;
	mutex_exit(&sc->lock);
	if (error != 0)
		return error;
	error = sun60i_a733_de352_start(sc->phandle);
	if (error == 0) {
		error = a733_register(sc);
		/* Only the pre-registration sink probe can request a retry. */
		if (error == EAGAIN)
			error = EIO;
	}
	mutex_enter(&sc->lock);
	sc->registering = false;
	if (error == 0)
		sc->stage = 1;
	else if (error != EAGAIN && sc->fault == 0)
		sc->fault = error;
	mutex_exit(&sc->lock);
	mutex_spin_enter(&sc->startup_lock);
	if (error != EAGAIN) {
		sc->startup_registered = error == 0;
		sc->startup_stopped = true;
	}
	mutex_spin_exit(&sc->startup_lock);
	return error;
}

/* Softclock callback: enqueue only. The worker owns all sleeping work. */
static void
a733_startup_enqueue(void *arg)
{
	struct a733_drm *sc = arg;
	bool enqueue = false;

	mutex_spin_enter(&sc->startup_lock);
	if (!sc->startup_stopped && !sc->startup_registered &&
	    !sc->startup_queued && !sc->startup_running) {
		sc->startup_queued = true;
		enqueue = true;
	}
	mutex_spin_exit(&sc->startup_lock);
	if (enqueue)
		workqueue_enqueue(sc->queue, &sc->startup_work, sc->worker_cpu);
}

static void
a733_startup_run(struct a733_drm *sc)
{
	bool busy, retry;
	int error;

	mutex_spin_enter(&sc->startup_lock);
	sc->startup_queued = false;
	if (sc->startup_stopped || sc->startup_registered) {
		mutex_spin_exit(&sc->startup_lock);
		return;
	}
	sc->startup_running = true;
	mutex_spin_exit(&sc->startup_lock);
	error = a733_start(sc, &busy);
	mutex_spin_enter(&sc->startup_lock);
	sc->startup_running = false;
	if (error != 0 && error != EAGAIN && !busy)
		sc->startup_stopped = true;
	retry = (error == EAGAIN || busy) && !sc->startup_stopped;
	mutex_spin_exit(&sc->startup_lock);
	if (retry)
		callout_schedule(&sc->startup_timer, hz);
}

static void
a733_dispatch(struct work *work, void *arg)
{
	struct a733_drm *sc = arg;

	if (work == &sc->startup_work)
		a733_startup_run(sc);
	else
		a733_drm_work(work, sc);
}

static void
a733_mountroot(device_t self)
{
	a733_startup_enqueue(device_private(self));
}

static int
a733_startup_init(struct a733_drm *sc, bool observe_only)
{
	int error;

	mutex_init(&sc->startup_lock, MUTEX_DEFAULT, IPL_SOFTCLOCK);
	callout_init(&sc->startup_timer, CALLOUT_MPSAFE);
	callout_setfunc(&sc->startup_timer, a733_startup_enqueue, sc);
	sc->startup_stopped = observe_only;
	error = workqueue_create(&sc->queue, "a733kms", a733_dispatch, sc,
	    PRI_NONE, IPL_SOFTCLOCK, WQ_MPSAFE |
	    (sc->worker_cpu != NULL ? WQ_PERCPU : 0));
	if (error != 0) {
		sc->startup_stopped = true;
		return error;
	}
	if (!observe_only)
		config_mountroot(sc->device, a733_mountroot);
	return 0;
}

static int
a733_stage(SYSCTLFN_ARGS)
{
	struct a733_drm *sc = container_of((int *)rnode->sysctl_data,
	    struct a733_drm, stage);
	struct sysctlnode node = *rnode;
	int stage, error;

	mutex_enter(&sc->lock);
	stage = sc->stage;
	mutex_exit(&sc->lock);
	node.sysctl_data = &stage;
	node.sysctl_size = sizeof(stage);
	error = sysctl_lookup(SYSCTLFN_CALL(&node));
	if (error != 0 || newp == NULL)
		return error;
	if (stage != 1)
		return EINVAL;
	/* Registration creates sysctl nodes. Keep the tree lock outside it. */
	sysctl_unlock();
	error = a733_start(sc, NULL);
	sysctl_relock();
	return error;
}

static int
a733_fault(SYSCTLFN_ARGS)
{
	struct a733_drm *sc = container_of((int *)rnode->sysctl_data,
	    struct a733_drm, fault);
	struct sysctlnode node = *rnode;
	int fault;

	mutex_enter(&sc->lock);
	fault = sc->fault;
	mutex_exit(&sc->lock);
	node.sysctl_data = &fault;
	node.sysctl_size = sizeof(fault);
	return sysctl_lookup(SYSCTLFN_CALL(&node));
}

static int
a733_match(device_t parent, cfdata_t cf, void *aux)
{
	static const struct device_compatible_entry compat[] = {
		{ .compat = "allwinner,sun60i-a733-display" },
		DEVICE_COMPAT_EOL
	};
	const struct fdt_attach_args *faa = aux;
	(void)parent;
	(void)cf;
	return of_compatible_match(faa->faa_phandle, compat);
}

/*
 * Reserve contiguous scanout memory below 4 GiB during attach. A later
 * contiguous allocation can fail after memory use spreads.
 */
static void
a733_pool_create(struct a733_drm *sc, bus_dma_tag_t parent)
{
	int error, nsegs = 0;
	const bus_size_t bytes = sc->shadow_enabled ?
	    UINT64_C(64) << 20 : A733_DRM_POOL_BYTES;

	error = bus_dmatag_subregion(parent, 0, A733_DRM_POOL_LIMIT - 1,
	    &sc->pool_tag, BUS_DMA_NOWAIT);
	if (error == 0) {
		error = bus_dmamem_alloc(sc->pool_tag, bytes,
		    PAGE_SIZE, 0, &sc->pool_segment, 1, &nsegs, BUS_DMA_NOWAIT);
		if (error == 0 && (nsegs != 1 ||
		    sc->pool_segment.ds_len != bytes ||
		    sc->pool_segment.ds_addr >
		    A733_DRM_POOL_LIMIT - bytes)) {
			bus_dmamem_free(sc->pool_tag, &sc->pool_segment, nsegs);
			error = ERANGE;
		}
		if (error != 0) {
			bus_dmatag_destroy(sc->pool_tag);
			sc->pool_tag = NULL;
		}
	}
	if (error != 0) {
		aprint_error_dev(sc->device,
		    "KMS buffer reservation failed, error %d\n", error);
		return;
	}
	sc->pool = vmem_create("a733kms", sc->pool_segment.ds_addr,
	    sc->pool_segment.ds_len, PAGE_SIZE, NULL, NULL, NULL, 0, VM_SLEEP,
	    IPL_NONE);
	aprint_normal_dev(sc->device, "reserved %u MiB for KMS buffers\n",
	    (u_int)(bytes >> 20));
}

static void
a733_attach(device_t parent, device_t self, void *aux)
{
	struct a733_drm *sc = device_private(self);
	const struct fdt_attach_args *faa = aux;
	const struct sysctlnode *node;
	int error;

	(void)parent;
	sc->device = self;
	mutex_init(&sc->lock, MUTEX_DEFAULT, IPL_NONE);
	cv_init(&sc->idle, "a733idle");
	mutex_init(&sc->vblank_history_lock, MUTEX_DEFAULT, IPL_VM);
	mutex_init(&sc->pool_lock, MUTEX_DEFAULT, IPL_NONE);
	sc->phandle = fdtbus_get_phandle(faa->faa_phandle, "display-engine");
	aprint_naive("\n");
	aprint_normal(": native KMS frontend\n");
	if (sc->phandle <= 0)
		return;
	sc->shadow_enabled = of_hasprop(faa->faa_phandle,
	    "emberbsd,shadow-scanout");
	a733_pool_create(sc, faa->faa_dmat);
	/* Use the same recovery convention as the other native A733 owners. */
	error = OF_getproplen(faa->faa_phandle, "netbsd,observe-only");
	if (error > 0) {
		sc->fault = EINVAL;
		sc->startup_stopped = true;
		return;
	}
	uint32_t cpu;
	if (of_getprop_uint32(faa->faa_phandle, "emberbsd,kms-worker-cpu",
	    &cpu) == 0 && cpu < ncpu) {
		sc->worker_cpu = cpu_lookup(cpu);
		if (sc->worker_cpu != NULL)
			aprint_normal_dev(self, "KMS work on CPU %u\n", cpu);
	}
	error = a733_startup_init(sc, error == 0);
	if (error != 0) {
		sc->fault = error;
		return;
	}
	error = sysctl_createv(&sc->sysctllog, 0, NULL, &node, 0, CTLTYPE_NODE,
	    device_xname(self), NULL, NULL, 0, NULL, 0, CTL_HW, CTL_CREATE, CTL_EOL);
	if (error != 0)
		return;
	error = sysctl_createv(&sc->sysctllog, 0, &node, NULL, CTLFLAG_READWRITE,
	    CTLTYPE_INT, "stage", SYSCTL_DESCR("Register KMS after accepted native scanout"),
	    a733_stage, 0, &sc->stage, sizeof(sc->stage), CTL_CREATE, CTL_EOL);
	if (error == 0)
		error = sysctl_createv(&sc->sysctllog, 0, &node, NULL, CTLFLAG_READONLY,
		    CTLTYPE_INT, "fault", NULL, a733_fault, 0, &sc->fault,
		    sizeof(sc->fault), CTL_CREATE, CTL_EOL);
	if (error != 0)
		sysctl_teardown(&sc->sysctllog);
}

CFATTACH_DECL_NEW(sun60i_a733_drm, sizeof(struct a733_drm),
    a733_match, a733_attach, NULL, NULL);
