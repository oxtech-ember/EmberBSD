/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Origin: EmberBSD contract for a drag with the mechanical button: one
 * finger presses and holds the switch, a second finger moves the pointer.
 */
#include <sys/types.h>
#undef NDEBUG
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <dev/hid/hidmtvar.h>

size_t hidmt_test_allocations;

#define FLAGS	(HIDMT_TAP_LEFT | HIDMT_TAP_RIGHT)

static struct hidmt_frame frame;
static const struct hidmt_scroll scroll = { .enable = true,
    .natural = false, .scale = 1000, .range = 1000 };
static const struct hidmt_scroll *sc;

static void
contacts(unsigned buttons, unsigned n, ...)
{
	va_list ap;
	unsigned i;

	memset(&frame, 0, sizeof(frame));
	frame.buttons = buttons;
	frame.ncontacts = n;
	va_start(ap, n);
	for (i = 0; i < n; i++) {
		frame.contacts[i].id = va_arg(ap, unsigned);
		frame.contacts[i].x = va_arg(ap, int);
		frame.contacts[i].y = va_arg(ap, int);
	}
	va_end(ap);
}

static void
update(struct hidmt_pointer *ptr, uint64_t now, struct hidmt_motion *m)
{
	if (sc != NULL)
		hidmt_pointer_update_scroll(ptr, &frame, now, FLAGS, sc, m);
	else
		hidmt_pointer_update(ptr, &frame, now, FLAGS, m);
}

static unsigned checks;

#define CHECK(e)	do { assert(e); checks++; } while (0)

static void
run(const struct hidmt_scroll *with)
{
	struct hidmt_pointer ptr;
	struct hidmt_motion m;
	struct hidmt_gstate gs;
	struct hidmt_gconf gc = { .enable = true, .start = 0 };
	uint64_t t = 1000000;

	sc = with;
	memset(&ptr, 0, sizeof(ptr));
	memset(&gs, 0, sizeof(gs));

	/* Finger 3 touches and presses the switch: a left press. */
	contacts(0, 1, 3u, 1000, 800);
	update(&ptr, t, &m);
	contacts(1, 1, 3u, 1000, 800);
	update(&ptr, t += 10000, &m);
	CHECK(m.buttons == 1u && m.dx == 0 && m.dy == 0);

	/* Finger 7 lands: the button stays down, the pointer does not jump. */
	contacts(1, 2, 3u, 1000, 800, 7u, 2000, 900);
	update(&ptr, t += 10000, &m);
	CHECK(m.buttons == 1u && m.dx == 0 && m.dy == 0);
	hidmt_gesture_update(&gs, &gc, &frame, &m);
	CHECK(m.gesture == 0);

	/* Finger 7 moves 160 units right, 80 up: the pointer follows it. */
	contacts(1, 2, 3u, 1000, 800, 7u, 2160, 820);
	update(&ptr, t += 10000, &m);
	CHECK(m.buttons == 1u && m.dx == 20 && m.dy == 10);
	CHECK(m.scroll_x == 0 && m.scroll_y == 0);
	hidmt_gesture_update(&gs, &gc, &frame, &m);
	CHECK(m.gesture == 0);

	/* The pressing finger drags by itself while finger 7 rests. */
	contacts(1, 2, 3u, 1080, 800, 7u, 2160, 820);
	update(&ptr, t += 10000, &m);
	CHECK(m.buttons == 1u && m.dx == 10 && m.dy == 0);
	contacts(1, 2, 3u, 1000, 800, 7u, 2160, 820);
	update(&ptr, t += 10000, &m);
	CHECK(m.buttons == 1u && m.dx == -10 && m.dy == 0);

	/* The report order changes: still finger 7, no jump. */
	contacts(1, 2, 7u, 2240, 820, 3u, 1000, 800);
	update(&ptr, t += 10000, &m);
	CHECK(m.buttons == 1u && m.dx == 10 && m.dy == 0);

	/* Finger 7 lifts: the hold continues, the still finger 3 rests. */
	contacts(1, 1, 3u, 1000, 800);
	update(&ptr, t += 10000, &m);
	CHECK(m.buttons == 1u && m.dx == 0 && m.dy == 0);

	/* A new finger 9 lands elsewhere: no jump, then it moves. */
	contacts(1, 2, 3u, 1000, 800, 9u, 500, 300);
	update(&ptr, t += 10000, &m);
	CHECK(m.buttons == 1u && m.dx == 0 && m.dy == 0);
	contacts(1, 2, 3u, 1000, 800, 9u, 420, 300);
	update(&ptr, t += 10000, &m);
	CHECK(m.buttons == 1u && m.dx == -10 && m.dy == 0);

	/* The switch opens: the drag ends at once, both fingers still down. */
	contacts(0, 2, 3u, 1000, 800, 9u, 420, 300);
	update(&ptr, t += 10000, &m);
	CHECK(m.buttons == 0u && m.dx == 0 && m.dy == 0);
	contacts(0, 0);
	update(&ptr, t += 10000, &m);
	CHECK(m.buttons == 0u && !m.tap);

	/*
	 * Clickfinger: a press with two contacts is the right button and
	 * moves no pointer with the second finger.
	 */
	memset(&ptr, 0, sizeof(ptr));
	contacts(0, 2, 3u, 1000, 800, 7u, 2000, 900);
	update(&ptr, t += 100000, &m);
	contacts(1, 2, 3u, 1000, 800, 7u, 2000, 900);
	update(&ptr, t += 10000, &m);
	CHECK(m.buttons == 4u);
	contacts(1, 2, 3u, 1000, 800, 7u, 2160, 900);
	update(&ptr, t += 10000, &m);
	CHECK(m.buttons == 4u && m.dx == 0);
	contacts(0, 0);
	update(&ptr, t += 10000, &m);
	CHECK(m.buttons == 0u);

	/* A press with three contacts is no button drag (middle). */
	memset(&ptr, 0, sizeof(ptr));
	contacts(0, 3, 3u, 1000, 800, 7u, 2000, 900, 8u, 2500, 900);
	update(&ptr, t += 100000, &m);
	contacts(1, 3, 3u, 1000, 800, 7u, 2000, 900, 8u, 2500, 900);
	update(&ptr, t += 10000, &m);
	CHECK(m.buttons == 2u && !m.bdrag);
	contacts(1, 3, 3u, 1000, 800, 7u, 2160, 900, 8u, 2500, 900);
	update(&ptr, t += 10000, &m);
	CHECK(m.buttons == 2u && !m.bdrag && m.dx == 0);
	contacts(0, 0);
	update(&ptr, t += 10000, &m);

	/*
	 * The pressing finger lifts while the second finger keeps the
	 * switch closed: the second finger is tracked, no jump.
	 */
	memset(&ptr, 0, sizeof(ptr));
	contacts(0, 1, 3u, 1000, 800);
	update(&ptr, t += 100000, &m);
	contacts(1, 1, 3u, 1000, 800);
	update(&ptr, t += 10000, &m);
	contacts(1, 2, 3u, 1000, 800, 7u, 2000, 900);
	update(&ptr, t += 10000, &m);
	contacts(1, 1, 7u, 2000, 900);
	update(&ptr, t += 10000, &m);
	CHECK(m.buttons == 1u && m.dx == 0 && m.dy == 0 && !m.bdrag);
	contacts(1, 1, 7u, 2080, 900);
	update(&ptr, t += 10000, &m);
	CHECK(m.buttons == 1u && m.dx == 10);
	contacts(0, 0);
	update(&ptr, t += 10000, &m);
	CHECK(m.buttons == 0u);

	if (with == NULL)
		return;
	/* Without the switch two moving fingers still scroll. */
	memset(&ptr, 0, sizeof(ptr));
	contacts(0, 2, 3u, 1000, 800, 7u, 1300, 800);
	update(&ptr, t += 100000, &m);
	contacts(0, 2, 3u, 1000, 900, 7u, 1300, 900);
	update(&ptr, t += 10000, &m);
	contacts(0, 2, 3u, 1000, 1000, 7u, 1300, 1000);
	update(&ptr, t += 10000, &m);
	CHECK(m.buttons == 0u && m.dx == 0 && m.dy == 0 && m.scroll_y != 0);
	contacts(0, 0);
	update(&ptr, t += 10000, &m);
}

int
main(void)
{
	run(NULL);
	run(&scroll);
	printf("hidmt button drag with a second finger: %u checks passed\n",
	    checks);
	return 0;
}
