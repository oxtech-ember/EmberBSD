/* SPDX-License-Identifier: BSD-2-Clause */
/* AI-assisted checks of actual fixed-image and IGTK wire helpers. */
#include <sys/types.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <openssl/evp.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", \
    __FILE__, __LINE__, #c); exit(1); } checks++; } while (0)
#ifndef __arraycount
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#endif
static unsigned checks;
static uint32_t
check_le32dec(const void *p)
{
	const uint8_t *b = p;
	return (uint32_t)b[0] | (uint32_t)b[1] << 8 |
	    (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}
static void
hash(const uint8_t *p, size_t n, uint8_t out[32])
{
	unsigned size = 0;
	CHECK(EVP_Digest(p, n, out, &size, EVP_sha256(), NULL) == 1);
	CHECK(size == 32);
}
static unsigned hash_calls;
static void
fault_hash(const uint8_t *p, size_t n, uint8_t out[32])
{
	hash(p, n, out);
	if (++hash_calls == 2)
		out[0] ^= 1;
}
#define le32dec check_le32dec
#include "if_aicwf_fw_bip.h"
#include "if_aicwf_sae.h"

static void
wire_contract(void)
{
	uint8_t req[24] = {4}, wire[44], saved[44];
	unsigned i, index, cap;

	for (index = 4; index <= 5; index++) {
		req[0] = index;
		for (i = 0; i < 16; i++)
			req[8 + i] = 0x80 + i;
		for (cap = 0; cap <= 1; cap++) {
			memset(req + 2, 0, 6);
			CHECK(aicwf_sae_igtk_wire(wire, req, 24, 1, cap));
			CHECK(wire[0] == index && wire[1] == 255);
			CHECK(wire[4] == 16 && wire[40] == 5);
			CHECK(wire[41] == 1 && wire[42] == 0 && wire[43] == 0);
			CHECK(memcmp(wire + 8, req + 8, 16) == 0);
			CHECK(memcmp(wire + 24, req + 2, 6) == 0);
			CHECK(memcmp(wire + 30, "\0\0\0\0\0\0\0\0\0\0", 10) == 0);
		}
		for (i = 0; i < 6; i++) {
			memset(req + 2, 0, 6);
			req[2 + i] = 0xff;
			memset(wire, 0x55, sizeof(wire));
			memcpy(saved, wire, sizeof(wire));
			CHECK(!aicwf_sae_igtk_valid(req, sizeof(req)));
			CHECK(!aicwf_sae_igtk_wire(wire, req, 24, 1, false));
			CHECK(memcmp(wire, saved, sizeof(wire)) == 0);
			CHECK(aicwf_sae_igtk_wire(wire, req, 24, 1, true));
			CHECK(memcmp(wire + 24, req + 2, 6) == 0);
		}
	}
	for (i = 0; i < 26; i++) {
		if (i != 24)
			CHECK(!aicwf_sae_igtk_wire(wire, req, i, 0, true));
	}
	req[0] = 4;
	memset(req + 2, 0, 6);
	for (i = 0; i < 256; i++)
		CHECK(aicwf_sae_igtk_wire(wire, req, 24, i, true) == (i < 4));
	CHECK(aicwf_sae_igtk_wire(wire, req, 24, 255, false));
	req[1] = 1;
	CHECK(!aicwf_sae_igtk_wire(wire, req, 24, 0, true));
	req[1] = 0;
	req[0] = 6;
	CHECK(!aicwf_sae_igtk_wire(wire, req, 24, 0, true));
}

static void
map_contract(void)
{
	uint32_t options[2][2] = {{0xb4, 0xf3010001}, {0x188, 1}};
	uint32_t a, value, saved;
	unsigned i;

	CHECK(aicwf_fw_bip_map(0x1730d4, 0x174000, 0x06090101,
	    0x179718, options, 2));
	CHECK(!aicwf_fw_bip_map(0x1730d4, 0x142be0, 0x06090101,
	    0x179718, options, 2));
	CHECK(!aicwf_fw_bip_map(0xfffffff0, 0x174000, 0x06090101,
	    0x179718, options, 2));
	CHECK(!aicwf_fw_bip_map(0x1730d4, 0x174000, 0x06090100,
	    0x179718, options, 2));
	CHECK(!aicwf_fw_bip_map(0x1730d4, 0x174000, 0x06090101,
	    0x165e48, options, 2));
	for (i = 0; i < 4; i++) {
		saved = options[i / 2][i % 2];
		options[i / 2][i % 2] ^= 1;
		CHECK(!aicwf_fw_bip_map(0x1730d4, 0x174000, 0x06090101,
		    0x179718, options, 2));
		options[i / 2][i % 2] = saved;
	}
	for (i = 0; i < 181; i++) {
		CHECK(aicwf_fw_bip_word(i, &a, &value));
		if (i < 85) {
			CHECK(a == 0x142be0 + i * 4);
			CHECK(value == check_le32dec(aicwf_fw_bip_key + i * 4));
		} else {
			CHECK(a == 0x165e48 + (i - 85) * 4);
			CHECK(value == check_le32dec(aicwf_fw_bip_rx + (i - 85) * 4));
		}
	}
	a = 123;
	value = 456;
	CHECK(!aicwf_fw_bip_word(181, &a, &value));
	CHECK(a == 123 && value == 456);
	for (i = 0; i < 12; i++)
		CHECK(aicwf_fw_bip_meta_word(i, &a, &value));
	CHECK(!aicwf_fw_bip_meta_word(12, &a, &value));
	for (i = 8; i < 12; i++) {
		CHECK(aicwf_fw_bip_meta_word(i, &a, &value));
		CHECK(a == 0x174030 + (i - 8) * 4 && value == 0);
	}
}

static void
image_contract(const char *path)
{
	uint8_t *data, *bad, digest[32];
	bool pending = true;
	FILE *f;
	static const unsigned offsets[] = {0, 1, 0x22be0, 0x45e48};
	unsigned i;

	data = calloc(1, 341201);
	CHECK(data != NULL);
	CHECK(aicwf_fw_bip_patch(data, 341200, false, &pending, hash) == 0);
	CHECK(!pending);
	if (path == NULL) {
		free(data);
		return;
	}
	f = fopen(path, "rb");
	CHECK(f != NULL);
	CHECK(fread(data, 1, 341201, f) == 341200);
	CHECK(fclose(f) == 0);
	CHECK(aicwf_fw_bip_patch(data, 341200, true, &pending, hash) == 0);
	CHECK(!pending);
	for (i = 0; i < __arraycount(offsets); i++) {
		data[offsets[i]] ^= 1;
		CHECK(aicwf_fw_bip_patch(data, 341200, false, &pending, hash) == 0);
		CHECK(!pending);
		data[offsets[i]] ^= 1;
	}
	CHECK(aicwf_fw_bip_patch(data, 341199, false, &pending, hash) == 0);
	CHECK(!pending);
	bad = malloc(341200);
	CHECK(bad != NULL);
	memcpy(bad, data, 341200);
	hash_calls = 0;
	CHECK(aicwf_fw_bip_patch(bad, 341200, false, &pending, fault_hash) == EIO);
	CHECK(!pending && hash_calls == 2);
	free(bad);
	CHECK(aicwf_fw_bip_patch(data, 341200, false, &pending, hash) == 0);
	CHECK(pending);
	hash(data, 341200, digest);
	CHECK(memcmp(digest, aicwf_fw_bip_modified_sha256, 32) == 0);
	CHECK(memcmp(data + 0x22be0, aicwf_fw_bip_key, 340) == 0);
	CHECK(memcmp(data + 0x45e48, aicwf_fw_bip_rx, 384) == 0);
	free(data);
}

int
main(int argc, char **argv)
{
	CHECK(argc == 1 || argc == 2);
	wire_contract();
	map_contract();
	image_contract(argc == 2 ? argv[1] : NULL);
	printf("PASS: %u fixed-image/wire checks; external image %s\n", checks,
	    argc == 2 ? "checked" : "not supplied");
	return 0;
}
