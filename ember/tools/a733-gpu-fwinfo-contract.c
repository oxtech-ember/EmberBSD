/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 EmberBSD contributors. AI-assisted metadata tests. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sun60i_a733_gpu_fwinfo.h"

#define IMAGE_SIZE 12288
#define INFO_OFFSET 8192
#define DEVICE_OFFSET 4096
#define LAYOUT_OFFSET (INFO_OFFSET + 48)
#define BVNC UINT64_C(0x00240038006800b7)
#define CHECK(c) do { \
	checks++; \
	if (!(c)) { \
		fprintf(stderr, "line %d: %s\n", __LINE__, #c); \
		exit(1); \
	} \
} while (0)

static unsigned int checks;
static uint8_t original[IMAGE_SIZE], image[IMAGE_SIZE];

static void
put32(size_t offset, uint32_t value)
{
	for (unsigned int n = 0; n < 4; n++)
		image[offset + n] = (uint8_t)(value >> (8 * n));
}

static void
put64(size_t offset, uint64_t value)
{
	put32(offset, (uint32_t)value);
	put32(offset + 4, (uint32_t)(value >> 32));
}

static void
fixture(void)
{
	/* Published layout metadata; the synthetic executable area stays zero. */
	static const uint32_t entries[6][6] = {
		{ 4, 1, 0xc0000000, 192512, 94208, 0 },
		{ 5, 1, 0x9fc01000, 8192, 4096, 94208 },
		{ 6, 1, 0xbfc00000, 4096, 4096, 98304 },
		{ 7, 2, 0xc0032000, 32768, 12288, 0 },
		{ 8, 2, 0xbfc01000, 4096, 4096, 12288 },
		{ 9, 2, 0xcf600000, 4096, 4096, 16384 },
	};

	memset(image, 0, sizeof(image));
	put32(INFO_OFFSET, 3);
	put32(INFO_OFFSET + 4, 48);
	put32(INFO_OFFSET + 8, 6);
	put32(INFO_OFFSET + 12, 24);
	put64(INFO_OFFSET + 16, BVNC);
	put32(INFO_OFFSET + 24, 4096);
	put32(INFO_OFFSET + 28, 1);
	put32(INFO_OFFSET + 32, 0x00010001);
	put32(INFO_OFFSET + 36, 6976702);
	put32(INFO_OFFSET + 40, 4096);
	for (unsigned int n = 0; n < 6; n++)
		for (unsigned int j = 0; j < 6; j++)
			put32(LAYOUT_OFFSET + n * 24 + j * 4, entries[n][j]);
	put64(DEVICE_OFFSET, 1);
	put64(DEVICE_OFFSET + 8, 1);
	put64(DEVICE_OFFSET + 16, 2);
	put64(DEVICE_OFFSET + 24, 0);
	put64(DEVICE_OFFSET + 32, 0x201);
	put64(DEVICE_OFFSET + 40, 0xf5);
	put64(DEVICE_OFFSET + 48, UINT64_C(1) << 27);
	memcpy(original, image, sizeof(image));
}

static void
expect(int expected)
{
	struct sun60i_a733_gpu_fwinfo info, saved;
	uint8_t unchanged[IMAGE_SIZE];
	int error;

	memset(&info, 0xa5, sizeof(info));
	memcpy(&saved, &info, sizeof(saved));
	memcpy(unchanged, image, sizeof(image));
	error = sun60i_a733_gpu_fwinfo_parse(image, sizeof(image), BVNC, &info);
	CHECK(error == expected);
	CHECK(memcmp(unchanged, image, sizeof(image)) == 0);
	if (error != 0)
		CHECK(memcmp(&saved, &info, sizeof(info)) == 0);
}

static void
header_tests(void)
{
	static const struct {
		size_t offset;
		uint32_t value;
		int error;
	} cases[] = {
		{ 0, 0, EOPNOTSUPP }, { 0, 4, EOPNOTSUPP },
		{ 4, 47, EINVAL }, { 4, UINT32_MAX, EINVAL },
		{ 8, 0, EINVAL }, { 8, 9, EINVAL },
		{ 8, UINT32_MAX, EINVAL }, { 12, 23, EINVAL },
		{ 16, 0, ENODEV }, { 24, 0, EOPNOTSUPP },
		{ 24, 8192, EOPNOTSUPP }, { 28, 0, EOPNOTSUPP },
		{ 32, 0, EOPNOTSUPP }, { 32, 2, EOPNOTSUPP },
		{ 40, 0, EINVAL }, { 40, 31, EINVAL },
		{ 40, INFO_OFFSET, EINVAL }, { 40, UINT32_MAX, EINVAL }
	};
	struct sun60i_a733_gpu_fwinfo info, saved;

	fixture(); expect(0);
	memset(&info, 0xa5, sizeof(info));
	memcpy(&saved, &info, sizeof(saved));
	CHECK(sun60i_a733_gpu_fwinfo_parse(NULL, IMAGE_SIZE, BVNC, &info) == EINVAL);
	CHECK(sun60i_a733_gpu_fwinfo_parse(image, IMAGE_SIZE, BVNC, NULL) == EINVAL);
	CHECK(sun60i_a733_gpu_fwinfo_parse(image, IMAGE_SIZE, 0, &info) == EINVAL);
	CHECK(memcmp(&saved, &info, sizeof(info)) == 0);
	for (size_t n = 0; n < IMAGE_SIZE; n++) {
		uint8_t *short_input = malloc(n + 1);

		CHECK(short_input != NULL);
		memcpy(short_input + 1, image, n);
		CHECK(sun60i_a733_gpu_fwinfo_parse(short_input + 1, n,
		    BVNC, &info) != 0);
		CHECK(memcmp(&saved, &info, sizeof(info)) == 0);
		free(short_input);
	}
	for (unsigned int n = 0; n < sizeof(cases) / sizeof(cases[0]); n++) {
		fixture();
		put32(INFO_OFFSET + cases[n].offset, cases[n].value);
		expect(cases[n].error);
	}
	for (unsigned int n = 0; n < 8; n++) {
		uint8_t *unaligned = malloc(IMAGE_SIZE + n);

		CHECK(unaligned != NULL);
		memcpy(unaligned + n, original, IMAGE_SIZE);
		CHECK(sun60i_a733_gpu_fwinfo_parse(unaligned + n, IMAGE_SIZE,
		    BVNC, &info) == 0);
		CHECK(info.bvnc == BVNC && info.version_major == 1 &&
		    info.version_minor == 1 && info.version_build == 6976702);
		CHECK(info.payload_size == DEVICE_OFFSET && info.layout_count == 6);
		CHECK(info.allocation_size[1] == 102400 &&
		    info.allocation_size[2] == 20480);
		free(unaligned);
	}
}

static void
layout_tests(void)
{
	static const struct {
		size_t offset;
		uint32_t value;
		int error;
	} cases[] = {
		{ 0, 0, EOPNOTSUPP }, { 0, 3, EOPNOTSUPP },
		{ 0, 10, EOPNOTSUPP }, { 0, 14, EOPNOTSUPP },
		{ 0, 15, EINVAL }, { 0, UINT32_MAX, EINVAL },
		{ 4, 0, EINVAL }, { 4, 2, EINVAL }, { 4, 3, EINVAL },
		{ 8, 0xfffff000, EINVAL }, { 12, 0, EINVAL },
		{ 12, 94207, EINVAL }, { 16, 0, EINVAL },
		{ 16, 192513, EINVAL }, { 20, 0xffffffff, EINVAL },
		{ 24, 4, EINVAL }, { 24 + 8, 0xc0001000, EINVAL },
		{ 24 + 20, 0, EINVAL }, { 24 + 20, 110592, EINVAL }
	};

	for (unsigned int n = 0; n < sizeof(cases) / sizeof(cases[0]); n++) {
		fixture();
		put32(LAYOUT_OFFSET + cases[n].offset, cases[n].value);
		expect(cases[n].error);
	}
	/* Maximum sizes do not reserve VA space beyond the actual allocation. */
	fixture(); put32(LAYOUT_OFFSET + 24 + 8, 0xc0017000); expect(0);
	/* Code and data allocation offsets may overlap in separate buffers. */
	fixture(); expect(0);
	/* A valid subset is metadata, not proof of a bootable MIPS program. */
	fixture(); put32(INFO_OFFSET + 8, 1); expect(0);
	/* Sum overflow must fail before a later consumer allocates the buffer. */
	fixture();
	for (unsigned int n = 0; n < 2; n++) {
		put32(LAYOUT_OFFSET + n * 24 + 8, 0);
		put32(LAYOUT_OFFSET + n * 24 + 12, 0x90000000);
		put32(LAYOUT_OFFSET + n * 24 + 16, 0x90000000);
		put32(LAYOUT_OFFSET + n * 24 + 20, 0);
	}
	expect(EINVAL);
}

static void
device_tests(void)
{
	/* Parameter IDs from pinned pvr_device_info.c, independent of the parser. */
	static const unsigned int parameters[] = { 1, 3, 9, 10, 11, 12, 13, 14,
	    20, 21, 23, 24, 25, 26, 28, 29, 30, 31, 36, 42, 43, 44, 46,
	    51, 52, 57, 60, 63, 66, 67, 68 };
	struct sun60i_a733_gpu_fwinfo info;
	uint64_t value;

	for (unsigned int n = 0; n < 4; n++) {
		fixture(); put64(DEVICE_OFFSET + n * 8, UINT64_MAX); expect(EINVAL);
		fixture(); put64(DEVICE_OFFSET + n * 8, UINT32_MAX); expect(EINVAL);
	}
	fixture(); put64(DEVICE_OFFSET, 2); expect(EOPNOTSUPP);
	fixture(); put64(DEVICE_OFFSET + 8, 2); expect(EOPNOTSUPP);
	fixture(); put64(DEVICE_OFFSET + 16, 3); expect(EOPNOTSUPP);
	fixture(); put64(DEVICE_OFFSET + 24, 32); expect(EOPNOTSUPP);
	fixture(); put64(DEVICE_OFFSET + 32, UINT64_C(1) << 12); expect(EOPNOTSUPP);
	fixture(); put64(DEVICE_OFFSET + 40, UINT64_C(1) << 8); expect(EOPNOTSUPP);
	fixture(); put64(DEVICE_OFFSET + 56, UINT64_C(1) << 7); expect(EOPNOTSUPP);
	fixture(); put64(DEVICE_OFFSET + 48, 0); expect(EOPNOTSUPP);
	fixture(); put64(DEVICE_OFFSET + 24, 1); expect(EOPNOTSUPP);
	fixture(); put64(DEVICE_OFFSET + 48, (UINT64_C(1) << 27) | 2); expect(EINVAL);
	/* Every count must fit after earlier arrays consume their bytes. */
	fixture();
	put32(INFO_OFFSET + 40, 48);
	put64(INFO_OFFSET - 48, 1);
	put64(INFO_OFFSET - 40, 1);
	put64(INFO_OFFSET - 32, 1);
	put64(INFO_OFFSET - 24, 0);
	expect(EINVAL);
	for (unsigned int bit = 0; bit < 71; bit++) {
		int has_parameter = 0;
		int expected = bit == 25 || bit == 37 ? EOPNOTSUPP : 0;

		fixture();
		for (unsigned int n = 0; n < sizeof(parameters) / sizeof(parameters[0]); n++)
			if (bit == parameters[n])
				has_parameter = 1;
		if (bit < 64)
			put64(DEVICE_OFFSET + 48, (UINT64_C(1) << 27) | (UINT64_C(1) << bit));
		else
			put64(DEVICE_OFFSET + 56, UINT64_C(1) << (bit - 64));
		if (has_parameter) {
			put64(DEVICE_OFFSET + 24, 1);
			/* Zero is a valid value, distinct from an absent capability. */
			put64(DEVICE_OFFSET + 64, bit == 26 ? 0 : 0xabc000 + bit);
		}
		expect(expected);
		if (expected != 0)
			continue;
		CHECK(sun60i_a733_gpu_fwinfo_parse(image, sizeof(image), BVNC, &info) == 0);
		value = UINT64_MAX;
		CHECK(sun60i_a733_gpu_fwinfo_feature(&info,
		    (enum sun60i_a733_gpu_fw_feature)bit, &value) == 0);
		CHECK(value == (has_parameter ? (bit == 26 ? 0 : 0xabc000 + bit) : 1));
	}
	fixture();
	CHECK(sun60i_a733_gpu_fwinfo_parse(image, sizeof(image), BVNC, &info) == 0);
	value = UINT64_MAX;
	CHECK(sun60i_a733_gpu_fwinfo_feature(&info, A733_GPU_FW_FEATURE_META,
	    &value) == ENOENT && value == UINT64_MAX);
	CHECK(sun60i_a733_gpu_fwinfo_feature(NULL, A733_GPU_FW_FEATURE_MIPS,
	    &value) == EINVAL);
	CHECK(sun60i_a733_gpu_fwinfo_feature(&info, A733_GPU_FW_FEATURE_MIPS,
	    NULL) == EINVAL);
	CHECK(sun60i_a733_gpu_fwinfo_feature(&info, A733_GPU_FW_FEATURE_MAX,
	    &value) == EINVAL && value == UINT64_MAX);
	CHECK(sun60i_a733_gpu_fwinfo_feature(&info,
	    (enum sun60i_a733_gpu_fw_feature)-1, &value) == EINVAL);
}

static void
mutation_tests(void)
{
	struct sun60i_a733_gpu_fwinfo info, saved;
	uint32_t random = 0x733f;

	fixture();
	for (unsigned int n = 0; n < 20000; n++) {
		size_t pos;
		int error;

		memcpy(image, original, sizeof(image));
		memset(&info, 0xa5, sizeof(info));
		memcpy(&saved, &info, sizeof(saved));
		random = random * 1664525 + 1013904223;
		/* Mutate metadata only, never bytes from the real firmware. */
		pos = DEVICE_OFFSET + random % (IMAGE_SIZE - DEVICE_OFFSET);
		image[pos] ^= (uint8_t)(1U << ((random >> 16) & 7));
		error = sun60i_a733_gpu_fwinfo_parse(image, sizeof(image), BVNC, &info);
		if (error != 0)
			CHECK(memcmp(&saved, &info, sizeof(info)) == 0);
		else {
			CHECK(info.bvnc == BVNC && info.layout_count > 0 &&
			    info.layout_count <= 8 && info.page_size == 4096);
			CHECK((info.feature_mask[0] & (UINT64_C(1) << 27)) != 0);
		}
	}
}

static void
blob_test(const char *path)
{
	struct sun60i_a733_gpu_fwinfo info;
	uint8_t *bytes;
	uint64_t value;
	FILE *file = fopen(path, "rb");
	long length;

	CHECK(file != NULL);
	CHECK(fseek(file, 0, SEEK_END) == 0);
	length = ftell(file);
	CHECK(length == 131072);
	CHECK(fseek(file, 0, SEEK_SET) == 0);
	bytes = malloc((size_t)length);
	CHECK(bytes != NULL);
	CHECK(fread(bytes, 1, (size_t)length, file) == (size_t)length);
	CHECK(fclose(file) == 0);
	CHECK(sun60i_a733_gpu_fwinfo_parse(bytes, (size_t)length, BVNC, &info) == 0);
	CHECK(info.version_major == 1 && info.version_minor == 1 &&
	    info.version_build == 6976702 && info.layout_count == 6);
	CHECK(info.payload_size == 122880 && info.device_info_size == 4096);
	CHECK(info.allocation_size[1] == 102400 && info.allocation_size[2] == 20480);
	CHECK(info.array[0].count == 1 && info.array[1].count == 1 &&
	    info.array[2].count == 2 && info.array[3].count == 29);
	CHECK(info.brn_mask == 0x201 && info.ern_mask == 0xf5);
	CHECK(info.feature_mask[0] == UINT64_C(0x92d9ff57fdbf7cdb) &&
	    info.feature_mask[1] == 0x1f);
	CHECK(sun60i_a733_gpu_fwinfo_feature(&info,
	    A733_GPU_FW_FEATURE_PHYS_BUS_WIDTH, &value) == 0 && value == 36);
	CHECK(sun60i_a733_gpu_fwinfo_feature(&info,
	    A733_GPU_FW_FEATURE_VIRTUAL_ADDRESS_SPACE_BITS, &value) == 0 &&
	    value == 40);
	free(bytes);
	puts("Pinned 36.56.104.183 firmware metadata: accepted, no execution");
}

int
main(int argc, char **argv)
{
	CHECK(argc == 1 || argc == 2);
	header_tests();
	layout_tests();
	device_tests();
	mutation_tests();
	if (argc == 2)
		blob_test(argv[1]);
	printf("A733 PowerVR firmware metadata: %u checks passed\n", checks);
	return 0;
}
