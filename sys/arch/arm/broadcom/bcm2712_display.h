/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _ARM_BROADCOM_BCM2712_DISPLAY_H_
#define _ARM_BROADCOM_BCM2712_DISPLAY_H_

#include <sys/types.h>

/* GPU0 _DSD property. Version 1 preserves the legacy resource indices. */
#define BCM2712_DISPLAY_PROPERTY "emberbsd,display-contract-version"
#define BCM2712_DISPLAY_VERSION 1
#define BCM2712_DISPLAY_NMEM 32
#define BCM2712_DISPLAY_NIRQ 7
#define BCM2712_DISPLAY_NCLOCK 4
#define BCM2712_DISPLAY_NDOMAIN 3
#define BCM2712_DISPLAY_NLEAF 17
#define BCM2712_DISPLAY_HSM_MIN_RATE 120000000U

enum bcm2712_display_owner {
	BCM2712_OWNER_RENDER,
	BCM2712_OWNER_SCANOUT,
	BCM2712_OWNER_DVP_CLOCK,
	BCM2712_OWNER_HD_SHARED,
	BCM2712_OWNER_AON_IRQ,
	BCM2712_OWNER_BSC_IRQ,
	BCM2712_OWNER_DDC0,
	BCM2712_OWNER_DDC1,
	BCM2712_OWNER_HDMI0,
	BCM2712_OWNER_HDMI1
};

struct bcm2712_display_memory {
	uint64_t address;
	uint64_t length;
};

struct bcm2712_display_resource {
	const char *name;
	enum bcm2712_display_owner owner;
	struct bcm2712_display_memory memory;
};

struct bcm2712_display_irq {
	const char *name;
	uint32_t gsi;
};

struct bcm2712_display_domain {
	const char *name;
	unsigned int memory_index;
	unsigned int irq_index;
};

struct bcm2712_display_leaf {
	const char *name;
	unsigned int domain;
	unsigned int bit;
};

struct bcm2712_display_contract {
	uint32_t version;
	struct bcm2712_display_memory memory[BCM2712_DISPLAY_NMEM];
	uint32_t irq[BCM2712_DISPLAY_NIRQ];
};

struct bcm2712_display_clock {
	uint32_t id;
	uint32_t state;
	uint32_t rate;
};

struct bcm2712_display_clocks {
	struct bcm2712_display_clock clock[BCM2712_DISPLAY_NCLOCK];
};

extern const struct bcm2712_display_resource
    bcm2712_display_resources[BCM2712_DISPLAY_NMEM];
extern const struct bcm2712_display_irq
    bcm2712_display_irqs[BCM2712_DISPLAY_NIRQ];
extern const struct bcm2712_display_domain
    bcm2712_display_domains[BCM2712_DISPLAY_NDOMAIN];
extern const struct bcm2712_display_leaf
    bcm2712_display_leaves[BCM2712_DISPLAY_NLEAF];

/* No mapping, clock mutation, reset or resource claim occurs in these APIs. */
int bcm2712_display_decode(uint64_t, const struct bcm2712_display_memory *,
    size_t, const uint32_t *, size_t, struct bcm2712_display_contract *);

/* The transport exchanges one aligned eight-word firmware property packet. */
typedef int (*bcm2712_display_exchange_fn)(void *, uint32_t *);
int bcm2712_display_read_clocks(bcm2712_display_exchange_fn, void *,
    struct bcm2712_display_clocks *);

/* Success validates these stages only. It never authorizes display MMIO. */
#endif
