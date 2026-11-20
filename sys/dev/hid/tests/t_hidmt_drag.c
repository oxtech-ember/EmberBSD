/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD tap and drag contract (HIDMT_TAP_DRAG). */
#include <sys/types.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <dev/hid/hidmtvar.h>

size_t hidmt_test_allocations;

#define T	(HIDMT_TAP_LEFT | HIDMT_TAP_RIGHT | HIDMT_TAP_DRAG)

static struct hidmt_frame frame;

static void
touch(unsigned id, int x, int y)
{
	memset(&frame, 0, sizeof(frame));
	frame.ncontacts = 1;
	frame.contacts[0].id = id;
	frame.contacts[0].x = x;
	frame.contacts[0].y = y;
}

static void
lift(void)
{
	memset(&frame, 0, sizeof(frame));
}

static void
update(struct hidmt_pointer *ptr, uint64_t now, unsigned flags,
    struct hidmt_motion *m)
{
	hidmt_pointer_update(ptr, &frame, now, flags, m);
}

/* A short still touch at t ending at t + 100 ms: the first tap. */
static void
tap(struct hidmt_pointer *ptr, uint64_t t, unsigned flags,
    struct hidmt_motion *m)
{
	touch(1, 1000, 800);
	update(ptr, t, flags, m);
	assert(m->buttons == 0 && !m->tap);
	lift();
	update(ptr, t + 100000, flags, m);
}

int
main(void)
{
	struct hidmt_pointer ptr = {0};
	struct hidmt_motion m;
	struct hidmt_assembler as = {0};
	struct hidmt_gstate gs = {0};

	/* Without HIDMT_TAP_DRAG a tap is the classic click. */
	tap(&ptr, 1000000, HIDMT_TAP_LEFT, &m);
	assert(m.tap && m.tap_button == 1 && m.buttons == 0);
	assert(!ptr.drag_held);

	/* Tap, then touch and move: the left button stays down. */
	tap(&ptr, 2000000, T, &m);
	assert(!m.tap && m.buttons == 1 && !m.click_after);
	assert(ptr.drag_held && !ptr.dragging);
	assert(ptr.drag_deadline == 2100000 + HIDMT_DRAG_US);
	assert(hidmt_live(&ptr, &as, &gs));
	assert(!hidmt_drag_expire(&ptr, 2100000 + HIDMT_DRAG_US - 1));
	touch(2, 1000, 800);
	update(&ptr, 2200000, T, &m);
	assert(m.buttons == 1 && ptr.dragging);
	touch(2, 1400, 800);
	update(&ptr, 2300000, T, &m);
	assert(m.buttons == 1 && m.dx == 50 && !m.tap);
	/* The timer does not end a drag in progress. */
	assert(!hidmt_drag_expire(&ptr, 9000000));
	touch(2, 1800, 800);
	update(&ptr, 2900000, T, &m);
	assert(m.buttons == 1 && m.dx == 50);
	lift();
	update(&ptr, 3000000, T, &m);
	assert(m.buttons == 0 && !m.tap && !m.click_after);
	assert(!ptr.drag_held && !ptr.dragging);
	update(&ptr, 3001000, T, &m);
	assert(m.buttons == 0 && !m.tap && !m.click_after);

	/* Tap with no new touch: the timer turns it into a click. */
	tap(&ptr, 4000000, T, &m);
	assert(m.buttons == 1 && ptr.drag_held);
	assert(hidmt_drag_expire(&ptr, 4100000 + HIDMT_DRAG_US));
	assert(!ptr.drag_held && !hidmt_drag_expire(&ptr, 5000000));
	touch(3, 1000, 800);
	update(&ptr, 5000000, T, &m);
	assert(m.buttons == 0 && !ptr.dragging);
	lift();
	update(&ptr, 5500000, T, &m);
	assert(m.buttons == 0 && !m.tap);

	/* A touch after the deadline (timer late) releases, no drag. */
	tap(&ptr, 6000000, T, &m);
	assert(m.buttons == 1);
	touch(4, 1000, 800);
	update(&ptr, 6100000 + HIDMT_DRAG_US + 1, T, &m);
	assert(m.buttons == 0 && !ptr.dragging && !ptr.drag_held);

	/* Double tap: release and one more click after the second tap. */
	lift();
	update(&ptr, 6900000, T, &m);
	tap(&ptr, 7000000, T, &m);
	assert(m.buttons == 1);
	touch(5, 1000, 800);
	update(&ptr, 7150000, T, &m);
	assert(m.buttons == 1 && ptr.dragging);
	lift();
	update(&ptr, 7230000, T, &m);
	assert(m.buttons == 0 && m.click_after && !m.tap);
	assert(!ptr.drag_held);

	/* A two-finger tap stays a right click and is never held. */
	memset(&frame, 0, sizeof(frame));
	frame.ncontacts = 2;
	frame.contacts[0].id = 6; frame.contacts[0].x = 1000; frame.contacts[0].y = 800;
	frame.contacts[1].id = 7; frame.contacts[1].x = 1500; frame.contacts[1].y = 800;
	update(&ptr, 8000000, T, &m);
	lift();
	update(&ptr, 8100000, T, &m);
	assert(m.tap && m.tap_button == 4 && m.buttons == 0 && !ptr.drag_held);

	/* A physical click on the second touch keeps the button down. */
	tap(&ptr, 9000000, T, &m);
	touch(8, 1000, 800);
	frame.buttons = 1;
	update(&ptr, 9150000, T, &m);
	assert(m.buttons == 1 && !ptr.dragging && !ptr.drag_held);

	/* A physical left click at the end of a drag stays down. */
	lift();
	update(&ptr, 9600000, T, &m);
	tap(&ptr, 9700000, T, &m);
	touch(9, 1000, 800);
	update(&ptr, 9850000, T, &m);
	assert(ptr.dragging && m.buttons == 1);
	lift();
	frame.buttons = 1;
	update(&ptr, 9860000, T, &m);
	assert(m.buttons == 1 && !ptr.drag_held);

	/* A second finger during a drag releases and ends the drag. */
	lift();
	update(&ptr, 10500000, T, &m);
	tap(&ptr, 10600000, T, &m);
	touch(10, 1000, 800);
	update(&ptr, 10750000, T, &m);
	assert(ptr.dragging && m.buttons == 1);
	frame.ncontacts = 2;
	frame.contacts[1].id = 11; frame.contacts[1].x = 1500; frame.contacts[1].y = 800;
	update(&ptr, 10760000, T, &m);
	assert(m.buttons == 0 && !ptr.dragging && !ptr.drag_held);
	update(&ptr, 10780000, T, &m);
	assert(m.buttons == 0);
	lift();
	update(&ptr, 10800000, T, &m);
	assert(m.buttons == 0 && !m.click_after);
	/* ... and the two-finger tap that it was still gives a right click. */
	assert(m.tap && m.tap_button == 4);

	/* A two-finger physical click during a drag is right only. */
	lift();
	update(&ptr, 10900000, T, &m);
	tap(&ptr, 11000000, T, &m);
	touch(12, 1000, 800);
	update(&ptr, 11150000, T, &m);
	assert(ptr.dragging && m.buttons == 1);
	frame.ncontacts = 2;
	frame.contacts[1].id = 13; frame.contacts[1].x = 1500; frame.contacts[1].y = 800;
	frame.buttons = 1;
	update(&ptr, 11160000, T, &m);
	assert(m.buttons == 4 && !ptr.dragging);

	/* A lifecycle release forgets a held tap (the driver reports up). */
	lift();
	update(&ptr, 11900000, T, &m);
	tap(&ptr, 12000000, T, &m);
	assert(ptr.drag_held);
	(void)hidmt_release(&ptr, &gs);
	assert(!ptr.drag_held && !hidmt_live(&ptr, &as, &gs));

	puts("hidmt tap and drag: hold, move, expiry, late touch, double tap, right tap, button, release: passed");
	return 0;
}
