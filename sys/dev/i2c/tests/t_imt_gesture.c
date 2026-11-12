/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Review Dev Bots #472 on the real imt adapter and the real hid.c/hidmt.c,
 * fed with reports in the format of the ELAN 04f3:31fd descriptor (one
 * contact per report, hybrid frames).  Doubles: imt_doubles.h.
 *
 * -DLEGACY builds against the pre-review sources: there is no timer, no
 * sysctl handlers and no WSMOUSEIO_GGESTURE; the shims below do what the
 * old code did (set the sysctl variable directly, no timer).  The tests
 * that exercise the new behaviour must then fail (red run).
 */
#define IMT_REAL_HIDMT
#include "imt_doubles.h"
#include "elan_descriptor.h"
#include "adapter.c"

#ifdef LEGACY
static void imt_tmo(void *a) { (void)a; }	/* no timer */
#define HIDMT_STALL_US 250000			/* the new rule's value, for timing */
#endif

static unsigned npass, nfail;
#ifdef LEGACY
/* Red run: report every failed check, do not stop at the first. */
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", \
    __FILE__, __LINE__, #c); nfail++; } else npass++; } while (0)
#else
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", \
    __FILE__, __LINE__, #c); exit(1); } npass++; } while (0)
#endif

static struct imt_softc sc;
static struct device child, mouse, parent;
static struct ihidev_softc bus;
static uint64_t t_us;

static void
set_time(uint64_t us)
{
	t_us = us;
	test_now.tv_sec = (time_t)(us / 1000000);
	test_now.tv_nsec = (long)(us % 1000000) * 1000;
}

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

static unsigned scan;

/* One frame of n fingers on a row, 300 units apart, all touching or all up. */
static void
frame(int n, int x0, int y, int tip)
{
	uint8_t b[11];
	int i;

	set_time(t_us + 8000);
	scan++;
	for (i = 0; i < n; i++) {
		erep(b, i + 1, tip, x0 + 300 * i, y, scan, i == 0 ? n : 0);
		imt_intr(&sc.sc_hdev, b, 11);
	}
}

/* Only the head report of a frame (the rest is lost or late). */
static void
head_only(int n, int x0, int y)
{
	uint8_t b[11];

	set_time(t_us + 8000);
	scan++;
	erep(b, 1, 1, x0, y, scan, n);
	imt_intr(&sc.sc_hdev, b, 11);
}

static bool attached;

static void
teardown(void)
{
	if (attached)
		CHECK(imt_detach(&child, 0) == 0);
	attached = false;
}

static void
setup(void)
{
	teardown();
	memset(&sc, 0, sizeof(sc));
	memset(&child, 0, sizeof(child));
	memset(&mouse, 0, sizeof(mouse));
	memset(&parent, 0, sizeof(parent));
	memset(&bus, 0, sizeof(bus));
	child.private = &sc;
	bus.sc_dev = &parent;
	parent.private = &bus;
	test_mouse = &mouse;
	test_desc = elan_descriptor;
	test_desc_len = sizeof(elan_descriptor);
	suspend_cb = resume_cb = NULL;
	ngesture = 0;
	gesture_fail = gesture_fail_count = 0;
	events = 0;
	scan = 100;
	set_time(5000000);
	expect_poll = false;
	struct ihidev_attach_arg aux = {.parent = &bus, .reportid = 4};
	imt_attach(&parent, &child, &aux);
	attached = true;
	CHECK(sc.sc_config.nslots == 1 && sc.sc_config.input_size == 11);
	CHECK(imt_enable(&sc) == 0);
	sc.sc_hybrid = 1;
	sc.sc_gestures = 1;
}

/* Run a sysctl write through the handler (or the old direct store). */
static void
set_flag(int *field, int v)
{
#ifdef LEGACY
	*field = v;
#else
	struct sysctlnode node = {.sysctl_data = &sc};
	int (*h)(SYSCTLFN_PROTO) = field == &sc.sc_hybrid ? imt_sysctl_hybrid :
	    field == &sc.sc_scroll ? imt_sysctl_scroll : imt_sysctl_gestures;

	CHECK(h(NULL, 0, NULL, NULL, &v, sizeof(v), NULL, NULL, &node) == 0);
#endif
}

static unsigned
count(unsigned phase)
{
	unsigned i, n = 0;

	for (i = 0; i < ngesture; i++)
		n += gesture_phases[i] == phase;
	return n;
}

/* Fingers placed, then moved far enough for BEGIN (100 units mean travel). */
static void
begin_swipe(void)
{
	frame(3, 1000, 1200, 1);
	CHECK(ngesture == 0);
	frame(3, 1100, 1200, 1);
	frame(3, 1200, 1200, 1);
	CHECK(count(WSMOUSE_GESTURE_BEGIN) == 1);
	CHECK(gesture_phases[0] == WSMOUSE_GESTURE_BEGIN &&
	    gesture_args[0][0] == 3);
	CHECK(sc.sc_gstate.started);
}

static unsigned last_phase(void)
{
	return ngesture ? gesture_phases[ngesture - 1] : 99;
}

static void
test_baseline_swipe(void)
{
	puts("baseline_swipe: begin, update, end");
	setup();
	begin_swipe();
	frame(3, 1300, 1200, 1);
	CHECK(count(WSMOUSE_GESTURE_UPDATE) >= 1);
	frame(3, 1300, 1200, 0);
	CHECK(count(WSMOUSE_GESTURE_END) == 1 && count(WSMOUSE_GESTURE_CANCEL) == 0);
}

/* #472.2: lifecycle events cancel a swipe in progress. */
static void
test_lifecycle_cancel(void)
{
	puts("lifecycle_cancel: disable / suspend / resume in the middle of a swipe");
	setup();
	begin_swipe();
	imt_disable(&sc);
	CHECK(count(WSMOUSE_GESTURE_CANCEL) == 1 && !sc.sc_gstate.started);

	setup();
	begin_swipe();
	CHECK(suspend_cb(&child, NULL));
	CHECK(count(WSMOUSE_GESTURE_CANCEL) == 1 && !sc.sc_gstate.started);
	CHECK(!sc.sc_gstate.latched && !sc.sc_pointer.active);
	expect_poll = true;
	CHECK(resume_cb(&child, NULL));
	CHECK(count(WSMOUSE_GESTURE_CANCEL) == 1);	/* not twice */

	/* Resume alone (suspend flag set by an earlier failed attempt). */
	setup();
	begin_swipe();
	expect_poll = true;
	CHECK(resume_cb(&child, NULL));
	CHECK(count(WSMOUSE_GESTURE_CANCEL) == 1 && !sc.sc_gstate.started);
}

/* #472.2: switching gestures or hybrid off and on clears the state. */
static void
test_mode_change(void)
{
	puts("mode_change: gestures off/on, hybrid off/on");
	/* Started swipe: off cancels at once, without waiting for a report. */
	setup();
	begin_swipe();
	set_flag(&sc.sc_gestures, 0);
	CHECK(count(WSMOUSE_GESTURE_CANCEL) == 1 && !sc.sc_gstate.started);
	set_flag(&sc.sc_gestures, 1);

	/* Candidate that has not started is dropped too. */
	setup();
	frame(3, 1000, 1200, 1);
	CHECK(sc.sc_gstate.latched && !sc.sc_gstate.started);
	set_flag(&sc.sc_gestures, 0);
	CHECK(!sc.sc_gstate.latched && sc.sc_gstate.fingers == 0);
	set_flag(&sc.sc_gestures, 1);
	/* Far from the old start: a new candidate, not an instant BEGIN. */
	frame(3, 2200, 1700, 1);
	CHECK(count(WSMOUSE_GESTURE_BEGIN) == 0 && ngesture == 0);

	/* The same through the report path (value stored without the handler). */
	setup();
	frame(3, 1000, 1200, 1);
	sc.sc_gestures = 0;
	frame(3, 1000, 1200, 1);
	CHECK(!sc.sc_gstate.latched);

	/* Hybrid off and on does not keep the assembler. */
	setup();
	head_only(3, 1000, 1200);
	CHECK(sc.sc_asm.pending);
	set_flag(&sc.sc_hybrid, 0);
	CHECK(!sc.sc_asm.pending);
	set_flag(&sc.sc_hybrid, 1);
	CHECK(!sc.sc_asm.pending);
}

/* #472.3: a lost report releases; a frame without continuation times out. */
static void
test_lost_frame(void)
{
	puts("lost_frame: lost report, timeout without continuation, silence");
	/* A tail report is lost; the next frame's head finds the old one pending. */
	setup();
	begin_swipe();
	head_only(3, 1300, 1200);			/* frame 1: head only */
	CHECK(sc.sc_asm.pending);
	head_only(3, 1400, 1200);			/* frame 2 starts: frame 1 lost */
	CHECK(count(WSMOUSE_GESTURE_CANCEL) == 1 && !sc.sc_gstate.started);
	CHECK(sc.sc_asm.pending && sc.sc_asm.drops == 1);

	/* Head report, nothing follows: the timer drops the frame and cancels. */
	setup();
	begin_swipe();
	head_only(3, 1300, 1200);
	set_time(t_us + 60000);
	imt_tmo(&sc);
	CHECK(count(WSMOUSE_GESTURE_CANCEL) == 1 && !sc.sc_gstate.started);
	CHECK(!sc.sc_asm.pending && sc.sc_asm.drops == 1);

	/* Timer not due yet: the frame is kept. */
	setup();
	begin_swipe();
	head_only(3, 1300, 1200);
	set_time(t_us + 20000);
	imt_tmo(&sc);
	CHECK(count(WSMOUSE_GESTURE_CANCEL) == 0 && sc.sc_asm.pending);

	/* The device goes silent in the middle of a swipe. */
	setup();
	begin_swipe();
	set_time(t_us + HIDMT_STALL_US - 10000);
	imt_tmo(&sc);
	CHECK(count(WSMOUSE_GESTURE_CANCEL) == 0);
	set_time(t_us + 20000);
	imt_tmo(&sc);
	CHECK(count(WSMOUSE_GESTURE_CANCEL) == 1 && !sc.sc_gstate.started);
	CHECK(!sc.sc_pointer.active);
}

/* #472.4: the consumer must not stay in a swipe when events are lost. */
static void
test_queue_full(void)
{
	puts("queue_full: lost END / BEGIN / UPDATE");
	/* END lost -> CANCEL retried on the timer. */
	setup();
	begin_swipe();
	gesture_fail = 1u << WSMOUSE_GESTURE_END;
	gesture_fail_count = 1;
	frame(3, 1200, 1200, 0);
	CHECK(count(WSMOUSE_GESTURE_END) == 0 && count(WSMOUSE_GESTURE_CANCEL) == 0);
	set_time(t_us + 50000);
	imt_tmo(&sc);
	CHECK(count(WSMOUSE_GESTURE_CANCEL) == 1);
	imt_tmo(&sc);
	CHECK(count(WSMOUSE_GESTURE_CANCEL) == 1);	/* once */

	/* CANCEL lost twice, then goes through on the next report. */
	setup();
	begin_swipe();
	gesture_fail = 1u << WSMOUSE_GESTURE_END | 1u << WSMOUSE_GESTURE_CANCEL;
	gesture_fail_count = 2;
	frame(3, 1200, 1200, 0);			/* END fails */
	imt_tmo(&sc);				/* CANCEL fails */
	CHECK(count(WSMOUSE_GESTURE_CANCEL) == 0);
	frame(3, 1000, 600, 1);			/* any report retries */
	CHECK(count(WSMOUSE_GESTURE_CANCEL) == 1);
	/* A new swipe afterwards starts with a clean pair. */
	frame(3, 1100, 600, 1);
	frame(3, 1250, 600, 1);
	CHECK(count(WSMOUSE_GESTURE_BEGIN) == 2);
	CHECK(last_phase() == WSMOUSE_GESTURE_UPDATE || last_phase() == WSMOUSE_GESTURE_BEGIN);

	/* BEGIN lost: no UPDATE, no END/CANCEL for a swipe the consumer never saw. */
	setup();
	gesture_fail = 1u << WSMOUSE_GESTURE_BEGIN;
	gesture_fail_count = 1;
	frame(3, 1000, 1200, 1);
	frame(3, 1100, 1200, 1);
	frame(3, 1200, 1200, 1);		/* BEGIN fails here */
	frame(3, 1300, 1200, 1);
	frame(3, 1400, 1200, 1);
	CHECK(ngesture == 0);
	frame(3, 1400, 1200, 0);
	CHECK(ngesture == 0);
	/* The next swipe is normal. */
	frame(3, 1000, 600, 1);
	frame(3, 1100, 600, 1);
	frame(3, 1250, 600, 1);
	CHECK(count(WSMOUSE_GESTURE_BEGIN) == 1);

	/* UPDATE lost: the delta is carried to the next UPDATE. */
	setup();
	begin_swipe();
	unsigned u0 = count(WSMOUSE_GESTURE_UPDATE);
	int sum0 = 0;
	for (unsigned i = 0; i < ngesture; i++)
		if (gesture_phases[i] == WSMOUSE_GESTURE_UPDATE)
			sum0 += gesture_args[i][1];
	gesture_fail = 1u << WSMOUSE_GESTURE_UPDATE;
	gesture_fail_count = 1;
	frame(3, 1320, 1200, 1);			/* lost */
	frame(3, 1440, 1200, 1);
	int sum1 = 0;
	for (unsigned i = 0; i < ngesture; i++)
		if (gesture_phases[i] == WSMOUSE_GESTURE_UPDATE)
			sum1 += gesture_args[i][1];
	CHECK(count(WSMOUSE_GESTURE_UPDATE) == u0 + 1);
	CHECK(sum1 - sum0 >= 29 && sum1 - sum0 <= 31);	/* 240 units / 8 */
}

static void
test_ioctl(void)
{
	struct wsmouse_gesture_info gi = {99, 99};
	u_int type = 0;

	puts("ioctl: WSMOUSEIO_GGESTURE");
	setup();
	sc.sc_gestures = 0;
	CHECK(imt_ioctl(&sc, WSMOUSEIO_GGESTURE, &gi, 0, NULL) == 0);
	CHECK(gi.wgi_supported == 1 && gi.wgi_enabled == 0);
	sc.sc_gestures = 1;
	CHECK(imt_ioctl(&sc, WSMOUSEIO_GGESTURE, &gi, 0, NULL) == 0);
	CHECK(gi.wgi_supported == 1 && gi.wgi_enabled == 1);
	CHECK(imt_ioctl(&sc, WSMOUSEIO_GTYPE, &type, 0, NULL) == 0 && type == 1);
	CHECK(imt_ioctl(&sc, 12345, &gi, 0, NULL) == EPASSTHROUGH);
}

#ifndef LEGACY
/* Defaults (all sysctls off): no timer, no gesture traffic, as before. */
static void
test_defaults(void)
{
	uint8_t b[11];

	puts("defaults: hybrid=0 gestures=0 keeps the old behaviour");
	setup();
	sc.sc_hybrid = sc.sc_gestures = 0;
	erep(b, 1, 1, 1000, 800, 7, 1);
	imt_intr(&sc.sc_hdev, b, 11);
	CHECK(sc.sc_pointer.active && !sc.sc_tmo.armed && ngesture == 0);
	erep(b, 1, 1, 1100, 800, 8, 1);
	imt_intr(&sc.sc_hdev, b, 11);
	set_time(t_us + 10 * HIDMT_STALL_US);
	imt_tmo(&sc);			/* even if it fired: nothing happens */
	CHECK(sc.sc_pointer.active && ngesture == 0);
	/* Malformed report in the default path still releases the pointer. */
	imt_intr(&sc.sc_hdev, b, 7);
	CHECK(!sc.sc_pointer.active && sc.sc_drops == 1);
	/* Gestures on: the timer runs while a contact is live, stops when idle. */
	setup();
	frame(3, 1000, 1200, 1);
	CHECK(sc.sc_tmo.armed);
	/* Idle from the start (nothing touching): the timer is not started. */
	setup();
	erep(b, 1, 0, 0, 0, 9, 0);
	imt_intr(&sc.sc_hdev, b, 11);
	CHECK(!sc.sc_tmo.armed);
}
#endif

int
main(void)
{
	test_baseline_swipe();
	test_lifecycle_cancel();
	test_mode_change();
	test_lost_frame();
	test_queue_full();
	test_ioctl();
#ifndef LEGACY
	test_defaults();
#endif
	teardown();
	CHECK(hidmt_test_allocations == 0);
	printf("imt gesture/lifecycle: %u checks passed, %u failed\n", npass,
	    nfail);
	return nfail != 0;
}
