/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD, AI-assisted; independent read-only FE admission controls. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

typedef void *bus_space_tag_t;
typedef unsigned long bus_space_handle_t;
typedef unsigned long bus_size_t;
struct clk { unsigned id; };
static unsigned checks;
#define CHECK(c) do { checks++; if (!(c)) { \
	fprintf(stderr, "check %u failed at line %d: %s\n", checks, __LINE__, #c); exit(1); \
} } while (0)
static struct clk clock1;
static const int cookie;
static unsigned proofs, maps, unmaps, reads, fail_proof, fail_read;
static bool fail_map;
static uint32_t samples[2][12];
static const unsigned offsets[12] = {
	0x000, 0x004, 0x014, 0x024, 0x030, 0x388,
	0x3a4, 0x3b4, 0x654, 0x664, 0x668, 0x66c
};
static int sun60i_a733_ccu_npu_ready(struct clk *clock, const void *owner)
{
	CHECK(clock == &clock1 && owner == &cookie && (proofs & 1) == 0);
	return ++proofs == fail_proof ? EIO : 0;
}
static int sun60i_a733_pck_npu_ready(int power, const void *owner)
{
	CHECK(power == 7 && owner == &cookie && (proofs & 1) == 1);
	return ++proofs == fail_proof ? EIO : 0;
}
static int bus_space_map(bus_space_tag_t tag, uint64_t address,
    size_t bytes, int flags, bus_space_handle_t *out)
{
	CHECK(tag == (void *)(uintptr_t)1 && address == 0x03600000);
	CHECK(bytes == 0x1000 && flags == 0 && proofs == 2); maps++;
	if (fail_map)
		return ENOMEM;
	*out = 3;
	return 0;
}
static void bus_space_unmap(bus_space_tag_t tag, bus_space_handle_t handle, size_t bytes)
{ CHECK(tag == (void *)(uintptr_t)1 && handle == 3 && bytes == 0x1000); unmaps++; }
static int bus_space_peek_4(bus_space_tag_t tag, bus_space_handle_t handle,
    bus_size_t offset, uint32_t *out)
{
	CHECK(tag == (void *)(uintptr_t)1 && handle == 3 && reads < 24);
	CHECK(offset != 0x10 && offset == offsets[reads % 12]);
	unsigned index = reads++;
	if (reads == fail_read) return 1;
	*out = samples[index / 12][index % 12]; return 0;
}
#include "sun60i_a733_npu_fe.h"
#include "sun60i_a733_npu_fe.c"
static void reset(void)
{
	proofs = maps = unmaps = reads = fail_proof = fail_read = 0; fail_map = false;
	for (unsigned i = 0; i < 12; i++) samples[0][i] = i * 0x1234;
	samples[0][1] = 0x7fffffff; samples[0][2] = 0; samples[0][3] = 0x9202;
	samples[0][4] = 0x1000003b; samples[0][5] = 0; samples[0][6] = 0;
	memcpy(samples[1], samples[0], sizeof(samples[0]));
}
static int snapshot(struct sun60i_a733_npu_fe_snapshot *out)
{
	return sun60i_a733_npu_fe_snapshot((void *)(uintptr_t)1, &clock1, 7, &cookie, out);
}
static void reject_snapshot(int error)
{
	struct sun60i_a733_npu_fe_snapshot out, saved;
	memset(&out, 0xa5, sizeof(out)); saved = out;
	CHECK(snapshot(&out) == error); CHECK(memcmp(&out, &saved, sizeof(out)) == 0);
}
static void admissions(void)
{
	struct sun60i_a733_npu_fe_snapshot out;
	reset(); CHECK(snapshot(&out) == 0);
	CHECK(out.valid && proofs == 4 && maps == 1 && unmaps == 1 && reads == 24);
	CHECK(memcmp(out.words, samples, sizeof(samples)) == 0);
	CHECK(sun60i_a733_npu_fe_admit(&out) == 0);
	out.valid = false; CHECK(sun60i_a733_npu_fe_admit(&out) == EINVAL);
	CHECK(sun60i_a733_npu_fe_admit(NULL) == EINVAL);
	for (unsigned i = 0; i < 12; i++) {
		reset(); samples[1][i] ^= 1; CHECK(snapshot(&out) == 0);
		CHECK(out.valid && sun60i_a733_npu_fe_admit(&out) == EBUSY);
	}
	for (unsigned bit = 0; bit < 31; bit++) {
		reset(); samples[0][1] &= ~(UINT32_C(1) << bit);
		samples[1][1] = samples[0][1]; CHECK(snapshot(&out) == 0);
		CHECK(sun60i_a733_npu_fe_admit(&out) == EBUSY);
	}
	const unsigned fields[] = { 2, 5, 5, 6 };
	const uint32_t values[] = { 1, 1, 0x20, 0x10000 };
	for (unsigned i = 0; i < 4; i++) {
		reset(); samples[0][fields[i]] = samples[1][fields[i]] = values[i];
		CHECK(snapshot(&out) == 0); CHECK(sun60i_a733_npu_fe_admit(&out) == EBUSY);
	}
	for (unsigned i = 3; i <= 4; i++) {
		reset(); samples[0][i] = samples[1][i] = 0;
		CHECK(snapshot(&out) == 0); CHECK(sun60i_a733_npu_fe_admit(&out) == ENODEV);
	}
}
static void failures(void)
{
	for (unsigned i = 1; i <= 4; i++) {
		reset(); fail_proof = i; reject_snapshot(EIO);
		CHECK(proofs == i && maps == (i > 2 ? 1u : 0u));
		CHECK(unmaps == maps && reads == (i > 2 ? 24u : 0u));
	}
	for (unsigned i = 1; i <= 24; i++) {
		reset(); fail_read = i; reject_snapshot(EFAULT);
		CHECK(reads == i && maps == 1 && unmaps == 1 && proofs == 2);
	}
	reset(); fail_map = true; reject_snapshot(ENOMEM);
	CHECK(maps == 1 && unmaps == 0 && reads == 0);
	reset(); struct sun60i_a733_npu_fe_snapshot out;
	CHECK(sun60i_a733_npu_fe_snapshot(NULL, &clock1, 7, &cookie, &out) == EINVAL);
	CHECK(sun60i_a733_npu_fe_snapshot((void *)(uintptr_t)1, NULL, 7, &cookie, &out) == EINVAL);
	CHECK(sun60i_a733_npu_fe_snapshot((void *)(uintptr_t)1, &clock1, 0, &cookie, &out) == EINVAL);
	CHECK(sun60i_a733_npu_fe_snapshot((void *)(uintptr_t)1, &clock1, 7, NULL, &out) == EINVAL);
	CHECK(sun60i_a733_npu_fe_snapshot((void *)(uintptr_t)1, &clock1, 7, &cookie, NULL) == EINVAL);
	CHECK(proofs == 0 && maps == 0 && reads == 0);
}
static void event(void)
{
	const uint8_t expected[16] = { 1, 14, 1, 8, 68, 0, 0, 0,
	    0, 0, 0, 16, 0, 0, 0, 0 };
	uint8_t bytes[24], saved[24]; memset(bytes, 0xa5, sizeof(bytes));
	memcpy(saved, bytes, sizeof(bytes));
	CHECK(sun60i_a733_npu_fe_event_image(NULL, 16) == EINVAL);
	CHECK(sun60i_a733_npu_fe_event_image(bytes, 15) == EINVAL);
	CHECK(memcmp(bytes, saved, sizeof(bytes)) == 0);
	CHECK(sun60i_a733_npu_fe_event_image(bytes + 1, 16) == 0);
	CHECK(memcmp(bytes + 1, expected, 16) == 0);
	CHECK(bytes[0] == 0xa5 && bytes[17] == 0xa5 && bytes[23] == 0xa5);
}
int main(void)
{
	failures(); admissions(); event();
	printf("A733 NPU FE admission: %u checks passed\n", checks); return 0;
}
