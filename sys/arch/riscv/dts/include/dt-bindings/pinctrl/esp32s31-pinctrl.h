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
#define ESP32S31_I2S0_BCK 25
#define ESP32S31_I2S0_MCLK 26
#define ESP32S31_I2S0_WS 27
#define ESP32S31_I2S0_SD 28

#define ESP32S31_SPI3_CLK 47
#define ESP32S31_SPI3_Q 48
#define ESP32S31_SPI3_D 49
#define ESP32S31_SPI3_HOLD 50
#define ESP32S31_SPI3_WP 51
#define ESP32S31_SPI3_CS 52

/* ESP-IDF v6.1 ESP32-S31 camera input routes. */
#define ESP32S31_CAM_PCLK 158
#define ESP32S31_CAM_DE 159
#define ESP32S31_CAM_VSYNC 161
#define ESP32S31_CAM_DATA0 162
#define ESP32S31_CAM_DATA1 163
#define ESP32S31_CAM_DATA2 164
#define ESP32S31_CAM_DATA3 165
#define ESP32S31_CAM_DATA4 166
#define ESP32S31_CAM_DATA5 167
#define ESP32S31_CAM_DATA6 168
#define ESP32S31_CAM_DATA7 169

#endif
