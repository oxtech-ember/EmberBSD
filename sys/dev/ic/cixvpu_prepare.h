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
struct cixvpu_boot_rpc_header {
	uint32_t state, callid, size;
	uint32_t alloc_size, alloc_max_size;
	uint32_t resize_address, resize_new_size;
	uint8_t alloc_region, alloc_log2_alignment;
	bool valid, alloc_valid, resize_valid;
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
/*
 * Cache only, with the same context/output guards as receive. No device access.
 * valid means twelve bytes were copied in one existing RPC observation, not an
 * atomic/stable firmware transaction. size is numeric telemetry only.
 * alloc_valid requires exact PARAM/call2/size12 and matching headers around a
 * fixed twelve-byte payload copy. Fields describe an observation, not an
 * accepted allocation. resize_valid requires exact PARAM/call3/size8 and a
 * matching header after a fixed eight-byte payload copy. Its numeric VPU VA
 * and size do not authorize access, resize or allocation. Neither observation
 * is atomic; committed PARAM immutability remains a firmware promise.
 * After initial sync, before the first read, fields are zero and valid=false.
 * A sticky retry retains the last diagnostic. A new attempt or retained-map
 * fault invalidates it until
 * another observation; argument/alias errors preserve it. Errors preserve out.
 */
int cixvpu_prepared_boot_rpc_header(struct cixvpu_prepared *,
    struct cixvpu_boot_rpc_header *);

enum cixvpu_boot_debug_result {
	CIXVPU_BOOT_DEBUG_IDLE,
	CIXVPU_BOOT_DEBUG_PENDING,
	CIXVPU_BOOT_DEBUG_REPLIED
};
/*
 * Inactive closed primitive, not used by boot receive or its coordinator.
 * Thread context, serialized published owner and the same output guards apply.
 * FREE/RETURN ignore stale call/size and return IDLE/PENDING without writes.
 * Stable PARAM/call1/size<=1012 changes only state to RETURN, then REPLIED.
 * No payload read/log, IRQ notification, message ACK or allocation occurs.
 * Two matching header copies rely on committed PARAM staying firmware-immutable
 * until RETURN; they do not establish an atomic read or physical cache proof.
 * Four unstable pairs return nonsticky EAGAIN. Protocol/map faults are sticky;
 * an existing sticky fault permits no I/O. Errors preserve out; diagnostics
 * retain the last acquired header. No exposed-owner reclamation is allowed.
 * After basic context/NULL checks, an existing sticky error takes priority
 * over map/alias checks; it preserves the primary error and performs no I/O.
 * Future integration must notify firmware after REPLIED before another step.
 */
int cixvpu_prepared_boot_debugparam(struct cixvpu_prepared *,
    enum cixvpu_boot_debug_result *);

enum cixvpu_boot_rpc_result {
	CIXVPU_BOOT_RPC_IDLE,
	CIXVPU_BOOT_RPC_PENDING,
	CIXVPU_BOOT_RPC_ALLOC_REPLIED,
	CIXVPU_BOOT_RPC_DEBUG_REPLIED
};
/*
 * Closed plan1 bootstrap only. The published owner remains externally
 * serialized in thread context. Basic context/NULL checks precede a sticky
 * error check; that error permits no I/O and preserves outputs/diagnostics.
 * FREE/RETURN ignore stale call/size and return IDLE/PENDING without writes.
 * Exact PARAM/call2/size12 accepts only the fixed observed allocation tuple.
 * One allocation and at most sixteen DEBUG call1 completions are allowed.
 * Slots are consumed before the first response store and never reused.
 * Reply body/size precede the release RETURN store and its DMA publication.
 * The caller must notify firmware once after REPLIED, before another step.
 * No MMIO, heap access, runtime allocation, PTE change or free occurs here.
 * Duplicate ALLOC returns EBUSY; DEBUG17 E2BIG; unsupported calls/tuples
 * ENOTSUP; malformed state/shape EPROTO. These faults are sticky. Four
 * unstable observations return nonsticky EAGAIN. Errors preserve out.
 * Cache numeric observations before semantic rejection; cache is not authority.
 * Committed PARAM immutability and DMA granularity remain firmware assumptions.
 * receive_rpc permits FREE/RETURN queue processing. An observed PARAM returns
 * EAGAIN without queue access/advance; no global atomic exclusion is implied.
 * Legacy receive remains strict. No API bypasses an existing held error.
 */
int cixvpu_prepared_boot_rpc_step(struct cixvpu_prepared *,
    enum cixvpu_boot_rpc_result *);
int cixvpu_prepared_boot_receive_rpc(struct cixvpu_prepared *,
    struct cixvpu_boot_reply *);

#endif
