/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _ARM_BROADCOM_BCM2712_MMU4_COREVAR_H_
#define _ARM_BROADCOM_BCM2712_MMU4_COREVAR_H_

#include <sys/types.h>

struct bcm2712_iommuc_core;

struct bcm2712_mmu4_config {
	uint32_t debug;
	uint64_t top_pa, l2_pa[2], default_pa;
};

/* Closed register keys, not addresses or offsets. */
enum bcm2712_mmu4_register {
	BCM2712_MMU4_DEBUG,
	BCM2712_MMU4_CONTROL,
	BCM2712_MMU4_SHOOT_DOWN,
	BCM2712_MMU4_MISC,
	BCM2712_MMU4_CAP,
	BCM2712_MMU4_BYPASS_START,
	BCM2712_MMU4_BYPASS_END,
	BCM2712_MMU4_PT_BASE,
	BCM2712_MMU4_ILLEGAL_ADR
};

/*
 * prepare_pages validates its full plan against cfg, uses the fixed encoder,
 * copies/zeros already allocated backing and completes full PREWRITE before
 * any MMIO. It must not allocate, publish pointers, perform MMIO or change a
 * lease. The caller owns the large plan/image/native backing outside this core.
 * Fallible callbacks return positive errno. The mandatory barrier orders writes
 * and cannot recover native stalls/aborts. The caller serializes this
 * core and shared MMU2/4/5, prevents callback mutation/reentry, and preserves
 * storage/text/argument lifetime. These contracts do not prove power, physical
 * ownership, PA/DMA correspondence, interconnect access or client exclusion.
 */
struct bcm2712_mmu4_io {
	int (*prepare_pages)(void *, const struct bcm2712_mmu4_config *);
	int (*read)(void *, enum bcm2712_mmu4_register, uint32_t *);
	int (*write)(void *, enum bcm2712_mmu4_register, uint32_t);
	void (*barrier)(void *);
	void *arg;
};

enum bcm2712_mmu4_stage {
	BCM2712_MMU4_IDLE,
	BCM2712_MMU4_RESERVE,
	BCM2712_MMU4_PREPARE,
	BCM2712_MMU4_PREFLIGHT,
	BCM2712_MMU4_RETAIN,
	BCM2712_MMU4_WRITE,
	BCM2712_MMU4_POLL,
	BCM2712_MMU4_MISC_READ,
	BCM2712_MMU4_CACHE,
	BCM2712_MMU4_VERIFY,
	BCM2712_MMU4_COMPLETE,
	BCM2712_MMU4_CLOSED
};

struct bcm2712_mmu4_status {
	enum bcm2712_mmu4_stage stage;
	unsigned int step; /* Fixed sequence step1..9; step8 is shared flush. */
	unsigned int poll;
	unsigned int reads_attempted, reads_completed;
	unsigned int writes_attempted, writes_completed;
	enum bcm2712_mmu4_register raw_register;
	uint32_t raw;
	int first_error, fault, close_error;
	bool raw_valid; /* Cleared before each read, set only after success. */
	bool program_started, prepared, reservation_owned, retained;
	bool mutation_attempted, sequence_complete;
};

struct bcm2712_mmu4_core {
	struct bcm2712_mmu4_config cfg;
	struct bcm2712_mmu4_io io;
	struct bcm2712_iommuc_core *cache;
	struct bcm2712_mmu4_status status;
	bool initialized, running;
};

/*
 * Fresh zeroed storage only. cfg/io can alias sc->cfg/sc->io. Re-init is EBUSY
 * in every initialized state. Init/status do no I/O and preserve output on
 * error. All caller objects are accessible and disjoint from writable core and
 * cache storage, except those explicit cfg/io aliases during init.
 *
 * Program is one-shot, including a pre-retain refusal. It reserves only its
 * own MMU4 lease and retains before any local write. Before retain, first_error
 * is not a sticky fault: fini may release that own untouched reservation.
 * After retain, success and every failure keep all native obligations alive.
 * No retry, disable, restore, release-after-retain or free predicate exists.
 * COMPLETE is a protocol result, not translation, publication, drain or power.
 */
int bcm2712_mmu4_core_init(struct bcm2712_mmu4_core *,
    const struct bcm2712_mmu4_config *, const struct bcm2712_mmu4_io *,
    struct bcm2712_iommuc_core *);
int bcm2712_mmu4_core_program(struct bcm2712_mmu4_core *);
int bcm2712_mmu4_core_status(const struct bcm2712_mmu4_core *,
    struct bcm2712_mmu4_status *);
int bcm2712_mmu4_core_fini(struct bcm2712_mmu4_core *);

#endif /* _ARM_BROADCOM_BCM2712_MMU4_COREVAR_H_ */
