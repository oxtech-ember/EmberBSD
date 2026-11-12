/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Hybrid frame assembly and two-finger scroll tests, plus a golden
 * comparison of the default (hybrid=0, scroll=0) path against the frozen
 * pre-change implementation in hidmt_baseline.c.
 */
#include <sys/types.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dev/hid/hidmtvar.h>
#include "ptp_descriptor.h"
#include "hybrid_descriptor.h"

int old_hidmt_parse(const void *, int, struct hidmt_config *);
int old_hidmt_decode(const struct hidmt_config *, const uint8_t *, unsigned,
    struct hidmt_frame *);
void old_hidmt_pointer_update(struct hidmt_pointer *,
    const struct hidmt_frame *, uint64_t, unsigned, struct hidmt_motion *);

size_t hidmt_test_allocations;
static unsigned npass;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", \
    __FILE__, __LINE__, #c); exit(1); } npass++; } while (0)

static const unsigned both = HIDMT_TAP_LEFT | HIDMT_TAP_RIGHT;

/* Build one hybrid-layout report (9 bytes). tip=1 touching. */
static void
hrep(uint8_t *r, unsigned id, int tip, int x, int y, unsigned scan,
    unsigned count, unsigned btn)
{
	memset(r, 0, 9);
	r[0] = (tip ? 3 : 1) | (id << 2);
	r[1] = x & 255; r[2] = x >> 8;
	r[3] = y & 255; r[4] = y >> 8;
	r[5] = scan & 255; r[6] = scan >> 8;
	r[7] = count;
	r[8] = btn;
}

static uint32_t rng = 12345;
static unsigned
rnd(unsigned n)
{
	rng = rng * 1664525u + 1013904223u;
	return (rng >> 8) % n;
}

static void
test_golden(void)
{
	struct hidmt_config a, b;
	struct hidmt_frame fa, fb;
	struct hidmt_pointer pa = {0}, pb = {0}, pc = {0};
	struct hidmt_motion ma, mb, mc;
	struct hidmt_scroll off = {0};
	struct hidmt_assembler as = {0};
	uint8_t in[64];
	uint64_t now = 1000000;
	unsigned i, k, ra;
	int rb;

	CHECK(hidmt_parse(ptp_descriptor, sizeof(ptp_descriptor), &a) == 0);
	CHECK(old_hidmt_parse(ptp_descriptor, sizeof(ptp_descriptor), &b) == 0);
	CHECK(a.scantime.size == 16);
	memset(&a.scantime, 0, sizeof(a.scantime));
	CHECK(a.input_id == b.input_id && a.nslots == b.nslots &&
	    a.input_size == b.input_size && a.maxx == b.maxx &&
	    a.maxy == b.maxy && a.config_mode_only == b.config_mode_only);
	CHECK(memcmp(a.slots, b.slots, sizeof(a.slots)) == 0);
	hidmt_parse(ptp_descriptor, sizeof(ptp_descriptor), &a);
	for (i = 0; i < 40000; i++) {
		memset(in, 0, sizeof(in));
		for (k = 0; k < a.input_size; k++)
			in[k] = rnd(256);
		/* Mostly sane frames: small counts, valid coordinates. */
		in[27] = rnd(8);
		for (k = 0; k < 5; k++) {
			unsigned o = k * 5;
			unsigned x = rnd(3685), y = rnd(2177);

			in[o] = (rnd(5) ? 3 : rnd(4)) | (rnd(5) << 2);
			if (rnd(30) == 0)
				x = 5000;
			in[o + 1] = x & 255; in[o + 2] = x >> 8;
			in[o + 3] = y & 255; in[o + 4] = y >> 8;
		}
		ra = hidmt_decode(&a, in, a.input_size, &fa);
		rb = old_hidmt_decode(&a, in, a.input_size, &fb);
		CHECK((int)ra == rb);
		CHECK(fa.ncontacts == fb.ncontacts && fa.buttons == fb.buttons &&
		    fa.cancel_tap == fb.cancel_tap);
		CHECK(memcmp(fa.contacts, fb.contacts,
		    fa.ncontacts * sizeof(fa.contacts[0])) == 0);
		/* The assembler agrees with decode on parallel frames. */
		{
			struct hidmt_frame fc;
			int r;

			/* Per-report check: no state carried over. */
			memset(&as, 0, sizeof(as));
			r = hidmt_assemble(&a, &as, in, a.input_size, now, &fc);

			if (ra == 0) {
				CHECK(r == HIDMT_ASM_READY);
				CHECK(fc.ncontacts == fa.ncontacts &&
				    memcmp(fc.contacts, fa.contacts,
				    fa.ncontacts * sizeof(fa.contacts[0])) == 0);
			} else if (in[27] <= 5)
				CHECK(r == HIDMT_ASM_ERROR);
			else
				CHECK(r == HIDMT_ASM_MORE || r == HIDMT_ASM_ERROR);
		}
		if (ra != 0)
			continue;
		now += 5000 + rnd(300000);
		k = rnd(4);
		hidmt_pointer_update(&pa, &fa, now, k, &ma);
		old_hidmt_pointer_update(&pb, &fb, now, k, &mb);
		hidmt_pointer_update_scroll(&pc, &fa, now, k, &off, &mc);
		CHECK(ma.dx == mb.dx && ma.dy == mb.dy &&
		    ma.buttons == mb.buttons && ma.tap == mb.tap &&
		    ma.tap_button == mb.tap_button);
		CHECK(mc.dx == mb.dx && mc.dy == mb.dy &&
		    mc.buttons == mb.buttons && mc.tap == mb.tap &&
		    mc.tap_button == mb.tap_button);
		CHECK(ma.scroll_x == 0 && ma.scroll_y == 0 &&
		    mc.scroll_x == 0 && mc.scroll_y == 0);
		/* gestures=0: the gesture stage changes nothing at all. */
		{
			struct hidmt_gstate gs = {0};
			struct hidmt_gconf off_g = {0};
			struct hidmt_motion md = mc;

			hidmt_gesture_update(&gs, &off_g, &fa, &md);
			CHECK(memcmp(&md, &mc, sizeof(md)) == 0);
			CHECK(mc.gesture == 0 && ma.gesture == 0);
		}
		CHECK(pa.active == pb.active && pa.id == pb.id &&
		    pa.x == pb.x && pa.y == pb.y && pa.remx == pb.remx &&
		    pa.remy == pb.remy && pa.tap_candidate == pb.tap_candidate &&
		    pa.tap_fingers == pb.tap_fingers && pa.started == pb.started);
		CHECK(pc.active == pb.active && pc.x == pb.x && pc.y == pb.y &&
		    pc.tap_candidate == pb.tap_candidate);
	}
}

static void
test_hybrid(void)
{
	struct hidmt_config cfg;
	struct hidmt_assembler as;
	struct hidmt_frame f;
	uint8_t r[9];
	uint64_t t = 1000000;

	CHECK(hidmt_parse(hybrid_descriptor, sizeof(hybrid_descriptor), &cfg)
	    == 0);
	CHECK(cfg.nslots == 1 && cfg.input_size == 9 && cfg.scantime.size == 16);
	CHECK(cfg.maxy == 2176);

	/* (b) two reports. */
	memset(&as, 0, sizeof(as));
	hrep(r, 1, 1, 1000, 800, 500, 2, 0);
	CHECK(hidmt_assemble(&cfg, &as, r, 9, t, &f) == HIDMT_ASM_MORE);
	hrep(r, 2, 1, 1200, 820, 500, 0, 0);
	CHECK(hidmt_assemble(&cfg, &as, r, 9, t + 1000, &f) == HIDMT_ASM_READY);
	CHECK(f.ncontacts == 2 && f.contacts[0].id == 1 &&
	    f.contacts[1].id == 2 && f.contacts[1].x == 1200 &&
	    f.contacts[1].y == 820 && as.drops == 0 && !as.pending);
	/* Three reports, button from the first. */
	hrep(r, 1, 1, 1000, 800, 510, 3, 1);
	CHECK(hidmt_assemble(&cfg, &as, r, 9, t + 8000, &f) == HIDMT_ASM_MORE);
	hrep(r, 2, 1, 1200, 800, 510, 0, 0);
	CHECK(hidmt_assemble(&cfg, &as, r, 9, t + 9000, &f) == HIDMT_ASM_MORE);
	hrep(r, 3, 1, 1400, 800, 510, 0, 0);
	CHECK(hidmt_assemble(&cfg, &as, r, 9, t + 10000, &f) == HIDMT_ASM_READY);
	CHECK(f.ncontacts == 3 && f.buttons == 1 && f.contacts[2].id == 3);
	/* A lifted finger in a hybrid frame (tip 0) is not a contact. */
	hrep(r, 1, 1, 1000, 800, 520, 2, 0);
	CHECK(hidmt_assemble(&cfg, &as, r, 9, t + 20000, &f) == HIDMT_ASM_MORE);
	hrep(r, 2, 0, 1200, 800, 520, 0, 0);
	CHECK(hidmt_assemble(&cfg, &as, r, 9, t + 21000, &f) == HIDMT_ASM_READY);
	CHECK(f.ncontacts == 1 && f.contacts[0].id == 1);

	/* (c) lost tail: a new frame starts, nothing partial is reported. */
	memset(&as, 0, sizeof(as));
	hrep(r, 1, 1, 1000, 800, 600, 3, 0);
	CHECK(hidmt_assemble(&cfg, &as, r, 9, t, &f) == HIDMT_ASM_MORE);
	hrep(r, 5, 1, 2000, 900, 610, 1, 0);
	CHECK(hidmt_assemble(&cfg, &as, r, 9, t + 8000, &f) == HIDMT_ASM_READY);
	CHECK(f.ncontacts == 1 && f.contacts[0].id == 5 && as.drops == 1);
	/* Its late tail (old scan time, count 0) is ignored, not a release. */
	hrep(r, 2, 1, 1200, 800, 600, 0, 0);
	CHECK(hidmt_assemble(&cfg, &as, r, 9, t + 9000, &f) == HIDMT_ASM_MORE);
	/* Timeout. */
	memset(&as, 0, sizeof(as));
	hrep(r, 1, 1, 1000, 800, 700, 2, 0);
	CHECK(hidmt_assemble(&cfg, &as, r, 9, t, &f) == HIDMT_ASM_MORE);
	hrep(r, 2, 1, 1200, 800, 700, 0, 0);
	CHECK(hidmt_assemble(&cfg, &as, r, 9, t + HIDMT_HYBRID_TIMEOUT_US + 1,
	    &f) == HIDMT_ASM_MORE);
	CHECK(as.drops == 1 && !as.pending);
	/* Changed scan time: the tail belongs to another frame. */
	memset(&as, 0, sizeof(as));
	hrep(r, 1, 1, 1000, 800, 800, 2, 0);
	CHECK(hidmt_assemble(&cfg, &as, r, 9, t, &f) == HIDMT_ASM_MORE);
	hrep(r, 2, 1, 1200, 800, 801, 0, 0);
	CHECK(hidmt_assemble(&cfg, &as, r, 9, t + 1000, &f) == HIDMT_ASM_MORE);
	CHECK(as.drops == 1 && !as.pending);
	/* Orphan tail without any pending frame (head report lost). */
	memset(&as, 0, sizeof(as));
	hrep(r, 1, 1, 1000, 800, 900, 1, 0);
	CHECK(hidmt_assemble(&cfg, &as, r, 9, t, &f) == HIDMT_ASM_READY);
	hrep(r, 2, 1, 1200, 800, 900, 0, 0);
	CHECK(hidmt_assemble(&cfg, &as, r, 9, t + 1000, &f) == HIDMT_ASM_MORE);
	/* A count 0 report with a fresh scan time is an explicit release. */
	hrep(r, 0, 0, 0, 0, 905, 0, 0);
	CHECK(hidmt_assemble(&cfg, &as, r, 9, t + 2000, &f) == HIDMT_ASM_READY);
	CHECK(f.ncontacts == 0);
	/* Errors reset the assembler: bad length, bad coordinates, count. */
	memset(&as, 0, sizeof(as));
	hrep(r, 1, 1, 1000, 800, 1000, 2, 0);
	CHECK(hidmt_assemble(&cfg, &as, r, 9, t, &f) == HIDMT_ASM_MORE);
	CHECK(hidmt_assemble(&cfg, &as, r, 8, t + 1, &f) == HIDMT_ASM_ERROR);
	CHECK(!as.pending && as.drops == 1);
	hrep(r, 1, 1, 1000, 800, 1001, 2, 0);
	CHECK(hidmt_assemble(&cfg, &as, r, 9, t + 10, &f) == HIDMT_ASM_MORE);
	hrep(r, 2, 1, 5000, 800, 1001, 0, 0);
	CHECK(hidmt_assemble(&cfg, &as, r, 9, t + 20, &f) == HIDMT_ASM_ERROR);
	CHECK(!as.pending);
	hrep(r, 1, 1, 1000, 800, 1002, 99, 0);
	CHECK(hidmt_assemble(&cfg, &as, r, 9, t + 30, &f) == HIDMT_ASM_ERROR);
	hrep(r, 1, 1, 1000, 800, 1003, 2, 0);
	CHECK(hidmt_assemble(&cfg, &as, r, 9, t + 40, &f) == HIDMT_ASM_MORE);
	hrep(r, 1, 1, 1100, 800, 1003, 0, 0);	/* duplicate contact id */
	CHECK(hidmt_assemble(&cfg, &as, r, 9, t + 50, &f) == HIDMT_ASM_ERROR);
}

static void
feed(const struct hidmt_config *cfg, struct hidmt_assembler *as,
    struct hidmt_pointer *p, const struct hidmt_scroll *sc, uint64_t now,
    unsigned scan, int n, const int *xy, const int *tip, struct hidmt_motion *m)
{
	struct hidmt_frame f;
	uint8_t r[9];
	int i, res = HIDMT_ASM_MORE;

	for (i = 0; i < n; i++) {
		hrep(r, i + 1, tip ? tip[i] : 1, xy[2 * i], xy[2 * i + 1], scan,
		    i == 0 ? n : 0, 0);
		res = hidmt_assemble(cfg, as, r, 9, now, &f);
	}
	if (res != HIDMT_ASM_READY)
		fprintf(stderr, "feed n=%d scan=%u res=%d\n", n, scan, res);
	CHECK(res == HIDMT_ASM_READY);
	hidmt_pointer_update_scroll(p, &f, now, both, sc, m);
}

static void
test_pointer(void)
{
	struct hidmt_config cfg;
	struct hidmt_assembler as = {0};
	struct hidmt_pointer p = {0};
	struct hidmt_motion m;
	struct hidmt_scroll sc = { true, false, 112, 2176 };
	struct hidmt_scroll nat = { true, true, 112, 2176 };
	uint64_t t = 5000000;
	unsigned scan = 1;
	int xy[4], sy = 0, sx = 0, y, k;

	CHECK(hidmt_parse(hybrid_descriptor, sizeof(hybrid_descriptor), &cfg)
	    == 0);
	/* (d) one finger: motion, tap, as on the parallel path. */
	xy[0] = 1000; xy[1] = 800;
	feed(&cfg, &as, &p, &sc, t, scan++, 1, xy, NULL, &m);
	CHECK(m.dx == 0 && m.dy == 0 && !m.tap);
	xy[0] = 1080;
	feed(&cfg, &as, &p, &sc, t += 8000, scan++, 1, xy, NULL, &m);
	CHECK(m.dx == 10 && m.dy == 0 && m.scroll_x == 0 && m.scroll_y == 0);
	{
		int tip0 = 0;

		feed(&cfg, &as, &p, &sc, t += 8000, scan++, 1, xy, &tip0, &m);
		CHECK(!m.tap);	/* moved 80 > 50: not a tap */
	}
	xy[0] = 1000;
	feed(&cfg, &as, &p, &sc, t += 100000, scan++, 1, xy, NULL, &m);
	{
		int tip0 = 0;

		feed(&cfg, &as, &p, &sc, t += 50000, scan++, 1, xy, &tip0, &m);
		CHECK(m.tap && m.tap_button == 1);
	}

	/* (d) two fingers, touch only: right tap, no scroll, no motion. */
	xy[0] = 1000; xy[1] = 800; xy[2] = 1200; xy[3] = 800;
	feed(&cfg, &as, &p, &sc, t += 100000, scan++, 2, xy, NULL, &m);
	xy[1] = 805; xy[3] = 803;
	feed(&cfg, &as, &p, &sc, t += 8000, scan++, 2, xy, NULL, &m);
	CHECK(m.dx == 0 && m.dy == 0 && m.scroll_x == 0 && m.scroll_y == 0);
	{
		int tip[2] = {0, 0};

		/* Both fingers lift: a hybrid report with count 0 contacts. */
		feed(&cfg, &as, &p, &sc, t += 50000, scan++, 2, xy, tip, &m);
		CHECK(m.tap && m.tap_button == 4);
	}

	/* (d) two fingers moving up over the full height: scroll only. */
	xy[0] = 1000; xy[1] = 2150; xy[2] = 1200; xy[3] = 2150;
	feed(&cfg, &as, &p, &sc, t += 200000, scan++, 2, xy, NULL, &m);
	sy = sx = 0;
	for (y = 2150 - 8, k = 0; y >= 20; y -= 8, k++) {
		xy[1] = xy[3] = y;
		feed(&cfg, &as, &p, &sc, t += 8000, scan++, 2, xy, NULL, &m);
		CHECK(m.dx == 0 && m.dy == 0);
		sx += m.scroll_x;
		sy += m.scroll_y;
	}
	/* Scrolling starts after 20 units: the rest of the pad is counted. */
	CHECK(sx == 0);
	CHECK(sy > 100 && sy <= 112);
	/* One finger remains: the pointer stays still, then release: no tap. */
	xy[0] = 1200; xy[1] = 100;
	feed(&cfg, &as, &p, &sc, t += 8000, scan++, 1, xy, NULL, &m);
	CHECK(m.dx == 0 && m.dy == 0 && !m.tap);
	xy[0] = 1300;
	feed(&cfg, &as, &p, &sc, t += 8000, scan++, 1, xy, NULL, &m);
	CHECK(m.dx == 0 && m.dy == 0);
	{
		int tip0 = 0;

		feed(&cfg, &as, &p, &sc, t += 8000, scan++, 1, xy, &tip0, &m);
		CHECK(!m.tap && m.buttons == 0);
	}
	/* Natural direction is the mirror image, and sideways works. */
	memset(&p, 0, sizeof(p));
	xy[0] = 1000; xy[1] = 1000; xy[2] = 1200; xy[3] = 1000;
	feed(&cfg, &as, &p, &nat, t += 200000, scan++, 2, xy, NULL, &m);
	sy = sx = 0;
	for (k = 1; k <= 100; k++) {
		xy[1] = xy[3] = 1000 - 8 * k;
		xy[0] = 1000 + 8 * k; xy[2] = 1200 + 8 * k;
		feed(&cfg, &as, &p, &nat, t += 8000, scan++, 2, xy, NULL, &m);
		sx += m.scroll_x;
		sy += m.scroll_y;
	}
	CHECK(sy < -20 && sx < -20);
	/* Jitter below the threshold neither scrolls nor cancels the tap. */
	memset(&p, 0, sizeof(p));
	xy[0] = 1000; xy[1] = 800; xy[2] = 1200; xy[3] = 800;
	feed(&cfg, &as, &p, &sc, t += 200000, scan++, 2, xy, NULL, &m);
	xy[1] = 810; xy[3] = 812;
	feed(&cfg, &as, &p, &sc, t += 8000, scan++, 2, xy, NULL, &m);
	CHECK(m.scroll_y == 0 && p.tap_candidate && !p.scrolling);
	/* (f) scroll disabled: two fingers behave as before (tracked move). */
	memset(&p, 0, sizeof(p));
	xy[0] = 1000; xy[1] = 800; xy[2] = 1200; xy[3] = 800;
	feed(&cfg, &as, &p, NULL, t += 200000, scan++, 2, xy, NULL, &m);
	xy[0] = 1080; xy[2] = 1280;
	feed(&cfg, &as, &p, NULL, t += 8000, scan++, 2, xy, NULL, &m);
	CHECK(m.dx == 10 && m.scroll_x == 0 && m.scroll_y == 0);
	/* Physical click with two contacts is still a right click. */
	{
		struct hidmt_frame f = {0};

		memset(&p, 0, sizeof(p));
		f.ncontacts = 2; f.buttons = 1;
		f.contacts[0] = (struct hidmt_contact){1000, 800, 1};
		f.contacts[1] = (struct hidmt_contact){1200, 800, 2};
		hidmt_pointer_update_scroll(&p, &f, t, both, &sc, &m);
		CHECK(m.buttons == 4 && m.dx == 0 && m.dy == 0);
	}
}

int
main(void)
{
	test_golden();
	test_hybrid();
	test_pointer();
	printf("hidmt hybrid/scroll/golden: %u checks passed, 0 failed\n", npass);
	return 0;
}
