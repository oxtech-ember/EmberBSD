/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#include <sys/cdefs.h>
#ifdef _KERNEL
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/systm.h>
#else
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#endif
#include "bcm2712_iommuc_corevar.h"
#include "bcm2712_mmu4_corevar.h"

/* Numeric facts: Raspberry Pi Linux a1a601d4857978d810175365550c4536701df4ed,
 * drivers/iommu/bcm2712-iommu.c. The bounded init poll and named readback
 * conditions are conservative BSD policy, not a hardware drain predicate.
 */
#define MMU4_CLEAR_WORD		0x0810100cU
#define MMU4_ENABLE_WORD	0x04090803U
#define MMU4_PERSISTENT_MASK	0x070f0f03U
#define MMU4_POLL_REJECT		0x08101101U
#define MMU4_FINAL_REJECT	0x08101080U
#define MMU4_TLB_CLEARING	0x80U
#define MMU4_CAP_WORD		0x80000000U
#define MMU4_ILLEGAL_ENABLE	0x80000000U
#define MMU4_SINGLE_TABLE	0x80000000U
#define MMU4_PA_LIMIT		(UINT64_C(1) << 36)

static int
mmu4_config(const struct bcm2712_mmu4_config *cfg)
{
	uint64_t pa[4];
	unsigned int i, j;

	if ((cfg->debug & 15U) != 4U ||
	    ((cfg->debug >> 4) & 15U) < 6U ||
	    ((cfg->debug >> 8) & 15U) < 6U ||
	    (cfg->debug & 0x20000000U) == 0)
		return EOPNOTSUPP;
	pa[0] = cfg->top_pa;
	pa[1] = cfg->l2_pa[0];
	pa[2] = cfg->l2_pa[1];
	pa[3] = cfg->default_pa;
	for (i = 0; i < 4; i++) {
		if (pa[i] == 0 || (pa[i] & 4095U) != 0)
			return EINVAL;
		if (pa[i] > MMU4_PA_LIMIT - 4096U)
			return ERANGE;
		for (j = 0; j < i; j++) {
			if (pa[i] == pa[j])
				return EINVAL;
		}
	}
	return 0;
}

static int
mmu4_finish(struct bcm2712_mmu4_core *sc, int error)
{

	if (error != 0) {
		if (sc->status.first_error == 0)
			sc->status.first_error = error;
		if (sc->status.retained)
			sc->status.fault = error;
		sc->status.sequence_complete = false;
	}
	sc->running = false;
	return error;
}

static int
mmu4_read(struct bcm2712_mmu4_core *sc, enum bcm2712_mmu4_register reg)
{
	uint32_t value;
	int error;

	sc->status.raw_valid = false;
	sc->status.raw_register = reg;
	sc->status.reads_attempted++;
	error = sc->io.read(sc->io.arg, reg, &value);
	if (error != 0)
		return error;
	sc->status.reads_completed++;
	sc->status.raw = value;
	sc->status.raw_valid = true;
	return 0;
}

static int
mmu4_write(struct bcm2712_mmu4_core *sc, unsigned int step,
    enum bcm2712_mmu4_register reg, uint32_t value)
{
	int error;

	sc->status.stage = BCM2712_MMU4_WRITE;
	sc->status.step = step;
	sc->status.writes_attempted++;
	sc->status.mutation_attempted = true;
	error = sc->io.write(sc->io.arg, reg, value);
	if (error != 0)
		return error;
	sc->status.writes_completed++;
	sc->io.barrier(sc->io.arg);
	return 0;
}

int
bcm2712_mmu4_core_init(struct bcm2712_mmu4_core *sc,
    const struct bcm2712_mmu4_config *cfg, const struct bcm2712_mmu4_io *io,
    struct bcm2712_iommuc_core *cache)
{
	struct bcm2712_mmu4_config value;
	struct bcm2712_mmu4_io callbacks;
	int error;

	if (sc == NULL || cfg == NULL || io == NULL || cache == NULL)
		return EINVAL;
	if (sc->initialized)
		return EBUSY;
	value = *cfg;
	callbacks = *io;
	if (callbacks.prepare_pages == NULL || callbacks.read == NULL ||
	    callbacks.write == NULL || callbacks.barrier == NULL)
		return EINVAL;
	error = mmu4_config(&value);
	if (error != 0)
		return error;
	memset(sc, 0, sizeof(*sc));
	sc->cfg = value;
	sc->io = callbacks;
	sc->cache = cache;
	sc->status.stage = BCM2712_MMU4_IDLE;
	sc->initialized = true;
	return 0;
}

int
bcm2712_mmu4_core_program(struct bcm2712_mmu4_core *sc)
{
	struct bcm2712_iommuc_status cache_status;
	unsigned int i;
	int error;

	if (sc == NULL || !sc->initialized)
		return EINVAL;
	if (sc->status.fault != 0)
		return EIO;
	if (sc->running || sc->status.program_started ||
	    sc->status.stage == BCM2712_MMU4_CLOSED)
		return EBUSY;
	sc->status.program_started = true;
	sc->running = true;
	sc->status.stage = BCM2712_MMU4_RESERVE;
	error = bcm2712_iommuc_core_status(sc->cache, &cache_status);
	if (error != 0)
		return mmu4_finish(sc, error);
	if (cache_status.fault != 0)
		return mmu4_finish(sc, EIO);
	error = bcm2712_iommuc_core_reserve(sc->cache, BCM2712_IOMMUC_MMU4);
	if (error != 0)
		return mmu4_finish(sc, error);
	sc->status.reservation_owned = true;
	sc->status.stage = BCM2712_MMU4_PREPARE;
	error = sc->io.prepare_pages(sc->io.arg, &sc->cfg);
	if (error != 0)
		return mmu4_finish(sc, error);
	sc->status.prepared = true;
	sc->status.stage = BCM2712_MMU4_PREFLIGHT;
	error = mmu4_read(sc, BCM2712_MMU4_DEBUG);
	if (error != 0)
		return mmu4_finish(sc, error);
	if (sc->status.raw != sc->cfg.debug)
		return mmu4_finish(sc, EAGAIN);
	error = mmu4_read(sc, BCM2712_MMU4_CONTROL);
	if (error != 0)
		return mmu4_finish(sc, error);
	if (sc->status.raw != 0)
		return mmu4_finish(sc, EBUSY);
	error = mmu4_read(sc, BCM2712_MMU4_SHOOT_DOWN);
	if (error != 0)
		return mmu4_finish(sc, error);
	if ((sc->status.raw & 0x80000000U) != 0)
		return mmu4_finish(sc, EBUSY);
	sc->status.stage = BCM2712_MMU4_RETAIN;
	error = bcm2712_iommuc_core_retain(sc->cache, BCM2712_IOMMUC_MMU4);
	if (error != 0)
		return mmu4_finish(sc, error);
	sc->status.retained = true;
	error = mmu4_write(sc, 1, BCM2712_MMU4_CONTROL, MMU4_CLEAR_WORD);
	if (error != 0)
		return mmu4_finish(sc, error);
	sc->status.stage = BCM2712_MMU4_POLL;
	for (i = 1; i <= 1024; i++) {
		sc->status.poll = i;
		error = mmu4_read(sc, BCM2712_MMU4_CONTROL);
		if (error != 0)
			return mmu4_finish(sc, error);
		if ((sc->status.raw & MMU4_POLL_REJECT) != 0)
			return mmu4_finish(sc, EIO);
		if ((sc->status.raw & MMU4_TLB_CLEARING) == 0)
			break;
	}
	if (i > 1024)
		return mmu4_finish(sc, ETIMEDOUT);
	sc->status.stage = BCM2712_MMU4_MISC_READ;
	sc->status.step = 2;
	error = mmu4_read(sc, BCM2712_MMU4_MISC);
	if (error != 0)
		return mmu4_finish(sc, error);
	error = mmu4_write(sc, 2, BCM2712_MMU4_MISC,
	    sc->status.raw & ~MMU4_SINGLE_TABLE);
	if (error != 0)
		return mmu4_finish(sc, error);
	error = mmu4_write(sc, 3, BCM2712_MMU4_CAP, MMU4_CAP_WORD);
	if (error != 0)
		return mmu4_finish(sc, error);
	error = mmu4_write(sc, 4, BCM2712_MMU4_BYPASS_START, 0);
	if (error != 0)
		return mmu4_finish(sc, error);
	error = mmu4_write(sc, 5, BCM2712_MMU4_BYPASS_END, 0);
	if (error != 0)
		return mmu4_finish(sc, error);
	error = mmu4_write(sc, 6, BCM2712_MMU4_PT_BASE,
	    (uint32_t)(sc->cfg.top_pa >> 12));
	if (error != 0)
		return mmu4_finish(sc, error);
	error = mmu4_write(sc, 7, BCM2712_MMU4_ILLEGAL_ADR,
	    MMU4_ILLEGAL_ENABLE | (uint32_t)(sc->cfg.default_pa >> 12));
	if (error != 0)
		return mmu4_finish(sc, error);
	sc->status.stage = BCM2712_MMU4_CACHE;
	sc->status.step = 8;
	error = bcm2712_iommuc_core_enable_flush(sc->cache, BCM2712_IOMMUC_MMU4);
	if (error != 0)
		return mmu4_finish(sc, error);
	error = mmu4_write(sc, 9, BCM2712_MMU4_CONTROL, MMU4_ENABLE_WORD);
	if (error != 0)
		return mmu4_finish(sc, error);
	sc->status.stage = BCM2712_MMU4_VERIFY;
	error = mmu4_read(sc, BCM2712_MMU4_CONTROL);
	if (error != 0)
		return mmu4_finish(sc, error);
	if ((sc->status.raw & MMU4_PERSISTENT_MASK) != MMU4_ENABLE_WORD ||
	    (sc->status.raw & MMU4_FINAL_REJECT) != 0)
		return mmu4_finish(sc, EIO);
	sc->status.stage = BCM2712_MMU4_COMPLETE;
	sc->status.sequence_complete = true;
	return mmu4_finish(sc, 0);
}

int
bcm2712_mmu4_core_status(const struct bcm2712_mmu4_core *sc,
    struct bcm2712_mmu4_status *out)
{
	struct bcm2712_mmu4_status value;

	if (sc == NULL || out == NULL || !sc->initialized)
		return EINVAL;
	memcpy(&value, &sc->status, sizeof(value));
	memcpy(out, &value, sizeof(value));
	return 0;
}

int
bcm2712_mmu4_core_fini(struct bcm2712_mmu4_core *sc)
{
	int error;

	if (sc == NULL || !sc->initialized)
		return EINVAL;
	if (sc->running || sc->status.retained || sc->status.mutation_attempted ||
	    sc->status.fault != 0 || sc->status.stage == BCM2712_MMU4_CLOSED)
		return EBUSY;
	if (sc->status.reservation_owned) {
		error = bcm2712_iommuc_core_release(sc->cache, BCM2712_IOMMUC_MMU4);
		if (error != 0) {
			sc->status.close_error = error;
			sc->status.fault = error;
			return error;
		}
		sc->status.reservation_owned = false;
	}
	sc->status.stage = BCM2712_MMU4_CLOSED;
	return 0;
}
