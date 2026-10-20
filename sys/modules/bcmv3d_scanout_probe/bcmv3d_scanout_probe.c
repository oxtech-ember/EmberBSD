/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* AI-assisted explicit native scanout acceptance. No ownership acquisition. */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");
#include <sys/param.h>
#include <sys/device.h>
#include <sys/errno.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <prop/proplib.h>
#include <dev/acpi/acpivar.h>
#include <arm/broadcom/bcm2712_display_acpi.h>
#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_gem.h>
#include <linux/err.h>
#include "drm_internal.h"
#include "bcmv3d_drv.h"
#include "bcmv3d_flip.h"
#include "bcmv3d_mmu4_native.h"
#include "bcmv3d_scanout.h"
#include "bcmv3d_vblank_native.h"

MODULE(MODULE_CLASS_MISC, bcmv3d_scanout_probe, NULL);

#define SCANOUT_PROBE_BYTES 8294400U
#define SCANOUT_PROBE_BO_BYTES \
	(((size_t)SCANOUT_PROBE_BYTES + PAGE_SIZE - 1) & ~((size_t)PAGE_SIZE - 1))
#define SCANOUT_PROBE_WIDTH 1920U
#define SCANOUT_PROBE_HEIGHT 1080U
#define SCANOUT_PROBE_POLLS 500U

struct scanout_probe_context {
	struct drm_minor *minor;
	struct bcmv3d_device *sc;
	struct bcmv3d_scanout *owner;
	struct bcmv3d_bo *bo[3];
	struct bcmv3d_scanout_frame *frame[3];
	unsigned int refs[3], submitted;
	int final_slot;
	bool refs_uncertain[3];
	struct bcmv3d_scanout_status status;
	struct bcmv3d_bo_stats before, after;
	const char *stage;
	bool attempted, status_valid, accepted;
};
static struct scanout_probe_context scanout_probe;

/* Resolve one actual registered native GPU0. Metadata grants no ownership. */
static int
scanout_probe_device(struct scanout_probe_context *ctx)
{
	struct bcm2712_display_contract contract;
	const struct acpi_devnode *node;
	struct drm_minor *minor;
	struct bcmv3d_device *sc;
	ACPI_HANDLE handle;
	device_t dev;
	int error;

	dev = device_find_by_xname("bcmv3d0");
	if (dev == NULL || !device_is_a(dev, "bcmv3d"))
		return ENODEV;
	minor = drm_minor_acquire(128);
	if (IS_ERR(minor))
		return ENODEV;
	error = ENODEV;
	if (minor->type != DRM_MINOR_RENDER || minor->dev->dev != dev ||
	    !minor->dev->registered || minor->dev->render != minor ||
	    minor->dev->driver == NULL || minor->dev->driver->name == NULL ||
	    strcmp(minor->dev->driver->name, "v3d") != 0 ||
	    (minor->dev->driver->driver_features & DRIVER_MODESET) != 0 ||
	    ACPI_FAILURE(AcpiGetHandle(NULL, "\\_SB.GPU0", &handle)))
		goto fail;
	node = acpi_match_node(handle);
	sc = minor->dev->dev_private;
	if (node == NULL || node->ad_device != dev || sc == NULL ||
	    sc->drm != minor->dev || sc->dev != dev || sc->handle != handle)
		goto fail;
	error = bcm2712_display_acpi_resources(dev, handle, &contract);
	if (error != 0)
		goto fail;
	ctx->minor = minor;
	ctx->sc = sc;
	return 0;
fail:
	drm_minor_release(minor);
	return error;
}

static int
scanout_probe_status(struct scanout_probe_context *ctx)
{
	int error;

	error = bcmv3d_scanout_cached_status(ctx->owner, &ctx->status);
	if (error != 0)
		return error;
	ctx->status_valid = true;
	if (ctx->status.version != 1 ||
	    ctx->status.length != sizeof(ctx->status))
		return EPROGMISMATCH;
	return ctx->status.first_error != 0 ? (int)ctx->status.first_error : 0;
}

/* Cached successful owners are prerequisites, never an ownership shortcut. */
static int
scanout_probe_pristine(struct scanout_probe_context *ctx)
{
	struct bcmv3d_mmu4_native_status mmu;
	struct bcmv3d_vblank_snapshot irq;
	const struct bcmv3d_scanout_status *s = &ctx->status;
	int error;

	if (ctx->sc->mmu4_native == NULL || ctx->sc->vblank_native == NULL ||
	    ctx->sc->scanout == NULL)
		return ENXIO;
	error = bcmv3d_mmu4_native_cached_status(ctx->sc->mmu4_native, &mmu);
	if (error != 0)
		return error;
	if (mmu.stage != BCMV3D_MMU4_NATIVE_COMPLETE || !mmu.sealed ||
	    !mmu.backing_valid || !mmu.backing.prepared || !mmu.backing.sealed ||
	    mmu.backing.mapping_uncertain || mmu.backing.first_error != 0 ||
	    mmu.fault != 0 || mmu.status_error != 0 ||
	    mmu.range.first_error != 0 || !mmu.sequence_valid ||
	    !mmu.sequence.sequence_complete || !mmu.sequence.retained ||
	    mmu.sequence.fault != 0 || !mmu.shared_valid ||
	    !mmu.shared.flush_complete || mmu.shared.fault != 0 ||
	    (mmu.shared.retained & 2U) == 0 || !mmu.icac_valid)
		return EIO;
	error = bcmv3d_vblank_native_snapshot(ctx->sc->vblank_native, &irq);
	if (error != 0)
		return error;
	if (irq.version != 1 || irq.length != sizeof(irq) || !irq.prepared ||
	    !irq.arm_attempted || !irq.routed || irq.first_error != 0 ||
	    irq.active_list != 32)
		return EIO;
	ctx->owner = ctx->sc->scanout;
	error = scanout_probe_status(ctx);
	if (error != 0)
		return error;
	if (s->current_slot != -1 || s->pending_slot != -1 || s->prepared != 0 ||
	    s->latches != 0 || s->retired != 0 || s->worker_pending != 0 ||
	    s->state[0] != BCMV3D_FLIP_FREE ||
	    s->state[1] != BCMV3D_FLIP_FREE ||
	    s->state[2] != BCMV3D_FLIP_FREE ||
	    s->state[3] != BCMV3D_FLIP_READY)
		return EBUSY;
	return 0;
}

/* Write private CPU storage only before the native reader lease exists. */
static void
scanout_probe_pattern(struct bcmv3d_bo *bo, unsigned int pattern)
{
	static const uint32_t colour[3] = { 0x00d02020, 0x0020d020, 0x002020d0 };
	uint32_t *pixel = bo->cma.vaddr;
	unsigned int x, y;

	memset(bo->cma.vaddr, 0, bo->cma.dmasize);
	for (y = 0; y < SCANOUT_PROBE_HEIGHT; y++) {
		for (x = 0; x < SCANOUT_PROBE_WIDTH; x++)
			pixel[y * SCANOUT_PROBE_WIDTH + x] =
			    ((x / 120 + y / 120) & 1) != 0 ?
			    colour[pattern] : 0x00ffffff;
	}
}

/* Match a cached snapshot against one exact admitted lifetime transition. */
static bool
scanout_probe_complete(const struct scanout_probe_context *ctx,
    unsigned int cycle, int slot)
{
	const struct bcmv3d_scanout_status *s = &ctx->status;
	unsigned int i;

	if (!(s->prepared == cycle + 1 && s->latches == cycle + 1 &&
	    s->retired == cycle && s->current_slot == slot &&
	    s->pending_slot == -1 && s->worker_pending == 0 &&
	    s->state[3] == BCMV3D_FLIP_READY))
		return false;
	for (i = 0; i < BCMV3D_FLIP_GEM_SLOTS; i++) {
		if (s->state[i] != ((int)i == slot ?
		    BCMV3D_FLIP_CURRENT : BCMV3D_FLIP_FREE))
			return false;
	}
	return true;
}

static bool
scanout_probe_accounted(const struct scanout_probe_context *ctx)
{

	return ctx->after.reserved_bytes == ctx->before.reserved_bytes +
	    SCANOUT_PROBE_BO_BYTES && ctx->after.reservations ==
	    ctx->before.reservations + 3 && ctx->after.releases ==
	    ctx->before.releases + 2;
}

/* A timeout never modifies the native protocol or authorizes reclamation. */
static int
scanout_probe_wait(struct scanout_probe_context *ctx, unsigned int cycle,
    int slot)
{
	const struct bcmv3d_scanout_status *s = &ctx->status;
	unsigned int poll;

	for (poll = 0; poll < SCANOUT_PROBE_POLLS; poll++) {
		int error;

		error = scanout_probe_status(ctx);
		if (error != 0)
			return error;
		if (s->prepared != cycle + 1 || s->latches > cycle + 1 ||
		    s->retired > cycle)
			return EPROTO;
		if (scanout_probe_complete(ctx, cycle, slot))
			return 0;
		error = kpause("v3dcycle", false, MAX(1, hz / 100), NULL);
		if (error != 0 && error != EWOULDBLOCK)
			return error;
	}
	return ETIMEDOUT;
}

static void
scanout_probe_report(struct scanout_probe_context *ctx, int error)
{
	const struct bcmv3d_scanout_status *s = &ctx->status;
	int status_error;

	bcmv3d_bo_snapshot(ctx->sc, &ctx->after);
	status_error = scanout_probe_status(ctx);
	if (ctx->accepted && (status_error != 0 ||
	    !scanout_probe_complete(ctx, 2, ctx->final_slot) ||
	    !scanout_probe_accounted(ctx))) {
		ctx->accepted = false;
		ctx->stage = "final-snapshot";
		error = status_error != 0 ? status_error : EPROTO;
	}
	aprint_normal("native scanout %s at %s: error %d submitted %u "
	    "last_cached_valid %u snapshot_error %d current %d pending %d "
	    "prepared %llu latches %llu "
	    "retired %llu worker %u states %u/%u/%u/%u fault %u; "
	    "caller frame refs %u/%u/%u uncertain %u/%u/%u BO refs %u/%u/%u; "
	    "BO bytes %llu -> %llu reservations %llu -> %llu "
	    "releases %llu -> %llu\n", ctx->accepted ? "cycle accepted" :
	    "NO SUCCESS; retained fault", ctx->stage, error, ctx->submitted,
	    ctx->status_valid, status_error, s->current_slot, s->pending_slot,
	    (unsigned long long)s->prepared, (unsigned long long)s->latches,
	    (unsigned long long)s->retired, s->worker_pending,
	    s->state[0], s->state[1], s->state[2], s->state[3], s->first_error,
	    ctx->refs[0], ctx->refs[1], ctx->refs[2], ctx->refs_uncertain[0],
	    ctx->refs_uncertain[1], ctx->refs_uncertain[2], ctx->bo[0] != NULL,
	    ctx->bo[1] != NULL, ctx->bo[2] != NULL,
	    (unsigned long long)ctx->before.reserved_bytes,
	    (unsigned long long)ctx->after.reserved_bytes,
	    (unsigned long long)ctx->before.reservations,
	    (unsigned long long)ctx->after.reservations,
	    (unsigned long long)ctx->before.releases,
	    (unsigned long long)ctx->after.releases);
	aprint_normal("native resources/module/render reference "
	    "retained until reboot; no KMS or Plasma acceptance\n");
}

static int
scanout_probe_cycle(struct scanout_probe_context *ctx)
{
	unsigned int i, j;
	int error;

	ctx->stage = "allocate";
	bcmv3d_bo_snapshot(ctx->sc, &ctx->before);
	for (i = 0; i < __arraycount(ctx->bo); i++) {
		ctx->bo[i] = bcmv3d_bo_alloc(ctx->sc, SCANOUT_PROBE_BYTES);
		if (IS_ERR(ctx->bo[i])) {
			error = -PTR_ERR(ctx->bo[i]);
			ctx->bo[i] = NULL;
			return error;
		}
		if (ctx->bo[i] == NULL || ctx->bo[i]->cma.vaddr == NULL ||
		    ctx->bo[i]->cma.base.size != SCANOUT_PROBE_BO_BYTES ||
		    ctx->bo[i]->cma.dmasize != SCANOUT_PROBE_BO_BYTES)
			return EIO;
		for (j = 0; j < i; j++) {
			if (ctx->bo[i] == ctx->bo[j])
				return EIO;
		}
		scanout_probe_pattern(ctx->bo[i], i);
	}
	for (i = 0; i < __arraycount(ctx->bo); i++) {
		int slot;

		ctx->stage = "prepare";
		/* Prepare can publish translation before reporting an error. */
		ctx->attempted = true;
		error = bcmv3d_scanout_prepare(ctx->owner, &ctx->bo[i]->cma.base,
		    &ctx->frame[i]);
		if (error != 0)
			return error;
		ctx->refs[i] = 1;
		if (!bcmv3d_scanout_matches(ctx->frame[i], &ctx->bo[i]->cma.base))
			return EPROTO;
		error = bcmv3d_scanout_get(ctx->frame[i]);
		if (error != 0)
			return error;
		ctx->refs[i]++;
		ctx->refs_uncertain[i] = true;
		error = bcmv3d_scanout_put(ctx->frame[i]);
		if (error != 0)
			return error;
		ctx->refs_uncertain[i] = false;
		ctx->refs[i]--;
		error = scanout_probe_status(ctx);
		if (error != 0)
			return error;
		slot = -1;
		for (j = 0; j < BCMV3D_FLIP_GEM_SLOTS; j++) {
			if (ctx->status.state[j] == BCMV3D_FLIP_READY) {
				if (slot != -1)
					return EPROTO;
				slot = (int)j;
			}
		}
		if (slot == -1 || ctx->status.prepared != i + 1)
			return EPROTO;
		ctx->stage = "submit";
		error = bcmv3d_scanout_submit(ctx->owner, ctx->frame[i]);
		if (error != 0)
			return error;
		ctx->submitted++;
		ctx->stage = "put-submitted";
		ctx->refs_uncertain[i] = true;
		error = bcmv3d_scanout_put(ctx->frame[i]);
		if (error != 0)
			return error;
		ctx->refs_uncertain[i] = false;
		ctx->refs[i]--;
		ctx->frame[i] = NULL;
		drm_gem_object_put_unlocked(&ctx->bo[i]->cma.base);
		ctx->bo[i] = NULL;
		ctx->stage = "latch-and-retirement";
		error = scanout_probe_wait(ctx, i, slot);
		if (error != 0)
			return error;
		ctx->final_slot = slot;
	}
	ctx->stage = "BO-accounting";
	bcmv3d_bo_snapshot(ctx->sc, &ctx->after);
	if (!scanout_probe_accounted(ctx))
		return EBUSY;
	ctx->accepted = true;
	ctx->stage = "complete";
	return 0;
}

static int
bcmv3d_scanout_probe_modcmd(modcmd_t command, void *argument)
{
	prop_dictionary_t properties = argument;
	struct scanout_probe_context *ctx = &scanout_probe;
	const char *expected, *mode;
	size_t length;
	unsigned int i;
	int error;

	if (command == MODULE_CMD_FINI)
		return ctx->attempted || ctx->minor != NULL ? EBUSY : 0;
	if (command != MODULE_CMD_INIT)
		return ENOTTY;
	if (ctx->attempted || ctx->minor != NULL)
		return EBUSY;
	if (cpu_intr_p() || cpu_softintr_p())
		return EWOULDBLOCK;
	ASSERT_SLEEPABLE();
	if (properties == NULL || !prop_dictionary_get_cstring_nocopy(properties,
	    "scanout-mode", &mode) || strcmp(mode, "cycle") != 0 ||
	    !prop_dictionary_get_cstring_nocopy(properties,
	    "expected-buildinfo", &expected))
		return EINVAL;
	length = strlen(buildinfo);
	if (length != 0 && buildinfo[length - 1] == '\n')
		length--;
	if (length == 0 || strlen(expected) != length ||
	    memcmp(expected, buildinfo, length) != 0)
		return EPROGMISMATCH;
	memset(ctx, 0, sizeof(*ctx));
	ctx->stage = "registered-native-device";
	error = scanout_probe_device(ctx);
	if (error != 0)
		return error;
	ctx->stage = "cached-pristine-owners";
	error = scanout_probe_pristine(ctx);
	if (error == 0)
		error = scanout_probe_cycle(ctx);
	if (ctx->attempted) {
		scanout_probe_report(ctx, error);
		/* Keep uncertain native references and this module until reboot. */
		return 0;
	}
	for (i = 0; i < __arraycount(ctx->bo); i++) {
		if (ctx->bo[i] != NULL)
			drm_gem_object_put_unlocked(&ctx->bo[i]->cma.base);
	}
	drm_minor_release(ctx->minor);
	memset(ctx, 0, sizeof(*ctx));
	return error;
}
