/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native RTL8822C firmware contracts, AI-assisted. */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rtw88_hw.h"

static unsigned int failures;
static unsigned int assertions;

#define CHECK(expr) do { \
	assertions++; \
	if (!(expr)) { \
		fprintf(stderr, "%s:%d: FAIL: %s\n", __func__, __LINE__, \
		    #expr); \
		failures++; \
	} \
} while (0)

/* Synthetic 9.9.11 metadata; reserved fields and build date are zero. */
static const uint8_t header[64] = {
	0x22, 0x88, 0x00, 0x00, 0x09, 0x00, 0x09, 0x0b,
	0x00, 0x00, 0x00, 0x00, 0xe7, 0x01, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x10, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x20, 0x80, 0x80, 0x4b, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x30, 0xd3, 0x00, 0x00, 0xd0, 0xf7, 0x01, 0x00,
	0x00, 0x00, 0x10, 0x80, 0x00, 0x00, 0x03, 0x80
};

static uint8_t image[202457];

static void
reset_image(void)
{

	memset(image, 0, sizeof(image));
	memcpy(image, header, sizeof(header));
}

static void
put32(uint8_t *p, uint32_t v)
{

	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

/* A failure must not publish any partially validated section. */
static void
reject(const uint8_t *data, size_t size, int expected)
{
	struct rtw88_fw_info info, before;

	memset(&info, 0xa5, sizeof(info));
	memcpy(&before, &info, sizeof(before));
	CHECK(rtw88_fw_parse(data, size, &info) == expected);
	CHECK(memcmp(&info, &before, sizeof(info)) == 0);
}

/* Allocations match short lengths so sanitizers detect early decoding. */
static void
test_fw_short(void)
{
	size_t i;

	for (i = 0; i < 64; i++) {
		uint8_t *data;

		data = malloc(i == 0 ? 1 : i);
		if (data == NULL) {
			perror("malloc");
			exit(1);
		}
		memcpy(data, header, i);
		reject(data, i, EINVAL);
		free(data);
	}
	reject(NULL, 202456, EINVAL);
	CHECK(rtw88_fw_parse(header, 64, NULL) == EINVAL);
}

/* Catch u32 checksum wrapping, missing trailers, and out-of-file ranges. */
static void
test_fw_section_overflow(void)
{
	static const size_t size_fields[] = { 36, 48, 52 };
	static const size_t truncated[] = { 64, 19399, 73471, 202455 };
	size_t i;

	for (i = 0; i < sizeof(size_fields) / sizeof(size_fields[0]); i++) {
		reset_image();
		put32(image + size_fields[i], UINT32_MAX);
		reject(image, 202456, EOVERFLOW);
		put32(image + size_fields[i], UINT32_MAX - 7);
		reject(image, 202456, EOVERFLOW);
		put32(image + size_fields[i], UINT32_MAX - 8);
		reject(image, 202456, EINVAL);
	}
	reset_image();
	for (i = 0; i < sizeof(truncated) / sizeof(truncated[0]); i++)
		reject(image, truncated[i], EINVAL);
	reject(image, 202457, EINVAL);
}

/* Common 0x8822 signature does not identify the silicon family. */
static void
test_fw_unsupported_profile(void)
{
	static const size_t addresses[] = { 32, 56, 60 };
	static const uint32_t untagged[] = { 0x00200000, 0x00100000,
	    0x00030000 };
	size_t i;

	reset_image();
	image[0] = 0x21;
	reject(image, 202456, ENOTSUP);
	for (i = 0; i < sizeof(addresses) / sizeof(addresses[0]); i++) {
		reset_image();
		put32(image + addresses[i], 0x80000000);
		reject(image, 202456, ENOTSUP);
		put32(image + addresses[i], untagged[i]);
		reject(image, 202456, ENOTSUP);
	}
	reset_image();
	image[24] &= (uint8_t)~0x10;
	/* Disabled EMEM leaves undeclared bytes after IMEM. */
	reject(image, 202456, EINVAL);
	/* A structurally valid two-section image is still unsupported. */
	reject(image, 73472, ENOTSUP);
	reset_image();
	/* Move one payload byte between sections: total size stays valid. */
	put32(image + 36, 19329);
	put32(image + 48, 54063);
	reject(image, 202456, ENOTSUP);
}

/* Catch swapped sections, omitted checksum bytes, and endian mistakes. */
static void
exact_sections(const uint8_t *data)
{
	struct rtw88_fw_info info;
	int error;

	memset(&info, 0, sizeof(info));
	error = rtw88_fw_parse(data, 202456, &info);
	CHECK(error == 0);
	if (error != 0)
		return;
	CHECK(info.signature == 0x8822);
	CHECK(info.version == 9);
	CHECK(info.subversion == 9);
	CHECK(info.subindex == 11);
	CHECK(info.h2c_version == 15);
	CHECK(info.feature == 0x000001e7);
	CHECK(info.dmem.offset == 64);
	CHECK(info.dmem.payload_size == 19328);
	CHECK(info.dmem.size == 19336);
	CHECK(info.dmem.load_address == 0x00200000);
	CHECK(info.imem.offset == 19400);
	CHECK(info.imem.payload_size == 54064);
	CHECK(info.imem.size == 54072);
	CHECK(info.imem.load_address == 0x00030000);
	CHECK(info.emem.offset == 73472);
	CHECK(info.emem.payload_size == 128976);
	CHECK(info.emem.size == 128984);
	CHECK(info.emem.load_address == 0x00100000);
	CHECK(info.emem.offset + info.emem.size == 202456);
}

static void
test_fw_exact_sections(void)
{
	uint8_t *unaligned;

	reset_image();
	exact_sections(image);
	unaligned = malloc(202457);
	if (unaligned == NULL) {
		perror("malloc");
		exit(1);
	}
	memcpy(unaligned + 1, image, 202456);
	exact_sections(unaligned + 1);
	free(unaligned);
}

/* Fixtures are hash-verified by the wrapper, not trusted by their names. */
static void
test_fw_fixture(const char *path, size_t size, int supported)
{
	uint8_t *data;
	FILE *fp;

	fp = fopen(path, "rb");
	if (fp == NULL) {
		perror(path);
		exit(1);
	}
	data = malloc(size);
	if (data == NULL) {
		perror("malloc");
		exit(1);
	}
	if (fread(data, 1, size, fp) != size || fgetc(fp) != EOF ||
	    ferror(fp) || fclose(fp) != 0) {
		fprintf(stderr, "%s: fixture read failed\n", path);
		exit(1);
	}
	if (supported)
		exact_sections(data);
	else
		reject(data, size, ENOTSUP);
	free(data);
}

int
main(int argc, char **argv)
{

	if (argc != 1 && argc != 3) {
		fprintf(stderr, "usage: %s [RTL8822C_FIRMWARE RTL8822B_FIRMWARE]\n",
		    argv[0]);
		return 2;
	}
	test_fw_short();
	test_fw_section_overflow();
	test_fw_unsupported_profile();
	test_fw_exact_sections();
	if (argc == 3) {
		test_fw_fixture(argv[1], 202456, 1);
		test_fw_fixture(argv[2], 150984, 0);
	}
	printf("rtw88 firmware: %u assertions, %u failures%s\n", assertions,
	    failures, argc == 3 ? " (real C/B fixtures)" : " (synthetic)");
	return failures != 0;
}
