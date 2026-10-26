/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD (AI-assisted). */
#include "compat_filemeta_features.h"
#include <fcntl.h>

int
main(void)
{

	return (O_REGULAR == 0);
}
