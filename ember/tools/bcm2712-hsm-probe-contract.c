/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual probe, HSM/DVP cores/providers, decoder and libfdt; controlled kernel. */
#define bcm2712_display_hsm_create actual_hsm_create
#define bcm2712_display_hsm_acquire actual_hsm_acquire
#define bcm2712_display_hsm_release actual_hsm_release
#define bcm2712_display_hsm_status actual_hsm_status
#define bcm2712_display_hsm_destroy actual_hsm_destroy
#define bcm2712_display_acpi_resources fixture_hsm_resources
#define main existing_hsm_main
#include "hsm-services.c"
#undef main
#undef bcm2712_display_hsm_create
#undef bcm2712_display_hsm_acquire
#undef bcm2712_display_hsm_release
#undef bcm2712_display_hsm_status
#undef bcm2712_display_hsm_destroy
#undef bcm2712_display_acpi_resources
#include <stdarg.h>
#include <libfdt.h>
#include "opt_bcm2712_ddc.h"

#ifndef EPROGMISMATCH
#define EPROGMISMATCH ENOEXEC
#endif
#define MODULE(a,b,c)
#define MODULE_CLASS_MISC 0
#define MODULE_CMD_INIT 1
#define MODULE_CMD_FINI 2
#define DEVITER_F_ROOT_FIRST 0
#define DRM_MINOR_RENDER 2
#define ACPI_FAILURE(e) ((e) != 0)
#define IS_ERR(p) ((intptr_t)(p) < 0)
#define PTR_ERR(p) ((intptr_t)(p))
typedef int modcmd_t;
typedef struct { unsigned int next; } deviter_t;
struct properties { const char *expected; bool cold; const char *mode;
    bool wrong_mode_type; const char *hpd_mode; bool wrong_hpd_type;
    const char *ddc_mode; bool wrong_ddc_type; };
typedef struct properties *prop_dictionary_t;
const char buildinfo[] = "EmberBSD:accepted-kernel\n";
static char blob[4096] __aligned(8);
static const void *tree;
static int fb_token;
static bool genfb, wsdisplay, find_device, right_driver, right_node, render;
static bool cleanup_error;
static uint64_t early_framebuffer;
static unsigned int references;
static char log_text[8192];
static size_t log_size;
struct drm_driver { const char *name; };
struct drm_device { device_t dev; struct drm_driver *driver; bool registered;
    struct drm_minor *render; };
struct drm_minor { int type; struct drm_device *dev; };
struct acpi_devnode { device_t ad_device; };
static struct drm_driver driver;
static struct drm_device drm;
static struct drm_minor fixture_minor;
static struct acpi_devnode fixture_node;

static bool prop_dictionary_get_bool(prop_dictionary_t p, const char *n, bool *v)
{ assert(strcmp(n, "cold-headless") == 0); *v = p->cold; return true; }
static bool prop_dictionary_get_cstring_nocopy(prop_dictionary_t p,
    const char *n, const char **v)
{
	if (strcmp(n, "dvp-mode") == 0) {
		if (p->wrong_mode_type) return false;
		*v = p->mode; return *v != NULL;
	}
	if (strcmp(n, "ddc-mode") == 0) {
		if (p->wrong_ddc_type) return false;
		*v = p->ddc_mode; return *v != NULL;
	}
	if (strcmp(n, "hpd-mode") == 0) {
		if (p->wrong_hpd_type) return false;
		*v = p->hpd_mode; return *v != NULL;
	}
	assert(strcmp(n, "expected-buildinfo") == 0);
	*v = p->expected; return *v != NULL;
}
static void *prop_dictionary_get(prop_dictionary_t p, const char *n)
{
	if (strcmp(n, "ddc-mode") == 0)
		return p->ddc_mode || p->wrong_ddc_type ? p : NULL;
	if (strcmp(n, "hpd-mode") == 0)
		return p->hpd_mode || p->wrong_hpd_type ? p : NULL;
	assert(strcmp(n, "dvp-mode") == 0);
	return p->mode || p->wrong_mode_type ? p : NULL;
}
static device_t device_find_by_xname(const char *name)
{ assert(strcmp(name, "bcmv3d0") == 0); return find_device ? &device_token : NULL; }
static bool device_is_a(device_t d, const char *kind)
{
	return (d == &device_token && strcmp(kind, "bcmv3d") == 0 && right_driver) ||
	    (d == &fb_token && ((genfb && strcmp(kind, "genfb") == 0) ||
	    (wsdisplay && strcmp(kind, "wsdisplay") == 0)));
}
static void deviter_init(deviter_t *i, int flags)
{ assert(flags == DEVITER_F_ROOT_FIRST); i->next = 0; }
static device_t deviter_next(deviter_t *i)
{ return i->next++ == 0 && (genfb || wsdisplay) ? &fb_token : NULL; }
static void deviter_release(deviter_t *i) { (void)i; }
static uint64_t arm_simplefb_physaddr(void) { return early_framebuffer; }
static const void *fdtbus_get_data(void) { return tree; }
static struct drm_minor *drm_minor_acquire(unsigned int id)
{ assert(id == 128); if (!render) return (void *)(intptr_t)-ENODEV; references++; return &fixture_minor; }
static void drm_minor_release(const struct drm_minor *m)
{ assert(m == &fixture_minor && references != 0); references--; }
static int AcpiGetHandle(ACPI_HANDLE parent, const char *path, ACPI_HANDLE *h)
{ assert(parent == NULL && strcmp(path, "\\_SB.GPU0") == 0); *h = &device_token; return 0; }
static struct acpi_devnode *acpi_match_node(ACPI_HANDLE h)
{ assert(h == &device_token); return right_node ? &fixture_node : NULL; }
static void payload_guard(void);
static void capture(const char *format, va_list ap)
{
	int length;

	if (strstr(format, "BCM2712_EDID0") != NULL) payload_guard();
	length = vsnprintf(log_text + log_size, sizeof(log_text) - log_size,
	    format, ap);
	assert(length >= 0 && (size_t)length < sizeof(log_text) - log_size);
	log_size += (size_t)length;
	/* Inject a kernel context failure at the unmutated cleanup boundary. */
	if (cleanup_error && strstr(log_text, "HSM probe stopped:") != NULL)
		in_irq = true;
}
static void aprint_normal_dev(device_t d, const char *f, ...)
{ va_list ap; assert(d == &device_token); va_start(ap, f); capture(f, ap); va_end(ap); }
static void aprint_error_dev(device_t d, const char *f, ...)
{ va_list ap; assert(d == &device_token); va_start(ap, f); capture(f, ap); va_end(ap); }
static void aprint_error(const char *f, ...)
{ va_list ap; va_start(ap, f); capture(f, ap); va_end(ap); }
/* Real resource decoder and DVP core/native provider, with kernel boundaries. */
#include "display-core.h"
#include "dvp-types.h"
#include "dvp-core.h"
struct bus_space { unsigned int token; };
struct bus_space arm_generic_bs_tag;
typedef struct bus_space *bus_space_tag_t;
typedef uintptr_t bus_space_handle_t;
#ifdef BCM2712_DISPLAY_DDC
#include "i2c-types.h"
#include "brcm-types.h"
#include "ddc-types.h"
struct bcm2712_ddc_provider { unsigned int token; };
static struct {
	struct bcm2712_ddc_provider owner;
	unsigned int creates, reads, statuses, destroys;
	int create_error, read_error, status_error, destroy_error, native_fault, core_fault;
	bool live, read_called, status_called, active, create_owner, null_owner;
	bool invalid_status, invalid_mask;
	uint8_t bytes[BCM2712_DDC_EDID_LENGTH];
} native_ddc;
#endif
#define BUS_SPACE_BARRIER_READ 1

#define BUS_SPACE_BARRIER_WRITE 2
static struct {
	uint32_t gates, resets, pending;
	unsigned int maps, unmaps, reads, writes, barriers, ignore_write;
	bool mapped, waiting, disturb_lease, hpd_mapped, external_gate;
	uint32_t hpd_raw;
	unsigned int hpd_maps, hpd_reads, hpd_unmaps;
	bool bsc_mapped, ddc_mapped, edid0;
	uint32_t bsc_status, bsc_mask, ddc_ctl, ddc_enable, ddc_ctlhi;
	unsigned int bsc_maps, bsc_reads, bsc_unmaps, ddc_maps, ddc_reads, ddc_unmaps;
} hw;
static char events[128];
static unsigned int event_count, fail_step;
static bool step(char event)
{
	assert(event_count + 1 < sizeof(events));
	events[event_count++] = event; events[event_count] = 0;
	return event_count == fail_step;
}
static void held_hsm(void)
{
	assert(bcm2712_hsm_owner != NULL);
	assert(bcm2712_hsm_owner->core.status.leases == 1);
	assert(bcm2712_hsm_owner->core.status.state == 1);
	assert(bcm2712_hsm_owner->core.status.rate >= 120000000);
	assert(bcm2712_hsm_owner->core.status.fault == 0);
}
static void held_dvp(void);
static int bus_space_map(bus_space_tag_t tag, uint64_t base, uint64_t size,
    int flags, bus_space_handle_t *handle)
{
	if (base == UINT64_C(0x107d508380)) {
		assert(tag == &arm_generic_bs_tag && size == 0x10 && flags == 0);
		assert(!hw.bsc_mapped && hw.bsc_maps == 0 && !hw.hpd_mapped);
		held_hsm(); held_dvp(); hw.bsc_maps++;
		if (step('B')) return EIO;
		hw.bsc_mapped = true; *handle = 29; return 0;
	}
	if (base == UINT64_C(0x107d508200)) {
		assert(tag == &arm_generic_bs_tag && size == 0x58 && flags == 0);
		assert(hw.bsc_mapped && hw.bsc_reads == (hw.edid0 ? 0U : 2U) && !hw.ddc_mapped);
		assert(hw.ddc_maps == 0 && !hw.hpd_mapped);
		held_hsm(); held_dvp(); hw.ddc_maps++;
		if (step('E')) return EIO;
		hw.ddc_mapped = true; *handle = 31; return 0;
	}
	if (base == UINT64_C(0x107c701400)) {
		assert(tag == &arm_generic_bs_tag && size == 0x300 && flags == 0);
		assert(!hw.hpd_mapped && hw.hpd_maps == 0);
		held_hsm(); held_dvp(); hw.hpd_maps++;
		if (step('H')) return EIO;
		hw.hpd_mapped = true; *handle = 23; return 0;
	}
	assert(tag == &arm_generic_bs_tag && base == UINT64_C(0x107c700000));
	assert(size == 0x10 && flags == 0 && !hw.mapped);
	held_hsm(); hw.maps++;
	if (step('M')) return EIO;
	hw.mapped = true; *handle = 17; return 0;
}
static uint32_t bus_space_read_4(bus_space_tag_t tag, bus_space_handle_t handle,
    unsigned int offset)
{
	if (handle == 29) {
		const unsigned int offsets[] = { 0, 4 };
		assert(tag == &arm_generic_bs_tag && hw.bsc_mapped && !hw.ddc_mapped);
		assert(hw.bsc_reads < 2 && offset == offsets[hw.bsc_reads]);
		assert(strstr(log_text, "first BSC resource13 MMIO") != NULL);
		held_hsm(); held_dvp(); hw.bsc_reads++; (void)step(offset == 0 ? 'b' : 'm');
		return offset == 0 ? hw.bsc_status : hw.bsc_mask;
	}
	if (handle == 31) {
		const unsigned int offsets[] = { 0x28, 0x2c, 0x50 };
		assert(tag == &arm_generic_bs_tag && hw.ddc_mapped && hw.bsc_mapped);
		assert(hw.ddc_reads < 3 && offset == offsets[hw.ddc_reads]);
		assert(strstr(log_text, "first controller resource14 MMIO") != NULL);
		held_hsm(); held_dvp(); hw.ddc_reads++;
		(void)step(offset == 0x28 ? 't' : offset == 0x2c ? 'e' : 'f');
		return offset == 0x28 ? hw.ddc_ctl : offset == 0x2c ?
		    hw.ddc_enable : hw.ddc_ctlhi;
	}
	if (handle == 23) {
		assert(tag == &arm_generic_bs_tag && hw.hpd_mapped && offset == 0x1c8);
		assert(hw.hpd_reads == 0 && !hw.waiting);
		assert(strstr(log_text, "first per-controller resource16 MMIO") != NULL);
		held_hsm(); held_dvp(); hw.hpd_reads++; (void)step('h');
		return hw.hpd_raw;
	}
	assert(tag == &arm_generic_bs_tag && handle == 17 && hw.mapped && !hw.waiting);
	assert(offset == 0x04 || offset == 0x08);
	held_hsm(); hw.reads++;
	return offset == 0x04 ? hw.resets : hw.gates;
}
static void bus_space_write_4(bus_space_tag_t tag, bus_space_handle_t handle,
    unsigned int offset, uint32_t value)
{
	assert(tag == &arm_generic_bs_tag && handle == 17 && hw.mapped && !hw.waiting);
	assert(offset == 0x08 && ((hw.gates ^ value) & ~(1U << 3)) == 0);
	held_hsm(); hw.writes++;
	hw.pending = hw.writes == hw.ignore_write ? hw.gates : value;
	hw.waiting = true;
}
static void bus_space_barrier(bus_space_tag_t tag, bus_space_handle_t handle,
    unsigned int offset, unsigned int size, unsigned int flags)
{
	assert(tag == &arm_generic_bs_tag && handle == 17 && hw.mapped && hw.waiting);
	assert(offset == 0 && size == 0x10 && flags == 3);
	held_hsm(); hw.barriers++; hw.gates = hw.pending; hw.waiting = false;
}
#define bcm2712_display_dvp_create actual_dvp_create
#define bcm2712_display_dvp_status actual_dvp_status
#define bcm2712_display_dvp_acquire actual_dvp_acquire
#define bcm2712_display_dvp_release actual_dvp_release
#define bcm2712_display_dvp_destroy actual_dvp_destroy
#include "dvp-native.h"
#undef bcm2712_display_dvp_create
#undef bcm2712_display_dvp_status
#undef bcm2712_display_dvp_acquire
#undef bcm2712_display_dvp_release
#undef bcm2712_display_dvp_destroy
static void held_dvp(void)
{
	assert(hw.mapped && bcm2712_dvp_owner != NULL);
	assert(bcm2712_dvp_owner->core.status.leases == 1);
	assert(bcm2712_dvp_owner->core.status.fault == 0);
	assert(!bcm2712_dvp_owner->core.status.mutation_attempted);
	assert((bcm2712_dvp_owner->core.status.gates & (1U << 3)) == 0);
	assert((bcm2712_dvp_owner->core.status.resets & (1U << 1)) == 0);
}
static void bus_space_unmap(bus_space_tag_t tag, bus_space_handle_t handle, uint64_t size)
{
	if (handle == 31) {
		assert(tag == &arm_generic_bs_tag && size == 0x58 && hw.ddc_mapped);
		assert(hw.bsc_mapped && hw.bsc_reads == (hw.edid0 ? 0U : 2U) &&
		    hw.ddc_reads == (hw.edid0 ? 0U : 3U));
#ifdef BCM2712_DISPLAY_DDC
		if (hw.edid0) assert(!native_ddc.live);
#endif
		held_hsm(); held_dvp(); (void)step('v');
		hw.ddc_unmaps++; hw.ddc_mapped = false; return;
	}
	if (handle == 29) {
		assert(tag == &arm_generic_bs_tag && size == 0x10 && hw.bsc_mapped);
		assert(!hw.ddc_mapped && (hw.edid0 || hw.ddc_unmaps == 1));
		held_hsm(); held_dvp(); (void)step('w');
		hw.bsc_unmaps++; hw.bsc_mapped = false; return;
	}
	assert(!hw.bsc_mapped && !hw.ddc_mapped);
	if (handle == 23) {
		assert(tag == &arm_generic_bs_tag && size == 0x300 && hw.hpd_mapped);
		assert(hw.hpd_reads == 1);
		held_hsm(); held_dvp(); (void)step('u');
		hw.hpd_unmaps++; hw.hpd_mapped = false; return;
	}
	assert(!hw.hpd_mapped);
	assert(tag == &arm_generic_bs_tag && handle == 17 && size == 0x10 && hw.mapped);
	assert(bcm2712_dvp_owner == NULL && !hw.waiting);
	held_hsm(); (void)step('U'); hw.unmaps++; hw.mapped = false;
}
/* Inject genuine process-context failures at each native API boundary. */
#define PROVIDER_CALL(event, call) do { \
    bool saved_irq = in_irq; int result; \
    if (step(event)) in_irq = true; \
    result = (call); in_irq = saved_irq; return result; \
} while (0)
static int bcm2712_display_hsm_create(device_t d, ACPI_HANDLE h,
    struct bcm2712_hsm_provider **out)
{ PROVIDER_CALL('C', actual_hsm_create(d, h, out)); }
static int bcm2712_display_hsm_status(struct bcm2712_hsm_provider *p,
    struct bcm2712_hsm_status *out)
{ PROVIDER_CALL('S', actual_hsm_status(p, out)); }
static int bcm2712_display_hsm_acquire(struct bcm2712_hsm_provider *p,
    unsigned int port, uint32_t rate)
{ PROVIDER_CALL('A', actual_hsm_acquire(p, port, rate)); }
static int bcm2712_display_hsm_release(struct bcm2712_hsm_provider *p, unsigned int port)
{ assert(!hw.mapped && !hw.hpd_mapped && !hw.bsc_mapped && !hw.ddc_mapped && bcm2712_dvp_owner == NULL); PROVIDER_CALL('R', actual_hsm_release(p, port)); }
static int bcm2712_display_hsm_destroy(struct bcm2712_hsm_provider *p)
{ assert(!hw.mapped && !hw.hpd_mapped && !hw.bsc_mapped && !hw.ddc_mapped && bcm2712_dvp_owner == NULL); PROVIDER_CALL('D', actual_hsm_destroy(p)); }
static int bcm2712_display_acpi_resources(device_t d, ACPI_HANDLE h,
    struct bcm2712_display_contract *out)
{
	unsigned int i;
	if (step('Q')) return EIO;
	int result = fixture_hsm_resources(d, h, out);
	if (result != 0) return result;
	out->version = 1;
	for (i = 0; i < BCM2712_DISPLAY_NMEM; i++)
		out->memory[i] = bcm2712_display_resources[i].memory;
	for (i = 0; i < BCM2712_DISPLAY_NIRQ; i++)
		out->irq[i] = bcm2712_display_irqs[i].gsi;
	return 0;
}
static int bcm2712_display_dvp_create(device_t d,
    const struct bcm2712_display_contract *c, const struct bcm2712_display_memory *m,
    bus_space_tag_t t, bus_space_handle_t h, struct bcm2712_dvp_provider **out)
{ held_hsm(); PROVIDER_CALL('c', actual_dvp_create(d, c, m, t, h, out)); }
static int bcm2712_display_dvp_status(struct bcm2712_dvp_provider *p,
    struct bcm2712_dvp_status *out)
{
	bool saved_irq = in_irq; int result;
	held_hsm(); if (step('s')) in_irq = true;
	result = actual_dvp_status(p, out); in_irq = saved_irq;
	if (result == 0 && out->leases == 1 && hw.disturb_lease)
		hw.resets |= 1U << 1;
	return result;
}
static int bcm2712_display_dvp_acquire(struct bcm2712_dvp_provider *p, unsigned int port)
{
	assert(port == 0); held_hsm();
	if (hw.external_gate) hw.gates |= 1U << 3;
	PROVIDER_CALL('a', actual_dvp_acquire(p, port));
}
static int bcm2712_display_dvp_release(struct bcm2712_dvp_provider *p, unsigned int port)
{ assert(port == 0 && !hw.hpd_mapped && !hw.bsc_mapped && !hw.ddc_mapped); held_hsm(); PROVIDER_CALL('r', actual_dvp_release(p, port)); }
static int bcm2712_display_dvp_destroy(struct bcm2712_dvp_provider *p)
{ held_hsm(); PROVIDER_CALL('d', actual_dvp_destroy(p)); }
#undef PROVIDER_CALL
/* The DDC API is controlled here. The I2C algorithm is not part of this fixture. */
#ifdef BCM2712_DISPLAY_DDC
int bcm2712_display_ddc_create(device_t dev,
    const struct bcm2712_display_contract *contract,
    const struct bcm2712_display_memory *bsc, bus_space_handle_t bsc_handle,
    const struct bcm2712_display_memory *ddc, bus_space_handle_t ddc_handle,
    bus_space_tag_t bst, struct bcm2712_ddc_provider **out)
{
	int error;

	held_hsm(); held_dvp();
	assert(dev == &device_token && bsc == &contract->memory[13]);
	assert(ddc == &contract->memory[14] && bst == &arm_generic_bs_tag);
	assert(bsc_handle == 29 && ddc_handle == 31);
	assert(hw.edid0 && hw.bsc_mapped && hw.ddc_mapped && !native_ddc.live);
	assert(bsc->address == UINT64_C(0x107d508380) && bsc->length == 0x10);
	assert(ddc->address == UINT64_C(0x107d508200) && ddc->length == 0x58);
	native_ddc.creates++;
	error = step('j') ? EIO : native_ddc.create_error;
	if ((error == 0 && !native_ddc.null_owner) || native_ddc.create_owner) {
		native_ddc.live = true; *out = &native_ddc.owner;
	}
	return error;
}
int bcm2712_display_ddc_read_edid(struct bcm2712_ddc_provider *owner,
    uint8_t *data, size_t length)
{
	held_hsm(); held_dvp();
	assert(native_ddc.live && owner == &native_ddc.owner && !native_ddc.read_called);
	assert(hw.bsc_mapped && hw.ddc_mapped && length == BCM2712_DDC_EDID_LENGTH);
	native_ddc.reads++; native_ddc.read_called = true;
	if (step('k')) { native_ddc.read_error = EIO; native_ddc.native_fault = EIO; }
	/* Poison even failed output: the probe must never print a failed payload. */
	memcpy(data, native_ddc.bytes, length);
	return native_ddc.read_error;
}
int bcm2712_display_ddc_status(struct bcm2712_ddc_provider *owner,
    struct bcm2712_ddc_status *out)
{
	held_hsm(); held_dvp();
	assert(native_ddc.live && owner == &native_ddc.owner && native_ddc.read_called);
	assert(hw.bsc_mapped && hw.ddc_mapped && !native_ddc.status_called);
	native_ddc.statuses++;
	if (step('l')) return EIO;
	if (native_ddc.status_error) return native_ddc.status_error;
	native_ddc.status_called = true;
	*out = (struct bcm2712_ddc_status){
	    .core = { .commands = 5, .polls = 9, .enable = 0,
	        .fault = native_ddc.core_fault, .active = native_ddc.active,
	        .mutation_attempted = true },
	    .bsc_status = 0, .bsc_mask = 2, .status_valid = !native_ddc.invalid_status,
	    .mask_valid = !native_ddc.invalid_mask,
	    .fault = native_ddc.native_fault };
	return 0;
}
int bcm2712_display_ddc_destroy(struct bcm2712_ddc_provider *owner)
{
	held_hsm(); held_dvp();
	assert(native_ddc.live && owner == &native_ddc.owner && native_ddc.status_called);
	assert(hw.bsc_mapped && hw.ddc_mapped);
	assert(!native_ddc.native_fault && !native_ddc.core_fault && !native_ddc.active);
	native_ddc.destroys++;
	if (step('x')) return EIO;
	if (native_ddc.destroy_error) return native_ddc.destroy_error;
	native_ddc.live = false; return 0;
}
#endif
static void payload_guard(void)
{
	assert(!hw.bsc_mapped && !hw.ddc_mapped && !hw.mapped && !hw.hpd_mapped);
	assert(bcm2712_hsm_owner == NULL && bcm2712_dvp_owner == NULL);
#ifdef BCM2712_DISPLAY_DDC
	assert(!native_ddc.live);
#endif
}
#include "probe.h"


static void
prepare(uint32_t state, uint32_t rate)
{
	reset(state, rate);
	assert(bcm2712_dvp_owner == NULL && !hw.mapped);
	memset(&hw, 0, sizeof(hw)); hw.gates = 0xa5a50018;
#ifdef BCM2712_DISPLAY_DDC
	assert(!native_ddc.live);
	memset(&native_ddc, 0, sizeof(native_ddc));
	for (unsigned int i = 0; i < sizeof(native_ddc.bytes); i++)
		native_ddc.bytes[i] = (uint8_t)(i * 73U);
#endif
	events[0] = 0; event_count = fail_step = 0;
	CHECK(fdt_create_empty_tree(blob, sizeof(blob)) == 0);
	tree = blob;
	genfb = wsdisplay = cleanup_error = false;
	find_device = right_driver = right_node = render = true;
	early_framebuffer = references = 0;
	log_text[0] = 0; log_size = 0;
	driver.name = "v3d";
	drm = (struct drm_device){ &device_token, &driver, true, &fixture_minor };
	fixture_minor = (struct drm_minor){ DRM_MINOR_RENDER, &drm };
	fixture_node.ad_device = &device_token;
}

static void
refusal(struct properties *p, int expected)
{
	int actual = bcm2712_hsm_probe_modcmd(MODULE_CMD_INIT, p);

	if (actual != expected)
		fprintf(stderr, "refusal at check %u: got %d, expected %d\n",
		    checks, actual, expected);
	CHECK(actual == expected);
	CHECK(fw.calls == 0 && fw.sets == 0 && references == 0);
	CHECK(hw.maps == 0 && hw.reads == 0 && hw.writes == 0);
	CHECK(hw.hpd_maps == 0 && hw.hpd_reads == 0 && hw.hpd_unmaps == 0);
	CHECK(hw.bsc_maps == 0 && hw.bsc_reads == 0 && hw.ddc_maps == 0 && hw.ddc_reads == 0);
	CHECK(event_count == 0 && resource_calls == 0);
	CHECK(bcm2712_hsm_owner == NULL);
	CHECK(strstr(log_text, "finite cycle complete") == NULL);
}

/* Only the test's simulated reboot can discard retained native owners/maps. */
static void reboot_model(void)
{
#ifdef BCM2712_DISPLAY_DDC
	native_ddc.live = false;
#endif
	if (bcm2712_dvp_owner != NULL) {
		kmem_free(bcm2712_dvp_owner, sizeof(*bcm2712_dvp_owner));
		bcm2712_dvp_owner = NULL;
	}
	hw.mapped = hw.hpd_mapped = hw.bsc_mapped = hw.ddc_mapped = hw.waiting = false;
	if (bcm2712_hsm_owner != NULL) {
		kmem_free(bcm2712_hsm_owner, sizeof(*bcm2712_hsm_owner));
		bcm2712_hsm_owner = NULL;
	}
	CHECK(allocated == freed && references == 0);
}
static void dvp_probe_tests(void)
{
	struct properties p = { .expected = "EmberBSD:accepted-kernel", .cold = true };
	const char *modes[] = { "observe", "gate0" };
	const char *order[] = { "CSASQMcsdURSD", "CSASQMcsasrsdURSD" };
	unsigned int mode, i, stop;

	prepare(1, 0); p.mode = ""; refusal(&p, EINVAL);
	prepare(1, 0); p.mode = "other"; refusal(&p, EINVAL);
	prepare(1, 0); p.mode = NULL; p.wrong_mode_type = true; refusal(&p, EINVAL);
	p.wrong_mode_type = false;
	for (mode = 0; mode < 2; mode++) {
		p.mode = modes[mode];
		prepare(1, 0); early_framebuffer = 1; refusal(&p, EBUSY);
		prepare(1, 0); tree = NULL; refusal(&p, EIO);
		prepare(1, 0); render = false; refusal(&p, ENODEV);
		prepare(0, 0);
		CHECK(bcm2712_hsm_probe_modcmd(MODULE_CMD_INIT, &p) == 0);
		CHECK(strcmp(events, order[mode]) == 0);
		CHECK(hw.maps == 1 && hw.unmaps == 1 && !hw.mapped);
		CHECK(hw.reads == (mode == 0 ? 2U : 10U));
		CHECK(hw.writes == (mode == 0 ? 0U : 2U) && hw.barriers == hw.writes);
		CHECK(hw.gates == 0xa5a50018 && hw.resets == 0);
		CHECK(bcm2712_dvp_owner == NULL && bcm2712_hsm_owner == NULL);
		CHECK(allocated == freed && references == 0);
		/* A native status/context failure cannot become success or a retry. */
		for (i = 1; i <= strlen(order[mode]); i++) {
			char operation = order[mode][i - 1];
			const unsigned int destroyed = mode == 0 ? 9 : 13;
			if (operation == 'U') continue; /* Native unmap has no error result. */
			prepare(1, 120000000); fail_step = i;
			CHECK(bcm2712_hsm_probe_modcmd(MODULE_CMD_INIT, &p) ==
			    (operation == 'Q' || operation == 'M' ? EIO : EWOULDBLOCK));
			CHECK(strstr(log_text, "finite cycle complete") == NULL);
			CHECK(strncmp(events, order[mode], i) == 0);
			if (operation == 'S' || operation == 's') {
				CHECK(event_count == i);
				CHECK(strstr(log_text, "current status unknown") != NULL);
			}
			if (operation == 's' && i == 8)
				CHECK(strstr(log_text, "DVP probe sample valid 0 at none") != NULL);
			if (operation == 's' && i > 8)
				CHECK(strstr(log_text, "DVP probe sample valid 1 at") != NULL);
			if (i >= 4) CHECK(bcm2712_hsm_owner != NULL);
			CHECK(hw.mapped == (i >= 7 && i <= destroyed));
			CHECK((bcm2712_dvp_owner != NULL) == (i >= 8 && i <= destroyed));
			if (hw.mapped) CHECK(bcm2712_hsm_owner->core.status.leases == 1);
			stop = event_count;
			CHECK(bcm2712_hsm_probe_modcmd(MODULE_CMD_FINI, NULL) == 0);
			CHECK(event_count == stop);
			reboot_model();
		}
		/* Every HSM transport error is also tested around the DVP lifetime. */
		for (i = 1; i <= 18; i++) {
			prepare(0, 0); fw.fail_call = i;
			CHECK(bcm2712_hsm_probe_modcmd(MODULE_CMD_INIT, &p) == ETIMEDOUT);
			CHECK(fw.calls == i && !hw.mapped && bcm2712_dvp_owner == NULL);
			CHECK(hw.maps == (i >= 12 ? 1U : 0U) && hw.maps == hw.unmaps);
			CHECK(strstr(log_text, "finite cycle complete") == NULL);
			reboot_model();
		}
	}
	p.mode = "observe"; prepare(1, 120000000); hw.resets = 1U << 1;
	CHECK(bcm2712_hsm_probe_modcmd(MODULE_CMD_INIT, &p) == EBUSY);
	CHECK(hw.reads == 2 && hw.writes == 0 && hw.resets == (1U << 1));
	CHECK(hw.mapped && bcm2712_dvp_owner != NULL);
	CHECK(strstr(log_text, "finite cycle complete") == NULL); reboot_model();
	p.mode = "gate0"; prepare(1, 120000000); hw.gates &= ~(1U << 3);
	CHECK(bcm2712_hsm_probe_modcmd(MODULE_CMD_INIT, &p) == 0);
	CHECK(hw.writes == 0 && hw.reads == 6 && hw.gates == 0xa5a50010);
	prepare(1, 120000000); hw.resets = 1U << 1;
	CHECK(bcm2712_hsm_probe_modcmd(MODULE_CMD_INIT, &p) == EBUSY);
	CHECK(hw.writes == 0 && hw.mapped && bcm2712_dvp_owner != NULL);
	CHECK(bcm2712_dvp_owner->core.status.leases == 0);
	CHECK(strstr(log_text, "finite cycle complete") == NULL); reboot_model();
	for (i = 1; i <= 2; i++) {
		prepare(1, 120000000); hw.ignore_write = i;
		CHECK(bcm2712_hsm_probe_modcmd(MODULE_CMD_INIT, &p) == EIO);
		CHECK(hw.writes == i && hw.mapped && bcm2712_dvp_owner != NULL);
		CHECK(bcm2712_dvp_owner->core.status.fault == EIO);
		CHECK(bcm2712_dvp_owner->core.status.mutation_attempted);
		CHECK(bcm2712_dvp_owner->core.status.leases == (i == 1 ? 0U : 1U));
		CHECK(bcm2712_hsm_owner->core.status.leases == 1);
		CHECK(strstr(log_text, "pre-failure cached values only; current status unknown") != NULL);
		CHECK(strstr(log_text, i == 1 ? "sample valid 1 at initial-status" :
		    "sample valid 1 at acquired-status") != NULL); reboot_model();
	}
	prepare(1, 120000000); hw.disturb_lease = true;
	CHECK(bcm2712_hsm_probe_modcmd(MODULE_CMD_INIT, &p) == EIO);
	CHECK(hw.mapped && hw.writes == 1 && bcm2712_dvp_owner->core.status.fault == EIO);
	CHECK(bcm2712_dvp_owner->core.status.leases == 1); reboot_model();
}

static void hpd_probe_tests(void)
{
	struct properties p = { .expected = "EmberBSD:accepted-kernel", .cold = true,
	    .hpd_mode = "sample0" };
	const char *order = "CSASQMcsasHhursdURSD";
	unsigned int i, stop;

	prepare(1, 0); p.hpd_mode = ""; refusal(&p, EINVAL);
	prepare(1, 0); p.hpd_mode = "other"; refusal(&p, EINVAL);
	prepare(1, 0); p.hpd_mode = NULL; p.wrong_hpd_type = true; refusal(&p, EINVAL);
	p.hpd_mode = "sample0"; p.wrong_hpd_type = false;
	prepare(1, 0); p.mode = "observe"; refusal(&p, EINVAL);
	prepare(1, 0); p.mode = "gate0"; refusal(&p, EINVAL);
	prepare(1, 0); p.mode = ""; refusal(&p, EINVAL);
	prepare(1, 0); p.mode = NULL; p.wrong_mode_type = true; refusal(&p, EINVAL);
	p.wrong_mode_type = false;
	prepare(1, 0); early_framebuffer = 1; refusal(&p, EBUSY);
	prepare(1, 0); genfb = true; refusal(&p, EBUSY);
	prepare(1, 0); wsdisplay = true; refusal(&p, EBUSY);
	prepare(1, 0); tree = NULL; refusal(&p, EIO);
	prepare(1, 0); render = false; refusal(&p, ENODEV);
	prepare(1, 0); p.expected = "wrong"; refusal(&p, EPROGMISMATCH);
	p.expected = "EmberBSD:accepted-kernel";
	/* Arbitrary foreign bits remain data; only bit0 is the HPD level. */
	for (i = 0; i < 3; i++) {
		prepare(i == 0 ? 0 : 1, i == 2 ? 240000000 : 0);
		hw.gates &= ~(1U << 3); hw.hpd_raw = 0xa5a50000U | (i & 1U);
		CHECK(bcm2712_hsm_probe_modcmd(MODULE_CMD_INIT, &p) == 0);
		CHECK(strcmp(events, order) == 0);
		CHECK(hw.maps == 1 && hw.unmaps == 1 && !hw.mapped);
		CHECK(hw.hpd_maps == 1 && hw.hpd_reads == 1 && hw.hpd_unmaps == 1);
		CHECK(!hw.hpd_mapped && hw.reads == 6 && hw.writes == 0 && hw.barriers == 0);
		CHECK(hw.gates == 0xa5a50010 && hw.resets == 0);
		CHECK(strstr(log_text, i == 1 ? "raw 0xa5a50001 connected 1" :
		    "raw 0xa5a50000 connected 0") != NULL);
		CHECK(strstr(log_text, "one HPD0 sample") != NULL);
		CHECK(bcm2712_dvp_owner == NULL && bcm2712_hsm_owner == NULL);
		CHECK(allocated == freed && references == 0);
	}
	for (i = 1; i <= strlen(order); i++) {
		char operation = order[i - 1];
		if (operation == 'h' || operation == 'u' || operation == 'U') continue;
		prepare(1, 120000000); hw.gates &= ~(1U << 3); fail_step = i;
		CHECK(bcm2712_hsm_probe_modcmd(MODULE_CMD_INIT, &p) ==
		    (operation == 'Q' || operation == 'M' || operation == 'H' ? EIO : EWOULDBLOCK));
		CHECK(strstr(log_text, "finite cycle complete") == NULL);
		CHECK(strncmp(events, order, i) == 0);
		CHECK(!hw.hpd_mapped && hw.hpd_reads == (i > 13 ? 1U : 0U));
		CHECK(hw.hpd_unmaps == hw.hpd_reads);
		CHECK(hw.mapped == (i >= 7 && i <= 16));
		CHECK((bcm2712_dvp_owner != NULL) == (i >= 8 && i <= 16));
		if (i >= 4) CHECK(bcm2712_hsm_owner != NULL);
		if (hw.mapped) CHECK(bcm2712_hsm_owner->core.status.leases == 1);
		if (operation == 'S' || operation == 's') {
			CHECK(event_count == i);
			CHECK(strstr(log_text, "current status unknown") != NULL);
		}
		if (i == 11) {
			CHECK(strstr(log_text, "stopped at HPD0-map") != NULL);
			CHECK(strstr(log_text, "resource16 mapping retained 0 sample valid 0") != NULL);
			CHECK(bcm2712_dvp_owner->core.status.leases == 1);
		}
		if (i > 13)
			CHECK(strstr(log_text, "resource16 mapping retained 0 sample valid 1") != NULL);
		stop = event_count;
		CHECK(bcm2712_hsm_probe_modcmd(MODULE_CMD_FINI, NULL) == 0);
		CHECK(event_count == stop); reboot_model();
	}
	for (i = 1; i <= 18; i++) {
		prepare(0, 0); hw.gates &= ~(1U << 3); fw.fail_call = i;
		CHECK(bcm2712_hsm_probe_modcmd(MODULE_CMD_INIT, &p) == ETIMEDOUT);
		CHECK(fw.calls == i && !hw.mapped && !hw.hpd_mapped);
		CHECK(bcm2712_dvp_owner == NULL);
		CHECK(hw.hpd_reads == (i >= 12 ? 1U : 0U));
		CHECK(hw.hpd_maps == hw.hpd_reads && hw.hpd_unmaps == hw.hpd_reads);
		CHECK(strstr(log_text, "finite cycle complete") == NULL); reboot_model();
	}
	/* Inherited closed gate and asserted reset refuse before native acquire. */
	for (i = 0; i < 2; i++) {
		prepare(1, 120000000);
		if (i == 1) { hw.gates &= ~(1U << 3); hw.resets = 1U << 1; }
		CHECK(bcm2712_hsm_probe_modcmd(MODULE_CMD_INIT, &p) == EBUSY);
		CHECK(hw.writes == 0 && hw.reads == 2 && hw.hpd_maps == 0);
		CHECK(strchr(events, 'a') == NULL && hw.mapped);
		CHECK(bcm2712_dvp_owner->core.status.leases == 0);
		CHECK(bcm2712_hsm_owner->core.status.leases == 1);
		CHECK(strstr(log_text, "finite cycle complete") == NULL); reboot_model();
	}
	/* An external gate change can cause a native write, but never HPD MMIO. */
	prepare(1, 120000000); hw.gates &= ~(1U << 3); hw.external_gate = true;
	CHECK(bcm2712_hsm_probe_modcmd(MODULE_CMD_INIT, &p) == EIO);
	CHECK(hw.writes == 1 && hw.barriers == 1 && hw.hpd_maps == 0 && hw.hpd_reads == 0);
	CHECK(hw.mapped && bcm2712_dvp_owner->core.status.leases == 1);
	CHECK(bcm2712_dvp_owner->core.status.mutation_attempted);
	CHECK(bcm2712_hsm_owner->core.status.leases == 1);
	CHECK(strstr(log_text, "sample valid 1 at acquired-status") != NULL);
	CHECK(strstr(log_text, "finite cycle complete") == NULL); reboot_model();
}

static void ddc_probe_tests(void)
{
	struct properties p = { .expected = "EmberBSD:accepted-kernel", .cold = true,
	    .ddc_mode = "observe0" };
	const char *order = "CSASQMcsasBbmEtefvwrsdURSD";
	unsigned int flags, i, stop;

	prepare(1, 0); p.ddc_mode = ""; refusal(&p, EINVAL);
	prepare(1, 0); p.ddc_mode = "other"; refusal(&p, EINVAL);
	prepare(1, 0); p.ddc_mode = NULL; p.wrong_ddc_type = true; refusal(&p, EINVAL);
	p.ddc_mode = "observe0"; p.wrong_ddc_type = false;
	prepare(1, 0); p.mode = "observe"; refusal(&p, EINVAL);
	prepare(1, 0); p.mode = "gate0"; refusal(&p, EINVAL);
	prepare(1, 0); p.mode = ""; refusal(&p, EINVAL);
	prepare(1, 0); p.mode = NULL; p.wrong_mode_type = true; refusal(&p, EINVAL);
	p.wrong_mode_type = false;
	prepare(1, 0); p.hpd_mode = "sample0"; refusal(&p, EINVAL);
	prepare(1, 0); p.hpd_mode = ""; refusal(&p, EINVAL);
	prepare(1, 0); p.hpd_mode = NULL; p.wrong_hpd_type = true; refusal(&p, EINVAL);
	p.wrong_hpd_type = false;
	prepare(1, 0); early_framebuffer = 1; refusal(&p, EBUSY);
	prepare(1, 0); tree = NULL; refusal(&p, EIO);
	prepare(1, 0); render = false; refusal(&p, ENODEV);
	/* Every predicate combination is still a successful read-only snapshot. */
	for (flags = 0; flags < 32; flags++) {
		prepare(flags == 0 ? 0 : 1, flags == 31 ? 240000000 : 0);
		hw.gates &= ~(1U << 3);
		hw.bsc_mask = 0xa5a50002U & ~((flags & 1U) << 1);
		hw.bsc_status = 0xa5a50000U | (flags & 2U);
		hw.ddc_ctl = 0x90U | ((flags & 4U) << 4);
		/* Latched NOACK/condition/unknown bits do not prove active transfer. */
		hw.ddc_enable = 0x80000074U | ((flags >> 3) & 3U);
		hw.ddc_ctlhi = flags & 1U ? 0 : UINT32_MAX;
		CHECK(bcm2712_hsm_probe_modcmd(MODULE_CMD_INIT, &p) == 0);
		CHECK(strcmp(events, order) == 0);
		CHECK(hw.maps == 1 && hw.unmaps == 1 && !hw.mapped);
		CHECK(hw.bsc_maps == 1 && hw.bsc_reads == 2 && hw.bsc_unmaps == 1);
		CHECK(hw.ddc_maps == 1 && hw.ddc_reads == 3 && hw.ddc_unmaps == 1);
		CHECK(!hw.bsc_mapped && !hw.ddc_mapped && hw.hpd_maps == 0);
		CHECK(hw.reads == 6 && hw.writes == 0 && hw.barriers == 0);
		CHECK(hw.bsc_mask == (0xa5a50002U & ~((flags & 1U) << 1)));
		CHECK(hw.bsc_status == (0xa5a50000U | (flags & 2U)));
		CHECK(hw.ddc_ctl == (0x90U | ((flags & 4U) << 4)));
		CHECK(hw.ddc_enable == (0x80000074U | ((flags >> 3) & 3U)));
		CHECK(hw.ddc_ctlhi == (flags & 1U ? 0 : UINT32_MAX));
		CHECK(strstr(log_text, flags == 0 ? "candidate_quiescent 1" :
		    "candidate_quiescent 0") != NULL);
		CHECK(strstr(log_text, "DDC0 snapshot complete; no idle or transfer claim") != NULL);
		CHECK(bcm2712_hsm_owner == NULL && bcm2712_dvp_owner == NULL);
		CHECK(allocated == freed && references == 0);
	}
	for (i = 1; i <= strlen(order); i++) {
		char operation = order[i - 1];
		if (strchr("bmtefvwU", operation) != NULL) continue;
		prepare(1, 120000000); hw.gates &= ~(1U << 3); fail_step = i;
		CHECK(bcm2712_hsm_probe_modcmd(MODULE_CMD_INIT, &p) ==
		    (strchr("QMBE", operation) != NULL ? EIO : EWOULDBLOCK));
		CHECK(strstr(log_text, "finite cycle complete") == NULL);
		CHECK(strncmp(events, order, i) == 0);
		CHECK(!hw.ddc_mapped && hw.bsc_mapped == (i > 11 && i < 19));
		CHECK(hw.bsc_reads == (i > 13 ? 2U : 0U));
		CHECK(hw.ddc_reads == (i > 19 ? 3U : 0U));
		CHECK(hw.mapped == (i >= 7 && i <= 22));
		CHECK((bcm2712_dvp_owner != NULL) == (i >= 8 && i <= 22));
		if (i >= 4) CHECK(bcm2712_hsm_owner != NULL);
		if (hw.mapped) CHECK(bcm2712_hsm_owner->core.status.leases == 1);
		if (operation == 'S' || operation == 's') CHECK(event_count == i);
		if (i == 11 || i == 14) {
			CHECK(strstr(log_text, i == 11 ? "stopped at BSC-map" : "stopped at DDC0-map") != NULL);
			CHECK(strstr(log_text, i == 11 ? "BSC sample valid 0 DDC sample valid 0" :
			    "BSC sample valid 1 DDC sample valid 0") != NULL);
			CHECK(strstr(log_text, "candidate_quiescent") == NULL);
			CHECK(strstr(log_text, "retained cached: ctl") == NULL);
		}
		if (i > 19) {
			CHECK(strstr(log_text, "BSC sample valid 1 DDC sample valid 1") != NULL);
			CHECK(strstr(log_text, "retained cached: ctl") != NULL);
		}
		stop = event_count;
		CHECK(bcm2712_hsm_probe_modcmd(MODULE_CMD_FINI, NULL) == 0);
		CHECK(event_count == stop); reboot_model();
	}
	for (i = 1; i <= 18; i++) {
		prepare(0, 0); hw.gates &= ~(1U << 3); fw.fail_call = i;
		CHECK(bcm2712_hsm_probe_modcmd(MODULE_CMD_INIT, &p) == ETIMEDOUT);
		CHECK(fw.calls == i && !hw.mapped && !hw.bsc_mapped && !hw.ddc_mapped);
		CHECK(bcm2712_dvp_owner == NULL);
		CHECK(hw.bsc_reads == (i >= 12 ? 2U : 0U) && hw.ddc_reads == (i >= 12 ? 3U : 0U));
		CHECK(strstr(log_text, "finite cycle complete") == NULL); reboot_model();
	}
	for (i = 0; i < 2; i++) {
		prepare(1, 120000000);
		if (i == 1) { hw.gates &= ~(1U << 3); hw.resets = 1U << 1; }
		CHECK(bcm2712_hsm_probe_modcmd(MODULE_CMD_INIT, &p) == EBUSY);
		CHECK(hw.reads == 2 && hw.writes == 0 && hw.bsc_maps == 0 && hw.ddc_maps == 0);
		CHECK(bcm2712_dvp_owner->core.status.leases == 0);
		CHECK(bcm2712_hsm_owner->core.status.leases == 1); reboot_model();
	}
	prepare(1, 120000000); hw.gates &= ~(1U << 3); hw.external_gate = true;
	CHECK(bcm2712_hsm_probe_modcmd(MODULE_CMD_INIT, &p) == EIO);
	CHECK(hw.writes == 1 && hw.bsc_maps == 0 && hw.ddc_maps == 0);
	CHECK(hw.mapped && bcm2712_dvp_owner->core.status.leases == 1);
	CHECK(bcm2712_dvp_owner->core.status.mutation_attempted); reboot_model();
}

static void edid_no_payload(void)
{
	CHECK(strstr(log_text, "BCM2712_EDID0") == NULL);
	CHECK(strstr(log_text, "finite cycle complete") == NULL);
	CHECK(references == 0);
}
#ifdef BCM2712_DISPLAY_DDC
static void prepare_edid(uint32_t state, uint32_t rate)
{
	prepare(state, rate); hw.edid0 = true; hw.gates &= ~(1U << 3);
}
static void edid_freed(void)
{
	CHECK(!native_ddc.live && !hw.bsc_mapped && !hw.ddc_mapped && !hw.mapped);
	CHECK(bcm2712_dvp_owner == NULL && bcm2712_hsm_owner == NULL);
	CHECK(allocated == freed && references == 0);
}
static void edid_payload(void)
{
	const char *line = strstr(log_text, "BCM2712_EDID0 begin length=128\n");
	char expected[128];
	unsigned int i, j;
	int length;

	CHECK(line != NULL); line += strlen("BCM2712_EDID0 begin length=128\n");
	for (i = 0; i < BCM2712_DDC_EDID_LENGTH; i += 16) {
		length = snprintf(expected, sizeof(expected), "BCM2712_EDID0 data %02x:", i);
		for (j = 0; j < 16; j++)
			length += snprintf(expected + length, sizeof(expected) - (size_t)length,
			    " %02x", native_ddc.bytes[i + j]);
		CHECK((size_t)length + 1 < sizeof(expected));
		expected[length++] = '\n'; expected[length] = 0;
		CHECK(strncmp(line, expected, (size_t)length) == 0); line += length;
	}
	CHECK(strncmp(line, "BCM2712_EDID0 end length=128\n", strlen("BCM2712_EDID0 end length=128\n")) == 0);
	CHECK(strstr(line + strlen("BCM2712_EDID0 end length=128\n"), "BCM2712_EDID0") == NULL);
}
#endif
static void edid_probe_tests(void)
{
	struct properties p = { .expected = "EmberBSD:accepted-kernel", .cold = true,
	    .ddc_mode = "edid0" };
#ifdef BCM2712_DISPLAY_DDC
	const char *order = "CSASQMcsasBEjklxvwrsdURSD";
	const int clean_errors[] = { EBADMSG, EBUSY };
	unsigned int i, stop;
	int error;
#endif

	/* Conflicts are invalid even if a feature-disabled kernel rejects EDID0. */
	prepare(1, 120000000); p.hpd_mode = "observe0"; refusal(&p, EINVAL);
	prepare(1, 120000000); p.hpd_mode = NULL; p.wrong_hpd_type = true; refusal(&p, EINVAL);
	prepare(1, 120000000); p.wrong_hpd_type = false; p.mode = "observe"; refusal(&p, EINVAL);
	prepare(1, 120000000); p.mode = NULL; p.wrong_mode_type = true; refusal(&p, EINVAL);
	p.wrong_mode_type = false;
#ifndef BCM2712_DISPLAY_DDC
	prepare(1, 120000000); refusal(&p, EOPNOTSUPP); edid_no_payload();
#else
	prepare_edid(1, 120000000); p.cold = false; refusal(&p, EINVAL); p.cold = true;
	prepare_edid(1, 120000000); p.expected = "wrong"; refusal(&p, EPROGMISMATCH);
	p.expected = "EmberBSD:accepted-kernel";
	prepare_edid(1, 120000000); early_framebuffer = 1; refusal(&p, EBUSY);
	prepare_edid(1, 120000000); genfb = true; refusal(&p, EBUSY);
	prepare_edid(1, 120000000); wsdisplay = true; refusal(&p, EBUSY);
	prepare_edid(1, 120000000); tree = NULL; refusal(&p, EIO);
	prepare_edid(1, 120000000); render = false; refusal(&p, ENODEV);
	for (i = 0; i < 3; i++) {
		prepare_edid(i == 0 ? 0 : 1, i == 2 ? 240000000 : 0);
		CHECK(bcm2712_hsm_probe_modcmd(MODULE_CMD_INIT, &p) == 0);
		CHECK(strcmp(events, order) == 0);
		CHECK(native_ddc.creates == 1 && native_ddc.reads == 1 &&
		    native_ddc.statuses == 1 && native_ddc.destroys == 1);
		CHECK(hw.bsc_maps == 1 && hw.bsc_unmaps == 1 && hw.bsc_reads == 0);
		CHECK(hw.ddc_maps == 1 && hw.ddc_unmaps == 1 && hw.ddc_reads == 0);
		CHECK(hw.reads == 6 && hw.writes == 0 && hw.barriers == 0);
		CHECK(strstr(log_text, "native_fault 0 core_fault 0 active 0 commands 5 polls 9") != NULL);
		CHECK(strstr(log_text, "mutation_attempted 1 BSC_status 0 status_valid 1 BSC_mask 0x2 mask_valid 1") != NULL);
		edid_freed(); edid_payload();
	}
	/* Every API/map failure, including all cleanup stages after valid data. */
	for (i = 1; i <= strlen(order); i++) {
		char operation = order[i - 1];
		if (strchr("vwU", operation) != NULL) continue;
		prepare_edid(1, 120000000); fail_step = i;
		error = bcm2712_hsm_probe_modcmd(MODULE_CMD_INIT, &p);
		CHECK(error == (strchr("QMBEjklx", operation) != NULL ? EIO : EWOULDBLOCK));
		CHECK(strncmp(events, order, i) == 0); edid_no_payload();
		if (strchr("BEj", operation) != NULL) {
			edid_freed();
			CHECK(event_count > i && native_ddc.reads == 0);
		} else {
			if (strchr("ASRD", operation) == NULL)
				CHECK(event_count == i + (operation == 'k' ? 1U : 0U));
			CHECK(native_ddc.live == (operation == 'k' || operation == 'l' || operation == 'x'));
			CHECK(hw.bsc_mapped == native_ddc.live && hw.ddc_mapped == native_ddc.live);
			CHECK(hw.mapped == (i >= 7 && i <= 21));
			CHECK((bcm2712_dvp_owner != NULL) == (i >= 8 && i <= 21));
			if (i >= 4) CHECK(bcm2712_hsm_owner != NULL);
		}
		stop = event_count;
		CHECK(bcm2712_hsm_probe_modcmd(MODULE_CMD_FINI, NULL) == 0);
		CHECK(event_count == stop); reboot_model();
	}
	/* Successful transport with invalid EDID and pre-invoke acquire refusal. */
	for (i = 0; i < sizeof(clean_errors) / sizeof(clean_errors[0]); i++) {
		prepare_edid(1, 120000000); native_ddc.read_error = clean_errors[i];
		CHECK(bcm2712_hsm_probe_modcmd(MODULE_CMD_INIT, &p) == clean_errors[i]);
		CHECK(strcmp(events, order) == 0); edid_freed(); edid_no_payload();
		CHECK(strstr(log_text, "cleanup complete; operation error") != NULL);
	}
	/* Fault/status/destroy uncertainty must keep both controller maps and clocks. */
	for (i = 0; i < 9; i++) {
		prepare_edid(1, 120000000);
		switch (i) {
		case 0: native_ddc.native_fault = EIO; break;
		case 1: native_ddc.core_fault = ETIMEDOUT; break;
		case 2: native_ddc.active = true; break;
		case 3: native_ddc.status_error = EWOULDBLOCK; break;
		case 4: native_ddc.destroy_error = EWOULDBLOCK; break;
		case 5: native_ddc.create_error = EIO; native_ddc.create_owner = true; break;
		case 6: native_ddc.null_owner = true; break;
		case 7: native_ddc.invalid_status = true; break;
		case 8: native_ddc.invalid_mask = true; break;
		}
		error = bcm2712_hsm_probe_modcmd(MODULE_CMD_INIT, &p);
		CHECK(error == (i == 1 ? ETIMEDOUT : i == 2 ? EBUSY :
		    i == 3 || i == 4 ? EWOULDBLOCK : EIO));
		CHECK(hw.bsc_mapped && hw.ddc_mapped && hw.mapped);
		CHECK(bcm2712_hsm_owner->core.status.leases == 1);
		CHECK(bcm2712_dvp_owner->core.status.leases == 1);
		CHECK(native_ddc.live == (i != 6));
		CHECK(native_ddc.destroys == (i == 4 ? 1U : 0U));
		edid_no_payload(); reboot_model();
	}
	/* An operation error never hides a subsequent cleanup failure. */
	for (i = 19; i <= strlen(order); i++) {
		if (order[i - 1] == 'U') continue;
		prepare_edid(1, 120000000); native_ddc.read_error = EBADMSG; fail_step = i;
		CHECK(bcm2712_hsm_probe_modcmd(MODULE_CMD_INIT, &p) == EWOULDBLOCK);
		edid_no_payload(); CHECK(event_count >= i); reboot_model();
	}
	for (i = 1; i <= 18; i++) {
		prepare_edid(0, 0); fw.fail_call = i;
		CHECK(bcm2712_hsm_probe_modcmd(MODULE_CMD_INIT, &p) == ETIMEDOUT);
		CHECK(fw.calls == i && !hw.mapped && !hw.bsc_mapped && !hw.ddc_mapped);
		CHECK(bcm2712_dvp_owner == NULL && !native_ddc.live);
		CHECK(native_ddc.reads == (i >= 12 ? 1U : 0U));
		edid_no_payload(); reboot_model();
	}
	for (i = 0; i < 2; i++) {
		prepare_edid(1, 120000000);
		if (i == 0) hw.gates |= 1U << 3; else hw.resets = 1U << 1;
		CHECK(bcm2712_hsm_probe_modcmd(MODULE_CMD_INIT, &p) == EBUSY);
		CHECK(native_ddc.creates == 0 && hw.bsc_maps == 0 && hw.writes == 0);
		edid_no_payload(); reboot_model();
	}
	prepare_edid(1, 120000000); hw.external_gate = true;
	CHECK(bcm2712_hsm_probe_modcmd(MODULE_CMD_INIT, &p) == EIO);
	CHECK(native_ddc.creates == 0 && hw.bsc_maps == 0 && hw.writes == 1);
	edid_no_payload(); reboot_model();
#endif
}

int
main(void)
{
	struct properties p = { .expected = "EmberBSD:accepted-kernel", .cold = true };
	unsigned int i;
	int chosen, child;
	uint32_t off, size;

	CHECK(existing_hsm_main() == 0);
	prepare(1, 0); refusal(NULL, EINVAL);
	prepare(1, 0); p.cold = false; refusal(&p, EINVAL); p.cold = true;
	prepare(1, 0); p.expected = "wrong"; refusal(&p, EPROGMISMATCH);
	p.expected = "EmberBSD:accepted-kernel";
	prepare(1, 0); early_framebuffer = 0x1000; refusal(&p, EBUSY);
	prepare(1, 0); genfb = true; refusal(&p, EBUSY);
	prepare(1, 0); wsdisplay = true; refusal(&p, EBUSY);
	prepare(1, 0); tree = NULL; refusal(&p, EIO);
	prepare(1, 0); fdt_set_magic(blob, 0); refusal(&p, EIO);
	prepare(1, 0); fdt_set_totalsize(blob, sizeof(struct fdt_header)); refusal(&p, EIO);
	/* Header and nodes are valid, but the reserve map has no terminator. */
	prepare(1, 0); off = sizeof(blob) - sizeof(struct fdt_reserve_entry);
	fdt_set_off_mem_rsvmap(blob, off);
	((struct fdt_reserve_entry *)(blob + off))->address = cpu_to_fdt64(1);
	((struct fdt_reserve_entry *)(blob + off))->size = cpu_to_fdt64(1);
	CHECK(fdt_check_header(blob) == 0); refusal(&p, EIO);
	/* Header remains valid, but the required final FDT_END is missing. */
	prepare(1, 0); size = fdt_size_dt_struct(blob);
	fdt_set_size_dt_struct(blob, size - 4);
	CHECK(fdt_check_header(blob) == 0); refusal(&p, EIO);
	/* A second top-level framebuffer cannot hide past the first END_NODE. */
	prepare(1, 0); size = fdt_size_dt_struct(blob);
	off = fdt_off_dt_struct(blob) + size - 4;
	*(fdt32_t *)(blob + off) = cpu_to_fdt32(FDT_BEGIN_NODE);
	memcpy(blob + off + 4, "framebuffer@1", 14);
	memset(blob + off + 18, 0, 2);
	*(fdt32_t *)(blob + off + 20) = cpu_to_fdt32(FDT_END_NODE);
	*(fdt32_t *)(blob + off + 24) = cpu_to_fdt32(FDT_END);
	fdt_set_size_dt_struct(blob, size + 24);
	fdt_set_off_dt_strings(blob, fdt_off_dt_struct(blob) + size + 24);
	CHECK(fdt_check_header(blob) == 0); refusal(&p, EIO);
	prepare(1, 0); chosen = fdt_add_subnode(blob, 0, "chosen");
	CHECK(chosen >= 0); child = fdt_add_subnode(blob, chosen, "nested");
	CHECK(child >= 0);
	CHECK(fdt_setprop_string(blob, child, "compatible", "simple-framebuffer") == 0);
	CHECK(fdt_setprop_string(blob, child, "status", "disabled") == 0);
	refusal(&p, EBUSY);
	prepare(1, 0); find_device = false; refusal(&p, ENODEV);
	prepare(1, 0); render = false; refusal(&p, ENODEV);
	prepare(1, 0); drm.registered = false; refusal(&p, ENODEV);
	prepare(1, 0); driver.name = "other"; refusal(&p, ENODEV);
	prepare(1, 0); right_node = false; refusal(&p, ENODEV);
	prepare(1, 0); fixture_node.ad_device = &fb_token; refusal(&p, ENODEV);
	for (i = 0; i < 3; i++) {
		prepare(i == 0 ? 0 : 1, i == 2 ? 240000000 : 0);
		CHECK(bcm2712_hsm_probe_modcmd(MODULE_CMD_INIT, &p) == 0);
		CHECK(bcm2712_hsm_owner == NULL && allocated == freed && references == 0);
		CHECK(fw.state == (i == 0 ? 0 : 1) && fw.rate >= 120000000);
		CHECK(i != 2 || fw.sets == 0);
		CHECK(strstr(log_text, "released (before destroy)") != NULL);
		CHECK(strstr(log_text, "finite cycle complete") != NULL);
	}
	/* Every native transaction can fail. There are no retries or blind restores. */
	for (i = 1; i <= 18; i++) {
		prepare(0, 0); fw.fail_call = i;
		CHECK(bcm2712_hsm_probe_modcmd(MODULE_CMD_INIT, &p) == ETIMEDOUT);
		CHECK(fw.calls == i && references == 0);
		CHECK(strstr(log_text, "finite cycle complete") == NULL);
		if (i <= 6) {
			CHECK(bcm2712_hsm_owner == NULL && allocated == freed && fw.sets == 0);
		} else {
			CHECK(bcm2712_hsm_owner != NULL);
			CHECK(bcm2712_hsm_owner->core.status.mutation_attempted);
			CHECK(i < 12 || i > 16 || bcm2712_hsm_owner->core.status.leases == 1);
			CHECK(strstr(log_text, "owner retained") != NULL);
			CHECK(bcm2712_hsm_probe_modcmd(MODULE_CMD_FINI, NULL) == 0);
			CHECK(fw.calls == i);
			/* Model a controlled reboot only after the retention assertions. */
			kmem_free(bcm2712_hsm_owner, sizeof(*bcm2712_hsm_owner));
			bcm2712_hsm_owner = NULL;
		}
	}
	prepare(0, 0); cleanup_error = true; fw.fail_call = 4;
	CHECK(bcm2712_hsm_probe_modcmd(MODULE_CMD_INIT, &p) == ETIMEDOUT);
	CHECK(bcm2712_hsm_owner != NULL && fw.sets == 0 && fw.calls == 4);
	CHECK(strstr(log_text, "owner retained") != NULL);
	in_irq = false;
	kmem_free(bcm2712_hsm_owner, sizeof(*bcm2712_hsm_owner));
	bcm2712_hsm_owner = NULL;
	CHECK(allocated == freed);
	dvp_probe_tests();
	hpd_probe_tests();
	ddc_probe_tests();
	edid_probe_tests();
	printf("HSM probe: %u checks PASS; real libfdt and native owner, host only\n", checks);
	return 0;
}
