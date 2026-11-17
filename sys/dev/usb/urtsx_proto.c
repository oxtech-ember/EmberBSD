/* Origin: EmberBSD Realtek USB wire-format helpers (AI-assisted). */
/* SPDX-License-Identifier: BSD-2-Clause */
#ifdef _KERNEL
#include <sys/types.h>
#include <sys/errno.h>
#include <sys/null.h>
#else
#include <errno.h>
#endif

#include "urtsx_proto.h"
#include "urtsxreg.h"

/* These packets encode the USB controller ABI, not a SCSI command block. */
int
urtsx_cmd_init(struct urtsx_cmd *packet, uint8_t *buffer, size_t capacity)
{
	if (packet == NULL || buffer == NULL || capacity < URTSX_CMD_HEADER ||
	    capacity > URTSX_CMD_SIZE)
		return EINVAL;
	packet->buffer = buffer;
	packet->capacity = capacity;
	packet->count = 0;
	packet->responses = 0;
	buffer[0] = 'R';
	buffer[1] = 'T';
	buffer[2] = 'C';
	buffer[3] = 'R';
	for (size_t i = 4; i < URTSX_CMD_HEADER; i++)
		buffer[i] = 0;
	return 0;
}

int
urtsx_cmd_add(struct urtsx_cmd *packet, uint8_t type, uint16_t address,
    uint8_t mask, uint8_t value)
{
	uint8_t *command;

	if (packet == NULL || packet->buffer == NULL ||
	    packet->capacity < URTSX_CMD_HEADER ||
	    packet->capacity > URTSX_CMD_SIZE || type > 2)
		return EINVAL;
	if (packet->count >= (packet->capacity - URTSX_CMD_HEADER) / 4)
		return ENOSPC;
	command = packet->buffer + URTSX_CMD_HEADER + packet->count * 4;
	command[0] = (uint8_t)(type << 6) | (uint8_t)((address >> 8) & 0x3f);
	command[1] = (uint8_t)address;
	command[2] = mask;
	command[3] = value;
	packet->count++;
	if (type != 1)
		packet->responses++;
	packet->buffer[5] = (uint8_t)(packet->count >> 8);
	packet->buffer[6] = (uint8_t)packet->count;
	return 0;
}

int
urtsx_cmd_finish(struct urtsx_cmd *packet, uint8_t stage, size_t *length)
{
	if (packet == NULL || packet->buffer == NULL || length == NULL ||
	    packet->capacity < URTSX_CMD_HEADER ||
	    packet->capacity > URTSX_CMD_SIZE || packet->count == 0 ||
	    packet->count > (packet->capacity - URTSX_CMD_HEADER) / 4 ||
	    packet->responses > packet->count)
		return EINVAL;
	switch (stage) {
	case URTSX_STAGE_C:
	case URTSX_STAGE_CR:
	case URTSX_STAGE_CDIR:
	case URTSX_STAGE_CDOR:
		break;
	default:
		return EINVAL;
	}
	/* A response stage exists only for read/check commands. */
	stage = (stage & ~1U) | (packet->responses != 0);
	packet->buffer[7] = stage;
	*length = URTSX_CMD_HEADER + packet->count * 4;
	return 0;
}

int
urtsx_cmd_response_size(const struct urtsx_cmd *packet, size_t *length)
{
	if (packet == NULL || length == NULL || packet->count > 254 ||
	    packet->responses > packet->count)
		return EINVAL;
	*length = (packet->responses + 3) & ~(size_t)3;
	return 0;
}

int
urtsx_status_decode(const uint8_t *data, size_t length, uint16_t *status)
{
	if (data == NULL || status == NULL || length != 2)
		return EINVAL;
	*status = (uint16_t)data[0] | (uint16_t)data[1] << 8;
	return 0;
}

int
urtsx_bulk_status_decode(const uint8_t *data, size_t length, uint16_t *status)
{
	if (data == NULL || status == NULL || length != 2)
		return EINVAL;
	*status = ((data[0] >> 2) & 0x0f) | ((data[1] & 3) << 4);
	return 0;
}

uint16_t
urtsx_ep0_read_value(uint16_t address)
{
	uint16_t value = address | 0x8000;

	return (uint16_t)(value << 8) | (uint16_t)(value >> 8);
}

/* Encode the controller's SD host registers. The sdmmc bus owns SD policy. */
int
urtsx_sd_command(struct urtsx_cmd *packet, uint8_t opcode, uint32_t argument,
    uint8_t cfg2, size_t data_length)
{
	size_t response_length, needed;
	int error;

	if (opcode > 63 || data_length > 512)
		return EINVAL;
	response_length = (cfg2 & 3) == 2 ? 15 : (cfg2 & 3) == 1 ? 5 : 0;
	needed = 11 + response_length + (data_length != 0 ? 4 : 0);
	if (packet == NULL || packet->buffer == NULL ||
	    packet->capacity < URTSX_CMD_HEADER ||
	    packet->capacity > URTSX_CMD_SIZE ||
	    packet->count > (packet->capacity - URTSX_CMD_HEADER) / 4 ||
	    needed > (packet->capacity - URTSX_CMD_HEADER) / 4 - packet->count)
		return ENOSPC;
#define ADD(t, a, m, v) do { \
	error = urtsx_cmd_add(packet, (t), (a), (m), (v)); \
	if (error) return error; \
} while (0)
	ADD(1, URTSX_SD_CMD0, 0xff, 0x40 | opcode);
	for (unsigned i = 0; i < 4; i++)
		ADD(1, (URTSX_SD_CMD0 + 1) + i, 0xff, argument >> (24 - 8 * i));
	if (data_length != 0) {
		ADD(1, URTSX_SD_BYTE_COUNT_L, 0xff, data_length);
		ADD(1, URTSX_SD_BYTE_COUNT_H, 0xff, data_length >> 8);
		ADD(1, URTSX_SD_BLOCK_COUNT_L, 0xff, 1);
		ADD(1, URTSX_SD_BLOCK_COUNT_H, 0xff, 0);
	}
	ADD(1, URTSX_SD_CFG2, 0xff, cfg2);
	ADD(1, URTSX_CARD_DATA_SOURCE, 1, 1);
	ADD(1, URTSX_SD_TRANSFER, 0xff, data_length != 0 ? 0x8c : 0x88);
	ADD(2, URTSX_SD_TRANSFER, data_length != 0 ? 0x40 : 0x60,
	    data_length != 0 ? 0x40 : 0x60);
	if (response_length == 15) {
		/* sdmmc omits the CRC byte and stores the low word first. */
		for (unsigned i = 15; i != 0; i--)
			ADD(0, URTSX_PPBUF + i, 0, 0);
	} else {
		for (unsigned i = 0; i < response_length; i++)
			ADD(0, URTSX_SD_CMD0 + i, 0, 0);
	}
	ADD(0, URTSX_SD_STATUS1, 0, 0);
	ADD(0, URTSX_SD_STATUS2, 0, 0);
#undef ADD
	return 0;
}

int
urtsx_sd_response(const uint8_t *data, size_t length, uint8_t cfg2,
    uint32_t response[4])
{
	size_t count = (cfg2 & 3) == 2 ? 15 : (cfg2 & 3) == 1 ? 5 : 0;
	size_t expected = (1 + count + 2 + 3) & ~(size_t)3;

	if (data == NULL || response == NULL || length != expected)
		return EINVAL;
	if (data[2 + count] & 1)
		return ETIMEDOUT;
	if ((data[0] & 0x50) != 0x40 ||
	    (data[1 + count] & 0x40) ||
	    (!(cfg2 & 4) && (data[1 + count] & 0x80)))
		return EIO;
	for (unsigned i = 0; i < 4; i++)
		response[i] = 0;
	if (count == 15) {
		for (unsigned i = 0; i < 15; i++)
			response[i / 4] |= (uint32_t)data[1 + i] << (8 * (i % 4));
	} else if (count == 5) {
		for (unsigned i = 0; i < 4; i++)
			response[0] = (response[0] << 8) | data[2 + i];
	}
	return 0;
}

int
urtsx_clock_params(unsigned frequency, struct urtsx_clock *clock)
{
	unsigned mhz, n, divider, mcus, depth = 2;

	if (frequency == 0 || frequency > 25000 || clock == NULL)
		return EINVAL;
	/* The hardware uses 30 MHz / 128 during card identification. */
	mhz = frequency <= 1000 ? 30 : (frequency / 1000) * 2;
	if (mhz <= 2)
		return EINVAL;
	n = mhz - 2;
	for (divider = 0; n < 60 && divider < 2; divider++)
		n = (n + 2) * 2 - 2;
	if (n < 60 || n > 120)
		return EINVAL;
	mcus = 60 / mhz + 3;
	if (divider > 1)
		depth -= divider - 1;
	clock->sd_div = frequency <= 1000 ? 0x80 : 0;
	clock->clk_div = (uint8_t)(divider << 4 | mcus);
	clock->n = (uint8_t)n;
	clock->depth = (uint8_t)depth;
	return 0;
}

/* Capabilities of the initial SD-memory adapter, including no-data commands. */
int
urtsx_sd_readonly_opcode(unsigned opcode)
{
	switch (opcode) {
	case 0:  /* reset protocol state */
	case 2:  /* CID */
	case 3:  /* RCA */
	case 7:  /* select card */
	case 8:  /* interface condition */
	case 9:  /* CSD */
	case 10: /* CID */
	case 12: /* stop transfer */
	case 13: /* card status or application SD status */
	case 16: /* volatile block length */
	case 17: /* read one block */
	case 41: /* application operating conditions */
	case 51: /* application SCR */
	case 55: /* application command prefix */
		return 1;
	default:
		return 0;
	}
}

/* Source and destination are separate buffers. This only fills controller RAM. */
int
urtsx_ppbuf_write_packet(uint8_t *buffer, size_t capacity, const uint8_t *data,
    size_t length, size_t *packet_length)
{
	if (buffer == NULL || data == NULL || packet_length == NULL ||
	    length == 0 || length > 512 || capacity > URTSX_CMD_SIZE ||
	    capacity < 12 + length)
		return EINVAL;
	buffer[0] = 'R'; buffer[1] = 'T'; buffer[2] = 'C'; buffer[3] = 'R';
	buffer[4] = 2; /* Sequential register write. */
	buffer[5] = (uint8_t)(length >> 8);
	buffer[6] = (uint8_t)length;
	buffer[7] = URTSX_STAGE_C;
	buffer[8] = URTSX_PPBUF >> 8;
	buffer[9] = URTSX_PPBUF & 0xff;
	buffer[10] = buffer[11] = 0;
	for (size_t i = 0; i < length; i++)
		buffer[12 + i] = data[i];
	*packet_length = 12 + length;
	return 0;
}

/* The preceding command phase has already sent CMD24 and accepted its R1. */
int
urtsx_sd_write_data(struct urtsx_cmd *packet)
{
	int error;

	if (packet == NULL || packet->buffer == NULL ||
	    packet->capacity < URTSX_CMD_HEADER ||
	    packet->capacity > URTSX_CMD_SIZE ||
	    packet->count > (packet->capacity - URTSX_CMD_HEADER) / 4 ||
	    11 > (packet->capacity - URTSX_CMD_HEADER) / 4 - packet->count)
		return ENOSPC;
#define ADD(t, a, m, v) do { \
	error = urtsx_cmd_add(packet, (t), (a), (m), (v)); \
	if (error) return error; \
} while (0)
	ADD(1, URTSX_SD_BYTE_COUNT_L, 0xff, 0);
	ADD(1, URTSX_SD_BYTE_COUNT_H, 0xff, 2);
	ADD(1, URTSX_SD_BLOCK_COUNT_L, 0xff, 1);
	ADD(1, URTSX_SD_BLOCK_COUNT_H, 0xff, 0);
	ADD(1, URTSX_CARD_DATA_SOURCE, 1, 1);
	ADD(1, URTSX_SD_CFG2, 0xff, 0);
	ADD(1, URTSX_SD_TRANSFER, 0xff, 0x81);
	ADD(2, URTSX_SD_TRANSFER, 0x40, 0x40);
	/* Wait for the card's physical busy line before reporting completion. */
	ADD(2, URTSX_SD_BUS_STATUS, 2, 2);
	ADD(0, URTSX_SD_STATUS1, 0, 0);
	ADD(0, URTSX_SD_STATUS2, 0, 0);
#undef ADD
	return 0;
}

int
urtsx_sd_write_status(const uint8_t *data, size_t length)
{
	if (data == NULL || length != 4)
		return EINVAL;
	if ((data[0] & 0x50) != 0x40 || (data[2] & 0xe0))
		return EIO;
	if (!(data[1] & 2) || (data[2] & 2) || (data[3] & 1))
		return ETIMEDOUT;
	return 0;
}
