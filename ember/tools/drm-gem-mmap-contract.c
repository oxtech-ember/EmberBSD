/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual-source GEM-only and legacy mmap contracts. No device execution. */
#include <sys/types.h>
#include <sys/mman.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "target-errno.h"
#ifndef TEST_PAGE_SHIFT
#define TEST_PAGE_SHIFT 12
#endif
#define PAGE_SHIFT TEST_PAGE_SHIFT
#define PAGE_SIZE (1L << PAGE_SHIFT)
#define DRIVER_GEM 1
#define UVM_ADV_RANDOM 1
#define RW_READER 1
#ifndef __unused
#define __unused __attribute__((unused))
#endif
#ifndef __type_max
#define __type_max(t) ((t)((UINTMAX_C(1) << (sizeof(t) * CHAR_BIT - 1)) - 1))
#endif
#define container_of(p, t, m) ((t *)((char *)(p) - offsetof(t, m)))
typedef off_t voff_t;
typedef unsigned long vmem_addr_t;
struct file;
struct drm_device;
struct drm_file;
struct drm_vma_offset_node;
struct uvm_object { unsigned marker; };
struct kref { unsigned count; };
struct mock_lock { unsigned held; };
struct mock_tree { struct drm_vma_offset_node *node; };
struct drm_vma_offset_node {
	unsigned long von_startpage, von_npages;
	struct drm_file *allowed;
};
struct drm_vma_offset_manager {
	struct mock_lock vom_lock;
	struct mock_tree vom_nodes;
};
struct drm_gem_object {
	struct drm_device *dev;
	struct kref refcount;
	struct drm_vma_offset_node vma_node;
	struct uvm_object gemo_uvmobj;
	uint32_t first, last;
};
struct drm_driver {
	int (*mmap_object)(struct drm_device *, off_t, size_t, int,
	    struct uvm_object **, voff_t *, struct file *);
	const void *gem_uvm_ops;
};
struct drm_device {
	struct mock_lock struct_mutex;
	struct drm_vma_offset_manager *vma_offset_manager;
	struct drm_driver *driver;
};
struct drm_minor { struct drm_device *dev; };
struct drm_file { struct drm_minor *minor; struct file *filp; };
struct file { void *f_data; };
static unsigned checks, legacy_calls, destroyed, retained, released;
static struct uvm_object legacy_object;
static struct drm_vma_offset_manager manager;
static struct drm_gem_object object;
#define CHECK(e) do { checks++; if (!(e)) { \
	fprintf(stderr, "FAIL contract:%u: %s\n", (unsigned)__LINE__, #e); exit(1); \
} } while (0)
#define KASSERT(e) CHECK(e)
static void rw_enter(struct mock_lock *l, int type)
{ CHECK(type == RW_READER && l->held == 0); l->held++; }
static void rw_exit(struct mock_lock *l)
{ CHECK(l->held == 1); l->held--; }
static int rw_lock_held(const struct mock_lock *l) { return l->held != 0; }
static void mutex_lock(struct mock_lock *l)
{ CHECK(l->held == 0); l->held++; }
static void mutex_unlock(struct mock_lock *l)
{ CHECK(l->held == 1); l->held--; }
static int mutex_is_locked(const struct mock_lock *l) { return l->held != 0; }
static void *rb_tree_find_node(struct mock_tree *t, const vmem_addr_t *key)
{ return t->node != NULL && t->node->von_startpage == *key ? t->node : NULL; }
static void *rb_tree_find_node_leq(struct mock_tree *t, const vmem_addr_t *key)
{ return t->node != NULL && t->node->von_startpage <= *key ? t->node : NULL; }
static int drm_core_check_feature(const struct drm_device *dev, int feature)
{ CHECK(dev != NULL && feature == DRIVER_GEM); return 1; }
static unsigned long drm_vma_node_start(const struct drm_vma_offset_node *node)
{ return node->von_startpage; }
static int drm_vma_node_is_allowed(const struct drm_vma_offset_node *node,
    const struct drm_file *file)
{
	CHECK(manager.vom_lock.held == 0);
	return node->allowed == file;
}
static int kref_get_unless_zero(struct kref *ref)
{
	CHECK(manager.vom_lock.held == 1);
	if (ref->count == 0) return 0;
	ref->count++; retained++;
	return 1;
}
static void drm_gem_object_get(struct drm_gem_object *obj)
{
	CHECK(obj->refcount.count != 0);
	obj->refcount.count++; retained++;
}
static void drm_gem_object_put_unlocked(struct drm_gem_object *obj)
{
	CHECK(manager.vom_lock.held == 0 && obj->refcount.count != 0);
	released++;
	if (--obj->refcount.count == 0) destroyed++;
}
static int drm_legacy_mmap_object(const struct drm_device *dev, off_t offset,
    size_t size, int prot, struct uvm_object **uobjp, voff_t *offp,
    const struct file *file)
{
	CHECK(dev != NULL && file != NULL && size != 0 && prot == PROT_READ);
	legacy_calls++;
	*uobjp = &legacy_object; *offp = offset;
	return 0;
}
#include "gem-mmap-body.h"
static struct drm_driver driver = {
	.gem_uvm_ops = &legacy_object,
#include "gem-mmap-binding.h"
};
static struct drm_device test_device;
static struct drm_minor minor;
static struct drm_file owner, other;
static struct file owner_fp, other_fp;
static void
reset(unsigned long pages)
{
	CHECK(manager.vom_lock.held == 0 && test_device.struct_mutex.held == 0);
	object = (struct drm_gem_object) {
	    .dev = &test_device, .refcount = {1}, .vma_node = {4096, pages, &owner},
	    .gemo_uvmobj = {0x11223344}, .first = 0x1234abcd, .last = 0x98765432};
	manager.vom_nodes.node = &object.vma_node;
	legacy_calls = destroyed = retained = released = 0;
}
static void
map_case(struct file *fp, off_t offset, size_t size, int prot, int expected)
{
	struct uvm_object *uobj = &legacy_object;
	off_t relative = offset;
	unsigned refs = object.refcount.count;
	int flags = MAP_SHARED, advice = 0, maxprot = 0, error;

	/* The cdevsw caller asserts nonzero lengths before the driver API. */
	if (size == 0)
		error = -driver.mmap_object(&test_device, relative, size, prot, &uobj,
		    &relative, fp);
	else
		error = drm_fop_mmap(fp, &relative, size, prot, &flags, &advice,
		    &uobj, &maxprot);
	CHECK(error == expected);
	CHECK(legacy_calls == 0 && manager.vom_lock.held == 0 &&
	    test_device.struct_mutex.held == 0);
	if (expected == 0) {
		/* A successful NULL result would become anonymous zeroes in UVM. */
		CHECK(uobj == &object.gemo_uvmobj && relative == 0);
		CHECK(object.refcount.count == refs + 1);
		drm_gem_pager_detach(uobj);
	} else {
		CHECK(uobj == NULL && relative == 0);
		CHECK(object.refcount.count == refs);
	}
}
static void
legacy_cases(void)
{
	struct uvm_object *uobj = NULL;
	voff_t off = 0;
	off_t base = (off_t)object.vma_node.von_startpage << PAGE_SHIFT;

	CHECK(drm_gem_or_legacy_mmap_object(&test_device, base, 3 * PAGE_SIZE,
	    PROT_READ, &uobj, &off, &owner_fp) == 0);
	CHECK(uobj == &object.gemo_uvmobj && off == 0 && legacy_calls == 0);
	drm_gem_pager_detach(uobj);
	/* Preserve the old exact-size lookup and explicit legacy fallback. */
	CHECK(drm_gem_or_legacy_mmap_object(&test_device, base, PAGE_SIZE,
	    PROT_READ, &uobj, &off, &owner_fp) == 0);
	CHECK(uobj == &legacy_object && off == base && legacy_calls == 1);
	CHECK(drm_gem_or_legacy_mmap_object(&test_device, base - PAGE_SIZE, PAGE_SIZE,
	    PROT_READ, &uobj, &off, &owner_fp) == 0);
	CHECK(uobj == &legacy_object && legacy_calls == 2);
	CHECK(drm_gem_or_legacy_mmap_object(&test_device, base, 3 * PAGE_SIZE,
	    PROT_READ, &uobj, &off, &other_fp) == -EACCES);
	CHECK(legacy_calls == 2 && object.refcount.count == 1);
}
int
main(void)
{
	const unsigned long sizes[] = {3, 5, (16U * 1024 * 1024) / PAGE_SIZE};
	struct uvm_object *uobj = NULL;
	voff_t relative = 0;
	off_t base;

	test_device = (struct drm_device) {.vma_offset_manager = &manager, .driver = &driver};
	minor.dev = &test_device;
	owner = (struct drm_file) {.minor = &minor, .filp = &owner_fp};
	other = (struct drm_file) {.minor = &minor, .filp = &other_fp};
	owner_fp.f_data = &owner; other_fp.f_data = &other;
	for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
		reset(sizes[i]);
		base = (off_t)object.vma_node.von_startpage << PAGE_SHIFT;
		map_case(&owner_fp, base, PAGE_SIZE, PROT_READ, 0);
		map_case(&owner_fp, base, 2 * PAGE_SIZE, PROT_READ | PROT_WRITE, 0);
		map_case(&owner_fp, base, sizes[i] * PAGE_SIZE, PROT_READ, 0);
		if (sizes[i] * PAGE_SIZE >= 8294400) {
			size_t visible = (8294400 + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
			map_case(&owner_fp, base, visible, PROT_READ, 0);
		}
		map_case(&other_fp, base, PAGE_SIZE, PROT_READ, EACCES);
		map_case(&owner_fp, base, (sizes[i] + 1) * PAGE_SIZE, PROT_READ, EINVAL);
		map_case(&owner_fp, base + PAGE_SIZE, PAGE_SIZE, PROT_READ, EINVAL);
		map_case(&owner_fp, base - PAGE_SIZE, PAGE_SIZE, PROT_READ, EINVAL);
		map_case(&owner_fp, -PAGE_SIZE, PAGE_SIZE, PROT_READ, EINVAL);
		map_case(&owner_fp, base + 1, PAGE_SIZE, PROT_READ, EINVAL);
		map_case(&owner_fp, base, 0, PROT_READ, EINVAL);
		map_case(&owner_fp, base, PAGE_SIZE - 1, PROT_READ, EINVAL);
		map_case(&owner_fp, base, PAGE_SIZE, PROT_EXEC, EINVAL);
		map_case(&owner_fp, (__type_max(off_t) & ~(off_t)(PAGE_SIZE - 1)),
		    2 * PAGE_SIZE, PROT_READ, EINVAL);
		manager.vom_nodes.node = NULL;
		map_case(&owner_fp, base, PAGE_SIZE, PROT_READ, EINVAL);
		manager.vom_nodes.node = &object.vma_node;
		CHECK(object.refcount.count == 1 && retained == released);
		drm_gem_object_put_unlocked(&object);
		CHECK(destroyed == 1);
	}
	reset(3);
	base = (off_t)object.vma_node.von_startpage << PAGE_SHIFT;
	CHECK(drm_gem_mmap_object_strict(NULL, base, PAGE_SIZE, PROT_READ,
	    &uobj, &relative, &owner_fp) == -EINVAL);
	CHECK(drm_gem_mmap_object_strict(&test_device, base, PAGE_SIZE, PROT_READ,
	    &uobj, &relative, NULL) == -EINVAL);
	CHECK(drm_gem_mmap_object_strict(&test_device, base, PAGE_SIZE, PROT_READ,
	    NULL, &relative, &owner_fp) == -EINVAL);
	CHECK(drm_gem_mmap_object_strict(&test_device, base, PAGE_SIZE, PROT_READ,
	    &uobj, NULL, &owner_fp) == -EINVAL);
	CHECK(object.refcount.count == 1 && retained == released);
	legacy_cases();
	drm_gem_object_put_unlocked(&object);
	CHECK(destroyed == 1);
	reset(5);
	base = (off_t)object.vma_node.von_startpage << PAGE_SHIFT;
	CHECK(drm_gem_mmap_object_strict(&test_device, base, PAGE_SIZE, PROT_READ,
	    &uobj, &relative, &owner_fp) == 0 && uobj == &object.gemo_uvmobj);
	/* The mapping retains the same bytes after the last handle closes. */
	drm_gem_object_put_unlocked(&object);
	CHECK(object.refcount.count == 1 && destroyed == 0);
	CHECK(container_of(uobj, struct drm_gem_object, gemo_uvmobj)->first == 0x1234abcd);
	CHECK(container_of(uobj, struct drm_gem_object, gemo_uvmobj)->last == 0x98765432);
	drm_gem_pager_reference(uobj);
	CHECK(object.refcount.count == 2);
	drm_gem_pager_detach(uobj);
	drm_gem_pager_detach(uobj);
	CHECK(object.refcount.count == 0 && destroyed == 1);
	/* A teardown node can remain visible while its last ref is already zero. */
	map_case(&owner_fp, base, PAGE_SIZE, PROT_READ, EINVAL);
	CHECK(object.refcount.count == 0 && destroyed == 1);
	printf("PASS: %u actual GEM mmap checks page%u\n", checks, (unsigned)PAGE_SIZE);
	return 0;
}
