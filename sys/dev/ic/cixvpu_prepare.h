/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: original CPU-only firmware preparation interface, AI-assisted. */
#ifndef _DEV_IC_CIXVPU_PREPARE_H_
#define _DEV_IC_CIXVPU_PREPARE_H_

#include <dev/ic/cixvpu_dma.h>

struct cixvpu_prepared;

/*
 * CPU-only, thread context, externally serialized. No sync or hardware
 * exposure.
 * owned is an exclusive never-device-visible allocation, with no outstanding
 * page-view borrows. Image, expected plan and trusted SHA256 pin stay immutable
 * through this call. They and both handle slots must not overlap owner pages.
 * Handle slots must not overlap each other or those immutable inputs either.
 *
 * Failure preserves both handles, but CPU contents can be partially changed.
 * The caller must free that allocation, never retry or publish partial bytes.
 * Success moves ownership: *owned becomes NULL and *out is an opaque CPU owner.
 * Old pointer copies/views are invalid by contract; no mutable getter or L0
 * handoff API is exposed. This is not an enforced device lifetime/alias guard.
 * Hardware exposure requires a separate reviewed lifetime and sync mechanism.
 * Free errors preserve the whole prepared owner; no automatic retry occurs.
 */
int cixvpu_prepare_cpu(struct cixvpu_dma **, const struct cixvpu_fw_plan *,
    const void *, size_t, const uint8_t[32], struct cixvpu_prepared **);
int cixvpu_prepared_free(struct cixvpu_prepared **);

#endif
