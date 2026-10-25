/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD (AI-assisted). Native ABI capability probe. */
#include <stddef.h>

int reallocarr(void *, size_t, size_t);

int
main(void)
{
	void *p = 0;
	return reallocarr(&p, 0, 0);
}
