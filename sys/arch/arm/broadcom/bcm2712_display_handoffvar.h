/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _ARM_BROADCOM_BCM2712_DISPLAY_HANDOFFVAR_H_
#define _ARM_BROADCOM_BCM2712_DISPLAY_HANDOFFVAR_H_

#include <sys/types.h>
#ifndef _KERNEL
#include <stdbool.h>
#endif

struct bcm2712_display_handoff_status {
	bool attempted;
	bool complete;
	int fault;
};

/*
 * The callback exchanges exactly 24 little-endian bytes. It returns BSD errno.
 * It must not retain the packet pointer or access it after return on any errno.
 * It may inspect status but must not change it.
 * A native transport copies into its own stable DMA buffer and quarantines
 * that buffer on uncertain completion. It must not retain this helper's stack.
 */
typedef int (*bcm2712_display_handoff_exchange_fn)(void *, void *, size_t);

/*
 * Use fresh zeroed status. The caller serializes calls and owns status/arg
 * lifetime. Never reset or replace attempted status during this boot. Invalid
 * NULL arguments leave status unchanged. Nonfresh status returns EBUSY with
 * no I/O. Set attempted before the only exchange; preserve the first error.
 * A valid envelope with the tag response bit sets complete. Desired length may
 * exceed the zero-sized value buffer; no payload is read. An unchanged zero
 * request field returns EOPNOTSUPP. Other bit-clear fields are malformed.
 * Other malformed replies return EIO. There is no retry or cleanup operation.
 *
 * The explicit native boot owner requires its successful held-serial CPU close.
 * The raw protocol helper supplies no caller admission or hardware authority.
 * Notify firmware BEFORE new PHY/HVS changes. ACK does not prove AXI drain,
 * shared MMU/cache exclusion, clock ownership or current register contents.
 */
int bcm2712_display_notify_done(bcm2712_display_handoff_exchange_fn, void *,
    struct bcm2712_display_handoff_status *);

#endif
