/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Real-source startup ordering and failure checks; no GPU emulation. */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sun60i_a733_gpu_fwinfo.h"

#define CHECK(c) do { \
	checks++; \
	if (!(c)) { \
		fprintf(stderr, "%d: %s\n", __LINE__, #c); \
		exit(1); \
	} \
} while (0)
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#define BUS_SPACE_BARRIER_READ 1
#define BUS_SPACE_BARRIER_WRITE 2
typedef void *bus_space_tag_t;
typedef uintptr_t bus_space_handle_t;
#include "start.h"

struct operation {
	bool write;
	unsigned width;
	uint32_t reg;
	uint64_t value;
};
static struct operation trace[64];
static uint64_t registers[8192];
static unsigned checks, ntrace, fail_at, ndelay, release_delay;
static bool fail_after_write, write_barrier_due, read_barrier_ready;
static uint32_t barrier_reg;
static unsigned barrier_width, nwrite, secure_prefix, reset2_extra;
static struct sun60i_a733_gpu_fwstart_result observed;

static int
access_register(bool write, unsigned width, uint32_t reg, uint64_t *value)
{
	CHECK(reg < 65536 && (reg & (width - 1)) == 0 && ntrace < 64);
	CHECK(width == 4 || width == 8);
	CHECK(!write_barrier_due);
	CHECK(!write || observed.touched);
	CHECK(observed.last_register == reg);
	if (write) {
		CHECK(!read_barrier_ready);
		nwrite++;
		CHECK(observed.writes == nwrite);
	} else {
		CHECK(read_barrier_ready && barrier_reg == reg && barrier_width == width);
		read_barrier_ready = false;
	}
	trace[ntrace++] = (struct operation){ write, width, reg, *value };
	if (ntrace == fail_at && !fail_after_write)
		return -1;
	if (write) {
		if (width == 4)
			registers[reg / 8] = (registers[reg / 8] &
			    UINT64_C(0xffffffff00000000)) | (uint32_t)*value;
		else
			registers[reg / 8] = *value;
		write_barrier_due = ntrace != fail_at;
		if (reg == 0x100 && *value == 0)
			release_delay = ndelay;
	} else {
		*value = registers[reg / 8];
	}
	return ntrace == fail_at ? -1 : 0;
}

static int
bus_space_poke_4(bus_space_tag_t t, bus_space_handle_t h, uint32_t reg, uint32_t v)
{
	uint64_t value = v;
	CHECK(t == &trace && h == 0);
	return access_register(true, 4, reg, &value);
}

static int
bus_space_poke_8(bus_space_tag_t t, bus_space_handle_t h, uint32_t reg, uint64_t v)
{
	CHECK(t == &trace && h == 0);
	return access_register(true, 8, reg, &v);
}

static int
bus_space_peek_4(bus_space_tag_t t, bus_space_handle_t h, uint32_t reg, uint32_t *v)
{
	uint64_t value = 0;
	int error;
	CHECK(t == &trace && h == 0);
	error = access_register(false, 4, reg, &value);
	if (error == 0)
		*v = (uint32_t)value;
	return error;
}

static int
bus_space_peek_8(bus_space_tag_t t, bus_space_handle_t h, uint32_t reg, uint64_t *v)
{
	uint64_t value = 0;
	int error;
	CHECK(t == &trace && h == 0);
	error = access_register(false, 8, reg, &value);
	if (error == 0)
		*v = value;
	return error;
}

static void
bus_space_barrier(bus_space_tag_t t, bus_space_handle_t h, uint32_t reg,
    unsigned width, unsigned flags)
{
	CHECK(t == &trace && h == 0 && reg < 65536);
	CHECK(width == 4 || width == 8);
	CHECK(flags == 2 || flags == 3);
	if (flags == 2) {
		CHECK(write_barrier_due && ntrace != 0);
		CHECK(trace[ntrace - 1].write && trace[ntrace - 1].reg == reg &&
		    trace[ntrace - 1].width == width);
		write_barrier_due = false;
	} else {
		CHECK(!write_barrier_due && !read_barrier_ready);
		read_barrier_ready = true;
		barrier_reg = reg;
		barrier_width = width;
	}
}

static void
delay(unsigned us)
{
	CHECK(us == 3);
	CHECK(!write_barrier_due && !read_barrier_ready);
	CHECK(ndelay < 2);
	CHECK(ntrace == secure_prefix + reset2_extra + (ndelay == 0 ? 20U : 22U));
	ndelay++;
}
#include "driver.h"

static void
feature(struct sun60i_a733_gpu_fwinfo *info, unsigned id, uint64_t value)
{
	info->feature_mask[id / 64] |= UINT64_C(1) << (id % 64);
	info->feature_value[id] = value;
}

/* Exact metadata from the pinned 36.56.104.183 image; no firmware payload. */
static const struct sun60i_a733_gpu_fwinfo pinned_profile = {
	.bvnc = UINT64_C(0x00240038006800b7),
	.brn_mask = UINT64_C(0x201),
	.ern_mask = UINT64_C(0xf5),
	.feature_mask = { UINT64_C(0x92d9ff57fdbf7cdb), UINT64_C(0x1f) },
	.feature_value = {
		1, 1, 0, 19456, 1, 0, 1, 1, 0, 0,
		50, 50, 7, 0, 0, 0, 1, 1, 1, 1,
		6, 1, 0, 1, 12, 0, 0, 1, 1, 6,
		8, 1, 1, 1, 1, 0, 36, 0, 1, 0,
		1, 1, 2, 1, 512, 1, 16, 1, 1, 0,
		0, 16, 16, 0, 1, 1, 0, 2, 0, 0,
		40, 0, 0, 1, 1, 1, 19, 3, 1, 0, 0
	}
};

static void
reset(struct sun60i_a733_gpu_fwinfo *info)
{
	*info = pinned_profile;
	memset(registers, 0, sizeof(registers));
	memset(trace, 0, sizeof(trace));
	observed = (struct sun60i_a733_gpu_fwstart_result){ UINT32_MAX, UINT32_MAX, true, true };
	ntrace = fail_at = ndelay = release_delay = nwrite = 0;
	secure_prefix = 2;
	reset2_extra = 6;
	fail_after_write = write_barrier_due = read_barrier_ready = false;
	registers[0x3800 / 8] = UINT64_C(0x76543210dead0002);
}

static int
run(const struct sun60i_a733_gpu_fwinfo *info)
{
	return sun60i_a733_gpu_fwstart(&trace, 0, info, 0x40018000,
	    0x4001c000, 0x40017000, &observed);
}

/*
 * Independent golden trace from the pinned upstream pvr_fw_start() and
 * pvr_mips_wrapper_init(). These values do not come from a native test run.
 * Reads fence writes; their value field is the fixture's initial read input.
 */
static const struct operation upstream_default[] = {
	{ true, 4, 0xa100, UINT64_C(0x0000000000000000) },
	{ false, 4, 0xa100, 0 },
	{ true, 8, 0x0100, UINT64_C(0xffeffffffffffc3d) },
	{ true, 8, 0x0108, UINT64_C(0x00000000001fffff) },
	{ false, 8, 0x0100, 0 },
	{ false, 8, 0x0108, 0 },
	{ true, 8, 0x0100, UINT64_C(0xffeffe071ffffc3d) },
	{ true, 8, 0x0108, UINT64_C(0x0000000000000000) },
	{ false, 8, 0x0100, 0 },
	{ false, 8, 0x0108, 0 },
	{ true, 8, 0x0100, UINT64_C(0x0000000100000000) },
	{ true, 8, 0x0108, UINT64_C(0x0000000000000000) },
	{ false, 8, 0x0100, 0 },
	{ false, 8, 0x0108, 0 },
	{ false, 4, 0x3800, 0 },
	{ true, 4, 0x3800, UINT64_C(0x0000000000200003) },
	{ true, 4, 0x0810, UINT64_C(0x000000000001cf80) },
	{ true, 8, 0x0818, UINT64_C(0x000000001fc00001) },
	{ true, 8, 0x0820, UINT64_C(0x000000004001800c) },
	{ true, 8, 0x0858, UINT64_C(0x0000000000000001) },
	{ true, 8, 0x0860, UINT64_C(0x000000004001800c) },
	{ true, 8, 0x0828, UINT64_C(0x000000001fc01001) },
	{ true, 8, 0x0830, UINT64_C(0x000000004001c00c) },
	{ true, 8, 0x0838, UINT64_C(0x000000001fc02001) },
	{ true, 8, 0x0840, UINT64_C(0x000000004001700c) },
	{ true, 8, 0x0b50, UINT64_C(0x0000000000000000) },
	{ true, 4, 0x08c8, UINT64_C(0x0000000000000000) },
	{ true, 8, 0x38c0, UINT64_C(0x000000008882af00) },
	{ true, 8, 0x0100, UINT64_C(0x0000000000000000) },
	{ false, 8, 0x0100, 0 }
};

static void
clear_feature(struct sun60i_a733_gpu_fwinfo *info, unsigned id)
{
	info->feature_mask[id / 64] &= ~(UINT64_C(1) << (id % 64));
	info->feature_value[id] = 0;
}

/* Bits: secure reset, BRN71242, XE_TPU2, GPU multicore. */
static unsigned
prepare(struct sun60i_a733_gpu_fwinfo *info, struct operation *golden,
    unsigned variant)
{
	unsigned count = 0;

	reset(info);
	secure_prefix = variant & 1 ? 2 : 0;
	reset2_extra = variant & 4 ? 6 : 0;
	if ((variant & 1) == 0)
		clear_feature(info, A733_GPU_FW_FEATURE_SYS_BUS_SECURE_RESET);
	if (variant & 2)
		info->brn_mask |= UINT64_C(1) << 11;
	if ((variant & 4) == 0)
		clear_feature(info, A733_GPU_FW_FEATURE_XE_TPU2);
	if ((variant & 8) == 0)
		clear_feature(info, A733_GPU_FW_FEATURE_GPU_MULTICORE_SUPPORT);
	for (unsigned i = 0; i < __arraycount(upstream_default); i++) {
		struct operation op = upstream_default[i];
		if ((op.reg == 0xa100 && (variant & 1) == 0) ||
		    (op.reg == 0x0108 && (variant & 4) == 0))
			continue;
		if (op.reg == 0x3800 && op.write &&
		    (variant & 2) != 0 && (variant & 8) == 0)
			op.value |= 0x01000000;
		golden[count++] = op;
	}
	return count;
}

static void
check_trace(const struct operation *golden, unsigned count)
{
	CHECK(ntrace == count);
	for (unsigned i = 0; i < count; i++) {
		CHECK(trace[i].write == golden[i].write);
		CHECK(trace[i].width == golden[i].width);
		CHECK(trace[i].reg == golden[i].reg);
		CHECK(trace[i].value == golden[i].value);
	}
	CHECK(!write_barrier_due && !read_barrier_ready);
}

static void
test_sequence(void)
{
	struct sun60i_a733_gpu_fwinfo info;
	struct operation golden[30];
	for (unsigned variant = 0; variant < 16; variant++) {
		unsigned count = prepare(&info, golden, variant);
		CHECK(run(&info) == 0);
		check_trace(golden, count);
		CHECK(observed.touched && observed.released && observed.writes == nwrite);
		CHECK(observed.writes == 17 + (variant & 1) + (variant & 4 ? 3 : 0));
		CHECK(ndelay == 2 && release_delay == 1);
		CHECK(registers[0x3800 / 8] == (UINT64_C(0x7654321000200003) |
		    ((variant & 2) != 0 && (variant & 8) == 0 ? UINT64_C(0x1000000) : 0)));
		for (unsigned after = 0; after < 2; after++) {
			for (unsigned failure = 1; failure <= count; failure++) {
				prepare(&info, golden, variant);
				fail_at = failure;
				fail_after_write = after != 0;
				CHECK(run(&info) == EFAULT);
				check_trace(golden, failure);
				CHECK(observed.touched && observed.writes == nwrite);
				CHECK(observed.last_register == golden[failure - 1].reg);
				CHECK(observed.released == (failure == count));
				CHECK(ndelay == (failure >= count - 1 ? 1U : 0U));
				/* A failed poke can release reset despite released=false. */
				if (failure == count - 1 && after != 0) {
					CHECK(registers[0x100 / 8] == 0);
					CHECK(!observed.released && observed.touched);
				}
			}
		}
	}
	/* Preserve an absent PSG hazard flag and every upper SLC scramble bit. */
	reset(&info);
	registers[0x3800 / 8] = UINT64_C(0xfedcba98dead0000);
	CHECK(run(&info) == 0);
	CHECK(registers[0x3800 / 8] == UINT64_C(0xfedcba9800200001));
}

static void
test_preflight(void)
{
	struct sun60i_a733_gpu_fwinfo info;
	const uint64_t invalid[] = {
		1, UINT64_C(1) << 36, UINT64_MAX,
		0x1fc00000, 0x1fc01000, 0x1fc02000, 0x1fc03000
	};
	uint64_t addresses[3];

	for (unsigned bad = 0; bad < 8; bad++) {
		reset(&info);
		switch (bad) {
		case 0: info.bvnc++; break;
		case 1: info.brn_mask = 0; break;
		case 2: feature(&info, A733_GPU_FW_FEATURE_PHYS_BUS_WIDTH, 32); break;
		case 3: feature(&info, A733_GPU_FW_FEATURE_SLC_CACHE_LINE_SIZE_BITS, 1024); break;
		case 4: info.feature_mask[0] &= ~(UINT64_C(1) << A733_GPU_FW_FEATURE_MIPS); break;
		case 5: info.feature_mask[0] &= ~(UINT64_C(1) << A733_GPU_FW_FEATURE_PBE2_IN_XE); break;
		case 6: info.feature_mask[0] &= ~(UINT64_C(1) << A733_GPU_FW_FEATURE_PHYS_BUS_WIDTH); break;
		case 7: info.feature_mask[0] &= ~(UINT64_C(1) << A733_GPU_FW_FEATURE_SLC_CACHE_LINE_SIZE_BITS); break;
		}
		CHECK(run(&info) == EOPNOTSUPP && ntrace == 0 && !observed.touched);
		CHECK(!observed.released && observed.writes == 0 && ndelay == 0);
	}
	for (unsigned slot = 0; slot < 3; slot++) {
		for (unsigned i = 0; i < __arraycount(invalid); i++) {
			reset(&info);
			addresses[0] = 0x40018000;
			addresses[1] = 0x4001c000;
			addresses[2] = 0x40017000;
			addresses[slot] = invalid[i];
			CHECK(sun60i_a733_gpu_fwstart(&trace, 0, &info,
			    addresses[0], addresses[1], addresses[2], &observed) == EINVAL);
			CHECK(ntrace == 0 && !observed.touched && !observed.released);
		}
	}
	for (unsigned pair = 0; pair < 3; pair++) {
		reset(&info);
		addresses[0] = 0x40018000;
		addresses[1] = 0x4001c000;
		addresses[2] = 0x40017000;
		if (pair == 0) addresses[1] = addresses[0];
		if (pair == 1) addresses[2] = addresses[0];
		if (pair == 2) addresses[2] = addresses[1];
		CHECK(sun60i_a733_gpu_fwstart(&trace, 0, &info,
		    addresses[0], addresses[1], addresses[2], &observed) == EINVAL);
		CHECK(ntrace == 0 && !observed.touched);
	}
	/* Full 36-bit output addresses and the excluded range's upper boundary. */
	reset(&info);
	CHECK(sun60i_a733_gpu_fwstart(&trace, 0, &info, UINT64_C(0xffffff000),
	    UINT64_C(0xfffffe000), 0x1fc04000, &observed) == 0);
	CHECK(registers[0x820 / 8] == UINT64_C(0xffffff00c));
	CHECK(registers[0x830 / 8] == UINT64_C(0xfffffe00c));
	CHECK(registers[0x840 / 8] == 0x1fc0400c);
	reset(&info);
	CHECK(sun60i_a733_gpu_fwstart(&trace, 0, NULL, 0, 0, 0, &observed) == EINVAL);
	CHECK(!observed.touched && !observed.released && observed.writes == 0);
	CHECK(ntrace == 0);
	CHECK(sun60i_a733_gpu_fwstart(&trace, 0, &info, 0, 0, 0, NULL) == EINVAL);
	CHECK(ntrace == 0);
}

static void
test_firmware(const char *path)
{
	struct sun60i_a733_gpu_fwinfo parsed, info;
	uint8_t *bytes = malloc(131072);
	FILE *file = fopen(path, "rb");
	const unsigned count = __arraycount(upstream_default);

	CHECK(bytes != NULL && file != NULL);
	CHECK(fread(bytes, 1, 131072, file) == 131072);
	CHECK(fgetc(file) == EOF && !ferror(file));
	CHECK(fclose(file) == 0);
	CHECK(sun60i_a733_gpu_fwinfo_parse(bytes, 131072,
	    pinned_profile.bvnc, &parsed) == 0);
	free(bytes);
	CHECK(parsed.bvnc == pinned_profile.bvnc);
	CHECK(parsed.brn_mask == pinned_profile.brn_mask);
	CHECK(parsed.ern_mask == pinned_profile.ern_mask);
	CHECK(memcmp(parsed.feature_mask, pinned_profile.feature_mask,
	    sizeof(parsed.feature_mask)) == 0);
	CHECK(memcmp(parsed.feature_value, pinned_profile.feature_value,
	    sizeof(parsed.feature_value)) == 0);
	/* Execute the parsed profile, including every fault prefix. */
	for (unsigned after = 0; after < 2; after++) {
		for (unsigned failure = 0; failure <= count; failure++) {
			reset(&info);
			info = parsed;
			fail_at = failure;
			fail_after_write = after != 0;
			CHECK(run(&info) == (failure == 0 ? 0 : EFAULT));
			check_trace(upstream_default, failure == 0 ? count : failure);
			CHECK(observed.touched && observed.writes == nwrite);
			CHECK(observed.released == (failure == 0 || failure == count));
			CHECK(ndelay == (failure == 0 ? 2U : failure >= count - 1 ? 1U : 0U));
		}
	}
	puts("PASS: pinned firmware profile matches the default fixture and upstream trace");
}

int
main(int argc, char **argv)
{
	CHECK(argc == 1 || argc == 2);
	if (argc == 2)
		test_firmware(argv[1]);
	test_sequence();
	test_preflight();
	printf("PASS: %u firmware startup order/fault checks\n", checks);
	return 0;
}
