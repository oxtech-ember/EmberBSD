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

/* Original, AI-assisted EmberBSD parser for the CIX VPU firmware format. */

#ifndef _DEV_IC_CIXVPU_FW_H_
#define _DEV_IC_CIXVPU_FW_H_

#include <sys/types.h>
#ifndef _KERNEL
#include <stddef.h>
#include <stdint.h>
#endif

#define CIXVPU_FW_HEADER_SIZE	180U
#define CIXVPU_FW_PAGE_SIZE	4096U
#define CIXVPU_FW_REGION_SIZE	1048576U
#define CIXVPU_FW_BITMAP_WORDS	16U

enum cixvpu_fw_error {
	CIXVPU_FW_OK = 0,
	CIXVPU_FW_ARGUMENT,
	CIXVPU_FW_SHORT,
	CIXVPU_FW_PROTOCOL,
	CIXVPU_FW_TEXT,
	CIXVPU_FW_BITMAP,
	CIXVPU_FW_BSS,
	CIXVPU_FW_SHARED
};

struct cixvpu_fw_info {
	uint32_t jump;
	uint8_t minor;
	uint8_t major;
	uint8_t format_bytes[2];
	/* These byte fields need not contain a terminating NUL. */
	uint8_t description[56];
	uint8_t part[8];
	uint8_t revision[8];
	uint8_t version[16];
	uint32_t text_size;
	uint32_t text_pages;
	uint32_t bss_address;
	uint32_t bss_pages;
	uint32_t bitmap[CIXVPU_FW_BITMAP_WORDS];
	uint32_t shared_address;
	uint32_t shared_size;
	uint32_t shared_pages;
	uint32_t private_pages;
};

/*
 * Parse a non-secure protocol 3.x image. The image may be unaligned.
 * Read only its first CIXVPU_FW_HEADER_SIZE bytes. image_size must describe
 * the full image extent, including text. Ignore trailing image bytes.
 * Keep *info unchanged on error. No firmware is executed or authenticated.
 */
enum cixvpu_fw_error cixvpu_fw_parse(const void *, size_t,
    struct cixvpu_fw_info *);
const char *cixvpu_fw_strerror(enum cixvpu_fw_error);

#endif /* _DEV_IC_CIXVPU_FW_H_ */
