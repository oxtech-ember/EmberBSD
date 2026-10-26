/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD Precision Touchpad feature-report safety metadata. */
#ifndef _DEV_HID_HIDMTVAR_H_
#define _DEV_HID_HIDMTVAR_H_

#include <dev/hid/hid.h>

#define HIDMT_MAX_CONTACTS 10

struct hidmt_slot {
	struct hid_location x, y, tip, confidence, id;
};

struct hidmt_config {
	int input_id, config_id;
	unsigned input_size, config_size, nslots;
	bool config_mode_only;
	int minx, maxx, miny, maxy;
	struct hid_location mode, count, button;
	struct hidmt_slot slots[HIDMT_MAX_CONTACTS];
};

struct hidmt_contact {
	int x, y;
	unsigned id;
};

struct hidmt_frame {
	unsigned ncontacts, buttons;
	bool cancel_tap;
	struct hidmt_contact contacts[HIDMT_MAX_CONTACTS];
};

struct hidmt_pointer {
	bool active, tap_candidate;
	unsigned id;
	int x, y, startx, starty, remx, remy;
	uint64_t started;
};

struct hidmt_motion {
	int dx, dy;
	unsigned buttons;
	bool tap;
};

int hidmt_parse(const void *, int, struct hidmt_config *);
int hidmt_decode(const struct hidmt_config *, const uint8_t *, unsigned,
    struct hidmt_frame *);
void hidmt_pointer_update(struct hidmt_pointer *, const struct hidmt_frame *,
    uint64_t, bool, struct hidmt_motion *);

#endif
