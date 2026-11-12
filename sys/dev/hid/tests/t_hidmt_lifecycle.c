/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * -DLEGACY_API: only what exists in the pre-review sources (for the red
 * run against them, see run-hidmt.sh and README.hidmt.md).
 *
 * Review Dev Bots #472: reset on lifecycle events and mode changes, release
 * after a lost frame, gestures switched off, timer rule hidmt_stall().
 */
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dev/hid/hidmtvar.h>
#include "hybrid_descriptor.h"

size_t hidmt_test_allocations;
static unsigned npass;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", \
    __FILE__, __LINE__, #c); exit(1); } npass++; } while (0)

static void
hrep(uint8_t *r, unsigned id, int tip, int x, int y, unsigned scan,
    unsigned count)
{
	memset(r, 0, 9);
	r[0] = (tip ? 3 : 1) | (id << 2);
	r[1] = x & 255; r[2] = x >> 8;
	r[3] = y & 255; r[4] = y >> 8;
	r[5] = scan & 255; r[6] = scan >> 8;
	r[7] = count;
}

/* A frame of n contacts on one row, 300 units apart. */
static void
mkframe(struct hidmt_frame *f, unsigned n, int x0, int y)
{
	unsigned i;

	memset(f, 0, sizeof(*f));
	for (i = 0; i < n; i++) {
		f->contacts[i].id = i + 1;
		f->contacts[i].x = x0 + 300 * (int)i;
		f->contacts[i].y = y;
	}
	f->ncontacts = n;
}

static unsigned
gupdate(struct hidmt_gstate *g, bool enable, const struct hidmt_frame *f)
{
	struct hidmt_gconf gc = {.enable = enable, .start = 0};
	struct hidmt_motion m = {0};

	hidmt_gesture_update(g, &gc, f, &m);
	return m.gesture;
}

/* A started swipe, as produced by two frames 200 units apart. */
static void
start_swipe(struct hidmt_gstate *g)
{
	struct hidmt_frame f;

	memset(g, 0, sizeof(*g));
	mkframe(&f, 3, 1000, 1200);
	CHECK(gupdate(g, true, &f) == 0);
	mkframe(&f, 3, 1200, 1200);
	CHECK((gupdate(g, true, &f) & HIDMT_GESTURE_BEGIN) != 0);
	CHECK(g->started);
}

/* After a dropped hybrid frame a report without contacts is a release. */
static void
test_release_after_drop(void)
{
	struct hidmt_config cfg;
	struct hidmt_assembler as;
	struct hidmt_frame f;
	uint8_t r[9];
	uint64_t t = 1000000;

	CHECK(hidmt_parse(hybrid_descriptor, sizeof(hybrid_descriptor), &cfg)
	    == 0);
	/* Frame at scan 800 loses its tail; the finger then lifts at scan 805. */
	memset(&as, 0, sizeof(as));
	hrep(r, 1, 1, 1000, 800, 800, 2);
	CHECK(hidmt_assemble(&cfg, &as, r, 9, t, &f) == HIDMT_ASM_MORE);
	hrep(r, 0, 0, 0, 0, 805, 0);
	CHECK(hidmt_assemble(&cfg, &as, r, 9, t + 8000, &f) == HIDMT_ASM_READY);
	CHECK(f.ncontacts == 0 && as.drops == 1);
	/* The same after a timeout: the release is a different frame. */
	memset(&as, 0, sizeof(as));
	hrep(r, 1, 1, 1000, 800, 810, 2);
	CHECK(hidmt_assemble(&cfg, &as, r, 9, t, &f) == HIDMT_ASM_MORE);
	hrep(r, 0, 0, 0, 0, 815, 0);
	CHECK(hidmt_assemble(&cfg, &as, r, 9,
	    t + HIDMT_HYBRID_TIMEOUT_US + 1, &f) == HIDMT_ASM_READY);
	CHECK(f.ncontacts == 0);
	/* A tail that still has a contact down stays an ignored orphan. */
	memset(&as, 0, sizeof(as));
	hrep(r, 1, 1, 1000, 800, 820, 2);
	CHECK(hidmt_assemble(&cfg, &as, r, 9, t, &f) == HIDMT_ASM_MORE);
	hrep(r, 2, 1, 1200, 800, 821, 0);
	CHECK(hidmt_assemble(&cfg, &as, r, 9, t + 1000, &f) == HIDMT_ASM_MORE);
	CHECK(as.drops == 1);
#ifndef LEGACY_API
	CHECK(as.lost);
#endif
}

static void
test_gesture_off(void)
{
	struct hidmt_gstate g;
	struct hidmt_frame f;

	/* Started swipe: switching off cancels it and clears the state. */
	start_swipe(&g);
	mkframe(&f, 3, 1300, 1200);
	CHECK(gupdate(&g, false, &f) == HIDMT_GESTURE_CANCEL);
	CHECK(!g.started && !g.latched && g.fingers == 0);
	/* Candidate that has not started: forgotten, no event. */
	memset(&g, 0, sizeof(g));
	mkframe(&f, 3, 1000, 1200);
	CHECK(gupdate(&g, true, &f) == 0);
	CHECK(g.latched && g.fingers == 3);
	CHECK(gupdate(&g, false, &f) == 0);
	CHECK(!g.latched && g.fingers == 0 && g.startx == 0);
	/* Switched on again far away: a new candidate, not an instant BEGIN. */
	mkframe(&f, 3, 2000, 1500);
	CHECK(gupdate(&g, true, &f) == 0 && !g.started);
}

#ifndef LEGACY_API
static void
test_reset(void)
{
	struct hidmt_pointer p;
	struct hidmt_assembler as;
	struct hidmt_gstate g;

	memset(&p, 0, sizeof(p));
	memset(&as, 0, sizeof(as));
	start_swipe(&g);
	p.active = p.tap_candidate = true;
	as.pending = true;
	as.expected = 3;
	as.drops = 7;
	CHECK(hidmt_live(&p, &as, &g));
	CHECK(hidmt_reset(&p, &as, &g) == HIDMT_GESTURE_CANCEL);
	CHECK(!p.active && !p.tap_candidate && !as.pending && as.expected == 0);
	CHECK(as.drops == 7);			/* counter survives */
	CHECK(g.started == false && g.latched == false);
	CHECK(!hidmt_live(&p, &as, &g));
	/* Nothing started: no CANCEL. */
	memset(&g, 0, sizeof(g));
	g.latched = true;
	CHECK(hidmt_reset(&p, &as, &g) == 0 && !g.latched);
	/* hidmt_release() keeps the assembler (a new frame may be in it). */
	as.pending = true;
	CHECK(hidmt_release(&p, &g) == 0 && as.pending);
}

static void
test_stall(void)
{
	struct hidmt_pointer p;
	struct hidmt_assembler as;
	struct hidmt_gstate g;
	uint64_t t = 5000000;
	bool rel;

	/* Idle: nothing to do at any time. */
	memset(&p, 0, sizeof(p));
	memset(&as, 0, sizeof(as));
	memset(&g, 0, sizeof(g));
	CHECK(hidmt_stall(&p, &as, &g, t + 10 * HIDMT_STALL_US, t, &rel) == 0);
	CHECK(!rel);
	/* Unfinished hybrid frame: waits until the timeout, then everything
	 * is released and a swipe in progress is cancelled. */
	start_swipe(&g);
	p.active = true;
	as.pending = true;
	as.started = t;
	as.scantime = 40;
	CHECK(hidmt_stall(&p, &as, &g, t + HIDMT_HYBRID_TIMEOUT_US, t, &rel)
	    == 0 && !rel && as.pending);
	CHECK(hidmt_stall(&p, &as, &g, t + HIDMT_HYBRID_TIMEOUT_US + 1, t,
	    &rel) == HIDMT_GESTURE_CANCEL);
	CHECK(rel && !as.pending && !as.lost && !p.active && !g.started);
	CHECK(as.drops == 1 && as.have_dropped && as.dropped_scantime == 40);
	/* Silent contacts are released, without a tap. */
	memset(&as, 0, sizeof(as));
	start_swipe(&g);
	p.active = p.tap_candidate = true;
	CHECK(hidmt_stall(&p, &as, &g, t + HIDMT_STALL_US - 1, t, &rel) == 0);
	CHECK(!rel && g.started);
	CHECK(hidmt_stall(&p, &as, &g, t + HIDMT_STALL_US, t, &rel) ==
	    HIDMT_GESTURE_CANCEL);
	CHECK(rel && !p.active && !p.tap_candidate && !g.started);
	/* A candidate that never started is dropped quietly. */
	memset(&g, 0, sizeof(g));
	g.latched = true;
	CHECK(hidmt_stall(&p, &as, &g, t + HIDMT_STALL_US, t, &rel) == 0);
	CHECK(rel && !g.latched);
	/* A lost frame flagged earlier is released at the next check. */
	memset(&p, 0, sizeof(p));
	start_swipe(&g);
	as.lost = true;
	CHECK(hidmt_stall(&p, &as, &g, t, t, &rel) == HIDMT_GESTURE_CANCEL);
	CHECK(rel && !as.lost);
}

#endif /* LEGACY_API: the reset/stall API is new */

int
main(void)
{
	test_release_after_drop();
	test_gesture_off();
#ifndef LEGACY_API
	test_reset();
	test_stall();
#endif
	printf("hidmt lifecycle: %u checks passed, 0 failed\n", npass);
	return 0;
}
