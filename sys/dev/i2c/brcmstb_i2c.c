/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Bounded BRCMSTB combined reads. No attachment or resource ownership.
 * Register facts: Raspberry Pi Linux a1a601d4857978d810175365550c4536701df4ed,
 * drivers/i2c/busses/i2c-brcmstb.c. No Linux implementation is copied.
 */
#include <sys/types.h>
#ifdef _KERNEL
#include <sys/errno.h>
#include <sys/systm.h>
#else
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#endif
#include <dev/i2c/brcmstb_i2creg.h>
#include <dev/i2c/brcmstb_i2cvar.h>

static int
brcmstb_i2c_error(int error)
{

	return error < 0 ? EIO : error;
}

static int
brcmstb_i2c_tick(struct brcmstb_i2c *sc)
{
	uint64_t now = sc->io.time_us(sc->io.arg);

	if (now < sc->previous)
		return EIO;
	sc->previous = now;
	if (now - sc->started >= BRCMSTB_I2C_TIMEOUT_US)
		return ETIMEDOUT;
	return 0;
}

static int
brcmstb_i2c_read(struct brcmstb_i2c *sc, unsigned int reg, uint32_t *value)
{
	int error;

	error = brcmstb_i2c_tick(sc);
	if (error != 0)
		return error;
	return brcmstb_i2c_error(sc->io.read(sc->io.arg, reg, value));
}

static int
brcmstb_i2c_write(struct brcmstb_i2c *sc, unsigned int reg, uint32_t value)
{
	int error;

	error = brcmstb_i2c_tick(sc);
	if (error != 0)
		return error;
	error = brcmstb_i2c_error(sc->io.guard(sc->io.arg, false));
	if (error != 0)
		return error;
	error = brcmstb_i2c_tick(sc);
	if (error != 0)
		return error;
	sc->status.mutation_attempted = true;
	error = brcmstb_i2c_error(sc->io.write(sc->io.arg, reg, value));
	if (error != 0)
		return error;
	sc->io.barrier(sc->io.arg);
	return 0;
}

static int
brcmstb_i2c_wait(struct brcmstb_i2c *sc, bool complete)
{
	uint32_t enable;

	for (;;) {
		int error;

		if (sc->status.polls == BRCMSTB_I2C_POLL_BUDGET)
			return ETIMEDOUT;
		sc->status.polls++;
		error = brcmstb_i2c_read(sc, BRCMSTB_I2C_ENABLE, &enable);
		if (error != 0)
			return error;
		sc->status.enable = enable;
		if (complete && (enable & BRCMSTB_I2C_INTRP) != 0)
			return (enable & BRCMSTB_I2C_NOACK) != 0 ? EIO : 0;
		if (!complete && (enable & BRCMSTB_I2C_ENABLE_MASK) == 0)
			return 0;
		sc->io.delay_us(sc->io.arg, 10);
	}
}

static int
brcmstb_i2c_command(struct brcmstb_i2c *sc, uint32_t ctl,
    uint32_t condition, size_t count)
{
	uint32_t value;
	int error;

	error = brcmstb_i2c_write(sc, BRCMSTB_I2C_COUNT, (uint32_t)count);
	if (error != 0)
		return error;
	error = brcmstb_i2c_write(sc, BRCMSTB_I2C_CTL,
	    ctl | BRCMSTB_I2C_INT_EN);
	if (error != 0)
		return error;
	error = brcmstb_i2c_write(sc, BRCMSTB_I2C_ENABLE,
	    condition | BRCMSTB_I2C_START);
	if (error != 0)
		return error;
	sc->status.commands++;
	error = brcmstb_i2c_wait(sc, true);
	if (error != 0)
		return error;
	error = brcmstb_i2c_write(sc, BRCMSTB_I2C_CTL, ctl);
	if (error != 0)
		return error;
	error = brcmstb_i2c_write(sc, BRCMSTB_I2C_COUNT, 0);
	if (error != 0)
		return error;
	error = brcmstb_i2c_write(sc, BRCMSTB_I2C_ENABLE, 0);
	if (error != 0)
		return error;
	error = brcmstb_i2c_wait(sc, false);
	if (error != 0)
		return error;
	error = brcmstb_i2c_read(sc, BRCMSTB_I2C_COUNT, &value);
	if (error != 0)
		return error;
	if ((value & BRCMSTB_I2C_COUNT_MASK) != 0)
		return EIO;
	error = brcmstb_i2c_read(sc, BRCMSTB_I2C_CTL, &value);
	if (error != 0)
		return error;
	if ((value & BRCMSTB_I2C_CTL_MASK) !=
	    (ctl & BRCMSTB_I2C_CTL_MASK))
		return EIO;
	return 0;
}

int
brcmstb_i2c_init(struct brcmstb_i2c *sc, const struct brcmstb_i2c_io *io)
{
	struct brcmstb_i2c_io saved;

	if (sc == NULL || io == NULL || io->read == NULL ||
	    io->write == NULL || io->barrier == NULL || io->time_us == NULL ||
	    io->delay_us == NULL || io->guard == NULL)
		return EINVAL;
	saved = *io;
	memset(sc, 0, sizeof(*sc));
	sc->io = saved;
	sc->initialized = true;
	return 0;
}

int
brcmstb_i2c_exec(void *arg, i2c_op_t op, i2c_addr_t addr, const void *cmd,
    size_t cmdlen, void *buf, size_t buflen, int flags)
{
	struct brcmstb_i2c *sc = arg;
	uint8_t data[BRCMSTB_I2C_MAX_READ];
	uint32_t ctl, hi, value, condition;
	size_t done, chunk, word, byte;
	int error;

	if (sc == NULL || cmd == NULL || buf == NULL || cmdlen != 1 ||
	    buflen == 0 || buflen > BRCMSTB_I2C_MAX_READ || addr > 0x7f)
		return EINVAL;
	if (op != I2C_OP_READ_WITH_STOP || (flags & ~I2C_F_POLL) != 0)
		return EOPNOTSUPP;
	if (!sc->initialized)
		return ENXIO;
	if (sc->status.fault != 0)
		return sc->status.fault;
	if (sc->status.active)
		return EBUSY;
	memset(&sc->status, 0, sizeof(sc->status));
	sc->status.active = true;
	sc->started = sc->previous = sc->io.time_us(sc->io.arg);
	error = brcmstb_i2c_error(sc->io.guard(sc->io.arg, true));
	if (error != 0)
		goto out;
	error = brcmstb_i2c_read(sc, BRCMSTB_I2C_CTL, &ctl);
	if (error != 0)
		goto out;
	error = brcmstb_i2c_read(sc, BRCMSTB_I2C_CTLHI, &hi);
	if (error != 0)
		goto out;
	error = brcmstb_i2c_read(sc, BRCMSTB_I2C_COUNT, &value);
	if (error != 0)
		goto out;
	if ((value & BRCMSTB_I2C_COUNT_MASK) != 0) {
		error = EBUSY;
		goto out;
	}
	error = brcmstb_i2c_read(sc, BRCMSTB_I2C_ENABLE, &value);
	if (error != 0)
		goto out;
	if ((value & BRCMSTB_I2C_ENABLE_MASK) != 0 ||
	    (ctl & BRCMSTB_I2C_INT_EN) != 0) {
		error = EBUSY;
		goto out;
	}
	ctl = (ctl & ~(BRCMSTB_I2C_DTF | BRCMSTB_I2C_SPEED |
	    BRCMSTB_I2C_INT_EN)) | BRCMSTB_I2C_97500;
	hi = (hi & ~BRCMSTB_I2C_IGNORE_ACK) | BRCMSTB_I2C_DATA4;
	error = brcmstb_i2c_write(sc, BRCMSTB_I2C_CTLHI, hi);
	if (error != 0)
		goto out;
	error = brcmstb_i2c_read(sc, BRCMSTB_I2C_CTLHI, &value);
	if (error != 0)
		goto out;
	if ((value & (BRCMSTB_I2C_DATA4 | BRCMSTB_I2C_IGNORE_ACK)) !=
	    BRCMSTB_I2C_DATA4) {
		error = EIO;
		goto out;
	}
	error = brcmstb_i2c_write(sc, BRCMSTB_I2C_ADDRESS, (uint32_t)addr << 1);
	if (error != 0)
		goto out;
	error = brcmstb_i2c_write(sc, BRCMSTB_I2C_DATA_IN(0),
	    *(const uint8_t *)cmd);
	if (error != 0)
		goto out;
	error = brcmstb_i2c_command(sc, ctl,
	    BRCMSTB_I2C_RESTART | BRCMSTB_I2C_NOSTOP, 1);
	if (error != 0)
		goto out;
	error = brcmstb_i2c_write(sc, BRCMSTB_I2C_ADDRESS, ((uint32_t)addr << 1) | 1);
	if (error != 0)
		goto out;
	for (done = 0; done < buflen; done += chunk) {
		chunk = buflen - done;
		if (chunk > BRCMSTB_I2C_CHUNK)
			chunk = BRCMSTB_I2C_CHUNK;
		condition = done == 0 ? 0 : BRCMSTB_I2C_NOSTART;
		if (done + chunk < buflen)
			condition |= BRCMSTB_I2C_NOSTOP;
		error = brcmstb_i2c_command(sc, ctl | BRCMSTB_I2C_READ,
		    condition, chunk);
		if (error != 0)
			goto out;
		for (word = 0; word * 4 < chunk; word++) {
			error = brcmstb_i2c_read(sc, BRCMSTB_I2C_DATA_OUT(word),
			    &value);
			if (error != 0)
				goto out;
			for (byte = 0; byte < 4 && word * 4 + byte < chunk; byte++)
				data[done + word * 4 + byte] =
				    (uint8_t)(value >> (8 * byte));
		}
	}

	error = brcmstb_i2c_tick(sc);
	if (error != 0)
		goto out;
	memcpy(buf, data, buflen);
out:
	/* Never write after failure. The lifecycle owner must retain resources. */
	if (error != 0 && sc->status.mutation_attempted)
		sc->status.fault = error;
	sc->status.active = false;
	return error;
}

int
brcmstb_i2c_status(const struct brcmstb_i2c *sc,
    struct brcmstb_i2c_status *out)
{

	if (sc == NULL || out == NULL || !sc->initialized)
		return EINVAL;
	*out = sc->status;
	return 0;
}

int
brcmstb_i2c_fini(struct brcmstb_i2c *sc)
{

	if (sc == NULL || !sc->initialized)
		return EINVAL;
	if (sc->status.active)
		return EBUSY;
	if (sc->status.fault != 0)
		return sc->status.fault;
	sc->initialized = false;
	return 0;
}
