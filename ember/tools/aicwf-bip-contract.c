/* SPDX-License-Identifier: BSD-2-Clause */
/* Test the production BIP helper with independent OpenSSL CMAC and RFC 4493. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/params.h>

#include "if_aicwf_bip.h"

static unsigned checks, block_calls;
static const uint8_t peer[6] = { 2, 3, 4, 5, 6, 7 };
static const uint8_t nist_key[16] = {
	0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
	0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
};
static const uint8_t message[64] = {
	0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
	0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a,
	0xae, 0x2d, 0x8a, 0x57, 0x1e, 0x03, 0xac, 0x9c,
	0x9e, 0xb7, 0x6f, 0xac, 0x45, 0xaf, 0x8e, 0x51,
	0x30, 0xc8, 0x1c, 0x46, 0xa3, 0x5c, 0xe4, 0x11,
	0xe5, 0xfb, 0xc1, 0x19, 0x1a, 0x0a, 0x52, 0xef,
	0xf6, 0x9f, 0x24, 0x45, 0xdf, 0x4f, 0x9b, 0x17,
	0xad, 0x2b, 0x41, 0x7b, 0xe6, 0x6c, 0x37, 0x10
};

#define CHECK(x) do { \
	if (!(x)) { \
		fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); \
		exit(EXIT_FAILURE); \
	} \
	checks++; \
} while (0)

struct cipher {
	EVP_CIPHER_CTX *ctx;
};

static void
cipher_init(struct cipher *c, const uint8_t material[16])
{
	c->ctx = EVP_CIPHER_CTX_new();
	CHECK(c->ctx != NULL);
	CHECK(EVP_EncryptInit_ex(c->ctx, EVP_aes_128_ecb(), NULL,
	    material, NULL) == 1);
	CHECK(EVP_CIPHER_CTX_set_padding(c->ctx, 0) == 1);
}

static void
block_encrypt(const void *arg, const uint8_t in[16], uint8_t out[16])
{
	const struct cipher *c = arg;
	int len = 0;

	CHECK(in != out);
	CHECK(EVP_EncryptUpdate(c->ctx, out, &len, in, 16) == 1);
	CHECK(len == 16);
	block_calls++;
}

static void
oracle(const uint8_t *data, size_t len, uint8_t result[16])
{
	char name[] = "AES-128-CBC";
	OSSL_PARAM params[] = {
		OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_CIPHER, name, 0),
		OSSL_PARAM_construct_end()
	};
	EVP_MAC *algorithm = EVP_MAC_fetch(NULL, "CMAC", NULL);
	EVP_MAC_CTX *ctx;
	size_t size = 0;

	CHECK(algorithm != NULL);
	ctx = EVP_MAC_CTX_new(algorithm);
	CHECK(ctx != NULL);
	CHECK(EVP_MAC_init(ctx, nist_key, sizeof(nist_key), params) == 1);
	CHECK(EVP_MAC_update(ctx, data, len) == 1);
	CHECK(EVP_MAC_final(ctx, result, &size, 16) == 1 && size == 16);
	EVP_MAC_CTX_free(ctx);
	EVP_MAC_free(algorithm);
}

static void
sign_frame(uint8_t *frame, size_t len)
{
	uint8_t authenticated[AICWF_BIP_MAX_FRAME], mic[16];

	authenticated[0] = frame[0];
	authenticated[1] = frame[1] & 0xc7;
	memcpy(authenticated + 2, frame + 4, 18);
	memcpy(authenticated + 20, frame + 24, len - 24);
	memset(authenticated + (len - 12), 0, 8);
	oracle(authenticated, len - 4, mic);
	memcpy(frame + len - 8, mic, 8);
}

static void
make_frame(uint8_t *frame, size_t len, uint8_t subtype, uint16_t index,
    uint64_t pn)
{
	uint8_t *mmie = frame + len - 18;

	memset(frame, 0, len);
	frame[0] = subtype;
	memset(frame + 4, 0xff, 6);
	memcpy(frame + 10, peer, 6);
	memcpy(frame + 16, peer, 6);
	for (size_t i = 24; i < len - 18; i++)
		frame[i] = (uint8_t)i;
	frame[24] = subtype == 0xd0 ? 5 : 3;
	frame[25] = 0;
	mmie[0] = 76;
	mmie[1] = 16;
	mmie[2] = (uint8_t)index;
	mmie[3] = (uint8_t)(index >> 8);
	for (unsigned i = 0; i < 6; i++)
		mmie[4 + i] = (uint8_t)(pn >> (8 * i));
	sign_frame(frame, len);
}

static void
verify(const struct cipher *c, const uint8_t *frame, size_t len, uint16_t index,
    uint64_t previous, bool expected, uint64_t expected_pn)
{
	const uint64_t sentinel = UINT64_C(0xdeadbeefdeadbeef);
	uint64_t pn = sentinel;

	CHECK(aicwf_bip_verify(block_encrypt, c, frame, len, peer, peer,
	    index, previous, &pn) == expected);
	CHECK(pn == (expected ? expected_pn : sentinel));
}

static void
cmac_tests(const struct cipher *c)
{
	static const size_t sizes[] = { 0, 16, 40, 64 };
	static const uint8_t answers[4][16] = {
		{ 0xbb,0x1d,0x69,0x29,0xe9,0x59,0x37,0x28,
		  0x7f,0xa3,0x7d,0x12,0x9b,0x75,0x67,0x46 },
		{ 0x07,0x0a,0x16,0xb4,0x6b,0x4d,0x41,0x44,
		  0xf7,0x9b,0xdd,0x9d,0xd0,0x4a,0x28,0x7c },
		{ 0xdf,0xa6,0x67,0x47,0xde,0x9a,0xe6,0x30,
		  0x30,0xca,0x32,0x61,0x14,0x97,0xc8,0x27 },
		{ 0x51,0xf0,0xbe,0xbf,0x7e,0x3b,0x9d,0x92,
		  0xfc,0x49,0x74,0x17,0x79,0x36,0x3c,0xfe }
	};
	struct aicwf_bip_part parts[3];
	uint8_t result[16], expected[16], saved[16];

	for (unsigned n = 0; n < 4; n++) {
		parts[0].data = message;
		parts[0].len = sizes[n];
		CHECK(aicwf_bip_cmac(block_encrypt, c, parts, 1, result));
		CHECK(memcmp(result, answers[n], 16) == 0);
		oracle(message, sizes[n], expected);
		CHECK(memcmp(result, expected, 16) == 0);
	}
	for (size_t len = 0; len <= sizeof(message); len++) {
		oracle(message, len, expected);
		for (size_t first = 0; first <= len; first++) {
			for (size_t second = first; second <= len; second++) {
				parts[0] = (struct aicwf_bip_part){message, first};
				parts[1] = (struct aicwf_bip_part){message + first,
				    second - first};
				parts[2] = (struct aicwf_bip_part){message + second,
				    len - second};
				CHECK(aicwf_bip_cmac(block_encrypt, c, parts, 3,
				    result));
				CHECK(memcmp(result, expected, 16) == 0);
			}
		}
	}
	CHECK(aicwf_bip_cmac(block_encrypt, c, NULL, 0, result));
	CHECK(memcmp(result, answers[0], 16) == 0);
	memset(result, 0x5a, sizeof(result));
	memcpy(saved, result, sizeof(saved));
	parts[0] = (struct aicwf_bip_part){NULL, 1};
	CHECK(!aicwf_bip_cmac(block_encrypt, c, parts, 1, result));
	parts[0] = (struct aicwf_bip_part){message, SIZE_MAX};
	CHECK(!aicwf_bip_cmac(block_encrypt, c, parts, 1, result));
	CHECK(!aicwf_bip_cmac(block_encrypt, c, parts, 4, result));
	CHECK(!aicwf_bip_cmac(block_encrypt, c, NULL, 1, result));
	CHECK(!aicwf_bip_cmac(NULL, c, parts, 0, result));
	CHECK(!aicwf_bip_cmac(block_encrypt, NULL, parts, 0, result));
	CHECK(!aicwf_bip_cmac(block_encrypt, c, parts, 0, NULL));
	CHECK(memcmp(result, saved, sizeof(result)) == 0);
}

static void
frame_tests(const struct cipher *c)
{
	uint8_t frame[AICWF_BIP_MAX_FRAME + 1], saved[128], wrong[6];
	uint8_t wrong_key[16];
	struct cipher bad;
	uint64_t pn = 99;
	static const uint64_t numbers[] = { 1, 255, 256, 65535, 65536,
	    UINT64_C(0x100000000), AICWF_BIP_MAX_PN };

	for (unsigned index = 4; index <= 5; index++) {
		for (unsigned i = 0; i < sizeof(numbers) / sizeof(numbers[0]); i++) {
			make_frame(frame, 44, 0xc0, index, numbers[i]);
			verify(c, frame, 44, index, numbers[i] - 1, true, numbers[i]);
			verify(c, frame, 44, index, numbers[i], false, 0);
		}
	}
	for (unsigned index = 0; index < 8; index++) {
		if (index == 4 || index == 5)
			continue;
		make_frame(frame, 44, 0xc0, index, 42);
		verify(c, frame, 44, index, 0, false, 0);
	}
	make_frame(frame, 44, 0xa0, 4, 42);
	verify(c, frame, 44, 4, 41, true, 42);
	verify(c, frame, 44, 4, 43, false, 0);
	verify(c, frame, 44, 4, AICWF_BIP_MAX_PN + 1, false, 0);
	for (unsigned i = 0; i < 8; i++) {
		if (i != 4)
			verify(c, frame, 44, i, 0, false, 0);
	}
	for (size_t len = 45; len <= AICWF_BIP_MAX_FRAME; len++) {
		make_frame(frame, len, 0xd0, 5, 300);
		verify(c, frame, len, 5, 256, true, 300);
	}
	make_frame(frame, 128, 0xd0, 4, 257);
	memcpy(saved, frame, sizeof(saved));
	for (size_t len = 0; len < 128; len++) {
		uint8_t *exact = malloc(len == 0 ? 1 : len);
		CHECK(exact != NULL);
		memcpy(exact, frame, len);
		verify(c, exact, len, 4, 0, false, 0);
		free(exact);
	}
	make_frame(frame, sizeof(frame), 0xd0, 4, 257);
	verify(c, frame, sizeof(frame), 4, 0, false, 0);
	for (size_t i = 0; i < sizeof(saved); i++) {
		/* Duration and sequence number are not part of BIP AAD. */
		if (i == 2 || i == 3 || i == 22 || i == 23)
			continue;
		for (unsigned bit = 0; bit < 8; bit++) {
			memcpy(frame, saved, sizeof(saved));
			frame[i] ^= (uint8_t)(1U << bit);
			verify(c, frame, 128, 4, 0,
			    i == 1 && bit >= 3 && bit <= 5, 257);
		}
	}
	memcpy(frame, saved, sizeof(saved));
	frame[2] = frame[3] = frame[23] = 0xff;
	frame[22] = 0xf0;
	verify(c, frame, 128, 4, 0, true, 257);
	frame[22] |= 1;
	verify(c, frame, 128, 4, 0, false, 0);
	memcpy(frame, saved, sizeof(saved));
	frame[24] = 10;
	sign_frame(frame, 128);
	verify(c, frame, 128, 4, 0, true, 257);
	frame[24] = 4; /* Public Action is not a robust frame. */
	sign_frame(frame, 128);
	verify(c, frame, 128, 4, 0, false, 0);
/* Valid MICs must not bypass the supported header/body contract. */
for (unsigned bit = 0; bit < 8; bit++) {
	if (bit >= 3 && bit <= 5)
		continue;
	make_frame(frame, 44, 0xc0, 4, 42);
	frame[1] = (uint8_t)(1U << bit);
	sign_frame(frame, 44);
	verify(c, frame, 44, 4, 0, false, 0);
}
make_frame(frame, 44, 0xc0, 4, 42);
frame[4] &= ~1;
sign_frame(frame, 44);
verify(c, frame, 44, 4, 0, false, 0);
make_frame(frame, 45, 0xc0, 4, 42);
verify(c, frame, 45, 4, 0, false, 0);
make_frame(frame, 44, 0xd0, 4, 42);
verify(c, frame, 44, 4, 0, false, 0);
make_frame(frame, 44, 0x80, 4, 42);
verify(c, frame, 44, 4, 0, false, 0);
	make_frame(frame, 44, 0xc0, 4, 0);
	verify(c, frame, 44, 4, 0, false, 0);
	make_frame(frame, 44, 0xc0, 4, 42);
	memcpy(wrong, peer, sizeof(wrong));
	wrong[5]++;
	CHECK(!aicwf_bip_verify(block_encrypt, c, frame, 44, wrong,
	    peer, 4, 0, &pn));
	CHECK(!aicwf_bip_verify(block_encrypt, c, frame, 44, peer,
	    wrong, 4, 0, &pn));
	CHECK(!aicwf_bip_verify(block_encrypt, c, NULL, 44, peer,
	    peer, 4, 0, &pn));
	CHECK(!aicwf_bip_verify(block_encrypt, c, frame, 44, NULL,
	    peer, 4, 0, &pn));
	CHECK(!aicwf_bip_verify(block_encrypt, c, frame, 44, peer,
	    NULL, 4, 0, &pn));
	CHECK(!aicwf_bip_verify(block_encrypt, c, frame, 44, peer,
	    peer, 4, 0, NULL));
	CHECK(!aicwf_bip_verify(NULL, c, frame, 44, peer, peer, 4, 0, &pn));
	CHECK(!aicwf_bip_verify(block_encrypt, NULL, frame, 44, peer,
	    peer, 4, 0, &pn));
	CHECK(pn == 99);
	memcpy(wrong_key, nist_key, sizeof(wrong_key));
	wrong_key[0] ^= 1;
	cipher_init(&bad, wrong_key);
	verify(&bad, frame, 44, 4, 0, false, 0);
	EVP_CIPHER_CTX_free(bad.ctx);
}

int
main(void)
{
	struct cipher c;

	cipher_init(&c, nist_key);
	cmac_tests(&c);
	frame_tests(&c);
	EVP_CIPHER_CTX_free(c.ctx);
	printf("PASS: %u assertions, %u real AES block operations\n", checks,
	    block_calls);
	return EXIT_SUCCESS;
}
