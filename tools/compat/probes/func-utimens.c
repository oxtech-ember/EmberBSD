/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD (AI-assisted). Native function link probe. */
#include <sys/stat.h>
#include <stddef.h>

extern int utimens(const char *, const struct timespec *);
static int (*volatile update)(const char *, const struct timespec *) = utimens;

int
main(void)
{
	return update == NULL;
}
