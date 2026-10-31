/*
 * Copyright (c) 2026 The EmberBSD Project.
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Pinmux cell layout interoperates with Zephyr's ESP32_PINMUX encoding:
 * zephyrproject-rtos/zephyr commit 0534e997eacb94bae1df6c9a7dd4a9a6cbf1cecf,
 * include/zephyr/dt-bindings/pinctrl/esp-pinctrl-common.h (Apache-2.0).
 * Signal indices are from ESP-IDF v6.1 gpio_sig_map.h (Apache-2.0 OR MIT).
 */
#ifndef _DT_BINDINGS_PINCTRL_ESP32S31_H_
#define _DT_BINDINGS_PINCTRL_ESP32S31_H_

#define ESP32S31_SIG_NONE 0x1ff
#define ESP32S31_GPIO_OUT 256
#define ESP32S31_PINMUX(pin, in, out) \
	(((pin) & 0x3f) | (((in) & 0x1ff) << 6) | (((out) & 0x1ff) << 15))

#define ESP32S31_SPI2_CLK 53
#define ESP32S31_SPI2_Q 54
#define ESP32S31_SPI2_D 55
#define ESP32S31_SPI2_HD 56
#define ESP32S31_SPI2_WP 57
#define ESP32S31_SPI2_CS 62

#endif
