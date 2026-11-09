/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD Realtek Bluetooth patch image contract, AI-assisted. */
/*
 * Host checks for usr.sbin/rtlbtfw/rtlbt_fw.c: chip identification, patch
 * selection, image assembly, rejection of malformed files, and the fragment
 * index sequence. The HCI transport and hardware remain outside this test.
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rtlbt_fw.h"

static unsigned assertions, failures;

#define CHECK(x)                                                               \
	do {                                                                   \
		assertions++;                                                  \
		if (!(x)) {                                                    \
			failures++;                                            \
			fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__,     \
			    #x);                                               \
		}                                                              \
	} while (0)

static void
put16(uint8_t *p, uint16_t v)
{

	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static void
put32(uint8_t *p, uint32_t v)
{

	put16(p, (uint16_t)v);
	put16(p + 2, (uint16_t)(v >> 16));
}

/*
 * Build a v1 file with two patches (chip ids 1 and 2), a project id
 * record, and the extension signature. Return the length.
 */
static size_t
make_v1(uint8_t *buf, size_t size, uint8_t project, uint32_t version)
{
	static const uint8_t patch1[8] = { 1, 1, 1, 1, 0xde, 0xad, 0xbe, 0xef };
	static const uint8_t patch2[12] = { 2, 2, 2, 2, 2, 2, 2, 2, 9, 9, 9, 9 };
	size_t off = 0, table = 14, p1, p2;

	memset(buf, 0, size);
	memcpy(buf, "Realtech", 8);
	put32(buf + 8, version);
	put16(buf + 12, 2);
	put16(buf + table, 1);
	put16(buf + table + 2, 2);
	put16(buf + table + 4, sizeof(patch1));
	put16(buf + table + 6, sizeof(patch2));
	p1 = table + 16;
	p2 = p1 + sizeof(patch1);
	put32(buf + table + 8, (uint32_t)p1);
	put32(buf + table + 12, (uint32_t)p2);
	memcpy(buf + p1, patch1, sizeof(patch1));
	memcpy(buf + p2, patch2, sizeof(patch2));
	off = p2 + sizeof(patch2);
	/*
	 * Records are read backwards from the signature: the walk must skip
	 * the unrelated record (opcode 5, 2 bytes) to reach the project id.
	 */
	buf[off++] = project;
	buf[off++] = 1;
	buf[off++] = 0;
	buf[off++] = 0x11;
	buf[off++] = 0x22;
	buf[off++] = 2;
	buf[off++] = 5;
	buf[off++] = 0x51;
	buf[off++] = 0x04;
	buf[off++] = 0xfd;
	buf[off++] = 0x77;
	return off;
}

static void
test_chip_lookup(void)
{
	struct rtlbt_version v = { 8, 0x000c, 8, RTLBT_MANUFACTURER, 0x8821 };
	const struct rtlbt_chip *chip;

	chip = rtlbt_chip_lookup(&v);
	CHECK(chip != NULL && strcmp(chip->name, "rtl8821c") == 0);
	CHECK(chip != NULL && strcmp(chip->fw_suffix, "_fw.bin") == 0);
	/* Patched RTL8821C reports its firmware version instead. */
	v.hci_revision = 0x75b8;
	v.lmp_subversion = 0xf098;
	CHECK(rtlbt_chip_lookup(&v) == NULL);
	/* Another vendor with the same numbers is never patched. */
	v.hci_revision = 0x000c;
	v.lmp_subversion = 0x8821;
	v.manufacturer = 0x000f;
	CHECK(rtlbt_chip_lookup(&v) == NULL);
	/* A chip whose file uses the v2 suffix is still identified. */
	v.manufacturer = RTLBT_MANUFACTURER;
	v.lmp_subversion = 0x8852;
	v.hci_revision = 0x000c;
	v.hci_version = 0x0c;
	chip = rtlbt_chip_lookup(&v);
	CHECK(chip != NULL && strcmp(chip->fw_suffix, "_fw_v2.bin") == 0);
	/* 8723B and 8723D share the LMP subversion; revision and version differ. */
	v.lmp_subversion = 0x8723;
	v.hci_revision = 0x000b;
	v.hci_version = 0x06;
	chip = rtlbt_chip_lookup(&v);
	CHECK(chip != NULL && strcmp(chip->name, "rtl8723b") == 0);
	CHECK(chip != NULL && chip->flags == 0);
	v.hci_revision = 0x000d;
	v.hci_version = 0x08;
	chip = rtlbt_chip_lookup(&v);
	CHECK(chip != NULL && strcmp(chip->name, "rtl8723d") == 0);
	CHECK(chip != NULL && (chip->flags & RTLBT_CHIP_CONFIG) != 0);
	v.hci_version = 0x06;
	CHECK(rtlbt_chip_lookup(&v) == NULL);
	/* The RTL8723A raw image format is not supported. */
	v.lmp_subversion = 0x1200;
	v.hci_revision = 0x000b;
	CHECK(rtlbt_chip_lookup(&v) == NULL);
}

static void
test_image_build(void)
{
	static const uint8_t cfg[3] = { 0xaa, 0xbb, 0xcc };
	uint8_t fw[128];
	struct rtlbt_image img;
	const char *why;
	size_t len;
	int error;

	len = make_v1(fw, sizeof(fw), 10, 0x75b8f098);

	/* ROM version 1 selects chip id 2; version replaces the tail. */
	error = rtlbt_image_build(fw, len, cfg, sizeof(cfg), 1, 0x8821, &img,
	    &why);
	CHECK(error == 0);
	CHECK(img.len == 12 + sizeof(cfg));
	CHECK(img.data != NULL && memcmp(img.data, "\2\2\2\2\2\2\2\2", 8) == 0);
	CHECK(img.data != NULL &&
	    memcmp(img.data + 8, "\x98\xf0\xb8\x75", 4) == 0);
	CHECK(img.data != NULL && memcmp(img.data + 12, cfg, sizeof(cfg)) == 0);
	free(img.data);

	/* ROM version 0 selects chip id 1; no config file. */
	error = rtlbt_image_build(fw, len, NULL, 0, 0, 0x8821, &img, &why);
	CHECK(error == 0);
	CHECK(img.len == 8);
	CHECK(img.data != NULL && memcmp(img.data, "\1\1\1\1\x98\xf0\xb8\x75", 8)
	    == 0);
	free(img.data);

	/* No patch for this ROM version. */
	error = rtlbt_image_build(fw, len, NULL, 0, 7, 0x8821, &img, &why);
	CHECK(error == ENOENT && img.data == NULL);

	/* Project id 10 is RTL8821C; another chip must not accept it. */
	error = rtlbt_image_build(fw, len, NULL, 0, 1, 0x8822, &img, &why);
	CHECK(error == EINVAL && img.data == NULL);
	CHECK(strcmp(why, "firmware is for another chip family") == 0);

	/* An empty patch table. */
	put16(fw + 12, 0);
	error = rtlbt_image_build(fw, len, NULL, 0, 1, 0x8821, &img, &why);
	CHECK(error == EINVAL && strcmp(why, "patch table overruns file") == 0);

	/* A patch shorter than its version tail. */
	len = make_v1(fw, sizeof(fw), 10, 1);
	put16(fw + 14 + 6, 3);
	error = rtlbt_image_build(fw, len, NULL, 0, 1, 0x8821, &img, &why);
	CHECK(error == EINVAL && strcmp(why, "patch entry overruns file") == 0);

	/* A patch that reaches into the extension signature. */
	len = make_v1(fw, sizeof(fw), 10, 1);
	put16(fw + 14 + 6, (uint16_t)(len - 4 - 38 + 1));
	error = rtlbt_image_build(fw, len, NULL, 0, 1, 0x8821, &img, &why);
	CHECK(error == EINVAL && strcmp(why, "patch entry overruns file") == 0);

	/* A patch offset inside the table. */
	len = make_v1(fw, sizeof(fw), 10, 1);
	put32(fw + 14 + 12, 20);
	error = rtlbt_image_build(fw, len, NULL, 0, 1, 0x8821, &img, &why);
	CHECK(error == EINVAL && strcmp(why, "patch entry overruns file") == 0);

	/* Unknown project id. */
	len = make_v1(fw, sizeof(fw), 99, 1);
	error = rtlbt_image_build(fw, len, NULL, 0, 1, 0x8821, &img, &why);
	CHECK(error == EINVAL && strcmp(why, "unknown project id") == 0);

	/* Broken extension signature. */
	len = make_v1(fw, sizeof(fw), 10, 1);
	fw[len - 1] = 0;
	error = rtlbt_image_build(fw, len, NULL, 0, 1, 0x8821, &img, &why);
	CHECK(error == EINVAL && strcmp(why, "invalid extension signature")
	    == 0);

	/* A terminator record (opcode 0xff) hides the project id. */
	len = make_v1(fw, sizeof(fw), 10, 1);
	fw[len - 5] = 0xff;
	error = rtlbt_image_build(fw, len, NULL, 0, 1, 0x8821, &img, &why);
	CHECK(error == EINVAL && strcmp(why, "project id record not found")
	    == 0);

	/* A project id record with the wrong length. */
	len = make_v1(fw, sizeof(fw), 10, 1);
	fw[len - 10] = 2;
	error = rtlbt_image_build(fw, len, NULL, 0, 1, 0x8821, &img, &why);
	CHECK(error == EINVAL &&
	    strcmp(why, "invalid project id record length") == 0);

	/* A record length that reaches into the header. */
	len = make_v1(fw, sizeof(fw), 10, 1);
	fw[len - 6] = 0x7f;
	error = rtlbt_image_build(fw, len, NULL, 0, 1, 0x8821, &img, &why);
	CHECK(error == EINVAL &&
	    strcmp(why, "extension record overruns header") == 0);

	/* The project id must be found, even for a controller with lmp 0. */
	len = make_v1(fw, sizeof(fw), 10, 1);
	fw[len - 5] = 0xff;
	error = rtlbt_image_build(fw, len, NULL, 0, 1, 0x0000, &img, &why);
	CHECK(error == EINVAL && img.data == NULL);

	/* An oversized config file is refused before any copy. */
	len = make_v1(fw, sizeof(fw), 10, 1);
	error = rtlbt_image_build(fw, len, cfg, 1 << 20, 1, 0x8821, &img, &why);
	CHECK(error == EINVAL && strcmp(why, "config file too large") == 0);

	/* Patch offset beyond the file. */
	len = make_v1(fw, sizeof(fw), 10, 1);
	put32(fw + 14 + 12, 0x1000);
	error = rtlbt_image_build(fw, len, NULL, 0, 1, 0x8821, &img, &why);
	CHECK(error == EINVAL && strcmp(why, "patch entry overruns file") == 0);

	/* Patch table larger than the file. */
	len = make_v1(fw, sizeof(fw), 10, 1);
	put16(fw + 12, 200);
	error = rtlbt_image_build(fw, len, NULL, 0, 1, 0x8821, &img, &why);
	CHECK(error == EINVAL && strcmp(why, "patch table overruns file") == 0);

	/* The v2 format is reported, not guessed. */
	len = make_v1(fw, sizeof(fw), 10, 1);
	memcpy(fw, "RTBTCore", 8);
	error = rtlbt_image_build(fw, len, NULL, 0, 1, 0x8821, &img, &why);
	CHECK(error == EOPNOTSUPP);

	/* Anything else is rejected. */
	memcpy(fw, "Realtek!", 8);
	error = rtlbt_image_build(fw, len, NULL, 0, 1, 0x8821, &img, &why);
	CHECK(error == EINVAL && strcmp(why, "unknown firmware signature") == 0);
	error = rtlbt_image_build(fw, 4, NULL, 0, 1, 0x8821, &img, &why);
	CHECK(error == EINVAL);
}

static void
test_fragments(void)
{
	uint8_t index;
	size_t count, i, len, off, total;

	/* The RTL8821C image: 34916 bytes of patch and 10 bytes of config. */
	count = rtlbt_fragment_count(34926);
	CHECK(count == 139);
	total = 0;
	for (i = 0; i < count; i++) {
		len = rtlbt_fragment(34926, i, &index, &off);
		CHECK(off == i * RTLBT_FRAG_LEN);
		CHECK(off + len <= 34926);
		if (i + 1 < count) {
			CHECK(len == RTLBT_FRAG_LEN);
			CHECK((index & RTLBT_FRAG_LAST) == 0);
		}
		total += len;
	}
	CHECK(total == 34926);
	CHECK(len == 34926 % RTLBT_FRAG_LEN);
	CHECK((index & RTLBT_FRAG_LAST) != 0);

	/* Index sequence: 0, 1..0x7f, then 1 again. */
	rtlbt_fragment(100000, 0, &index, &off);
	CHECK(index == 0);
	rtlbt_fragment(100000, 1, &index, &off);
	CHECK(index == 1);
	rtlbt_fragment(100000, 0x7f, &index, &off);
	CHECK(index == 0x7f);
	rtlbt_fragment(100000, 0x80, &index, &off);
	CHECK(index == 1);
	rtlbt_fragment(100000, 0xfe, &index, &off);
	CHECK(index == 0x7f);
	rtlbt_fragment(100000, 0xff, &index, &off);
	CHECK(index == 1);

	/* A multiple of the fragment size ends with an empty fragment. */
	CHECK(rtlbt_fragment_count(504) == 3);
	len = rtlbt_fragment(504, 2, &index, &off);
	CHECK(len == 0 && off == 504 && index == (2 | RTLBT_FRAG_LAST));

	/* A short image is one final fragment. */
	CHECK(rtlbt_fragment_count(10) == 1);
	len = rtlbt_fragment(10, 0, &index, &off);
	CHECK(len == 10 && index == RTLBT_FRAG_LAST);

	/* The last fragment of a long image carries the wrapped index. */
	CHECK(rtlbt_fragment_count(0x81 * RTLBT_FRAG_LEN + 1) == 0x82);
	len = rtlbt_fragment(0x81 * RTLBT_FRAG_LEN + 1, 0x81, &index, &off);
	CHECK(len == 1 && index == (2 | RTLBT_FRAG_LAST));
}

/* Optional: the real linux-firmware file named by RTLBT_FIRMWARE. */
static void
test_real_file(void)
{
	struct rtlbt_image img;
	const char *path, *why;
	uint8_t *buf;
	FILE *fp;
	size_t len;
	int error;

	path = getenv("RTLBT_FIRMWARE");
	if (path == NULL)
		return;
	fp = fopen(path, "rb");
	CHECK(fp != NULL);
	if (fp == NULL)
		return;
	buf = malloc(1 << 20);
	CHECK(buf != NULL);
	if (buf == NULL) {
		fclose(fp);
		return;
	}
	len = fread(buf, 1, 1 << 20, fp);
	fclose(fp);
	/* linux-firmware 0a62bdca: ROM version 1 selects the 34916-byte patch. */
	error = rtlbt_image_build(buf, len, NULL, 0, 1, 0x8821, &img, &why);
	CHECK(error == 0);
	if (error == 0) {
		printf("%s: rom 1 patch %zu bytes, %zu fragments\n", path,
		    img.len, rtlbt_fragment_count(img.len));
		CHECK(img.len == 34916);
		CHECK(memcmp(img.data + img.len - 4, "\x98\xf0\xb8\x75", 4) == 0);
		free(img.data);
	} else
		fprintf(stderr, "%s: %s\n", path, why);
	/* The firmware refuses a ROM version without a patch. */
	error = rtlbt_image_build(buf, len, NULL, 0, 9, 0x8821, &img, &why);
	CHECK(error == ENOENT);
	free(buf);
}

int
main(void)
{

	test_chip_lookup();
	test_image_build();
	test_fragments();
	test_real_file();
	printf("rtlbtfw contract: %u assertions, %u failures\n", assertions,
	    failures);
	return failures == 0 ? 0 : 1;
}
