/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pv0var.h"
#include "pv0.h"

static unsigned int checks;
#define CHECK(c) do { \
	checks++; \
	if (!(c)) { \
		fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); \
		exit(1); \
	} \
} while (0)

static void
check_payload(const struct bcm2712_pv0_timing *words)
{

	/* Independent literals from the pinned PV0 profile and register facts. */
	CHECK(words->control == UINT32_C(0x00177002));
	CHECK(words->v_control == UINT32_C(0x20000002));
	CHECK(words->vsyncd_even == 0);
	CHECK(words->horza == UINT32_C(0x0094002c));
	CHECK(words->horzb == UINT32_C(0x00580780));
	CHECK(words->verta == UINT32_C(0x00240005));
	CHECK(words->vertb == UINT32_C(0x00040438));
	CHECK(words->mux_cfg == UINT32_C(0x20));
	CHECK(words->pipe_init_ctrl == UINT32_C(0x111));
	CHECK((words->control & 1U) == 0); /* PV.EN */
	CHECK((words->v_control & 1U) == 0); /* VIDEN */
	CHECK(((words->control >> 2) & 3U) == 0); /* BCM2712 HDMI0 */
	CHECK(((words->control >> 15) & 63U) == 46);
	CHECK(((words->control >> 21) & 7U) == 0); /* RGB24 */
	CHECK((words->control & 2U) != 0); /* Future FIFO clear command */
	CHECK((words->pipe_init_ctrl & 1U) != 0); /* Pipe init only */
	CHECK((words->horza >> 16) + (words->horza & 0xffffU) +
	    (words->horzb >> 16) + (words->horzb & 0xffffU) == 2200);
	CHECK((words->verta >> 16) + (words->verta & 0xffffU) +
	    (words->vertb >> 16) + (words->vertb & 0xffffU) == 1125);
}

int
main(void)
{
	struct {
		uint64_t before;
		struct bcm2712_pv0_timing words;
		uint64_t after;
	} output;
	unsigned int i;

	CHECK(bcm2712_pv0_encode_1080p60(NULL) == EINVAL);
	for (i = 0; i < 3; i++) {
		memset(&output, i == 0 ? 0 : i == 1 ? 0x80 : 0xff,
		    sizeof(output));
		output.before = UINT64_C(0x123456789abcdef0);
		output.after = UINT64_C(0xfedcba9876543210);
		CHECK(bcm2712_pv0_encode_1080p60(&output.words) == 0);
		check_payload(&output.words);
		CHECK(output.before == UINT64_C(0x123456789abcdef0));
		CHECK(output.after == UINT64_C(0xfedcba9876543210));
		CHECK(bcm2712_pv0_encode_1080p60(NULL) == EINVAL);
		check_payload(&output.words);
	}
	printf("PV0 timing encoder: %u checks PASS; source only, no hardware claim\n",
	    checks);
	return 0;
}
