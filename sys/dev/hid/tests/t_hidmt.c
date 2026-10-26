/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD Precision Touchpad feature-report safety regression. */
#include <sys/types.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <dev/hid/hidmtvar.h>
#include "ptp_descriptor.h"

static size_t
find_bytes(const unsigned char *data, size_t size, const unsigned char *pattern,
    size_t pattern_size)
{
	size_t i;

	for (i = 0; i + pattern_size <= size; i++)
		if (memcmp(data + i, pattern, pattern_size) == 0)
			return i;
	return size;
}

size_t hidmt_test_allocations;

int
main(void)
{
	struct hidmt_config cfg;
	struct hidmt_frame frame = {0};
	struct hidmt_pointer ptr = {0};
	struct hidmt_motion motion;
	uint8_t input[64] = {0};

	assert(hidmt_parse(ptp_descriptor, sizeof(ptp_descriptor), &cfg) == 0);
	assert(cfg.input_id == 4 && cfg.config_id == 3);
	assert(cfg.config_mode_only);
	assert(cfg.nslots == 5 && cfg.input_size == 29);
	assert(cfg.maxx == 3684 && cfg.maxy == 2176);
	{
		unsigned char mixed[sizeof(ptp_descriptor)];
		struct hidmt_config mixed_cfg;
		static const unsigned char feature_id_5[] = { 0x85, 0x05 };
		size_t offset;

		memcpy(mixed, ptp_descriptor, sizeof(mixed));
		offset = find_bytes(mixed, sizeof(mixed), feature_id_5,
		    sizeof(feature_id_5));
		assert(offset < sizeof(mixed));
		mixed[offset + 1] = 0x03;
		assert(hidmt_parse(mixed, sizeof(mixed), &mixed_cfg) == 0);
		assert(!mixed_cfg.config_mode_only);
	}
	{
		static const unsigned char mode_field[] = {
		    0x85, 0x03, 0x09, 0x22, 0xa1, 0x02, 0x09, 0x52,
		    0x15, 0x00, 0x25, 0x0a, 0x75, 0x08, 0x95, 0x01,
		    0xb1, 0x02
		};
		unsigned char changed[sizeof(ptp_descriptor)];
		struct hidmt_config changed_cfg;
		size_t offset;

		/* An array feature field must never enable the direct SET path. */
		memcpy(changed, ptp_descriptor, sizeof(changed));
		offset = find_bytes(changed, sizeof(changed), mode_field,
		    sizeof(mode_field));
		assert(offset < sizeof(changed));
		changed[offset + sizeof(mode_field) - 1] = 0x00;
		assert(hidmt_parse(changed, sizeof(changed), &changed_cfg) == 0);
		assert(!changed_cfg.config_mode_only);

		/* Input Mode 3 is unavailable when the logical range ends at 2. */
		memcpy(changed, ptp_descriptor, sizeof(changed));
		offset = find_bytes(changed, sizeof(changed), mode_field,
		    sizeof(mode_field));
		assert(offset < sizeof(changed));
		changed[offset + 11] = 0x02;
		assert(hidmt_parse(changed, sizeof(changed), &changed_cfg) == 0);
		assert(!changed_cfg.config_mode_only);
	}
	{
		static const unsigned char vendor_id[] = {
		    0x85, 0x0e, 0x09, 0x01, 0x19
		};
		static const unsigned char extra_feature[] = {
		    0x85, 0x03, 0x09, 0x01, 0x75, 0x08, 0x95, 0x01,
		    0xb1, 0x02, 0x85, 0x0e
		};
		unsigned char changed[sizeof(ptp_descriptor) + sizeof(extra_feature)];
		size_t offset;

		memcpy(changed, ptp_descriptor, sizeof(ptp_descriptor));
		offset = find_bytes(changed, sizeof(ptp_descriptor), vendor_id,
		    sizeof(vendor_id));
		assert(offset < sizeof(ptp_descriptor));
		offset += 2;
		memmove(changed + offset + sizeof(extra_feature), changed + offset,
		    sizeof(ptp_descriptor) - offset);
		memcpy(changed + offset, extra_feature, sizeof(extra_feature));
		assert(hidmt_parse(changed, sizeof(changed), &cfg) == 0);
		assert(!cfg.config_mode_only);
	}
	/* An unsafe report reparse must clear a previously true classification. */
	{
		unsigned char changed[sizeof(ptp_descriptor)];
		static const unsigned char feature_id_5[] = { 0x85, 0x05 };
		size_t offset;

		memcpy(changed, ptp_descriptor, sizeof(changed));
		assert(hidmt_parse(ptp_descriptor, sizeof(ptp_descriptor), &cfg) == 0);
		assert(cfg.config_mode_only);
		offset = find_bytes(changed, sizeof(changed), feature_id_5,
		    sizeof(feature_id_5));
		assert(offset < sizeof(changed));
		changed[offset + 1] = 0x03;
		assert(hidmt_parse(changed, sizeof(changed), &cfg) == 0);
		assert(!cfg.config_mode_only);
	}
	/* First finger: confidence, tip, contact id 2; x=1000, y=800. */
	input[0] = 3 | (2 << 2);
	input[1] = 1000 & 255; input[2] = 1000 >> 8;
	input[3] = 800 & 255; input[4] = 800 >> 8;
	input[27] = 1;
	assert(hidmt_decode(&cfg, input, cfg.input_size, &frame) == 0);
	assert(frame.ncontacts == 1 && frame.contacts[0].id == 2);
	assert(frame.contacts[0].x == 1000 && frame.contacts[0].y == 800);
	hidmt_pointer_update(&ptr, &frame, 1000000, true, &motion);
	assert(motion.dx == 0 && motion.dy == 0 && !motion.tap);
	frame.ncontacts = 0;
	hidmt_pointer_update(&ptr, &frame, 1100000, true, &motion);
	assert(motion.tap && motion.buttons == 0);
	/* A duplicate release cannot synthesize another click. */
	hidmt_pointer_update(&ptr, &frame, 1101000, true, &motion);
	assert(!motion.tap);

	/* A long press and a drag must not turn into taps. */
	frame.ncontacts = 1;
	hidmt_pointer_update(&ptr, &frame, 2000000, true, &motion);
	frame.ncontacts = 0;
	hidmt_pointer_update(&ptr, &frame, 2200000, true, &motion);
	assert(!motion.tap);
	frame.ncontacts = 1;
	hidmt_pointer_update(&ptr, &frame, 3000000, true, &motion);
	frame.contacts[0].x += 80;
	hidmt_pointer_update(&ptr, &frame, 3030000, true, &motion);
	assert(motion.dx == 10 && motion.dy == 0);
	frame.ncontacts = 0;
	hidmt_pointer_update(&ptr, &frame, 3100000, true, &motion);
	assert(!motion.tap);
	/* Disabling the policy preserves pointer motion, without clicking. */
	frame.ncontacts = 1;
	hidmt_pointer_update(&ptr, &frame, 4000000, false, &motion);
	frame.ncontacts = 0;
	hidmt_pointer_update(&ptr, &frame, 4100000, false, &motion);
	assert(!motion.tap);
	/* Invalid report sizes and out-of-range contact counts are rejected. */
	assert(hidmt_decode(&cfg, input, cfg.input_size - 1, &frame) != 0);
	input[27] = 6;
	assert(hidmt_decode(&cfg, input, cfg.input_size, &frame) != 0);
	input[27] = 1;
	/* A finger that becomes a low-confidence palm cannot generate a tap. */
	assert(hidmt_decode(&cfg, input, cfg.input_size, &frame) == 0);
	hidmt_pointer_update(&ptr, &frame, 5000000, true, &motion);
	input[0] &= ~1U;
	assert(hidmt_decode(&cfg, input, cfg.input_size, &frame) == 0);
	hidmt_pointer_update(&ptr, &frame, 5050000, true, &motion);
	assert(!motion.tap);

	/* Enabling tapping in the middle of a contact must not click. */
	frame.ncontacts = 1; frame.cancel_tap = false;
	hidmt_pointer_update(&ptr, &frame, 6000000, false, &motion);
	frame.ncontacts = 0;
	hidmt_pointer_update(&ptr, &frame, 6100000, true, &motion);
	assert(!motion.tap);
	/* Duplicate IDs and coordinates outside the descriptor are rejected. */
	input[0] = 3 | (2 << 2);
	input[27] = 2;
	memcpy(&input[5], &input[0], 5);
	assert(hidmt_decode(&cfg, input, cfg.input_size, &frame) != 0);
	input[5] = 3 | (3 << 2);
	assert(hidmt_decode(&cfg, input, cfg.input_size, &frame) == 0);
	assert(frame.ncontacts == 2);
	input[27] = 1;
	input[1] = input[2] = 255;
	assert(hidmt_decode(&cfg, input, cfg.input_size, &frame) != 0);
	input[1] = 1000 & 255; input[2] = 1000 >> 8;
	/* Reported contact count zero explicitly releases a parallel frame. */
	input[27] = 0;
	assert(hidmt_decode(&cfg, input, cfg.input_size, &frame) == 0);
	assert(frame.ncontacts == 0);
	/* Keep the chosen contact when the report order changes. */
	frame.ncontacts = 2;
	frame.contacts[0] = (struct hidmt_contact){1000, 800, 2};
	frame.contacts[1] = (struct hidmt_contact){2000, 800, 3};
	hidmt_pointer_update(&ptr, &frame, 7000000, true, &motion);
	frame.contacts[0] = (struct hidmt_contact){2000, 800, 3};
	frame.contacts[1] = (struct hidmt_contact){1016, 800, 2};
	hidmt_pointer_update(&ptr, &frame, 7020000, true, &motion);
	assert(motion.dx == 2 && motion.dy == 0);
	frame.ncontacts = 0;
	hidmt_pointer_update(&ptr, &frame, 7100000, true, &motion);
	assert(!motion.tap);
	/* Physical button activity cancels synthetic clicks. */
	frame.ncontacts = 1; frame.buttons = 1;
	hidmt_pointer_update(&ptr, &frame, 8000000, true, &motion);
	assert(motion.buttons == 1);
	frame.buttons = 0;
	hidmt_pointer_update(&ptr, &frame, 8020000, true, &motion);
	frame.ncontacts = 0;
	hidmt_pointer_update(&ptr, &frame, 8100000, true, &motion);
	assert(!motion.tap);
	assert(hidmt_test_allocations == 0);
	puts("hidmt descriptor, bounds, motion, contacts, taps, buttons, palms: passed");
	return 0;
}
