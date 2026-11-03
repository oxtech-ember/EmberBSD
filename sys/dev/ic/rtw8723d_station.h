/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native RTL8723D integration, AI-assisted. */
#ifndef _DEV_IC_RTW8723D_STATION_H_
#define _DEV_IC_RTW8723D_STATION_H_
#ifdef _KERNEL
#include <sys/types.h>
#else
#include <stdbool.h>
#include <stdint.h>
#endif
struct rtw88_io;
struct rtw8723d_mailbox;
#define RTW8723D_STATION_MACID 1U
struct rtw8723d_station_config {
	uint8_t mac[6], bssid[6];
	unsigned aid, basic_rates, slot;
	bool linked;
};

/* Single interface, hardware port0 and firmware peer MACID1. Owner has a
 * powered/configured 2G chip, hardware crypto disabled, drained TX and closed
 * software admission. Unlinked config has zero BSSID/AID; linked AID1..2007.
 * Snapshot/validate all input before I/O. Check hook may be NULL only under
 * full synchronous owner serialization; cancellable actors always supply it.
 * Reserve the media-status mailbox command before a managed state update.
 * No default-port command or association/firmware ACK is inferred here.
 * Failure after mutation independently closes data TX/port/RX filtering and
 * requires owner power unwind; caller publishes link state only on success. */
int rtw8723d_station_apply(struct rtw88_io *, struct rtw8723d_mailbox *,
    uint32_t, const struct rtw8723d_station_config *, int (*)(void *), void *);
#endif /* _DEV_IC_RTW8723D_STATION_H_ */
