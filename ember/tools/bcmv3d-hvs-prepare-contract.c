/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Actual helper, module and contiguous BO bodies; kernel services are controlled. */
#include <libfdt.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdarg.h>
#define main bo_reference_main
#include "bo-services.h"
#undef main
#ifndef round_page
#define round_page(n) (((n) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))
#endif
#define BUS_DMA_WRITE 4
#define BUS_DMASYNC_PREWRITE 8
#include "prepare-types.h"
#include "mmu4-core-types.h"
#include "mmu4-backing-types.h"

static unsigned consumer_fail, consumer_geometry, backing_geometry;
static unsigned creates, loads, unloads, destroys, gem_puts, syncs;
static bus_dmamap_t consumer_map;
static bus_addr_t consumer_address = 0x65000000;
static struct drm_gem_cma_object saved_cma;
static char events[128];
static size_t nevents;
static bool verify_sync, module_phase;
static unsigned tokens[8];
#define TOKEN(n) ((void *)&tokens[(n)])
static bus_dma_tag_t expected_tag;

static void
event(char c)
{
	CHECK(nevents + 1 < sizeof(events));
	events[nevents++] = c;
	events[nevents] = 0;
}

static struct bcmv3d_bo *
prepare_alloc(struct bcmv3d_device *sc, uint32_t size)
{
	struct bcmv3d_bo *bo;

	event('B');
	CHECK(size == 8294400);
	bo = bcmv3d_bo_alloc(sc, size);
	if (IS_ERR(bo))
		return bo;
	CHECK(bo->maxsegs == 1 && bo->nsegs == 1);
	CHECK(bo->cma.base.refs == 1);
	saved_cma = bo->cma;
	switch (backing_geometry) {
	case 1: bo->cma.vaddr = NULL; break;
	case 2: bo->cma.dmasize--; break;
	case 3: bo->cma.base.size--; break;
	case 4: bo->cma.dmat = NULL; break;
	case 5: bo->cma.dmamap = NULL; break;
	case 6: bo->cma.dmamap->dm_mapsize--; break;
	case 7: bo->cma.dmamap->dm_nsegs = 0; break;
	case 8: bo->cma.dmamap->dm_segs[0].ds_len--; break;
	}
	return bo;
}

static int
prepare_map_create(bus_dma_tag_t tag, bus_size_t size, int nseg,
    bus_size_t max, bus_size_t boundary, int flags, bus_dmamap_t *map)
{

	event('C');
	CHECK(tag == expected_tag && size == round_page(8294400) && nseg == 1);
	CHECK(max == size && boundary == 0 && flags == BUS_DMA_WAITOK);
	creates++;
	if (consumer_fail == 1)
		return ENOMEM;
	int error = bus_dmamap_create(tag, size, nseg, max, boundary, flags, map);
	CHECK(error == 0);
	consumer_map = *map;
	return 0;
}

static int
prepare_map_load(bus_dma_tag_t tag, bus_dmamap_t map, void *kva,
    bus_size_t size, void *lwp, int flags)
{

	event('L');
	CHECK(tag == expected_tag && map == consumer_map && size == saved_cma.dmasize);
	CHECK(kva == saved_cma.vaddr && lwp == NULL);
	CHECK(flags == (BUS_DMA_WAITOK | BUS_DMA_WRITE));
	loads++;
	if (consumer_fail == 2)
		return EIO;
	CHECK(bus_dmamap_load(tag, map, kva, size, lwp, flags) == 0);
	map->dm_segs[0].ds_addr = consumer_address;
	switch (consumer_geometry) {
	case 1: map->dm_nsegs = 0; break;
	case 2: map->dm_nsegs = 2; break;
	case 3: map->dm_mapsize--; break;
	case 4: map->dm_segs[0].ds_len--; break;
	case 5: map->dm_segs[0].ds_len++; break;
	}
	return 0;
}

static void
prepare_map_unload(bus_dma_tag_t tag, bus_dmamap_t map)
{

	event('U');
	CHECK(map == consumer_map);
	unloads++;
	bus_dmamap_unload(tag, map);
}

static void
prepare_map_destroy(bus_dma_tag_t tag, bus_dmamap_t map)
{

	event('D');
	CHECK(map == consumer_map);
	destroys++;
	bus_dmamap_destroy(tag, map);
	consumer_map = NULL;
}

static void
prepare_put(struct drm_gem_object *gem)
{
	struct bcmv3d_bo *bo = bcmv3d_to_bo(gem);

	event('P');
	CHECK(consumer_map == NULL && gem->refs == 1);
	gem_puts++;
	/* Undo only deliberately corrupted fixture metadata before real BO free. */
	CHECK(saved_cma.dmamap != NULL && saved_cma.vaddr != NULL);
	bo->cma = saved_cma;
	bo->cma.dmamap->dm_nsegs = 1;
	bo->cma.dmamap->dm_mapsize = round_page(8294400);
	bo->cma.dmamap->dm_segs[0].ds_len = round_page(8294400);
	drm_gem_object_put_unlocked(gem);
}

static void
prepare_sync(bus_dma_tag_t tag, bus_dmamap_t map, bus_addr_t offset,
    bus_size_t size, int flags)
{
	const uint32_t *pixels = saved_cma.vaddr;
	bool correct = true;

	event('S');
	CHECK(tag == expected_tag && map == consumer_map && offset == 0);
	CHECK(size == saved_cma.dmasize && flags == BUS_DMASYNC_PREWRITE);
	if (verify_sync) {
		for (size_t i = 0; i < size / sizeof(*pixels); i++) {
			uint32_t expected = (uint32_t)((i * 2654435761U) ^
			    0x005aa55aU) & 0x00ffffffU;
			if (pixels[i] != expected)
				correct = false;
		}
		CHECK(correct);
	}
	syncs++;
}

#define bcmv3d_bo_alloc prepare_alloc
#define bus_dmamap_create prepare_map_create
#define bus_dmamap_load prepare_map_load
#define bus_dmamap_unload prepare_map_unload
#define bus_dmamap_destroy prepare_map_destroy
#define drm_gem_object_put_unlocked prepare_put
#define bus_dmamap_sync prepare_sync
/* These two native macros conflict after DRM headers. Preserve that boundary. */
#include "page-macros.h"
#include "prepare-body.h"
#undef round_page
#undef PAGE_MASK
/* Test expectations and controlled services use explicit PAGE_SIZE rounding. */
#define round_page(n) (((size_t)(n) + PAGE_SIZE - 1) & ~((size_t)PAGE_SIZE - 1))
#undef bcmv3d_bo_alloc
#undef drm_gem_object_put_unlocked

/* Controlled module services. The actual framebuffer traversal uses libfdt. */
#define MODULE(a, b, c)
#define MODULE_CLASS_MISC 0
#define MODULE_CMD_INIT 1
#define MODULE_CMD_FINI 2
#ifndef EPROGMISMATCH
#define EPROGMISMATCH 999
#endif
#define DEVITER_F_ROOT_FIRST 0
#define DRM_MINOR_RENDER 2
#define ACPI_FAILURE(e) ((e) != 0)
#define ASSERT_SLEEPABLE() CHECK(!interrupt_kind)
typedef int modcmd_t;
typedef unsigned deviter_t;
struct drm_driver { const char *name; };
struct drm_minor { int type; struct drm_device *dev; };
struct acpi_devnode { device_t ad_device; };
struct properties {
	bool prepare, wrong_bool, wrong_string;
	const char *build, *conflict;
	bool mmu4_present, mmu4_value, mmu4_wrong, debug_present, debug_wrong;
	uint32_t debug;
};
typedef struct properties *prop_dictionary_t;
struct bcm2712_display_contract { unsigned version; };
static const char buildinfo[] = "EmberBSD:test\n";
static struct drm_device drm;
static struct drm_driver driver = { .name = "v3d" };
static struct drm_minor minor;
static struct bcmv3d_device device;
static struct acpi_devnode node;
static unsigned interrupt_kind, gate_failure, minor_refs;
static unsigned char tree[512];
static bool no_tree, simplefb;
static unsigned enumerated_fb;
static const void *fdtbus_get_data(void) { return no_tree ? NULL : tree; }
static uint64_t arm_simplefb_physaddr(void) { return simplefb; }
static void deviter_init(deviter_t *it, int flags) { (void)flags; *it = 0; }
static device_t deviter_next(deviter_t *it)
{ return (*it)++ == 0 && enumerated_fb ? TOKEN(2) : NULL; }
static void deviter_release(deviter_t *it) { (void)it; }
static bool device_is_a(device_t dev, const char *name)
{ return dev == TOKEN(2) ? strcmp(name, "genfb") == 0 : strcmp(name, "bcmv3d") == 0; }
static device_t device_find_by_xname(const char *name)
{ CHECK(strcmp(name, "bcmv3d0") == 0); return gate_failure == 1 ? NULL : device.dev; }
static struct drm_minor *drm_minor_acquire(int index)
{ CHECK(index == 128); if (gate_failure == 2) return ERR_PTR(-ENODEV); minor_refs++; event('R'); return &minor; }
static void drm_minor_release(struct drm_minor *ref)
{ CHECK(ref == &minor && minor_refs == 1 && !consumer_map && !gems); minor_refs--; event('r'); }
static int AcpiGetHandle(void *parent, const char *path, ACPI_HANDLE *handle)
{ CHECK(parent == NULL && strcmp(path, "\\_SB.GPU0") == 0); *handle = TOKEN(3); return gate_failure == 3; }
static const struct acpi_devnode *acpi_match_node(ACPI_HANDLE handle)
{ (void)handle; return gate_failure == 4 ? NULL : &node; }
static int cpu_intr_p(void) { return interrupt_kind == 1; }
static int cpu_softintr_p(void) { return interrupt_kind == 2; }
static void aprint_normal_dev(device_t dev, const char *format, ...)
{ (void)dev; (void)format; }
static int last_module_error, last_cleanup_error;
static void aprint_error(const char *format, ...)
{
	va_list ap;
	if (strncmp(format, "MMU4 preparation failed:", 24) != 0)
		return;
	va_start(ap, format); last_module_error = va_arg(ap, int); va_end(ap);
}
static void aprint_error_dev(device_t dev, const char *format, ...)
{
	va_list ap;
	CHECK(dev == device.dev);
	if (strncmp(format, "MMU4 preparation cleanup failed:", 32) != 0)
		return;
	va_start(ap, format); last_cleanup_error = va_arg(ap, int); va_end(ap);
}
static bool prop_dictionary_get_bool(prop_dictionary_t p, const char *key, bool *v)
{
	if (strcmp(key, "mmu4-prepare-only") == 0) {
		*v = p->mmu4_value;
		return p->mmu4_present && !p->mmu4_wrong;
	}
	CHECK(strcmp(key, "prepare-only") == 0);
	*v = p->prepare;
	return !p->wrong_bool;
}
static bool prop_dictionary_get_uint32(prop_dictionary_t p, const char *key, uint32_t *v)
{
	CHECK(strcmp(key, "expected-mmu4-debug") == 0);
	*v = p->debug;
	return p->debug_present && !p->debug_wrong;
}
static bool prop_dictionary_get_cstring_nocopy(prop_dictionary_t p, const char *key, const char **v)
{ CHECK(strcmp(key, "expected-buildinfo") == 0); *v = p->build; return p->build != NULL && !p->wrong_string; }
static const void *prop_dictionary_get(prop_dictionary_t p, const char *key)
{
	if (strcmp(key, "mmu4-prepare-only") == 0)
		return p->mmu4_present ? p : NULL;
	if (strcmp(key, "expected-mmu4-debug") == 0)
		return p->debug_present ? p : NULL;
	return p->conflict != NULL && strcmp(p->conflict, key) == 0 ? p : NULL;
}
static int bcm2712_display_acpi_resources(device_t dev, ACPI_HANDLE h, struct bcm2712_display_contract *c)
{ CHECK(dev == device.dev && h == device.handle); c->version = 1; return gate_failure == 5 ? EINVAL : 0; }
/* Controlled backing API only. Its actual implementation has separate tests. */
struct bcmv3d_mmu4_backing {
	struct bcmv3d_hvs_frame *frame;
	uint32_t debug;
	bool prepared, uncertain;
};
static struct bcmv3d_mmu4_backing *mmu4_live;
static unsigned mmu4_failure, mmu4_malformed, mmu4_calls[5];
int bcmv3d_mmu4_backing_create(struct bcmv3d_device *sc, uint32_t debug,
    struct bcmv3d_mmu4_backing **out)
{
	struct bcmv3d_mmu4_backing *b;
	int error;

	CHECK(sc == &device && debug == 0x20804774U && minor_refs == 1);
	mmu4_calls[0]++;
	if (mmu4_failure == 1)
		return ENOMEM;
	CHECK(mmu4_live == NULL);
	b = calloc(1, sizeof(*b));
	CHECK(b != NULL);
	error = bcmv3d_hvs_prepare(sc, sc->dmat, &b->frame);
	if (error != 0) { free(b); return error; }
	b->debug = debug;
	mmu4_live = *out = b;
	return 0;
}
int bcmv3d_mmu4_backing_get_config(const struct bcmv3d_mmu4_backing *b,
    struct bcm2712_mmu4_config *out)
{
	CHECK(b == mmu4_live);
	mmu4_calls[1]++;
	if (mmu4_failure == 2)
		return EINVAL;
	memset(out, 0, sizeof(*out)); out->debug = b->debug;
	return 0;
}
int bcmv3d_mmu4_backing_prepare(void *arg, const struct bcm2712_mmu4_config *cfg)
{
	struct bcmv3d_mmu4_backing *b = arg;
	CHECK(b == mmu4_live && cfg->debug == b->debug);
	mmu4_calls[2]++;
	if (mmu4_failure == 3)
		return EINVAL;
	if (mmu4_failure == 6) { b->uncertain = true; return EAGAIN; }
	bcmv3d_hvs_prewrite(b->frame);
	b->prepared = true;
	return 0;
}
int bcmv3d_mmu4_backing_status(const struct bcmv3d_mmu4_backing *b,
    struct bcmv3d_mmu4_backing_status *out)
{
	CHECK(b == mmu4_live);
	mmu4_calls[3]++;
	if (mmu4_failure == 4)
		return EIO;
	memset(out, 0, sizeof(*out));
	out->stage = b->prepared ? BCMV3D_MMU4_BACKING_PREPARED : BCMV3D_MMU4_BACKING_IDLE;
	out->frame_bytes = b->frame->size; out->control_bytes = PAGE_SIZE;
	out->fragments = (out->frame_bytes + 4 * PAGE_SIZE) / 4096;
	out->pmap_calls = 2 * out->fragments; out->syncs = b->prepared ? 5 : 0;
	out->prepare_started = out->prepared = b->prepared;
	out->mapping_uncertain = b->uncertain;
	switch (mmu4_malformed) {
	case 1: out->frame_bytes -= 4096; break;
	case 2: out->control_bytes -= 4096; break;
	case 3: out->fragments--; break;
	case 4: out->pmap_calls--; break;
	case 5: out->syncs--; break;
	case 6: out->prepared = false; break;
	case 7: out->sealed = true; break;
	case 8: out->first_error = EIO; break;
	case 9: out->mapping_uncertain = true; break;
	case 10: out->stage = BCMV3D_MMU4_BACKING_IDLE; break;
	}
	return 0;
}
int bcmv3d_mmu4_backing_destroy_unexposed(struct bcmv3d_mmu4_backing *b)
{
	CHECK(b == mmu4_live);
	mmu4_calls[4]++;
	if (mmu4_failure == 5 || b->uncertain)
		return EBUSY;
	bcmv3d_hvs_destroy(b->frame);
	mmu4_live = NULL; free(b);
	return 0;
}
#include "module-body.h"

static void
reset(void)
{

	CHECK(mmu4_live == NULL && hvs_retained_backing == NULL &&
	    hvs_retained_minor == NULL);
	empty(&device);
	mmu4_failure = mmu4_malformed = 0;
	last_module_error = last_cleanup_error = 0;
	memset(mmu4_calls, 0, sizeof(mmu4_calls));
	consumer_fail = consumer_geometry = backing_geometry = 0;
	creates = loads = unloads = destroys = gem_puts = syncs = 0;
	nevents = 0; events[0] = 0; verify_sync = false;
	consumer_address = 0x65000000;
	expected_tag = TOKEN(module_phase ? 4 : 5);
	fail = bad_geometry = gate_failure = interrupt_kind = 0;
	minor_refs = 0; no_tree = simplefb = false; enumerated_fb = 0;
	CHECK(fdt_create_empty_tree(tree, sizeof(tree)) == 0);
	device = (struct bcmv3d_device){ .drm = &drm, .dev = TOKEN(1),
	    .dmat = TOKEN(4), .handle = TOKEN(3), .bo_total = BCMV3D_BO_TOTAL };
	drm = (struct drm_device){ .dev_private = &device, .dev = device.dev,
	    .registered = true, .driver = &driver, .render = &minor };
	minor = (struct drm_minor){ .type = DRM_MINOR_RENDER, .dev = &drm };
	node.ad_device = device.dev;
}

int
main(void)
{
	struct bcmv3d_hvs_frame *frame, *poison = TOKEN(7);
	struct properties properties = { .prepare = true, .build = "EmberBSD:test" };
	const char *conflicts[] = { "clock-mode", "dvp-mode", "hd-mode", "hpd-mode",
	    "ddc-mode", "phy-mode", "iommu-mode", "disp-mode" };

	(void)bo_reference_main;
	reset(); frame = poison;
	CHECK(bcmv3d_hvs_prepare(NULL, TOKEN(5), &frame) == EINVAL && frame == poison);
	CHECK(bcmv3d_hvs_prepare(&device, NULL, &frame) == EINVAL && frame == poison);
	CHECK(bcmv3d_hvs_prepare(&device, TOKEN(5), NULL) == EINVAL);
	for (unsigned i = 1; i <= 5; i++) {
		reset(); fail = i; frame = poison;
		CHECK(bcmv3d_hvs_prepare(&device, TOKEN(5), &frame) == (i == 5 ? EIO : ENOMEM) && frame == poison);
		CHECK(gem_puts == 0); empty(&device);
	}
	for (unsigned i = 1; i <= 8; i++) {
		reset(); backing_geometry = i; frame = poison;
		CHECK(bcmv3d_hvs_prepare(&device, TOKEN(5), &frame) == EINVAL && frame == poison);
		CHECK(creates == 0 && gem_puts == 1); empty(&device);
	}
	for (unsigned i = 1; i <= 2; i++) {
		reset(); consumer_fail = i; frame = poison;
		CHECK(bcmv3d_hvs_prepare(&device, TOKEN(5), &frame) == (i == 1 ? ENOMEM : EIO));
		CHECK(frame == poison && gem_puts == 1 && unloads == 0);
		CHECK(destroys == (i == 2)); empty(&device);
	}
	for (unsigned i = 1; i <= 5; i++) {
		reset(); consumer_geometry = i; frame = poison;
		CHECK(bcmv3d_hvs_prepare(&device, TOKEN(5), &frame) == EINVAL && frame == poison);
		CHECK(gem_puts == 1 && unloads == 1 && destroys == 1); empty(&device);
	}
	const bus_addr_t accepted[] = { 0, 0x65000001,
	    (UINT64_C(1) << 32) - round_page(8294400) };
	for (unsigned i = 0; i < sizeof(accepted) / sizeof(*accepted); i++) {
		reset(); consumer_address = accepted[i];
		CHECK(bcmv3d_hvs_prepare(&device, TOKEN(5), &frame) == 0);
		CHECK(frame->address == accepted[i] && frame->address != frame->bo->gpu);
		CHECK(frame->address != frame->bo->cma.dmamap->dm_segs[0].ds_addr);
		CHECK(frame->size == round_page(8294400) && frame->pitch == 7680);
		bcmv3d_hvs_prewrite(frame); bcmv3d_hvs_destroy(frame);
		CHECK(strcmp(events, "BCLSUDP") == 0 && gem_puts == 1); empty(&device);
	}
	const bus_addr_t rejected[] = {
	    (UINT64_C(1) << 32) - round_page(8294400) + 1,
	    UINT64_C(1) << 32, UINT64_MAX };
	for (unsigned i = 0; i < sizeof(rejected) / sizeof(*rejected); i++) {
		reset(); consumer_address = rejected[i]; frame = poison;
		CHECK(bcmv3d_hvs_prepare(&device, TOKEN(5), &frame) == EFBIG && frame == poison);
		CHECK(gem_puts == 1 && unloads == 1 && destroys == 1); empty(&device);
	}
	module_phase = true;
	reset(); verify_sync = true;
	CHECK(bcmv3d_hvs_prepare_modcmd(MODULE_CMD_INIT, &properties) == 0);
	CHECK(strcmp(events, "RBCLSUDPr") == 0 && syncs == 1 && gem_puts == 1);
	empty(&device);
	CHECK(bcmv3d_hvs_prepare_modcmd(MODULE_CMD_FINI, NULL) == 0);
	CHECK(bcmv3d_hvs_prepare_modcmd(0, NULL) == ENOTTY);
	for (unsigned i = 1; i <= 2; i++) {
		reset(); interrupt_kind = i;
		CHECK(bcmv3d_hvs_prepare_modcmd(MODULE_CMD_INIT, &properties) == EWOULDBLOCK);
		CHECK(nevents == 0);
	}
	for (unsigned i = 0; i < sizeof(conflicts) / sizeof(*conflicts); i++) {
		reset(); properties.conflict = conflicts[i];
		CHECK(bcmv3d_hvs_prepare_modcmd(MODULE_CMD_INIT, &properties) == EINVAL);
		CHECK(nevents == 0);
	}
	properties.conflict = NULL;
	reset(); properties.prepare = false;
	CHECK(bcmv3d_hvs_prepare_modcmd(MODULE_CMD_INIT, &properties) == EINVAL && nevents == 0);
properties.prepare = true;
	reset(); properties.wrong_bool = true;
	CHECK(bcmv3d_hvs_prepare_modcmd(MODULE_CMD_INIT, &properties) == EINVAL && nevents == 0);
	properties.wrong_bool = false;
	reset(); properties.wrong_string = true;
	CHECK(bcmv3d_hvs_prepare_modcmd(MODULE_CMD_INIT, &properties) == EINVAL && nevents == 0);
	properties.wrong_string = false;
	reset(); properties.build = "wrong";
	CHECK(bcmv3d_hvs_prepare_modcmd(MODULE_CMD_INIT, &properties) == EPROGMISMATCH && nevents == 0);
	properties.build = "EmberBSD:test";
	for (unsigned i = 1; i <= 5; i++) {
		reset(); gate_failure = i;
		CHECK(bcmv3d_hvs_prepare_modcmd(MODULE_CMD_INIT, &properties) != 0);
		CHECK(creates == 0 && minor_refs == 0); empty(&device);
	}
	reset(); simplefb = true;
	CHECK(bcmv3d_hvs_prepare_modcmd(MODULE_CMD_INIT, &properties) == EBUSY && nevents == 0);
	reset(); enumerated_fb = 1;
	CHECK(bcmv3d_hvs_prepare_modcmd(MODULE_CMD_INIT, &properties) == EBUSY && nevents == 0);
	reset(); no_tree = true;
	CHECK(bcmv3d_hvs_prepare_modcmd(MODULE_CMD_INIT, &properties) == EIO && nevents == 0);
	reset(); CHECK(fdt_add_subnode(tree, 0, "framebuffer@0") >= 0);
	CHECK(bcmv3d_hvs_prepare_modcmd(MODULE_CMD_INIT, &properties) == EBUSY && nevents == 0);
	for (unsigned i = 1; i <= 5; i++) {
		reset(); fail = i;
		CHECK(bcmv3d_hvs_prepare_modcmd(MODULE_CMD_INIT, &properties) != 0);
		CHECK(gem_puts == 0 && minor_refs == 0); empty(&device);
	}
	for (unsigned i = 1; i <= 2; i++) {
		reset(); consumer_fail = i;
		CHECK(bcmv3d_hvs_prepare_modcmd(MODULE_CMD_INIT, &properties) != 0);
		CHECK(gem_puts == 1 && minor_refs == 0); empty(&device);
	}
	/* Exercise the real dispatch/cycle with a controlled backing boundary. */
	properties.mmu4_present = properties.mmu4_value = properties.debug_present = true;
	properties.debug = 0x20804774U;
	reset();
	CHECK(bcmv3d_hvs_prepare_modcmd(MODULE_CMD_INIT, &properties) == 0);
	for (unsigned i = 0; i < 5; i++) CHECK(mmu4_calls[i] == 1);
	CHECK(minor_refs == 0 && mmu4_live == NULL);
	CHECK(bcmv3d_hvs_prepare_modcmd(MODULE_CMD_FINI, NULL) == 0);
	for (unsigned i = 1; i <= 4; i++) {
		reset(); mmu4_failure = i;
		CHECK(bcmv3d_hvs_prepare_modcmd(MODULE_CMD_INIT, &properties) ==
		    (i == 1 ? ENOMEM : i == 4 ? EIO : EINVAL));
		CHECK(minor_refs == 0 && mmu4_live == NULL);
		CHECK(mmu4_calls[0] == 1 && mmu4_calls[4] == (i != 1));
		CHECK(mmu4_calls[3] == (i == 4));
	}
	for (unsigned i = 1; i <= 10; i++) {
		reset(); mmu4_malformed = i;
		CHECK(bcmv3d_hvs_prepare_modcmd(MODULE_CMD_INIT, &properties) == EIO);
		CHECK(mmu4_calls[4] == 1 && minor_refs == 0 && mmu4_live == NULL);
	}
	for (unsigned i = 0; i < 5; i++) {
		struct properties bad = properties;
		reset();
		switch (i) {
		case 0: bad.mmu4_value = false; break;
		case 1: bad.mmu4_wrong = true; break;
		case 2: bad.debug_present = false; break;
		case 3: bad.debug_wrong = true; break;
		case 4: bad.mmu4_present = false; break;
		}
		CHECK(bcmv3d_hvs_prepare_modcmd(MODULE_CMD_INIT, &bad) == EINVAL);
		CHECK(nevents == 0 && mmu4_calls[0] == 0);
	}
	/* Never restore unknown mapping metadata to make fixture cleanup pass. */
	for (unsigned i = 5; i <= 6; i++) {
		int status;
		pid_t child = fork();
		CHECK(child >= 0);
		if (child == 0) {
			reset(); mmu4_failure = i;
			CHECK(bcmv3d_hvs_prepare_modcmd(MODULE_CMD_INIT, &properties) == 0);
			CHECK(hvs_retained_backing == mmu4_live && mmu4_live != NULL);
			CHECK(hvs_retained_minor == &minor && minor_refs == 1);
			CHECK(last_cleanup_error == EBUSY);
			CHECK(last_module_error == (i == 5 ? EBUSY : EAGAIN));
			CHECK(bcmv3d_hvs_prepare_modcmd(MODULE_CMD_FINI, NULL) == EBUSY);
			CHECK(bcmv3d_hvs_prepare_modcmd(MODULE_CMD_INIT, &properties) == EBUSY);
			CHECK(mmu4_calls[0] == 1 && mmu4_calls[4] == 1);
			_Exit(0);
		}
		CHECK(waitpid(child, &status, 0) == child);
		CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
	}
	printf("PASS: %u HVS preparation actual-source checks (PAGE_SIZE %u)\n", checks, (unsigned)PAGE_SIZE);
	return 0;
}
