/*
 * SPDX-License-Identifier: BSD-2-Clause AND Apache-2.0
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SC101IOT page-select and identification sequence adapted from Espressif
 * esp_cam_sensor v2.4.0, esp-video-components commit
 * 8fc931636ffa937c89cab53b53f4ce380190c1a4 (Apache-2.0).
 */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#ifdef _KERNEL
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/types.h>
#else
#include <errno.h>
#include <stddef.h>
#endif

#include "sc101iot_logic.h"

#define SC101IOT_XCLK_SETTLE_US	20000
#define SC101IOT_PWDN_SETTLE_US	10000
#define SC101IOT_RESET_SETTLE_US	10000
#define SC101IOT_REG_PIDH	0x31f7
#define SC101IOT_REG_PIDL	0x31f8
#define SC101IOT_PID		0xda4a

static int
sc101iot_park(const struct sc101iot_sequence_ops *ops)
{
	int error, first_error = 0;

	/* Keep reset asserted before placing the sensor in power-down. */
	error = ops->set_reset(ops->cookie, 1);
	if (error != 0)
		first_error = error;
	error = ops->set_powerdown(ops->cookie, 1);
	if (error != 0 && first_error == 0)
		first_error = error;
	ops->delay_us(ops->cookie, SC101IOT_RESET_SETTLE_US);

	return first_error;
}

int
sc101iot_identify(const struct sc101iot_sequence_ops *ops, uint16_t *pid)
{
	uint8_t pidh, pidl;
	int error, park_error;

	if (ops == NULL || pid == NULL || ops->set_reset == NULL ||
	    ops->set_powerdown == NULL || ops->delay_us == NULL ||
	    ops->read_reg == NULL)
		return EINVAL;

	*pid = 0;

	/* XCLK is provided by the module and is not controlled or rate-checked. */
	error = ops->set_reset(ops->cookie, 1);
	if (error != 0)
		goto fail;
	error = ops->set_powerdown(ops->cookie, 1);
	if (error != 0)
		goto fail;
	ops->delay_us(ops->cookie, SC101IOT_XCLK_SETTLE_US);
	ops->delay_us(ops->cookie, SC101IOT_PWDN_SETTLE_US);

	/* Espressif sequence: release PWDN, pulse reset, then access SCCB. */
	error = ops->set_powerdown(ops->cookie, 0);
	if (error != 0)
		goto fail;
	ops->delay_us(ops->cookie, SC101IOT_PWDN_SETTLE_US);
	error = ops->set_reset(ops->cookie, 1);
	if (error != 0)
		goto fail;
	ops->delay_us(ops->cookie, SC101IOT_RESET_SETTLE_US);
	error = ops->set_reset(ops->cookie, 0);
	if (error != 0)
		goto fail;
	ops->delay_us(ops->cookie, SC101IOT_RESET_SETTLE_US);

	/* Each read selects page 0x31 before accessing register 0xf7 or 0xf8. */
	error = ops->read_reg(ops->cookie, SC101IOT_REG_PIDH, &pidh);
	if (error != 0)
		goto fail;
	error = ops->read_reg(ops->cookie, SC101IOT_REG_PIDL, &pidl);
	if (error != 0)
		goto fail;
	*pid = ((uint16_t)pidh << 8) | pidl;
	if (*pid != SC101IOT_PID) {
		error = ENODEV;
		goto fail;
	}
	error = 0;

fail:
	park_error = sc101iot_park(ops);
	if (error == 0)
		error = park_error;
	return error;
}
