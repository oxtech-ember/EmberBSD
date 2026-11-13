/* $OpenBSD: hidmt.c,v 1.16 2025/07/21 21:46:40 bru Exp $ */
/*
 * HID multitouch driver for devices conforming to Windows Precision Touchpad
 * standard
 *
 * https://msdn.microsoft.com/en-us/library/windows/hardware/dn467314%28v=vs.85%29.aspx
 * https://docs.microsoft.com/en-us/windows-hardware/design/component-guidelines/touchscreen-packet-reporting-modes
 *
 * Copyright (c) 2016 joshua stein <jcs@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* NetBSD wscons adaptation. Pointer policy is independent of the I2C transport. */
#include <sys/param.h>
#include <sys/systm.h>
#include <dev/hid/hidmtvar.h>

static bool
hidmt_location_valid(const struct hid_location *loc, unsigned bytes)
{
	return loc->size > 0 && loc->size <= 32 && loc->count == 1 &&
	    loc->pos <= bytes * 8 && loc->size <= bytes * 8 - loc->pos;
}

int
hidmt_parse(const void *desc, int len, struct hidmt_config *cfg)
{
	struct hid_data *hd;
	struct hid_item h;
	struct hidmt_slot *slot = NULL;
	int touchlevel = 0, fingerlevel = 0, configlevel = 0;
	unsigned i;
	unsigned feature_fields, mode_fields;
	bool mode_range_ok;

	memset(cfg, 0, sizeof(*cfg));
	cfg->input_id = cfg->config_id = -1;
	if (len <= 0 || len > 65535)
		return 1;
	hd = hid_start_parse(desc, len, hid_input);
	if (hd == NULL)
		return 1;
	while (hid_get_item(hd, &h)) {
		if (h.kind == hid_collection) {
			if (h.usage == HID_USAGE2(HUP_DIGITIZERS, HUD_TOUCHPAD))
				touchlevel = h.collevel;
			if (touchlevel && h.usage ==
			    HID_USAGE2(HUP_DIGITIZERS, HUD_FINGER)) {
				if (cfg->nslots >= HIDMT_MAX_CONTACTS)
					goto fail;
				slot = &cfg->slots[cfg->nslots++];
				fingerlevel = h.collevel;
			}
			continue;
		}
		if (h.kind == hid_endcollection) {
			if (h.collevel < fingerlevel) {
				fingerlevel = 0;
				slot = NULL;
			}
			if (h.collevel < touchlevel)
				touchlevel = 0;
			continue;
		}
		if (h.kind != hid_input || !touchlevel ||
		    (h.flags & HIO_CONST))
			continue;
		if (slot != NULL) {
			if (cfg->input_id < 0)
				cfg->input_id = h.report_ID;
			if ((unsigned)cfg->input_id != h.report_ID)
				goto fail;
			switch (h.usage) {
			case HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_X):
				if (h.flags & HIO_RELATIVE)
					goto fail;
				slot->x = h.loc;
				cfg->minx = h.logical_minimum;
				cfg->maxx = h.logical_maximum;
				break;
			case HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_Y):
				if (h.flags & HIO_RELATIVE)
					goto fail;
				slot->y = h.loc;
				cfg->miny = h.logical_minimum;
				cfg->maxy = h.logical_maximum;
				break;
			case HID_USAGE2(HUP_DIGITIZERS, HUD_TIP_SWITCH):
				slot->tip = h.loc;
				break;
			case HID_USAGE2(HUP_DIGITIZERS, HUD_CONFIDENCE):
				slot->confidence = h.loc;
				break;
			case HID_USAGE2(HUP_DIGITIZERS, HUD_CONTACTID):
				slot->id = h.loc;
				break;
			default:
				break;
			}
		} else if ((unsigned)cfg->input_id == h.report_ID) {
			if (h.usage == HID_USAGE2(HUP_DIGITIZERS, HUD_CONTACTCOUNT))
				cfg->count = h.loc;
			if (h.usage == HID_USAGE2(HUP_BUTTON, 1))
				cfg->button = h.loc;
			if (h.usage == HID_USAGE2(HUP_DIGITIZERS, HUD_SCAN_TIME))
				cfg->scantime = h.loc;
		}
	}
	hid_end_parse(hd);
	hd = hid_start_parse(desc, len, hid_feature);
	if (hd == NULL)
		return 1;
	while (hid_get_item(hd, &h)) {
		if (h.kind == hid_collection && h.usage ==
		    HID_USAGE2(HUP_DIGITIZERS, HUD_CONFIG))
			configlevel = h.collevel;
		if (h.kind == hid_endcollection && h.collevel < configlevel)
			configlevel = 0;
		if (configlevel && h.kind == hid_feature && h.usage ==
		    HID_USAGE2(HUP_DIGITIZERS, HUD_INPUT_MODE)) {
			cfg->config_id = h.report_ID;
			cfg->mode = h.loc;
		}
	}
	hid_end_parse(hd);
	if (cfg->input_id < 0 || cfg->config_id < 0 || cfg->nslots == 0 ||
	    cfg->minx < 0 || cfg->miny < 0 || cfg->maxx <= cfg->minx ||
	    cfg->maxy <= cfg->miny || cfg->maxx > 65535 || cfg->maxy > 65535)
		return 1;
	cfg->input_size = hid_report_size(desc, len, hid_input, cfg->input_id);
	cfg->config_size = hid_report_size(desc, len, hid_feature, cfg->config_id);
	if (cfg->input_size > 1024 || cfg->config_size > 1024 ||
	    cfg->mode.size < 2 ||
	    !hidmt_location_valid(&cfg->mode, cfg->config_size) ||
	    !hidmt_location_valid(&cfg->count, cfg->input_size))
		return 1;
	if (cfg->button.size &&
	    !hidmt_location_valid(&cfg->button, cfg->input_size))
		return 1;
	/* Scan Time only refines hybrid frames; an unusable one is ignored. */
	if (cfg->scantime.size &&
	    !hidmt_location_valid(&cfg->scantime, cfg->input_size))
		memset(&cfg->scantime, 0, sizeof(cfg->scantime));
	for (i = 0; i < cfg->nslots; i++) {
		slot = &cfg->slots[i];
		if (!hidmt_location_valid(&slot->x, cfg->input_size) ||
		    !hidmt_location_valid(&slot->y, cfg->input_size) ||
		    !hidmt_location_valid(&slot->tip, cfg->input_size) ||
		    !hidmt_location_valid(&slot->confidence, cfg->input_size) ||
		    !hidmt_location_valid(&slot->id, cfg->input_size))
			return 1;
	}
	/*
	 * A direct SET is safe only when Input Mode is the sole variable in
	 * its complete feature report.  Scan the whole report, including items
	 * outside the configuration collection; constant padding is harmless.
	 */
	feature_fields = mode_fields = 0;
	mode_range_ok = false;
	hd = hid_start_parse(desc, len, hid_feature);
	if (hd == NULL)
		return 1;
	while (hid_get_item(hd, &h)) {
		if (h.kind != hid_feature || h.report_ID != (unsigned)cfg->config_id ||
		    (h.flags & HIO_CONST))
			continue;
		feature_fields++;
		if (h.usage == HID_USAGE2(HUP_DIGITIZERS, HUD_INPUT_MODE) &&
		    (h.flags & HIO_VARIABLE) &&
		    h.loc.size == cfg->mode.size && h.loc.pos == cfg->mode.pos &&
		    h.loc.count == cfg->mode.count) {
			mode_fields++;
			mode_range_ok = h.logical_minimum <= 3 &&
			    h.logical_maximum >= 3;
		}
	}
	hid_end_parse(hd);
	cfg->config_mode_only = feature_fields == 1 && mode_fields == 1 &&
	    mode_range_ok;
	return 0;
fail:
	hid_end_parse(hd);
	return 1;
}

/* Is any contact slot of this report touching? */
static bool
hidmt_any_tip(const struct hidmt_config *cfg, const uint8_t *data)
{
	unsigned i;

	for (i = 0; i < cfg->nslots; i++)
		if (hid_get_udata(data, &cfg->slots[i].tip))
			return true;
	return false;
}

/*
 * Append contacts from slots 0..n-1 of one report. Returns non-zero for a
 * malformed contact.
 */
static int
hidmt_decode_slots(const struct hidmt_config *cfg, const uint8_t *data,
    unsigned n, struct hidmt_frame *frame)
{
	unsigned i, j;
	const struct hidmt_slot *slot;
	struct hidmt_contact *contact;

	for (i = 0; i < n; i++) {
		slot = &cfg->slots[i];
		if (!hid_get_udata(data, &slot->tip))
			continue;
		if (!hid_get_udata(data, &slot->confidence)) {
			frame->cancel_tap = true;
			continue;
		}
		contact = &frame->contacts[frame->ncontacts];
		contact->id = hid_get_udata(data, &slot->id);
		contact->x = hid_get_udata(data, &slot->x);
		contact->y = hid_get_udata(data, &slot->y);
		if (contact->x < cfg->minx || contact->x > cfg->maxx ||
		    contact->y < cfg->miny || contact->y > cfg->maxy)
			return 1;
		for (j = 0; j < frame->ncontacts; j++)
			if (frame->contacts[j].id == contact->id)
				return 1;
		frame->ncontacts++;
	}
	return 0;
}

int
hidmt_decode(const struct hidmt_config *cfg, const uint8_t *data, unsigned len,
    struct hidmt_frame *frame)
{
	unsigned count;

	memset(frame, 0, sizeof(*frame));
	if (len != cfg->input_size)
		return 1;
	count = hid_get_udata(data, &cfg->count);
	/* This transport accepts complete parallel frames, not hybrid frames. */
	if (count > cfg->nslots)
		return 1;
	frame->buttons = cfg->button.size && hid_get_udata(data, &cfg->button);
	return hidmt_decode_slots(cfg, data, count, frame);
}

/*
 * Feed one input report to the hybrid frame assembler. A report whose
 * Contact Count fits the parallel slots is a complete frame, exactly as for
 * hidmt_decode(). A larger count starts a hybrid frame: the contacts of
 * this and of the following reports (Contact Count 0) are collected until
 * the announced number is reached. An unfinished frame is dropped when a new
 * frame starts, when the Scan Time changes, or when it is older than
 * HIDMT_HYBRID_TIMEOUT_US; a dropped frame never yields contacts.
 */
int
hidmt_assemble(const struct hidmt_config *cfg, struct hidmt_assembler *as,
    const uint8_t *data, unsigned len, uint64_t now, struct hidmt_frame *frame)
{
	unsigned count, scantime = 0, n;
	bool have_st, dropped = false;

	if (len != cfg->input_size) {
		if (as->pending) {
			as->drops++;
			as->lost = true;
		}
		as->pending = false;
		memset(frame, 0, sizeof(*frame));
		return HIDMT_ASM_ERROR;
	}
	count = hid_get_udata(data, &cfg->count);
	have_st = cfg->scantime.size != 0;
	if (have_st)
		scantime = hid_get_udata(data, &cfg->scantime);

	if (as->pending && (count != 0 || now < as->started ||
	    now - as->started > HIDMT_HYBRID_TIMEOUT_US ||
	    (have_st && scantime != as->scantime))) {
		as->pending = false;
		as->drops++;
		as->lost = true;
		as->have_dropped = true;
		as->dropped_scantime = as->scantime;
		dropped = true;
	}

	if (as->pending) {
		/* Continuation report (Contact Count 0) of the same frame. */
		n = cfg->nslots;
		if (n > as->expected - as->have)
			n = as->expected - as->have;
		if (hidmt_decode_slots(cfg, data, n, &as->frame) != 0) {
			as->pending = false;
			as->drops++;
			as->lost = true;
			memset(frame, 0, sizeof(*frame));
			return HIDMT_ASM_ERROR;
		}
		as->have += n;
		if (as->have < as->expected)
			return HIDMT_ASM_MORE;
		as->pending = false;
		*frame = as->frame;
		as->last_scantime = scantime;
		return HIDMT_ASM_READY;
	}

	/*
	 * Contact Count 0 is either the tail of a hybrid frame (Microsoft
	 * Precision Touchpad: Contact Count only in the first report of a
	 * frame, 0 in the following ones, the same Scan Time) or a release.
	 * A tail always carries contacts that are still down; a report that
	 * has none is a release even when a frame was just dropped or the
	 * Scan Time changed (otherwise the release is swallowed and the
	 * pointer and gesture stay active).  Confirm the layout with a
	 * hw.imt0.rawlog capture on the hardware.
	 */
	if (count == 0 && (((dropped || (have_st && as->have_dropped &&
	    scantime == as->dropped_scantime)) && hidmt_any_tip(cfg, data)) ||
	    (have_st && as->have_scantime && scantime == as->last_scantime))) {
		/* Orphan tail of a lost or dropped hybrid frame. */
		as->last_scantime = scantime;
		return HIDMT_ASM_MORE;
	}
	as->have_scantime = have_st;
	as->last_scantime = scantime;
	if (count == 0)
		as->have_dropped = false;
	if (count <= cfg->nslots) {
		if (hidmt_decode(cfg, data, len, frame) != 0)
			return HIDMT_ASM_ERROR;
		return HIDMT_ASM_READY;
	}
	if (count > HIDMT_MAX_CONTACTS) {
		memset(frame, 0, sizeof(*frame));
		return HIDMT_ASM_ERROR;
	}
	/* First report of a hybrid frame. */
	memset(&as->frame, 0, sizeof(as->frame));
	as->frame.buttons = cfg->button.size &&
	    hid_get_udata(data, &cfg->button);
	if (hidmt_decode_slots(cfg, data, cfg->nslots, &as->frame) != 0) {
		as->drops++;
		memset(frame, 0, sizeof(*frame));
		return HIDMT_ASM_ERROR;
	}
	as->pending = true;
	as->expected = count;
	as->have = cfg->nslots;
	as->scantime = scantime;
	as->started = now;
	return HIDMT_ASM_MORE;
}

static void
hidmt_pointer_base(struct hidmt_pointer *ptr, const struct hidmt_frame *frame,
    uint64_t now, unsigned tap_flags, struct hidmt_motion *motion)
{
	const struct hidmt_contact *contact;
	unsigned i, tap_fingers, fingers, allowed;
	int dx, dy;
	bool same_contact, want_left, want_right;

	want_left = (tap_flags & HIDMT_TAP_LEFT) != 0;
	want_right = (tap_flags & HIDMT_TAP_RIGHT) != 0;
	memset(motion, 0, sizeof(*motion));
	/*
	 * Clickfinger: a physical click with two contacts is right, with
	 * three contacts is middle. One contact keeps the hardware button.
	 * This path does not depend on tap synthesis flags.
	 */
	motion->buttons = frame->buttons;
	if (frame->buttons && frame->ncontacts >= 3)
		motion->buttons = 2u;
	else if (frame->buttons && frame->ncontacts >= 2)
		motion->buttons = 4u;
	if (frame->cancel_tap)
		ptr->tap_candidate = false;
	if (frame->ncontacts == 0) {
		tap_fingers = ptr->tap_fingers;
		allowed = tap_fingers >= 2 ? HIDMT_TAP_RIGHT : HIDMT_TAP_LEFT;
		motion->tap = ptr->active && ptr->tap_candidate &&
		    (tap_flags & allowed) != 0 && !frame->buttons &&
		    now >= ptr->started && now - ptr->started <= 250000;
		if (motion->tap)
			motion->tap_button = tap_fingers >= 2 ? 4u : 1u;
		memset(ptr, 0, sizeof(*ptr));
		return;
	}
	contact = &frame->contacts[0];
	for (i = 0; i < frame->ncontacts; i++)
		if (ptr->active && frame->contacts[i].id == ptr->id) {
			contact = &frame->contacts[i];
			break;
		}
	same_contact = ptr->active && ptr->id == contact->id;
	if (!ptr->active) {
		memset(ptr, 0, sizeof(*ptr));
		ptr->active = true;
		/*
		 * HIDMT_TAP_LEFT: one-finger tap → left.
		 * HIDMT_TAP_RIGHT: two-finger tap → right. A right-only
		 * candidate may start on one finger and grow to two.
		 * More than two contacts are gestures, not taps.
		 */
		ptr->tap_candidate = !frame->buttons && !frame->cancel_tap &&
		    frame->ncontacts >= 1 && frame->ncontacts <= 2 &&
		    ((want_left && frame->ncontacts == 1) ||
			(want_right && frame->ncontacts <= 2));
		ptr->tap_fingers = frame->ncontacts;
		ptr->id = contact->id;
		ptr->x = ptr->startx = contact->x;
		ptr->y = ptr->starty = contact->y;
		ptr->started = now;
		return;
	}
	if (!same_contact) {
		/*
		 * Fingers leave the pad asynchronously. Keep the tap
		 * candidate and finger count when the tracked contact ends
		 * and another contact from the same gesture remains.
		 */
		ptr->id = contact->id;
		ptr->x = contact->x;
		ptr->y = contact->y;
		ptr->startx = contact->x;
		ptr->starty = contact->y;
		ptr->remx = ptr->remy = 0;
		if (frame->ncontacts > 2 || frame->buttons ||
		    frame->cancel_tap)
			ptr->tap_candidate = false;
		else if (frame->ncontacts > ptr->tap_fingers)
			ptr->tap_fingers = frame->ncontacts;
		return;
	}
	fingers = frame->ncontacts;
	if (frame->ncontacts > 2 || frame->buttons ||
	    abs(contact->x - ptr->startx) > 50 ||
	    abs(contact->y - ptr->starty) > 50)
		ptr->tap_candidate = false;
	else if (fingers > ptr->tap_fingers)
		ptr->tap_fingers = fingers;
	dx = contact->x - ptr->x + ptr->remx;
	dy = ptr->y - contact->y + ptr->remy;
	motion->dx = dx / 8;
	motion->dy = dy / 8;
	ptr->remx = dx % 8;
	ptr->remy = dy % 8;
	ptr->x = contact->x;
	ptr->y = contact->y;
}

/* Contact distance (sum of two contacts, i.e. twice the mean) to start. */
#define HIDMT_SCROLL_START	40

void
hidmt_pointer_update(struct hidmt_pointer *ptr, const struct hidmt_frame *frame,
    uint64_t now, unsigned tap_flags, struct hidmt_motion *motion)
{
	hidmt_pointer_base(ptr, frame, now, tap_flags, motion);
}

/*
 * Pointer policy with two-finger scrolling. With exactly two contacts the
 * pointer does not move; once the mean position has travelled
 * HIDMT_SCROLL_START the gesture becomes a scroll and stops being a tap
 * candidate. The scroll stays latched until every finger is lifted, so the
 * remaining finger does not jerk the pointer.
 */
void
hidmt_pointer_update_scroll(struct hidmt_pointer *ptr,
    const struct hidmt_frame *frame, uint64_t now, unsigned tap_flags,
    const struct hidmt_scroll *sc, struct hidmt_motion *motion)
{
	const struct hidmt_contact *a, *b;
	int sumx, sumy, dx, dy, den;
	int64_t acc;
	bool same;

	hidmt_pointer_base(ptr, frame, now, tap_flags, motion);
	if (sc == NULL || !sc->enable || sc->scale == 0 || sc->range <= 0 ||
	    frame->ncontacts == 0)
		return;
	if (frame->ncontacts != 2) {
		if (frame->ncontacts == 1 && ptr->scrolling)
			motion->dx = motion->dy = 0;
		ptr->scroll_valid = false;
		return;
	}
	motion->dx = motion->dy = 0;
	ptr->remx = ptr->remy = 0;
	if (frame->buttons)
		return;
	a = &frame->contacts[0];
	b = &frame->contacts[1];
	sumx = a->x + b->x;
	sumy = a->y + b->y;
	same = ptr->scroll_valid &&
	    ((ptr->scroll_id[0] == a->id && ptr->scroll_id[1] == b->id) ||
	    (ptr->scroll_id[0] == b->id && ptr->scroll_id[1] == a->id));
	if (!same) {
		ptr->scroll_valid = true;
		ptr->scroll_id[0] = a->id;
		ptr->scroll_id[1] = b->id;
		ptr->scroll_x = ptr->scroll_startx = sumx;
		ptr->scroll_y = ptr->scroll_starty = sumy;
		ptr->scroll_remx = ptr->scroll_remy = 0;
		return;
	}
	if (!ptr->scrolling) {
		if (abs(sumx - ptr->scroll_startx) < HIDMT_SCROLL_START &&
		    abs(sumy - ptr->scroll_starty) < HIDMT_SCROLL_START)
			return;
		ptr->scrolling = true;
		ptr->tap_candidate = false;
		ptr->scroll_x = sumx;
		ptr->scroll_y = sumy;
		return;
	}
	/* Touch Y grows downwards; positive output means fingers moved up. */
	dx = sumx - ptr->scroll_x;
	dy = ptr->scroll_y - sumy;
	ptr->scroll_x = sumx;
	ptr->scroll_y = sumy;
	if (sc->natural) {
		dx = -dx;
		dy = -dy;
	}
	/* The sums are twice the mean, hence the factor two in the divisor. */
	den = 2 * sc->range;
	acc = (int64_t)dx * sc->scale + ptr->scroll_remx;
	motion->scroll_x = (int)(acc / den);
	ptr->scroll_remx = (int)(acc % den);
	acc = (int64_t)dy * sc->scale + ptr->scroll_remy;
	motion->scroll_y = (int)(acc / den);
	ptr->scroll_remy = (int)(acc % den);
}

static void
hidmt_gesture_quiet(struct hidmt_motion *motion)
{
	motion->dx = motion->dy = 0;
	motion->scroll_x = motion->scroll_y = 0;
	motion->tap = false;
	motion->tap_button = 0;
}

/* Integer Euclidean distance. Clamp hostile coordinates before squaring. */
static uint64_t
hidmt_distance(const struct hidmt_contact *a, const struct hidmt_contact *b)
{
	int64_t x = (int64_t)a->x - b->x, y = (int64_t)a->y - b->y;
	uint64_t n, root = 0, bit = (uint64_t)1 << 62;

	if (x < -INT_MAX)
		x = -INT_MAX;
	if (x > INT_MAX)
		x = INT_MAX;
	if (y < -INT_MAX)
		y = -INT_MAX;
	if (y > INT_MAX)
		y = INT_MAX;
	n = (uint64_t)(x * x) + (uint64_t)(y * y);
	while (bit > n)
		bit >>= 2;
	while (bit != 0) {
		if (n >= root + bit) {
			n -= root + bit;
			root = (root >> 1) + bit;
		} else
			root >>= 1;
		bit >>= 2;
	}
	return root;
}

/* Called only for two contacts, before a gesture has been latched. */
static void
hidmt_pinch_candidate(struct hidmt_gstate *gs,
    const struct hidmt_frame *frame, struct hidmt_motion *motion)
{
	const struct hidmt_contact *a = &frame->contacts[0];
	const struct hidmt_contact *b = &frame->contacts[1];
	int64_t x = (int64_t)a->x + b->x, y = (int64_t)a->y + b->y;
	uint64_t distance = hidmt_distance(a, b), change, travel;
	bool same;

	if (gs->two_scroll)
		return;
	same = gs->two_valid &&
     ((gs->two_id[0] == a->id && gs->two_id[1] == b->id) ||
      (gs->two_id[0] == b->id && gs->two_id[1] == a->id));
	if (frame->buttons || frame->cancel_tap || (gs->two_valid && !same)) {
		gs->two_scroll = true;
		return;
	}
	if (!gs->two_valid) {
		gs->two_valid = true;
		gs->two_id[0] = a->id;
		gs->two_id[1] = b->id;
		gs->two_x = x;
		gs->two_y = y;
		gs->two_distance = distance;
		return;
	}
	/* Zero separation is not a usable scale origin. */
	if (gs->two_distance == 0) {
		gs->two_scroll = true;
		return;
	}
	change = distance > gs->two_distance ? distance - gs->two_distance :
     gs->two_distance - distance;
	travel = (uint64_t)(x > gs->two_x ? x - gs->two_x : gs->two_x - x);
	travel += (uint64_t)(y > gs->two_y ? y - gs->two_y : gs->two_y - y);
	if (change >= HIDMT_SCROLL_START && change * 2 > travel) {
		gs->latched = gs->started = true;
		gs->fingers = 2;
		gs->pinch_scale = 65536;
		motion->gesture |= HIDMT_GESTURE_BEGIN;
		motion->gesture_fingers = 2;
	} else if (travel >= HIDMT_SCROLL_START)
		gs->two_scroll = true;
}

static void
hidmt_pinch_update(struct hidmt_gstate *gs,
    const struct hidmt_frame *frame, struct hidmt_motion *motion)
{
	const struct hidmt_contact *a = &frame->contacts[0];
	const struct hidmt_contact *b = &frame->contacts[1];
	uint64_t scale;
	bool same;

	hidmt_gesture_quiet(motion);
	if (!gs->started)
		return;
	same = frame->ncontacts == 2 &&
     ((gs->two_id[0] == a->id && gs->two_id[1] == b->id) ||
      (gs->two_id[0] == b->id && gs->two_id[1] == a->id));
	if (!same || frame->buttons || frame->cancel_tap) {
		motion->gesture |= frame->ncontacts < 2 ? HIDMT_GESTURE_END :
      HIDMT_GESTURE_CANCEL;
		gs->started = false;
		return;
	}
	scale = hidmt_distance(a, b) * 65536 / gs->two_distance;
	if (scale == 0)
		scale = 1;
	if (scale > INT_MAX)
		scale = INT_MAX;
	if ((int)scale != gs->pinch_scale) {
		gs->pinch_scale = (int)scale;
		motion->gesture_scale = (int)scale;
		motion->gesture |= HIDMT_GESTURE_UPDATE;
	}
}

/*
 * Three- and four-finger gesture stage; call after the pointer policy with
 * the same frame. Contacts 3 and 4 never move the pointer or synthesise a
 * tap, and the pointer stays still until every finger is lifted, so the
 * last finger does not jerk it. A gesture begins once the mean of the
 * contacts has travelled gc->start; BEGIN and the first UPDATE come in one
 * frame, so the start distance is not lost. Fewer fingers or all lifted end
 * the gesture (END); more fingers cancel it (CANCEL), a new candidate then
 * starts from the new position. A gesture that never reached the threshold
 * produces no events.
 */
void
hidmt_gesture_update(struct hidmt_gstate *gs, const struct hidmt_gconf *gc,
    const struct hidmt_frame *frame, struct hidmt_motion *motion)
{
	unsigned i, n = frame->ncontacts;
	int sumx = 0, sumy = 0, x, y, dx, dy, start;

	if (gc == NULL || !gc->enable) {
		/*
		 * Switched off: a started swipe is cancelled and a candidate
		 * that has not started is forgotten too, so that re-enabling
		 * does not resume from stale state.
		 */
		if (gs->started)
			motion->gesture |= HIDMT_GESTURE_CANCEL;
		memset(gs, 0, sizeof(*gs));
		return;
	}
	if (n == 0) {
		if (gs->started)
			motion->gesture |= HIDMT_GESTURE_END;
		if (gs->latched)
			hidmt_gesture_quiet(motion);
		memset(gs, 0, sizeof(*gs));
		return;
	}
	if (gs->latched && gs->fingers == 2) {
		hidmt_pinch_update(gs, frame, motion);
		return;
	}
	if (n == 2 && !gs->latched) {
		hidmt_pinch_candidate(gs, frame, motion);
		if (gs->started)
			hidmt_pinch_update(gs, frame, motion);
		return;
	}
	if (n != 2) {
		gs->two_valid = false;
	}
	if (n < 3 || n > 4) {
		if (!gs->latched)
			return;
		if (gs->started)
			motion->gesture |= n < 3 ? HIDMT_GESTURE_END :
			    HIDMT_GESTURE_CANCEL;
		gs->started = false;
		gs->fingers = 0;
		hidmt_gesture_quiet(motion);
		return;
	}
	for (i = 0; i < n; i++) {
		sumx += frame->contacts[i].x;
		sumy += frame->contacts[i].y;
	}
	x = sumx / (int)n;
	y = sumy / (int)n;
	gs->latched = true;
	hidmt_gesture_quiet(motion);
	if (gs->fingers != n) {
		if (gs->started)
			motion->gesture |= HIDMT_GESTURE_CANCEL;
		gs->started = false;
		gs->fingers = n;
		gs->startx = gs->lastx = x;
		gs->starty = gs->lasty = y;
		gs->remx = gs->remy = 0;
		return;
	}
	if (!gs->started) {
		start = gc->start > 0 ? gc->start : HIDMT_GESTURE_START;
		if (abs(x - gs->startx) < start && abs(y - gs->starty) < start)
			return;
		gs->started = true;
		motion->gesture |= HIDMT_GESTURE_BEGIN;
		motion->gesture_fingers = n;
	}
	/* Contact Y grows downwards; positive output means fingers moved up. */
	dx = x - gs->lastx + gs->remx;
	dy = gs->lasty - y + gs->remy;
	gs->lastx = x;
	gs->lasty = y;
	motion->gesture_dx = dx / 8;
	motion->gesture_dy = dy / 8;
	gs->remx = dx % 8;
	gs->remy = dy % 8;
	if (motion->gesture_dx != 0 || motion->gesture_dy != 0)
		motion->gesture |= HIDMT_GESTURE_UPDATE;
}

/*
 * Common reset on lifecycle events (disable, suspend, resume, enable) and on
 * a mode change (hybrid, scroll, gestures): pointer and gesture state are
 * dropped.  Returns HIDMT_GESTURE_CANCEL when a swipe had begun, so the
 * caller can tell the consumer.  hidmt_release() leaves the assembler alone
 * (a frame may just have started in it); hidmt_reset() drops its frame in
 * progress as well, keeping counters and Scan Time history.
 */
unsigned
hidmt_release(struct hidmt_pointer *ptr, struct hidmt_gstate *gs)
{
	unsigned r = gs->started ? HIDMT_GESTURE_CANCEL : 0;

	memset(ptr, 0, sizeof(*ptr));
	memset(gs, 0, sizeof(*gs));
	return r;
}

unsigned
hidmt_reset(struct hidmt_pointer *ptr, struct hidmt_assembler *as,
    struct hidmt_gstate *gs)
{
	/*
	 * Forget the frame in progress only; the Scan Time history and the
	 * counters stay, they classify late tails of dropped frames.
	 */
	as->pending = as->lost = false;
	as->expected = as->have = 0;
	as->started = 0;
	memset(&as->frame, 0, sizeof(as->frame));
	return hidmt_release(ptr, gs);
}

/* Is there state that a missing report would leave hanging? */
bool
hidmt_live(const struct hidmt_pointer *ptr, const struct hidmt_assembler *as,
    const struct hidmt_gstate *gs)
{
	return ptr->active || ptr->tap_candidate || as->pending ||
	    gs->latched || gs->started;
}

/*
 * Timer rule for a device that has gone quiet.  now and last are the time
 * of this check and of the last report (microseconds).  A hybrid frame that
 * did not get its remaining reports within HIDMT_HYBRID_TIMEOUT_US is
 * dropped, and a frame lost this way (or earlier, as->lost) means the
 * contacts can no longer be tracked: everything is released.  Contacts that
 * stay silent for HIDMT_STALL_US are released too.  A release never
 * synthesises a tap.  Returns HIDMT_GESTURE_CANCEL when a swipe had begun
 * and sets *released when anything was dropped.  A touchpad that reports
 * continuously while a finger rests is assumed (Microsoft Precision
 * Touchpad); to be confirmed by hw.imt0.rawlog on the hardware.
 */
unsigned
hidmt_stall(struct hidmt_pointer *ptr, struct hidmt_assembler *as,
    struct hidmt_gstate *gs, uint64_t now, uint64_t last, bool *released)
{
	bool silent = now >= last && now - last >= HIDMT_STALL_US;

	*released = false;
	if (as->pending && (now < as->started ||
	    now - as->started > HIDMT_HYBRID_TIMEOUT_US)) {
		as->pending = false;
		as->drops++;
		as->lost = true;
		as->have_dropped = true;
		as->dropped_scantime = as->scantime;
	}
	if (!as->lost && !(silent && hidmt_live(ptr, as, gs)))
		return 0;
	as->lost = false;
	*released = true;
	return hidmt_reset(ptr, as, gs);
}
