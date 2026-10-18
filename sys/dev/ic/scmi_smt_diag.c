/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: original checked SCMI wire helper, AI-assisted. */
#ifdef _KERNEL
#include <sys/null.h>
#include <sys/errno.h>
#else
#include <errno.h>
#endif
#include "scmi_smt_diag.h"

/* Arm DEN0056B sections4.1 and5.1. No vendor implementation is copied. */
#define SMT_RESERVED 0U
#define SMT_STATUS 4U
#define SMT_FLAGS 16U
#define SMT_LENGTH 20U
#define SMT_HEADER 24U
#define SMT_PAYLOAD 28U
#define SMT_DOORBELL 128U
#define SMT_FREE 1U
#define SMT_ERROR 2U
#define SMT_TIMEOUT_US 30000U
#define SMT_MAX_POLLS 3000U
#define SMT_MAX_TOKEN 1023U

#define SMT_FRESH 0U
#define SMT_READY 1U
#define SMT_BUSY 2U
#define SMT_POISONED 3U

/* Internal only. Public entry points construct fixed, validated requests. */
struct scmi_smt_request {
	uint32_t header;
	uint32_t length;
	uint32_t minimum;
	uint32_t arguments[2];
	bool list;
	bool status_only;
};

static int
scmi_smt_diag_command(enum scmi_smt_diag_cmd cmd, uint32_t argument,
    uint32_t *header, uint32_t *length, uint32_t *minimum)
{
	*length = 4;
	*minimum = 4;
	switch (cmd) {
	case SCMI_SMT_DIAG_BASE_VERSION:
		*header = 0x4000;
		break;
	case SCMI_SMT_DIAG_BASE_ATTRIBUTES:
		*header = 0x4001;
		break;
	case SCMI_SMT_DIAG_BASE_LIST_PROTOCOLS:
		if (argument > 255)
			return EINVAL;
		*header = 0x4006;
		*length = 8;
		return 0;
	case SCMI_SMT_DIAG_CLOCK_MESSAGE_ATTRIBUTES:
		if (argument != 7)
			return EINVAL;
		*header = 0x5002;
		*length = 8;
		return 0;
	case SCMI_SMT_DIAG_CLOCK_ATTRIBUTES:
		*header = 0x5001;
		break;
	case SCMI_SMT_DIAG_CLOCK_VERSION:
		*header = 0x5000;
		break;
	case SCMI_SMT_DIAG_CLOCK_ENTITY_ATTRIBUTES:
	case SCMI_SMT_DIAG_CLOCK_RATE_GET:
		if (argument != 67)
			return EINVAL;
		*header = cmd == SCMI_SMT_DIAG_CLOCK_ENTITY_ATTRIBUTES ?
		    0x5003 : 0x5006;
		*length = 8;
		*minimum = cmd == SCMI_SMT_DIAG_CLOCK_ENTITY_ATTRIBUTES ? 20 : 8;
		return 0;
	default:
		return EINVAL;
	}
	return argument == 0 ? 0 : EINVAL;
}

static uint32_t
scmi_smt_diag_le32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
	    (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int
scmi_smt_diag_wait(struct scmi_smt_diag *s, int initial,
    struct scmi_smt_diag_trace *trace)
{
	uint64_t start = 0;
	unsigned int poll;

	for (poll = 0; poll < SMT_MAX_POLLS; poll++) {
		uint64_t now = s->io.now_us(s->io.cookie);
		uint32_t status;
		if (s->time_seen && now < s->last_time) {
			trace->stage = SCMI_SMT_DIAG_CLOCK;
			return EIO;
		}
		s->time_seen = true;
		s->last_time = now;
		if (poll == 0)
			start = now;
		if (now - start >= SMT_TIMEOUT_US)
			break;
		status = s->io.read_4(s->io.cookie, SMT_STATUS);
		/* Never erase a pre-existing channel fault, even when FREE. */
		if ((status & ~(SMT_FREE | SMT_ERROR)) != 0 ||
		    (status & SMT_ERROR) != 0) {
			trace->stage = SCMI_SMT_DIAG_CHANNEL;
			return EPROTO;
		}
		if ((status & SMT_FREE) != 0)
			return 0;
		if (poll + 1 < SMT_MAX_POLLS)
			s->io.pause(s->io.cookie);
	}
	return initial ? EBUSY : ETIMEDOUT;
}

int
scmi_smt_diag_init(struct scmi_smt_diag *s, const struct scmi_smt_diag_io *io)
{
	if (s == NULL || io == NULL || io->read_4 == NULL ||
	    io->write_4 == NULL || io->barrier == NULL ||
	    io->now_us == NULL || io->pause == NULL)
		return EINVAL;
	if (s->state != SMT_FRESH)
		return EBUSY;
	s->io = *io;
	s->next_token = 1;
	s->last_time = 0;
	s->time_seen = false;
	s->state = SMT_READY;
	return 0;
}

static int
scmi_smt_ready(const struct scmi_smt_diag *s, const struct scmi_smt_diag_reply *out,
    const struct scmi_smt_diag_trace *trace)
{
	if (s == NULL || out == NULL || trace == NULL)
		return EINVAL;
	if (s->state == SMT_POISONED)
		return EIO;
	if (s->state == SMT_FRESH)
		return EINVAL;
	if (s->state != SMT_READY)
		return EBUSY;
	return 0;
}

static int
scmi_smt_transfer(struct scmi_smt_diag *s, const struct scmi_smt_request *request,
    struct scmi_smt_diag_reply *out, struct scmi_smt_diag_trace *trace)
{
	struct scmi_smt_diag_reply reply = { 0 };
	struct scmi_smt_diag_trace record = { .stage = SCMI_SMT_DIAG_WAIT_FREE };
	uint32_t length = request->length, status, word;
	size_t i;
	int error;

	if (s->next_token == 0 || s->next_token > SMT_MAX_TOKEN)
		return EOVERFLOW;
	record.request_header = request->header | (uint32_t)s->next_token << 18;
	s->state = SMT_BUSY;
	error = scmi_smt_diag_wait(s, 1, &record);
	if (error != 0) {
		s->state = error == EBUSY ? SMT_READY : SMT_POISONED;
		*trace = record;
		return error;
	}

	/* Preserve the implementation-defined words at offsets8..15. */
	s->io.write_4(s->io.cookie, SMT_RESERVED, 0);
	s->io.write_4(s->io.cookie, SMT_FLAGS, 0);
	s->io.write_4(s->io.cookie, SMT_LENGTH, length);
	s->io.write_4(s->io.cookie, SMT_HEADER, record.request_header);
	if (length >= 8)
		s->io.write_4(s->io.cookie, SMT_PAYLOAD, request->arguments[0]);
	if (length == 12)
		s->io.write_4(s->io.cookie, SMT_PAYLOAD + 4, request->arguments[1]);
	s->io.barrier(s->io.cookie, SCMI_SMT_DIAG_PUBLISH);
	s->io.write_4(s->io.cookie, SMT_STATUS, 0);
	s->next_token++;
	s->io.barrier(s->io.cookie, SCMI_SMT_DIAG_SEND);
	s->io.write_4(s->io.cookie, SMT_DOORBELL, 1);

	record.stage = SCMI_SMT_DIAG_WAIT_RESPONSE;
	error = scmi_smt_diag_wait(s, 0, &record);
	if (error != 0)
		goto poison;
	s->io.barrier(s->io.cookie, SCMI_SMT_DIAG_RECEIVE);
	record.response_header = s->io.read_4(s->io.cookie, SMT_HEADER);
	record.response_length = s->io.read_4(s->io.cookie, SMT_LENGTH);
	/* Exact equality includes type, protocol, message, token and reserved bits. */
	if (record.response_header != record.request_header) {
		record.stage = SCMI_SMT_DIAG_HEADER;
		error = EPROTO;
		goto poison;
	}
	length = record.response_length;
	if (length < 8 || length > 104 || (length & 3) != 0) {
		record.stage = SCMI_SMT_DIAG_LENGTH;
		error = EPROTO;
		goto poison;
	}
	status = s->io.read_4(s->io.cookie, SMT_PAYLOAD);
	/* Decode two's complement without an out-of-range signed conversion. */
	record.firmware_status = status <= INT32_MAX ? (int32_t)status :
	    -(int32_t)(UINT32_MAX - status) - 1;
	record.firmware_status_valid = true;
	if (status != 0) {
		record.stage = SCMI_SMT_DIAG_FIRMWARE;
		s->state = SMT_READY;
		*trace = record;
		return EIO;
	}
	reply.data_length = length - 8;
	if (reply.data_length < request->minimum) {
		record.stage = SCMI_SMT_DIAG_LENGTH;
		error = EPROTO;
		goto poison;
	}
	for (i = 0; !request->status_only && i < reply.data_length; i += 4) {
		word = s->io.read_4(s->io.cookie, SMT_PAYLOAD + 4 + (uint32_t)i);
		reply.data[i] = (uint8_t)word;
		reply.data[i + 1] = (uint8_t)(word >> 8);
		reply.data[i + 2] = (uint8_t)(word >> 16);
		reply.data[i + 3] = (uint8_t)(word >> 24);
	}
	if (request->list &&
	    scmi_smt_diag_le32(reply.data) > reply.data_length - 4) {
		record.stage = SCMI_SMT_DIAG_LENGTH;
		error = EPROTO;
		goto poison;
	}
	record.stage = SCMI_SMT_DIAG_COMPLETE;
	s->state = SMT_READY;
	*out = reply;
	*trace = record;
	return 0;

poison:
	s->state = SMT_POISONED;
	*trace = record;
	return error;
}

int
scmi_smt_diag_query(struct scmi_smt_diag *s, enum scmi_smt_diag_cmd cmd,
    uint32_t argument, struct scmi_smt_diag_reply *out,
    struct scmi_smt_diag_trace *trace)
{
	struct scmi_smt_request request = { .arguments = { argument, 0 } };
	int error;

	error = scmi_smt_ready(s, out, trace);
	if (error != 0)
		return error;
	error = scmi_smt_diag_command(cmd, argument, &request.header,
	    &request.length, &request.minimum);
	if (error != 0)
		return error;
	request.list = cmd == SCMI_SMT_DIAG_BASE_LIST_PROTOCOLS;
	return scmi_smt_transfer(s, &request, out, trace);
}

int
scmi_smt_clock67_config_set(struct scmi_smt_diag *s, uint32_t enable,
    struct scmi_smt_diag_trace *trace)
{
	const struct scmi_smt_request request = {
		.header = 0x5007, .length = 12, .minimum = 0,
		.arguments = { 67, enable }, .status_only = true
	};
	struct scmi_smt_diag_reply reply = { 0 };
	int error;

	error = scmi_smt_ready(s, &reply, trace);
	if (error != 0)
		return error;
	if (enable > 1)
		return EINVAL;
	error = scmi_smt_transfer(s, &request, &reply, trace);
	if (error != 0)
		return error;
	/* Framing completed. A semantic failure does not poison this wire. */
	return reply.data_length == 0 ? 0 : EPROTO;
}
