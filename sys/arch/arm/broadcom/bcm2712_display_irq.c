/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Register facts: Raspberry Pi Linux a1a601d4857978d810175365550c4536701df4ed. */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");
#include <sys/param.h>
#include <sys/systm.h>
#include <arm/broadcom/bcm2712_display_irq.h>

int
bcm2712_l2_init(struct bcm2712_l2 *out,
    const struct bcm2712_display_contract *contract, enum bcm2712_l2_domain domain,
    const struct bcm2712_display_memory *mapped, uint32_t owned,
    const struct bcm2712_l2_io *io)
{
	struct bcm2712_display_contract checked;
	struct bcm2712_l2 result;
	uint32_t allowed = 0;
	unsigned int i, index;
	int error;

	if (out == NULL || contract == NULL || mapped == NULL || io == NULL ||
	    io->read == NULL || io->write == NULL || io->barrier == NULL ||
	    (unsigned int)domain >= BCM2712_DISPLAY_NDOMAIN)
		return EINVAL;
	error = bcm2712_display_decode(contract->version, contract->memory,
	    BCM2712_DISPLAY_NMEM, contract->irq, BCM2712_DISPLAY_NIRQ, &checked);
	if (error != 0)
		return error;
	index = bcm2712_display_domains[domain].memory_index;
	if (mapped->address != checked.memory[index].address ||
	    mapped->length != checked.memory[index].length)
		return EINVAL;
	for (i = 0; i < BCM2712_DISPLAY_NLEAF; i++)
		if (bcm2712_display_leaves[i].domain == (unsigned int)domain)
			allowed |= __BIT(bcm2712_display_leaves[i].bit);
	if (owned == 0 || (owned & ~allowed) != 0)
		return EINVAL;
	memset(&result, 0, sizeof(result));
	result.io = *io;
	result.owned = owned;
	result.declared = allowed;
	if (domain == BCM2712_L2_BSC) {
		/* BCM7271 layout: level source, no interrupt-clear register. */
		result.mask_status = 0x04;
		result.mask_set = 0x08;
		result.mask_clear = 0x0c;
	} else {
		/* BCM2711 layout: AON and DISP have a separate W1C register. */
		result.has_clear = true;
		result.clear = 0x08;
		result.mask_status = 0x0c;
		result.mask_set = 0x10;
		result.mask_clear = 0x14;
	}
	*out = result;
	return 0;
}

int
bcm2712_l2_prepare(struct bcm2712_l2 *sc)
{
	uint32_t mask, status;

	if (sc->prepared)
		return EBUSY;
	mask = sc->io.read(sc->io.arg, sc->mask_status);
	status = sc->io.read(sc->io.arg, 0);
	/*
	 * Contract v1 declares leaf sources, not every implemented mask bit.
	 * Reserved mask bits can read zero. Reject unmasked declared foreign
	 * leaves and any currently asserted unknown source without writes.
	 */
	if ((~mask & sc->declared & ~sc->owned) != 0 ||
	    (status & ~mask & ~sc->owned) != 0)
		return EBUSY;
	sc->saved_mask = mask;
	sc->io.write(sc->io.arg, sc->mask_set, sc->owned);
	sc->io.barrier(sc->io.arg);
	sc->prepared = true;
	return 0;
}

int
bcm2712_l2_arm(struct bcm2712_l2 *sc)
{
	if (!sc->prepared || sc->fault != 0)
		return ENXIO;
	if (sc->armed)
		return EBUSY;
	sc->armed = true;
	sc->io.write(sc->io.arg, sc->mask_clear, sc->owned);
	sc->io.barrier(sc->io.arg);
	return 0;
}

void
bcm2712_l2_stop(struct bcm2712_l2 *sc, int error)
{
	if (sc->prepared) {
		sc->io.write(sc->io.arg, sc->mask_set, sc->owned);
		sc->io.barrier(sc->io.arg);
	}
	sc->armed = false;
	sc->pending = 0;
	if (sc->fault == 0)
		sc->fault = error;
}

int
bcm2712_l2_collect(struct bcm2712_l2 *sc, uint32_t *collected)
{
	uint32_t status, mask, pending;

	*collected = 0;
	if (!sc->armed)
		return 0;
	/* One snapshot only. Never poll a level source in hard interrupt context. */
	status = sc->io.read(sc->io.arg, 0);
	mask = sc->io.read(sc->io.arg, sc->mask_status);
	pending = status & ~mask;
	if ((pending & ~sc->owned) != 0) {
		bcm2712_l2_stop(sc, EIO);
		return EIO;
	}
	if (pending == 0)
		return 0;
	sc->io.write(sc->io.arg, sc->mask_set, pending);
	if (sc->has_clear)
		sc->io.write(sc->io.arg, sc->clear, pending);
	sc->io.barrier(sc->io.arg);
	sc->pending |= pending;
	*collected = pending;
	return 0;
}

uint32_t
bcm2712_l2_take(struct bcm2712_l2 *sc)
{
	uint32_t pending = sc->pending;

	sc->pending = 0;
	return pending;
}

void
bcm2712_l2_complete(struct bcm2712_l2 *sc, uint32_t pending, int error)
{
	if (error != 0) {
		bcm2712_l2_stop(sc, error);
		return;
	}
	if (!sc->armed)
		return;
	pending &= sc->owned & ~sc->pending;
	if (pending != 0) {
		sc->io.write(sc->io.arg, sc->mask_clear, pending);
		sc->io.barrier(sc->io.arg);
	}
}

void
bcm2712_l2_rollback(struct bcm2712_l2 *sc)
{
	/* Startup failure only. Restore only the owned mask bits. */
	bcm2712_l2_stop(sc, 0);
	if (sc->prepared && (~sc->saved_mask & sc->owned) != 0) {
		sc->io.write(sc->io.arg, sc->mask_clear,
		    ~sc->saved_mask & sc->owned);
		sc->io.barrier(sc->io.arg);
	}
	sc->prepared = false;
}
