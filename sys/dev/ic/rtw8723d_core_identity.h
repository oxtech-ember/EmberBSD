/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native RTL8723D integration, AI-assisted. */
#ifndef _DEV_IC_RTW8723D_CORE_IDENTITY_H_
#define _DEV_IC_RTW8723D_CORE_IDENTITY_H_
#include <dev/ic/rtw8723d_core_boot.h>
#include <dev/ic/rtw8723d.h>

struct rtw8723d_core_identity {
	struct rtw8723d_identity chip;
	struct rtw8723d_efuse efuse;
};

/* Pre-netif identity probe: initialized off core_boot context, one acquired
 * F1/block owner, stable sole hardware actor and immutable callbacks/hooks.
 * No firmware, RF, board-power or network admission. Use CMD52 before MAC
 * power; read/decode efuse under the shared power owner; confirm off before
 * publishing MAC/cut/calibration into core and separate output storage.
 * No regulatory domain is inferred from efuse country/channel-plan bytes.
 * Failed off retains ownership and blocks attach/restart. On any failure
 * output and core identity cache are unchanged; caller handles retained power.
 * Output, core and context storage must be separate and pinned.
 */
int rtw8723d_core_read_identity(struct rtw8723d_core_boot *,
    struct rtw8723d_core_identity *);
#endif
