/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD (AI-assisted). Native ABI capability probe. */
#include <stdlib.h>

__typeof__(&reallocarr) emberbsd_probe = &reallocarr;
