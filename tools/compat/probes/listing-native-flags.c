/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD (AI-assisted). */
#include <sys/types.h>
#include <stddef.h>
#include <util.h>

static char *(*volatile query)(unsigned long, const char *) = flags_to_string;

int
main(void)
{
	return (query == NULL);
}
