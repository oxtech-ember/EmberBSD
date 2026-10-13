/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 EmberBSD contributors. AI-assisted real-source tests. */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sun60i_a733_gpu_fwinit.h"
#include "powervr/pvr_rogue_fwif.h"

static unsigned int checks;
#define CHECK(e) do { \
	checks++; \
	if (!(e)) { \
		fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #e); \
		exit(1); \
	} \
} while (0)

/* Golden sizes and offsets follow the pinned upstream ABI. */
static const struct {
	uint32_t offset, size;
} expected[] = {
	{0, 144}, {192, 11264}, {11456, 512}, {11968, 144}, {12160, 2816},
	{14976, 4}, {15040, 2336}, {17408, 4}, {17472, 3624}, {24576, 4096},
	{28672, 10280}, {38976, 72}, {39104, 48000}, {87104, 48000},
	{135104, 864}, {136000, 584}, {0, 16}, {4096, 104}, {8192, 544}
};
#define REQUIRED 139264
#define CAPACITY (256 * 1024)
#define CONFIG_SIZE 12288
#define WORK_BYTES (CAPACITY + CONFIG_SIZE + 8192)
static _Alignas(4096) uint8_t work_store[WORK_BYTES];
static _Alignas(4096) uint8_t config_store[CONFIG_SIZE + 8192];
static uint8_t work_before[WORK_BYTES], config_before[sizeof(config_store)];
static uint8_t golden_work[REQUIRED], golden_config[CONFIG_SIZE];
static uint8_t *const work = work_store + 4096;
static uint8_t *const config = config_store + 4096;
static struct sun60i_a733_gpu_fwinit_layout result, result_before;

static uint32_t
get32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
	    (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t
get64(const uint8_t *p)
{
	return (uint64_t)get32(p) | (uint64_t)get32(p + 4) << 32;
}

static void
put32(uint8_t *p, uint32_t value)
{
	p[0] = (uint8_t)value;
	p[1] = (uint8_t)(value >> 8);
	p[2] = (uint8_t)(value >> 16);
	p[3] = (uint8_t)(value >> 24);
}

static void
put64(uint8_t *p, uint64_t value)
{
	put32(p, (uint32_t)value);
	put32(p + 4, (uint32_t)(value >> 32));
}

static uint32_t
expected_va(size_t id)
{
	return id < 16 ? UINT32_C(0xc0100000) + expected[id].offset :
	    UINT32_C(0xc0fd0000) + (uint32_t)(id - 16) * 65536;
}

static void
reset(void)
{
	memset(work_store, 0xa5, sizeof(work_store));
	memset(config_store, 0xb6, sizeof(config_store));
	memset(&result, 0xc7, sizeof(result));
}

/* Every nonzero firmware pointer written by the initializers. */
static const struct {
	unsigned int owner, offset, target;
} pointers[] = {
	{17, 0, 0}, {17, 4, 1}, {17, 8, 2}, {17, 12, 3}, {17, 16, 4},
	{17, 28, 6}, {17, 36, 15}, {15, 572, 5},
	{18, 316, 11}, {18, 320, 14}, {18, 324, 8}, {18, 328, 10},
	{14, 12, 12}, {14, 436, 13}
};

static void
golden(uint64_t phys, uint32_t hz, uint32_t clusters, uint32_t slc)
{
	uint8_t *p;
	size_t i;

	memset(golden_work, 0, sizeof(golden_work));
	memset(golden_config, 0, sizeof(golden_config));
	for (i = 0; i < sizeof(pointers) / sizeof(pointers[0]); i++) {
		unsigned int id = pointers[i].owner;

		p = id < 16 ? golden_work : golden_config;
		put32(p + expected[id].offset + pointers[i].offset,
		    expected_va(pointers[i].target));
	}
	put32(golden_work + 132, 127);
	put32(golden_work + 136, 88);
	put32(golden_work + 11968 + 132, 31);
	put32(golden_work + 11968 + 136, 88);
	if (slc < 4)
		put32(golden_work + 17472, UINT32_C(1) << 19);
	for (i = 0; i < 4096; i += 4)
		put32(golden_work + 24576 + i, 0xdeadbee0);
	golden_work[38976 + 8] = 1;
	put32(golden_work + 38976 + 12, hz);
	put32(golden_work + 38976 + 16, clusters);
	put32(golden_work + 135104 + 856, 12000);
	put32(golden_config + 4096 + 40, 3);
	put32(golden_config + 4096 + 56, 3);
	put64(golden_config + 8192, phys + 24576);
	put64(golden_config + 8192 + 8, UINT64_C(0xda00000000));
	put64(golden_config + 8192 + 16, UINT64_C(0xe000000000));
	put32(golden_config + 8192 + 344, hz);
	put32(golden_config + 8192 + 356, 1);
}

static void
success(size_t capacity, uint64_t phys, uint32_t hz, uint32_t clusters,
    uint32_t slc)
{
	const uint8_t *p;
	size_t i, j;

	reset();
	CHECK(sun60i_a733_gpu_fwinit(work, capacity, config, CONFIG_SIZE,
	    phys, hz, clusters, slc, &result) == 0);
	golden(phys, hz, clusters, slc);
	CHECK(memcmp(work, golden_work, REQUIRED) == 0);
	CHECK(memcmp(config, golden_config, CONFIG_SIZE) == 0);
	CHECK(result.total_work_size == REQUIRED);
	CHECK(sizeof(expected) / sizeof(expected[0]) ==
	    A733_GPU_FWINIT_OBJECT_COUNT);
	for (i = 0; i < 19; i++) {
		const struct sun60i_a733_gpu_fwinit_region *r = &result.object[i];

		CHECK(r->offset == expected[i].offset);
		CHECK(r->size == expected[i].size);
		CHECK(r->buffer == (i < 16 ? 0U : 1U));
		CHECK(r->fw_addr == expected_va(i));
		CHECK(r->offset % (i == 9 ? 4096 : 64) == 0);
		CHECK(r->offset + r->size <= (i < 16 ? REQUIRED : CONFIG_SIZE));
		for (j = 0; j < i; j++) {
			const struct sun60i_a733_gpu_fwinit_region *s = &result.object[j];

			if (r->buffer == s->buffer)
				CHECK(s->offset + s->size <= r->offset ||
				    r->offset + r->size <= s->offset);
		}
	}
	for (i = 0; i < sizeof(pointers) / sizeof(pointers[0]); i++) {
		unsigned int id = pointers[i].owner, target = pointers[i].target;
		uint32_t address;
		unsigned int matches = 0;

		p = (id < 16 ? work : config) + expected[id].offset;
		address = get32(p + pointers[i].offset);
		CHECK(address == result.object[target].fw_addr);
		for (j = 0; j < 19; j++) {
			if (address >= result.object[j].fw_addr &&
			    address - result.object[j].fw_addr < result.object[j].size)
				matches++;
		}
		CHECK(matches == 1);
	}
	/* Firmware addresses are real; host-only trace pointers remain NULL. */
	p = work + expected[14].offset;
	CHECK(get64(p + 16) == 0 && get64(p + 440) == 0);
	CHECK(get64(config + 8192) == phys + 24576);
	CHECK(get64(config + 8192 + 8) == UINT64_C(0xda00000000));
	CHECK(get64(config + 8192 + 16) == UINT64_C(0xe000000000));
	CHECK(get64(work + 28672 + 10248) == PVR_FWIF_GPU_UTIL_STATE_IDLE);
	for (i = 0; i < 4096; i++)
		CHECK(work_store[i] == 0xa5 && config_store[i] == 0xb6);
	for (i = REQUIRED; i < WORK_BYTES - 4096; i++)
		CHECK(work[i] == 0xa5);
	for (i = CONFIG_SIZE; i < CONFIG_SIZE + 4096; i++)
		CHECK(config[i] == 0xb6);
}

static void
failure(void *w, size_t ws, void *c, size_t cs, uint64_t phys, uint64_t hz,
    uint32_t clusters, uint32_t slc, struct sun60i_a733_gpu_fwinit_layout *out,
    int error)
{
	memcpy(work_before, work_store, sizeof(work_store));
	memcpy(config_before, config_store, sizeof(config_store));
	memcpy(&result_before, &result, sizeof(result));
	CHECK(sun60i_a733_gpu_fwinit(w, ws, c, cs, phys, hz, clusters, slc, out)
	    == error);
	CHECK(memcmp(work_before, work_store, sizeof(work_store)) == 0);
	CHECK(memcmp(config_before, config_store, sizeof(config_store)) == 0);
	CHECK(memcmp(&result_before, &result, sizeof(result)) == 0);
}

#define FAIL(w, ws, c, cs, pa, hz, cl, sl, o, e) \
	failure(w, ws, c, cs, pa, hz, cl, sl, o, e)
#define DEFAULT_PA UINT64_C(0x123450000)

static void
failure_tests(void)
{
	size_t i;

	reset();
	FAIL(NULL, REQUIRED, config, CONFIG_SIZE, DEFAULT_PA, 600000000,
	    2, 16, &result, EINVAL);
	FAIL(work, REQUIRED, NULL, CONFIG_SIZE, DEFAULT_PA, 600000000,
	    2, 16, &result, EINVAL);
	FAIL(work, REQUIRED, config, CONFIG_SIZE, DEFAULT_PA, 600000000,
	    2, 16, NULL, EINVAL);
	FAIL(work, REQUIRED - 4096, config, CONFIG_SIZE, DEFAULT_PA,
	    600000000, 2, 16, &result, EINVAL);
	FAIL(work, CAPACITY + 4096, config, CONFIG_SIZE, DEFAULT_PA,
	    600000000, 2, 16, &result, EINVAL);
	FAIL(work, SIZE_MAX, config, CONFIG_SIZE, DEFAULT_PA, 600000000,
	    2, 16, &result, EINVAL);
	FAIL(work, REQUIRED, config, CONFIG_SIZE - 4096, DEFAULT_PA,
	    600000000, 2, 16, &result, EINVAL);
	FAIL(work, REQUIRED, config, CONFIG_SIZE + 4096, DEFAULT_PA,
	    600000000, 2, 16, &result, EINVAL);
	FAIL(work, REQUIRED, config, SIZE_MAX, DEFAULT_PA, 600000000,
	    2, 16, &result, EINVAL);
	FAIL(work, REQUIRED, config, CONFIG_SIZE, DEFAULT_PA, 0,
	    2, 16, &result, EINVAL);
	FAIL(work, REQUIRED, config, CONFIG_SIZE, DEFAULT_PA,
	    UINT64_C(1) << 32, 2, 16, &result, EINVAL);
	FAIL(work, REQUIRED, config, CONFIG_SIZE, DEFAULT_PA,
	    UINT64_MAX, 2, 16, &result, EINVAL);
	FAIL(work, REQUIRED, config, CONFIG_SIZE, DEFAULT_PA, 600000000,
	    0, 16, &result, EINVAL);
	FAIL(work, REQUIRED, config, CONFIG_SIZE, DEFAULT_PA, 600000000,
	    2, 0, &result, EINVAL);
	for (i = 1; i < 4096; i++) {
		FAIL(work + i, REQUIRED, config, CONFIG_SIZE, DEFAULT_PA,
		    600000000, 2, 16, &result, EINVAL);
		FAIL(work, REQUIRED, config + i, CONFIG_SIZE, DEFAULT_PA,
		    600000000, 2, 16, &result, EINVAL);
		FAIL(work, REQUIRED + i, config, CONFIG_SIZE, DEFAULT_PA,
		    600000000, 2, 16, &result, EINVAL);
		FAIL(work, REQUIRED, config, CONFIG_SIZE, DEFAULT_PA + i,
		    600000000, 2, 16, &result, EINVAL);
	}
	for (i = 36; i < 64; i++)
		FAIL(work, REQUIRED, config, CONFIG_SIZE, UINT64_C(1) << i,
		    600000000, 2, 16, &result, EINVAL);
	FAIL(work, REQUIRED, config, CONFIG_SIZE,
	    (UINT64_C(1) << 36) - REQUIRED + 4096, 600000000,
	    2, 16, &result, EINVAL);
	/* Extra claimed capacity must also fit the physical span. */
	FAIL(work, CAPACITY, config, CONFIG_SIZE,
	    (UINT64_C(1) << 36) - REQUIRED, 600000000,
	    2, 16, &result, EINVAL);
	FAIL(work, REQUIRED, config, CONFIG_SIZE, 0x1fc00000, 600000000,
	    2, 16, &result, EOPNOTSUPP);
	FAIL(work, REQUIRED, config, CONFIG_SIZE, 0x1fc03000, 600000000,
	    2, 16, &result, EOPNOTSUPP);
	FAIL(work, REQUIRED, config, CONFIG_SIZE, 0x1fc00000 - REQUIRED + 4096,
	    600000000, 2, 16, &result, EOPNOTSUPP);
	/* The forbidden window can fall solely in the spare capacity. */
	FAIL(work, CAPACITY, config, CONFIG_SIZE, 0x1fc00000 - REQUIRED,
	    600000000, 2, 16, &result, EOPNOTSUPP);
	FAIL((void *)(UINTPTR_MAX - 4095), REQUIRED, config, CONFIG_SIZE,
	    DEFAULT_PA, 600000000, 2, 16, &result, EINVAL);
	FAIL(work, REQUIRED, (void *)(UINTPTR_MAX - 4095), CONFIG_SIZE,
	    DEFAULT_PA, 600000000, 2, 16, &result, EINVAL);
	FAIL(work, REQUIRED, config, CONFIG_SIZE, DEFAULT_PA, 600000000,
	    2, 16, (void *)(UINTPTR_MAX - 7), EINVAL);
	FAIL(work, REQUIRED, config, CONFIG_SIZE, DEFAULT_PA, 600000000,
	    2, 16, (void *)((uint8_t *)&result + 1), EINVAL);
	/* Alias in either direction, at the last page, and inside spare work. */
	FAIL(work, REQUIRED, work, CONFIG_SIZE, DEFAULT_PA, 600000000,
	    2, 16, &result, EINVAL);
	FAIL(work, REQUIRED, work - 4096, CONFIG_SIZE, DEFAULT_PA, 600000000,
	    2, 16, &result, EINVAL);
	FAIL(work, REQUIRED, work + REQUIRED - 4096, CONFIG_SIZE, DEFAULT_PA,
	    600000000, 2, 16, &result, EINVAL);
	FAIL(work, CAPACITY, work + REQUIRED, CONFIG_SIZE, DEFAULT_PA,
	    600000000, 2, 16, &result, EINVAL);
	FAIL(work, REQUIRED, config, CONFIG_SIZE, DEFAULT_PA, 600000000,
	    2, 16, (void *)work, EINVAL);
	FAIL(work, CAPACITY, config, CONFIG_SIZE, DEFAULT_PA, 600000000,
	    2, 16, (void *)(work + REQUIRED), EINVAL);
	FAIL(work, REQUIRED, config, CONFIG_SIZE, DEFAULT_PA, 600000000,
	    2, 16, (void *)(config + CONFIG_SIZE - 8), EINVAL);
}

int
main(void)
{
	CHECK(sun60i_a733_gpu_fwinit_work_size() == REQUIRED);
	CHECK(A733_GPU_FWINIT_KCCB_COUNT == 128);
	CHECK(A733_GPU_FWINIT_FWCCB_COUNT == 32);
	CHECK(sizeof(struct rogue_fwif_kccb_cmd) == 88);
	CHECK(sizeof(struct rogue_fwif_fwccb_cmd) == 88);
	success(REQUIRED, DEFAULT_PA, 600000000, 2, 16);
	success(CAPACITY, DEFAULT_PA, 400000000, 1, 1);
	success(REQUIRED + 4096, DEFAULT_PA, UINT32_MAX, UINT32_MAX, 3);
	success(REQUIRED, (UINT64_C(1) << 36) - REQUIRED, 1, 1, 4);
	success(REQUIRED, 0, 600000000, 2, UINT32_MAX);
	success(REQUIRED, 0x1fc04000, 600000000, 2, 16);
	success(REQUIRED, 0x1fc00000 - REQUIRED, 600000000, 2, 16);
	failure_tests();
	reset();
	/* Exactly adjacent CPU spans are permitted. */
	CHECK(sun60i_a733_gpu_fwinit(work, REQUIRED, work + REQUIRED,
	    CONFIG_SIZE, DEFAULT_PA, 600000000, 2, 16, &result) == 0);
	golden(DEFAULT_PA, 600000000, 2, 16);
	CHECK(memcmp(work, golden_work, REQUIRED) == 0);
	CHECK(memcmp(work + REQUIRED, golden_config, CONFIG_SIZE) == 0);
	printf("a733-gpu-fwinit-contract: PASS %u checks\n", checks);
	return 0;
}
