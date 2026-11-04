/* Origin: EmberBSD Goodix FDT attachment interface. */
/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _DEV_I2C_GT9XX_FDT_H_
#define _DEV_I2C_GT9XX_FDT_H_

#include <sys/device.h>
#include <dev/i2c/i2cvar.h>

int gt9xx_fdt_match(struct i2c_attach_args *, cfdata_t);
int gt9xx_fdt_prepare(struct i2c_attach_args *);

#endif /* _DEV_I2C_GT9XX_FDT_H_ */
