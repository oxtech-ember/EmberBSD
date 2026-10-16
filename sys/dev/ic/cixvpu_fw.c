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

/*
 * Original, AI-assisted implementation from the functional image format.
 * Format reference: linux-sky1 drivers/media/platform/cix, revision
 * 57e018a398248d7e5e4d798610df79a557c0629f. No GPL implementation is copied.
 */

#ifdef _KERNEL
#include <sys/null.h>
#endif

#include "cixvpu_fw.h"

static uint32_t
cixvpu_fw_le32(const uint8_t *p)
{

	return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
	    (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void
cixvpu_fw_bytes(uint8_t *dst, const uint8_t *src, size_t count)
{
	size_t i;

	for (i = 0; i < count; i++)
		dst[i] = src[i];
}

enum cixvpu_fw_error
cixvpu_fw_parse(const void *image, size_t image_size,
    struct cixvpu_fw_info *info)
{
	struct cixvpu_fw_info parsed = { 0 };
	const uint8_t *p = image;
	uint32_t bss_end, shared_end, shared_first, shared_limit, i;

	if (image == NULL || info == NULL)
		return CIXVPU_FW_ARGUMENT;
	if (image_size < CIXVPU_FW_HEADER_SIZE)
		return CIXVPU_FW_SHORT;

	parsed.jump = cixvpu_fw_le32(p);
	parsed.minor = p[4];
	parsed.major = p[5];
	if (parsed.major != 3)
		return CIXVPU_FW_PROTOCOL;
	/* Bytes 6 and 7 are nonzero in the maintained 3.5 HEVC image. */
	parsed.format_bytes[0] = p[6];
	parsed.format_bytes[1] = p[7];
	cixvpu_fw_bytes(parsed.description, p + 8, sizeof(parsed.description));
	cixvpu_fw_bytes(parsed.part, p + 64, sizeof(parsed.part));
	cixvpu_fw_bytes(parsed.revision, p + 72, sizeof(parsed.revision));
	cixvpu_fw_bytes(parsed.version, p + 80, sizeof(parsed.version));
	parsed.text_size = cixvpu_fw_le32(p + 96);
	parsed.bss_address = cixvpu_fw_le32(p + 100);
	parsed.bss_pages = cixvpu_fw_le32(p + 104);
	for (i = 0; i < CIXVPU_FW_BITMAP_WORDS; i++)
		parsed.bitmap[i] = cixvpu_fw_le32(p + 108 + i * 4);
	parsed.shared_address = cixvpu_fw_le32(p + 172);
	parsed.shared_size = cixvpu_fw_le32(p + 176);

	if (parsed.text_size < CIXVPU_FW_HEADER_SIZE ||
	    parsed.text_size > image_size ||
	    parsed.text_size > CIXVPU_FW_REGION_SIZE)
		return CIXVPU_FW_TEXT;
	parsed.text_pages = (parsed.text_size + CIXVPU_FW_PAGE_SIZE - 1) /
	    CIXVPU_FW_PAGE_SIZE;
	if (parsed.bss_pages > CIXVPU_FW_BITMAP_WORDS * 32)
		return CIXVPU_FW_BITMAP;
	if (parsed.bss_address % CIXVPU_FW_PAGE_SIZE != 0 ||
	    parsed.bss_address > CIXVPU_FW_REGION_SIZE ||
	    parsed.text_pages * CIXVPU_FW_PAGE_SIZE > parsed.bss_address ||
	    parsed.bss_pages > (CIXVPU_FW_REGION_SIZE -
	    parsed.bss_address) / CIXVPU_FW_PAGE_SIZE)
		return CIXVPU_FW_BSS;
	bss_end = parsed.bss_address + parsed.bss_pages * CIXVPU_FW_PAGE_SIZE;

	/* Check the byte extent before page rounding. All sums stay bounded. */
	if (parsed.shared_address % CIXVPU_FW_PAGE_SIZE != 0 ||
	    parsed.shared_address < parsed.bss_address ||
	    parsed.shared_address > bss_end ||
	    parsed.shared_size > bss_end - parsed.shared_address)
		return CIXVPU_FW_SHARED;
	parsed.shared_pages = (parsed.shared_size + CIXVPU_FW_PAGE_SIZE - 1) /
	    CIXVPU_FW_PAGE_SIZE;
	shared_end = parsed.shared_address +
	    parsed.shared_pages * CIXVPU_FW_PAGE_SIZE;
	if (shared_end > bss_end)
		return CIXVPU_FW_SHARED;
	shared_first = (parsed.shared_address - parsed.bss_address) /
	    CIXVPU_FW_PAGE_SIZE;
	shared_limit = shared_first + parsed.shared_pages;
	for (i = 0; i < parsed.bss_pages; i++) {
		/* Shared pages are mapped even when their bitmap bits are clear. */
		if (i >= shared_first && i < shared_limit)
			continue;
		if ((parsed.bitmap[i / 32] & ((uint32_t)1 << (i % 32))) != 0)
			parsed.private_pages++;
	}

	*info = parsed;
	return CIXVPU_FW_OK;
}

const char *
cixvpu_fw_strerror(enum cixvpu_fw_error error)
{

	switch (error) {
	case CIXVPU_FW_OK:
		return "valid firmware layout";
	case CIXVPU_FW_ARGUMENT:
		return "invalid parser argument";
	case CIXVPU_FW_SHORT:
		return "firmware header is truncated";
	case CIXVPU_FW_PROTOCOL:
		return "unsupported firmware protocol (requires 3.x)";
	case CIXVPU_FW_TEXT:
		return "invalid firmware text extent";
	case CIXVPU_FW_BITMAP:
		return "firmware BSS bitmap exceeds 512 bits";
	case CIXVPU_FW_BSS:
		return "invalid firmware BSS page extent";
	case CIXVPU_FW_SHARED:
		return "invalid firmware shared page extent";
	default:
		return "unknown firmware parser error";
	}
}
