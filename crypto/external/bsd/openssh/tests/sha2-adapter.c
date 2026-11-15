/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../include/emberbsd-openssh-sha2.h"

static int
check_digest(const char *name, const uint8_t *digest, size_t len,
    const char *expected)
{
	static const char hex[] = "0123456789abcdef";
	char actual[SHA512_DIGEST_LENGTH * 2 + 1];
	size_t i;

	for (i = 0; i < len; i++) {
		actual[2 * i] = hex[digest[i] >> 4];
		actual[2 * i + 1] = hex[digest[i] & 0xf];
	}
	actual[2 * len] = '\0';
	if (strcmp(actual, expected) == 0)
		return 0;
	fprintf(stderr, "%s mismatch: %s\n", name, actual);
	return 1;
}

static int
check_sha256(void)
{
	SHA2_CTX ctx;
	uint8_t digest[SHA256_DIGEST_LENGTH];

	SHA256Init(&ctx);
	SHA256Update(&ctx, (const uint8_t *)"", 0);
	SHA256Final(digest, &ctx);
	if (check_digest("SHA-256 empty", digest, sizeof(digest),
	    "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"))
		return 1;

	SHA256Init(&ctx);
	SHA256Update(&ctx, (const uint8_t *)"a", 1);
	SHA256Update(&ctx, (const uint8_t *)"b", 1);
	SHA256Update(&ctx, (const uint8_t *)"c", 1);
	SHA256Final(digest, &ctx);
	return check_digest("SHA-256 abc split", digest, sizeof(digest),
	    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

static int
check_sha384(void)
{
	SHA2_CTX ctx;
	uint8_t digest[SHA384_DIGEST_LENGTH];

	SHA384Init(&ctx);
	SHA384Update(&ctx, (const uint8_t *)"", 0);
	SHA384Final(digest, &ctx);
	if (check_digest("SHA-384 empty", digest, sizeof(digest),
	    "38b060a751ac96384cd9327eb1b1e36a21fdb71114be07434c0cc7bf63f6e1da274edebfe76f65fbd51ad2f14898b95b"))
		return 1;

	SHA384Init(&ctx);
	SHA384Update(&ctx, (const uint8_t *)"a", 1);
	SHA384Update(&ctx, (const uint8_t *)"b", 1);
	SHA384Update(&ctx, (const uint8_t *)"c", 1);
	SHA384Final(digest, &ctx);
	return check_digest("SHA-384 abc split", digest, sizeof(digest),
	    "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7");
}

static int
check_sha512(void)
{
	SHA2_CTX ctx;
	uint8_t digest[SHA512_DIGEST_LENGTH];

	SHA512Init(&ctx);
	SHA512Update(&ctx, (const uint8_t *)"", 0);
	SHA512Final(digest, &ctx);
	if (check_digest("SHA-512 empty", digest, sizeof(digest),
	    "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e"))
		return 1;

	SHA512Init(&ctx);
	SHA512Update(&ctx, (const uint8_t *)"a", 1);
	SHA512Update(&ctx, (const uint8_t *)"b", 1);
	SHA512Update(&ctx, (const uint8_t *)"c", 1);
	SHA512Final(digest, &ctx);
	return check_digest("SHA-512 abc split", digest, sizeof(digest),
	    "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f");
}

int
main(void)
{
	if (check_sha256() || check_sha384() || check_sha512())
		return 1;
	puts("OpenSSH SHA2 adapter known-answer tests PASS");
	return 0;
}
