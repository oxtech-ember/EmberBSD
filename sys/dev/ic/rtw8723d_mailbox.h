/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native RTL8723D integration, AI-assisted. */
#ifndef _DEV_IC_RTW8723D_MAILBOX_H_
#define _DEV_IC_RTW8723D_MAILBOX_H_
#ifdef _KERNEL
#include <sys/types.h>
#else
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#endif
struct rtw88_io;
struct rtw8723d_mailbox {
	uint32_t generation;
	uint8_t next, first, total, sent;
	bool active, uncertain;
	int error;
};

/* Owner-zeroed storage, reset only after a fresh chip power cycle and
 * firmware boot, strictly increasing nonzero generation. This API does not
 * perform or verify that hardware reset. Uncertain commands cannot be
 * replayed in the same generation, including through a fresh I/O context. */
int rtw8723d_mailbox_reset(struct rtw8723d_mailbox *, uint32_t);

/* Powered serialized chip, exclusive software command owner. check validates
 * generation/stop identity at each callback boundary; it may be NULL only
 * if the caller prevents such changes for the complete synchronous call.
 * A cancellable actor always supplies it. Reserve0..3 consecutive
 * boxes, ready0 on occupancy without mutation; caller supplies a deadline.
 * No packet-H2C offload or firmware command completion/ACK is implied. */
int rtw8723d_mailbox_begin(struct rtw88_io *, struct rtw8723d_mailbox *,
    uint32_t, unsigned, int *, int (*)(void *), void *);
int rtw8723d_mailbox_end(struct rtw8723d_mailbox *, uint32_t, int);

/* Command ID plus at most7 payload bytes, zero pad both words, EX first.
 * Reserved command checks availability once; standalone command polls at
 * most100 times with100us gaps. A failed/uncertain write or post-write
 * cancellation quarantines the mailbox until fresh-power reset. Output
 * progress is firmware FIFO publication only, never radio/station success. */
int rtw8723d_mailbox_send(struct rtw88_io *, struct rtw8723d_mailbox *,
    uint32_t, uint8_t, const void *, size_t, int (*)(void *), void *);
#endif /* _DEV_IC_RTW8723D_MAILBOX_H_ */
