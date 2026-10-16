/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Interface facts: Raspberry Pi Linux a1a601d48579 clk-bcm2711-dvp.c. */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");
#include <sys/param.h>
#include <sys/errno.h>
#include <arm/broadcom/bcm2712_display_dvp.h>

static uint32_t
bcm2712_dvp_gate(unsigned int port)
{
	/* The two 108 MHz gates use SET_TO_DISABLE polarity. */
	return 1U << (port + 3);
}

static uint32_t
bcm2712_dvp_reset(unsigned int port)
{
	return 1U << (port + 1);
}

static int
bcm2712_dvp_read(struct bcm2712_dvp *sc, uint32_t *gates, uint32_t *resets)
{
	uint32_t g, r;
	int error;

	error = sc->io.read(sc->io.arg, BCM2712_DVP_SW_INIT, &r);
	if (error != 0)
		return error;
	error = sc->io.read(sc->io.arg, BCM2712_DVP_MISC_CONFIG, &g);
	if (error != 0)
		return error;
	*gates = g;
	*resets = r;
	return 0;
}

static int
bcm2712_dvp_fault(struct bcm2712_dvp *sc, int error)
{
	sc->status.fault = error;
	return error;
}

static int
bcm2712_dvp_ready(struct bcm2712_dvp *sc, unsigned int port)
{
	if (sc == NULL || port >= BCM2712_DVP_PORTS)
		return EINVAL;
	if (!sc->initialized)
		return ENXIO;
	return sc->status.fault;
}

/* Detect an externally gated or reset port while its lease is still active. */
static int
bcm2712_dvp_check(struct bcm2712_dvp *sc, uint32_t gates, uint32_t resets)
{
	unsigned int port;

	for (port = 0; port < BCM2712_DVP_PORTS; port++) {
		if ((sc->status.leases & (1U << port)) != 0 &&
		    ((gates & bcm2712_dvp_gate(port)) != 0 ||
		    (resets & bcm2712_dvp_reset(port)) != 0))
			return bcm2712_dvp_fault(sc, EIO);
	}
	return 0;
}

int
bcm2712_dvp_init(struct bcm2712_dvp *sc,
    const struct bcm2712_display_contract *contract,
    const struct bcm2712_display_memory *mapped, const struct bcm2712_dvp_io *io)
{
	struct bcm2712_display_contract checked;
	struct bcm2712_dvp result = { 0 };
	int error;

	if (sc == NULL || contract == NULL || mapped == NULL || io == NULL ||
	    io->read == NULL || io->write == NULL || io->barrier == NULL)
		return EINVAL;
	error = bcm2712_display_decode(contract->version, contract->memory,
	    BCM2712_DISPLAY_NMEM, contract->irq, BCM2712_DISPLAY_NIRQ, &checked);
	if (error != 0)
		return error;
	if (mapped->address != checked.memory[BCM2712_DVP_RESOURCE].address ||
	    mapped->length != checked.memory[BCM2712_DVP_RESOURCE].length)
		return EINVAL;
	result.io = *io;
	error = bcm2712_dvp_read(&result, &result.status.gates,
	    &result.status.resets);
	if (error != 0)
		return error;
	result.initialized = true;
	*sc = result;
	return 0;
}

/* Verify successful writes; retain ownership after any uncertain write. */
static int
bcm2712_dvp_write(struct bcm2712_dvp *sc, unsigned int port, uint32_t wanted,
    uint32_t *gates, uint32_t *resets)
{
	uint32_t g, r;
	int error;

	sc->status.mutation_attempted = true;
	error = sc->io.write(sc->io.arg, BCM2712_DVP_MISC_CONFIG, wanted);
	if (error != 0)
		return bcm2712_dvp_fault(sc, error);
	sc->io.barrier(sc->io.arg);
	error = bcm2712_dvp_read(sc, &g, &r);
	if (error != 0)
		return bcm2712_dvp_fault(sc, error);
	if (g != wanted || (r & bcm2712_dvp_reset(port)) != 0)
		return bcm2712_dvp_fault(sc, EIO);
	/* The released port may now be gated; check only the other live port. */
	if ((sc->status.leases & (1U << (port ^ 1U))) != 0 &&
	    (r & bcm2712_dvp_reset(port ^ 1U)) != 0)
		return bcm2712_dvp_fault(sc, EIO);
	*gates = g;
	*resets = r;
	return 0;
}

int
bcm2712_dvp_acquire(struct bcm2712_dvp *sc, unsigned int port)
{
	uint32_t gates, resets, bit;
	int error = bcm2712_dvp_ready(sc, port);

	if (error != 0)
		return error;
	if ((sc->status.leases & (1U << port)) != 0)
		return EBUSY;
	error = bcm2712_dvp_read(sc, &gates, &resets);
	if (error != 0)
		return error;
	error = bcm2712_dvp_check(sc, gates, resets);
	if (error != 0)
		return error;
	if ((resets & bcm2712_dvp_reset(port)) != 0)
		return EBUSY;
	bit = bcm2712_dvp_gate(port);
	if ((gates & bit) != 0) {
		error = bcm2712_dvp_write(sc, port, gates & ~bit, &gates, &resets);
		if (error != 0)
			return error;
		sc->restore |= bit;
	}
	sc->status.gates = gates;
	sc->status.resets = resets;
	sc->status.leases |= 1U << port;
	return 0;
}

int
bcm2712_dvp_release(struct bcm2712_dvp *sc, unsigned int port)
{
	uint32_t gates, resets, bit;
	int error = bcm2712_dvp_ready(sc, port);

	if (error != 0)
		return error;
	if ((sc->status.leases & (1U << port)) == 0)
		return ENOENT;
	error = bcm2712_dvp_read(sc, &gates, &resets);
	if (error != 0)
		return error;
	error = bcm2712_dvp_check(sc, gates, resets);
	if (error != 0)
		return error;
	bit = bcm2712_dvp_gate(port);
	if ((sc->restore & bit) != 0) {
		error = bcm2712_dvp_write(sc, port, gates | bit, &gates, &resets);
		if (error != 0)
			return error;
	}
	sc->status.gates = gates;
	sc->status.resets = resets;
	sc->status.leases &= ~(1U << port);
	sc->restore &= ~bit;
	return 0;
}

int
bcm2712_dvp_fini(struct bcm2712_dvp *sc)
{
	int error = bcm2712_dvp_ready(sc, 0);

	if (error != 0)
		return error;
	if (sc->status.leases != 0)
		return EBUSY;
	sc->initialized = false;
	return 0;
}
