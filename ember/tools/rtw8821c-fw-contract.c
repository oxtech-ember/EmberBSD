/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD RTL8821C firmware format checks, AI-assisted. */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rtw88_hw.h"
#include "rtw8821c.h"
#include "rtw8822c.h"

static unsigned checks, failures;
#define CHECK(e) do { checks++; if (!(e)) { \
    fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #e); failures++; \
} } while (0)

/* Metadata of linux-firmware 0a62bdca RTL8821C 24.11.0. */
static const uint8_t header[64] = {
	0x21,0x88,0,0,0x18,0,0x0b,0,0,0,0,0,7,0,0,0,
	0,0,0,0,0,0,0,0,0x18,0,0,0,0x0c,0,0,0,
	0,0,0x20,0x80,0xe8,0x7c,0,0,0,0,0,0,0,0,0,0,
	8,0xf7,0,0,0x88,0xac,0,0,0,0,0x10,0x80,0,0,3,0x80
};
static uint8_t image[139473];

static void
reset(void)
{
	memset(image, 0, sizeof(image));
	memcpy(image, header, sizeof(header));
}

static void
put32(uint8_t *p, uint32_t v)
{
	p[0] = v;
	p[1] = v >> 8;
	p[2] = v >> 16;
	p[3] = v >> 24;
}

static void
reject(const uint8_t *p, size_t n, const struct rtw88_fw_profile *profile,
    int error)
{
	struct rtw88_fw_info out, before;
	memset(&out, 0xa5, sizeof(out));
	memcpy(&before, &out, sizeof(out));
	CHECK(rtw88_fw_parse(p, n, profile, &out) == error);
	CHECK(memcmp(&out, &before, sizeof(out)) == 0);
}

static void
accepted(const uint8_t *p)
{
	struct rtw88_fw_info out;
	int error = rtw88_fw_parse(p, 139472, &rtw8821c_fw_profile, &out);
	CHECK(error == 0);
	if (error != 0)
		return;
	CHECK(out.signature == 0x8821 && out.version == 24);
	CHECK(out.subversion == 11 && out.subindex == 0);
	CHECK(out.h2c_version == 12 && out.feature == 7);
	CHECK(out.dmem.offset == 64 && out.dmem.size == 31984);
	CHECK(out.dmem.payload_size == 31976);
	CHECK(out.dmem.load_address == 0x00200000);
	CHECK(out.imem.offset == 32048 && out.imem.size == 63248);
	CHECK(out.imem.payload_size == 63240);
	CHECK(out.imem.load_address == 0x00030000);
	CHECK(out.emem.offset == 95296 && out.emem.size == 44176);
	CHECK(out.emem.payload_size == 44168);
	CHECK(out.emem.load_address == 0x00100000);
	CHECK(out.emem.offset + out.emem.size == 139472);
}

static void
bounds(void)
{
	static const size_t fields[] = { 36, 48, 52 };
	static const size_t addresses[] = { 32, 60, 56 };
	static const size_t ends[] = { 64, 32047, 95295, 139471, 139473 };
	size_t i;
	uint8_t *p;
	for (i = 0; i < 64; i++) {
		p = malloc(i == 0 ? 1 : i);
		if (p == NULL)
			exit(2);
		memcpy(p, header, i);
		reject(p, i, &rtw8821c_fw_profile, EINVAL);
		free(p);
	}
	reset();
	reject(NULL, 139472, &rtw8821c_fw_profile, EINVAL);
	reject(image, 139472, NULL, EINVAL);
	CHECK(rtw88_fw_parse(image, 139472, &rtw8821c_fw_profile, NULL) == EINVAL);
	for (i = 0; i < sizeof(ends) / sizeof(ends[0]); i++)
		reject(image, ends[i], &rtw8821c_fw_profile, EINVAL);
	for (i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
		reset();
		put32(image + fields[i], UINT32_MAX);
		reject(image, 139472, &rtw8821c_fw_profile, EOVERFLOW);
		put32(image + fields[i], UINT32_MAX - 7);
		reject(image, 139472, &rtw8821c_fw_profile, EOVERFLOW);
		put32(image + fields[i], UINT32_MAX - 8);
		reject(image, 139472, &rtw8821c_fw_profile, EINVAL);
	}
	for (i = 0; i < sizeof(addresses) / sizeof(addresses[0]); i++) {
		reset();
		image[addresses[i] + 3] &= 0x7f;
		reject(image, 139472, &rtw8821c_fw_profile, ENOTSUP);
		put32(image + addresses[i], UINT32_MAX);
		reject(image, 139472, &rtw8821c_fw_profile, ENOTSUP);
	}
	reset();
	put32(image + 36, 31977);
	put32(image + 48, 63239);
	reject(image, 139472, &rtw8821c_fw_profile, ENOTSUP);
	reset();
	image[24] &= ~0x10;
	reject(image, 139472, &rtw8821c_fw_profile, EINVAL);
	reject(image, 95296, &rtw8821c_fw_profile, ENOTSUP);
	reset();
	reject(image, 139472, &rtw8822c_fw_profile, ENOTSUP);
	image[0] = 0x22;
	reject(image, 139472, &rtw8821c_fw_profile, ENOTSUP);
	/* A substituted signature cannot turn the 8821C layout into 8822C. */
	reject(image, 139472, &rtw8822c_fw_profile, ENOTSUP);
	reset();
	accepted(image);
	p = malloc(139473);
	if (p == NULL)
		exit(2);
	memcpy(p + 1, image, 139472);
	accepted(p + 1);
	free(p);
}

int
main(int argc, char **argv)
{

	if (argc != 1 && argc != 2)
		return 2;
	bounds();
	if (argc == 2) {
		FILE *fp;
		size_t n;
		int error, close_error;

		fp = fopen(argv[1], "rb");
		if (fp == NULL)
			return 2;
		n = fread(image, 1, sizeof(image), fp);
		error = ferror(fp);
		close_error = fclose(fp);
		if (n != 139472 || error || close_error)
			return 2;
		accepted(image);
	}
	printf("rtw8821c firmware: %u checks, %u failures\n", checks, failures);
	return failures != 0;
}
