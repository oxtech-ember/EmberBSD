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

int
hidmt_decode(const struct hidmt_config *cfg, const uint8_t *data, unsigned len,
    struct hidmt_frame *frame)
{
	unsigned i, j, count;
	const struct hidmt_slot *slot;
	struct hidmt_contact *contact;

	memset(frame, 0, sizeof(*frame));
	if (len != cfg->input_size)
		return 1;
	count = hid_get_udata(data, &cfg->count);
	/* This transport accepts complete parallel frames, not hybrid frames. */
	if (count > cfg->nslots)
		return 1;
	frame->buttons = cfg->button.size && hid_get_udata(data, &cfg->button);
	for (i = 0; i < count; i++) {
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

void
hidmt_pointer_update(struct hidmt_pointer *ptr, const struct hidmt_frame *frame,
    uint64_t now, bool tapping, struct hidmt_motion *motion)
{
	const struct hidmt_contact *contact;
	unsigned i;
	int dx, dy;

	memset(motion, 0, sizeof(*motion));
	motion->buttons = frame->buttons;
	if (frame->cancel_tap)
		ptr->tap_candidate = false;
	if (frame->ncontacts == 0) {
		motion->tap = ptr->active && ptr->tap_candidate && tapping &&
		    !frame->buttons && now >= ptr->started &&
		    now - ptr->started <= 180000;
		memset(ptr, 0, sizeof(*ptr));
		return;
	}
	contact = &frame->contacts[0];
	for (i = 0; i < frame->ncontacts; i++)
		if (ptr->active && frame->contacts[i].id == ptr->id) {
			contact = &frame->contacts[i];
			break;
		}
	if (!ptr->active || ptr->id != contact->id) {
		bool was_active = ptr->active;
		memset(ptr, 0, sizeof(*ptr));
		ptr->active = true;
		ptr->tap_candidate = tapping && !was_active && frame->ncontacts == 1 &&
		    !frame->buttons && !frame->cancel_tap;
		ptr->id = contact->id;
		ptr->x = ptr->startx = contact->x;
		ptr->y = ptr->starty = contact->y;
		ptr->started = now;
		return;
	}
	if (frame->ncontacts != 1 || frame->buttons ||
	    abs(contact->x - ptr->startx) > 50 ||
	    abs(contact->y - ptr->starty) > 50)
		ptr->tap_candidate = false;
	dx = contact->x - ptr->x + ptr->remx;
	dy = ptr->y - contact->y + ptr->remy;
	motion->dx = dx / 8;
	motion->dy = dy / 8;
	ptr->remx = dx % 8;
	ptr->remy = dy % 8;
	ptr->x = contact->x;
	ptr->y = contact->y;
}
