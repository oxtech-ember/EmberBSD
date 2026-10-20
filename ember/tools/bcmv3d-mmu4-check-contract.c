/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual check caller. Controlled BO/native services; no hardware model. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "check-types.h"
#include "stats-types.h"
#ifndef CHECK_PAGE_SIZE
#define CHECK_PAGE_SIZE 4096
#endif
#define CHECK_BO_BYTES ((UINT64_C(8294400) + CHECK_PAGE_SIZE - 1) & ~(UINT64_C(1) * CHECK_PAGE_SIZE - 1))
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "CHECK %d: %s\n", __LINE__, #x); abort(); } checks++; } while (0)
static unsigned checks;
struct drm_gem_object { unsigned refs, slot; };
struct bcmv3d_bo { struct { struct drm_gem_object base; } cma; };
struct bcmv3d_device { int identity; };
struct bcmv3d_mmu4_native { int identity; };
static struct bcmv3d_device renderer;
static struct bcmv3d_mmu4_native native;
static struct bcmv3d_bo objects[6], *slots[3];
static bool live[3];
static unsigned allocated, released, staged, published, unmapped, discarded;
static unsigned failure, failure_at;
static unsigned drift;
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
#define PTR_ERR(p) ((intptr_t)(p))

struct bcmv3d_bo *
bcmv3d_bo_alloc(struct bcmv3d_device *sc, uint32_t size)
{
	CHECK(sc == &renderer && size == 8294400 && allocated < 6);
	if (failure == 1 && allocated + 1 == failure_at)
		return (void *)(intptr_t)-ENOMEM;
	objects[allocated].cma.base.refs = 1;
	return &objects[allocated++];
}

void
drm_gem_object_put_unlocked(struct drm_gem_object *obj)
{
	CHECK(obj->refs != 0);
	if (--obj->refs == 0)
		released++;
}

int
bcmv3d_mmu4_native_stage_gem(struct bcmv3d_mmu4_native *n, unsigned slot,
    struct drm_gem_object *obj)
{
	CHECK(n == &native && slot < 3 && slots[slot] == NULL && !live[slot]);
	CHECK(obj->refs == 1);
	if (failure == 2 && staged + 1 == failure_at)
		return EINVAL;
	obj->refs++;
	obj->slot = slot;
	slots[slot] = (struct bcmv3d_bo *)obj;
	staged++;
	return 0;
}

int
bcmv3d_mmu4_native_publish_staged_gem(struct bcmv3d_mmu4_native *n, unsigned slot)
{
	CHECK(n == &native && slot < 3 && slots[slot] != NULL && !live[slot]);
	CHECK(slots[slot]->cma.base.refs == 1);
	live[slot] = true; /* Exposure can precede an operation error. */
	if (failure == 3 && published + 1 == failure_at)
		return ETIMEDOUT;
	published++;
	return 0;
}

int
bcmv3d_mmu4_native_unmap_gem(struct bcmv3d_mmu4_native *n, unsigned slot)
{
	CHECK(n == &native && slot < 3 && slots[slot] != NULL && live[slot]);
	if (failure == 4 && unmapped + 1 == failure_at)
		return EIO;
	live[slot] = false;
	drm_gem_object_put_unlocked(&slots[slot]->cma.base);
	slots[slot] = NULL;
	unmapped++;
	return 0;
}

int
bcmv3d_mmu4_native_discard_staged_gem(struct bcmv3d_mmu4_native *n, unsigned slot)
{
	CHECK(n == &native && slot < 3 && slots[slot] != NULL);
	discarded++;
	if (live[slot])
		return EBUSY;
	drm_gem_object_put_unlocked(&slots[slot]->cma.base);
	slots[slot] = NULL;
	return 0;
}

void
bcmv3d_bo_snapshot(struct bcmv3d_device *sc, struct bcmv3d_bo_stats *s)
{
	CHECK(sc == &renderer);
	memset(s, 0, sizeof(*s));
	s->reserved_bytes = 123456 + (allocated - released) * CHECK_BO_BYTES;
	s->reservations = 11 + allocated;
	s->releases = 7 + released;
	if (allocated == 6 && released == 6) {
		if (drift == 1) s->reserved_bytes++;
		if (drift == 2) s->reservations++;
		if (drift == 3) s->releases++;
		if (drift == 4) s->failures++;
	}
}

#include "check-body.h"

static void
reset(void)
{
	memset(objects, 0, sizeof(objects));
	memset(slots, 0, sizeof(slots));
	memset(live, 0, sizeof(live));
	allocated = released = staged = published = unmapped = discarded = 0;
	failure = failure_at = 0;
	drift = false;
}

int
main(void)
{
	struct bcmv3d_mmu4_check_status status;
	unsigned kind, at, i;
	int error;

	reset();
	memset(&status, 0, sizeof(status));
	CHECK(bcmv3d_mmu4_check_ranges(NULL, &native, &status) == EINVAL);
	CHECK(bcmv3d_mmu4_check_ranges(&renderer, NULL, &status) == EINVAL);
	CHECK(bcmv3d_mmu4_check_ranges(&renderer, &native, NULL) == EINVAL);
	CHECK(allocated == 0 && !status.attempted);
	CHECK(bcmv3d_mmu4_check_ranges(&renderer, &native, &status) == 0);
	CHECK(status.complete && status.attempted && status.first_error == 0);
	CHECK(status.cycles == 2 && status.staged == 6 && status.published == 6 && status.unmapped == 6);
	CHECK(status.before_bytes == 123456 && status.after_bytes == status.before_bytes);
	CHECK(status.reservations == 6 && status.releases == 6 && allocated == released);
	CHECK(discarded == 0);
	CHECK(bcmv3d_mmu4_check_ranges(&renderer, &native, &status) == EALREADY);
	CHECK(allocated == 6);
	for (kind = 1; kind <= 4; kind++) {
		for (at = 1; at <= 6; at++) {
			reset();
			memset(&status, 0, sizeof(status));
			failure = kind;
			failure_at = at;
			error = bcmv3d_mmu4_check_ranges(&renderer, &native, &status);
			CHECK(error == (kind == 1 ? ENOMEM : kind == 2 ? EINVAL : kind == 3 ? ETIMEDOUT : EIO));
			CHECK(status.attempted && !status.complete && status.first_error == error);
			CHECK(status.staged == staged && status.published == published && status.unmapped == unmapped);
			CHECK(status.cycles == (at > 3 ? 1 : 0));
			CHECK((kind == 1 ? allocated : kind == 2 ? staged : kind == 3 ? published : unmapped) == at - 1);
			for (i = 0; i < 3; i++) {
				CHECK(slots[i] == NULL || live[i]);
				if (slots[i] != NULL)
					CHECK(slots[i]->cma.base.refs == 1);
			}
			if (kind < 3)
				CHECK(allocated == released);
			CHECK(bcmv3d_mmu4_check_ranges(&renderer, &native, &status) == EALREADY);
		}
	}
	for (i = 1; i <= 4; i++) {
		reset();
		drift = i;
		memset(&status, 0, sizeof(status));
		CHECK(bcmv3d_mmu4_check_ranges(&renderer, &native, &status) == EIO);
		CHECK(!status.complete && status.first_error == EIO && status.cycles == 2);
	}
	printf("PASS unused-range caller: %u checks, 24 fault positions, retained exposure\n", checks);
	return 0;
}
