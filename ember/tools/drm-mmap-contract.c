/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD actual-source DRM mmap contract, AI-assisted. */

#include <sys/types.h>

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#define KASSERT(e) assert(e)
#define UVM_ADV_RANDOM 1
#ifndef ERESTARTSYS
#define ERESTARTSYS 512
#endif
#ifndef ERESTART
#define ERESTART 513
#endif

struct file;
struct drm_device;
struct uvm_object { int unused; };
struct drm_driver {
	int (*mmap_object)(struct drm_device *, off_t, size_t, int,
	    struct uvm_object **, off_t *, struct file *);
};
struct drm_device { const struct drm_driver *driver; };
struct drm_minor { struct drm_device *dev; };
struct drm_file { struct drm_minor *minor; struct file *filp; };
struct file { struct drm_file *f_data; };

static unsigned checks, calls;
static int callback_error, expected_prot;
static size_t expected_len;
static struct drm_device *expected_dev;
static struct file *expected_file;
static struct uvm_object old_object, new_object;

#define CHECK(e) do { \
	checks++; \
	if (!(e)) { \
		fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #e); \
		exit(EXIT_FAILURE); \
	} \
} while (0)

#include "drm-mmap-body.h"

static int
mapped(struct drm_device *dev, off_t offset, size_t len, int prot,
    struct uvm_object **uobjp, off_t *offp, struct file *fp)
{
	calls++;
	CHECK(dev == expected_dev);
	CHECK(fp == expected_file);
	CHECK(offset == 0x1000 && *offp == offset);
	CHECK(len == expected_len && prot == expected_prot);
	CHECK(*uobjp == &old_object);
	if (callback_error != 0)
		return -callback_error;
	*uobjp = &new_object;
	*offp = 0x2000;
	return 0;
}

int
main(void)
{
	const int errors[] = { 0, EIO, ERESTARTSYS };
	struct drm_driver driver = { 0 };
	struct drm_device dev = { .driver = &driver };
	struct drm_minor minor = { .dev = &dev };
	struct drm_file file = { .minor = &minor };
	struct file fp = { .f_data = &file };
	unsigned mode, prot, length;

	file.filp = &fp;
	expected_dev = &dev;
	expected_file = &fp;
	for (mode = 0; mode < 4; mode++) {
		for (prot = 0; prot < 8; prot++) {
			for (length = 1; length <= 2; length++) {
				struct uvm_object *uobj = &old_object;
				off_t offset = 0x1000;
				int flags = 0x55, advice = 0x66, maxprot = 0x77;
				int error;

				driver.mmap_object = mode == 0 ? NULL : mapped;
				callback_error = mode == 0 ? 0 : errors[mode - 1];
				expected_prot = (int)prot;
				expected_len = length * 4096;
				calls = 0;
				error = drm_fop_mmap(&fp, &offset, expected_len,
				    (int)prot, &flags, &advice, &uobj, &maxprot);
				CHECK(flags == 0x55);
				if (mode == 0) {
					CHECK(error == EOPNOTSUPP && calls == 0);
					CHECK(offset == 0x1000 && uobj == &old_object);
					CHECK(advice == 0x66 && maxprot == 0x77);
					continue;
				}
				CHECK(calls == 1);
				CHECK(error == (callback_error == ERESTARTSYS ?
				    ERESTART : callback_error));
				CHECK(maxprot == (int)prot && advice == UVM_ADV_RANDOM);
				CHECK(offset == (callback_error == 0 ? 0x2000 : 0x1000));
				CHECK(uobj == (callback_error == 0 ?
				    &new_object : &old_object));
			}
		}
	}
	printf("PASS: %u actual-source DRM mmap checks\n", checks);
	return EXIT_SUCCESS;
}
