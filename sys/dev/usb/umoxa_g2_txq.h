/* Origin: EmberBSD bounded shared MOXA G2 transmit engine, 2026-10-10. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#ifndef _DEV_USB_UMOXA_G2_TXQ_H_
#define _DEV_USB_UMOXA_G2_TXQ_H_

#ifdef _KERNEL
#include <sys/types.h>
#else
#include <stddef.h>
#include <stdint.h>
#endif

#define UMOXA_G2_PORTS 4
#define UMOXA_G2_FRAME_MAX 1024
#define UMOXA_G2_PAYLOAD_MAX (UMOXA_G2_FRAME_MAX - 4)
#define UMOXA_G2_CREDIT_THRESHOLD 512

enum umoxa_g2_port_state { UMOXA_CLOSED, UMOXA_OPEN, UMOXA_FAULT };
enum umoxa_g2_job_state { UMOXA_EMPTY, UMOXA_QUEUED, UMOXA_INFLIGHT };

struct umoxa_g2_port {
	uint64_t epoch, last_epoch, cookie, last_cookie, deadline;
	size_t len, sent;
	enum umoxa_g2_port_state state;
	enum umoxa_g2_job_state job;
	int waiting, error;
	uint8_t data[UMOXA_G2_PAYLOAD_MAX];
};

struct umoxa_g2_xfer {
	uint64_t token, epoch, cookie;
	unsigned int port;
	const uint8_t *data;
	size_t len;
	uint32_t fault_mask;
};

struct umoxa_g2_result {
	uint64_t epoch, cookie;
	unsigned int port;
	size_t actual;
	int deliver, error;
	uint32_t fault_mask;
};

/* Caller supplies locking and quiescence; never edits initialized fields. */
struct umoxa_g2_txq {
	struct umoxa_g2_port ports[UMOXA_G2_PORTS];
	struct umoxa_g2_xfer flight;
	uint64_t timeout, now, last_token, deadline;
	size_t budget;
	unsigned int cursor;
	int active;
	uint8_t wire[UMOXA_G2_FRAME_MAX];
};

/* Valid inputs/outputs never alias q or each other; inputs stay immutable. */
int umoxa_g2_txq_init(struct umoxa_g2_txq *, size_t, uint64_t);
int umoxa_g2_txq_open(struct umoxa_g2_txq *, unsigned int, uint64_t);
int umoxa_g2_txq_close(struct umoxa_g2_txq *, unsigned int);
int umoxa_g2_txq_submit(struct umoxa_g2_txq *, unsigned int, uint64_t,
    uint64_t, const uint8_t *, size_t);
/* Borrowed wire frame stays valid through close/fault until physical done. */
int umoxa_g2_txq_pick(struct umoxa_g2_txq *, uint64_t, struct umoxa_g2_xfer *);
/* 1 consumes matching physical completion; 0 ignores stale/invalid input. */
int umoxa_g2_txq_done(struct umoxa_g2_txq *, uint64_t, size_t, int,
    struct umoxa_g2_result *);
/* Untagged wire credits require the parent's receive/purge reopen barrier. */
int umoxa_g2_txq_events(struct umoxa_g2_txq *, const uint8_t *, size_t,
    uint32_t *);
int umoxa_g2_txq_tick(struct umoxa_g2_txq *, uint64_t, uint32_t *);
uint32_t umoxa_g2_txq_fault_all(struct umoxa_g2_txq *, int);
uint32_t umoxa_g2_txq_fault_port(struct umoxa_g2_txq *, unsigned int, int);

#endif /* _DEV_USB_UMOXA_G2_TXQ_H_ */
