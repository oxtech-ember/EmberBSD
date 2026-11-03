/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native RTL8723D integration, AI-assisted. */
#ifndef _DEV_IC_RTW8723D_CORE_BOOT_H_
#define _DEV_IC_RTW8723D_CORE_BOOT_H_
#include <dev/ic/rtw8723d_boot.h>
struct rtw88_softc;
struct rtw8723d_core_hooks {
	void (*wait_ms)(void *, unsigned);
	/* Must safely observe external stop/revocation under its own interlock. */
	bool (*revoked)(void *);
	void *cookie;
};
struct rtw8723d_core_boot {
	struct rtw88_softc *core;
	struct rtw8723d_boot_ops ops;
	struct rtw8723d_core_hooks hooks;
	uint32_t generation;
	bool owned, firmware_booted, failed;
};
/* Context starts zeroed and survives every failed off sequence. Caller owns
 * transport/function, immutable firmware bytes and the sole hardware actor:
 * core flags/generation are actor-owned, never changed concurrently here.
 * The copied hooks handle sleepable waits and separately locked external
 * revocation; their cookie and callbacks remain alive until confirmed off. This
 * replaces the bootstrap power/firmware owner; never run them together.
 * No board power, IRQ, netif or RF readiness is established by these calls.
 * Initialize for the current start generation while fully off; reinitializing
 * a live/dirty/failed context is forbidden. Only confirmed off clears failure. */
int rtw8723d_core_boot_init(struct rtw8723d_core_boot *, struct rtw88_softc *,
    const struct rtw8723d_core_hooks *);
int rtw8723d_core_power_on(struct rtw8723d_core_boot *);
int rtw8723d_core_firmware(struct rtw8723d_core_boot *, const uint8_t *, size_t);
int rtw8723d_core_power_off(struct rtw8723d_core_boot *);
#endif
