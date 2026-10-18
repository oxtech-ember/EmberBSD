/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual probe, HSM core/provider, and libfdt. Kernel services are controlled. */
#define main existing_hsm_main
#include "hsm-services.c"
#undef main
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
struct properties { const char *expected; bool cold; };
typedef struct properties *prop_dictionary_t;
const char buildinfo[] = "EmberBSD:accepted-kernel\n";
static char blob[4096] __aligned(8);
static const void *tree;
static int fb_token;
static bool genfb, wsdisplay, find_device, right_driver, right_node, render;
static bool cleanup_error;
static uint64_t early_framebuffer;
static unsigned int references;
static char log_text[4096];
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
{ assert(strcmp(n, "expected-buildinfo") == 0); *v = p->expected; return *v != NULL; }
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
#include "probe.h"

static void
prepare(uint32_t state, uint32_t rate)
{
	reset(state, rate);
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
	CHECK(bcm2712_hsm_owner == NULL);
	CHECK(strstr(log_text, "finite cycle complete") == NULL);
}

int
main(void)
{
	struct properties p = { "EmberBSD:accepted-kernel", true };
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
	printf("HSM probe: %u checks PASS; real libfdt and native owner, host only\n", checks);
	return 0;
}
