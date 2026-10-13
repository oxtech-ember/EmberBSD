/* Origin: EmberBSD - error-checked RTL8723DS SDIO function lifecycle. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 Anton and EmberBSD contributors. */
#ifndef _RTL8723DS_FUNCTION_H_
#define _RTL8723DS_FUNCTION_H_

#include <sys/types.h>
#ifndef _KERNEL
#include <stdbool.h>
#include <stdint.h>
#endif

struct rtl8723ds_function_ops {
	int (*command)(void *, uint32_t, uint32_t *);
	void (*wait_ms)(void *, unsigned int);
	void *cookie;
};

struct rtl8723ds_function_state {
	uint8_t original, ready;
	bool changed;
};

bool rtl8723ds_function_write_allowed(bool, uint32_t);
int rtl8723ds_function_enable(const struct rtl8723ds_function_ops *,
    struct rtl8723ds_function_state *);
int rtl8723ds_function_restore(const struct rtl8723ds_function_ops *,
    struct rtl8723ds_function_state *);

#endif
