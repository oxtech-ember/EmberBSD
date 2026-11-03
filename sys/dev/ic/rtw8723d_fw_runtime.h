/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native RTL8723D integration, AI-assisted. */
#ifndef _DEV_IC_RTW8723D_FW_RUNTIME_H_
#define _DEV_IC_RTW8723D_FW_RUNTIME_H_
struct rtw88_softc;
/* RTL8723D WCPU_11N does not send packet-H2C general/PHYDM startup info.
 * Validate the fresh legacy firmware owner; do not fabricate packet-H2C
 * readiness or an ACK. Register mailbox commands remain separate. */
int rtw8723d_fw_runtime_init(struct rtw88_softc *);
#endif
