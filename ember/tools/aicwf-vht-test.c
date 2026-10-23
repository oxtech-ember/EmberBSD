/* SPDX-License-Identifier: BSD-2-Clause */
/* Exercise the production capability builder and association decoder. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef __NetBSD__
static uint16_t
le16dec(const void *p)
{
	const uint8_t *b = p;

	return (uint16_t)(b[0] | (uint16_t)b[1] << 8);
}

static uint32_t
le32dec(const void *p)
{
	const uint8_t *b = p;

	return b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 |
	    (uint32_t)b[3] << 24;
}

static void
le16enc(void *p, uint16_t v)
{
	uint8_t *b = p;

	b[0] = v;
	b[1] = v >> 8;
}

static void
le32enc(void *p, uint32_t v)
{
	uint8_t *b = p;

	b[0] = v;
	b[1] = v >> 8;
	b[2] = v >> 16;
	b[3] = v >> 24;
}

#endif

#include "if_aicwf_vht.h"

static unsigned checks;
#define CHECK(e) do { assert(e); checks++; } while (0)

static void
wire_checks(void)
{
	const uint32_t fw = 0x06090101, phy = 0x5ee24111, feat = 0x01e877d7;
	const uint8_t vht_bytes[12] = {
		0x30, 0x01, 0x80, 0x01, 0xfe, 0xff, 0x86, 0x01,
		0xfe, 0xff, 0x86, 0x01
	};
	uint8_t buf[114], ht[112] = { 0 }, want[112];
	unsigned i;

	/* Existing HT40 wire request, independent of the production offsets. */
	ht[0] = 0x6f;
	ht[1] = 1;
	ht[2] = 0x1f;
	ht[3] = 0xff;
	ht[100] = 100;
	ht[102] = 1;
	ht[103] = 1;
	memcpy(want, ht, sizeof(want));
	memcpy(want + 32, vht_bytes, sizeof(vht_bytes));
	want[102] = 2;
	want[104] = 1;
	memset(buf, 0xa5, sizeof(buf));
	CHECK(aicwf_me_config_build(buf + 1, fw, phy, feat, true));
	CHECK(buf[0] == 0xa5 && buf[113] == 0xa5);
	CHECK(memcmp(buf + 1, want, sizeof(want)) == 0);
	/* Zero maps would claim MCS 0-7 on all eight spatial streams. */
	for (i = 1; i < 8; i++) {
		CHECK(((le16dec(buf + 37) >> (2 * i)) & 3) == 3);
		CHECK(((le16dec(buf + 41) >> (2 * i)) & 3) == 3);
	}
	CHECK(!aicwf_me_config_build(buf + 1, fw ^ 1, phy, feat, true));
	CHECK(memcmp(buf + 1, ht, sizeof(ht)) == 0);
	CHECK(!aicwf_me_config_build(buf + 1, fw, phy, feat, false));
	CHECK(memcmp(buf + 1, ht, sizeof(ht)) == 0);
	CHECK(!aicwf_me_config_build(buf + 1, fw, phy, feat & ~0x200u, true));
	CHECK(memcmp(buf + 1, ht, sizeof(ht)) == 0);
	CHECK(!aicwf_me_config_build(buf + 1, fw, phy & ~0x400000u, feat, true));
	CHECK(memcmp(buf + 1, ht, sizeof(ht)) == 0);
	CHECK(!aicwf_me_config_build(buf + 1, fw, phy & ~0xf00u, feat, true));
	CHECK(memcmp(buf + 1, ht, sizeof(ht)) == 0);
	CHECK(!aicwf_me_config_build(buf + 1, fw,
	    (phy & ~0x3000000u) | 0x1000000u, feat, true));
	CHECK(memcmp(buf + 1, ht, sizeof(ht)) == 0);
	/* A wider/multiple-stream PHY remains restricted to our 1SS80 request. */
	CHECK(aicwf_me_config_build(buf + 1, fw,
	    (phy & ~0xf00u) | 0x200u | 0x3000000u, feat, true));
	CHECK(memcmp(buf + 1, want, sizeof(want)) == 0);
}

static void
ind_make(uint8_t ind[852])
{
	/* Request: VHT capability. Response: capability and VHT operation. */
	memset(ind, 0, 852);
	le16enc(ind + 14, 14);
	le16enc(ind + 16, 21);
	ind[20] = 191;
	ind[21] = 12;
	le32enc(ind + 22, 0x01800130);
	le16enc(ind + 26, 0xfffe);
	le16enc(ind + 30, 0xfffe);
	ind[34] = 191;
	ind[35] = 12;
	le32enc(ind + 36, 0x038001b1);
	le16enc(ind + 40, 0xfffa);
	le16enc(ind + 44, 0xfffa);
	ind[48] = 192;
	ind[49] = 5;
	ind[50] = 1;
	ind[51] = 42;
	ind[822] = 1;
	le16enc(ind + 824, 5180);
	ind[826] = 2;
	le32enc(ind + 828, 5210);
}

static void
assoc_checks(void)
{
	struct aicwf_assoc_info info;
	uint8_t ind[852], *short_ind;
	size_t n;

	ind_make(ind);
	CHECK(aicwf_assoc_parse(ind, sizeof(ind), &info));
	CHECK(info.req.vht && info.rsp.vht && info.rsp.operation);
	CHECK(info.req.cap == 0x01800130 && info.req.rx_map == 0xfffe &&
	    info.req.tx_map == 0xfffe);
	CHECK(info.rsp.cap == 0x038001b1 && info.rsp.rx_map == 0xfffa &&
	    info.rsp.tx_map == 0xfffa);
	CHECK(info.rsp.op_width == 1 && info.rsp.seg0 == 42 && info.rsp.seg1 == 0);
	CHECK(info.channel && info.band == 1 && info.primary == 5180 &&
	    info.width == 2 && info.center1 == 5210 && info.center2 == 0);
	/* Every truncation has an exact allocation so ASan catches overreads. */
	for (n = 0; n <= sizeof(ind); n++) {
		short_ind = malloc(n != 0 ? n : 1);
		assert(short_ind != NULL);
		memcpy(short_ind, ind, n);
		CHECK(aicwf_assoc_parse(short_ind, n, &info) == (n >= 55));
		if (n >= 55)
			CHECK(info.channel == (n >= 836));
		free(short_ind);
	}
	le16enc(ind + 14, 801);
	CHECK(!aicwf_assoc_parse(ind, sizeof(ind), &info));
	le16enc(ind + 14, 790);
	le16enc(ind + 16, 11);
	CHECK(!aicwf_assoc_parse(ind, sizeof(ind), &info));
	le16enc(ind + 14, 65535);
	CHECK(!aicwf_assoc_parse(ind, sizeof(ind), &info));
	ind_make(ind);
	ind[21] = 11;
	CHECK(!aicwf_assoc_parse(ind, sizeof(ind), &info));
	ind[21] = 13;
	CHECK(!aicwf_assoc_parse(ind, sizeof(ind), &info));
	ind_make(ind);
	ind[49] = 4;
	CHECK(!aicwf_assoc_parse(ind, sizeof(ind), &info));
	ind[49] = 6;
	CHECK(!aicwf_assoc_parse(ind, sizeof(ind), &info));
	ind_make(ind);
	le16enc(ind + 16, 22);
	CHECK(!aicwf_assoc_parse(ind, sizeof(ind), &info));
	/* Duplicate VHT capability elements cannot establish one interpretation. */
	ind_make(ind);
	memcpy(ind + 48, ind + 34, 14);
	le16enc(ind + 16, 28);
	CHECK(!aicwf_assoc_parse(ind, sizeof(ind), &info));
	/* Empty and unrelated IEs do not become VHT evidence. */
	memset(ind, 0, sizeof(ind));
	CHECK(aicwf_assoc_parse(ind, 20, &info));
	CHECK(!info.req.vht && !info.rsp.vht && !info.channel);
	ind[20] = 1;
	ind[21] = 0;
	le16enc(ind + 14, 2);
	CHECK(aicwf_assoc_parse(ind, 22, &info));
	CHECK(!info.req.vht);
	/* The full fixed IE array is valid, but cannot spill into channel fields. */
	memset(ind, 0, sizeof(ind));
	le16enc(ind + 14, 800);
	CHECK(aicwf_assoc_parse(ind, 820, &info));
	CHECK(!info.channel);
}

int
main(void)
{
	wire_checks();
	assoc_checks();
	printf("PASS: %u AIC VHT wire and association bounds checks\n", checks);
	return 0;
}
