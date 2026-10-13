/* Origin: EmberBSD BCM2712 V3D takeover ownership interface, 2026-10-10. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#ifndef _ARM_BROADCOM_BCM2712_V3D_TAKEOVER_H
#define _ARM_BROADCOM_BCM2712_V3D_TAKEOVER_H

#include <sys/bus.h>

/*
 * The takeover retains the PM claim and the HUB/CORE/SMS mappings until
 * reboot. These accessors expose the sealed HUB window to the opt-in
 * DMA probe only after the complete takeover, never before it.
 */
bool	bcmv3d_takeover_complete(void);
int	bcmv3d_takeover_hub_peek(bus_size_t, uint32_t *);
int	bcmv3d_takeover_hub_poke(bus_size_t, uint32_t);
int	bcmv3d_takeover_core_peek(bus_size_t, uint32_t *);
int	bcmv3d_takeover_core_poke(bus_size_t, uint32_t);

#endif /* _ARM_BROADCOM_BCM2712_V3D_TAKEOVER_H */
