/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD tests of the production PowerVR registration and query. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

typedef uint32_t __u32;
typedef uint64_t __u64;
#define DRM_COMMAND_BASE 0x40
#define DRM_IOWR(n, t) (0xc0000000U | (sizeof(t) << 16) | ('d' << 8) | (n))
#define DRM_IOCTL_NR(n) ((n) & 0xff)
#include "pvr-uapi-body.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#ifndef __arraycount
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#endif
#define DRIVER_RENDER 1
#define DRIVER_GEM 8
#define DRM_RENDER_ALLOW 2
#define DRM_UNLOCKED 4
#define DRM_NATIVE_PLATFORM_VERSION 1
#define DRM_NATIVE_BUS_PLATFORM 2
#define KM_SLEEP 0
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
#define PTR_ERR(p) ((intptr_t)(p))
typedef void *device_t;
typedef void *bus_dma_tag_t;
struct drm_file { int unused; };
struct file;
struct uvm_object;
/* UVM exports this kernel type in uvm_param.h, not user sys/types.h. */
typedef off_t voff_t;
struct drm_device;
struct drm_ioctl_desc {
	unsigned long cmd;
	int (*func)(struct drm_device *, void *, struct drm_file *);
	unsigned int flags;
};
#define DRM_IOCTL_DEF_DRV(n, f, flags_) \
	[DRM_IOCTL_NR(DRM_IOCTL_##n) - DRM_COMMAND_BASE] = \
	{ .cmd = DRM_IOCTL_##n, .func = f, .flags = flags_ }
struct drm_driver {
	unsigned int driver_features;
	const struct drm_ioctl_desc *ioctls;
	size_t num_ioctls;
	void (*release)(struct drm_device *);
	int (*mmap_object)(struct drm_device *, off_t, size_t, int,
	    struct uvm_object **, voff_t *, struct file *);
	const void *gem_uvm_ops;
	const char *name, *desc, *date;
	unsigned int major, minor, patchlevel;
};
struct drm_device {
	void *dev_private;
	bus_dma_tag_t bus_dmat, bus_dmat32, dmat;
	struct {
		uint32_t version, length, bus_type;
		char fullname[64], compatible[64];
	} native_platform;
	struct drm_driver *driver;
	unsigned int refs;
};

static unsigned int checks, allocs, frees, private_allocs, private_frees;
static unsigned int registered, finalized, init_calls, mode;
static unsigned int copy_calls, fail_copy;
static struct drm_device *held;
static uint8_t *user_memory;
static size_t user_size;
#define CHECK(e) do { \
	checks++; \
	if (!(e)) { \
		fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #e); \
		abort(); \
	} \
} while (0)

static int
copyout(const void *from, void *to, size_t size)
{
	uintptr_t p = (uintptr_t)to, base = (uintptr_t)user_memory;

	copy_calls++;
	if (copy_calls == fail_copy || p < base || p - base > user_size ||
	    size > user_size - (p - base))
		return EFAULT;
	memcpy(to, from, size);
	return 0;
}

static void *
kmem_zalloc(size_t size, int flags)
{
	void *p;

	(void)flags;
	p = calloc(1, size);
	assert(p != NULL);
	private_allocs++;
	return p;
}

static void
kmem_free(void *p, size_t size)
{

	(void)size;
	private_frees++;
	free(p);
}

static void
kfree(void *p)
{

	frees++;
	free(p);
}

static size_t
test_strlcpy(char *to, const char *from, size_t size)
{
	size_t n = strlen(from);

	if (size != 0) {
		memcpy(to, from, MIN(n, size - 1));
		to[MIN(n, size - 1)] = 0;
	}
	return n;
}
#define strlcpy test_strlcpy

static const char *
device_xname(device_t dev)
{

	assert(dev != NULL);
	return "sun60igpuid0";
}

int drm_guarantee_initialized(void);
int
drm_guarantee_initialized(void)
{

	init_calls++;
	return mode == 1 ? ENOMEM : 0;
}

static struct drm_device *
drm_dev_alloc(struct drm_driver *driver, device_t parent)
{
	struct drm_device *dev;

	assert(parent != NULL);
	if (mode == 2)
		return (void *)(intptr_t)-ENOMEM;
	dev = calloc(1, sizeof(*dev));
	assert(dev != NULL);
	dev->driver = driver;
	dev->refs = 1;
	allocs++;
	return dev;
}

static void
drm_dev_fini(struct drm_device *dev)
{

	assert(dev != NULL);
	finalized++;
}

static void
drm_dev_put(struct drm_device *dev)
{

	assert(dev->refs != 0);
	if (--dev->refs == 0)
		dev->driver->release(dev);
}

static int
drm_dev_register(struct drm_device *dev, unsigned int flags)
{

	(void)flags;
	registered++;
	/* Simulate an open reference acquired during minor publication. */
	if (mode == 4) {
		dev->refs++;
		held = dev;
	}
	return mode >= 3 ? -EIO : 0;
}

/* BO behavior has a separate production-source contract. */
static const int drm_gem_cma_uvm_ops;
static int pvr_create_bo(struct drm_device *d, void *a, struct drm_file *f)
{ abort(); }
static int pvr_get_bo_mmap_offset(struct drm_device *d, void *a, struct drm_file *f)
{ abort(); }
static int pvr_mmap_object(struct drm_device *d, off_t o, size_t n, int p,
    struct uvm_object **u, voff_t *vo, struct file *f)
{ abort(); }

#include "pvr-drv-body.h"

_Static_assert(sizeof(struct drm_pvr_ioctl_dev_query_args) == 16, "query ABI");
_Static_assert(offsetof(struct drm_pvr_ioctl_dev_query_args, pointer) == 8,
    "query pointer ABI");
_Static_assert(sizeof(struct drm_pvr_dev_query_gpu_info) == 16, "GPU ABI");
_Static_assert(offsetof(struct drm_pvr_dev_query_gpu_info, num_phantoms) == 8,
    "GPU phantom ABI");
_Static_assert(DRM_IOCTL_NR(DRM_IOCTL_PVR_DEV_QUERY) == 0x40, "ioctl ABI");

static void
queries(struct drm_device *dev)
{
	struct drm_pvr_ioctl_dev_query_args args, saved;
	struct drm_pvr_dev_query_gpu_info info;
	uint8_t memory[1024], before[sizeof(memory)];
	unsigned int size, i;

	user_memory = memory + 8;
	user_size = sizeof(memory) - 16;
	for (size = 0; size < 700; size++) {
		memset(memory, 0xa5, sizeof(memory));
		memcpy(before, memory, sizeof(before));
		args = (struct drm_pvr_ioctl_dev_query_args){ 0, size,
		    (uintptr_t)user_memory };
		saved = args;
		copy_calls = fail_copy = 0;
		if (size < 12) {
			CHECK(pvr_dev_query(dev, &args, NULL) == -EINVAL);
			CHECK(copy_calls == 0);
			CHECK(memcmp(&args, &saved, sizeof(args)) == 0);
			CHECK(memcmp(memory, before, sizeof(memory)) == 0);
			continue;
		}
		CHECK(pvr_dev_query(dev, &args, NULL) == 0);
		CHECK(args.size == MIN(size, 16));
		memcpy(&info, user_memory, sizeof(info));
		CHECK(info.gpu_id == UINT64_C(0x00240038006800b7));
		CHECK(info.num_phantoms == 1);
		for (i = 12; i < size; i++)
			CHECK(user_memory[i] == 0);
		CHECK(memcmp(memory, before, 8) == 0);
		CHECK(memcmp(user_memory + size, before + 8 + size,
		    sizeof(memory) - 8 - size) == 0);
	}
	args = (struct drm_pvr_ioctl_dev_query_args){ 0, UINT32_MAX, 0 };
	copy_calls = 0;
	CHECK(pvr_dev_query(dev, &args, NULL) == 0);
	CHECK(args.size == 16 && copy_calls == 0);
	for (i = 1; i <= 7; i++) {
		args = (struct drm_pvr_ioctl_dev_query_args){ i, 16, 0 };
		saved = args;
		CHECK(pvr_dev_query(dev, &args, NULL) == -EOPNOTSUPP);
		CHECK(memcmp(&args, &saved, sizeof(args)) == 0);
	}
	args = (struct drm_pvr_ioctl_dev_query_args){ 0, 16, UINT64_MAX - 2 };
	saved = args;
	CHECK(pvr_dev_query(dev, &args, NULL) == -EFAULT);
	CHECK(copy_calls == 0 && memcmp(&args, &saved, sizeof(args)) == 0);
	args.pointer = 1;
	CHECK(pvr_dev_query(dev, &args, NULL) == -EFAULT);
	CHECK(args.size == 16);
	for (i = 1; i <= 3; i++) {
		args = (struct drm_pvr_ioctl_dev_query_args){ 0, 600,
		    (uintptr_t)user_memory };
		copy_calls = 0;
		fail_copy = i;
		CHECK(pvr_dev_query(dev, &args, NULL) == -EFAULT);
		CHECK(args.size == 600 && copy_calls == i);
	}
	fail_copy = 0;
}

int
main(void)
{
	struct drm_device *owner = NULL;
	device_t parent = (void *)(uintptr_t)1;
	bus_dma_tag_t dmat = (void *)(uintptr_t)2;
	unsigned int calls;

	CHECK(pvr_render_attach(parent, dmat, 0, &owner) == ENODEV);
	CHECK(pvr_render_attach(NULL, dmat, PVR_A733_BVNC, &owner) == EINVAL);
	CHECK(pvr_render_attach(parent, NULL, PVR_A733_BVNC, &owner) == EINVAL);
	CHECK(pvr_render_attach(parent, dmat, PVR_A733_BVNC, NULL) == EINVAL);
	CHECK(init_calls == 0 && allocs == 0 && owner == NULL);
	for (mode = 1; mode <= 4; mode++) {
		CHECK(pvr_render_attach(parent, dmat, PVR_A733_BVNC, &owner) ==
		    (mode <= 2 ? ENOMEM : EIO));
		CHECK(owner == NULL);
		if (mode == 4) {
			CHECK(held != NULL && held->dev_private != NULL);
			CHECK(private_allocs == private_frees + 1);
			queries(held);
			drm_dev_put(held);
			held = NULL;
		}
		CHECK(allocs == frees && private_allocs == private_frees);
		CHECK(finalized == frees);
	}
	mode = 0;
	CHECK(pvr_render_attach(parent, dmat, PVR_A733_BVNC, &owner) == 0);
	CHECK(owner != NULL && owner->refs == 1);
	CHECK(owner->dmat == dmat && owner->bus_dmat == dmat &&
	    owner->bus_dmat32 == dmat);
	CHECK(owner->native_platform.version == DRM_NATIVE_PLATFORM_VERSION);
	CHECK(owner->native_platform.length == sizeof(owner->native_platform));
	CHECK(owner->native_platform.bus_type == DRM_NATIVE_BUS_PLATFORM);
	CHECK(strcmp(owner->native_platform.fullname, "sun60igpuid0") == 0);
	CHECK(strcmp(owner->native_platform.compatible,
	    "allwinner,sun60i-a733-gpu") == 0);
	CHECK(strcmp(owner->driver->name, "powervr") == 0);
	CHECK(owner->driver->driver_features == (DRIVER_GEM | DRIVER_RENDER));
	CHECK(owner->driver->num_ioctls == 3);
	CHECK(owner->driver->mmap_object == pvr_mmap_object);
	CHECK(owner->driver->gem_uvm_ops == &drm_gem_cma_uvm_ops);
	CHECK(owner->driver->ioctls[0].flags == (DRM_RENDER_ALLOW | DRM_UNLOCKED));
	calls = registered;
	CHECK(pvr_render_attach(parent, dmat, PVR_A733_BVNC, &owner) == EALREADY);
	CHECK(registered == calls);
	queries(owner);
	drm_dev_put(owner);
	CHECK(allocs == frees && private_allocs == private_frees);
	printf("PASS: %u native PowerVR registration/query checks\n", checks);
	return 0;
}
