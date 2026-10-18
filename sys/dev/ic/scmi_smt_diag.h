/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: original checked SCMI wire helper, AI-assisted. */
#ifndef _DEV_IC_SCMI_SMT_DIAG_H_
#define _DEV_IC_SCMI_SMT_DIAG_H_

#include <sys/types.h>
#ifndef _KERNEL
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#endif

#define SCMI_SMT_DIAG_INITIALIZER { 0 }
#define SCMI_SMT_DIAG_REPLY_MAX 96U

/* Closed read-only query commands. Control has a separate explicit API. */
enum scmi_smt_diag_cmd {
	SCMI_SMT_DIAG_BASE_VERSION,
	SCMI_SMT_DIAG_BASE_ATTRIBUTES,
	SCMI_SMT_DIAG_BASE_LIST_PROTOCOLS,
	SCMI_SMT_DIAG_CLOCK_ATTRIBUTES,
	SCMI_SMT_DIAG_CLOCK_ENTITY_ATTRIBUTES,
	SCMI_SMT_DIAG_CLOCK_RATE_GET,
	SCMI_SMT_DIAG_CLOCK_VERSION,
	SCMI_SMT_DIAG_CLOCK_MESSAGE_ATTRIBUTES
};

enum scmi_smt_diag_barrier {
	SCMI_SMT_DIAG_PUBLISH = 1, /* Request words before busy status. */
	SCMI_SMT_DIAG_SEND,        /* Complete busy status before doorbell. */
	SCMI_SMT_DIAG_RECEIVE      /* Acquire response after FREE. */
};

struct scmi_smt_diag_io {
	void *cookie;
	/* Aligned32-bit numeric LE words, relative to a validated TX region. */
	uint32_t (*read_4)(void *, uint32_t);
	void (*write_4)(void *, uint32_t, uint32_t);
	void (*barrier)(void *, enum scmi_smt_diag_barrier);
	uint64_t (*now_us)(void *);
	void (*pause)(void *); /* Pause10us; must return. */
};

/* Opaque by contract. Allocate once with SCMI_SMT_DIAG_INITIALIZER. */
struct scmi_smt_diag {
	struct scmi_smt_diag_io io;
	uint64_t last_time;
	uint16_t next_token;
	unsigned int state;
	bool time_seen;
};

struct scmi_smt_diag_reply {
	size_t data_length;
	uint8_t data[SCMI_SMT_DIAG_REPLY_MAX]; /* Excludes firmware status. */
};

enum scmi_smt_diag_stage {
	SCMI_SMT_DIAG_WAIT_FREE,
	SCMI_SMT_DIAG_WAIT_RESPONSE,
	SCMI_SMT_DIAG_CLOCK,
	SCMI_SMT_DIAG_CHANNEL,
	SCMI_SMT_DIAG_HEADER,
	SCMI_SMT_DIAG_LENGTH,
	SCMI_SMT_DIAG_FIRMWARE,
	SCMI_SMT_DIAG_COMPLETE
};

struct scmi_smt_diag_trace {
	enum scmi_smt_diag_stage stage;
	bool firmware_status_valid;
	uint32_t request_header;
	uint32_t response_header;
	uint32_t response_length;
	int32_t firmware_status;
};

/*
 * Init only a fresh zero-initialized instance. Init performs no callbacks and
 * never resets a live or poisoned instance. Invalid input leaves it unchanged.
 * The caller owns validated resources, bus ordering, exclusive channel lifetime
 * and serialization. Callbacks must not reenter. No concurrent or IRQ use.
 *
 * Query accepts argument0 for no-argument commands, skip0..255 for BASE LIST,
 * clock67 for entity attributes, clocks67/264/266 for rate, or message7 for
 * CLOCK MESSAGE ATTRIBUTES. UART264/266 are read-only functional clock IDs;
 * this does not qualify their firmware support, rate or UART operation.
 * Other commands/arguments return EINVAL
 * without callbacks. Each issued request gets a fresh10-bit token; exhaustion
 * is EOVERFLOW, without callbacks. A poisoned instance returns EIO likewise.
 *
 * Reply stays unchanged on all errors. Trace is an explicit failure-reporting
 * output, published after a transport attempt; invalid API/state leaves it
 * unchanged. Nonzero firmware status is EIO with its full signed32-bit value.
 * A complete nonzero firmware response does not poison the transport.
 *
 * Each FREE/completion wait is bounded by30ms and3000 polls. Initial busy is
 * EBUSY without writes. Clock regression/wrap poisons with EIO. Transport failure after send or a
 * corrupt channel poisons the instance. No reset, forced FREE, retry, token reuse
 * or post-poison callback is permitted. Keep the channel lease boot-long.
 * The query API does not qualify capabilities or activate any resource.
 */
int scmi_smt_diag_init(struct scmi_smt_diag *, const struct scmi_smt_diag_io *);
int scmi_smt_diag_query(struct scmi_smt_diag *, enum scmi_smt_diag_cmd, uint32_t,
    struct scmi_smt_diag_reply *, struct scmi_smt_diag_trace *);

/*
 * Explicit CLOCK_CONFIG_SET for fixed clock67 only. Enable must be0 or1.
 * The caller must qualify capabilities, own the clock and serialize this wire.
 * This helper shares the query token/state and context rules; it has no retries.
 * Invalid input/state, poison and exhaustion perform no callbacks and preserve
 * trace. A control error can follow a hardware change; retain caller ownership.
 *
 * Success requires status0 and no data body. An otherwise fully framed reply
 * with unexpected data returns EPROTO with trace COMPLETE; the wire stays usable
 * for an explicit disable attempt. The data body is not read. A framed firmware
 * error is EIO with its full signed status and likewise leaves the wire usable.
 * Transport faults poison as above and prohibit even rollback I/O. No public
 * reply or arbitrary ID/message/payload argument is exposed by this operation.
 */
int scmi_smt_clock67_config_set(struct scmi_smt_diag *, uint32_t,
    struct scmi_smt_diag_trace *);

#endif
