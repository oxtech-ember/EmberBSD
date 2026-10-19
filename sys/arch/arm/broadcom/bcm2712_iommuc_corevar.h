/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _ARM_BROADCOM_BCM2712_IOMMUC_COREVAR_H_
#define _ARM_BROADCOM_BCM2712_IOMMUC_COREVAR_H_

#include <sys/types.h>

enum bcm2712_iommuc_mmu {
	BCM2712_IOMMUC_MMU2 = 2,
	BCM2712_IOMMUC_MMU4 = 4,
	BCM2712_IOMMUC_MMU5 = 5
};

/*
 * Private fixed CONTROL+0 callbacks. No register-offset interface exists.
 * Read/write return positive BSD errno. The mandatory barrier orders a
 * successful write; it cannot recover native MMIO stalls or bus aborts.
 * The caller serializes calls, prevents callback re-entry into mutations,
 * and owns the callback/argument/storage lifetime. A future native owner
 * must prove shared MMU2/4/5 exclusion and power/interconnect accessibility.
 * This pure interface does not grant those conditions or authorize MMIO.
 */
struct bcm2712_iommuc_io {
	int (*read_control)(void *, uint32_t *);
	int (*write_control)(void *, uint32_t);
	void (*barrier)(void *);
	void *arg;
};

enum bcm2712_iommuc_stage {
	BCM2712_IOMMUC_IDLE,
	BCM2712_IOMMUC_PREFLIGHT,
	BCM2712_IOMMUC_WRITE,
	BCM2712_IOMMUC_POLL,
	BCM2712_IOMMUC_COMPLETE,
	BCM2712_IOMMUC_CLOSED
};

struct bcm2712_iommuc_status {
	/* Logical bits0/1/2 denote MMU2/4/5, not hardware ownership. */
	unsigned int reserved, retained;
	enum bcm2712_iommuc_mmu mmu;
	enum bcm2712_iommuc_stage stage;
	unsigned int poll;
	unsigned int reads_attempted, reads_completed;
	unsigned int writes_attempted, writes_completed;
	uint32_t raw_control;
	int fault;
	bool raw_valid;
	bool mutation_attempted; /* Shared-cache write attempt, not local MMU. */
	bool flush_complete; /* Protocol result only, never AXI drain or power. */
};

struct bcm2712_iommuc_core {
	struct bcm2712_iommuc_io io;
	struct bcm2712_iommuc_status status;
	bool initialized;
};

/*
 * Init needs fresh zero-initialized storage. Never pass uninitialized storage.
 * io may alias sc->io after its callbacks are filled. Re-init returns EBUSY,
 * including after close/fault. Init/status do no I/O and preserve output on
 * rejection. Callers must not replace a retained core with a fresh instance.
 */
int bcm2712_iommuc_core_init(struct bcm2712_iommuc_core *,
    const struct bcm2712_iommuc_io *);
int bcm2712_iommuc_core_reserve(struct bcm2712_iommuc_core *,
    enum bcm2712_iommuc_mmu);
/* Irreversible obligation BEFORE any local-MMU or shared-cache write. */
int bcm2712_iommuc_core_retain(struct bcm2712_iommuc_core *,
    enum bcm2712_iommuc_mmu);
/* Requires retain; tables/default pages and synchronization belong to caller. */
int bcm2712_iommuc_core_enable_flush(struct bcm2712_iommuc_core *,
    enum bcm2712_iommuc_mmu);
/* Only untouched reservations can be released. No release proves drain. */
int bcm2712_iommuc_core_release(struct bcm2712_iommuc_core *,
    enum bcm2712_iommuc_mmu);
int bcm2712_iommuc_core_status(const struct bcm2712_iommuc_core *,
    struct bcm2712_iommuc_status *);
/* Logical close only before retention, with no leases/fault; no hardware I/O. */
int bcm2712_iommuc_core_fini(struct bcm2712_iommuc_core *);
/* No disable/write0, retry, restore, global registry or native caller exists. */
#endif
