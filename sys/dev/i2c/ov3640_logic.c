/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: BSD-2-Clause AND Apache-2.0
 *
 * OV3640 power-up timing and accepted PIDs adapted from Espressif
 * esp_cam_sensor v2.4.0 (Apache-2.0), repository commit
 * 8fc931636ffa937c89cab53b53f4ce380190c1a4. Registry archive SHA256:
 * b345bc4361b8106f05fe4c56ceb907600555b3f4de98e1983ec365733c45b1a6.
 */
#ifdef _KERNEL
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");
#endif

#include <sys/errno.h>
#include <sys/types.h>

#include "ov3640_logic.h"

#define OV3640_XCLK_SETTLE_US	20000
#define OV3640_RESET_SETTLE_US	10000
#define OV3640_REG_PIDH		0x300a
#define OV3640_REG_PIDL		0x300b
#define OV3640_PID_1		0x364c
#define OV3640_PID_2		0x3641

static int
ov3640_park(const struct ov3640_sequence_ops *ops)
{
	int error, first_error = 0;

	/* Reset is active low.  Assert it before putting the sensor to sleep. */
	error = ops->set_reset(ops->cookie, 1);
	if (error != 0)
		first_error = error;
	error = ops->set_powerdown(ops->cookie, 1);
	if (error != 0 && first_error == 0)
		first_error = error;
	ops->delay_us(ops->cookie, OV3640_RESET_SETTLE_US);

	return first_error;
}

int
ov3640_identify(const struct ov3640_sequence_ops *ops, uint16_t *pid)
{
	uint8_t pidh, pidl;
	int error, park_error;

	if (!ops || !pid || !ops->set_reset || !ops->set_powerdown ||
	    !ops->delay_us || !ops->read_reg)
		return EINVAL;

	*pid = 0;

	/* Hold reset before releasing PWDN; XCLK is an always-on module clock. */
	error = ops->set_reset(ops->cookie, 1);
	if (error != 0)
		goto fail;
	error = ops->set_powerdown(ops->cookie, 1);
	if (error != 0)
		goto fail;
	ops->delay_us(ops->cookie, OV3640_XCLK_SETTLE_US);
	ops->delay_us(ops->cookie, OV3640_RESET_SETTLE_US);

	error = ops->set_powerdown(ops->cookie, 0);
	if (error != 0)
		goto fail;
	ops->delay_us(ops->cookie, OV3640_RESET_SETTLE_US);
	error = ops->set_reset(ops->cookie, 0);
	if (error != 0)
		goto fail;
	ops->delay_us(ops->cookie, OV3640_RESET_SETTLE_US);

	error = ops->read_reg(ops->cookie, OV3640_REG_PIDH, &pidh);
	if (error != 0)
		goto fail;
	error = ops->read_reg(ops->cookie, OV3640_REG_PIDL, &pidl);
	if (error != 0)
		goto fail;
	*pid = ((uint16_t)pidh << 8) | pidl;
	if (*pid != OV3640_PID_1 && *pid != OV3640_PID_2) {
		error = ENODEV;
		goto fail;
	}

	error = 0;

fail:
	park_error = ov3640_park(ops);
	if (error == 0)
		error = park_error;
	return error;
}
