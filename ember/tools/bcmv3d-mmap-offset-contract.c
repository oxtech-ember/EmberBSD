/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual native callers and VMA publication, with controlled pthread services. */
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PAGE_SIZE 4096U
#define RW_WRITER 1
#define VM_NOSLEEP 1
#define VM_BESTFIT 2
#define __diagused
#define CHECK(c) do { atomic_fetch_add(&checks, 1); if (!(c)) { \
	fprintf(stderr, "FAIL offset:%u: %s\n", (unsigned)__LINE__, #c); exit(1); \
} } while (0)
#define KASSERT(c) CHECK(c)
typedef uint32_t u32;
typedef uint64_t u64;
typedef unsigned long vmem_size_t;
struct contract_mutex { pthread_mutex_t real; };
struct drm_vma_offset_node { unsigned long von_startpage, von_npages; };
struct mock_tree { struct drm_vma_offset_node *node; };
struct drm_vma_offset_manager {
	struct contract_mutex vom_lock;
	struct mock_tree vom_nodes;
	void *vom_vmem;
};
struct drm_device;
struct drm_gem_object {
	struct drm_device *dev;
	struct drm_vma_offset_node vma_node;
	size_t size;
	void *import_attach;
	atomic_uint refs;
};
struct drm_file { struct drm_gem_object *handles[3]; };
struct drm_driver {
	bool dumb_create;
	int (*dumb_map_offset)(struct drm_file *, struct drm_device *, u32, u64 *);
};
struct drm_device {
	struct contract_mutex struct_mutex;
	struct drm_vma_offset_manager *vma_offset_manager;
	struct drm_driver *driver;
};
struct drm_v3d_mmap_bo { u32 handle, flags; u64 offset; };
struct drm_mode_map_dumb { u32 handle, pad; u64 offset; };
static atomic_uint checks;
static struct drm_vma_offset_manager manager;
static struct drm_device device;
static struct drm_gem_object object;
static struct drm_file owner_file, stranger;
static pthread_mutex_t schedule = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
static _Thread_local unsigned worker, native_depth;
static bool pause_enabled, paused, resume_first, second_attempt, published;
static unsigned pause_stage, allocations, releases, inserts, attempts, frees;
static int allocation_error;

static void
pause_first(unsigned stage)
{
	CHECK(pthread_mutex_lock(&schedule) == 0);
	if (pause_enabled && worker == 1 && pause_stage == stage) {
		paused = true;
		CHECK(pthread_cond_broadcast(&changed) == 0);
		while (!resume_first)
			CHECK(pthread_cond_wait(&changed, &schedule) == 0);
	}
	CHECK(pthread_mutex_unlock(&schedule) == 0);
}

static void
mutex_lock(struct contract_mutex *lock)
{
	CHECK(lock == &device.struct_mutex && native_depth == 0);
	CHECK(pthread_mutex_lock(&schedule) == 0);
	if (worker == 2) {
		second_attempt = true;
		CHECK(pthread_cond_broadcast(&changed) == 0);
	}
	CHECK(pthread_mutex_unlock(&schedule) == 0);
	CHECK(pthread_mutex_lock(&lock->real) == 0);
	native_depth++;
}

static void
mutex_unlock(struct contract_mutex *lock)
{
	CHECK(lock == &device.struct_mutex && native_depth == 1);
	native_depth--;
	CHECK(pthread_mutex_unlock(&lock->real) == 0);
}

static void
rw_enter(struct contract_mutex *lock, int type)
{
	CHECK(lock == &manager.vom_lock && type == RW_WRITER);
	/* Actual add has assigned both node fields before this writer lock. */
	pause_first(1);
	CHECK(pthread_mutex_lock(&lock->real) == 0);
}

static void
rw_exit(struct contract_mutex *lock)
{
	CHECK(lock == &manager.vom_lock);
	CHECK(pthread_mutex_unlock(&lock->real) == 0);
}

static int
vmem_alloc(const void *arena, unsigned long pages, int flags, vmem_size_t *start)
{
	CHECK(arena == &manager && pages == 5 && flags == (VM_NOSLEEP | VM_BESTFIT));
	attempts++;
	if (allocation_error != 0)
		return allocation_error;
	CHECK(pthread_mutex_lock(&schedule) == 0);
	allocations++;
	/* Two first allocations for one retained object would leak a range. */
	CHECK(allocations == 1);
	*start = 0x10000;
	CHECK(pthread_mutex_unlock(&schedule) == 0);
	pause_first(0);
	return 0;
}

static void
vmem_free(const void *arena, unsigned long start, unsigned long pages)
{
	CHECK(arena == &manager && start == 0x10000 && pages == 5);
	releases++;
}

static struct drm_vma_offset_node *
rb_tree_insert_node(struct mock_tree *tree, struct drm_vma_offset_node *node)
{
	CHECK(tree == &manager.vom_nodes && tree->node == NULL);
	tree->node = node;
	published = true;
	inserts++;
	return node;
}

static void
rb_tree_remove_node(struct mock_tree *tree, const struct drm_vma_offset_node *node)
{
	CHECK(tree == &manager.vom_nodes && tree->node == node && published);
	tree->node = NULL;
	published = false;
}

static u64
drm_vma_node_offset_addr(const struct drm_vma_offset_node *node)
{
	/* A nonzero node field alone does not establish a published mapping. */
	CHECK(published && manager.vom_nodes.node == node);
	return (u64)node->von_startpage * PAGE_SIZE;
}

static struct drm_gem_object *
drm_gem_object_lookup(struct drm_file *owner, u32 handle)
{
	struct drm_gem_object *obj;

	CHECK(native_depth == 0);
	CHECK(pthread_mutex_lock(&schedule) == 0);
	obj = handle < 3 ? owner->handles[handle] : NULL;
	if (obj != NULL)
		CHECK(atomic_fetch_add(&obj->refs, 1) > 0);
	CHECK(pthread_mutex_unlock(&schedule) == 0);
	return obj;
}
static void drm_gem_object_put_unlocked(struct drm_gem_object *);
#include "offset-body.h"
static struct drm_driver driver = {
	.dumb_create = true,
#include "offset-binding.h"
};

static void
drm_gem_object_put_unlocked(struct drm_gem_object *obj)
{
	CHECK(native_depth == 0 && atomic_load(&obj->refs) > 0);
	if (atomic_fetch_sub(&obj->refs, 1) == 1) {
		drm_vma_offset_remove(&manager, &obj->vma_node);
		frees++;
	}
}

struct call { unsigned id, kind; u32 handle; int result; u64 offset; };
static void *
invoke(void *data)
{
	struct call *call = data;

	worker = call->id;
	call->offset = UINT64_MAX;
	if (call->kind == 0) {
		struct drm_v3d_mmap_bo args = { .handle = call->handle };
		call->result = bcmv3d_mmap_bo(&device, &args, &owner_file);
		call->offset = args.offset;
	} else {
		struct drm_mode_map_dumb args = { .handle = call->handle };
		call->result = drm_mode_mmap_dumb_ioctl(&device, &args, &owner_file);
		call->offset = args.offset;
	}
	CHECK(native_depth == 0);
	return NULL;
}

static void
reset(void)
{
	CHECK(!published && manager.vom_nodes.node == NULL);
	object = (struct drm_gem_object) { .dev = &device, .size = 5 * PAGE_SIZE };
	atomic_init(&object.refs, 2);
	owner_file = (struct drm_file) { .handles = { NULL, &object, &object } };
	paused = resume_first = second_attempt = pause_enabled = false;
	allocations = releases = inserts = attempts = frees = 0;
	allocation_error = 0;
}

static void
close_handle(u32 handle)
{
	struct drm_gem_object *obj;

	CHECK(pthread_mutex_lock(&schedule) == 0);
	obj = owner_file.handles[handle];
	owner_file.handles[handle] = NULL;
	CHECK(pthread_mutex_unlock(&schedule) == 0);
	CHECK(obj != NULL);
	drm_gem_object_put_unlocked(obj);
}

static void
concurrent(unsigned stage, unsigned first_kind, unsigned second_kind, bool alias)
{
	pthread_t first, second;
	struct call a = { .id = 1, .kind = first_kind, .handle = 1 };
	struct call b = { .id = 2, .kind = second_kind, .handle = alias ? 2 : 1 };

	reset();
	pause_enabled = true;
	pause_stage = stage;
	CHECK(pthread_create(&first, NULL, invoke, &a) == 0);
	CHECK(pthread_mutex_lock(&schedule) == 0);
	while (!paused)
		CHECK(pthread_cond_wait(&changed, &schedule) == 0);
	CHECK(pthread_mutex_unlock(&schedule) == 0);
	CHECK(pthread_create(&second, NULL, invoke, &b) == 0);
	CHECK(pthread_mutex_lock(&schedule) == 0);
	while (!second_attempt)
		CHECK(pthread_cond_wait(&changed, &schedule) == 0);
	resume_first = true;
	CHECK(pthread_cond_broadcast(&changed) == 0);
	CHECK(pthread_mutex_unlock(&schedule) == 0);
	CHECK(pthread_join(first, NULL) == 0 && pthread_join(second, NULL) == 0);
	pause_enabled = false;
	CHECK(a.result == 0 && b.result == 0 && a.offset == b.offset);
	CHECK(a.offset == UINT64_C(0x10000) * PAGE_SIZE);
	CHECK(allocations == 1 && inserts == 1 && attempts == 1);
	CHECK(atomic_load(&object.refs) == 2 && published);
	close_handle(1);
	CHECK(published && frees == 0);
	close_handle(2);
	CHECK(!published && releases == 1 && frees == 1);
}

static void
close_during_publication(void)
{
	pthread_t first;
	struct call call = { .id = 1, .kind = 0, .handle = 1 };

	reset();
	pause_enabled = true;
	pause_stage = 1;
	CHECK(pthread_create(&first, NULL, invoke, &call) == 0);
	CHECK(pthread_mutex_lock(&schedule) == 0);
	while (!paused)
		CHECK(pthread_cond_wait(&changed, &schedule) == 0);
	CHECK(pthread_mutex_unlock(&schedule) == 0);
	close_handle(1);
	close_handle(2);
	CHECK(atomic_load(&object.refs) == 1 && frees == 0 && !published);
	CHECK(pthread_mutex_lock(&schedule) == 0);
	resume_first = true;
	CHECK(pthread_cond_broadcast(&changed) == 0);
	CHECK(pthread_mutex_unlock(&schedule) == 0);
	CHECK(pthread_join(first, NULL) == 0);
	pause_enabled = false;
	/* A concurrent handle close may invalidate the returned offset. */
	CHECK(call.result == 0 && call.offset != 0);
	CHECK(atomic_load(&object.refs) == 0 && frees == 1);
	CHECK(!published && allocations == 1 && releases == 1);
}

static void
errors_and_lifetime(void)
{
	struct drm_v3d_mmap_bo args = { .handle = 1, .flags = 1, .offset = 7 };
	u64 offset = 7;

	reset();
	CHECK(bcmv3d_mmap_bo(&device, &args, &owner_file) == -EINVAL && args.offset == 0);
	args.flags = 0;
	CHECK(bcmv3d_mmap_bo(&device, &args, &stranger) == -ENOENT && args.offset == 0);
	CHECK(bcmv3d_dumb_map_offset(&stranger, &device, 1, &offset) == -ENOENT && offset == 0);
	object.import_attach = &object;
	CHECK(bcmv3d_dumb_map_offset(&owner_file, &device, 1, &offset) == -EINVAL && offset == 0);
	CHECK(attempts == 0 && atomic_load(&object.refs) == 2);
	object.import_attach = NULL;
	allocation_error = ENOMEM;
	CHECK(bcmv3d_mmap_bo(&device, &args, &owner_file) == -ENOSPC && args.offset == 0);
	CHECK(bcmv3d_dumb_map_offset(&owner_file, &device, 1, &offset) == -ENOSPC && offset == 0);
	CHECK(!published && allocations == 0 && atomic_load(&object.refs) == 2);
	allocation_error = EIO;
	CHECK(bcmv3d_dumb_map_offset(&owner_file, &device, 1, &offset) == -EIO && offset == 0);
	allocation_error = 0;
	CHECK(bcmv3d_dumb_map_offset(&owner_file, &device, 2, &offset) == 0 && published);
	CHECK(bcmv3d_mmap_bo(&device, &args, &owner_file) == 0 && args.offset == offset);
	CHECK(allocations == 1 && inserts == 1 && atomic_load(&object.refs) == 2);
	close_handle(1);
	close_handle(2);
	CHECK(!published && releases == 1 && frees == 1);
}

int
main(void)
{
	CHECK(pthread_mutex_init(&device.struct_mutex.real, NULL) == 0);
	CHECK(pthread_mutex_init(&manager.vom_lock.real, NULL) == 0);
	manager.vom_vmem = &manager;
	device.vma_offset_manager = &manager;
	device.driver = &driver;
	for (unsigned stage = 0; stage < 2; stage++)
		for (unsigned a = 0; a < 2; a++)
			for (unsigned b = 0; b < 2; b++)
				for (unsigned alias = 0; alias < 2; alias++)
					concurrent(1 - stage, a, b, alias != 0);
	close_during_publication();
	errors_and_lifetime();
	CHECK(pthread_mutex_destroy(&manager.vom_lock.real) == 0);
	CHECK(pthread_mutex_destroy(&device.struct_mutex.real) == 0);
	CHECK(pthread_cond_destroy(&changed) == 0);
	CHECK(pthread_mutex_destroy(&schedule) == 0);
	printf("PASS: %u actual native mmap offset checks\n", atomic_load(&checks));
	return 0;
}
