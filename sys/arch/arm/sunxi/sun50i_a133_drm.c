/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD, AI-assisted. Fixed-mode KMS for the A133 MIPI DSI panel.
 *
 * The Allwinner boot loader configures the whole output path: DE2 mixer 0,
 * TCON LCD0 in CPU/DSI trigger mode and the MIPI DSI host in video mode. This
 * driver accepts that path as it is and changes only the scanout address of
 * the layer the boot loader enabled. It never touches clocks, resets, TCON
 * timing or DSI.
 *
 * Page flips are latched in the TCON TRI_FINISH interrupt, which fires once
 * at the end of every frame transfer (every 12 ms). The new address is
 * written there, before the next transfer starts, and the commit completes
 * at once: the new buffer is scanned out from that frame on. The buffer it
 * replaced is read no more from the same frame on; a client that rotates
 * three buffers reuses it a frame later, which also covers interrupt
 * latency. Disabling the plane or the CRTC returns the panel to the boot
 * framebuffer, so a client that exits leaves the boot splash visible.
 */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");
#include <sys/param.h>
#include <sys/atomic.h>
#include <sys/bus.h>
#include <sys/condvar.h>
#include <sys/container_of.h>
#include <sys/device.h>
#include <sys/intr.h>
#include <sys/kmem.h>
#include <sys/mman.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/vmem.h>
#include <sys/workqueue.h>
#include <dev/fdt/fdtvar.h>
#include <uvm/uvm_extern.h>
#include <linux/completion.h>
#include <linux/dma-fence.h>
#include <linux/jiffies.h>
#include <linux/ktime.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_encoder.h>
#include <drm/drm_file.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_modes.h>
#include <drm/drm_modeset_helper.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_plane.h>
#include <drm/drm_prime.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>

/* Register windows, from the vendor "allwinner,sunxi-disp" node. */
#define A133_MIXER0_BASE	0x06100000	/* DE2 mixer 0 */
#define A133_MIXER0_SIZE	0x4000
#define A133_TCON_REG_INDEX	4		/* TCON LCD0, 0x06511000 */
#define A133_TCON_SIZE		0x1000
#define A133_TCON_INTR_INDEX	0		/* SPI 69, TCON LCD0 */

/* DE2 mixer 0 global block. */
#define GLB_CTL			0x0000
#define  GLB_CTL_EN			__BIT(0)
#define GLB_DBUFF		0x0008
#define  GLB_DBUFF_RDY			__BIT(0)
#define GLB_SIZE		0x000c
/* The layer the boot loader scans out: channel at 0x3000, layer 0. */
#define LAY_ATTR		0x3000
#define  LAY_ATTR_EN			__BIT(0)
#define  LAY_ATTR_FMT			__BITS(12, 8)
#define  LAY_ATTR_FMT_ARGB8888		0
#define LAY_SIZE		0x3004
#define LAY_PITCH		0x300c
#define LAY_TOP_LADDR		0x3018

/* TCON LCD0. */
#define TCON_GINT0		0x004
#define  TCON_GINT0_TRI_FINISH_EN	__BIT(27)
#define  TCON_GINT0_TRI_FINISH		__BIT(11)
#define TCON_CPU_IF		0x060
#define  TCON_CPU_IF_MODE		__BITS(31, 28)
#define  TCON_CPU_IF_MODE_DSI		1
#define  TCON_CPU_IF_TRI_EN		__BIT(0)

#define A133_WIDTH		800
#define A133_HEIGHT		1280
#define A133_PITCH		(A133_WIDTH * 4)
#define A133_FRAME_BYTES	(A133_PITCH * A133_HEIGHT)
#define A133_SIZE_REG		(((A133_HEIGHT - 1) << 16) | (A133_WIDTH - 1))
/*
 * <linux/mm.h> redefines PAGE_MASK, which breaks round_page() here. A frame
 * is a whole number of pages, so it needs no rounding.
 */
__CTASSERT(A133_FRAME_BYTES % PAGE_SIZE == 0);
/*
 * One scanout buffer: a frame plus slack. Mesa PowerVR requires a buffer
 * bound to imported memory to be a page larger than its size (4100096 bytes
 * for this frame, measured on OFFICE), so a dma-buf of exactly one frame
 * cannot back a full-frame copy. sun50i_a133_fdt.c reserves three of these.
 */
#define A133_BO_BYTES		(A133_FRAME_BYTES + 65536)

/* Scanout buffers live in a reservation below 4 GiB (sun50i_a133_fdt.c). */
#define A133_POOL_LIMIT		(UINT64_C(1) << 32)

/* Bounds for one flip. A frame takes 12 ms. */
#define A133_FLIP_TIMEOUT_MS	100
#define A133_POLL_FRAME_MS	30

enum a133_flip_state {
	FLIP_IDLE,
	FLIP_ARMED,	/* the interrupt writes the address at TRI_FINISH */
	FLIP_DONE,
};

struct a133_drm {
	device_t device;
	bus_space_tag_t bst;
	bus_space_handle_t mixer, tcon;
	int phandle;
	void *ih;

	kmutex_t lock;			/* fault, busy */
	kcondvar_t idle_cv;		/* busy cleared */
	int fault;
	bool busy;
	/* Commit worker only: the GEM object the panel scans out, held. */
	struct drm_gem_object *shown;

	bus_dma_tag_t dmat;
	int pool_frames;
	vmem_t *pool;

	/* Interrupt state, under intr_lock. */
	kmutex_t intr_lock;
	kcondvar_t flip_cv;
	enum a133_flip_state flip;
	uint32_t flip_addr;
	uint64_t frames;		/* TRI_FINISH count while enabled */
	uint64_t shown_frame;
	uint64_t shown_ns;
	bool vblank_ready;
	unsigned vblank_users;		/* DRM vblank and flips in flight */

	bool vblank_irq;		/* interrupt verified at registration */
	bool vblank_on;			/* commit worker only */
	uint32_t boot_addr;

	struct workqueue *queue;
	struct drm_device *dev;
	struct drm_plane plane;
	struct drm_crtc crtc;
	struct drm_encoder encoder;
	struct drm_connector connector;

	struct sysctllog *sysctllog;
	uint64_t flips, flip_timeouts;	/* commit worker only */
};

struct a133_drm_fb {
	struct drm_framebuffer base;
	struct drm_gem_cma_object *object;
};

struct a133_commit {
	struct work work;
	struct completion done;
	struct drm_atomic_state *state;
	bool nonblock;
};

#define MIXER_READ(sc, r)	bus_space_read_4((sc)->bst, (sc)->mixer, (r))
#define MIXER_WRITE(sc, r, v)	bus_space_write_4((sc)->bst, (sc)->mixer, (r), (v))
#define TCON_READ(sc, r)	bus_space_read_4((sc)->bst, (sc)->tcon, (r))
#define TCON_WRITE(sc, r, v)	bus_space_write_4((sc)->bst, (sc)->tcon, (r), (v))

/*
 * The panel takes a frame every 11.99 ms (measured on two units). The DSI
 * host has VT 1320, VBP 24, VSA 8; horizontal blanking is not exposed, so
 * the horizontal timing below only makes the nominal refresh 83.4 Hz.
 */
static const struct drm_display_mode a133_mode = {
	DRM_MODE("800x1280", DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
	    96876, 800, 840, 860, 880, 0, 1280, 1288, 1296, 1320, 0,
	    DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC)
};

/* ------------------------------------------------------------------ */
/* Interrupt and flips */

static void
a133_tri_enable_locked(struct a133_drm *sc, bool on)
{
	uint32_t gint;

	KASSERT(mutex_owned(&sc->intr_lock));
	gint = TCON_READ(sc, TCON_GINT0);
	/* Writing 0 clears a status flag; keep pending flags as they are. */
	if (on)
		gint = (gint | TCON_GINT0_TRI_FINISH_EN) & ~TCON_GINT0_TRI_FINISH;
	else
		gint &= ~TCON_GINT0_TRI_FINISH_EN;
	TCON_WRITE(sc, TCON_GINT0, gint);
}

static void
a133_users_locked(struct a133_drm *sc, int delta)
{
	const bool was = sc->vblank_users > 0;

	sc->vblank_users += delta;
	if ((sc->vblank_users > 0) != was)
		a133_tri_enable_locked(sc, !was);
}

static void
a133_latch(struct a133_drm *sc, uint32_t addr)
{
	MIXER_WRITE(sc, LAY_TOP_LADDR, addr);
	MIXER_WRITE(sc, GLB_DBUFF, GLB_DBUFF_RDY);
}

/* Returns true when this was a frame boundary. Caller holds intr_lock. */
static bool
a133_frame_locked(struct a133_drm *sc)
{
	const uint32_t gint = TCON_READ(sc, TCON_GINT0);

	if ((gint & TCON_GINT0_TRI_FINISH) == 0)
		return false;
	TCON_WRITE(sc, TCON_GINT0, gint & ~TCON_GINT0_TRI_FINISH);
	sc->frames++;
	switch (sc->flip) {
	case FLIP_ARMED:
		/* The next transfer has not started yet: latch now. */
		a133_latch(sc, sc->flip_addr);
		sc->shown_frame = sc->frames;
		sc->shown_ns = ktime_get_ns();
		sc->flip = FLIP_DONE;
		cv_broadcast(&sc->flip_cv);
		break;
	default:
		break;
	}
	return true;
}

static int
a133_intr(void *arg)
{
	struct a133_drm *sc = arg;
	bool frame, vblank;

	mutex_spin_enter(&sc->intr_lock);
	frame = a133_frame_locked(sc);
	vblank = frame && sc->vblank_ready;
	mutex_spin_exit(&sc->intr_lock);
	if (vblank)
		(void)drm_crtc_handle_vblank(&sc->crtc);
	return frame;
}

/*
 * Show addr from the next frame on. Returns when the address is latched,
 * with the frame number and time the new buffer started to be scanned out.
 * A flip that does not complete in time is latched directly and counted;
 * the display keeps working. Sleeps; only the commit worker calls it.
 */
static void
a133_flip(struct a133_drm *sc, uint32_t addr, uint64_t *frame, uint64_t *ns)
{
	int error = 0;

	mutex_spin_enter(&sc->intr_lock);
	if (sc->vblank_irq)
		a133_users_locked(sc, 1);
	else {
		/* Polling: drop a flag left from an earlier frame. */
		TCON_WRITE(sc, TCON_GINT0,
		    TCON_READ(sc, TCON_GINT0) & ~TCON_GINT0_TRI_FINISH);
	}
	sc->flip_addr = addr;
	sc->flip = FLIP_ARMED;
	if (sc->vblank_irq) {
		while (sc->flip != FLIP_DONE && error == 0)
			error = cv_timedwait(&sc->flip_cv, &sc->intr_lock,
			    MAX(1, mstohz(A133_FLIP_TIMEOUT_MS)));
	} else {
		/*
		 * No verified interrupt: poll the same flag. A poll can see it
		 * late, after the next transfer started, so the address may take
		 * effect a frame later; wait for one more frame boundary before
		 * calling the old buffer free.
		 */
		const uint64_t deadline = ktime_get_ns() +
		    (uint64_t)A133_FLIP_TIMEOUT_MS * 1000000;
		int boundaries = 0;

		while (boundaries < 2) {
			if (a133_frame_locked(sc))
				boundaries++;
			if (boundaries >= 2)
				break;
			if (ktime_get_ns() > deadline) {
				error = ETIMEDOUT;
				break;
			}
			mutex_spin_exit(&sc->intr_lock);
			kpause("a133flp", false, 1, NULL);
			mutex_spin_enter(&sc->intr_lock);
		}
	}
	if (sc->flip == FLIP_ARMED) {
		a133_latch(sc, addr);
		sc->shown_frame = sc->frames;
		sc->shown_ns = ktime_get_ns();
	}
	*frame = sc->shown_frame;
	*ns = sc->shown_ns;
	sc->flip = FLIP_IDLE;
	if (sc->vblank_irq)
		a133_users_locked(sc, -1);
	mutex_spin_exit(&sc->intr_lock);
	if (sc->flip_timeouts == 0 && error != 0)
		aprint_error_dev(sc->device, "flip timed out, latched directly\n");
	atomic_inc_64(error != 0 ? &sc->flip_timeouts : &sc->flips);
}

/* ------------------------------------------------------------------ */
/* Framebuffers and buffers */

static void
a133_fb_destroy(struct drm_framebuffer *base)
{
	struct a133_drm_fb *fb = container_of(base, struct a133_drm_fb, base);

	drm_framebuffer_cleanup(base);
	drm_gem_object_put_unlocked(&fb->object->base);
	kmem_free(fb, sizeof(*fb));
}

static int
a133_fb_handle(struct drm_framebuffer *base, struct drm_file *file,
    unsigned int *handle)
{
	struct a133_drm_fb *fb = container_of(base, struct a133_drm_fb, base);

	return drm_gem_handle_create(file, &fb->object->base, handle);
}

static const struct drm_framebuffer_funcs a133_fb_funcs = {
	.destroy = a133_fb_destroy,
	.create_handle = a133_fb_handle,
};

static struct drm_framebuffer *
a133_fb_create(struct drm_device *dev, struct drm_file *file,
    const struct drm_mode_fb_cmd2 *cmd)
{
	struct a133_drm *sc = dev->dev_private;
	struct drm_gem_object *gem;
	struct drm_gem_cma_object *obj;
	struct a133_drm_fb *fb;
	uint64_t bytes, address;
	int error;

	/* The layer keeps the boot loader's format, size and pitch. */
	if (cmd->pixel_format != DRM_FORMAT_XRGB8888 ||
	    cmd->width != A133_WIDTH || cmd->height != A133_HEIGHT ||
	    cmd->pitches[0] != A133_PITCH || cmd->offsets[0] != 0 ||
	    (cmd->flags & ~DRM_MODE_FB_MODIFIERS) != 0 ||
	    ((cmd->flags & DRM_MODE_FB_MODIFIERS) != 0 &&
	    cmd->modifier[0] != DRM_FORMAT_MOD_LINEAR))
		return ERR_PTR(-EINVAL);
	for (u_int i = 1; i < 4; i++)
		if (cmd->handles[i] || cmd->pitches[i] || cmd->offsets[i] ||
		    cmd->modifier[i])
			return ERR_PTR(-EINVAL);
	gem = drm_gem_object_lookup(file, cmd->handles[0]);
	if (gem == NULL)
		return ERR_PTR(-ENOENT);
	error = -EINVAL;
	if (gem->dev != dev)
		goto fail;
	obj = to_drm_gem_cma_obj(gem);
	bytes = A133_FRAME_BYTES;
	if (obj->dmat != sc->dmat || obj->vmem_pool != sc->pool ||
	    obj->dmamap == NULL ||
	    obj->dmamap->dm_nsegs != 1 || bytes > gem->size ||
	    bytes > obj->dmamap->dm_segs[0].ds_len)
		goto fail;
	address = obj->dmamap->dm_segs[0].ds_addr;
	if ((address & 63) != 0 || address + bytes > A133_POOL_LIMIT)
		goto fail;
	fb = kmem_zalloc(sizeof(*fb), KM_SLEEP);
	fb->object = obj;
	drm_helper_mode_fill_fb_struct(dev, &fb->base, cmd);
	error = drm_framebuffer_init(dev, &fb->base, &a133_fb_funcs);
	if (error == 0)
		return &fb->base;
	kmem_free(fb, sizeof(*fb));
fail:
	drm_gem_object_put_unlocked(gem);
	return ERR_PTR(error);
}

/*
 * A scanout buffer from the reservation. The reservation is outside the
 * kernel direct map, and drm_gem_cma_create() would map it through
 * bus_dmamem_map(), which also cleans the direct-map alias of each page and
 * faults there. Map it write-combining only; drm_gem_cma_free_object()
 * undoes exactly this (bus_dmamem_unmap of a non-direct mapping, vmem free).
 */
static struct drm_gem_cma_object *
a133_bo_create(struct a133_drm *sc, size_t size)
{
	struct drm_device *dev = sc->dev;
	struct drm_gem_cma_object *obj;
	vaddr_t va;

	obj = kmem_zalloc(sizeof(*obj), KM_SLEEP);
	obj->dmat = dev->dmat;
	obj->dmasize = size;
	if (vmem_xalloc(sc->pool, size, PAGE_SIZE, 0, 0, VMEM_ADDR_MIN,
	    VMEM_ADDR_MAX, VM_BESTFIT | VM_NOSLEEP, &obj->vmem_addr) != 0) {
		kmem_free(obj, sizeof(*obj));
		return NULL;
	}
	obj->vmem_pool = sc->pool;
	/* The display DMA tag has no address translation. */
	obj->dmasegs[0].ds_addr = obj->vmem_addr;
	obj->dmasegs[0].ds_len = size;
	va = uvm_km_alloc(kernel_map, size, 0, UVM_KMF_VAONLY | UVM_KMF_WAITVA);
	for (size_t off = 0; off < size; off += PAGE_SIZE)
		pmap_kenter_pa(va + off, obj->vmem_addr + off,
		    VM_PROT_READ | VM_PROT_WRITE, PMAP_WRITE_COMBINE);
	pmap_update(pmap_kernel());
	obj->vaddr = (void *)va;
	if (bus_dmamap_create(obj->dmat, size, 1, size, 0, BUS_DMA_WAITOK,
	    &obj->dmamap) != 0)
		goto fail;
	if (bus_dmamap_load_raw(obj->dmat, obj->dmamap, obj->dmasegs, 1, size,
	    BUS_DMA_WAITOK) != 0) {
		bus_dmamap_destroy(obj->dmat, obj->dmamap);
		goto fail;
	}
	memset(obj->vaddr, 0, size);
	drm_gem_private_object_init(dev, &obj->base, size);
	return obj;
fail:
	pmap_kremove(va, size);
	pmap_update(pmap_kernel());
	uvm_km_free(kernel_map, va, size, UVM_KMF_VAONLY);
	vmem_xfree(sc->pool, obj->vmem_addr, size);
	kmem_free(obj, sizeof(*obj));
	return NULL;
}

static int
a133_dumb_create(struct drm_file *file, struct drm_device *dev,
    struct drm_mode_create_dumb *args)
{
	struct a133_drm *sc = dev->dev_private;
	struct drm_gem_cma_object *obj;
	int error;

	if (args->bpp != 32 || args->width != A133_WIDTH ||
	    args->height != A133_HEIGHT || args->flags != 0)
		return -EINVAL;
	obj = a133_bo_create(sc, A133_BO_BYTES);
	if (obj == NULL)
		return -ENOMEM;
	error = drm_gem_handle_create(file, &obj->base, &args->handle);
	drm_gem_object_put_unlocked(&obj->base);
	if (error == 0) {
		args->pitch = A133_PITCH;
		args->size = A133_BO_BYTES;
	}
	return error;
}

/*
 * The reserved pages have no vm_page, so they cannot be described as a page
 * list. Importers take the physical segment (pvr_gem_prime_import).
 */
static struct sg_table *
a133_prime_get_sg_table(struct drm_gem_object *obj)
{
	(void)obj;
	return ERR_PTR(-EOPNOTSUPP);
}

static int
a133_prime_mmap(struct drm_gem_object *obj, off_t *offp, size_t size,
    int prot, int *flagsp, int *advicep, struct uvm_object **uobjp,
    int *maxprotp)
{
	(void)advicep;
	if (*offp < 0 || ((uint64_t)*offp & (PAGE_SIZE - 1)) != 0 ||
	    size == 0 || (size & (PAGE_SIZE - 1)) != 0 ||
	    (uint64_t)*offp > obj->size || size > obj->size - (size_t)*offp ||
	    (prot & ~(PROT_READ | PROT_WRITE)) != 0 ||
	    (*flagsp & MAP_SHARED) == 0 || (*flagsp & MAP_PRIVATE) != 0)
		return -EINVAL;
	drm_gem_object_get(obj);
	*uobjp = &obj->gemo_uvmobj;
	*maxprotp = PROT_READ | PROT_WRITE;
	return 0;
}

/* ------------------------------------------------------------------ */
/* Atomic commits */

static void
a133_commit_event(struct a133_drm *sc, struct drm_crtc_state *state,
    uint64_t frame, uint64_t time_ns, int error)
{
	struct drm_pending_vblank_event *event = state->event;
	struct drm_pending_event *base;
	unsigned long flags;

	if (event == NULL)
		return;
	base = &event->base;
	spin_lock_irqsave(&sc->dev->event_lock, flags);
	state->event = NULL;
	if (error == 0) {
		/* The time the new buffer started to be scanned out. */
		event->pipe = drm_crtc_index(&sc->crtc);
		event->event.vbl.sequence = (uint32_t)frame;
		event->event.vbl.tv_sec = time_ns / 1000000000;
		event->event.vbl.tv_usec = (time_ns % 1000000000) / 1000;
		drm_send_event_locked(sc->dev, base);
	} else {
		if (base->fence != NULL) {
			dma_fence_set_error(base->fence, -error);
			(void)dma_fence_signal(base->fence);
		}
		if (base->completion != NULL) {
			complete_all(base->completion);
			if (base->completion_release != NULL)
				base->completion_release(base->completion);
			base->completion = NULL;
			base->completion_release = NULL;
		}
	}
	spin_unlock_irqrestore(&sc->dev->event_lock, flags);
	if (error != 0)
		drm_event_cancel_free(sc->dev, base);
}

static int
a133_wait_fence(struct dma_fence *fence)
{
	long waited;
	int status;

	if (fence == NULL)
		return 0;
	waited = dma_fence_wait_timeout(fence, false, msecs_to_jiffies(1000));
	if (waited <= 0)
		return waited < 0 ? (int)-waited : ETIMEDOUT;
	status = dma_fence_get_status(fence);
	return status > 0 ? 0 : status < 0 ? -status : EIO;
}

static void
a133_commit_work(struct work *work, void *cookie)
{
	struct a133_commit *job = container_of(work, struct a133_commit, work);
	struct a133_drm *sc = cookie;
	struct drm_atomic_state *state = job->state;
	struct drm_plane_state *plane;
	struct drm_crtc_state *crtc;
	uint64_t frame = 0, time_ns = 0;
	uint32_t addr = sc->boot_addr;
	bool vblank_ref = false;
	int error;

	struct drm_gem_object *object = NULL;

	plane = drm_atomic_get_new_plane_state(state, &sc->plane);
	crtc = drm_atomic_get_new_crtc_state(state, &sc->crtc);

	/* No plane or no CRTC: the panel shows the boot framebuffer again. */
	if (crtc->active && plane->fb != NULL) {
		struct a133_drm_fb * const fb =
		    container_of(plane->fb, struct a133_drm_fb, base);

		addr = (uint32_t)fb->object->dmamap->dm_segs[0].ds_addr;
		object = &fb->object->base;
	}
	error = crtc->active && plane->fb != NULL ?
	    a133_wait_fence(plane->fence) : 0;
	if (error == 0 && sc->vblank_irq) {
		if (crtc->active && !sc->vblank_on) {
			drm_crtc_vblank_on(&sc->crtc);
			sc->vblank_on = true;
		}
		if (sc->vblank_on)
			vblank_ref = drm_crtc_vblank_get(&sc->crtc) == 0;
	}
	/*
	 * A failed producer fails this event only; the panel keeps its frame.
	 * The object on screen stays referenced until another one replaces
	 * it, even after DRM drops the framebuffer that carried it, so its
	 * pool range cannot be reused while the panel still reads it.
	 */
	if (error == 0) {
		a133_flip(sc, addr, &frame, &time_ns);
		if (object != NULL)
			drm_gem_object_get(object);
		if (sc->shown != NULL)
			drm_gem_object_put_unlocked(sc->shown);
		sc->shown = object;
	}
	/*
	 * The hardware part is done. A following flip may be submitted as soon
	 * as the event arrives; the single-threaded queue runs it after this
	 * worker returns.
	 */
	mutex_enter(&sc->lock);
	sc->busy = false;
	cv_broadcast(&sc->idle_cv);
	mutex_exit(&sc->lock);
	a133_commit_event(sc, crtc, frame, time_ns, error);
	if (vblank_ref)
		drm_crtc_vblank_put(&sc->crtc);
	if (!crtc->active && sc->vblank_on) {
		drm_crtc_vblank_off(&sc->crtc);
		sc->vblank_on = false;
	}
	drm_atomic_helper_commit_hw_done(state);
	drm_atomic_helper_cleanup_planes(sc->dev, state);
	drm_atomic_helper_commit_cleanup_done(state);
	drm_atomic_state_put(state);
	if (job->nonblock) {
		destroy_completion(&job->done);
		kmem_free(job, sizeof(*job));
	} else
		complete_all(&job->done);
}

static int
a133_atomic_commit(struct drm_device *dev, struct drm_atomic_state *state,
    bool nonblock)
{
	struct a133_drm *sc = dev->dev_private;
	struct a133_commit *job;
	int error;

	if (state->async_update)
		return -EOPNOTSUPP;
	mutex_enter(&sc->lock);
	/* A blocking commit (mode set, framebuffer removal) waits its turn. */
	while (!nonblock && sc->busy && sc->fault == 0)
		cv_wait(&sc->idle_cv, &sc->lock);
	error = sc->fault != 0 ? -sc->fault : sc->busy ? -EBUSY : 0;
	if (error == 0)
		sc->busy = true;
	mutex_exit(&sc->lock);
	if (error != 0)
		return error;
	job = kmem_zalloc(sizeof(*job), KM_SLEEP);
	init_completion(&job->done);
	job->state = state;
	job->nonblock = nonblock;
	error = drm_atomic_helper_setup_commit(state, nonblock);
	if (error != 0)
		goto fail;
	error = drm_atomic_helper_prepare_planes(dev, state);
	if (error != 0)
		goto fail;
	error = drm_atomic_helper_swap_state(state, true);
	if (error != 0) {
		drm_atomic_helper_cleanup_planes(dev, state);
		goto fail;
	}
	drm_atomic_state_get(state);
	workqueue_enqueue(sc->queue, &job->work, NULL);
	if (!nonblock) {
		wait_for_completion(&job->done);
		destroy_completion(&job->done);
		kmem_free(job, sizeof(*job));
	}
	/* The state is swapped; a producer error goes to the event fence. */
	return 0;
fail:
	destroy_completion(&job->done);
	kmem_free(job, sizeof(*job));
	mutex_enter(&sc->lock);
	sc->busy = false;
	cv_broadcast(&sc->idle_cv);
	mutex_exit(&sc->lock);
	return error;
}

static int
a133_atomic_check(struct drm_device *dev, struct drm_atomic_state *state)
{
	struct a133_drm *sc = dev->dev_private;
	struct drm_crtc_state *crtc;
	const struct drm_plane_state *plane;
	const struct drm_connector_state *connector;
	int error;

	if (state->async_update)
		return -EOPNOTSUPP;
	mutex_enter(&sc->lock);
	error = sc->fault;
	mutex_exit(&sc->lock);
	if (error != 0)
		return -error;
	crtc = drm_atomic_get_crtc_state(state, &sc->crtc);
	if (IS_ERR(crtc))
		return PTR_ERR(crtc);
	plane = drm_atomic_get_plane_state(state, &sc->plane);
	if (IS_ERR(plane))
		return PTR_ERR(plane);
	connector = drm_atomic_get_connector_state(state, &sc->connector);
	if (IS_ERR(connector))
		return PTR_ERR(connector);
	if (!crtc->enable) {
		if (crtc->active || plane->fb != NULL || plane->crtc != NULL ||
		    connector->crtc != NULL)
			return -EINVAL;
		return drm_atomic_helper_check(dev, state);
	}
	if (connector->crtc != &sc->crtc ||
	    !drm_mode_equal(&a133_mode, &crtc->mode))
		return -EINVAL;
	if (plane->fb == NULL && plane->crtc == NULL)
		return drm_atomic_helper_check(dev, state);
	if (plane->fb == NULL || plane->crtc != &sc->crtc ||
	    plane->src_x != 0 || plane->src_y != 0 ||
	    plane->src_w != (A133_WIDTH << 16) ||
	    plane->src_h != (A133_HEIGHT << 16) ||
	    plane->crtc_x != 0 || plane->crtc_y != 0 ||
	    plane->crtc_w != A133_WIDTH || plane->crtc_h != A133_HEIGHT ||
	    plane->rotation != DRM_MODE_ROTATE_0)
		return -EINVAL;
	return drm_atomic_helper_check(dev, state);
}

/* ------------------------------------------------------------------ */
/* KMS objects */

static bool
a133_format(struct drm_plane *plane, uint32_t format, uint64_t modifier)
{
	(void)plane;
	return format == DRM_FORMAT_XRGB8888 && modifier == DRM_FORMAT_MOD_LINEAR;
}

static const struct drm_plane_funcs a133_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = drm_plane_cleanup,
	.reset = drm_atomic_helper_plane_reset,
	.atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_plane_destroy_state,
	.format_mod_supported = a133_format,
};
/* The commit worker owns all hardware access. */
static const struct drm_plane_helper_funcs a133_plane_helpers = {0};

/* DRM calls these with its spin locks held; only intr_lock is taken. */
static int
a133_vblank_enable(struct drm_crtc *crtc)
{
	struct a133_drm *sc = crtc->dev->dev_private;

	if (!sc->vblank_irq)
		return -EOPNOTSUPP;
	mutex_spin_enter(&sc->intr_lock);
	a133_users_locked(sc, 1);
	mutex_spin_exit(&sc->intr_lock);
	return 0;
}

static void
a133_vblank_disable(struct drm_crtc *crtc)
{
	struct a133_drm *sc = crtc->dev->dev_private;

	if (!sc->vblank_irq)
		return;
	mutex_spin_enter(&sc->intr_lock);
	a133_users_locked(sc, -1);
	mutex_spin_exit(&sc->intr_lock);
}

static const struct drm_crtc_funcs a133_crtc_funcs = {
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.destroy = drm_crtc_cleanup,
	.reset = drm_atomic_helper_crtc_reset,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
	.enable_vblank = a133_vblank_enable,
	.disable_vblank = a133_vblank_disable,
};

static const struct drm_encoder_funcs a133_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static enum drm_connector_status
a133_detect(struct drm_connector *connector, bool force)
{
	(void)connector;
	(void)force;
	return connector_status_connected;
}

static void
a133_connector_destroy(struct drm_connector *connector)
{
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

static const struct drm_connector_funcs a133_connector_funcs = {
	.detect = a133_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = a133_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int
a133_get_modes(struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &a133_mode);
	if (mode == NULL)
		return 0;
	drm_mode_set_name(mode);
	drm_mode_probed_add(connector, mode);
	connector->display_info.width_mm = 135;
	connector->display_info.height_mm = 216;
	return 1;
}

static enum drm_mode_status
a133_mode_valid(struct drm_connector *connector, struct drm_display_mode *mode)
{
	(void)connector;
	return drm_mode_equal(&a133_mode, mode) ? MODE_OK : MODE_BAD;
}

static const struct drm_connector_helper_funcs a133_connector_helpers = {
	.get_modes = a133_get_modes,
	.mode_valid = a133_mode_valid,
};

static const struct drm_mode_config_funcs a133_mode_funcs = {
	.fb_create = a133_fb_create,
	.atomic_check = a133_atomic_check,
	.atomic_commit = a133_atomic_commit,
};

static struct drm_driver a133_driver = {
	.driver_features = DRIVER_MODESET | DRIVER_ATOMIC | DRIVER_GEM,
	.gem_free_object = drm_gem_cma_free_object,
	.mmap_object = drm_gem_or_legacy_mmap_object,
	.gem_uvm_ops = &drm_gem_cma_uvm_ops,
	.dumb_create = a133_dumb_create,
	.dumb_destroy = drm_gem_dumb_destroy,
	.prime_handle_to_fd = drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle = drm_gem_prime_fd_to_handle,
	.gem_prime_mmap = a133_prime_mmap,
	.gem_prime_get_sg_table = a133_prime_get_sg_table,
	.gem_prime_vmap = drm_gem_cma_prime_vmap,
	.gem_prime_vunmap = drm_gem_cma_prime_vunmap,
	.name = "sun50ia133drm",
	.desc = "A133 MIPI DSI fixed-mode KMS",
	.date = "20261109",
	.major = 1,
	.minor = 0,
	.patchlevel = 0,
};

/* ------------------------------------------------------------------ */
/* Attachment */

/* Accept only the output path the boot loader is known to leave. */
static int
a133_check_boot_path(struct a133_drm *sc)
{
	const uint32_t ctl = MIXER_READ(sc, GLB_CTL);
	const uint32_t size = MIXER_READ(sc, GLB_SIZE);
	const uint32_t attr = MIXER_READ(sc, LAY_ATTR);
	const uint32_t lsize = MIXER_READ(sc, LAY_SIZE);
	const uint32_t pitch = MIXER_READ(sc, LAY_PITCH);
	const uint32_t cpu_if = TCON_READ(sc, TCON_CPU_IF);
	const uint32_t gint = TCON_READ(sc, TCON_GINT0);

	sc->boot_addr = MIXER_READ(sc, LAY_TOP_LADDR);
	if ((ctl & GLB_CTL_EN) == 0 || size != A133_SIZE_REG ||
	    (attr & LAY_ATTR_EN) == 0 ||
	    __SHIFTOUT(attr, LAY_ATTR_FMT) != LAY_ATTR_FMT_ARGB8888 ||
	    lsize != A133_SIZE_REG || pitch != A133_PITCH ||
	    __SHIFTOUT(cpu_if, TCON_CPU_IF_MODE) != TCON_CPU_IF_MODE_DSI ||
	    (cpu_if & TCON_CPU_IF_TRI_EN) == 0 ||
	    (gint & 0xfc000000) != 0 || sc->boot_addr == 0 ||
	    (sc->boot_addr & 3) != 0) {
		aprint_error_dev(sc->device, "unexpected boot output: "
		    "ctl %#x size %#x attr %#x lsize %#x pitch %#x addr %#x "
		    "cpu_if %#x gint %#x\n", ctl, size, attr, lsize, pitch,
		    sc->boot_addr, cpu_if, gint);
		return ENXIO;
	}
	return 0;
}

/*
 * The interrupt line comes from the vendor node. Prove it delivers
 * TRI_FINISH before relying on it; otherwise flips poll the flag.
 */
static void
a133_check_intr(struct a133_drm *sc)
{
	uint64_t before, after;

	if (sc->ih == NULL)
		return;
	mutex_spin_enter(&sc->intr_lock);
	before = sc->frames;
	a133_users_locked(sc, 1);
	mutex_spin_exit(&sc->intr_lock);
	kpause("a133irq", false, MAX(1, mstohz(60)), NULL);
	mutex_spin_enter(&sc->intr_lock);
	after = sc->frames;
	a133_users_locked(sc, -1);
	mutex_spin_exit(&sc->intr_lock);
	sc->vblank_irq = after - before >= 3;
	aprint_normal_dev(sc->device, "%s frame interrupt (%" PRIu64
	    " in 60 ms)\n", sc->vblank_irq ? "using" : "polling without",
	    after - before);
}

static int
a133_register(struct a133_drm *sc)
{
	static const uint32_t formats[] = { DRM_FORMAT_XRGB8888 };
	static const uint64_t modifiers[] = {
		DRM_FORMAT_MOD_LINEAR, DRM_FORMAT_MOD_INVALID
	};
	struct drm_device *dev;
	int error;
	extern int drm_guarantee_initialized(void);

	error = drm_guarantee_initialized();
	if (error != 0)
		return error;
	a133_check_intr(sc);
	dev = drm_dev_alloc(&a133_driver, sc->device);
	if (IS_ERR(dev))
		return -PTR_ERR(dev);
	sc->dev = dev;
	dev->dev_private = sc;
	dev->cma_pool = sc->pool;
	dev->bus_dmat = dev->bus_dmat32 = dev->dmat = sc->dmat;
	dev->dmat_subregion_p = false;
	dev->native_platform.version = DRM_NATIVE_PLATFORM_VERSION;
	dev->native_platform.length = sizeof(dev->native_platform);
	dev->native_platform.bus_type = DRM_NATIVE_BUS_PLATFORM;
	strlcpy(dev->native_platform.fullname, device_xname(sc->device),
	    sizeof(dev->native_platform.fullname));
	strlcpy(dev->native_platform.compatible, "allwinner,sun50i-a133-display",
	    sizeof(dev->native_platform.compatible));
	drm_mode_config_init(dev);
	dev->mode_config.funcs = &a133_mode_funcs;
	dev->mode_config.min_width = dev->mode_config.max_width = A133_WIDTH;
	dev->mode_config.min_height = dev->mode_config.max_height = A133_HEIGHT;
	error = -drm_universal_plane_init(dev, &sc->plane, 1, &a133_plane_funcs,
	    formats, __arraycount(formats), modifiers, DRM_PLANE_TYPE_PRIMARY,
	    NULL);
	if (error != 0)
		goto fail;
	drm_plane_helper_add(&sc->plane, &a133_plane_helpers);
	error = -drm_crtc_init_with_planes(dev, &sc->crtc, &sc->plane, NULL,
	    &a133_crtc_funcs, NULL);
	if (error != 0)
		goto fail;
	error = -drm_encoder_init(dev, &sc->encoder, &a133_encoder_funcs,
	    DRM_MODE_ENCODER_DSI, NULL);
	if (error != 0)
		goto fail;
	sc->encoder.possible_crtcs = drm_crtc_mask(&sc->crtc);
	error = -drm_connector_init(dev, &sc->connector, &a133_connector_funcs,
	    DRM_MODE_CONNECTOR_DSI);
	if (error != 0)
		goto fail;
	drm_connector_helper_add(&sc->connector, &a133_connector_helpers);
	error = -drm_connector_attach_encoder(&sc->connector, &sc->encoder);
	if (error != 0)
		goto fail;
	drm_mode_config_reset(dev);
	if (sc->plane.state == NULL || sc->crtc.state == NULL ||
	    sc->connector.state == NULL) {
		error = ENOMEM;
		goto fail;
	}
	if (sc->vblank_irq) {
		error = -drm_vblank_init(dev, 1);
		if (error != 0)
			goto fail;
		mutex_spin_enter(&sc->intr_lock);
		sc->vblank_ready = true;
		mutex_spin_exit(&sc->intr_lock);
		dev->irq_enabled = true;
	}
	error = -drm_dev_register(dev, 0);
	if (error == 0)
		return 0;
fail:
	mutex_spin_enter(&sc->intr_lock);
	sc->vblank_ready = false;
	mutex_spin_exit(&sc->intr_lock);
	drm_mode_config_cleanup(dev);
	drm_dev_put(dev);
	sc->dev = NULL;
	return error;
}

static void
a133_mountroot(device_t self)
{
	struct a133_drm *sc = device_private(self);
	int error;

	error = a133_register(sc);
	if (error != 0) {
		aprint_error_dev(self, "KMS registration failed, error %d\n",
		    error);
		mutex_enter(&sc->lock);
		sc->fault = error;
		mutex_exit(&sc->lock);
	}
}

/*
 * The scanout buffers come from memory the platform reserved before UVM
 * (sun50i_a133_fdt.c), because contiguous frames cannot be allocated once
 * free pages are spread over all of memory. These pages have no vm_page,
 * so they are exported to the GPU by physical segment, not by page list.
 */
static void
a133_pool_create(struct a133_drm *sc)
{
	const uint64_t frame = A133_BO_BYTES;
	uint64_t base, size;
	const uint32_t *cells;
	int len;

	cells = fdtbus_get_prop(sc->phandle, "netbsd,scanout-pool", &len);
	if (cells == NULL || len != 16) {
		aprint_error_dev(sc->device, "no reserved scanout memory\n");
		return;
	}
	base = ((uint64_t)be32toh(cells[0]) << 32) | be32toh(cells[1]);
	size = ((uint64_t)be32toh(cells[2]) << 32) | be32toh(cells[3]);
	if ((base & (PAGE_SIZE - 1)) != 0 || size < 2 * frame ||
	    base + size > A133_POOL_LIMIT || base + size < base) {
		aprint_error_dev(sc->device, "bad scanout memory %#" PRIx64
		    " + %#" PRIx64 "\n", base, size);
		return;
	}
	sc->pool_frames = size / frame;
	sc->pool = vmem_create("a133kms", base, sc->pool_frames * frame,
	    PAGE_SIZE, NULL, NULL, NULL, 0, VM_SLEEP, IPL_NONE);
}

static void
a133_sysctl(struct a133_drm *sc)
{
	const struct sysctlnode *node;

	if (sysctl_createv(&sc->sysctllog, 0, NULL, &node, 0, CTLTYPE_NODE,
	    device_xname(sc->device), NULL, NULL, 0, NULL, 0, CTL_HW,
	    CTL_CREATE, CTL_EOL) != 0)
		return;
	(void)sysctl_createv(&sc->sysctllog, 0, &node, NULL, CTLFLAG_READONLY,
	    CTLTYPE_INT, "fault", NULL, NULL, 0, &sc->fault, 0,
	    CTL_CREATE, CTL_EOL);
	(void)sysctl_createv(&sc->sysctllog, 0, &node, NULL, CTLFLAG_READONLY,
	    CTLTYPE_QUAD, "flips", NULL, NULL, 0, &sc->flips, 0,
	    CTL_CREATE, CTL_EOL);
	(void)sysctl_createv(&sc->sysctllog, 0, &node, NULL, CTLFLAG_READONLY,
	    CTLTYPE_QUAD, "flip_timeouts", NULL, NULL, 0, &sc->flip_timeouts,
	    0, CTL_CREATE, CTL_EOL);
	(void)sysctl_createv(&sc->sysctllog, 0, &node, NULL, CTLFLAG_READONLY,
	    CTLTYPE_QUAD, "frames", NULL, NULL, 0, &sc->frames, 0,
	    CTL_CREATE, CTL_EOL);
	(void)sysctl_createv(&sc->sysctllog, 0, &node, NULL, CTLFLAG_READONLY,
	    CTLTYPE_BOOL, "frame_irq", NULL, NULL, 0, &sc->vblank_irq, 0,
	    CTL_CREATE, CTL_EOL);
}

static int
a133_match(device_t parent, cfdata_t cf, void *aux)
{
	static const struct device_compatible_entry compat[] = {
		{ .compat = "allwinner,sunxi-disp" },
		DEVICE_COMPAT_EOL
	};
	const struct fdt_attach_args *faa = aux;
	bus_addr_t addr;
	bus_size_t size;

	(void)parent;
	(void)cf;
	/* The vendor tree also has register-less "disp1" placeholders. */
	if (fdtbus_get_reg(faa->faa_phandle, A133_TCON_REG_INDEX, &addr,
	    &size) != 0)
		return 0;
	return of_compatible_match(faa->faa_phandle, compat);
}

static void
a133_attach(device_t parent, device_t self, void *aux)
{
	struct a133_drm *sc = device_private(self);
	const struct fdt_attach_args *faa = aux;
	bus_addr_t addr;
	bus_size_t size;
	char intrstr[128];
	int error;

	(void)parent;
	sc->device = self;
	sc->bst = faa->faa_bst;
	sc->phandle = faa->faa_phandle;
	mutex_init(&sc->lock, MUTEX_DEFAULT, IPL_NONE);
	mutex_init(&sc->intr_lock, MUTEX_DEFAULT, IPL_VM);
	cv_init(&sc->flip_cv, "a133flip");
	cv_init(&sc->idle_cv, "a133idle");
	aprint_naive("\n");
	aprint_normal(": A133 DSI fixed-mode KMS\n");
	sc->fault = ENXIO;

	/* Map only mixer 0 and TCON LCD0, never the whole DE window. */
	if (fdtbus_get_reg(sc->phandle, 0, &addr, &size) != 0 ||
	    addr != A133_MIXER0_BASE - 0x100000 ||
	    fdtbus_get_reg(sc->phandle, A133_TCON_REG_INDEX, &addr, &size) != 0 ||
	    addr != 0x06511000 || size < A133_TCON_SIZE - 1 ||
	    bus_space_map(sc->bst, addr, A133_TCON_SIZE, 0, &sc->tcon) != 0 ||
	    bus_space_map(sc->bst, A133_MIXER0_BASE, A133_MIXER0_SIZE, 0,
	    &sc->mixer) != 0) {
		aprint_error_dev(self, "cannot map registers\n");
		return;
	}
	if (a133_check_boot_path(sc) != 0)
		return;
	sc->dmat = faa->faa_dmat;
	a133_pool_create(sc);
	if (sc->pool == NULL)
		return;
	error = workqueue_create(&sc->queue, "a133kms", a133_commit_work, sc,
	    PRI_NONE, IPL_NONE, WQ_MPSAFE);
	if (error != 0)
		return;
	if (fdtbus_intr_str(sc->phandle, A133_TCON_INTR_INDEX, intrstr,
	    sizeof(intrstr)))
		sc->ih = fdtbus_intr_establish_xname(sc->phandle,
		    A133_TCON_INTR_INDEX, IPL_VM, FDT_INTR_MPSAFE, a133_intr,
		    sc, device_xname(self));
	aprint_normal_dev(self, "boot scanout %#x, %d scanout buffers, "
	    "interrupt %s\n", sc->boot_addr, sc->pool_frames,
	    sc->ih != NULL ? intrstr : "none");
	sc->fault = 0;
	a133_sysctl(sc);
	config_mountroot(self, a133_mountroot);
}

CFATTACH_DECL_NEW(sun50i_a133_drm, sizeof(struct a133_drm),
    a133_match, a133_attach, NULL, NULL);
