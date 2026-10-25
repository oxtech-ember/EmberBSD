/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD (AI-assisted). Native ABI capability probe. */
#include <inttypes.h>

__typeof__(&strtoi) emberbsd_probe = &strtoi;
