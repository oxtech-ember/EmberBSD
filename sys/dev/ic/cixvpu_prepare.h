/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: original CPU-only firmware preparation interface, AI-assisted. */
#ifndef _DEV_IC_CIXVPU_PREPARE_H_
#define _DEV_IC_CIXVPU_PREPARE_H_

#include <dev/ic/cixvpu_dma.h>

struct cixvpu_prepared;

/*
 * prepare_cpu is CPU-only. All operations require thread context and external
 * serialization. Only publish_root returns an encoded address.
 * No operation accesses MMIO.
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
 * invalid by the C lifetime contract. No mutable getter exists.
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

struct cixvpu_boot_reply {
	uint16_t code;
	uint32_t core; /* Only SWITCHED_IN has a core value; PONG returns zero. */
};
/*
 * Fixed one-core protocol3.5 sum0005 CPU initializer. Reuses prepare_cpu rules.
 * Seed PING before seal/sync. The caller supplies the trusted full-image pin.
 * publish_root requires successful initial sync and changes exposure before
 * returning a root word. Every later free remains EBUSY, even without MMIO.
 * Both outputs must be distinct from all known retained prepared/DMA storage.
 * No arbitrary native-cookie or invalid kernel pointer authentication is claimed.
 * receive returns one SWITCHED_IN(core0)/PONG; EAGAIN is empty/unstable snapshot.
 * Malformed data/RPC or retained-map error is sticky, without cursor advancement.
 * Argument/output-alias errors before I/O preserve state. No protocol retry after
 * a sticky error, automatic destruction, mutable view or general RPC exists.
 */
int cixvpu_prepare_boot_cpu(struct cixvpu_dma **, const struct cixvpu_fw_plan *,
    const void *, size_t, const uint8_t[32], struct cixvpu_prepared **);
int cixvpu_prepared_publish_root(struct cixvpu_prepared *, uint32_t *);
int cixvpu_prepared_boot_receive(struct cixvpu_prepared *,
    struct cixvpu_boot_reply *);

#endif
