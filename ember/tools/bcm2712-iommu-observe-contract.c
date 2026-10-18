/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual observer, clock reader and libfdt; controlled kernel service boundary. */
#if defined(__linux__) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE 1
#endif
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <libfdt.h>
#if defined(__APPLE__)
#include <libkern/OSByteOrder.h>
#define htole32 OSSwapHostToLittleInt32
#define le32toh OSSwapLittleToHostInt32
#elif defined(__linux__)
#include <endian.h>
#else
#include <sys/endian.h>
#endif
#ifndef __aligned
#define __aligned(n) __attribute__((__aligned__(n)))
#endif
#ifndef __arraycount
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#endif
#ifndef EPROGMISMATCH
#define EPROGMISMATCH ENOEXEC
#endif
#include "display.h"
#include "properties.h"
#include "display-core.h"

#define MODULE(a,b,c)
#define MODULE_CLASS_MISC 0
#define MODULE_CMD_INIT 1
#define MODULE_CMD_FINI 2
#define DEVITER_F_ROOT_FIRST 0
#define DRM_MINOR_RENDER 2
#define BCMMBOX_CHANARM2VC 8
#define ACPI_FAILURE(e) ((e) != 0)
#define IS_ERR(p) ((intptr_t)(p) < 0)
typedef int modcmd_t;
typedef void *device_t;
typedef void *ACPI_HANDLE;
typedef uint64_t bus_addr_t;
typedef uint64_t bus_size_t;
typedef uintptr_t bus_space_handle_t;
struct bus_space { int unused; } arm_generic_bs_tag;
typedef struct { unsigned int next; } deviter_t;
struct properties {
	const char *expected, *mode, *conflict;
	bool cold, bad_bool, bad_string;
};
typedef struct properties *prop_dictionary_t;
struct drm_driver { const char *name; };
struct drm_minor;
struct drm_device {
	device_t dev;
	struct drm_driver *driver;
	bool registered;
	struct drm_minor *render;
};
struct drm_minor { int type; struct drm_device *dev; };
struct acpi_devnode { device_t ad_device; };
const char buildinfo[] = "EmberBSD:accepted-observer-kernel\n";
static int device_token, fb_token;
static struct drm_driver driver;
static struct drm_device drm;
static struct drm_minor minor;
static struct acpi_devnode node;
static char blob[4096] __aligned(8), log_text[32768];
static const void *tree;
static size_t log_size;
static unsigned int references, acquisitions, releases, resources, maps, unmaps;
static unsigned int reads, exchanges, checks;
static bool mapped, find_device, right_driver, render;
static bool genfb, wsdisplay, get_handle_error, missing_node;
static uint64_t early_fb;
static int resource_error, resource_bad, map_error, transport_at, wire_at;
static uint32_t clock_state[2][4], clock_rate[2][4], words[2][7];
static struct properties properties;
static const unsigned int ids[] = { 13, 14, 4, 16 };
static const uint64_t offsets[] = { 0x38, 0, 4, 0x14, 0x1c, 0x20, 0x24 };
#define CHECK(e) do { checks++; if (!(e)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #e); exit(1); } } while (0)

static bool
prop_dictionary_get_bool(prop_dictionary_t p, const char *name, bool *out)
{
	CHECK(strcmp(name, "cold-headless") == 0);
	if (p->bad_bool) return false;
	*out = p->cold;
	return true;
}
static bool
prop_dictionary_get_cstring_nocopy(prop_dictionary_t p, const char *name,
    const char **out)
{
	if (strcmp(name, "expected-buildinfo") == 0) {
		*out = p->expected;
		return *out != NULL;
	}
	CHECK(strcmp(name, "iommu-mode") == 0);
	if (p->bad_string) return false;
	*out = p->mode;
	return *out != NULL;
}
static void *
prop_dictionary_get(prop_dictionary_t p, const char *name)
{
	return p->conflict != NULL && strcmp(name, p->conflict) == 0 ? p : NULL;
}
static device_t
device_find_by_xname(const char *name)
{
	CHECK(strcmp(name, "bcmv3d0") == 0);
	return find_device ? &device_token : NULL;
}
static bool
device_is_a(device_t dev, const char *name)
{
	return (dev == &device_token && strcmp(name, "bcmv3d") == 0 && right_driver) ||
	    (dev == &fb_token && ((genfb && strcmp(name, "genfb") == 0) ||
	    (wsdisplay && strcmp(name, "wsdisplay") == 0)));
}
static struct drm_minor *
drm_minor_acquire(unsigned int id)
{
	CHECK(id == 128 && references == 0);
	acquisitions++;
	if (!render) return (void *)(intptr_t)-ENODEV;
	references++;
	return &minor;
}
static void
drm_minor_release(struct drm_minor *value)
{
	CHECK(value == &minor && references == 1 && !mapped);
	references--;
	releases++;
}
static int
AcpiGetHandle(ACPI_HANDLE parent, const char *path, ACPI_HANDLE *out)
{
	CHECK(parent == NULL && strcmp(path, "\\_SB.GPU0") == 0);
	*out = &device_token;
	return get_handle_error;
}
static const struct acpi_devnode *
acpi_match_node(ACPI_HANDLE handle)
{
	CHECK(handle == &device_token);
	return missing_node ? NULL : &node;
}
static void deviter_init(deviter_t *i, int flags)
{ CHECK(flags == DEVITER_F_ROOT_FIRST); i->next = 0; }
static device_t deviter_next(deviter_t *i)
{ return i->next++ == 0 && (genfb || wsdisplay) ? &fb_token : NULL; }
static void deviter_release(deviter_t *i) { (void)i; }
static uint64_t arm_simplefb_physaddr(void) { return early_fb; }
static const void *fdtbus_get_data(void) { return tree; }

static int
bcm2712_display_acpi_resources(device_t dev, ACPI_HANDLE handle,
    struct bcm2712_display_contract *out)
{
	unsigned int i;

	CHECK(dev == &device_token && handle == dev && references == 1);
	CHECK(exchanges == 0 && maps == 0);
	resources++;
	if (resource_error) return resource_error;
	memset(out, 0, sizeof(*out));
	out->version = 1;
	for (i = 0; i < 32; i++) out->memory[i] = bcm2712_display_resources[i].memory;
	if (resource_bad == 1) out->memory[4].address += 4;
	if (resource_bad == 2) out->memory[4].length += 4;
	return 0;
}
static int
bcmmbox_request(unsigned int channel, void *buffer, size_t length, uint32_t *reply)
{
	uint32_t *request = buffer, id, tag, value;
	unsigned int position = exchanges++, pass = position / 8;
	unsigned int index = (position % 8) / 2;

	CHECK(channel == 8 && length == 32 && ((uintptr_t)buffer & 15) == 0);
	CHECK(references == 1 && !mapped && position < 16);
	CHECK(pass == 0 ? maps == 0 : (unmaps == 1 && reads == 14));
	id = le32toh(request[5]); tag = le32toh(request[2]);
	CHECK(id == ids[index] && tag == (position % 2 ? 0x30002U : 0x30001U));
	CHECK(le32toh(request[0]) == 32 && request[1] == 0 &&
	    le32toh(request[3]) == 8 && request[4] == 0 &&
	    request[6] == 0 && request[7] == 0);
	if ((int)position == transport_at) return ETIMEDOUT;
	value = position % 2 ? clock_rate[pass][index] : clock_state[pass][index];
	request[1] = htole32(0x80000000U);
	request[4] = htole32(0x80000008U);
	request[6] = htole32(value);
	if ((int)position == wire_at) request[5] = htole32(UINT32_MAX);
	*reply = 0;
	return 0;
}
static int
bus_space_map(struct bus_space *tag, bus_addr_t address, bus_size_t length,
    int flags, bus_space_handle_t *out)
{
	CHECK(tag == &arm_generic_bs_tag && address == UINT64_C(0x1000005200));
	CHECK(length == 0x80 && flags == 0 && references == 1);
	CHECK(exchanges == 8 && resources == 1 && maps == 0 && !mapped);
	maps++;
	if (map_error) return map_error;
	*out = 0x4150;
	mapped = true;
	return 0;
}
static uint32_t
bus_space_read_4(struct bus_space *tag, bus_space_handle_t handle, bus_size_t offset)
{
	unsigned int position = reads++;

	CHECK(tag == &arm_generic_bs_tag && handle == 0x4150 && mapped);
	CHECK(references == 1 && exchanges == 8 && position < 14);
	CHECK(offset == offsets[position % 7] && offset + 4 <= 0x80);
	return words[position / 7][position % 7];
}
static void
bus_space_unmap(struct bus_space *tag, bus_space_handle_t handle, bus_size_t length)
{
	CHECK(tag == &arm_generic_bs_tag && handle == 0x4150 && length == 0x80);
	CHECK(references == 1 && mapped && unmaps == 0 && exchanges == 8);
	mapped = false;
	unmaps++;
}
static void
capture(const char *format, va_list ap)
{
	int length;

	if (strstr(format, "first resource4 MMIO") == NULL)
		CHECK(references == 0 && !mapped);
	length = vsnprintf(log_text + log_size, sizeof(log_text) - log_size, format, ap);
	CHECK(length >= 0 && (size_t)length < sizeof(log_text) - log_size);
	log_size += (size_t)length;
}
static void aprint_normal(const char *format, ...)
{ va_list ap; va_start(ap, format); capture(format, ap); va_end(ap); }
static void aprint_error(const char *format, ...)
{ va_list ap; va_start(ap, format); capture(format, ap); va_end(ap); }
#include "observer.h"

static void
reset(void)
{
	unsigned int pass, i;

	CHECK(references == 0 && !mapped);
	references = acquisitions = releases = resources = maps = unmaps = reads = exchanges = 0;
	resource_error = resource_bad = map_error = 0;
	transport_at = wire_at = -1;
	find_device = right_driver = render = true;
	genfb = wsdisplay = get_handle_error = missing_node = false;
	early_fb = 0;
	driver.name = "v3d";
	drm = (struct drm_device){ &device_token, &driver, true, &minor };
	minor = (struct drm_minor){ DRM_MINOR_RENDER, &drm };
	node.ad_device = &device_token;
	properties = (struct properties){ "EmberBSD:accepted-observer-kernel", "observe4", NULL, true, false, false };
	CHECK(fdt_create_empty_tree(blob, sizeof(blob)) == 0);
	tree = blob;
	log_size = 0; log_text[0] = 0;
	for (pass = 0; pass < 2; pass++) {
		for (i = 0; i < 4; i++) {
			clock_state[pass][i] = 1;
			clock_rate[pass][i] = i == 0 ? 120000000 : 324000000;
		}
		/* Independent literal vector: 40GiB bypass, 64GiB exclusive cap. */
		memcpy(words[pass], (uint32_t[]){ 0x20000664, 1, 0x12345678,
		    0x800000ff, 0, 0x800000a0, 0 }, sizeof(words[pass]));
	}
}
static int
run(void)
{
	int error = bcm2712_iommu_observe_modcmd(MODULE_CMD_INIT, &properties);

	CHECK(references == 0 && !mapped);
	CHECK(bcm2712_iommu_observe_modcmd(MODULE_CMD_FINI, NULL) == 0);
	CHECK(bcm2712_iommu_observe_modcmd(99, NULL) == ENOTTY);
	return error;
}
static void
zero_effects(void)
{
	CHECK(resources == 0 && maps == 0 && reads == 0 && exchanges == 0);
}
static void
completed(bool candidate)
{
	CHECK(run() == 0 && reads == 14 && exchanges == 16);
	CHECK(maps == 1 && unmaps == 1 && releases == 1);
	CHECK(strstr(log_text, candidate ? "candidate 1" : "candidate 0") != NULL);
	CHECK(strstr(log_text, "no DMA authorization or lifetime proof") != NULL);
}
static void
early_tests(void)
{
	static const char * const conflicts[] = { "clock-mode", "dvp-mode",
	    "hd-mode", "hpd-mode", "ddc-mode", "phy-mode" };
	unsigned int i;
	int offset;

	reset(); CHECK(bcm2712_iommu_observe_modcmd(MODULE_CMD_INIT, NULL) == EINVAL); zero_effects();
	reset(); properties.cold = false; CHECK(run() == EINVAL); zero_effects();
	reset(); properties.bad_bool = true; CHECK(run() == EINVAL); zero_effects();
	reset(); properties.expected = NULL; CHECK(run() == EINVAL); zero_effects();
	reset(); properties.expected = "wrong"; CHECK(run() == EPROGMISMATCH); zero_effects();
	reset(); properties.mode = NULL; CHECK(run() == EINVAL); zero_effects();
	reset(); properties.mode = "other"; CHECK(run() == EINVAL); zero_effects();
	reset(); properties.bad_string = true; CHECK(run() == EINVAL); zero_effects();
	for (i = 0; i < __arraycount(conflicts); i++) {
		reset(); properties.conflict = conflicts[i]; CHECK(run() == EINVAL); zero_effects();
	}
	reset(); early_fb = 0x1000; CHECK(run() == EBUSY); zero_effects();
	reset(); genfb = true; CHECK(run() == EBUSY); zero_effects();
	reset(); wsdisplay = true; CHECK(run() == EBUSY); zero_effects();
	reset(); tree = NULL; CHECK(run() == EIO); zero_effects();
	reset(); blob[0] = 0; CHECK(run() == EIO); zero_effects();
	reset(); offset = fdt_add_subnode(blob, 0, "framebuffer@0"); CHECK(offset >= 0);
	CHECK(run() == EBUSY); zero_effects();
	reset(); offset = fdt_add_subnode(blob, 0, "fb"); CHECK(offset >= 0);
	CHECK(fdt_setprop_string(blob, offset, "compatible", "simple-framebuffer") == 0);
	CHECK(run() == EBUSY); zero_effects();
	reset(); find_device = false; CHECK(run() == ENODEV); zero_effects();
	reset(); right_driver = false; CHECK(run() == ENODEV); zero_effects();
	reset(); render = false; CHECK(run() == ENODEV); zero_effects(); CHECK(releases == 0);
	reset(); minor.type = 1; CHECK(run() == ENODEV); zero_effects(); CHECK(releases == 1);
	reset(); drm.registered = false; CHECK(run() == ENODEV); zero_effects();
	reset(); drm.dev = NULL; CHECK(run() == ENODEV); zero_effects();
	reset(); drm.render = NULL; CHECK(run() == ENODEV); zero_effects();
	reset(); driver.name = "other"; CHECK(run() == ENODEV); zero_effects();
	reset(); get_handle_error = true; CHECK(run() == ENODEV); zero_effects();
	reset(); missing_node = true; CHECK(run() == ENODEV); zero_effects();
	reset(); node.ad_device = NULL; CHECK(run() == ENODEV); zero_effects();
}
static void
clock_tests(void)
{
	unsigned int position, pass, index, kind;
	char expected[64];

	for (position = 0; position < 16; position++) {
		reset(); transport_at = (int)position;
		CHECK(run() == ETIMEDOUT && exchanges == position + 1);
		CHECK(reads == (position < 8 ? 0U : 14U));
		CHECK(maps == (position < 8 ? 0U : 1U) && unmaps == maps);
		CHECK(strstr(log_text, "snapshot complete") == NULL);
		CHECK(strstr(log_text, "validated clock failure") == NULL);
		reset(); wire_at = (int)position;
		CHECK(run() == EIO && exchanges == position + 1);
		CHECK(strstr(log_text, "validated clock failure") == NULL);
	}
	for (pass = 0; pass < 2; pass++) {
		for (index = 0; index < 4; index++) {
			for (kind = 0; kind < 2; kind++) {
				reset();
				if (kind == 0) clock_state[pass][index] = 0;
				else clock_rate[pass][index] = 0;
				CHECK(run() == EBUSY && exchanges == pass * 8 + index * 2 + 2);
				CHECK(reads == (pass == 0 ? 0U : 14U));
				CHECK(unmaps == maps && releases == 1);
				(void)snprintf(expected, sizeof(expected), "id %u state", ids[index]);
				CHECK(strstr(log_text, "validated clock failure") != NULL);
				CHECK(strstr(log_text, expected) != NULL);
				CHECK(strstr(log_text, "snapshot complete") == NULL);
			}
		}
		reset(); clock_rate[pass][0] = 119999999;
		CHECK(run() == EBUSY && reads == (pass == 0 ? 0U : 14U));
	}
}
static void
register_tests(void)
{
	unsigned int i, bit, field;
	struct iommu_observe_sample sample;
	uint32_t debug;

	reset(); completed(true);
	CHECK(strstr(log_text, "bypass_end 0xa00000000 cap_end 0x1000000000") != NULL);
	for (i = 0; i < 3; i++) {
		reset(); if (i == 0) resource_error = EIO; else resource_bad = (int)i;
		CHECK(run() == (i == 0 ? EIO : EINVAL));
		CHECK(exchanges == 0 && maps == 0 && releases == 1);
	}
	reset(); map_error = ENOMEM; CHECK(run() == ENOMEM);
	CHECK(exchanges == 8 && reads == 0 && unmaps == 0 && releases == 1);
	for (i = 0; i < 7; i++) {
		reset(); words[1][i] ^= 1; CHECK(run() == EAGAIN);
		CHECK(reads == 14 && exchanges == 16 && unmaps == 1);
		CHECK(strstr(log_text, "snapshot complete") == NULL);
	}
	for (i = 0; i < 4; i++) {
		reset(); clock_rate[1][i]++; CHECK(run() == EAGAIN);
	}
	for (field = 0; field < 3; field++) {
		for (i = 0; i < 16; i++) {
			reset(); debug = (words[0][0] & ~(0xfU << (field * 4))) | (i << (field * 4));
			words[0][0] = words[1][0] = debug;
			if (i < (field == 0 ? 4U : 6U)) {
				CHECK(run() == EOPNOTSUPP && reads == 1 && unmaps == 1 && exchanges == 8);
			} else completed(true);
		}
	}
	reset(); words[0][0] &= ~(1U << 29); CHECK(run() == EOPNOTSUPP && reads == 1);
	for (bit = 0; bit < 32; bit++) {
		reset(); words[0][1] = words[1][1] = 1U | (1U << bit);
		completed((UINT32_C(0x070f0e03) & (1U << bit)) != 0);
	}
	reset(); words[0][1] = words[1][1] = 0; completed(false);
	reset(); words[0][6] = words[1][6] = 0x80000000; completed(false);
	for (bit = 0; bit < 32; bit++) {
		reset(); words[0][4] = words[1][4] = 1U << bit; completed(false);
	}
	reset(); words[0][5] = words[1][5] = 0xa0; completed(false);
	reset(); words[0][3] = words[1][3] = 0xff; completed(false);
	reset(); words[0][5] = words[1][5] = 0x80000000; completed(false);
	reset(); words[0][5] = words[1][5] = 0x80000100; completed(true);
	reset(); words[0][5] = words[1][5] = 0x80000101; completed(false);
	reset(); words[0][3] = words[1][3] = 0x80000100; completed(false);
	reset(); words[0][3] = words[1][3] = UINT32_MAX; completed(false);
	reset(); words[0][5] = words[1][5] = UINT32_MAX; completed(false);
	reset(); words[0][0] = words[1][0] = UINT32_MAX;
	words[0][5] = words[1][5] = 0x80004000; completed(true);
	CHECK(strstr(log_text, "bypass_end 0x1000000000") != NULL);
	reset(); words[0][0] = words[1][0] = 0x30000664;
	words[0][5] = words[1][5] = 0x80004001; completed(false);
	reset(); words[0][0] = words[1][0] = 0x30000664;
	words[0][5] = words[1][5] = 0x80000001; words[0][3] = words[1][3] = 0x80000000;
	completed(true); CHECK(strstr(log_text, "bypass_end 0x400000 cap_end 0x10000000") != NULL);
	/* Classification never queries or maps. A rejection commits no range. */
	memset(&sample, 0, sizeof(sample)); sample.word[0][0] = 0x20000664;
	sample.word[0][1] = 1; sample.word[0][3] = 0x800000ff; sample.word[0][5] = UINT32_MAX;
	CHECK(!iommu_observe_classify(&sample) && sample.bypass_end == 0 && sample.cap_end == 0);
}
int
main(void)
{
	early_tests(); clock_tests(); register_tests();
	printf("IOMMU4 observer: %u checks PASS\n", checks);
	return 0;
}
