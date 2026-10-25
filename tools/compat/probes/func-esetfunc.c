/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD (AI-assisted). Native ABI capability probe. */
#include <stddef.h>

void (*esetfunc(void (*)(int, const char *, ...)))(int, const char *, ...);

int
main(void)
{
	return esetfunc(0) != 0;
}
