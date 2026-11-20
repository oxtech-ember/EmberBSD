/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD three-finger drag contract (HIDMT_DRAG3). */
#include <sys/types.h>
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <dev/hid/hidmtvar.h>

size_t hidmt_test_allocations;

#define D3	(HIDMT_TAP_LEFT | HIDMT_TAP_RIGHT | HIDMT_DRAG3)

static struct hidmt_frame frame;

/* n contacts with ids 1..n, 300 units apart, the first one at (x, y). */
static void
fingers(unsigned n, int x, int y)
{
	unsigned i;

	memset(&frame, 0, sizeof(frame));
	frame.ncontacts = n;
	for (i = 0; i < n; i++) {
		frame.contacts[i].id = i + 1;
		frame.contacts[i].x = x + 300 * (int)i;
		frame.contacts[i].y = y;
	}
}

static void
update(struct hidmt_pointer *ptr, uint64_t now, unsigned flags,
    struct hidmt_motion *m)
{
	hidmt_pointer_update(ptr, &frame, now, flags, m);
}

/* Arm and start a drag at t; returns with the button down. */
static void
start(struct hidmt_pointer *ptr, uint64_t t, unsigned flags,
    struct hidmt_motion *m)
{
	fingers(3, 1000, 800);
	update(ptr, t, flags, m);
	assert(m->buttons == 0 && m->dx == 0 && m->dy == 0 && m->drag3);
	assert(ptr->drag3_armed && !ptr->drag3 && !hidmt_holds(ptr));
	fingers(3, 1000 + HIDMT_DRAG3_START, 800);
	update(ptr, t + HIDMT_DRAG3_SETTLE_US, flags, m);
	assert(m->buttons == 1 && m->dx == 0 && m->dy == 0 && m->drag3);
	assert(ptr->drag3 && hidmt_holds(ptr));
}

int
main(void)
{
	struct hidmt_pointer ptr = {0};
	struct hidmt_motion m;
	struct hidmt_assembler as = {0};
	struct hidmt_gstate gs = {0};
	struct hidmt_gconf gc = { .enable = true, .start = 0 };
	struct hidmt_scroll sc = { .enable = true, .natural = false,
	    .scale = 1000, .range = 1000 };

	/* Without HIDMT_DRAG3 three moving contacts only move the pointer. */
	fingers(3, 1000, 800);
	update(&ptr, 1000000, HIDMT_TAP_LEFT, &m);
	fingers(3, 1400, 800);
	update(&ptr, 1010000, HIDMT_TAP_LEFT, &m);
	assert(m.buttons == 0 && m.dx == 50 && !m.drag3 && !ptr.drag3);
	fingers(0, 0, 0);
	update(&ptr, 1020000, HIDMT_TAP_LEFT, &m);
	assert(m.buttons == 0 && !m.tap);

	/* Armed: the pointer stays still below the start distance. */
	fingers(3, 1000, 800);
	update(&ptr, 2000000, D3, &m);
	assert(m.buttons == 0 && m.drag3 && ptr.drag3_armed);
	fingers(3, 1000 + HIDMT_DRAG3_START - 1, 800);
	update(&ptr, 2000000 + HIDMT_DRAG3_SETTLE_US, D3, &m);
	assert(m.buttons == 0 && m.dx == 0 && ptr.drag3_armed && !ptr.drag3);
	/* Enough travel before the settle time does not press yet. */
	memset(&ptr, 0, sizeof(ptr));
	fingers(3, 1000, 800);
	update(&ptr, 2100000, D3, &m);
	fingers(3, 1000 + 2 * HIDMT_DRAG3_START, 800);
	update(&ptr, 2100000 + HIDMT_DRAG3_SETTLE_US - 1, D3, &m);
	assert(m.buttons == 0 && m.dx == 0 && ptr.drag3_armed);
	/* The start frame presses the button without motion. */
	fingers(3, 1000 + 2 * HIDMT_DRAG3_START, 800);
	update(&ptr, 2100000 + HIDMT_DRAG3_SETTLE_US, D3, &m);
	assert(m.buttons == 1 && m.dx == 0 && ptr.drag3);
	assert(hidmt_live(&ptr, &as, &gs));
	/* Then the tracked contact moves the pointer with the button down. */
	fingers(3, 1000 + 2 * HIDMT_DRAG3_START + 80, 840);
	update(&ptr, 2200000, D3, &m);
	assert(m.buttons == 1 && m.dx == 10 && m.dy == -5 && !m.tap);
	/* Fingers lift one by one: the button stays, the pointer stops. */
	fingers(2, 1300, 840);
	update(&ptr, 2210000, D3, &m);
	assert(m.buttons == 1 && m.dx == 0 && m.dy == 0 && ptr.drag3);
	fingers(1, 1500, 900);
	update(&ptr, 2220000, D3, &m);
	assert(m.buttons == 1 && m.dx == 0 && m.dy == 0 && ptr.drag3);
	/* A returning finger does not move the pointer either. */
	fingers(2, 1600, 900);
	update(&ptr, 2225000, D3, &m);
	assert(m.buttons == 1 && m.dx == 0 && m.dy == 0 && ptr.drag3);
	/* The last lift releases; no tap, no extra click. */
	fingers(0, 0, 0);
	update(&ptr, 2230000, D3, &m);
	assert(m.buttons == 0 && !m.tap && !m.click_after && !ptr.drag3);
	assert(!hidmt_holds(&ptr) && !ptr.drag3_armed && !ptr.drag3_blocked);
	update(&ptr, 2240000, D3, &m);
	assert(m.buttons == 0 && !m.tap);

	/* A still three-finger touch is neither a tap nor a drag. */
	fingers(3, 1000, 800);
	update(&ptr, 3000000, D3, &m);
	fingers(0, 0, 0);
	update(&ptr, 3050000, D3, &m);
	assert(m.buttons == 0 && !m.tap && !ptr.drag3 && !ptr.drag3_armed);

	/* A physical click with three contacts stays middle (clickfinger). */
	fingers(3, 1000, 800);
	frame.buttons = 1;
	update(&ptr, 4000000, D3, &m);
	fingers(3, 1200, 800);
	frame.buttons = 1;
	update(&ptr, 4010000, D3, &m);
	assert(m.buttons == 2 && !ptr.drag3 && !ptr.drag3_armed);
	fingers(0, 0, 0);
	update(&ptr, 4020000, D3, &m);
	assert(m.buttons == 0);

	/* During a drag a physical click is the same left button. */
	start(&ptr, 5000000, D3, &m);
	fingers(3, 1100, 800);
	frame.buttons = 1;
	update(&ptr, 5100000, D3, &m);
	assert(m.buttons == 1 && ptr.drag3);
	/* The physical button held over the last lift stays down. */
	fingers(0, 0, 0);
	frame.buttons = 1;
	update(&ptr, 5200000, D3, &m);
	assert(m.buttons == 1 && !ptr.drag3);
	fingers(0, 0, 0);
	update(&ptr, 5300000, D3, &m);
	assert(m.buttons == 0);

	/* A fourth finger before the start disarms: no button. */
	fingers(3, 1000, 800);
	update(&ptr, 6000000, D3, &m);
	assert(ptr.drag3_armed);
	fingers(4, 1000, 800);
	update(&ptr, 6010000, D3, &m);
	assert(!ptr.drag3_armed && !m.drag3);
	fingers(4, 1600, 800);
	update(&ptr, 6020000, D3, &m);
	assert(m.buttons == 0 && !ptr.drag3);
	fingers(0, 0, 0);
	update(&ptr, 6030000, D3, &m);

	/* A fourth finger during a drag keeps the drag. */
	start(&ptr, 6500000, D3, &m);
	fingers(4, 1100, 800);
	update(&ptr, 6510000, D3, &m);
	assert(m.buttons == 1 && ptr.drag3 && m.drag3);
	fingers(0, 0, 0);
	update(&ptr, 6520000, D3, &m);
	assert(m.buttons == 0 && !ptr.drag3);

	/* Gestures: three fingers belong to the drag, four still swipe. */
	memset(&ptr, 0, sizeof(ptr));
	memset(&gs, 0, sizeof(gs));
	fingers(3, 1000, 800);
	update(&ptr, 7000000, D3, &m);
	hidmt_gesture_update(&gs, &gc, &frame, &m);
	assert(m.gesture == 0);
	fingers(3, 1600, 800);
	update(&ptr, 7000000 + HIDMT_DRAG3_SETTLE_US, D3, &m);
	hidmt_gesture_update(&gs, &gc, &frame, &m);
	assert(m.gesture == 0 && m.buttons == 1 && ptr.drag3);
	fingers(3, 2000, 800);
	update(&ptr, 7090000, D3, &m);
	hidmt_gesture_update(&gs, &gc, &frame, &m);
	assert(m.gesture == 0 && m.dx == 50 && !gs.latched);
	fingers(0, 0, 0);
	update(&ptr, 7095000, D3, &m);
	hidmt_gesture_update(&gs, &gc, &frame, &m);
	assert(m.gesture == 0 && m.buttons == 0);
	fingers(4, 1000, 800);
	update(&ptr, 7100000, D3, &m);
	hidmt_gesture_update(&gs, &gc, &frame, &m);
	fingers(4, 1000 + 2 * HIDMT_GESTURE_START, 800);
	update(&ptr, 7110000, D3, &m);
	hidmt_gesture_update(&gs, &gc, &frame, &m);
	assert((m.gesture & HIDMT_GESTURE_BEGIN) != 0 &&
	    m.gesture_fingers == 4 && m.buttons == 0);
	fingers(0, 0, 0);
	update(&ptr, 7120000, D3, &m);
	hidmt_gesture_update(&gs, &gc, &frame, &m);
	assert((m.gesture & HIDMT_GESTURE_END) != 0);

	/* Scrolling: two remaining fingers of a drag never scroll. */
	memset(&ptr, 0, sizeof(ptr));
	fingers(3, 1000, 800);
	hidmt_pointer_update_scroll(&ptr, &frame, 8000000, D3, &sc, &m);
	fingers(3, 1100, 800);
	hidmt_pointer_update_scroll(&ptr, &frame, 8000000 + HIDMT_DRAG3_SETTLE_US,
	    D3, &sc, &m);
	assert(m.buttons == 1 && ptr.drag3);
	fingers(2, 1100, 800);
	hidmt_pointer_update_scroll(&ptr, &frame, 8090000, D3, &sc, &m);
	fingers(2, 1100, 1300);
	hidmt_pointer_update_scroll(&ptr, &frame, 8095000, D3, &sc, &m);
	assert(m.buttons == 1 && m.scroll_x == 0 && m.scroll_y == 0 &&
	    m.dx == 0 && m.dy == 0 && !ptr.scrolling);
	fingers(0, 0, 0);
	hidmt_pointer_update_scroll(&ptr, &frame, 8099000, D3, &sc, &m);
	assert(m.buttons == 0);

	/* A release (lifecycle, lost frame, stall) drops the drag. */
	start(&ptr, 9000000, D3, &m);
	(void)hidmt_release(&ptr, &gs);
	assert(!ptr.drag3 && !hidmt_holds(&ptr));
	fingers(3, 1300, 800);
	update(&ptr, 9100000, D3, &m);
	assert(m.buttons == 0 && ptr.drag3_armed && !ptr.drag3);
	fingers(0, 0, 0);
	update(&ptr, 9110000, D3, &m);
	assert(m.buttons == 0 && !ptr.drag3_armed);

	/* With tap and drag: a held tap ends when three fingers come. */
	fingers(1, 1000, 800);
	update(&ptr, 10000000, D3 | HIDMT_TAP_DRAG, &m);
	fingers(0, 0, 0);
	update(&ptr, 10100000, D3 | HIDMT_TAP_DRAG, &m);
	assert(m.buttons == 1 && ptr.drag_held);
	fingers(3, 1000, 800);
	update(&ptr, 10150000, D3 | HIDMT_TAP_DRAG, &m);
	assert(m.buttons == 0 && !ptr.drag_held && ptr.drag3_armed);
	fingers(3, 1100, 800);
	update(&ptr, 10150000 + HIDMT_DRAG3_SETTLE_US, D3 | HIDMT_TAP_DRAG, &m);
	assert(m.buttons == 1 && ptr.drag3 && !ptr.dragging);
	fingers(0, 0, 0);
	update(&ptr, 10300000, D3 | HIDMT_TAP_DRAG, &m);
	assert(m.buttons == 0 && !m.click_after && !hidmt_holds(&ptr));

	/* Review A1: a four-finger swipe that loses a finger stays a swipe. */
	memset(&ptr, 0, sizeof(ptr));
	memset(&gs, 0, sizeof(gs));
	fingers(4, 1000, 800);
	update(&ptr, 11000000, D3, &m);
	hidmt_gesture_update(&gs, &gc, &frame, &m);
	fingers(4, 1000 + 2 * HIDMT_GESTURE_START, 800);
	update(&ptr, 11010000, D3, &m);
	hidmt_gesture_update(&gs, &gc, &frame, &m);
	assert((m.gesture & HIDMT_GESTURE_BEGIN) != 0);
	fingers(3, 1300, 800);
	update(&ptr, 11020000, D3, &m);
	hidmt_gesture_update(&gs, &gc, &frame, &m);
	assert(!m.drag3 && ptr.drag3_blocked && !ptr.drag3_armed);
	fingers(3, 1800, 800);
	update(&ptr, 11200000, D3, &m);
	hidmt_gesture_update(&gs, &gc, &frame, &m);
	assert(m.buttons == 0 && !ptr.drag3 && m.dx == 0);
	fingers(0, 0, 0);
	update(&ptr, 11210000, D3, &m);
	hidmt_gesture_update(&gs, &gc, &frame, &m);
	assert(m.buttons == 0 && !ptr.drag3_blocked);

	/* Review A2: staggered landing, the fourth finger comes in time. */
	memset(&ptr, 0, sizeof(ptr));
	memset(&gs, 0, sizeof(gs));
	fingers(1, 1000, 800);
	update(&ptr, 12000000, D3, &m);
	fingers(2, 1020, 800);
	update(&ptr, 12010000, D3, &m);
	fingers(3, 1040, 800);
	update(&ptr, 12020000, D3, &m);
	assert(ptr.drag3_armed);
	fingers(3, 1040 + 2 * HIDMT_DRAG3_START, 800);
	update(&ptr, 12040000, D3, &m);
	assert(m.buttons == 0 && !ptr.drag3);
	fingers(4, 1200, 800);
	update(&ptr, 12060000, D3, &m);
	hidmt_gesture_update(&gs, &gc, &frame, &m);
	assert(m.buttons == 0 && !ptr.drag3 && ptr.drag3_blocked);
	fingers(0, 0, 0);
	update(&ptr, 12100000, D3, &m);
	hidmt_gesture_update(&gs, &gc, &frame, &m);

	/* Review A3: a third contact during a two-finger scroll is blocked. */
	memset(&ptr, 0, sizeof(ptr));
	fingers(2, 1000, 800);
	hidmt_pointer_update_scroll(&ptr, &frame, 13000000, D3, &sc, &m);
	fingers(2, 1000, 1000);
	hidmt_pointer_update_scroll(&ptr, &frame, 13050000, D3, &sc, &m);
	assert(ptr.scrolling);
	fingers(3, 1000, 1000);
	hidmt_pointer_update_scroll(&ptr, &frame, 13100000, D3, &sc, &m);
	fingers(3, 1300, 1000);
	hidmt_pointer_update_scroll(&ptr, &frame, 13300000, D3, &sc, &m);
	assert(m.buttons == 0 && !ptr.drag3 && ptr.drag3_blocked);
	fingers(0, 0, 0);
	hidmt_pointer_update_scroll(&ptr, &frame, 13400000, D3, &sc, &m);

	/* A third contact that lands late never arms. */
	memset(&ptr, 0, sizeof(ptr));
	fingers(2, 1000, 800);
	update(&ptr, 14000000, D3, &m);
	fingers(3, 1000, 800);
	update(&ptr, 14000000 + HIDMT_DRAG3_LAND_US + 1, D3, &m);
	assert(!ptr.drag3_armed && ptr.drag3_blocked);
	fingers(0, 0, 0);
	update(&ptr, 14500000, D3, &m);

	/* Review A4: a new set of three contacts re-arms in place. */
	memset(&ptr, 0, sizeof(ptr));
	fingers(3, 1000, 800);
	update(&ptr, 15000000, D3, &m);
	fingers(3, 1000, 800);
	frame.contacts[2].id = 9;
	frame.contacts[2].x = 1600;
	update(&ptr, 15100000, D3, &m);
	assert(m.buttons == 0 && ptr.drag3_armed &&
	    ptr.drag3_armed_at == 15100000);
	fingers(0, 0, 0);
	update(&ptr, 15200000, D3, &m);

	/* Review A5: with the hybrid timer rule a resting drag stays held. */
	{
		bool released;

		memset(&ptr, 0, sizeof(ptr));
		memset(&gs, 0, sizeof(gs));
		memset(&as, 0, sizeof(as));
		start(&ptr, 16000000, D3, &m);
		(void)hidmt_stall(&ptr, &as, &gs, 16000000 + HIDMT_DRAG3_SETTLE_US +
		    HIDMT_STALL_US + 1, 16000000 + HIDMT_DRAG3_SETTLE_US, &released);
		assert(!released && ptr.drag3);
		(void)hidmt_stall(&ptr, &as, &gs, 16000000 + HIDMT_DRAG3_SETTLE_US +
		    HIDMT_DRAG_STALL_US, 16000000 + HIDMT_DRAG3_SETTLE_US, &released);
		assert(released && !ptr.drag3 && !hidmt_holds(&ptr));
	}

	printf("hidmt three-finger drag: ok\n");
	return 0;
}
