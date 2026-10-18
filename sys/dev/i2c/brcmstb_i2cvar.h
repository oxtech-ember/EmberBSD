/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _DEV_I2C_BRCMSTB_I2CVAR_H_
#define _DEV_I2C_BRCMSTB_I2CVAR_H_

#include <dev/i2c/i2cvar.h>

#define BRCMSTB_I2C_MAX_READ	128
#define BRCMSTB_I2C_CHUNK	32
#define BRCMSTB_I2C_TIMEOUT_US	100000
#define BRCMSTB_I2C_POLL_BUDGET	10000

/*
 * The owner serializes all calls. Callbacks finish before return.
 * Read, write and guard return zero or a positive BSD errno.
 * A failed write can have reached hardware. Barrier orders all MMIO.
 * Guard never writes. It verifies an inherited interrupt mask.
 * With idle=true, guard also rejects every pending source for this bus.
 * With idle=false, an owned completion can be pending under that mask.
 * Keep callbacks, their argument and all resource owners alive after fault.
 */
struct brcmstb_i2c_io {
	int (*read)(void *, unsigned int, uint32_t *);
	int (*write)(void *, unsigned int, uint32_t);
	void (*barrier)(void *);
	uint64_t (*time_us)(void *);
	void (*delay_us)(void *, unsigned int);
	int (*guard)(void *, bool);
	void *arg;
};

struct brcmstb_i2c_status {
	unsigned int commands;
	unsigned int polls;
	uint32_t enable;
	int fault;
	bool active;
	bool mutation_attempted;
};

struct brcmstb_i2c {
	struct brcmstb_i2c_io io;
	struct brcmstb_i2c_status status;
	uint64_t started, previous;
	bool initialized;
};

/* Initialize new storage once. Never reinitialize a retained owner. */
int brcmstb_i2c_init(struct brcmstb_i2c *, const struct brcmstb_i2c_io *);
int brcmstb_i2c_exec(void *, i2c_op_t, i2c_addr_t, const void *, size_t,
    void *, size_t, int);
int brcmstb_i2c_status(const struct brcmstb_i2c *,
    struct brcmstb_i2c_status *);
/* No hardware recovery. A fault prevents release of this owner. */
int brcmstb_i2c_fini(struct brcmstb_i2c *);

#endif /* _DEV_I2C_BRCMSTB_I2CVAR_H_ */
