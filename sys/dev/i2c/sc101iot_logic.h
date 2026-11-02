/* SPDX-License-Identifier: BSD-2-Clause AND Apache-2.0 */

#ifndef _DEV_I2C_SC101IOT_LOGIC_H_
#define _DEV_I2C_SC101IOT_LOGIC_H_

#ifdef _KERNEL
#include <sys/types.h>
#else
#include <stddef.h>
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
int sc101iot_power_on(const struct sc101iot_sequence_ops *);
int sc101iot_mode_layout(uint32_t, uint32_t, size_t *, size_t *);

#endif /* _DEV_I2C_SC101IOT_LOGIC_H_ */
