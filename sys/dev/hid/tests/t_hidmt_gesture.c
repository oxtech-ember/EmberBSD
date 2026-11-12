/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Three- and four-finger gesture tests on the descriptor of the real ELAN
 * 04f3:31fd touchpad (one contact per report, hybrid frames are required).
 */
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dev/hid/hidmtvar.h>
#include "elan_descriptor.h"

size_t hidmt_test_allocations;
static unsigned npass;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", \
    __FILE__, __LINE__, #c); exit(1); } npass++; } while (0)

static const unsigned both = HIDMT_TAP_LEFT | HIDMT_TAP_RIGHT;

struct rig {
	struct hidmt_config cfg;
	struct hidmt_assembler as;
	struct hidmt_pointer p;
	struct hidmt_gstate g;
	struct hidmt_scroll sc;
	struct hidmt_gconf gc;
	uint64_t t;
	unsigned scan;
	/* Accumulated over the frames since the last rig_clear(). */
	unsigned nbegin, nupdate, nend, ncancel, fingers;
	int gdx, gdy, pdx, pdy, sx, sy, taps;
	unsigned order[64], norder;
};

/* ELAN report: byte 0 conf|tip|id<<4, X, Y, Scan Time, Count, button. */
static void
erep(uint8_t *r, unsigned id, int tip, int x, int y, unsigned scan,
    unsigned count)
{
	memset(r, 0, 11);
	r[0] = (tip ? 3 : 1) | (id << 4);
	r[1] = x & 255; r[2] = x >> 8;
	r[3] = y & 255; r[4] = y >> 8;
	r[5] = scan & 255; r[6] = scan >> 8;
	r[7] = count;
}

static void
rig_init(struct rig *r, bool gestures)
{
	memset(r, 0, sizeof(*r));
	CHECK(hidmt_parse(elan_descriptor, sizeof(elan_descriptor), &r->cfg)
	    == 0);
	CHECK(r->cfg.nslots == 1 && r->cfg.input_size == 11);
	r->sc.enable = true;
	r->sc.scale = 112;
	r->sc.range = r->cfg.maxy - r->cfg.miny;
	r->gc.enable = gestures;
	r->gc.start = HIDMT_GESTURE_START;
	r->t = 9000000;
	r->scan = 1;
}

static void
rig_clear(struct rig *r)
{
	r->nbegin = r->nupdate = r->nend = r->ncancel = r->fingers = 0;
	r->gdx = r->gdy = r->pdx = r->pdy = r->sx = r->sy = r->taps = 0;
	r->norder = 0;
}

/* One frame with n contacts (tip[i] == 0: finger lifted, still reported). */
static struct hidmt_motion
frame(struct rig *r, int n, const int *xy, const int *tip)
{
	struct hidmt_frame f;
	struct hidmt_motion m;
	uint8_t b[11];
	int i, res = HIDMT_ASM_MORE;

	r->t += 8000;
	r->scan++;
	for (i = 0; i < n; i++) {
		erep(b, i + 1, tip ? tip[i] : 1, xy[2 * i], xy[2 * i + 1],
		    r->scan, i == 0 ? n : 0);
		res = hidmt_assemble(&r->cfg, &r->as, b, 11, r->t, &f);
	}
	CHECK(res == HIDMT_ASM_READY);
	hidmt_pointer_update_scroll(&r->p, &f, r->t, both, &r->sc, &m);
	hidmt_gesture_update(&r->g, &r->gc, &f, &m);
	r->pdx += m.dx; r->pdy += m.dy;
	r->sx += m.scroll_x; r->sy += m.scroll_y;
	r->taps += m.tap;
	if (m.gesture & HIDMT_GESTURE_CANCEL) {
		r->ncancel++; r->order[r->norder++] = HIDMT_GESTURE_CANCEL;
	}
	if (m.gesture & HIDMT_GESTURE_END) {
		r->nend++; r->order[r->norder++] = HIDMT_GESTURE_END;
	}
	if (m.gesture & HIDMT_GESTURE_BEGIN) {
		r->nbegin++; r->fingers = m.gesture_fingers;
		r->order[r->norder++] = HIDMT_GESTURE_BEGIN;
	}
	if (m.gesture & HIDMT_GESTURE_UPDATE) {
		r->nupdate++; r->gdx += m.gesture_dx; r->gdy += m.gesture_dy;
		r->order[r->norder++] = HIDMT_GESTURE_UPDATE;
	}
	return m;
}

/* Lift all fingers: a frame with the given number of tip-0 reports. */
static void
lift(struct rig *r, int n, const int *xy)
{
	int tip[4] = {0, 0, 0, 0};

	(void)frame(r, n, xy, tip);
}

/* Place n fingers 300 units apart at the row y, then move them. */
static void
place(int n, int x0, int y0, int *xy)
{
	int i;

	for (i = 0; i < n; i++) {
		xy[2 * i] = x0 + 300 * i;
		xy[2 * i + 1] = y0;
	}
}

static void
move(int n, int dx, int dy, int *xy)
{
	int i;

	for (i = 0; i < n; i++) {
		xy[2 * i] += dx;
		xy[2 * i + 1] += dy;
	}
}

static void
swipe(struct rig *r, int n, int dx, int dy, int steps, int *xy)
{
	int k;

	for (k = 0; k < steps; k++) {
		move(n, dx, dy, xy);
		(void)frame(r, n, xy, NULL);
	}
}

static void
test_swipe_right(void)
{
	struct rig r;
	int xy[8];

	rig_init(&r, true);
	place(3, 1000, 1200, xy);
	(void)frame(&r, 3, xy, NULL);
	CHECK(r.gc.start == HIDMT_GESTURE_START && r.nbegin == 0);
	swipe(&r, 3, 20, 0, 20, xy);	/* 400 units in total */
	CHECK(r.nbegin == 1 && r.fingers == 3 && r.nupdate >= 10);
	CHECK(r.nend == 0 && r.ncancel == 0);
	CHECK(r.order[0] == HIDMT_GESTURE_BEGIN &&
	    r.order[1] == HIDMT_GESTURE_UPDATE);
	/* Pointer units are 1/8 contact unit; the start distance is included. */
	CHECK(r.gdx >= 49 && r.gdx <= 50 && r.gdy == 0);
	CHECK(r.pdx == 0 && r.pdy == 0 && r.sx == 0 && r.sy == 0 &&
	    r.taps == 0);
	lift(&r, 3, xy);
	CHECK(r.nend == 1 && r.ncancel == 0 && r.nbegin == 1);
	CHECK(r.order[r.norder - 1] == HIDMT_GESTURE_END);
	CHECK(r.taps == 0 && r.pdx == 0 && r.pdy == 0);
}

static void
test_swipe_up_and_left(void)
{
	struct rig r;
	int xy[8];

	rig_init(&r, true);
	place(3, 1000, 1800, xy);
	(void)frame(&r, 3, xy, NULL);
	swipe(&r, 3, 0, -20, 20, xy);	/* fingers up, Y shrinks */
	CHECK(r.nbegin == 1 && r.fingers == 3);
	CHECK(r.gdy >= 49 && r.gdy <= 50 && r.gdx == 0);	/* up > 0 */
	CHECK(r.pdx == 0 && r.pdy == 0 && r.sy == 0);
	lift(&r, 3, xy);
	CHECK(r.nend == 1);
	rig_clear(&r);
	place(3, 2000, 1200, xy);
	(void)frame(&r, 3, xy, NULL);
	swipe(&r, 3, -20, 0, 20, xy);
	CHECK(r.gdx <= -49 && r.gdx >= -50 && r.gdy == 0);
	lift(&r, 3, xy);
	CHECK(r.nbegin == 1 && r.nend == 1);
}

static void
test_four_fingers(void)
{
	struct rig r;
	int xy[8];

	rig_init(&r, true);
	place(4, 500, 1200, xy);
	(void)frame(&r, 4, xy, NULL);
	swipe(&r, 4, -20, 0, 20, xy);
	CHECK(r.nbegin == 1 && r.fingers == 4 && r.gdx < -40);
	CHECK(r.pdx == 0 && r.pdy == 0);
	lift(&r, 4, xy);
	CHECK(r.nend == 1 && r.ncancel == 0);
}

static void
test_count_change(void)
{
	struct rig r;
	int xy[8];

	/* 3 -> 4 fingers in the middle: cancel, then a new gesture. */
	rig_init(&r, true);
	place(3, 500, 1200, xy);
	(void)frame(&r, 3, xy, NULL);
	swipe(&r, 3, 20, 0, 10, xy);
	CHECK(r.nbegin == 1 && r.fingers == 3 && r.ncancel == 0);
	place(4, 500, 1200, xy);
	xy[0] += 200; xy[2] += 200; xy[4] += 200;
	(void)frame(&r, 4, xy, NULL);
	CHECK(r.ncancel == 1 && r.nend == 0);
	swipe(&r, 4, 20, 0, 20, xy);
	CHECK(r.nbegin == 2 && r.fingers == 4);
	lift(&r, 4, xy);
	CHECK(r.nend == 1 && r.ncancel == 1);

	/* 3 -> 2 -> 1 -> 0 while lifting: one end, no pointer or scroll. */
	rig_init(&r, true);
	place(3, 1000, 1200, xy);
	(void)frame(&r, 3, xy, NULL);
	swipe(&r, 3, 20, 0, 10, xy);
	CHECK(r.nbegin == 1);
	rig_clear(&r);
	swipe(&r, 2, 60, 0, 3, xy);
	swipe(&r, 1, 60, 0, 3, xy);
	CHECK(r.nend == 1 && r.ncancel == 0 && r.nbegin == 0);
	CHECK(r.pdx == 0 && r.pdy == 0 && r.sx == 0 && r.sy == 0);
	lift(&r, 1, xy);
	CHECK(r.taps == 0 && r.nend == 1);
	/* The next touch is a normal pointer again. */
	rig_clear(&r);
	place(1, 1000, 1000, xy);
	(void)frame(&r, 1, xy, NULL);
	swipe(&r, 1, 40, 0, 3, xy);
	CHECK(r.pdx == 15 && r.nbegin == 0 && r.nupdate == 0);
}

static void
test_below_threshold(void)
{
	struct rig r;
	int xy[8];

	rig_init(&r, true);
	place(3, 1000, 1200, xy);
	(void)frame(&r, 3, xy, NULL);
	swipe(&r, 3, 10, 0, 8, xy);	/* 80 < HIDMT_GESTURE_START */
	CHECK(80 < HIDMT_GESTURE_START);
	CHECK(r.nbegin == 0 && r.nupdate == 0 && r.nend == 0 &&
	    r.ncancel == 0);
	CHECK(r.pdx == 0 && r.pdy == 0 && r.sx == 0 && r.sy == 0);
	lift(&r, 3, xy);
	CHECK(r.nbegin == 0 && r.nend == 0 && r.ncancel == 0 && r.taps == 0);
}

/* Same frames through the scroll policy alone and with gestures enabled. */
static void
test_one_two_fingers_unchanged(void)
{
	struct rig a, b;
	struct hidmt_motion ma, mb;
	int xy[8], k, y;

	rig_init(&a, true);
	rig_init(&b, false);
	place(1, 1000, 800, xy);
	/* One finger: motion, then tap-like release. */
	for (k = 0; k < 10; k++) {
		xy[0] += 24;
		ma = frame(&a, 1, xy, NULL);
		mb = frame(&b, 1, xy, NULL);
		CHECK(ma.dx == mb.dx && ma.dy == mb.dy && ma.gesture == 0);
	}
	lift(&a, 1, xy);
	lift(&b, 1, xy);
	CHECK(a.taps == b.taps && a.nbegin == 0);
	/* Two fingers scrolling up over the pad. */
	place(2, 1000, 2150, xy);
	(void)frame(&a, 2, xy, NULL);
	(void)frame(&b, 2, xy, NULL);
	for (y = 2150; y > 20; y -= 8) {
		xy[1] = xy[3] = y;
		ma = frame(&a, 2, xy, NULL);
		mb = frame(&b, 2, xy, NULL);
		CHECK(ma.dx == mb.dx && ma.dy == mb.dy &&
		    ma.scroll_x == mb.scroll_x && ma.scroll_y == mb.scroll_y &&
		    ma.tap == mb.tap && ma.buttons == mb.buttons &&
		    ma.gesture == 0);
	}
	CHECK(a.sy == b.sy && a.sy > 80);
	CHECK(a.nbegin == 0 && a.nupdate == 0);
	lift(&a, 2, xy);
	lift(&b, 2, xy);
	CHECK(a.taps == b.taps);
}

/* gestures=0: three fingers behave exactly as without the new code. */
static void
test_disabled(void)
{
	struct rig a, b;
	struct hidmt_motion ma;
	int xy[8], k;

	rig_init(&a, false);
	rig_init(&b, false);
	b.gc.enable = false;
	place(3, 1000, 1200, xy);
	for (k = 0; k < 30; k++) {
		xy[0] += 20; xy[2] += 20; xy[4] += 20;
		ma = frame(&a, 3, xy, NULL);
		CHECK(ma.gesture == 0 && ma.gesture_fingers == 0 &&
		    ma.gesture_dx == 0 && ma.gesture_dy == 0);
	}
	CHECK(a.nbegin == 0 && a.nupdate == 0 && a.nend == 0 &&
	    a.ncancel == 0);
	/* The pointer follows the first contact as before (no suppression). */
	CHECK(a.pdx > 0);
	lift(&a, 3, xy);
	CHECK(a.nend == 0);
}

int
main(void)
{
	test_swipe_right();
	test_swipe_up_and_left();
	test_four_fingers();
	test_count_change();
	test_below_threshold();
	test_one_two_fingers_unchanged();
	test_disabled();
	printf("hidmt gesture: %u checks passed, 0 failed\n", npass);
	return 0;
}
