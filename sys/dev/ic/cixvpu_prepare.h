/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: original CPU-only firmware preparation interface, AI-assisted. */
#ifndef _DEV_IC_CIXVPU_PREPARE_H_
#define _DEV_IC_CIXVPU_PREPARE_H_

#include <dev/ic/cixvpu_dma.h>

struct cixvpu_prepared;

/*
 * prepare_cpu is CPU-only. All operations require thread context and external
 * serialization. No operation publishes an address or accesses hardware.
 * owned is an exclusive never-device-visible allocation, with no outstanding
 * page-view borrows. Image, expected plan and trusted SHA256 pin stay immutable
 * through this call. They and both handle slots must not overlap owner pages.
 * Handle slots must not overlap each other or those immutable inputs either.
 *
 * Failure preserves both handles, but CPU contents can be partially changed.
 * The caller must free that allocation, never retry or publish partial bytes.
 * Success moves ownership: *owned becomes NULL and *out is an opaque CPU owner.
 * Live old allocation aliases receive EBUSY from public page/free after seal.
 * Existing view borrows must end before seal; pointers to freed objects remain
 * invalid by the C lifetime contract. No mutable getter or L0 handoff exists.
 * sync_initial prevalidates every map, then synchronizes unique data, leaves
 * and root in order. It returns no address and is allowed once. A preflight
 * error performs no sync and retains ownership for explicit cleanup.
 * free cancels matching PRE operations only for a never-exposed synced owner.
 * Any internally marked exposure makes every free path return EBUSY forever;
 * there is no reset/quiescence bypass or automatic retry.
 * Free errors preserve the whole prepared owner; no automatic retry occurs.
 */
int cixvpu_prepare_cpu(struct cixvpu_dma **, const struct cixvpu_fw_plan *,
    const void *, size_t, const uint8_t[32], struct cixvpu_prepared **);
int cixvpu_prepared_free(struct cixvpu_prepared **);
int cixvpu_prepared_sync_initial(struct cixvpu_prepared *);

#endif
