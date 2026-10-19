/*
 * Copyright (c) 2026 EmberBSD contributors.
 * SPDX-License-Identifier: BSD-2-Clause
 * Origin: Test actual vnode iterator code across safe preemption points.
 */
#include <sys/queue.h>
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KASSERT assert
#define VDEAD_NOWAIT 1
#define MAX_NODES 10000

typedef int kmutex_t;
struct mount;
typedef struct vnode {
	struct mount *v_mount;
	kmutex_t *v_interlock;
	kmutex_t guard;
	unsigned int id, v_usecount;
	bool marker, dead, selected, reclaim;
} vnode_t;
typedef struct vnode_impl {
	vnode_t vnode;
	TAILQ_ENTRY(vnode_impl) vi_mntvnodes;
} vnode_impl_t;
TAILQ_HEAD(vnode_list, vnode_impl);
struct mount {
	kmutex_t *mnt_vnodelock;
	struct vnode_list mnt_vnodelist;
};
struct vnode_iterator { vnode_impl_t vi_vnode; };
#define VIMPL_TO_VNODE(vi) ((vnode_t *)(vi))
#define VNODE_TO_VIMPL(vp) ((vnode_impl_t *)(vp))

static struct mount mount_point;
static kmutex_t list_lock;
static vnode_impl_t *nodes[MAX_NODES];
static unsigned int locks, scanned, points, total_scanned, mutations;
static unsigned int seen[MAX_NODES], case_count;
static int mutation_mode, expected_dead_flags;
static bool nested;
void preempt_point(void);

static void
mutex_enter(kmutex_t *lock)
{
	assert(*lock == 0);
	*lock = 1;
	locks++;
}

static void
mutex_exit(kmutex_t *lock)
{
	assert(*lock == 1 && locks > 0);
	*lock = 0;
	locks--;
}

static vnode_impl_t *
allocate(unsigned int id, bool marker)
{
	vnode_impl_t *vi = calloc(1, sizeof(*vi));
	assert(vi != NULL);
	vi->vnode.v_mount = &mount_point;
	vi->vnode.v_interlock = &vi->vnode.guard;
	vi->vnode.id = id;
	vi->vnode.marker = marker;
	return vi;
}

static vnode_t *
vnalloc_marker(const struct mount *mp)
{
	assert(mp == &mount_point);
	return VIMPL_TO_VNODE(allocate(MAX_NODES, true));
}

static void
vnfree_marker(vnode_t *vp)
{
	assert(vp->marker && vp->v_usecount == 0 && vp->guard == 0);
	free(VNODE_TO_VIMPL(vp));
}

static bool vnis_marker(const vnode_t *vp) { return vp->marker; }
static unsigned int vrefcnt(const vnode_t *vp) { return vp->v_usecount; }

static bool
vdead_check(const vnode_t *vp, int flags)
{
	assert(vp->guard == 1 && list_lock == 1);
	assert(flags == expected_dead_flags);
	/* Fail on the original unbounded iterator, including dead-vnode scans. */
	scanned++;
	assert(scanned <= 128);
	total_scanned++;
	return vp->dead;
}

static void
remove_node(vnode_impl_t *vi)
{
	assert(vi != NULL && vi->vnode.v_usecount == 0);
	assert(vi->vnode.guard == 0 && !vi->vnode.marker);
	TAILQ_REMOVE(&mount_point.mnt_vnodelist, vi, vi_mntvnodes);
	nodes[vi->vnode.id] = NULL;
	free(vi);
}

static int
vcache_vget(vnode_t *vp)
{
	assert(list_lock == 0 && vp->guard == 1 && !vp->marker);
	mutex_exit(vp->v_interlock);
	if (vp->reclaim) {
		mutex_enter(&list_lock);
		remove_node(VNODE_TO_VIMPL(vp));
		mutex_exit(&list_lock);
		return ENOENT;
	}
	vp->v_usecount++;
	return 0;
}

#include "iterator.inc"

/* Signature matches the kernel selector callback. */
static bool
/* cppcheck-suppress constParameterCallback */
select_node(void *cookie, vnode_t *vp)
{
	(void)cookie;
	assert(vp->guard == 1 && list_lock == 1 && vp->id < MAX_NODES);
	if (!nested) {
		seen[vp->id]++;
		assert(seen[vp->id] == 1);
	}
	return vp->selected;
}

static void
release_node(vnode_t *vp)
{
	assert(vp->v_usecount == 1 && vp->guard == 0);
	vp->v_usecount--;
}

void
preempt_point(void)
{
	vnode_impl_t *vi, *marker = NULL;
	struct vnode_iterator *other;

	assert(locks == 0 && list_lock == 0);
	points++;
	scanned = 0;
	if (mutation_mode == 0 || mutations != 0 || total_scanned < 128)
		return;
	mutations++;
	TAILQ_FOREACH(vi, &mount_point.mnt_vnodelist, vi_mntvnodes) {
		/* Every list entry is calloc-initialized by allocate(). */
		/* cppcheck-suppress uninitvar */
		if (vi->vnode.marker) {
			assert(marker == NULL);
			marker = vi;
		}
	}
	assert(marker != NULL && marker->vnode.v_usecount == 1);
	if (mutation_mode == 3) {
		vnode_t *vp;

		/* A second real iterator must not mistake the paused marker for a vnode. */
		nested = true;
		vfs_vnode_iterator_init(&mount_point, &other);
		while ((vp = vfs_vnode_iterator_next(other, select_node, NULL)) != NULL)
			release_node(vp);
		vfs_vnode_iterator_destroy(other);
		nested = false;
		scanned = 0;
		return;
	}
	mutex_enter(&list_lock);
	assert(TAILQ_PREV(marker, vnode_list, vi_mntvnodes) == nodes[127]);
	remove_node(nodes[127]);
	if (mutation_mode == 1) {
		remove_node(nodes[128]);
		vi = nodes[9000] = allocate(9000, false);
		vi->vnode.selected = true;
		TAILQ_INSERT_AFTER(&mount_point.mnt_vnodelist, marker, vi, vi_mntvnodes);
	} else {
		while ((vi = TAILQ_NEXT(marker, vi_mntvnodes)) != NULL)
			remove_node(vi);
	}
	mutex_exit(&list_lock);
}

static void
setup(unsigned int count)
{
	assert(locks == 0);
	memset(nodes, 0, sizeof(nodes));
	memset(seen, 0, sizeof(seen));
	TAILQ_INIT(&mount_point.mnt_vnodelist);
	mount_point.mnt_vnodelock = &list_lock;
	scanned = points = total_scanned = mutations = 0;
	mutation_mode = 0;
	expected_dead_flags = VDEAD_NOWAIT;
	for (unsigned int i = 0; i < count; i++) {
		nodes[i] = allocate(i, false);
		TAILQ_INSERT_TAIL(&mount_point.mnt_vnodelist, nodes[i], vi_mntvnodes);
	}
}

static void
cleanup(void)
{
	vnode_impl_t *vi;
	assert(locks == 0 && list_lock == 0);
	while ((vi = TAILQ_FIRST(&mount_point.mnt_vnodelist)) != NULL)
		remove_node(vi);
	case_count++;
}

int
main(void)
{
	struct vnode_iterator *it, *other;
	vnode_t *vp;
	const unsigned int lengths[] = {0, 1, 127, 128, 129, 8192};

	for (unsigned int i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
		setup(lengths[i]);
		vfs_vnode_iterator_init(&mount_point, &it);
		assert(vfs_vnode_iterator_next(it, select_node, NULL) == NULL);
		assert(total_scanned == lengths[i]);
		if (lengths[i] > 128)
			assert(points > 1);
		vfs_vnode_iterator_destroy(it);
		cleanup();
	}
	setup(512);
	vfs_vnode_iterator_init(&mount_point, &other);
	vfs_vnode_iterator_init(&mount_point, &it);
	for (unsigned int i = 0; i < 512; i++) {
		vp = vfs_vnode_iterator_next(it, NULL, NULL);
		assert(vp != NULL && vp->id == i);
		release_node(vp);
	}
	assert(vfs_vnode_iterator_next(it, NULL, NULL) == NULL);
	vfs_vnode_iterator_destroy(it);
	vfs_vnode_iterator_destroy(other);
	cleanup();

	setup(512);
	expected_dead_flags = 0;
	for (unsigned int i = 0; i < 511; i++)
		nodes[i]->vnode.dead = true;
	nodes[511]->vnode.selected = true;
	vfs_vnode_iterator_init(&mount_point, &it);
	vp = vfs_vnode_iterator_next1(it, select_node, NULL, true);
	assert(vp != NULL && vp->id == 511);
	release_node(vp);
	vfs_vnode_iterator_destroy(it); /* Also tests destruction before end-of-list. */
	cleanup();

	setup(512);
	nodes[128]->vnode.selected = nodes[128]->vnode.reclaim = true;
	nodes[511]->vnode.selected = true;
	vfs_vnode_iterator_init(&mount_point, &it);
	vp = vfs_vnode_iterator_next(it, select_node, NULL);
	assert(vp != NULL && vp->id == 511 && nodes[128] == NULL);
	release_node(vp);
	vfs_vnode_iterator_destroy(it);
	cleanup();

	for (int mode = 1; mode <= 3; mode++) {
		setup(512);
		mutation_mode = mode;
		nodes[511]->vnode.selected = true;
		vfs_vnode_iterator_init(&mount_point, &it);
		vp = vfs_vnode_iterator_next(it, select_node, NULL);
		assert(mutations == 1);
		if (mode == 1) {
			assert(vp != NULL && vp->id == 9000);
			release_node(vp);
			vp = vfs_vnode_iterator_next(it, select_node, NULL);
		}
		if (mode == 2) {
			assert(vp == NULL);
		} else {
			assert(vp != NULL && vp->id == 511);
			release_node(vp);
			assert(vfs_vnode_iterator_next(it, select_node, NULL) == NULL);
		}
		vfs_vnode_iterator_destroy(it);
		cleanup();
	}
	printf("PASS: %u vnode iterator cases, bounded scans and safe list mutation\n", case_count);
	return 0;
}
