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
	/* Optional Scan Time; size is zero when the descriptor has none. */
	struct hid_location scantime;
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
	unsigned id, tap_fingers;
	int x, y, startx, starty, remx, remy;
	uint64_t started;
	/* Two-finger scroll state; cleared with the rest on release. */
	bool scroll_valid, scrolling;
	unsigned scroll_id[2];
	int scroll_x, scroll_y;		/* sum of both contacts, last frame */
	int scroll_startx, scroll_starty;
	int scroll_remx, scroll_remy;
};

/*
 * Assembler for hybrid Precision Touchpad frames: the first report of a
 * frame carries Contact Count N > nslots, the following reports of the same
 * frame carry Contact Count 0 (and the same Scan Time) with the remaining
 * contacts.
 */
struct hidmt_assembler {
	bool pending, have_scantime, have_dropped;
	unsigned expected, have;
	unsigned scantime;		/* Scan Time of the pending frame */
	unsigned last_scantime;		/* Scan Time of the previous report */
	unsigned dropped_scantime;	/* Scan Time of the last dropped frame */
	uint64_t started;
	struct hidmt_frame frame;
	unsigned drops;			/* frames dropped by the assembler */
};

#define HIDMT_ASM_READY	0	/* *frame holds a complete frame */
#define HIDMT_ASM_MORE	1	/* report absorbed, need more reports */
#define HIDMT_ASM_ERROR	(-1)	/* malformed report, state reset */

/* An incomplete hybrid frame older than this (microseconds) is dropped. */
#define HIDMT_HYBRID_TIMEOUT_US	50000

/* Two-finger scroll parameters; all-zero means scrolling is off. */
struct hidmt_scroll {
	bool enable, natural;
	/* Precision-scroll units for a swipe over the whole pad height. */
	int scale;
	/* Pad height in contact units (maxy - miny). */
	int range;
};

struct hidmt_motion {
	int dx, dy;
	unsigned buttons;
	bool tap;
	/* wscons button bit: 1 = left, 4 = right */
	unsigned tap_button;
	/* Precision scroll units for wsmouse_precision_scroll(). */
	int scroll_x, scroll_y;
	/*
	 * Three/four-finger gesture (hidmt_gesture_update()): a mask of
	 * HIDMT_GESTURE_* phases in the order END/CANCEL, BEGIN, UPDATE;
	 * gesture_fingers is valid with BEGIN, gesture_dx/dy (pointer units,
	 * dy positive upwards) with UPDATE.
	 */
	unsigned gesture;
	unsigned gesture_fingers;
	int gesture_dx, gesture_dy;
};

#define HIDMT_GESTURE_BEGIN	0x01u
#define HIDMT_GESTURE_UPDATE	0x02u
#define HIDMT_GESTURE_END	0x04u
#define HIDMT_GESTURE_CANCEL	0x08u

/* Mean contact travel (contact units) before a gesture begins. */
#define HIDMT_GESTURE_START	100

struct hidmt_gconf {
	bool enable;
	int start;			/* 0 means HIDMT_GESTURE_START */
};

struct hidmt_gstate {
	bool latched;			/* 3+ contacts seen, until all lift */
	bool started;			/* BEGIN has been sent */
	unsigned fingers;		/* 3 or 4 while a candidate exists */
	int startx, starty, lastx, lasty, remx, remy;
};

/* Tap synthesis flags for hidmt_pointer_update(). */
#define HIDMT_TAP_LEFT	0x01u	/* one-finger tap → left button */
#define HIDMT_TAP_RIGHT	0x02u	/* two-finger tap → right button */

int hidmt_parse(const void *, int, struct hidmt_config *);
int hidmt_decode(const struct hidmt_config *, const uint8_t *, unsigned,
    struct hidmt_frame *);
int hidmt_assemble(const struct hidmt_config *, struct hidmt_assembler *,
    const uint8_t *, unsigned, uint64_t, struct hidmt_frame *);
void hidmt_pointer_update(struct hidmt_pointer *, const struct hidmt_frame *,
    uint64_t, unsigned, struct hidmt_motion *);
void hidmt_pointer_update_scroll(struct hidmt_pointer *,
    const struct hidmt_frame *, uint64_t, unsigned,
    const struct hidmt_scroll *, struct hidmt_motion *);
void hidmt_gesture_update(struct hidmt_gstate *, const struct hidmt_gconf *,
    const struct hidmt_frame *, struct hidmt_motion *);

#endif
