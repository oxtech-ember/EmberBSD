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
struct properties { const char *expected; bool cold; const char *mode; bool wrong_mode_type; };
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
	assert(strcmp(n, "expected-buildinfo") == 0);
	*v = p->expected; return *v != NULL;
}
static void *prop_dictionary_get(prop_dictionary_t p, const char *n)
{ assert(strcmp(n, "dvp-mode") == 0); return p->mode || p->wrong_mode_type ? p : NULL; }
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
static void capture(const char *format, va_list ap)
{
	int length = vsnprintf(log_text + log_size, sizeof(log_text) - log_size,
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
#define BUS_SPACE_BARRIER_READ 1
#define BUS_SPACE_BARRIER_WRITE 2
static struct {
	uint32_t gates, resets, pending;
	unsigned int maps, unmaps, reads, writes, barriers, ignore_write;
	bool mapped, waiting, disturb_lease;
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
static int bus_space_map(bus_space_tag_t tag, uint64_t base, uint64_t size,
    int flags, bus_space_handle_t *handle)
{
	assert(tag == &arm_generic_bs_tag && base == UINT64_C(0x107c700000));
	assert(size == 0x10 && flags == 0 && !hw.mapped);
	held_hsm(); hw.maps++;
	if (step('M')) return EIO;
	hw.mapped = true; *handle = 17; return 0;
}
static uint32_t bus_space_read_4(bus_space_tag_t tag, bus_space_handle_t handle,
    unsigned int offset)
{
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
static void bus_space_unmap(bus_space_tag_t tag, bus_space_handle_t handle, uint64_t size)
{
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
{ assert(!hw.mapped && bcm2712_dvp_owner == NULL); PROVIDER_CALL('R', actual_hsm_release(p, port)); }
static int bcm2712_display_hsm_destroy(struct bcm2712_hsm_provider *p)
{ assert(!hw.mapped && bcm2712_dvp_owner == NULL); PROVIDER_CALL('D', actual_hsm_destroy(p)); }
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
{ assert(port == 0); held_hsm(); PROVIDER_CALL('a', actual_dvp_acquire(p, port)); }
static int bcm2712_display_dvp_release(struct bcm2712_dvp_provider *p, unsigned int port)
{ assert(port == 0); held_hsm(); PROVIDER_CALL('r', actual_dvp_release(p, port)); }
static int bcm2712_display_dvp_destroy(struct bcm2712_dvp_provider *p)
{ held_hsm(); PROVIDER_CALL('d', actual_dvp_destroy(p)); }
#undef PROVIDER_CALL
#include "probe.h"

static void
prepare(uint32_t state, uint32_t rate)
{
	reset(state, rate);
	assert(bcm2712_dvp_owner == NULL && !hw.mapped);
	memset(&hw, 0, sizeof(hw)); hw.gates = 0xa5a50018;
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
	CHECK(event_count == 0 && resource_calls == 0);
	CHECK(bcm2712_hsm_owner == NULL);
	CHECK(strstr(log_text, "finite cycle complete") == NULL);
}

/* Only the test's simulated reboot can discard retained native owners/maps. */
static void reboot_model(void)
{
	if (bcm2712_dvp_owner != NULL) {
		kmem_free(bcm2712_dvp_owner, sizeof(*bcm2712_dvp_owner));
		bcm2712_dvp_owner = NULL;
	}
	hw.mapped = hw.waiting = false;
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
	printf("HSM probe: %u checks PASS; real libfdt and native owner, host only\n", checks);
	return 0;
}
