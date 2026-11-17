/* Origin: EmberBSD Realtek USB wire-format helpers (AI-assisted). */
/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _DEV_USB_URTSX_PROTO_H_
#define _DEV_USB_URTSX_PROTO_H_

#ifdef _KERNEL
#include <sys/types.h>
#else
#include <stddef.h>
#include <stdint.h>
#endif

#define URTSX_CMD_SIZE 1024
#define URTSX_CMD_HEADER 8
#define URTSX_STAGE_C 0
#define URTSX_STAGE_CR 1
#define URTSX_STAGE_CDIR 3
#define URTSX_STAGE_CDOR 5

struct urtsx_cmd {
	uint8_t *buffer;
	size_t capacity;
	size_t count;
	size_t responses;
};

int urtsx_cmd_init(struct urtsx_cmd *, uint8_t *, size_t);
int urtsx_cmd_add(struct urtsx_cmd *, uint8_t, uint16_t, uint8_t, uint8_t);
int urtsx_cmd_finish(struct urtsx_cmd *, uint8_t, size_t *);
int urtsx_cmd_response_size(const struct urtsx_cmd *, size_t *);
int urtsx_status_decode(const uint8_t *, size_t, uint16_t *);
int urtsx_bulk_status_decode(const uint8_t *, size_t, uint16_t *);
uint16_t urtsx_ep0_read_value(uint16_t);
int urtsx_sd_command(struct urtsx_cmd *, uint8_t, uint32_t, uint8_t, size_t);
int urtsx_sd_response(const uint8_t *, size_t, uint8_t, uint32_t[4]);
struct urtsx_clock {
	uint8_t sd_div, clk_div, n, depth;
};
int urtsx_clock_params(unsigned, struct urtsx_clock *);
int urtsx_sd_readonly_opcode(unsigned);
int urtsx_ppbuf_write_packet(uint8_t *, size_t, const uint8_t *, size_t, size_t *);
int urtsx_sd_write_data(struct urtsx_cmd *);
int urtsx_sd_write_status(const uint8_t *, size_t);

#endif /* _DEV_USB_URTSX_PROTO_H_ */
