/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native RTL8723D integration, AI-assisted. */
#ifndef _DEV_IC_RTW8723D_RUNTIME_H_
#define _DEV_IC_RTW8723D_RUNTIME_H_
#include <dev/ic/rtw8723d_core_identity.h>
#include <dev/ic/rtw8723d_core_phy.h>
#include <dev/ic/rtw8723d_mailbox.h>

struct rtw88_edca_params;
struct rtw88_channel_tuple;

/* Embedded, zero-initialized selected-chip storage. The frontend pins this
 * object in sc_chip_private before attach and retains it through confirmed
 * transport quiescence and power-off. The sole executor owns all fields.
 * Configuration/profile is explicit; identity comes from this device's EFUSE.
 * No default regulatory profile, board power, or readiness is inferred here. */
struct rtw8723d_runtime {
	struct rtw8723d_core_boot boot;
	struct rtw8723d_core_identity identity;
	struct rtw8723d_core_phy_config config;
	struct rtw8723d_core_phy_result phy;
	struct rtw8723d_mailbox mailbox;
	bool identified;
};

/* Native chip_ops control callbacks. Caller drains TX and closes software
 * admission before channel/station/EDCA changes, as required by net80211's
 * serialized control worker. Errors do not publish new channel/link state;
 * mutation failures close RF admission and require the existing owner unwind.
 * These callbacks do not make this an attached/operational chip_ops table. */
int rtw8723d_runtime_check(void *);
int rtw8723d_runtime_channel(struct rtw88_softc *,
    const struct rtw88_channel_tuple *);
int rtw8723d_runtime_station(struct rtw88_softc *, const uint8_t *, unsigned,
    unsigned, unsigned, int);
int rtw8723d_runtime_edca(struct rtw88_softc *,
    const struct rtw88_edca_params *, unsigned);
/* ready is separate caller-owned pinned output, zeroed before admission. */
int rtw8723d_runtime_mailbox_begin(struct rtw88_softc *, unsigned, int *);
int rtw8723d_runtime_mailbox_end(struct rtw88_softc *, int);
int rtw8723d_runtime_command(struct rtw88_softc *, uint8_t, const void *, size_t);
#endif
