/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD; native A133 PPU power ownership and request policy. */
#ifndef _ARM_SUNXI_A133_PPU_POWER_H_
#define _ARM_SUNXI_A133_PPU_POWER_H_

#ifdef _KERNEL
#include <sys/types.h>
#else
#include <stdbool.h>
#include <stdint.h>
#endif

#define A133_PPU_RSTN		0x00U
#define A133_PPU_CLOCK		0x04U
#define A133_PPU_ISOLATION	0x08U
#define A133_PPU_PSWON		0x0cU
#define A133_PPU_PSWOFF		0x10U
#define A133_PPU_STATE		0x20U
#define A133_PPU_CONTROL		0x24U
#define A133_PPU_REQUEST		0x28U
#define A133_PPU_IRQ_MASK		0x2cU

#define A133_PPU_3D	0U
#define A133_PPU_HOST	1U
#define A133_PPU_OFF	2U

struct a133_ppu_io {
	void *cookie;
	int (*read)(void *, unsigned, uint32_t *);
	int (*write)(void *, unsigned, uint32_t);
	void (*delay)(void *, unsigned);
};

struct a133_ppu_power {
	struct a133_ppu_io io;
	unsigned revision;
	bool owned;
	bool uncertain;
};

/* The caller serializes operations, supplies an established PPU interrupt,
 * and selects software control before power_set. Full OFF is permitted
 * only after the GPU consumer has quiesced all firmware and DMA work.
 * Only revision 6 admits a full transition from OFF. Revisions 0/3/4/5/6
 * can service domain-B requests. Other revisions have read-only access.
 * A failed mutation latches uncertainty; there is no implicit rollback.
 * Output arguments change only on success. No function accesses GPU MMIO. */
int a133_ppu_power_get(struct a133_ppu_power *, bool *);
/* Read-only preflight before acquiring clocks, reset or control mode.
 * change=false is a no-op; errors leave change unchanged. */
int a133_ppu_power_prepare(struct a133_ppu_power *, bool, bool *);
int a133_ppu_power_set(struct a133_ppu_power *, bool);
int a133_ppu_service_request(struct a133_ppu_power *, bool *);

#endif /* _ARM_SUNXI_A133_PPU_POWER_H_ */
