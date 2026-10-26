/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD (AI-assisted). */
#include <stdlib.h>

static char *(*volatile query)(int *, long *) = getbsize;

int
main(void)
{
	return (query == NULL);
}
