/* SPDX-License-Identifier: BSD-2-Clause */
/*	Copyright (c) 2026 EmberBSD contributors. All rights reserved. */
/*
 * Adapter between prepared OpenSSH's built-in SHA2 API and EmberBSD libc.
 */

#ifndef EMBERBSD_OPENSSH_SHA2_H
#define EMBERBSD_OPENSSH_SHA2_H

#include <sys/types.h>
#include <sha2.h>

typedef union {
	SHA256_CTX sha256;
	SHA512_CTX sha512;
} SHA2_CTX;

static inline void
SHA256Init(SHA2_CTX *ctx)
{
	(void)SHA256_Init(&ctx->sha256);
}

static inline void
SHA256Update(SHA2_CTX *ctx, const uint8_t *data, size_t len)
{
	(void)SHA256_Update(&ctx->sha256, data, len);
}

static inline void
SHA256Final(uint8_t digest[SHA256_DIGEST_LENGTH], SHA2_CTX *ctx)
{
	(void)SHA256_Final(digest, &ctx->sha256);
}

static inline void
SHA384Init(SHA2_CTX *ctx)
{
	(void)SHA384_Init(&ctx->sha512);
}

static inline void
SHA384Update(SHA2_CTX *ctx, const uint8_t *data, size_t len)
{
	(void)SHA384_Update(&ctx->sha512, data, len);
}

static inline void
SHA384Final(uint8_t digest[SHA384_DIGEST_LENGTH], SHA2_CTX *ctx)
{
	(void)SHA384_Final(digest, &ctx->sha512);
}

static inline void
SHA512Init(SHA2_CTX *ctx)
{
	(void)SHA512_Init(&ctx->sha512);
}

static inline void
SHA512Update(SHA2_CTX *ctx, const uint8_t *data, size_t len)
{
	(void)SHA512_Update(&ctx->sha512, data, len);
}

static inline void
SHA512Final(uint8_t digest[SHA512_DIGEST_LENGTH], SHA2_CTX *ctx)
{
	(void)SHA512_Final(digest, &ctx->sha512);
}

/* Prevent the OpenSSH compatibility header from declaring its fallback API. */
#define HAVE_SHA256UPDATE 1
#define HAVE_SHA384UPDATE 1
#define HAVE_SHA512UPDATE 1

#endif /* EMBERBSD_OPENSSH_SHA2_H */
