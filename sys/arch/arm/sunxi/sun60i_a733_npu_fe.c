/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD, AI-assisted; independent VIP format and admission facts. */
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/bus.h>
#include <sys/systm.h>
#include <arm/sunxi/sun60i_a733_ccu.h>
#include <arm/sunxi/sun60i_a733_pck600.h>
#include <arm/sunxi/sun60i_a733_npu_fe.h>

int
sun60i_a733_npu_fe_snapshot(bus_space_tag_t bst, struct clk *clock,
    int power, const void *owner, struct sun60i_a733_npu_fe_snapshot *out)
{
	static const bus_size_t offsets[A733_NPU_FE_NREGS] = {
		0x000, 0x004, 0x014, 0x024, 0x030, 0x388,
		0x3a4, 0x3b4, 0x654, 0x664, 0x668, 0x66c
	};
	struct sun60i_a733_npu_fe_snapshot sample = { 0 };
	bus_space_handle_t bsh;
	int error;

	if (bst == NULL || clock == NULL || power <= 0 ||
	    owner == NULL || out == NULL)
		return EINVAL;
	error = sun60i_a733_ccu_npu_ready(clock, owner);
	if (error == 0)
		error = sun60i_a733_pck_npu_ready(power, owner);
	if (error != 0)
		return error;
	error = bus_space_map(bst, 0x03600000, 0x1000, 0, &bsh);
	if (error != 0)
		return error;
	/* Never read IRQ status 0x010: it belongs to future IRQ servicing. */
	for (size_t row = 0; row < 2; row++) {
		for (size_t i = 0; i < A733_NPU_FE_NREGS; i++) {
			if (bus_space_peek_4(bst, bsh, offsets[i],
			    &sample.words[row][i]) != 0) {
				error = EFAULT;
				goto unmap;
			}
		}
	}
unmap:
	bus_space_unmap(bst, bsh, 0x1000);
	if (error != 0)
		return error;
	error = sun60i_a733_ccu_npu_ready(clock, owner);
	if (error == 0)
		error = sun60i_a733_pck_npu_ready(power, owner);
	if (error != 0)
		return error;
	sample.valid = true;
	*out = sample;
	return 0;
}

int
sun60i_a733_npu_fe_admit(const struct sun60i_a733_npu_fe_snapshot *sample)
{
	const uint32_t *words;

	if (sample == NULL || !sample->valid)
		return EINVAL;
	if (memcmp(sample->words[0], sample->words[1],
	    sizeof(sample->words[0])) != 0)
		return EBUSY;
	words = sample->words[1];
	if (words[A733_NPU_FE_PID] != 0x1000003b ||
	    words[A733_NPU_FE_REVISION] != 0x9202)
		return ENODEV;
	/* BSP EXPECT_IDLE_VALUE_ALL_MODULE excludes work in every module. */
	if (words[A733_NPU_FE_IDLE] != 0x7fffffff ||
	    words[A733_NPU_FE_IRQ_ENABLE] != 0 ||
	    (words[A733_NPU_FE_MMU] & 0x21) != 0 ||
	    (words[A733_NPU_FE_COMMAND_CONTROL] & 0x10000) != 0)
		return EBUSY;
	return 0;
}

int
sun60i_a733_npu_fe_event_image(void *out, size_t bytes)
{
	/* BSP event(block bit6, IRQ_EVENT_ID4), then END. Format facts only. */
	static const uint32_t words[4] = { 0x08010e01, 0x44, 0x10000000, 0 };
	uint8_t *p = out;

	if (out == NULL || bytes < A733_NPU_FE_EVENT_BYTES ||
	    A733_NPU_FE_EVENT_BYTES > UINTPTR_MAX - (uintptr_t)out)
		return EINVAL;
	for (size_t i = 0; i < 4; i++) {
		for (size_t byte = 0; byte < 4; byte++)
			p[i * 4 + byte] = (uint8_t)(words[i] >> (byte * 8));
	}
	return 0;
}
