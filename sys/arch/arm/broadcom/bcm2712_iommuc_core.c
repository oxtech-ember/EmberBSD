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

#define IOMMUC_ENABLE		0x1U
#define IOMMUC_FLUSH		0x2U
#define IOMMUC_FLUSHING		0x4U
#define IOMMUC_KNOWN		0x7U
#define IOMMUC_POLL_LIMIT	1024U

static int
bcm2712_iommuc_member(struct bcm2712_iommuc_core *sc,
    enum bcm2712_iommuc_mmu mmu, unsigned int *bit)
{

	if (sc == NULL || !sc->initialized)
		return EINVAL;
	switch (mmu) {
	case BCM2712_IOMMUC_MMU2:
		*bit = 1U;
		break;
	case BCM2712_IOMMUC_MMU4:
		*bit = 2U;
		break;
	case BCM2712_IOMMUC_MMU5:
		*bit = 4U;
		break;
	default:
		return EINVAL;
	}
	if (sc->status.fault != 0)
		return EIO;
	if (sc->status.stage == BCM2712_IOMMUC_CLOSED ||
	    sc->status.stage == BCM2712_IOMMUC_PREFLIGHT ||
	    sc->status.stage == BCM2712_IOMMUC_WRITE ||
	    sc->status.stage == BCM2712_IOMMUC_POLL)
		return EBUSY;
	return 0;
}

static int
bcm2712_iommuc_fault(struct bcm2712_iommuc_core *sc, int error)
{

	sc->status.fault = error;
	sc->status.flush_complete = false;
	return error;
}

static int
bcm2712_iommuc_read(struct bcm2712_iommuc_core *sc)
{
	uint32_t value;
	int error;

	sc->status.raw_valid = false;
	sc->status.reads_attempted++;
	error = sc->io.read_control(sc->io.arg, &value);
	if (error != 0)
		return error;
	sc->status.reads_completed++;
	sc->status.raw_control = value;
	sc->status.raw_valid = true;
	return 0;
}

int
bcm2712_iommuc_core_init(struct bcm2712_iommuc_core *sc,
    const struct bcm2712_iommuc_io *io)
{
	struct bcm2712_iommuc_io callbacks;

	if (sc == NULL || io == NULL)
		return EINVAL;
	if (sc->initialized)
		return EBUSY;
	callbacks = *io;
	if (callbacks.read_control == NULL || callbacks.write_control == NULL ||
	    callbacks.barrier == NULL)
		return EINVAL;
	memset(sc, 0, sizeof(*sc));
	sc->io = callbacks;
	sc->status.stage = BCM2712_IOMMUC_IDLE;
	sc->initialized = true;
	return 0;
}

int
bcm2712_iommuc_core_reserve(struct bcm2712_iommuc_core *sc,
    enum bcm2712_iommuc_mmu mmu)
{
	unsigned int bit;
	int error;

	error = bcm2712_iommuc_member(sc, mmu, &bit);
	if (error != 0)
		return error;
	if ((sc->status.reserved & bit) != 0)
		return EBUSY;
	sc->status.reserved |= bit;
	return 0;
}

int
bcm2712_iommuc_core_retain(struct bcm2712_iommuc_core *sc,
    enum bcm2712_iommuc_mmu mmu)
{
	unsigned int bit;
	int error;

	error = bcm2712_iommuc_member(sc, mmu, &bit);
	if (error != 0)
		return error;
	if ((sc->status.reserved & bit) == 0)
		return EINVAL;
	if ((sc->status.retained & bit) != 0)
		return EBUSY;
	sc->status.retained |= bit;
	return 0;
}

int
bcm2712_iommuc_core_release(struct bcm2712_iommuc_core *sc,
    enum bcm2712_iommuc_mmu mmu)
{
	unsigned int bit;
	int error;

	error = bcm2712_iommuc_member(sc, mmu, &bit);
	if (error != 0)
		return error;
	if ((sc->status.reserved & bit) == 0)
		return EINVAL;
	if ((sc->status.retained & bit) != 0)
		return EBUSY;
	sc->status.reserved &= ~bit;
	return 0;
}

int
bcm2712_iommuc_core_enable_flush(struct bcm2712_iommuc_core *sc,
    enum bcm2712_iommuc_mmu mmu)
{
	struct bcm2712_iommuc_status *s;
	unsigned int bit, i;
	int error;

	error = bcm2712_iommuc_member(sc, mmu, &bit);
	if (error != 0)
		return error;
	if ((sc->status.reserved & bit) == 0)
		return EINVAL;
	if ((sc->status.retained & bit) == 0)
		return EBUSY;
	s = &sc->status;
	s->mmu = mmu;
	s->stage = BCM2712_IOMMUC_PREFLIGHT;
	s->poll = 0;
	s->reads_attempted = s->reads_completed = 0;
	s->writes_attempted = s->writes_completed = 0;
	s->raw_valid = false;
	s->flush_complete = false;
	error = bcm2712_iommuc_read(sc);
	if (error != 0)
		return bcm2712_iommuc_fault(sc, error);
	if ((s->raw_control & IOMMUC_FLUSHING) != 0)
		return bcm2712_iommuc_fault(sc, EBUSY);
	if ((s->raw_control & ~IOMMUC_KNOWN) != 0)
		return bcm2712_iommuc_fault(sc, EOPNOTSUPP);

	s->stage = BCM2712_IOMMUC_WRITE;
	s->writes_attempted = 1;
	s->mutation_attempted = true;
	error = sc->io.write_control(sc->io.arg, IOMMUC_ENABLE | IOMMUC_FLUSH);
	if (error != 0)
		return bcm2712_iommuc_fault(sc, error);
	s->writes_completed = 1;
	sc->io.barrier(sc->io.arg);
	for (i = 1; i <= IOMMUC_POLL_LIMIT; i++) {
		s->stage = BCM2712_IOMMUC_POLL;
		s->poll = i;
		error = bcm2712_iommuc_read(sc);
		if (error != 0)
			return bcm2712_iommuc_fault(sc, error);
		if ((s->raw_control & ~IOMMUC_KNOWN) != 0)
			return bcm2712_iommuc_fault(sc, EIO);
		if ((s->raw_control & IOMMUC_FLUSHING) != 0)
			continue;
		if ((s->raw_control & IOMMUC_ENABLE) == 0)
			return bcm2712_iommuc_fault(sc, EIO);
		s->stage = BCM2712_IOMMUC_COMPLETE;
		s->flush_complete = true;
		return 0;
	}
	return bcm2712_iommuc_fault(sc, ETIMEDOUT);
}

int
bcm2712_iommuc_core_status(const struct bcm2712_iommuc_core *sc,
    struct bcm2712_iommuc_status *out)
{
	struct bcm2712_iommuc_status value;

	if (sc == NULL || out == NULL || !sc->initialized)
		return EINVAL;
	value = sc->status;
	*out = value;
	return 0;
}

int
bcm2712_iommuc_core_fini(struct bcm2712_iommuc_core *sc)
{

	if (sc == NULL || !sc->initialized)
		return EINVAL;
	if (sc->status.reserved != 0 || sc->status.retained != 0 ||
	    sc->status.fault != 0 || sc->status.mutation_attempted ||
	    sc->status.stage == BCM2712_IOMMUC_CLOSED)
		return EBUSY;
	sc->status.stage = BCM2712_IOMMUC_CLOSED;
	return 0;
}
