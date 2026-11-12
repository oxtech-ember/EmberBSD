/*
 * Host test of the efidrm shadow-frame logic (efidrm_shadow.h).
 * Model of efidrm_plane_atomic_update: GOP, shadow, source buffer, the same
 * plan/copy/done calls in the same order.  Review Dev Bots #471.1.
 *
 * -DEFIDRM_TEST_LEGACY replaces plan/done with the pre-review logic (the
 * shadow is dropped only by external invalidation): the regressions must
 * fail with it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "../efidrm_shadow.h"

#ifdef EFIDRM_TEST_LEGACY
#undef efidrm_shadow_plan
#undef efidrm_shadow_done
#define efidrm_shadow_plan(have, tun, inval, full, nocopy, valid) \
	legacy_plan(have, tun, inval, full, valid)
#define efidrm_shadow_done(have, nocopy, cover_all, valid) \
	legacy_done(have, cover_all, valid)
static bool legacy_plan(bool have, bool tun, bool inval, bool full, bool *valid)
{
	if (inval)
		*valid = false;
	return tun && have && *valid && !full;
}
static void legacy_done(bool have, bool cover_all, bool *valid)
{
	if (have && !*valid && cover_all)
		*valid = true;
}
#endif

#define W 160			/* pixels: a row is 640 bytes, > EFIDRM_SEG */
#define H 6
#define STRIDE (W * 4)

static uint8_t gop[H * STRIDE], shadow[H * STRIDE], src[H * STRIDE];
static bool valid, tun, nocopy;
static int fails;

#define CHECK(c, msg) do { if (!(c)) { printf("  FAIL: %s\n", msg); fails++; } } while (0)

static void
fill(uint8_t v)
{
	memset(src, v, sizeof(src));
}

/* One commit of the rectangle [x, x+w) x [y, y+h); full = whole-plane update. */
static size_t
commit(int x, int y, int w, int h, bool full, bool inval)
{
	size_t cmp = 0, written = 0;
	bool use, cover = x == 0 && y == 0 && w >= W && h >= H;

	if (full) {
		x = y = 0;
		w = W;
		h = H;
		cover = true;
	}
	use = efidrm_shadow_plan(true, tun, inval, full, nocopy, &valid);
	if (!nocopy)
		written = efidrm_copy_rows(gop + y * STRIDE + x * 4, STRIDE,
		    shadow + y * STRIDE + x * 4, src + y * STRIDE + x * 4,
		    STRIDE, (size_t)w * 4, h, use, !valid, &cmp);
	efidrm_shadow_done(true, nocopy, cover, &valid);
	return written;
}

static bool
rect_eq(const uint8_t *a, const uint8_t *b, int x, int y, int w, int h)
{
	for (int r = y; r < y + h; r++)
		if (memcmp(a + r * STRIDE + x * 4, b + r * STRIDE + x * 4,
		    (size_t)w * 4) != 0)
			return false;
	return true;
}

static void
reset(void)
{
	memset(gop, 0, sizeof(gop));
	memset(shadow, 0, sizeof(shadow));
	valid = false;
	tun = false;
	nocopy = false;
}

/* The review scenario: A -> shadow off -> B -> shadow on -> partial A. */
static void
t_toggle_bypass(void)
{
	puts("toggle_bypass: A, shadow=0, B, shadow=1, partial A");
	reset();
	tun = true;
	fill(0x11);
	commit(0, 0, W, H, true, false);		/* A, shadow valid */
	tun = false;
	fill(0x22);
	commit(10, 1, 40, 3, false, false);		/* B bypasses the shadow */
	tun = true;
	fill(0x11);
	commit(10, 1, 40, 3, false, false);		/* partial A */
	CHECK(rect_eq(gop, src, 10, 1, 40, 3), "GOP differs from A after partial update");
}

/* A full update while the shadow is valid must not leave a stale shadow. */
static void
t_full_keeps_sync(void)
{
	puts("full_keeps_sync: valid shadow, full B, partial A");
	reset();
	tun = true;
	fill(0x11);
	commit(0, 0, W, H, true, false);
	fill(0x22);
	commit(0, 0, 20, 2, true, false);		/* "full" flagged, copies everything */
	fill(0x11);
	commit(0, 0, 20, 2, false, false);
	CHECK(rect_eq(gop, src, 0, 0, 20, 2), "GOP differs from A");
}

/* nocopy must not declare a never-filled shadow valid. */
static void
t_nocopy_not_valid(void)
{
	puts("nocopy_not_valid: nocopy cover, then partial");
	reset();
	tun = true;
	nocopy = true;
	fill(0x33);
	commit(0, 0, W, H, true, false);
	nocopy = false;
	commit(5, 1, 30, 2, false, false);
	CHECK(rect_eq(gop, src, 5, 1, 30, 2), "partial rect not drawn");
}

/* Default (shadow off): every commit copies exactly its rectangle. */
static void
t_default_unchanged(void)
{
	puts("default_unchanged: shadow off writes full rectangles");
	reset();
	fill(0x44);
	CHECK(commit(0, 0, W, H, true, false) == (size_t)W * H * 4, "full byte count");
	fill(0x55);
	CHECK(commit(3, 2, 50, 2, false, false) == (size_t)50 * 2 * 4, "partial byte count");
	CHECK(rect_eq(gop, src, 3, 2, 50, 2), "content");
}

/* The diff path still writes only what differs. */
static void
t_diff_only(void)
{
	puts("diff_only: valid shadow writes only changed runs");
	reset();
	tun = true;
	fill(0x11);
	commit(0, 0, W, H, true, false);
	CHECK(commit(0, 0, 64, 2, false, false) == 0, "unchanged area was written");
	src[1 * STRIDE + 8 * 4] = 0x99;
	CHECK(commit(0, 0, 64, 2, false, false) == EFIDRM_SEG, "one segment expected");
}

/* Random sequences: GOP always equals the source in the committed rectangle. */
static void
t_random(void)
{
	puts("random: 20000 commits with random toggles, invalidations, rectangles");
	reset();
	srand(7);
	for (int i = 0; i < 20000; i++) {
		int x = rand() % W, y = rand() % H;
		int w = 1 + rand() % (W - x), h = 1 + rand() % (H - y);
		bool full = rand() % 9 == 0;

		if (rand() % 5 == 0)
			tun = !tun;
		nocopy = rand() % 40 == 0;
		if (!nocopy) {
			/* change a few pixels inside the rectangle */
			for (int k = 0; k < 6; k++)
				src[(y + rand() % h) * STRIDE + (x + rand() % w) * 4 + rand() % 3] = (uint8_t)rand();
		}
		commit(x, y, w, h, full, rand() % 25 == 0);
		if (!nocopy) {
			if (full)
				CHECK(rect_eq(gop, src, 0, 0, W, H), "full: GOP != src");
			else
				CHECK(rect_eq(gop, src, x, y, w, h), "rect: GOP != src");
		} else {
			/* no copy: the GOP is stale, resynchronize like the console redraw */
			memcpy(gop, src, sizeof(gop));
			valid = false;
		}
		if (fails > 5)
			return;
	}
}

int
main(void)
{
	t_default_unchanged();
	t_diff_only();
	t_toggle_bypass();
	t_full_keeps_sync();
	t_nocopy_not_valid();
	t_random();
	printf("%s (%d failures)\n", fails ? "FAIL" : "PASS", fails);
	return fails ? 1 : 0;
}
