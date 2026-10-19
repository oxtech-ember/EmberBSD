/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _ARM_BROADCOM_BCM2712_IOMMUC_EXECUTOR_H_
#define _ARM_BROADCOM_BCM2712_IOMMUC_EXECUTOR_H_

#include <arm/broadcom/bcm2712_iommuc_native.h>

/*
 * Private trusted built-in kernel composition. No runtime callback whitelist.
 * Process context only; caller holds its MMU4 mutex before the ICAC mutex.
 * The callback/arg/core pointer are borrowed only for this synchronous call.
 * Use a local actual MMU4 sequencer; copy status before untouched fini. Preserve
 * the primary error and separate close error in the persistent native owner.
 * No re-entry, allocation, AML, pointer escape, callback replacement, shared
 * close/re-init, MMU2/5 lease change or post-retain cleanup is permitted.
 * Native owner storage/text/backing/maps/render references outlive retention.
 * The provider consumes MMU4 once, before init/callback/IO, even on clean error.
 * It does not save the callback or arg. Status preserves attempt/error/fault.
 * Normal-active SoC access and all firmware/MMU2/4/5/cache writer exclusion are
 * external caller obligations. The observation sample grants no permission.
 * No default caller, recovery, disable, unmap or detach operation exists.
 */
typedef int (*bcm2712_iommuc_mmu4_operation)(void *,
    struct bcm2712_iommuc_core *);
int bcm2712_iommuc_execute_mmu4(struct bcm2712_iommuc_provider *,
    bcm2712_iommuc_mmu4_operation, void *);

#endif
