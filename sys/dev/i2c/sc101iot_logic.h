/* SPDX-License-Identifier: BSD-2-Clause AND Apache-2.0 */

#ifndef _DEV_I2C_SC101IOT_LOGIC_H_
#define _DEV_I2C_SC101IOT_LOGIC_H_

#ifdef _KERNEL
#include <sys/types.h>
#else
#include <stdint.h>
#endif

struct sc101iot_sequence_ops {
	void *cookie;
	int (*set_reset)(void *, int);
	int (*set_powerdown)(void *, int);
	void (*delay_us)(void *, unsigned int);
	int (*read_reg)(void *, uint16_t, uint8_t *);
};

int sc101iot_identify(const struct sc101iot_sequence_ops *, uint16_t *);

#endif /* _DEV_I2C_SC101IOT_LOGIC_H_ */
