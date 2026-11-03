/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD; AI-assisted native RTL8723D descriptor interface. */
#ifndef _DEV_IC_RTW8723D_TX_H_
#define _DEV_IC_RTW8723D_TX_H_
#ifdef _KERNEL
#include <sys/types.h>
#else
#include <stddef.h>
#include <stdint.h>
#endif

#define RTW8723D_TX_PACKET_DESC_SIZE 40U
struct rtw88_tx_info;

/* Encode one 40-byte descriptor for the initial 2.4 GHz HT20/1SS path.
 * queue is raw descriptor QSEL (DATA TID or the SDIO-selected management
 * queue); this function does not map software rings to queues. The caller
 * must authorize chip/profile/channel/key/generation and capacity first.
 * Reject unsupported width, rate, STBC/LDPC and packet offsets before output.
 * The checksum complements XOR of the first 16 LE16 words, with its field
 * zeroed, and excludes words 8/9. Output is unchanged on every error. */
int rtw8723d_tx_encode(uint8_t *, size_t, const struct rtw88_tx_info *);

#endif /* _DEV_IC_RTW8723D_TX_H_ */
