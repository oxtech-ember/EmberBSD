/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _ARM_BROADCOM_BCM2712_DISPLAY_DVP_H_
#define _ARM_BROADCOM_BCM2712_DISPLAY_DVP_H_

#include <sys/types.h>
#include <arm/broadcom/bcm2712_display.h>

#define BCM2712_DVP_PORTS 2
#define BCM2712_DVP_RESOURCE 10
#define BCM2712_DVP_SW_INIT 0x04
#define BCM2712_DVP_MISC_CONFIG 0x08

/* Callbacks finish before return. An error after write can mean mutation. */
struct bcm2712_dvp_io {
	int (*read)(void *, unsigned int, uint32_t *);
	int (*write)(void *, unsigned int, uint32_t);
	void (*barrier)(void *);
	void *arg;
};

struct bcm2712_dvp_status {
	uint32_t gates, resets;
	unsigned int leases;
	int fault;
	bool mutation_attempted;
};

/* The caller serializes core operations. This API never writes reset bits. */
struct bcm2712_dvp {
	struct bcm2712_dvp_io io;
	struct bcm2712_dvp_status status;
	uint32_t restore;
	bool initialized;
};

int bcm2712_dvp_init(struct bcm2712_dvp *,
    const struct bcm2712_display_contract *,
    const struct bcm2712_display_memory *, const struct bcm2712_dvp_io *);
int bcm2712_dvp_acquire(struct bcm2712_dvp *, unsigned int);
int bcm2712_dvp_release(struct bcm2712_dvp *, unsigned int);
int bcm2712_dvp_fini(struct bcm2712_dvp *);

#ifdef _KERNEL
#include <sys/bus.h>
#include <sys/device.h>
struct bcm2712_dvp_provider;
/*
 * Process context only. The lifecycle owner supplies an exclusively owned,
 * accessible resource 10 mapping. It must keep power, clocks and the mapping
 * alive until destroy succeeds. Neither resource decoding nor an HSM status
 * snapshot proves that MMIO is safe. No automatic attachment occurs.
 * Quiesce each port before release. Quiesce API users before destroy; it must
 * not race a new caller that retains a provider pointer after destruction.
 */
int bcm2712_display_dvp_create(device_t,
    const struct bcm2712_display_contract *,
    const struct bcm2712_display_memory *, bus_space_tag_t, bus_space_handle_t,
    struct bcm2712_dvp_provider **);
int bcm2712_display_dvp_acquire(struct bcm2712_dvp_provider *, unsigned int);
int bcm2712_display_dvp_release(struct bcm2712_dvp_provider *, unsigned int);
int bcm2712_display_dvp_status(struct bcm2712_dvp_provider *,
    struct bcm2712_dvp_status *);
int bcm2712_display_dvp_destroy(struct bcm2712_dvp_provider *);
#endif
#endif
