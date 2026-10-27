/* Origin: EmberBSD diagnostic wrapper for the common SDIO lifecycle. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 Anton and EmberBSD contributors. */
#ifdef _KERNEL
#include <sys/errno.h>
#include <sys/null.h>
#else
#include <errno.h>
#endif
#include "rtl8723ds_function.h"
#include "rtw88_sdio_io.h"

bool
rtl8723ds_function_write_allowed(bool opted_in, uint32_t argument)
{
	/* Function zero IOEx only; exclude RAW and every reserved argument bit. */
	return opted_in && (argument & ~0xffU) == 0x80000400U;
}

int
rtl8723ds_function_enable(const struct rtl8723ds_function_ops *ops,
    struct rtl8723ds_function_state *state)
{
	struct rtw88_sdio_ops common = { 0 };
	struct rtw88_sdio_bus bus = { .ops = &common };
	struct rtw88_sdio_function_state native = { 0 };
	int error;

	if (ops == NULL || ops->command == NULL || ops->wait_ms == NULL ||
	    state == NULL)
		return EINVAL;
	common.command52 = ops->command;
	common.wait_ms = ops->wait_ms;
	common.cookie = ops->cookie;
	error = rtw88_sdio_function_acquire(&bus, &native, false);
	state->original = native.original;
	state->ready = native.ready;
	state->changed = native.changed;
	return error;
}

int
rtl8723ds_function_restore(const struct rtl8723ds_function_ops *ops,
    struct rtl8723ds_function_state *state)
{
	struct rtw88_sdio_ops common = { 0 };
	struct rtw88_sdio_bus bus = { .ops = &common, .function_claimed = true };
	struct rtw88_sdio_function_state native;
	int error;

	if (ops == NULL || ops->command == NULL || state == NULL)
		return EINVAL;
	if (!state->changed)
		return 0;
	common.command52 = ops->command;
	common.cookie = ops->cookie;
	native.original = state->original;
	native.ready = 0;
	native.changed = native.owns_enable = true;
	error = rtw88_sdio_function_release(&bus, &native,
	    RTW88_SDIO_RESTORE_DIAGNOSTIC_SNAPSHOT);
	state->changed = native.changed;
	return error;
}
