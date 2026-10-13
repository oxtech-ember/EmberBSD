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

typedef int32_t __s32;
typedef uint16_t __u16;
typedef uint32_t __u32;
typedef uint64_t __u64;
#define DRM_COMMAND_BASE 0x40
#define DRM_IOWR(n, t) (0xc0000000U | (sizeof(t) << 16) | ('d' << 8) | (n))
#define DRM_IOW(n, t) (0x80000000U | (sizeof(t) << 16) | ('d' << 8) | (n))
#define DRM_IOCTL_NR(n) ((n) & 0xff)
#include "pvr-uapi-body.h"
#include "pvr_backend.h"
#include "pvr-query.h-body.h"
#define CTASSERT(e) _Static_assert(e, #e)

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#ifndef __arraycount
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#endif
#define DRIVER_RENDER 1
#define DRIVER_GEM 8
#define DRIVER_SYNCOBJ 16
#define DRIVER_SYNCOBJ_TIMELINE 32
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
	int (*open)(struct drm_device *, struct drm_file *);
	void (*postclose)(struct drm_device *, struct drm_file *);
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

static int
copyin(const void *from, void *to, size_t size)
{
	uintptr_t p = (uintptr_t)from, base = (uintptr_t)user_memory;

	copy_calls++;
	if (copy_calls == fail_copy || p < base || p - base > user_size ||
	    size > user_size - (p - base))
		return EFAULT;
	memcpy(to, from, size);
	return 0;
}

#include "pvr-query.c-body.h"

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
/* Darwin can provide strlcpy as a fortified macro. Use the boundary model. */
#ifdef strlcpy
#undef strlcpy
#endif
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
	/* A host allocation cannot be a kernel ERR_PTR encoding. */
	CHECK(!IS_ERR(dev));
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
static int pvr_file_open(struct drm_device *d, struct drm_file *f)
{ abort(); }
static void pvr_file_close(struct drm_device *d, struct drm_file *f)
{ abort(); }
static int pvr_create_vm_context(struct drm_device *d, void *a, struct drm_file *f)
{ abort(); }
static int pvr_destroy_vm_context(struct drm_device *d, void *a, struct drm_file *f)
{ abort(); }
static int pvr_vm_map_ioctl(struct drm_device *d, void *a, struct drm_file *f)
{ abort(); }
static int pvr_vm_unmap_ioctl(struct drm_device *d, void *a, struct drm_file *f)
{ abort(); }
static int pvr_create_context(struct drm_device *d, void *a, struct drm_file *f)
{ abort(); }
static int pvr_destroy_context(struct drm_device *d, void *a, struct drm_file *f)
{ abort(); }
static int pvr_create_free_list(struct drm_device *d, void *a, struct drm_file *f)
{ abort(); }
static int pvr_destroy_free_list(struct drm_device *d, void *a, struct drm_file *f)
{ abort(); }
static int pvr_submit_jobs(struct drm_device *d, void *a, struct drm_file *f)
{ abort(); }
/* The separate heap contract tests the actual query and user-copy behavior. */
static int pvr_heap_query(uint32_t type, struct drm_pvr_ioctl_dev_query_args *a)
{
	CHECK(type == 4 || type == 5);
	CHECK(a->type == type);
	return -EINTR; /* Distinguish routing from a generic unsupported return. */
}

#define KASSERT(e) CHECK(e)
#define MUTEX_DEFAULT 0
#define IPL_NONE 0
typedef struct { bool held; } kmutex_t;
static void mutex_init(kmutex_t *m, int type, int level)
{ CHECK(type == 0 && level == 0); m->held = false; }
static void mutex_enter(kmutex_t *m) { CHECK(!m->held); m->held = true; }
static void mutex_exit(kmutex_t *m) { CHECK(m->held); m->held = false; }
static void mutex_destroy(kmutex_t *m) { CHECK(!m->held); }
static bool mutex_owned(kmutex_t *m) { return m->held; }
static void drm_dev_get(struct drm_device *dev) { CHECK(dev->refs != 0); dev->refs++; }
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
		if (i <= 3) {
			CHECK(pvr_dev_query(dev, &args, NULL) == 0);
			CHECK(args.size == (i == 1 ? 32 : 16));
		} else {
			CHECK(pvr_dev_query(dev, &args, NULL) ==
			    ((i == 4 || i == 5) ? -EINTR : -EOPNOTSUPP));
			CHECK(memcmp(&args, &saved, sizeof(args)) == 0);
		}
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

static bool platform_held;
static unsigned platform_entries, platform_leaves;
static void
backend_enter(void *cookie)
{
	struct pvr_device *sc = cookie;
	CHECK(mutex_owned(&sc->gate) && !platform_held);
	platform_held = true;
	platform_entries++;
}
static void
backend_leave(void *cookie)
{
	struct pvr_device *sc = cookie;
	CHECK(mutex_owned(&sc->gate) && platform_held);
	platform_held = false;
	platform_leaves++;
}
static int backend_alloc(void *c, size_t n, struct pvr_fw_object *o)
{ CHECK(false); return EIO; }
static int backend_free(void *c, struct pvr_fw_object *o)
{ CHECK(false); return EIO; }
static void backend_sync(void *c, struct pvr_fw_object *o, int flags)
{ CHECK(false); }
static int backend_command(void *c, const struct rogue_fwif_kccb_cmd *cmd, uint32_t *result)
{ CHECK(false); return EIO; }
static int backend_check(void *c)
{ CHECK(false); return EIO; }
static void backend_fail(void *c)
{ CHECK(false); }

static void
backend_lifetime(struct drm_device *dev)
{
	struct pvr_device *sc = dev->dev_private;
	struct pvr_backend backend = {
		.cookie = sc, .enter = backend_enter, .leave = backend_leave,
		.alloc = backend_alloc, .free = backend_free, .sync = backend_sync,
		.command = backend_command, .flush = backend_check,
		.check = backend_check, .fail = backend_fail,
	};
	struct pvr_backend invalid;
	struct pvr_orphan *orphan;
	const struct pvr_backend *selected;
	unsigned refs;
	uint32_t id;
	int retained_owner;

	CHECK(pvr_device_enter(dev) == NULL);
	CHECK(mutex_owned(&sc->gate));
	CHECK(pvr_device_next_id(dev, &id) == 0 && id == 1);
	CHECK(pvr_device_next_id(dev, &id) == 0 && id == 2);
	sc->next_id = UINT32_MAX;
	CHECK(pvr_device_next_id(dev, &id) == EOVERFLOW && id == 2);
	pvr_device_leave(dev);
	CHECK(!mutex_owned(&sc->gate));
	CHECK(pvr_backend_attach(NULL, &backend) == EINVAL);
	CHECK(pvr_backend_attach(dev, NULL) == EINVAL);
	for (unsigned n = 0; n < 10; n++) {
		invalid = backend;
		switch (n) {
		case 0: invalid.cookie = NULL; break;
		case 1: invalid.enter = NULL; break;
		case 2: invalid.leave = NULL; break;
		case 3: invalid.alloc = NULL; break;
		case 4: invalid.free = NULL; break;
		case 5: invalid.sync = NULL; break;
		case 6: invalid.command = NULL; break;
		case 7: invalid.flush = NULL; break;
		case 8: invalid.check = NULL; break;
		case 9: invalid.fail = NULL; break;
		}
		CHECK(pvr_backend_attach(dev, &invalid) == EINVAL);
		CHECK(!sc->execution && !mutex_owned(&sc->gate));
	}
	CHECK(pvr_backend_attach(dev, &backend) == 0);
	CHECK(pvr_backend_attach(dev, &backend) == EALREADY);
	/* Registration copies the immutable callbacks into device-owned storage. */
	backend.cookie = NULL;
	backend.enter = NULL;
	selected = pvr_device_enter(dev);
	CHECK(selected == &sc->backend && selected->cookie == sc);
	CHECK(platform_held && mutex_owned(&sc->gate));
	refs = dev->refs;
	pvr_device_retain(dev, &retained_owner);
	CHECK(dev->refs == refs + 1 && sc->orphans != NULL);
	CHECK(sc->orphans->owner == &retained_owner);
	pvr_device_leave(dev);
	CHECK(!platform_held && !mutex_owned(&sc->gate));
	CHECK(platform_entries == 1 && platform_leaves == 1);
	/* A model reset ends reachability. The production driver has no reset API. */
	orphan = sc->orphans;
	sc->orphans = orphan->next;
	kmem_free(orphan, sizeof(*orphan));
	drm_dev_put(dev);
	CHECK(dev->refs == refs);
}

int
main(void)
{
	struct pvr_query_input input = {
		.bvnc = UINT64_C(0x00240038006800b7),
		.brn_mask = 0x201, .ern_mask = 0xf5,
		.fw_major = 1, .fw_minor = 1, .fw_build = 6976702,
		.feature = {19456, 12, 16, 16, 2, 1, 6, 1, 1},
	};
	struct drm_device *owner = NULL;
	device_t parent = (void *)(uintptr_t)1;
	bus_dma_tag_t dmat = (void *)(uintptr_t)2;
	unsigned int calls;

	CHECK(pvr_render_attach(parent, dmat, 0, &input, &owner) == ENODEV);
	CHECK(pvr_render_attach(NULL, dmat, PVR_A733_BVNC, &input, &owner) == EINVAL);
	CHECK(pvr_render_attach(parent, NULL, PVR_A733_BVNC, &input, &owner) == EINVAL);
	CHECK(pvr_render_attach(parent, dmat, PVR_A733_BVNC, &input, NULL) == EINVAL);
	CHECK(init_calls == 0 && allocs == 0 && owner == NULL);
	for (mode = 1; mode <= 4; mode++) {
		CHECK(pvr_render_attach(parent, dmat, PVR_A733_BVNC, &input, &owner) ==
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
	CHECK(pvr_render_attach(parent, dmat, PVR_A733_BVNC, &input, &owner) == 0);
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
	CHECK(owner->driver->driver_features == (DRIVER_GEM | DRIVER_RENDER |
	    DRIVER_SYNCOBJ | DRIVER_SYNCOBJ_TIMELINE));
	CHECK(owner->driver->num_ioctls == 14);
	CHECK(owner->driver->open == pvr_file_open);
	CHECK(owner->driver->postclose == pvr_file_close);
	CHECK(owner->driver->ioctls[3].func == pvr_create_vm_context);
	CHECK(owner->driver->ioctls[4].func == pvr_destroy_vm_context);
	CHECK(owner->driver->ioctls[5].func == pvr_vm_map_ioctl);
	CHECK(owner->driver->ioctls[6].func == pvr_vm_unmap_ioctl);
	CHECK(owner->driver->mmap_object == pvr_mmap_object);
	CHECK(owner->driver->gem_uvm_ops == &drm_gem_cma_uvm_ops);
	CHECK(owner->driver->ioctls[0].flags == (DRM_RENDER_ALLOW | DRM_UNLOCKED));
	calls = registered;
	CHECK(pvr_render_attach(parent, dmat, PVR_A733_BVNC, &input, &owner) == EALREADY);
	CHECK(registered == calls);
	queries(owner);
	backend_lifetime(owner);
	drm_dev_put(owner);
	CHECK(allocs == frees && private_allocs == private_frees);
	printf("PASS: %u native PowerVR registration/query checks\n", checks);
	return 0;
}
