/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual ARM/FDT/MI/finalizer chain. Only kernel/device/FDT services are doubled. */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <inttypes.h>
#ifndef __printflike
#define __printflike(fmtarg, firstvararg) \
	__attribute__((__format__(__printf__, fmtarg, firstvararg)))
#endif
struct cfattach { const char *ca_name; };

static int kernel_depth = 1;
#ifdef WSDISPLAY_BOOTFB_DEFER
static int cold = 1;
#endif
static void fixture_lock(int);
static void fixture_unlock(void);
static void fixture_before_map(bus_space_tag_t);
static bool fixture_bootfb_cpu_map_method(bus_space_tag_t);
static int wsdisplay_bootfb_test_phase(void);
int generic_bs_map(void *, bus_addr_t, bus_size_t, int, bus_space_handle_t *);
int armv7_generic_bs_map(void *, bus_addr_t, bus_size_t, int, bus_space_handle_t *);
static int nonposted_mmio_bs_map(void *, bus_addr_t, bus_size_t, int, bus_space_handle_t *);
#define KERNEL_LOCK(n, l) fixture_lock(n)
#define KERNEL_UNLOCK_ONE(l) fixture_unlock()
#define KERNEL_LOCKED_P() (kernel_depth > 0)
#define ASSERT_SLEEPABLE() CHECK(!hard_interrupt && !soft_interrupt)
#define KASSERTMSG(c, ...) do { if (!(c)) invalid_assertions++; } while (0)
static unsigned invalid_assertions;
struct device_compatible_entry { const char *compat; };
#define DEVICE_COMPAT_EOL { NULL }
static int OF_child(int);
static int OF_peer(int);
static bool fdtbus_status_okay(int);
static int of_compatible_match(int, const struct device_compatible_entry *);
#define BUS_SPACE_MAP_NONPOSTED 4
#define main earlyfb_original_main
#include "earlyfb-hooks-contract.c"
#undef main

static void fixture_lock(int n) { CHECK(n == 1); kernel_depth++; }
static void fixture_unlock(void) { CHECK(kernel_depth > 0); kernel_depth--; }
static int OF_child(int node) { CHECK(node == 2); return no_node ? 0 : 1; }
static int OF_peer(int node) { CHECK(node == 1); return 0; }
static bool fdtbus_status_okay(int node) { CHECK(node == 1); return true; }
static int of_compatible_match(int node, const struct device_compatible_entry *c)
{
	CHECK(node == 1 && strcmp(c->compat, "simple-framebuffer") == 0); return 1;
}
static bool genfb_mapping;
static unsigned genfb_attaches, genfb_initializations, pmf_calls;
static void fixture_before_map(bus_space_tag_t tag)
{
	CHECK(fixture_bootfb_cpu_map_method(tag) ||
	    tag->bs_map == nonposted_mmio_bs_map);
	if (genfb_mapping) CHECK(wsdisplay_bootfb_test_phase() == 4);
}

/* Mapping implementation is a controlled service; the method check is actual MD. */
int generic_bs_map(void *cookie, bus_addr_t a, bus_size_t s, int f,
    bus_space_handle_t *h)
{
	(void)cookie; return bus_space_map(&arm_generic_bs_tag, a, s, f, h);
}
int armv7_generic_bs_map(void *cookie, bus_addr_t a, bus_size_t s, int f,
    bus_space_handle_t *h)
{
	return generic_bs_map(cookie, a, s, f, h);
}
int unknown_map(void *c, bus_addr_t a, bus_size_t s, int f,
    bus_space_handle_t *h)
{
	(void)c; (void)a; (void)s; (void)f; (void)h; CHECK(false); return 1;
}
#include "md-functions.h"
#ifdef WSDISPLAY_BOOTFB_DEFER
static bool fixture_bootfb_cpu_map_method(bus_space_tag_t tag)
{
	return tag != NULL && tag->bs_map == BOOTFB_CPU_MAP;
}
#else
static bool fixture_bootfb_cpu_map_method(bus_space_tag_t tag)
{
	return tag != NULL && tag->bs_map == generic_bs_map;
}
#endif

struct properties {
	uint64_t override, address, virtual_address;
	uint32_t width, height, stride;
	uint8_t depth;
	bool swapped, tenbit, console;
};

struct device {
	void *private;
	const char *driver;
	struct cfattach attachment;
	struct properties properties;
#ifdef BOOTFB_CLAIM_JOB_CONTRACT
	struct device *dv_parent;
	int dv_pending;
#endif
	TAILQ_ENTRY(device) dv_pending_list;
};
static struct device display, legacy_owner;
#ifdef VIRTGPU_CONSOLE
static struct device foreign;
#endif
static bus_space_tag_t attachment_tag;
void *device_private(device_t d) { return d->private; }
bool device_is_a(device_t d, const char *s) { return strcmp(d->driver, s) == 0; }
struct cfattach *device_cfattach(device_t d) { return &d->attachment; }
static const char *device_xname(device_t d) { (void)d; return "fixture"; }
typedef struct properties *prop_dictionary_t;
static prop_dictionary_t device_properties(device_t d) { return &d->properties; }
static bool prop_dictionary_get_uint64(prop_dictionary_t p, const char *n, uint64_t *v)
{
	CHECK(strcmp(n, "simplefb-physaddr") == 0); *v = p->override; return *v != 0;
}
static void prop_dictionary_set_uint64(prop_dictionary_t p, const char *n, uint64_t v)
{
	if (strcmp(n, "address") == 0) p->address = v;
	else { CHECK(strcmp(n, "virtual_address") == 0); p->virtual_address = v; }
}
static void prop_dictionary_set_uint32(prop_dictionary_t p, const char *n, uint32_t v)
{
	if (strcmp(n, "width") == 0) p->width = v;
	else if (strcmp(n, "height") == 0) p->height = v;
	else { CHECK(strcmp(n, "linebytes") == 0); p->stride = v; }
}
static void prop_dictionary_set_uint8(prop_dictionary_t p, const char *n, uint8_t v)
{
	CHECK(strcmp(n, "depth") == 0); p->depth = v;
}
static void prop_dictionary_set_bool(prop_dictionary_t p, const char *n, bool v)
{
	if (strcmp(n, "is_swapped") == 0) p->swapped = v;
	else if (strcmp(n, "is_10bit") == 0) p->tenbit = v;
	else { CHECK(strcmp(n, "is_console") == 0); p->console = v; }
}
struct genfb_softc { device_t sc_dev; int sc_width; size_t sc_fbsize; };
struct genfb_ops { int (*genfb_ioctl)(void); int (*genfb_mmap)(void); };
struct fdt_attach_args { int faa_phandle; bus_space_tag_t faa_bst; };
static void genfb_init(struct genfb_softc *sc)
{
	CHECK(wsdisplay_bootfb_test_phase() == 4); genfb_initializations++;
	sc->sc_width = (int)sc->sc_dev->properties.width;
	sc->sc_fbsize = (size_t)sc->sc_dev->properties.height * sc->sc_dev->properties.stride;
}
static void genfb_attach(struct genfb_softc *sc, const struct genfb_ops *ops)
{
	CHECK(sc != NULL && ops != NULL && wsdisplay_bootfb_test_phase() == 4);
	genfb_attaches++; wsdisplay_console_initted = 2;
}
void genfb_cnattach(void) { }
static void pmf_device_register1(device_t d, const void *s, const void *r,
    bool (*f)(device_t, int))
{
	CHECK(d != NULL && s == NULL && r == NULL && f != NULL);
	CHECK(wsdisplay_bootfb_test_phase() == 4); pmf_calls++;
}
static bool simplefb_shutdown(device_t d, int flags) { (void)d; (void)flags; return true; }
static int simplefb_ioctl(void) { return 0; }
static int simplefb_mmap(void) { return 0; }
#define PRIxBUSADDR PRIx64
static void print_message(const char *f, ...) { (void)f; }
static void print_device_message(device_t d, const char *f, ...) { (void)d; (void)f; }
#define aprint_error print_message
#define aprint_normal print_message
#define aprint_naive print_message
#define aprint_normal_dev print_device_message

/* Actual configuration functions, including immediate callback and hook disposal. */
static int config_misc_lock, config_misc_cv, hz = 100, boothowto, config_do_twiddle;
static TAILQ_HEAD(, device) config_pending = TAILQ_HEAD_INITIALIZER(config_pending);
static void mutex_enter(int *p) { CHECK(p == &config_misc_lock); }
static void mutex_exit(int *p) { CHECK(p == &config_misc_lock); }
static unsigned getticks(void) { return 0; }
static unsigned mstohz(unsigned n) { return n; }
static int cv_timedwait(int *c, int *m, unsigned n)
{
	(void)c; (void)m; (void)n;
#ifdef BOOTFB_CLAIM_JOB_CONTRACT
	return fixture_config_wait();
#else
	CHECK(false); return 0;
#endif
}
static unsigned hook_allocations, hook_frees;
#define KM_SLEEP 1
static void *kmem_alloc(size_t n, int f)
{
	void *p = malloc(n); CHECK(p != NULL && f == KM_SLEEP); hook_allocations++; return p;
}
static void kmem_free(void *p, size_t n) { CHECK(n > 0); hook_frees++; free(p); }
static int aprint_get_error_count(void) { return 0; }
#define printf_nolog printf
#define aprint_debug print_message
#define AB_QUIET 1
#define AB_SILENT 2
#define AB_VERBOSE 4
struct pdevinit { void (*pdev_attach)(int); int pdev_count; };
/* This private service table remains an immutable sentinel in this fixture. */
static struct pdevinit pdevinit[] = {{ NULL, 0 }};
#include "finalizer-functions.h"
#include "bootfb-core.c"
static int wsdisplay_bootfb_test_phase(void) { return wsdisplay_bootfb.phase; }
#include "simplefb-state.h"
static int simplefb_console_phandle = 1;
static int simplefb_attach_genfb(struct simplefb_softc *);
#include "simplefb-functions.h"
static struct simplefb_softc simplefb_sc;

static void fresh(void)
{
	CHECK(TAILQ_EMPTY(&config_finalize_list));
	reset();
	memset(&wsdisplay_bootfb, 0, sizeof(wsdisplay_bootfb));
#ifdef WSDISPLAY_BOOTFB_DEFER
	memset(&wsdisplay_bootfb_lease, 0, sizeof(wsdisplay_bootfb_lease));
	memset(&arm_simplefb_boot, 0, sizeof(arm_simplefb_boot));
	arm_simplefb_captured = 0;
#endif
	memset(&simplefb_sc, 0, sizeof(simplefb_sc));
	memset(&display, 0, sizeof(display)); display.private = &simplefb_sc;
	display.driver = "genfb"; display.attachment.ca_name = "simplefb";
#ifdef WSDISPLAY_BOOTFB_DEFER
	arm_generic_bs_tag.bs_map = BOOTFB_CPU_MAP;
#else
	arm_generic_bs_tag.bs_map = generic_bs_map;
#endif
	attachment_tag = &arm_generic_bs_tag;
	config_finalize_done = 0; kernel_depth = 1;
#ifdef WSDISPLAY_BOOTFB_DEFER
	cold = 1;
#endif
	genfb_attaches = genfb_initializations = pmf_calls = 0;
	hook_allocations = hook_frees = invalid_assertions = 0;
	genfb_mapping = false;
}
static void attach_selected(int node)
{
	struct fdt_attach_args faa = { node, attachment_tag };
	genfb_mapping = true; simplefb_attach(NULL, &display, &faa); genfb_mapping = false;
}
static void finish(void)
{
	CHECK(pdevinit[0].pdev_attach == NULL);
	genfb_mapping = true; config_finalize(); genfb_mapping = false;
	CHECK(hook_allocations == hook_frees && kernel_depth == 1);
}
static void parser_reject(void)
{
	struct simplefb_framebuffer out, saved;
	memset(&out, 0xa5, sizeof(out)); memcpy(&saved, &out, sizeof(saved));
	CHECK(simplefb_read_node(1, &arm_generic_bs_tag, &out) != 0);
	CHECK(memcmp(&out, &saved, sizeof(out)) == 0 && maps == 0);
}
static void parser_tests(void)
{
	struct simplefb_framebuffer out;
	const uint64_t invalid[][2] = {
		{ UINT64_C(0xfffffffe00000000), UINT64_C(0x200001000) },
		{ UINT64_MAX, 1 }, { UINT64_MAX - 4095, 4095 }, {0, 0}
	};
	const char *formats[] = { "a8b8g8r8", "x8r8g8b8", "r8g8b8a8", "b8g8r8x8", "x2r10g10b10", "r5g6b5" };
	for (unsigned i = 0; i < 6; i++) {
		fresh(); fixture_format = formats[i];
		CHECK(simplefb_read_node(1, &arm_generic_bs_tag, &out) == 0);
		CHECK((unsigned)out.sf_format == i && out.sf_depth == (i == 5 ? 16 : 32));
		attach_selected(1); finish();
		CHECK(genfb_attaches == 1 && display.properties.swapped == (i == 2 || i == 3));
		CHECK(display.properties.tenbit == (i == 4));
	}
	for (unsigned i = 0; i < 8; i++) {
		fresh();
		switch (i) {
		case 0: fixture_width = 0; break;
		case 1: fixture_height = 0; break;
		case 2: fixture_stride = 0; break;
		case 3: fixture_stride = 1; break;
		case 4: fixture_width = UINT32_MAX; break;
		case 5: fixture_height = UINT32_MAX; break;
		case 6: fixture_format = "unknown"; break;
		case 7: fixture_size = 1; break;
		}
		parser_reject();
	}
	for (int a = 1; a <= 2; a++) for (int n = 1; n <= 2; n++) {
		fresh(); fixture_address_cells = a; fixture_size_cells = n; fixture_reg_length = (a + n) * 4;
		CHECK(simplefb_read_node(1, &arm_generic_bs_tag, &out) == 0);
	}
	fresh(); fixture_address = 0; fixture_handle = 0;
	CHECK(simplefb_read_node(1, &arm_generic_bs_tag, &out) == 0 && out.sf_address == 0);
	CHECK(simplefb_read_node(1, &arm_generic_bs_tag, NULL) == EINVAL);
	for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
		fresh(); fixture_address = invalid[i][0]; fixture_size = invalid[i][1]; parser_reject();
	}
	for (unsigned i = 0; i < 4; i++) {
		fresh(); fixture_address_cells = i == 3 ? INT_MAX : (int)i;
		if (i == 1 || i == 2) fixture_size_cells = 3;
		parser_reject(); CHECK(fixture_get_reg_calls == 0);
	}
	for (int n = 0; n <= 32; n++) {
		if (n == 16) continue;
		fresh(); fixture_reg_length = n; parser_reject(); CHECK(fixture_get_reg_calls == 0);
	}
#ifdef BOOTFB_BUS32
	fresh(); fixture_address = UINT64_C(0x100000000); parser_reject();
	fresh(); fixture_address = UINT32_MAX - 4095; fixture_size = 4096; parser_reject();
#else
	fresh(); fixture_address = UINT64_C(0x100000000);
	CHECK(simplefb_read_node(1, &arm_generic_bs_tag, &out) == 0);
#endif
}
#ifdef WSDISPLAY_BOOTFB_DEFER
static void early(void)
{
	arm_simplefb_preattach(); CHECK(arm_simplefb_captured == 1);
	CHECK(arm_simplefb_boot.map_present && maps == 1 && updates == 1);
}
static void native_tests(void)
{
	struct wsdisplay_bootfb_lease *lease, *saved_lease;
	struct simplefb_framebuffer fb, saved;
	struct bus_space copy;
	unsigned output;
	fresh(); fixture_address = 0; fixture_handle = 0; early();
	copy = arm_generic_bs_tag; attachment_tag = &copy; attach_selected(1);
	CHECK(wsdisplay_bootfb.phase == BOOTFB_DEFERRED && genfb_attaches == 0);
	cold = 0; CHECK(simplefb_console_claim(&lease, &fb) == 0 && fb.sf_address == 0);
	CHECK(wsdisplay_bootfb.phase == BOOTFB_CLAIMED);
	CHECK(!simplefb_console_reserve(&legacy_owner));
	CHECK(simplefb_console_get_framebuffer((device_t)lease, &fb) == EPERM);
	output = output_calls; wsdisplay_cnputc(0, 'l'); CHECK(output_calls == output + 1);
	CHECK(wsdisplay_bootfb_finalize(&display) == 0);
	finish(); CHECK(wsdisplay_bootfb.pending && wsdisplay_bootfb.phase == BOOTFB_CLAIMED);
	output = output_calls; wsdisplay_cnputc(0, 'p'); CHECK(output_calls == output + 1);
	genfb_mapping = true; CHECK(simplefb_console_cancel(lease) == 0); genfb_mapping = false;
	CHECK(genfb_attaches == 1 && pmf_calls == 1 && wsdisplay_bootfb.phase == BOOTFB_FALLBACK_DONE);
	CHECK(simplefb_console_cancel(lease) == EPERM); finish(); CHECK(genfb_attaches == 1);

	fresh(); early(); attach_selected(1); CHECK(simplefb_console_claim(&lease, &fb) == 0);
	CHECK(simplefb_console_cancel(lease) == 0);
	CHECK(simplefb_console_claim(&saved_lease, &fb) != 0); finish(); CHECK(genfb_attaches == 1);

	fresh(); early(); attach_selected(1); finish();
	memset(&fb, 0x5a, sizeof(fb)); memcpy(&saved, &fb, sizeof(fb)); lease = (void *)1; saved_lease = lease;
	CHECK(simplefb_console_claim(&lease, &fb) != 0 && lease == saved_lease);
	CHECK(memcmp(&fb, &saved, sizeof(fb)) == 0 && genfb_attaches == 1);

	fresh(); early(); attach_selected(1); unsigned allocations = hook_allocations;
	attach_selected(1); CHECK(hook_allocations == allocations && genfb_attaches == 0);
	CHECK(simplefb_console_reserve(&legacy_owner));
	CHECK(simplefb_console_claim(&lease, &fb) == EBUSY);
	CHECK(simplefb_console_release(&legacy_owner));
	CHECK(simplefb_console_claim(&lease, &fb) == 0);
	CHECK(simplefb_console_cancel((void *)&legacy_owner) == EPERM);
	CHECK(simplefb_console_cancel(lease) == 0); finish();

	for (unsigned k = 0; k < 4; k++) {
		fresh(); early(); attach_selected(1);
		if (k == 0) wsdisplay_console_initted = 2;
		if (k == 1) wsdisplay_console_conf.emulcookie = &display;
		if (k == 2) wsdisplay_console_device = (void *)&display;
		if (k == 3) cn_tab = &old_cons;
		memset(&fb, 0xa5, sizeof(fb)); memcpy(&saved, &fb, sizeof(fb)); lease = (void *)1;
		CHECK(simplefb_console_claim(&lease, &fb) != 0 && lease == (void *)1);
		CHECK(memcmp(&fb, &saved, sizeof(fb)) == 0 && wsdisplay_bootfb.phase == BOOTFB_RETAINED);
		finish(); CHECK(genfb_attaches == 0);
	}
	fresh(); early(); cold = 0; attach_selected(1);
	CHECK(!wsdisplay_bootfb.native && simplefb_console_claim(&lease, &fb) != 0); finish();
	fresh(); early(); copy = arm_generic_bs_tag; copy.bs_map = nonposted_mmio_bs_map;
	arm_generic_bs_tag.bs_map = unknown_map;
	CHECK(!fdtbus_bootfb_cpu_map(&copy));
	fresh(); early(); struct simplefb_framebuffer different = arm_simplefb_boot.framebuffer;
	CHECK(fdtbus_bootfb_defer(&display, 4, &arm_generic_bs_tag, &different,
	    simplefb_console_fallback) != 0 && wsdisplay_bootfb.phase == BOOTFB_NONE);
	fresh(); early(); config_finalize_done = 1; attach_selected(1);
	CHECK(genfb_attaches == 1 && wsdisplay_bootfb.phase == BOOTFB_FALLBACK_DONE);
	CHECK(simplefb_console_claim(&lease, &fb) != 0); finish(); CHECK(genfb_attaches == 1);

	fresh(); early(); CHECK(config_finalize_register(&display, wsdisplay_bootfb_finalize) == 0);
	attach_selected(1); CHECK(genfb_attaches == 1 && wsdisplay_bootfb.phase == BOOTFB_FALLBACK_DONE);
	CHECK(simplefb_console_claim(&lease, &fb) != 0); finish(); CHECK(genfb_attaches == 1);

	for (unsigned kind = 0; kind < 17; kind++) {
		fresh(); early();
		switch (kind) {
		case 0: fixture_blob = &fixture_blob_b; break;
		case 1: fixture_stdout = 4; break;
		case 2: fixture_size += 4096; break;
		case 3: fixture_stride += 4; break;
		case 4: fixture_format = "a8b8g8r8"; break;
		case 5: fixture_width--; break;
		case 6: fixture_height--; break;
		case 7: display.properties.override = 0x4000; break;
		case 8: attachment_tag = &copy; copy = arm_generic_bs_tag; copy.bs_map = unknown_map; break;
		case 9: copy = arm_generic_bs_tag; attachment_tag = &copy; arm_generic_bs_tag.bs_map = unknown_map; break;
		case 10: display.driver = "other"; break;
		case 11: display.attachment.ca_name = "other"; break;
		case 12: fixture_node_parent = 3; break;
		case 13: fixture_ranges_length = -1; break;
		case 14: fixture_chosen_parent = 4; break;
		case 15: arm_simplefb_boot.map_method = unknown_map; break;
		case 16: arm_simplefb_boot.map_present = false; break;
		}
		/* Call the actual bridge before ordinary fallback can have effects. */
		struct simplefb_framebuffer checked;
		simplefb_sc.sc_gen.sc_dev = &display;
		simplefb_sc.sc_phandle = 1; simplefb_sc.sc_bst = attachment_tag;
		CHECK(simplefb_read_framebuffer(&simplefb_sc, &checked) == 0);
		CHECK(fdtbus_bootfb_match(&display, 1, attachment_tag, &checked) != 0);
		CHECK(wsdisplay_bootfb.phase == BOOTFB_NONE);
	}
	fresh(); arm_generic_bs_tag.bs_map = unknown_map; arm_simplefb_preattach();
	CHECK(maps == 0 && arm_simplefb_captured == 0);
	fresh(); fixture_ranges_length = 4; attach_selected(1);
	CHECK(fixture_get_reg_calls == 0 && maps == 0 && genfb_attaches == 0); finish();
	fresh(); fail_map = true; arm_simplefb_preattach(); CHECK(arm_simplefb_captured == 0);
	fresh(); wsdisplay_console_initted = 2; arm_simplefb_preattach();
	CHECK(maps == 1 && updates == 0 && arm_simplefb_captured == 0);
	fresh(); change_binding = true; arm_simplefb_preattach(); CHECK(arm_simplefb_captured == 0);
	fresh(); panic_at_update = true; arm_simplefb_preattach(); CHECK(arm_simplefb_captured == 0);
	for (unsigned k = 0; k < 2; k++) {
		fresh(); early(); attach_selected(1); CHECK(simplefb_console_claim(&lease, &fb) == 0); finish();
		if (k == 0) fixture_blob = &fixture_blob_b; else wsdisplay_console_initted = 2;
		CHECK(simplefb_console_cancel(lease) != 0 && wsdisplay_bootfb.phase == BOOTFB_RETAINED);
		CHECK(genfb_attaches == 0); finish(); CHECK(genfb_attaches == 0);
	}
	fresh(); early(); copy = arm_generic_bs_tag; copy.bs_map = nonposted_mmio_bs_map;
	attachment_tag = &copy; attach_selected(1); CHECK(simplefb_console_claim(&lease, &fb) == 0);
	CHECK(simplefb_console_cancel(lease) == 0); finish(); CHECK(genfb_attaches == 1);
}
#endif
static void legacy_tests(void)
{
	struct simplefb_framebuffer fb, saved;
	fresh(); attach_selected(1);
#ifdef VIRTGPU_CONSOLE
	CHECK(genfb_attaches == 0 && wsdisplay_bootfb.phase == BOOTFB_DEFERRED);
	CHECK(!simplefb_console_reserve(NULL)); CHECK(simplefb_console_reserve(&legacy_owner));
	CHECK(!simplefb_console_reserve(&foreign));
	memset(&fb, 0xa5, sizeof(fb)); memcpy(&saved, &fb, sizeof(fb));
	CHECK(simplefb_console_get_framebuffer(&foreign, &fb) == EPERM);
	CHECK(memcmp(&fb, &saved, sizeof(fb)) == 0);
	CHECK(!simplefb_console_release(&foreign)); simplefb_console_commit(&foreign);
	CHECK(invalid_assertions == 1 && !wsdisplay_bootfb.committed);
	CHECK(simplefb_console_release(&legacy_owner)); CHECK(simplefb_console_reserve(&foreign));
	simplefb_console_commit(&foreign); CHECK(!simplefb_console_release(&foreign));
	finish(); CHECK(genfb_attaches == 0);
	fresh(); fixture_width = 0; attach_selected(1);
	CHECK(simplefb_console_reserve(&legacy_owner));
	CHECK(simplefb_console_get_framebuffer(&legacy_owner, &fb) == EINVAL);
	simplefb_console_commit(&legacy_owner); finish(); CHECK(genfb_attaches == 0);
#else
	(void)fb; (void)saved; CHECK(genfb_attaches == 1);
	CHECK(!simplefb_console_reserve(&legacy_owner)); finish();
#endif
	fresh(); attach_selected(1); finish();
	CHECK(genfb_attaches == 1 && wsdisplay_bootfb.phase == BOOTFB_FALLBACK_DONE);
	CHECK(!simplefb_console_reserve(&legacy_owner));
}
int main(void)
{
	parser_tests(); legacy_tests();
#ifdef WSDISPLAY_BOOTFB_DEFER
	native_tests();
#endif
	CHECK(kernel_depth == 1 && hook_allocations == hook_frees);
	printf("boot framebuffer issuer: %u checks PASS\n", atomic_load(&checks));
	return 0;
}
