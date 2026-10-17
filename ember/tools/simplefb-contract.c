/* Origin: EmberBSD actual-source firmware framebuffer contracts. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <inttypes.h>
#include <sys/bus.h>
#include <sys/device.h>
#include "simplefbvar.h"

#define PAGE_MASK 4095
#define BUS_SPACE_MAP_LINEAR 1
#define BUS_SPACE_MAP_PREFETCHABLE 2
#define PRIxBUSADDR PRIx64
static unsigned checks, assertions;
static void
check(bool condition, const char *expression, int line)
{
	checks++;
	if (!condition) {
		fprintf(stderr, "simplefb:%d: %s\n", line, expression);
		exit(1);
	}
}
#define CHECK(c) check((c), #c, __LINE__)
#define KASSERT(c) CHECK(c)
#define KASSERTMSG(c, ...) do { if (!(c)) assertions++; } while (0)
static void
print_message(const char *format, ...)
{
	(void)format;
}
static void
print_device_message(device_t device, const char *format, ...)
{
	(void)device;
	(void)format;
}
#define aprint_error print_message
#define aprint_normal print_message
#define aprint_naive print_message
#define aprint_normal_dev print_device_message

struct properties {
	uint64_t address, virtual_address, override;
	uint32_t width, height, stride;
	uint8_t depth;
	bool swapped, tenbit, console;
};
struct device {
	void *private;
	struct properties properties;
};
typedef struct properties *prop_dictionary_t;
struct genfb_softc {
	device_t sc_dev;
	int sc_width;
	size_t sc_fbsize;
};
struct genfb_ops {
	int (*genfb_ioctl)(void);
	int (*genfb_mmap)(void);
};
struct fdt_attach_args {
	int faa_phandle;
	bus_space_tag_t faa_bst;
};
struct firmware {
	bus_addr_t address;
	bus_size_t size;
	uint32_t width, height, stride;
	const char *format, *missing;
	bool reg_error, map_error, finalize_error;
};
static struct firmware firmware;
static unsigned maps, attaches, cnattaches, registrations;
static bus_addr_t mapped_address;
static bus_size_t mapped_size;
static int mapped_flags;
static int (*finalizer)(device_t);
static device_t finalize_device;
static unsigned char mapping[16];
static prop_dictionary_t
device_properties(device_t d)
{
	return &d->properties;
}

static void *
device_private(device_t d)
{
	return d->private;
}

static int
fdtbus_get_reg(int phandle, int reg_index, bus_addr_t *address, bus_size_t *size)
{
	(void)phandle;
	(void)reg_index;
	if (firmware.reg_error)
		return ENXIO;
	*address = firmware.address;
	*size = firmware.size;
	return 0;
}

static int
of_getprop_uint32(int phandle, const char *name, uint32_t *value)
{
	(void)phandle;
	if (firmware.missing && strcmp(name, firmware.missing) == 0)
		return ENXIO;
	if (strcmp(name, "width") == 0)
		*value = firmware.width;
	else if (strcmp(name, "height") == 0)
		*value = firmware.height;
	else if (strcmp(name, "stride") == 0)
		*value = firmware.stride;
	else
		return ENXIO;
	return 0;
}

static const char *
fdtbus_get_string(int phandle, const char *name)
{
	(void)phandle;
	CHECK(strcmp(name, "format") == 0);
	return firmware.format;
}

static bool
prop_dictionary_get_uint64(prop_dictionary_t p, const char *name, uint64_t *v)
{
	CHECK(strcmp(name, "simplefb-physaddr") == 0);
	*v = p->override;
	return *v != 0;
}

static void
prop_dictionary_set_uint64(prop_dictionary_t p, const char *name, uint64_t v)
{
	if (strcmp(name, "address") == 0)
		p->address = v;
	else if (strcmp(name, "virtual_address") == 0)
		p->virtual_address = v;
	else
		CHECK(false);
}

static void
prop_dictionary_set_uint32(prop_dictionary_t p, const char *name, uint32_t v)
{
	if (strcmp(name, "width") == 0)
		p->width = v;
	else if (strcmp(name, "height") == 0)
		p->height = v;
	else if (strcmp(name, "linebytes") == 0)
		p->stride = v;
	else if (strcmp(name, "address") == 0)
		p->address = v;
	else
		CHECK(false);
}

static void
prop_dictionary_set_uint16(prop_dictionary_t p, const char *n, uint16_t v)
{
	prop_dictionary_set_uint32(p, n, v);
}

static void
prop_dictionary_set_uint8(prop_dictionary_t p, const char *n, uint8_t v)
{
	CHECK(strcmp(n, "depth") == 0);
	p->depth = v;
}

static void
prop_dictionary_set_bool(prop_dictionary_t p, const char *name, bool v)
{
	if (strcmp(name, "is_swapped") == 0)
		p->swapped = v;
	else if (strcmp(name, "is_10bit") == 0)
		p->tenbit = v;
	else if (strcmp(name, "is_console") == 0)
		p->console = v;
	else
		CHECK(false);
}

static int
bus_space_map(bus_space_tag_t tag, bus_addr_t address, bus_size_t size,
	      int flags, bus_space_handle_t *handle)
{
	(void)tag;
	maps++;
	mapped_address = address;
	mapped_size = size;
	mapped_flags = flags;
	if (firmware.map_error)
		return ENOMEM;
	*handle = (uintptr_t)mapping;
	return 0;
}

static void *
bus_space_vaddr(bus_space_tag_t t, bus_space_handle_t h)
{
	(void)t;
	return (void *)h;
}

static void
genfb_init(struct genfb_softc *sc)
{
	const struct properties *p = device_properties(sc->sc_dev);
	sc->sc_width = (int)p->width;
	/* The seam must not repeat genfb's signed multiplication. */
	sc->sc_fbsize = (size_t)p->height * p->stride;
}

static void
genfb_attach(const struct genfb_softc *s, const struct genfb_ops *o)
{
	CHECK(s != NULL && o != NULL);
	attaches++;
}

static void
genfb_cnattach(void)
{
	cnattaches++;
}

static bool
simplefb_shutdown(device_t d, int flags)
{
	(void)d;
	(void)flags;
	return true;
}

static int
simplefb_ioctl(void)
{
	return 0;
}

static int
simplefb_mmap(void)
{
	return 0;
}

static void
pmf_device_register1(device_t d, const void *s, const void *r, bool (*f)(device_t, int))
{
	CHECK(d != NULL && !s && !r && f != NULL);
}

static int
config_finalize_register(device_t d, int (*f)(device_t))
{
	registrations++;
	finalizer = f;
	finalize_device = d;
	return firmware.finalize_error ? ENOMEM : 0;
}

#include "simplefb-structure.h"
static int simplefb_console_phandle = 7;
static struct simplefb_softc *simplefb_deferred;
static device_t simplefb_owner;
static bool simplefb_committed;
static int simplefb_attach_genfb(struct simplefb_softc *);
#include "simplefb-body.h"

static struct device framebuffer_device;
static struct simplefb_softc test_sc;
static void
reset(void)
{
	firmware = (struct firmware){
		.address = 0x200000, .size = 1280 * 1024 * 4,
		    .width = 1280, .height = 1024, .stride = 1280 * 4, .format = "x8r8g8b8"
	};
	memset(&test_sc, 0, sizeof(test_sc));
	memset(&framebuffer_device, 0, sizeof(framebuffer_device));
	framebuffer_device.private = &test_sc;
	test_sc.sc_gen.sc_dev = &framebuffer_device;
	test_sc.sc_phandle = 7;
	test_sc.sc_bst = &firmware;
	maps = attaches = cnattaches = registrations = assertions = 0;
	finalizer = NULL;
	finalize_device = NULL;
	simplefb_deferred = NULL;
	simplefb_owner = NULL;
	simplefb_committed = false;
}

static void
attach(int phandle)
{
	struct fdt_attach_args args = { .faa_phandle = phandle, .faa_bst = &firmware };
	simplefb_attach(NULL, &framebuffer_device, &args);
}

static void
legacy_oracle(const char *which)
{
	reset();
	if (strcmp(which, "null-owner") == 0) {
		simplefb_deferred = &test_sc;
		CHECK(!simplefb_console_reserve(NULL));
		return;
	}
	if (strcmp(which, "high-address") == 0)
		firmware.address = UINT64_C(0x100000123);
	else if (strcmp(which, "wide-stride") == 0) {
		firmware.width = 16384;
		firmware.height = 1;
		firmware.stride = 65536;
		firmware.size = 65536;
	} else if (strcmp(which, "zero-width") == 0)
		firmware.width = 0;
	else if (strcmp(which, "short-stride") == 0)
		firmware.stride = 1;
	else if (strcmp(which, "span-overflow") == 0) {
		firmware.width = 1;
		firmware.height = UINT32_C(0x40000000);
		firmware.stride = 4;
		firmware.size = 4096;
	} else if (strcmp(which, "address-wrap") == 0) {
		firmware.address = UINT64_MAX - 4095;
		firmware.size = 8192;
		firmware.width = 1;
		firmware.height = 1;
		firmware.stride = 4;
	} else
		CHECK(false);
	attach(8);
	if (strcmp(which, "high-address") == 0) {
		CHECK(attaches == 1);
		CHECK(framebuffer_device.properties.address == firmware.address);
	} else if (strcmp(which, "wide-stride") == 0) {
		CHECK(attaches == 1);
		CHECK(framebuffer_device.properties.stride == 65536);
	} else
		CHECK(maps == 0 && attaches == 0);
}

#ifndef SIMPLEFB_BASELINE
static void
reader_failure(void)
{
	struct simplefb_framebuffer result, before;
	memset(&result, 0x5a, sizeof(result));
	before = result;
	CHECK(simplefb_read_framebuffer(&test_sc, &result) != 0);
	CHECK(memcmp(&result, &before, sizeof(result)) == 0);
	CHECK(maps == 0);
}

static void
test_reader(void)
{
	static const char *const formats[] = {
		"a8b8g8r8", "x8r8g8b8", "r8g8b8a8",
		"b8g8r8x8", "x2r10g10b10", "r5g6b5"
	};
	static const enum simplefb_pixel_format ids[] = {
		SIMPLEFB_FORMAT_A8B8G8R8, SIMPLEFB_FORMAT_X8R8G8B8,
		SIMPLEFB_FORMAT_R8G8B8A8, SIMPLEFB_FORMAT_B8G8R8X8,
		SIMPLEFB_FORMAT_X2R10G10B10, SIMPLEFB_FORMAT_R5G6B5
	};
	struct simplefb_framebuffer fb;
	for (unsigned i = 0; i < 6; i++) {
		reset();
		firmware.format = formats[i];
		CHECK(simplefb_read_framebuffer(&test_sc, &fb) == 0);
		CHECK(fb.sf_format == ids[i]);
		CHECK(fb.sf_depth == (i == 5 ? 16 : 32));
		CHECK(fb.sf_bst == test_sc.sc_bst && fb.sf_address == firmware.address);
		CHECK(fb.sf_size == firmware.size && fb.sf_fbsize == firmware.size);
		attach(8);
		CHECK(attaches == 1 && maps == 1);
		CHECK(mapped_address == firmware.address && mapped_size == firmware.size);
		CHECK(mapped_flags == (BUS_SPACE_MAP_LINEAR | BUS_SPACE_MAP_PREFETCHABLE));
		CHECK(framebuffer_device.properties.swapped == (i == 2 || i == 3));
		CHECK(framebuffer_device.properties.tenbit == (i == 4));
	}
	reset();
	firmware.reg_error = true;
	reader_failure();
	reset();
	firmware.size = 0;
	reader_failure();
	reset();
	firmware.format = NULL;
	reader_failure();
	reset();
	firmware.format = "unknown";
	reader_failure();
	const char *missing[] = { "width", "height", "stride" };
	for (unsigned i = 0; i < 3; i++) {
		reset();
		firmware.missing = missing[i];
		reader_failure();
	}
	reset();
	firmware.width = UINT32_MAX;
	reader_failure();
	reset();
	firmware.height = UINT32_MAX;
	reader_failure();
	reset();
	firmware.stride = UINT32_MAX;
	reader_failure();
	reset();
	firmware.height = 0;
	reader_failure();
	reset();
	firmware.stride = 0;
	reader_failure();
	reset();
	firmware.size--;
	reader_failure();
	reset();
	firmware.address = UINT64_MAX - firmware.size;
	reader_failure();
	reset();
	framebuffer_device.properties.override = UINT64_MAX - firmware.size;
	reader_failure();
	reset();
	framebuffer_device.properties.override = UINT64_C(0x100000123);
	CHECK(simplefb_read_framebuffer(&test_sc, &fb) == 0);
	CHECK(fb.sf_address == framebuffer_device.properties.override);
	reset();
	firmware.map_error = true;
	attach(8);
	CHECK(maps == 1 && attaches == 0);
}

static void
test_ownership(void)
{
	struct device owner = {0 };
#ifdef VIRTGPU_CONSOLE
	struct device other = { 0 }, wrong_framebuffer = {0 };
#endif
	struct simplefb_framebuffer fb;
	reset();
	attach(7);
#ifndef VIRTGPU_CONSOLE
	CHECK(attaches == 1 && maps == 1 && registrations == 0);
	CHECK(!simplefb_console_reserve(&owner));
	CHECK(simplefb_console_get_framebuffer(&owner, &fb) == EPERM);
#else
	CHECK(attaches == 0 && maps == 0 && registrations == 1);
	CHECK(!simplefb_console_reserve(NULL));
	CHECK(simplefb_console_get_framebuffer(&owner, &fb) == EPERM);
	CHECK(simplefb_console_reserve(&owner));
	CHECK(!simplefb_console_reserve(&owner) && !simplefb_console_reserve(&other));
	CHECK(simplefb_console_get_framebuffer(&other, &fb) == EPERM);
	CHECK(simplefb_console_get_framebuffer(&owner, NULL) == EINVAL);
	firmware.width = 1;
	firmware.address = 0;
	CHECK(simplefb_console_get_framebuffer(&owner, &fb) == 0);
	CHECK(fb.sf_width == 1280 && fb.sf_address == 0x200000);
	CHECK(!simplefb_console_release(NULL) && !simplefb_console_release(&other));
	simplefb_console_commit(&other);
	CHECK(assertions == 1 && !simplefb_committed);
	CHECK(simplefb_console_release(&owner));
	CHECK(simplefb_console_reserve(&other));
	simplefb_console_commit(&other);
	CHECK(simplefb_committed);
	CHECK(!simplefb_console_release(&other));
	CHECK(simplefb_console_get_framebuffer(&other, &fb) == EPERM);
	simplefb_console_commit(&other);
	CHECK(assertions == 2);
	CHECK(finalizer(finalize_device) == 0);
	CHECK(attaches == 0 && maps == 0);
	CHECK(finalizer(finalize_device) == 0);
	CHECK(!simplefb_console_reserve(&owner));
	reset();
	attach(7);
	CHECK(simplefb_console_reserve(&owner));
	CHECK(finalizer(&wrong_framebuffer) == 0);
	CHECK(attaches == 0);
	CHECK(finalizer(finalize_device) == 0);
	CHECK(attaches == 1 && maps == 1);
	CHECK(finalizer(finalize_device) == 0);
	CHECK(attaches == 1);
	CHECK(!simplefb_console_release(&owner));
	reset();
	firmware.finalize_error = true;
	attach(7);
	CHECK(attaches == 1 && maps == 1 && !simplefb_console_reserve(&owner));
	reset();
	firmware.format = "bad";
	attach(7);
	CHECK(simplefb_console_reserve(&owner));
	CHECK(simplefb_console_get_framebuffer(&owner, &fb) == EINVAL);
	/*
	 * VirtGPU needs only selection; its replacement scanout can still
	 * commit.
	 */
	simplefb_console_commit(&owner);
	CHECK(finalizer(finalize_device) == 0);
	CHECK(attaches == 0 && maps == 0);
	reset();
	firmware.format = "bad";
	attach(7);
	CHECK(finalizer(finalize_device) == 0);
	CHECK(attaches == 0 && maps == 0);
#endif
}
#endif
int
main(int argc, char **argv)
{
	if (argc == 2) {
		legacy_oracle(argv[1]);
		return 0;
	}
#ifndef SIMPLEFB_BASELINE
	const char *oracles[] = {
		"high-address", "wide-stride", "zero-width", "short-stride",
		"span-overflow", "address-wrap", "null-owner"
	};
	for (unsigned i = 0; i < sizeof(oracles) / sizeof(oracles[0]); i++)
		legacy_oracle(oracles[i]);
	test_reader();
	test_ownership();
	printf("PASS: %u firmware descriptor and console lifecycle checks\n", checks);
#endif
	return 0;
}
