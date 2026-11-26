/* SPDX-License-Identifier: BSD-2-Clause */

#ifndef _ARM_ROCKCHIP_RK_GPIOVAR_H_
#define _ARM_ROCKCHIP_RK_GPIOVAR_H_

#include <sys/device.h>

int	rk_gpio_config_output(device_t, u_int, int, bool);

#endif /* _ARM_ROCKCHIP_RK_GPIOVAR_H_ */
