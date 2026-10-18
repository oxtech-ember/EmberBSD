/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _ARM_BROADCOM_BCM2712_DISPLAY_DDC_H_
#define _ARM_BROADCOM_BCM2712_DISPLAY_DDC_H_

#include <sys/types.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <dev/i2c/brcmstb_i2cvar.h>
#include <arm/broadcom/bcm2712_display.h>

#define BCM2712_DDC_EDID_LENGTH 128
#define BCM2712_DDC_EDID_COMPLETE_CAPACITY (2 * BCM2712_DDC_EDID_LENGTH)

struct bcm2712_ddc_provider;
struct bcm2712_ddc_status {
	struct brcmstb_i2c_status core;
	uint32_t bsc_status, bsc_mask;
	bool status_valid, mask_valid;
	int fault;
};

/*
 * Process context only, including polled transfers. The caller exclusively
 * owns accessible resource13/14 mappings and keeps HSM/DVP leases and power
 * valid until destroy succeeds. Exclude firmware and other BSC/IRQ users.
 * The inherited leaf1 mask is a guard, not proof of exclusive ownership.
 * No callback or i2c tag is accepted from or returned to the caller.
 * Quiesce API callers before destroying their last provider reference.
 * Destroy waits for a current operation and rejects later calls through the
 * registry. A fault retains the provider, tag and caller-owned resources.
 */
int bcm2712_display_ddc_create(device_t,
    const struct bcm2712_display_contract *,
    const struct bcm2712_display_memory *, bus_space_handle_t,
    const struct bcm2712_display_memory *, bus_space_handle_t, bus_space_tag_t,
    struct bcm2712_ddc_provider **);
/*
 * Base block only: address 0x50, command 0, exactly 128 bytes.
 * Output is unchanged on error.
 */
int bcm2712_display_ddc_read_edid(struct bcm2712_ddc_provider *, uint8_t *,
    size_t);
/*
 * Complete base plus at most one extension: address 0x50, offsets 0 and 128.
 * Capacity must be at least 256. Success initializes exactly the first 256
 * bytes; a base-only result has a zeroed second half and out_length 128.
 * One extension gives out_length 256 after its independent checksum passes.
 * More extensions return EOPNOTSUPP. Both outputs are unchanged on error.
 * Bytes beyond the first 256 are unchanged, including on success.
 * The caller supplies valid, disjoint writable out and out_length storage.
 */
int bcm2712_display_ddc_read_edid_complete(struct bcm2712_ddc_provider *,
    uint8_t *, size_t, size_t *);
/* Cached, non-atomic observations only. No MMIO is performed. */
int bcm2712_display_ddc_status(struct bcm2712_ddc_provider *,
    struct bcm2712_ddc_status *);
int bcm2712_display_ddc_destroy(struct bcm2712_ddc_provider *);

#endif /* _ARM_BROADCOM_BCM2712_DISPLAY_DDC_H_ */
