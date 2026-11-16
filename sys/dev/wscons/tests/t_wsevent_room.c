/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Host test of the free-slot rule of the wscons event queue.
 * run-wsevent.sh extracts the code between the "wsevent-room" markers from
 * wsevent.c; the model below fills and drains the queue the way
 * wsevent_inject() and wsevent_read() do (put == get means "empty").
 * Compile with -DLEGACY for the previous rule: an inject that used the last
 * free slot made put equal to get, and the reader lost the whole queue.
 */
#include <sys/types.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

#define WSEVENT_QSIZE	256

#ifdef LEGACY
static inline size_t
wsevent_room(unsigned get, unsigned put)
{
	if (put < get)
		return get - put;
	return WSEVENT_QSIZE - (put - get);
}
#else
#include "wsevent-room-extract.h"
#endif

static unsigned npass, nfail;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", \
    __LINE__, #c); nfail++; } else npass++; } while (0)

enum { MOVE = 6, UP = 4, DOWN = 5 };

static int q[WSEVENT_QSIZE];
static unsigned get, put;

/* wsevent_inject(): all events or none. */
static int
inject(const int *ev, size_t n)
{
	size_t i;

	if (wsevent_room(get, put) < n)
		return ENOSPC;
	for (i = 0; i < n; i++) {
		q[put] = ev[i];
		put = (put + 1) % WSEVENT_QSIZE;
	}
	return 0;
}

/* wsevent_read(): the reader takes all that it can see. */
static size_t
drain(int *out)
{
	size_t n = 0;

	while (get != put) {
		out[n++] = q[get];
		get = (get + 1) % WSEVENT_QSIZE;
	}
	return n;
}

/*
 * The consumer does not read while the pointer moves with a button held;
 * then the button is released.  Model of wsmouse_input(): the driver state
 * (ub) follows the consumer only when the inject succeeds, and a failed
 * change is sent again with the next report.
 */
static void
stalled_reader(unsigned start, unsigned moves_per_report)
{
	int out[2 * WSEVENT_QSIZE], ev[3];
	const int two[2] = { MOVE, MOVE };
	size_t i, n, down = 0, up = 0;
	unsigned mb, ub = 0, r;

	get = put = start;
	mb = 1;
	ev[0] = DOWN;
	CHECK(inject(ev, 1) == 0);
	ub = mb;
	/* Three queue lengths of motion while nobody reads. */
	for (r = 0; r < 3 * WSEVENT_QSIZE; r++)
		(void)inject(two, moves_per_report);
	/* The release, repeated with later reports until it is queued. */
	mb = 0;
	n = drain(out);
	for (i = 0; i < n; i++) {
		down += out[i] == DOWN;
		up += out[i] == UP;
	}
	for (r = 0; r < 4 && mb != ub; r++) {
		ev[0] = UP;
		if (inject(ev, 1) == 0)
			ub = mb;
	}
	n = drain(out);
	for (i = 0; i < n; i++) {
		down += out[i] == DOWN;
		up += out[i] == UP;
	}
	/* The consumer saw the press and its release. */
	CHECK(down == 1);
	CHECK(up == 1);
}

int
main(void)
{
	const int one = MOVE;
	int out[WSEVENT_QSIZE];
	unsigned start, i;

	/* An empty queue has room; it never offers all slots. */
	CHECK(wsevent_room(0, 0) == WSEVENT_QSIZE - 1);
	CHECK(wsevent_room(7, 6) == 0);
	CHECK(wsevent_room(0, WSEVENT_QSIZE - 1) == 0);
	CHECK(wsevent_room(10, 5) == 4);

	/* Fill from every start index: the queue never looks empty. */
	for (start = 0; start < WSEVENT_QSIZE; start++) {
		get = put = start;
		for (i = 0; i < 2 * WSEVENT_QSIZE; i++)
			(void)inject(&one, 1);
		CHECK(get != put);
		CHECK(drain(out) == WSEVENT_QSIZE - 1);
		CHECK(inject(&one, 1) == 0);
		(void)drain(out);
	}

	stalled_reader(0, 1);
	stalled_reader(0, 2);
	stalled_reader(200, 1);
	stalled_reader(201, 2);

	printf("wsevent room: %u passed, %u failed\n", npass, nfail);
	return nfail != 0;
}
