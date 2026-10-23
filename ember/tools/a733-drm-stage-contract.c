/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD, AI-assisted. Actual KMS sysctl wrapper checks. */
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { checks++; if (!(x)) { \
	fprintf(stderr, "%d: %s\n", __LINE__, #x); abort(); \
} } while (0)
#define container_of(p,t,m) ((t *)((char *)(p) - offsetof(t,m)))
#define SYSCTLFN_ARGS const struct sysctlnode *rnode, const void *newp
#define SYSCTLFN_CALL(node) node, newp

struct sysctlnode {
	void *sysctl_data;
	size_t sysctl_size;
};
struct a733_drm {
	bool lock;
	int stage, fault;
};
static bool tree_locked;
static struct a733_drm *active;
static unsigned starts, unlocks, relocks;
static int lookup_error, start_error, captured;

static void
mutex_enter(bool *lock)
{
	CHECK(!*lock);
	*lock = true;
}

static void
mutex_exit(bool *lock)
{
	CHECK(*lock);
	*lock = false;
}

static int
sysctl_lookup(SYSCTLFN_ARGS)
{
	CHECK(tree_locked && rnode->sysctl_size == sizeof(int));
	captured = *(int *)rnode->sysctl_data;
	if (lookup_error != 0)
		return lookup_error;
	if (newp != NULL)
		*(int *)rnode->sysctl_data = *(const int *)newp;
	return 0;
}

static void
sysctl_unlock(void)
{
	CHECK(tree_locked && !active->lock);
	tree_locked = false;
	unlocks++;
}

static void
sysctl_relock(void)
{
	CHECK(!tree_locked && !active->lock);
	tree_locked = true;
	relocks++;
}

/* Startup ownership and retry behavior have a separate actual-code contract. */
static int
a733_start(struct a733_drm *sc, bool *busy)
{
	CHECK(sc == active && busy == NULL && !tree_locked && !sc->lock);
	starts++;
	return start_error;
}

#include "stage.c"

static int
invoke(int (*handler)(SYSCTLFN_ARGS), SYSCTLFN_ARGS)
{
	int error;

	CHECK(!tree_locked);
	tree_locked = true;
	error = handler(rnode, newp);
	CHECK(tree_locked && !active->lock);
	tree_locked = false;
	return error;
}

int
main(void)
{
	struct a733_drm sc = {0};
	struct sysctlnode stage = {&sc.stage, sizeof(sc.stage)};
	struct sysctlnode fault = {&sc.fault, sizeof(sc.fault)};
	int one = 1, zero = 0;

	active = &sc;
	CHECK(invoke(a733_stage, &stage, NULL) == 0);
	CHECK(captured == 0 && starts == 0 && unlocks == 0 && relocks == 0);
	CHECK(invoke(a733_stage, &stage, &zero) == EINVAL);
	CHECK(sc.stage == 0 && starts == 0);
	lookup_error = EFAULT;
	CHECK(invoke(a733_stage, &stage, &one) == EFAULT);
	CHECK(starts == 0 && unlocks == 0 && relocks == 0);
	lookup_error = 0;
	CHECK(invoke(a733_stage, &stage, &one) == 0);
	CHECK(sc.stage == 0 && starts == 1 && unlocks == 1 && relocks == 1);
	start_error = EBUSY;
	CHECK(invoke(a733_stage, &stage, &one) == EBUSY);
	CHECK(sc.stage == 0 && starts == 2 && unlocks == 2 && relocks == 2);
	sc.stage = 1;
	CHECK(invoke(a733_stage, &stage, NULL) == 0 && captured == 1);
	CHECK(starts == 2 && unlocks == 2 && relocks == 2);
	sc.fault = EIO;
	CHECK(invoke(a733_fault, &fault, NULL) == 0 && captured == EIO);
	CHECK(starts == 2 && unlocks == 2 && relocks == 2);
	printf("PASS %u KMS stage sysctl wrapper checks\n", checks);
	return 0;
}
