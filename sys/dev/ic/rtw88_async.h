/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native RTL8723D integration, AI-assisted. */
#ifndef _DEV_IC_RTW88_ASYNC_H_
#define _DEV_IC_RTW88_ASYNC_H_

#include <dev/ic/rtw88_packet.h>
#include <dev/ic/rtw88_hci.h>
#include <dev/ic/rtw88_hw.h>

#define RTW88_ASYNC_SLOTS 64U
#define RTW88_ASYNC_DATA 56U
#define RTW88_ASYNC_PER_QUEUE 16U
#define RTW88_ASYNC_LOW_WATER 8U
#define RTW88_ASYNC_DEADLINE_MS 1000U

struct rtw88_async_epoch {
	uint32_t generation;
	uint64_t epoch, key_epoch;
};

enum rtw88_control_kind {
	RTW88_CTL_INIT, RTW88_CTL_STATE, RTW88_CTL_QUIET, RTW88_CTL_STOP,
	RTW88_CTL_KEY_FENCE, RTW88_CTL_RECOVER, RTW88_CTL_DIAG
};

enum rtw88_diag_op {
	RTW88_DIAG_OP_BOOTSTRAP, RTW88_DIAG_OP_H2C,
	RTW88_DIAG_OP_LOOPBACK, RTW88_DIAG_OP_CHANNEL, RTW88_DIAG_OP_REGS,
	RTW88_DIAG_OP_LBMODE
};

#define RTW88_CONTROL_BSSID 1U
#define RTW88_CONTROL_EDCA 2U
#define RTW88_CONTROL_RA 4U

struct rtw88_control_snapshot {
	/* Bound to the original stack request, never refreshed by the executor. */
	uint64_t sae_serial;
	enum rtw88_control_kind kind;
	struct rtw88_async_epoch id;
	int state, arg;
	struct rtw88_channel_tuple channel;
	uint8_t bssid[6];
	unsigned aid, basic, slot;
	struct rtw88_edca_params edca[4];
	struct rtw88_ra_config ra;
	uint32_t valid;
	enum rtw88_diag_op diag_op;
	uint32_t diag_value;
};

struct rtw88_control_result {
	struct rtw88_async_epoch id;
	int error;
	uint32_t regs[8];
};

struct rtw88_async_ticket {
	unsigned slot;
	uint64_t serial;
};

struct rtw88_tx_job {
	struct rtw88_async_epoch id;
	unsigned q, operation; /* AUTO: use_protection 0/1; otherwise zero. */
	enum rtw88_tx_mode mode;
	bool control;
	struct rtw88_tx_fixed fixed;
	struct rtw88_tx_agg agg;
	size_t length;
	uint8_t bytes[RTW88_PACKET_SIZE];
};

enum rtw88_async_state {
	RTW88_ASYNC_FREE,
	RTW88_ASYNC_RESERVED,
	RTW88_ASYNC_QUEUED,
	RTW88_ASYNC_ACTIVE
};

struct rtw88_async_slot {
	struct rtw88_tx_job job;
	struct rtw88_async_epoch owner;
	uint64_t serial;
	uint32_t committed_ms;
	unsigned q;
	enum rtw88_async_state state;
	bool control, stale;
};

struct rtw88_async {
	struct rtw88_async_epoch id;
	uint64_t serial;
	unsigned count[RTW88_NTXQ], total, data;
	unsigned fifo[RTW88_NTXQ][RTW88_ASYNC_PER_QUEUE];
	unsigned head[RTW88_NTXQ], tail[RTW88_NTXQ], queued[RTW88_NTXQ];
	unsigned nextq;
	uint32_t blocked_data, blocked_control, wake_mask;
	int closed, error;
	struct rtw88_async_slot slots[RTW88_ASYNC_SLOTS];
};

/* All calls and wake_mask changes require the same external interlock.
 * A producer may fill only mode/operation/fixed/agg/bytes while reserved.
 * Commit transfers ownership. Only a matching cancel releases a reservation.
 * The executor may read active bytes without the interlock until finish.
 * Withdraw cannot reuse reserved/active bytes. Repeated equal fences are inert.
 * Serial exhaustion closes new admission; committed owners can still drain.
 * The adapter consumes wake_mask under the interlock and schedules stack work.
 * Initialize only fresh or completely drained storage. No borrowed pointer
 * may survive cancellation, completion or lifetime destruction. */
void rtw88_async_init(struct rtw88_async *, const struct rtw88_async_epoch *);
int rtw88_async_reserve(struct rtw88_async *, const struct rtw88_async_epoch *,
    unsigned, bool, struct rtw88_async_ticket *, struct rtw88_tx_job **);
int rtw88_async_commit(struct rtw88_async *, const struct rtw88_async_ticket *,
    size_t, uint32_t);
int rtw88_async_cancel(struct rtw88_async *, const struct rtw88_async_ticket *);
int rtw88_async_take(struct rtw88_async *, uint32_t,
    struct rtw88_async_ticket *, const struct rtw88_tx_job **);
/* Return ownership-transition status, independent of the supplied TX errno.
 * Retry only NOT_STARTED+EBUSY within 1000ms of first commit. */
int rtw88_async_finish(struct rtw88_async *, const struct rtw88_async_ticket *,
    int, enum rtw88_tx_progress, uint32_t);
void rtw88_async_withdraw(struct rtw88_async *, const struct rtw88_async_epoch *);

#endif
