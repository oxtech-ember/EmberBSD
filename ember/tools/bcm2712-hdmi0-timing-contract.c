/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hdmi0var.h"
#include "hdmi0.h"

static unsigned int checks;
#define CHECK(c) do { \
	checks++; \
	if (!(c)) { \
		fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); \
		exit(1); \
	} \
} while (0)

static void
check_update(const struct bcm2712_hdmi0_update *update, uint32_t clear,
    uint32_t set)
{
	static const uint32_t old[] = {
		0, UINT32_MAX, UINT32_C(0xaaaaaaaa), UINT32_C(0x55555555),
		UINT32_C(0xdeadbeef),
	};
	uint32_t result;
	unsigned int i;

	CHECK(update->clear_mask == clear);
	CHECK(update->set_bits == set);
	for (i = 0; i < sizeof(old) / sizeof(old[0]); i++) {
		result = (old[i] & ~update->clear_mask) | update->set_bits;
		CHECK(result == ((old[i] & ~clear) | set));
		CHECK(((result ^ old[i]) & ~(clear | set)) == 0);
		CHECK((result & set) == set);
	}
}

static void
check_payload(const struct bcm2712_hdmi0_timing *words)
{

	/* Independent fixed-profile literals, not a hardware execution oracle. */
	CHECK(words->horza == UINT32_C(0x0058c780));
	CHECK(words->horzb == UINT32_C(0x0094002c));
	CHECK(words->verta0 == UINT32_C(0x05040438));
	CHECK(words->verta1 == UINT32_C(0x05040438));
	CHECK(words->vertb0 == UINT32_C(0x00000024));
	CHECK(words->vertb1 == UINT32_C(0x044c0024));
	CHECK((words->horza & UINT32_C(0xc000)) == UINT32_C(0xc000));
	CHECK((words->horza & UINT32_C(0x3fff)) == 1920);
	CHECK((words->vertb1 >> 16) == 1100); /* Nonzero for progressive. */
	CHECK((words->horza >> 16) + (words->horza & 0x3fffU) +
	    (words->horzb >> 16) + (words->horzb & 0x7ffU) == 2200);
	CHECK((words->verta0 >> 24) + ((words->verta0 >> 16) & 0x7fU) +
	    (words->verta0 & 0x1fffU) + words->vertb0 == 1125);
	check_update(&words->scheduler, 0, UINT32_C(0x8020));
	check_update(&words->deep_color, UINT32_C(0x70f), UINT32_C(0x200));
	check_update(&words->gcp_word, UINT32_C(0xffff), UINT32_C(0x10));
	check_update(&words->gcp_config, 0, UINT32_C(0x80000000));
	check_update(&words->misc_control, UINT32_C(0xf), 0);
	CHECK((words->scheduler.set_bits & 1U) == 0); /* No MODE_HDMI. */
	CHECK((words->deep_color.set_bits & 15U) == 0); /* RGB8. */
	CHECK(words->clock_stop == 0); /* Future whole-word command only. */
}

int
main(void)
{
	struct {
		uint32_t before[2];
		struct bcm2712_hdmi0_timing words;
		uint32_t after[2];
	} output;
	struct bcm2712_hdmi0_timing saved;
	unsigned int i;

	CHECK(bcm2712_hdmi0_encode_1080p60(NULL) == EINVAL);
	for (i = 0; i < 3; i++) {
		memset(&output, i == 0 ? 0 : i == 1 ? 0xa5 : 0xff,
		    sizeof(output));
		output.before[0] = UINT32_C(0x12345678);
		output.before[1] = UINT32_C(0x9abcdef0);
		output.after[0] = UINT32_C(0xfedcba98);
		output.after[1] = UINT32_C(0x76543210);
		CHECK(bcm2712_hdmi0_encode_1080p60(&output.words) == 0);
		check_payload(&output.words);
		CHECK(output.before[0] == UINT32_C(0x12345678));
		CHECK(output.before[1] == UINT32_C(0x9abcdef0));
		CHECK(output.after[0] == UINT32_C(0xfedcba98));
		CHECK(output.after[1] == UINT32_C(0x76543210));
		saved = output.words;
		CHECK(bcm2712_hdmi0_encode_1080p60(NULL) == EINVAL);
		CHECK(memcmp(&output.words, &saved, sizeof(saved)) == 0);
		CHECK(bcm2712_hdmi0_encode_1080p60(&output.words) == 0);
		CHECK(memcmp(&output.words, &saved, sizeof(saved)) == 0);
	}
	printf("HDMI0 timing encoder: %u checks PASS; source only, no hardware claim\n",
	    checks);
	return 0;
}
