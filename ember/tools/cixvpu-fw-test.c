/*-
 * Copyright (c) 2026 EmberBSD contributors
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES ARE DISCLAIMED. IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE.
 */

/* Original, AI-assisted software contract. This does not test a VPU. */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../sys/dev/ic/cixvpu_fw.h"

#define IMAGE_SIZE 8192U

static unsigned int checks;

static void
require(int condition, const char *name)
{

	checks++;
	if (!condition) {
		fprintf(stderr, "FAIL: %s\n", name);
		exit(1);
	}
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
fixture(uint8_t *p)
{

	memset(p, 0, IMAGE_SIZE);
	put32(p, UINT32_C(0x12345678));
	p[4] = 5;
	p[5] = 3;
	p[6] = 8;
	p[7] = 8;
	memset(p + 8, 'A', 56);
	p[8] = 0x1b;
	p[9] = '\\';
	p[10] = '\n';
	memset(p + 64, 'B', 8);
	memset(p + 72, 'C', 8);
	memset(p + 80, 'D', 16);
	put32(p + 96, 4097);
	put32(p + 100, 8192);
	put32(p + 104, 6);
	put32(p + 108, 0x2f);
	put32(p + 172, 16384);
	put32(p + 176, 4097);
}

static void
expect_error(const uint8_t *p, size_t size, enum cixvpu_fw_error expected,
    const char *name)
{
	struct cixvpu_fw_info result, original;

	memset(&result, 0xa5, sizeof(result));
	memcpy(&original, &result, sizeof(original));
	require(cixvpu_fw_parse(p, size, &result) == expected, name);
	require(memcmp(&result, &original, sizeof(result)) == 0,
	    "error leaves output unchanged");
}

static void
bad_word(size_t offset, uint32_t value, enum cixvpu_fw_error error,
    const char *name)
{
	uint8_t image[IMAGE_SIZE];

	fixture(image);
	put32(image + offset, value);
	expect_error(image, sizeof(image), error, name);
}

int
main(int argc, char **argv)
{
	uint8_t image[IMAGE_SIZE], unaligned[IMAGE_SIZE + 16];
	struct cixvpu_fw_info info;
	size_t i;

	if (argc > 2) {
		fprintf(stderr, "usage: %s [synthetic-fixture-output]\n", argv[0]);
		return 2;
	}
	fixture(image);
	require(cixvpu_fw_parse(image, sizeof(image), &info) == CIXVPU_FW_OK,
	    "valid 3.5 fixture");
	require(info.jump == UINT32_C(0x12345678), "little endian jump");
	require(info.major == 3 && info.minor == 5, "protocol order");
	require(info.format_bytes[0] == 8 && info.format_bytes[1] == 8,
	    "nonzero format bytes are accepted");
	require(info.text_size == 4097 && info.text_pages == 2,
	    "text rounds up to pages");
	require(info.bss_address == 8192 && info.bss_pages == 6,
	    "BSS extent");
	require(info.shared_pages == 2 && info.private_pages == 3,
	    "shared pages override set bitmap bits");
	require(memcmp(info.description, image + 8, sizeof(info.description)) == 0 &&
	    memcmp(info.part, image + 64, sizeof(info.part)) == 0 &&
	    memcmp(info.revision, image + 72, sizeof(info.revision)) == 0 &&
	    memcmp(info.version, image + 80, sizeof(info.version)) == 0,
	    "fixed-width fields do not require NUL");
	for (i = 0; i < 16; i++) {
		memcpy(unaligned + i, image, sizeof(image));
		require(cixvpu_fw_parse(unaligned + i, IMAGE_SIZE, &info) ==
		    CIXVPU_FW_OK, "unaligned input");
	}
	for (i = 0; i < CIXVPU_FW_HEADER_SIZE; i++)
		expect_error(image, i, CIXVPU_FW_SHORT, "all short headers");
	expect_error(NULL, IMAGE_SIZE, CIXVPU_FW_ARGUMENT, "null input");
	require(cixvpu_fw_parse(image, IMAGE_SIZE, NULL) == CIXVPU_FW_ARGUMENT,
	    "null output");
	image[5] = 2;
	expect_error(image, sizeof(image), CIXVPU_FW_PROTOCOL,
	    "unsupported major 2");
	image[5] = 4;
	expect_error(image, sizeof(image), CIXVPU_FW_PROTOCOL,
	    "unsupported major 4");
	fixture(image);
	image[4] = 255;
	require(cixvpu_fw_parse(image, sizeof(image), &info) == CIXVPU_FW_OK,
	    "minor versions do not alter layout contract");
	bad_word(96, 0, CIXVPU_FW_TEXT, "empty text");
	bad_word(96, CIXVPU_FW_HEADER_SIZE - 1, CIXVPU_FW_TEXT,
	    "text shorter than header");
	bad_word(96, IMAGE_SIZE + 1, CIXVPU_FW_TEXT, "text beyond image");
	bad_word(96, UINT32_MAX, CIXVPU_FW_TEXT, "text arithmetic overflow");
	fixture(image);
	put32(image + 96, CIXVPU_FW_REGION_SIZE + 1);
	expect_error(image, SIZE_MAX, CIXVPU_FW_TEXT,
	    "text beyond core region with large image extent");
	bad_word(100, 4096, CIXVPU_FW_BSS, "page-rounded text overlaps BSS");
	bad_word(100, 8193, CIXVPU_FW_BSS, "unaligned BSS");
	bad_word(100, UINT32_C(0xfffff000), CIXVPU_FW_BSS,
	    "BSS beyond core region");
	bad_word(100, CIXVPU_FW_REGION_SIZE, CIXVPU_FW_BSS,
	    "BSS pages cross region boundary");
	bad_word(104, 513, CIXVPU_FW_BITMAP, "bitmap exceeds storage");
	bad_word(104, UINT32_MAX, CIXVPU_FW_BITMAP, "bitmap count overflow");
	bad_word(104, 255, CIXVPU_FW_BSS, "BSS pages exceed region");
	bad_word(172, 16385, CIXVPU_FW_SHARED, "unaligned shared start");
	bad_word(172, 4096, CIXVPU_FW_SHARED, "shared starts before BSS");
	bad_word(172, 36864, CIXVPU_FW_SHARED, "shared starts after BSS");
	bad_word(172, UINT32_C(0xfffff000), CIXVPU_FW_SHARED,
	    "shared address overflow");
	bad_word(176, UINT32_MAX, CIXVPU_FW_SHARED,
	    "shared size arithmetic overflow");
	bad_word(176, 16385, CIXVPU_FW_SHARED,
	    "shared last byte crosses BSS");
	fixture(image);
	put32(image + 176, 16383);
	require(cixvpu_fw_parse(image, IMAGE_SIZE, &info) == CIXVPU_FW_OK &&
	    info.shared_pages == 4 && info.private_pages == 2,
	    "partial shared last page at BSS end");
	fixture(image);
	put32(image + 108, 0);
	require(cixvpu_fw_parse(image, IMAGE_SIZE, &info) == CIXVPU_FW_OK &&
	    info.shared_pages == 2 && info.private_pages == 0,
	    "shared pages count even with clear bitmap");
	fixture(image);
	put32(image + 176, 0);
	require(cixvpu_fw_parse(image, IMAGE_SIZE, &info) == CIXVPU_FW_OK &&
	    info.shared_pages == 0 && info.private_pages == 5,
	    "empty shared extent");
	put32(image + 172, 32768);
	require(cixvpu_fw_parse(image, IMAGE_SIZE, &info) == CIXVPU_FW_OK &&
	    info.shared_pages == 0, "empty shared extent at BSS end");
	fixture(image);
	put32(image + 176, 16384);
	require(cixvpu_fw_parse(image, IMAGE_SIZE, &info) == CIXVPU_FW_OK &&
	    info.shared_pages == 4 && info.private_pages == 2,
	    "shared extent ends exactly at BSS end");
	fixture(image);
	put32(image + 100, CIXVPU_FW_REGION_SIZE - CIXVPU_FW_PAGE_SIZE);
	put32(image + 104, 1);
	put32(image + 172, CIXVPU_FW_REGION_SIZE - CIXVPU_FW_PAGE_SIZE);
	put32(image + 176, CIXVPU_FW_PAGE_SIZE);
	require(cixvpu_fw_parse(image, SIZE_MAX, &info) == CIXVPU_FW_OK &&
	    info.shared_pages == 1 && info.private_pages == 0,
	    "region limit and SIZE_MAX image extent");
	fixture(image);
	put32(image + 100, 8192);
	put32(image + 104, 0);
	put32(image + 172, 8192);
	put32(image + 176, 0);
	require(cixvpu_fw_parse(image, IMAGE_SIZE, &info) == CIXVPU_FW_OK &&
	    info.private_pages == 0 && info.shared_pages == 0,
	    "empty BSS");
	fixture(image);
	put32(image + 104, 40);
	put32(image + 108 + 4, 0x81);
	require(cixvpu_fw_parse(image, IMAGE_SIZE, &info) == CIXVPU_FW_OK &&
	    info.private_pages == 5, "bitmap crosses 32-bit word boundary");
	fixture(image);
	put32(image + 108 + 15 * 4, UINT32_MAX);
	require(cixvpu_fw_parse(image, IMAGE_SIZE, &info) == CIXVPU_FW_OK &&
	    info.private_pages == 3, "bitmap bits outside BSS are ignored");
	require(strcmp(cixvpu_fw_strerror(CIXVPU_FW_TEXT),
	    "invalid firmware text extent") == 0, "error text");
	require(strcmp(cixvpu_fw_strerror((enum cixvpu_fw_error)999),
	    "unknown firmware parser error") == 0, "unknown error text");
	if (argc == 2) {
		FILE *file;

		fixture(image);
		file = fopen(argv[1], "wb");
		checks++;
		if (file == NULL) {
			perror("open synthetic fixture output");
			return EXIT_FAILURE;
		}
		require(fwrite(image, 1, sizeof(image), file) == sizeof(image),
		    "write synthetic fixture output");
		require(fclose(file) == 0, "close synthetic fixture output");
	}
	printf("PASS: %u CIX firmware parser checks (software contract only)\n",
	    checks);
	return 0;
}
