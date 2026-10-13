/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 EmberBSD contributors. AI-assisted production tests. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sun60i_a733_gpu_fwimage.h"

#define IMAGE_SIZE 12288
#define INFO 8192
#define DEVICE 4096
#define LAYOUT (INFO + 48)
#define CAPACITY 224
#define REQUIRED 192
#define BVNC UINT64_C(0x00240038006800b7)
#define PH(n, field) (52 + (n) * 32 + (field))
#define CHECK(c) do { \
	checks++; \
	if (!(c)) { \
		fprintf(stderr, "line %d: %s\n", __LINE__, #c); \
		exit(1); \
	} \
} while (0)

static unsigned int checks;
static uint8_t image[IMAGE_SIZE], original[IMAGE_SIZE];
static uint8_t code[CAPACITY], data[CAPACITY];

static void
put16(size_t offset, uint32_t value)
{
	for (unsigned int i = 0; i < 2; i++)
		image[offset + i] = (uint8_t)(value >> (8 * i));
}

static void
put32(size_t offset, uint32_t value)
{
	for (unsigned int i = 0; i < 4; i++)
		image[offset + i] = (uint8_t)(value >> (8 * i));
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
	static const uint32_t sections[6][6] = {
		{ 4, 1, 0xc0000000, 128, 64, 0 },
		{ 5, 1, 0x9fc01000, 128, 64, 64 },
		{ 6, 1, 0xbfc00000, 128, 64, 128 },
		{ 7, 2, 0xc0032000, 128, 64, 0 },
		{ 8, 2, 0xbfc01000, 128, 64, 64 },
		{ 9, 2, 0xcf600000, 128, 64, 128 }
	};
	static const uint32_t phdr[5][8] = {
		{ 1, 256, 0x9fc01000, 0x9fc01000, 8, 8, 5, 4 },
		{ 1, 272, 0xbfc00000, 0xbfc00000, 8, 8, 5, 4 },
		{ 1, 288, 0xc0000000, 0xc0000000, 16, 16, 5, 8 },
		{ 1, 320, 0xc0032000, 0xc0032000, 0, 8, 6, 8 },
		{ 1, 328, 0xc0032008, 0xc0032008, 8, 12, 6, 64 }
	};

	memset(image, 0, sizeof(image));
	put32(INFO, 3);
	put32(INFO + 4, 48);
	put32(INFO + 8, 6);
	put32(INFO + 12, 24);
	put64(INFO + 16, BVNC);
	put32(INFO + 24, 4096);
	put32(INFO + 28, 1);
	put32(INFO + 32, 0x00010001);
	put32(INFO + 36, 1);
	put32(INFO + 40, 4096);
	for (unsigned int i = 0; i < 6; i++)
		for (unsigned int j = 0; j < 6; j++)
			put32(LAYOUT + i * 24 + j * 4, sections[i][j]);
	put64(DEVICE + 16, 1);
	put64(DEVICE + 32, UINT64_C(1) << 27);
	memcpy(image, "\177ELF\1\1\1", 7);
	put16(16, 2);
	put16(18, 8);
	put32(20, 1);
	put32(24, 0xbfc00001);
	put32(28, 52);
	put16(40, 52);
	put16(42, 32);
	put16(44, 5);
	for (unsigned int i = 0; i < 5; i++)
		for (unsigned int j = 0; j < 8; j++)
			put32(PH(i, j * 4), phdr[i][j]);
	for (unsigned int i = 0; i < 256; i++)
		image[256 + i] = (uint8_t)(i + 1);
	memcpy(original, image, sizeof(image));
}

static void
fill_outputs(struct sun60i_a733_gpu_fwimage *out)
{
	memset(code, 0xa5, sizeof(code));
	memset(data, 0xa5, sizeof(data));
	memset(out, 0xa5, sizeof(*out));
}

static void
unchanged(const void *bytes, size_t size)
{
	const uint8_t *p = bytes;

	for (size_t i = 0; i < size; i++)
		CHECK(p[i] == 0xa5);
}

static void
expect(int expected)
{
	struct sun60i_a733_gpu_fwimage out;
	uint8_t saved[IMAGE_SIZE];
	int error;

	memcpy(saved, image, sizeof(saved));
	fill_outputs(&out);
	error = sun60i_a733_gpu_fwimage_prepare(image, sizeof(image), BVNC,
	    code, sizeof(code), data, sizeof(data), &out);
	if (expected >= 0)
		CHECK(error == expected);
	CHECK(memcmp(saved, image, sizeof(image)) == 0);
	if (error != 0) {
		unchanged(&out, sizeof(out));
		unchanged(code, sizeof(code));
		unchanged(data, sizeof(data));
	} else {
		CHECK(out.info.bvnc == BVNC);
		CHECK(out.load_count > 0 && out.load_count <= 16);
		CHECK(out.entry_offset >= 128 && out.entry_offset < 192);
		unchanged(code + REQUIRED, sizeof(code) - REQUIRED);
		unchanged(data + REQUIRED, sizeof(data) - REQUIRED);
	}
}

static void
success_tests(void)
{
	struct sun60i_a733_gpu_fwimage out;
	uint8_t want_code[CAPACITY], want_data[CAPACITY];
	uint8_t *unaligned;
	uint8_t layout[24];

	fill_outputs(&out);
	CHECK(sun60i_a733_gpu_fwimage_prepare(image, sizeof(image), BVNC,
	    code, sizeof(code), data, sizeof(data), &out) == 0);
	memset(want_code, 0xa5, sizeof(want_code));
	memset(want_data, 0xa5, sizeof(want_data));
	memset(want_code, 0, REQUIRED);
	memset(want_data, 0, REQUIRED);
	memcpy(want_code + 64, image + 256, 8);
	memcpy(want_code + 128, image + 272, 8);
	memcpy(want_code, image + 288, 16);
	memcpy(want_data + 8, image + 328, 8);
	CHECK(memcmp(want_code, code, sizeof(code)) == 0);
	CHECK(memcmp(want_data, data, sizeof(data)) == 0);
	CHECK(out.entry == 0xbfc00001 && out.entry_addr == 0xbfc00000);
	CHECK(out.entry_offset == 128 && out.load_count == 5);
	for (unsigned int i = 0; i < 6; i++) {
		CHECK(out.section[i].id == i + 4);
		CHECK(out.section[i].alloc_offset == (i % 3) * 64);
	}
	unaligned = malloc(IMAGE_SIZE + 1);
	CHECK(unaligned != NULL);
	memcpy(unaligned + 1, image, IMAGE_SIZE);
	CHECK(sun60i_a733_gpu_fwimage_prepare(unaligned + 1, IMAGE_SIZE, BVNC,
	    code, sizeof(code), data, sizeof(data), &out) == 0);
	CHECK(memcmp(want_code, code, sizeof(code)) == 0);
	free(unaligned);
	/* Metadata order is independent of the returned section order. */
	memcpy(layout, image + LAYOUT, 24);
	memcpy(image + LAYOUT, image + LAYOUT + 5 * 24, 24);
	memcpy(image + LAYOUT + 5 * 24, layout, 24);
	expect(0);
	memcpy(image, original, IMAGE_SIZE);
	put32(24, 0xbfc00000);
	expect(0);
	put32(24, 0xbfc00007);
	expect(0);
	memcpy(image, original, IMAGE_SIZE);
	put32(PH(0, 28), 0);
	put32(PH(1, 28), 1);
	expect(0);
	/* The full supported header count includes harmless PT_NULL entries. */
	memcpy(image + 1024, image + 52, 160);
	memset(image + 1024 + 160, 0, 11 * 32);
	put32(28, 1024);
	put16(44, 16);
	expect(0);
	/* Exercise all 16 plan slots with adjacent, disjoint CODE loads. */
	for (unsigned int i = 5; i < 16; i++) {
		size_t p = 1024 + i * 32;
		uint32_t address = 0xc0000010 + i - 5;

		put32(p, 1);
		put32(p + 4, 2048 + i);
		put32(p + 8, address);
		put32(p + 12, address);
		put32(p + 16, 1);
		put32(p + 20, 1);
		put32(p + 24, 5);
		put32(p + 28, 1);
	}
	expect(0);
	CHECK(sun60i_a733_gpu_fwimage_prepare(image, sizeof(image), BVNC,
	    code, sizeof(code), data, sizeof(data), &out) == 0);
	CHECK(out.load_count == 16);
	put32(1024 + 15 * 32 + 24, 7);
	expect(EOPNOTSUPP);
	memcpy(image, original, IMAGE_SIZE);
	/* A BSS segment can point to the payload end; it copies zero bytes. */
	put32(PH(3, 4), DEVICE);
	put32(PH(3, 28), 1);
	expect(0);
	memcpy(image, original, IMAGE_SIZE);
	/* Adjacent declared output spans are valid, including the result. */
	unaligned = malloc(REQUIRED * 2 + sizeof(out));
	CHECK(unaligned != NULL);
	CHECK(sun60i_a733_gpu_fwimage_prepare(image, sizeof(image), BVNC,
	    unaligned, REQUIRED, unaligned + REQUIRED, REQUIRED,
	    (struct sun60i_a733_gpu_fwimage *)(void *)(unaligned +
	    REQUIRED * 2)) == 0);
	CHECK(memcmp(unaligned, want_code, REQUIRED) == 0);
	CHECK(memcmp(unaligned + REQUIRED, want_data, REQUIRED) == 0);
	free(unaligned);
}

static void
corrupt_tests(void)
{
	static const struct {
		size_t offset;
		uint32_t value;
		int error;
		unsigned int width;
	} cases[] = {
		{ 0, 0, EINVAL, 4 },
		{ 4, 2, EOPNOTSUPP, 1 }, { 5, 2, EOPNOTSUPP, 1 },
		{ 6, 0, EOPNOTSUPP, 1 }, { 16, 3, EOPNOTSUPP, 2 },
		{ 18, 3, EOPNOTSUPP, 2 }, { 20, 0, EOPNOTSUPP, 4 },
		{ 40, 51, EINVAL, 2 }, { 42, 31, EINVAL, 2 },
		{ 44, 0, EINVAL, 2 }, { 44, 17, EOPNOTSUPP, 2 },
		{ 44, 65535, EOPNOTSUPP, 2 },
		{ 28, 0, EINVAL, 4 }, { 28, 51, EINVAL, 4 },
		{ 28, 4096, EINVAL, 4 }, { 28, 4097, EINVAL, 4 },
		{ 28, 4096 - 159, EINVAL, 4 },
		{ 28, UINT32_MAX, EINVAL, 4 },
		{ 24, 0xbfc00008, EINVAL, 4 },
		{ 24, 0xbfc00040, EINVAL, 4 },
		{ 24, 0xc0000001, EINVAL, 4 },
		{ PH(4, 0), 2, EOPNOTSUPP, 4 },
		{ PH(4, 0), 0x70000000, EOPNOTSUPP, 4 },
		{ PH(4, 4), UINT32_MAX, EINVAL, 4 },
		{ PH(4, 4), 4096, EINVAL, 4 },
		{ PH(4, 4), 4096 - 7, EINVAL, 4 },
		{ PH(3, 4), 4097, EINVAL, 4 },
		{ PH(4, 8), UINT32_MAX, EINVAL, 4 },
		{ PH(4, 12), 0, EOPNOTSUPP, 4 },
		{ PH(4, 16), 13, EINVAL, 4 },
		{ PH(4, 20), 0, EINVAL, 4 },
		{ PH(4, 20), UINT32_MAX, EINVAL, 4 },
		{ PH(4, 20), 57, EINVAL, 4 },
		{ PH(4, 24), 5, EOPNOTSUPP, 4 },
		{ PH(4, 24), 7, EOPNOTSUPP, 4 },
		{ PH(0, 24), 6, EOPNOTSUPP, 4 },
		{ PH(4, 28), 3, EINVAL, 4 },
		{ PH(4, 28), 128, EINVAL, 4 },
		{ INFO + 16, 0, ENODEV, 4 },
		{ INFO + 8, 5, EINVAL, 4 },
		{ INFO + 8, 1, EINVAL, 4 }
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		memcpy(image, original, IMAGE_SIZE);
		if (cases[i].width == 1)
			image[cases[i].offset] = (uint8_t)cases[i].value;
		else if (cases[i].width == 2)
			put16(cases[i].offset, cases[i].value);
		else
			put32(cases[i].offset, cases[i].value);
		expect(cases[i].error);
	}
	memcpy(image, original, IMAGE_SIZE);
	/* Reject memsz overlap even when the earlier segment has no file data. */
	put32(PH(3, 20), 9);
	expect(EINVAL);
	memcpy(image, original, IMAGE_SIZE);
	/* Valid metadata reserves max_size=128, but alloc_size is only 64. */
	put32(PH(4, 8), 0xc0032040);
	put32(PH(4, 12), 0xc0032040);
	put32(PH(4, 28), 1);
	expect(EINVAL);
	memcpy(image, original, IMAGE_SIZE);
	/* Entry cannot target zero-filled memory beyond file-backed boot code. */
	put32(PH(1, 20), 64);
	put32(24, 0xbfc00009);
	expect(EINVAL);
	memcpy(image, original, IMAGE_SIZE);
	put32(PH(1, 0), 0);
	expect(EINVAL);
	memcpy(image, original, IMAGE_SIZE);
	for (unsigned int i = 0; i < 5; i++)
		put32(PH(i, 0), 0);
	expect(EINVAL);
	memcpy(image, original, IMAGE_SIZE);
}

static void
argument_tests(void)
{
	struct sun60i_a733_gpu_fwimage out, saved;
	struct {
		const void *input;
		size_t length;
		uint64_t bvnc;
		void *code_pointer;
		size_t code_size;
		void *data_pointer;
		size_t data_size;
		struct sun60i_a733_gpu_fwimage *output;
		int error;
	} cases[] = {
		{ NULL, IMAGE_SIZE, BVNC, code, CAPACITY, data, CAPACITY, &out, EINVAL },
		{ image, IMAGE_SIZE, BVNC, NULL, CAPACITY, data, CAPACITY, &out, EINVAL },
		{ image, IMAGE_SIZE, BVNC, code, CAPACITY, NULL, CAPACITY, &out, EINVAL },
		{ image, IMAGE_SIZE, BVNC, code, CAPACITY, data, CAPACITY, NULL, EINVAL },
		{ image, 0, BVNC, code, CAPACITY, data, CAPACITY, &out, EINVAL },
		{ image, IMAGE_SIZE, BVNC, code, 0, data, CAPACITY, &out, EINVAL },
		{ image, IMAGE_SIZE, BVNC, code, CAPACITY, data, 0, &out, EINVAL },
		{ image, IMAGE_SIZE, 0, code, CAPACITY, data, CAPACITY, &out, EINVAL },
		{ image, IMAGE_SIZE, BVNC ^ 1, code, CAPACITY, data, CAPACITY, &out, ENODEV },
		{ image, IMAGE_SIZE, BVNC, code, REQUIRED - 1, data, CAPACITY, &out, ENOSPC },
		{ image, IMAGE_SIZE, BVNC, code, CAPACITY, data, REQUIRED - 1, &out, ENOSPC },
		{ image, IMAGE_SIZE, BVNC, image, CAPACITY, data, CAPACITY, &out, EINVAL },
		{ image, IMAGE_SIZE, BVNC, code, CAPACITY, image + 1, CAPACITY, &out, EINVAL },
		{ image, IMAGE_SIZE, BVNC, code, CAPACITY, code + 1, CAPACITY, &out, EINVAL },
		{ image, IMAGE_SIZE, BVNC, &out, sizeof(out), data, CAPACITY, &out, EINVAL },
		{ image, IMAGE_SIZE, BVNC, code, CAPACITY, &out, sizeof(out), &out, EINVAL },
		{ &out, sizeof(out), BVNC, code, CAPACITY, data, CAPACITY, &out, EINVAL },
		{ image, SIZE_MAX, BVNC, code, CAPACITY, data, CAPACITY, &out, EINVAL },
		{ image, IMAGE_SIZE, BVNC, code, SIZE_MAX, data, CAPACITY, &out, EINVAL },
		{ image, IMAGE_SIZE, BVNC, code, CAPACITY, data, SIZE_MAX, &out, EINVAL },
		{ (void *)(UINTPTR_MAX - 2), 4, BVNC, code, CAPACITY, data, CAPACITY, &out, EINVAL },
		{ image, IMAGE_SIZE, BVNC, code, CAPACITY, data, CAPACITY,
		    (struct sun60i_a733_gpu_fwimage *)(UINTPTR_MAX - 2), EINVAL }
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		fill_outputs(&out);
		memcpy(&saved, &out, sizeof(saved));
		CHECK(sun60i_a733_gpu_fwimage_prepare(cases[i].input,
		    cases[i].length, cases[i].bvnc, cases[i].code_pointer,
		    cases[i].code_size, cases[i].data_pointer, cases[i].data_size,
		    cases[i].output) == cases[i].error);
		CHECK(memcmp(&saved, &out, sizeof(out)) == 0);
		CHECK(memcmp(image, original, IMAGE_SIZE) == 0);
		unchanged(code, sizeof(code));
		unchanged(data, sizeof(data));
	}
	/* Bounds reach the production metadata parser before any ELF read. */
	for (size_t size = 1; size < IMAGE_SIZE; size++) {
		fill_outputs(&out);
		CHECK(sun60i_a733_gpu_fwimage_prepare(image, size, BVNC, code,
		    CAPACITY, data, CAPACITY, &out) != 0);
		unchanged(&out, sizeof(out));
		unchanged(code, sizeof(code));
		unchanged(data, sizeof(data));
	}
}

static void
mutation_tests(void)
{
	uint32_t state = 0x67318ab2;

	for (unsigned int i = 0; i < 4000; i++) {
		size_t offset;

		memcpy(image, original, IMAGE_SIZE);
		state = state * 1664525U + 1013904223U;
		offset = state % 212;
		state = state * 1664525U + 1013904223U;
		image[offset] ^= (uint8_t)((state >> 16) | 1);
		expect(-1);
	}
	memcpy(image, original, IMAGE_SIZE);
}

static void
blob_test(const char *path)
{
	struct sun60i_a733_gpu_fwimage out;
	uint8_t *blob, *saved, *blob_code, *blob_data, *want_code, *want_data;
	FILE *fp;
	const size_t size = 131072, code_size = 102400, data_size = 20480;

	fp = fopen(path, "rb");
	CHECK(fp != NULL);
	blob = malloc(size);
	saved = malloc(size);
	blob_code = malloc(code_size + 16);
	blob_data = malloc(data_size + 16);
	want_code = calloc(1, code_size);
	want_data = calloc(1, data_size);
	CHECK(blob != NULL && saved != NULL && blob_code != NULL &&
	    blob_data != NULL && want_code != NULL && want_data != NULL);
	CHECK(fread(blob, 1, size, fp) == size);
	CHECK(fgetc(fp) == EOF && ferror(fp) == 0);
	CHECK(fclose(fp) == 0);
	memcpy(saved, blob, size);
	memset(blob_code, 0xa5, code_size + 16);
	memset(blob_data, 0xa5, data_size + 16);
	CHECK(sun60i_a733_gpu_fwimage_prepare(blob, size, BVNC, blob_code,
	    code_size + 16, blob_data, data_size + 16, &out) == 0);
	CHECK(out.info.allocation_size[A733_GPU_FW_CODE] == code_size);
	CHECK(out.info.allocation_size[A733_GPU_FW_DATA] == data_size);
	CHECK(out.entry == 0xbfc00001 && out.entry_offset == 98304);
	CHECK(out.load_count == 5);
	CHECK(out.section[1].alloc_offset == 94208);
	CHECK(out.section[4].alloc_offset == 12288);
	CHECK(out.section[5].alloc_offset == 16384);
	/* Expected opaque copies come from the separately recorded ELF headers. */
	memcpy(want_code + 94208, blob + 212, 3088);
	memcpy(want_code + 98304, blob + 3300, 1748);
	memcpy(want_code, blob + 5048, 93916);
	memcpy(want_data + 8, blob + 99016, 10712);
	CHECK(memcmp(want_code, blob_code, code_size) == 0);
	CHECK(memcmp(want_data, blob_data, data_size) == 0);
	CHECK(memcmp(saved, blob, size) == 0);
	unchanged(blob_code + code_size, 16);
	unchanged(blob_data + data_size, 16);
	free(want_data);
	free(want_code);
	free(blob_data);
	free(blob_code);
	free(saved);
	free(blob);
	puts("pinned firmware: 5 opaque loads, code=102400, data=20480; CPU only");
}

int
main(int argc, char **argv)
{
	CHECK(argc <= 2);
	fixture();
	success_tests();
	corrupt_tests();
	argument_tests();
	mutation_tests();
	if (argc == 2)
		blob_test(argv[1]);
	printf("A733 GPU firmware image: %u production checks passed\n", checks);
	return 0;
}
