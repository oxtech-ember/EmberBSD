/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD contributors (AI-assisted). */
/*
 * Actual rcu_replace_pointer() from the Linux shim. Like Linux, it must
 * store the new value in the RCU pointer and return the old value.
 * set_engines() in i915 passes the old engine set to call_rcu(). If the
 * store goes to a copy, the context keeps the old set and the callback
 * frees it. A later execbuffer then pins a destroyed intel_context.
 */
#include <stdio.h>
#include <stdlib.h>

#include <linux/rcupdate.h>

int warnings;
static unsigned checks;
#define CHECK(e) do { checks++; if (!(e)) { \
	fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #e); exit(1); \
} } while (0)

struct engines { int id; int freed; };
struct context { struct engines *engines; };

static struct engines *
next_engines(struct engines **p)
{
	return ++*p;
}

int
main(void)
{
	struct engines sets[3] = { { .id = 0 }, { .id = 1 }, { .id = 2 } };
	struct engines *set, *cursor = &sets[0];
	struct context ctx = { .engines = &sets[0] };

	/* set_engines(): replace under engines_mutex, free the old set. */
	set = &sets[1];
	set = rcu_replace_pointer(ctx.engines, set, 1);
	CHECK(set == &sets[0]);
	CHECK(rcu_access_pointer(ctx.engines) == &sets[1]);
	set->freed = 1;			/* call_rcu(free_engines_rcu) */
	CHECK(!rcu_dereference(ctx.engines)->freed);
	CHECK(rcu_dereference(ctx.engines)->id == 1);

	/* A second replacement returns the previous new set. */
	set = rcu_replace_pointer(ctx.engines, &sets[2], 1);
	CHECK(set == &sets[1] && ctx.engines == &sets[2]);
	CHECK(warnings == 0);

	/* The condition is checked; NEWPTR is evaluated once. */
	set = rcu_replace_pointer(ctx.engines, next_engines(&cursor), 0);
	CHECK(warnings == 1);
	CHECK(set == &sets[2] && ctx.engines == &sets[1]);
	CHECK(cursor == &sets[1]);

	printf("Linux rcu_replace_pointer contract: %u checks passed\n",
	    checks);
	return 0;
}
