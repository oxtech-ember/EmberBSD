/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _DEV_I2C_OV3640_LOGIC_H_
#define _DEV_I2C_OV3640_LOGIC_H_

#ifdef _KERNEL
#include <sys/types.h>
#else
#include <stdint.h>
#endif

struct ov3640_sequence_ops {
	void *cookie;
	int (*set_reset)(void *, int asserted);
	int (*set_powerdown)(void *, int asserted);
	void (*delay_us)(void *, unsigned int);
	int (*read_reg)(void *, uint16_t, uint8_t *);
};

int ov3640_identify(const struct ov3640_sequence_ops *, uint16_t *);

#endif /* _DEV_I2C_OV3640_LOGIC_H_ */
