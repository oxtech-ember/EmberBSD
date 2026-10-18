/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hvsvar.h"
#include "hvs.h"

static unsigned int checks;
#define CHECK(c) do { \
	checks++; \
	if (!(c)) { \
		fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); \
		exit(1); \
	} \
} while (0)

static void
frame_init(struct bcm2712_hvs_frame *frame)
{

	memset(frame, 0, sizeof(*frame));
	frame->variant = BCM2712_HVS_C0;
	frame->dma_base = UINT64_C(0xa12345000);
	frame->dma_span = UINT64_C(0x7e9000);
	frame->upm_offset = 0x10000;
	frame->upm_length = 0x3d00;
	frame->upm_slot = 7;
}

static void
run_case(const struct bcm2712_hvs_frame *input, int error, uint32_t ptr0,
    uint32_t ptr1)
{
	/* Literal facts independently reviewed against the pinned sources. */
	const uint32_t c0[10] = { 0x4900c007, 0, 0x4000fff0, 0x0437077f,
	    0xc0c0c0c0, 0, 0, 0x1e00, 0x80000000, 0x80000000 };
	const uint32_t d0[10] = { 0x490cc007, 0, 0x0000fff0, 0x0437077f,
	    0xc0c0c0c0, 0, 0, 0x1e00, 0x80000000, 0x80000000 };
	struct {
		uint64_t before;
		struct bcm2712_hvs_dlist dlist;
		uint64_t after;
	} output;
	unsigned char original[sizeof(output)], saved_input[sizeof(*input)];
	const uint32_t *expected;
	unsigned int poison, i;

	if (input != NULL)
		memcpy(saved_input, input, sizeof(saved_input));
	for (poison = 0; poison < 3; poison++) {
		memset(&output, poison == 0 ? 0 : poison == 1 ? 0x80 : 0xff,
		    sizeof(output));
		output.before = UINT64_C(0x123456789abcdef0);
		output.after = UINT64_C(0xfedcba9876543210);
		memcpy(original, &output, sizeof(output));
		CHECK(bcm2712_hvs_encode_1080p60(input, &output.dlist) == error);
		if (input != NULL)
			CHECK(memcmp(saved_input, input, sizeof(saved_input)) == 0);
		if (error != 0) {
			CHECK(memcmp(original, &output, sizeof(output)) == 0);
			continue;
		}
		expected = input->variant == BCM2712_HVS_C0 ? c0 : d0;
		for (i = 0; i < 10; i++)
			CHECK(output.dlist.words[i] ==
			    (i == 5 ? ptr0 : i == 6 ? ptr1 : expected[i]));
		CHECK(output.before == UINT64_C(0x123456789abcdef0));
		CHECK(output.after == UINT64_C(0xfedcba9876543210));
		memcpy(original, &output, sizeof(output));
		CHECK(bcm2712_hvs_encode_1080p60(input, &output.dlist) == 0);
		CHECK(memcmp(original, &output, sizeof(output)) == 0);
	}
}

int
main(void)
{
	struct bcm2712_hvs_frame input;

	CHECK(sizeof(struct bcm2712_hvs_dlist) == 40);
	CHECK(BCM2712_HVS_DLIST_WORDS == 10);
	CHECK(bcm2712_hvs_encode_1080p60(NULL, NULL) == EINVAL);
	run_case(NULL, EINVAL, 0, 0);
	frame_init(&input);
	run_case(&input, 0, 0x01001c0a, 0x12345000);
	input.variant = BCM2712_HVS_D0;
	run_case(&input, 0, 0x01001c0a, 0x12345000);
	input.dma_base = UINT64_C(0xf12345678);
	input.upm_offset = 256;
	input.upm_slot = 31;
	run_case(&input, 0, 0x00017c0f, 0x12345678);
	input.variant = BCM2712_HVS_C0;
	run_case(&input, 0, 0x00017c0f, 0x12345678);

	input.dma_base = 0;
	input.upm_offset = 0;
	input.upm_slot = 0;
	run_case(&input, 0, 0, 0);
	input.dma_base = 1; /* No invented base-alignment requirement. */
	run_case(&input, 0, 0, 1);
	input.dma_base = UINT64_C(0x100000000);
	run_case(&input, 0, 1, 0);
	input.dma_base = UINT64_C(0xfffff000); /* Frame crosses low32. */
	run_case(&input, 0, 0, 0xfffff000);
	input.dma_base = UINT64_C(0xfff817000);
	run_case(&input, 0, 0xf, 0xff817000); /* Exclusive end is 2^36. */
	input.dma_base--;
	input.dma_span++;
	run_case(&input, 0, 0xf, 0xff816fff);
	input.dma_base = 0;
	input.dma_span = UINT64_C(0x1000000000);
	run_case(&input, 0, 0, 0); /* Entire supplied span, not only frame. */

	frame_init(&input);
	input.upm_offset = 0x3c300;
	input.upm_slot = 31;
	run_case(&input, 0, 0x03c37c0a, 0x12345000);
	input.upm_offset = 0;
	input.upm_length = 0x40000;
	run_case(&input, 0, 0x00007c0a, 0x12345000);

#define BAD(field, value) do { \
	frame_init(&input); \
	input.field = (value); \
	run_case(&input, EINVAL, 0, 0); \
} while (0)
	BAD(dma_base, UINT64_C(0x1000000000));
	BAD(dma_base, UINT64_MAX);
	BAD(dma_base, UINT64_C(0xfff817001));
	BAD(dma_span, 0);
	BAD(dma_span, UINT64_C(0x7e8fff));
	BAD(dma_span, UINT64_MAX);
	BAD(dma_span, UINT64_C(0x1000000000));
	BAD(upm_offset, 1);
	BAD(upm_offset, 255);
	BAD(upm_offset, 257);
	BAD(upm_offset, 0x3c400);
	BAD(upm_offset, 0x40000);
	BAD(upm_offset, 0xffffff00U);
	BAD(upm_offset, UINT32_MAX);
	BAD(upm_length, 0);
	BAD(upm_length, 0x3c00);
	BAD(upm_length, 0x3cff);
	BAD(upm_length, 0x3d01);
	BAD(upm_length, 0x40000);
	BAD(upm_length, 0xffffff00U);
	BAD(upm_length, UINT32_MAX);
	BAD(upm_slot, 32);
	BAD(upm_slot, UINT32_MAX);
#undef BAD
	frame_init(&input);
	input.variant = (enum bcm2712_hvs_variant)0;
	run_case(&input, EOPNOTSUPP, 0, 0);
	input.variant = (enum bcm2712_hvs_variant)0x55;
	input.dma_base = UINT64_MAX;
	input.upm_slot = UINT32_MAX;
	run_case(&input, EOPNOTSUPP, 0, 0); /* Variant before metadata. */
	CHECK(bcm2712_hvs_encode_1080p60(&input, NULL) == EINVAL);
	input.variant = (enum bcm2712_hvs_variant)-1;
	run_case(&input, EOPNOTSUPP, 0, 0);
	frame_init(&input);
	CHECK(bcm2712_hvs_encode_1080p60(&input, NULL) == EINVAL);
	printf("HVS plane encoder: %u checks PASS; source only, no hardware claim\n",
	    checks);
	return 0;
}
