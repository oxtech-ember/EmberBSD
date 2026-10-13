/* Origin: EmberBSD - error-checked RTL8723DS SDIO function lifecycle. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 Anton and EmberBSD contributors. */

#ifdef _KERNEL
#include <sys/errno.h>
#include <sys/systm.h>
#else
#include <errno.h>
#include <string.h>
#endif
#include "rtl8723ds_function.h"

bool
rtl8723ds_function_write_allowed(bool opted_in, uint32_t argument)
{
	/* Function zero IOEx only; exclude RAW and every reserved argument bit. */
	return opted_in && (argument & ~0xffU) == 0x80000400U;
}

/* Only function-zero IOEx/IORx; no chip, PMIC, interrupt or firmware writes. */
static int
rtl8723ds_function_command(const struct rtl8723ds_function_ops *ops,
    unsigned int reg, bool write, uint8_t *value)
{
	uint32_t argument, response;
	int error;

	argument = reg << 9;
	if (write)
		argument |= 0x80000000U | *value;
	error = ops->command(ops->cookie, argument, &response);
	if (error != 0)
		return error;
	if ((response & 0x8000) != 0)
		return EILSEQ;
	if ((response & 0x4b00) != 0)
		return EIO;
	if (!write)
		*value = response & 0xff;
	return 0;
}

int
rtl8723ds_function_enable(const struct rtl8723ds_function_ops *ops,
    struct rtl8723ds_function_state *state)
{
	uint8_t value, want;
	int error;

	if (ops == NULL || ops->command == NULL || ops->wait_ms == NULL ||
	    state == NULL)
		return EINVAL;
	memset(state, 0, sizeof(*state));
	error = rtl8723ds_function_command(ops, 2, false, &state->original);
	if (error != 0)
		return error;
	/* Do not claim or disable an already enabled function. */
	if ((state->original & 2) != 0)
		return EBUSY;
	want = state->original | 2;
	value = want;
	/* A timed-out write may still have reached the card. */
	state->changed = true;
	error = rtl8723ds_function_command(ops, 2, true, &value);
	if (error != 0)
		return error;
	error = rtl8723ds_function_command(ops, 2, false, &value);
	if (error != 0)
		return error;
	if (value != want)
		return EIO;
	for (unsigned int attempt = 0; attempt <= 100; attempt++) {
		error = rtl8723ds_function_command(ops, 3, false, &state->ready);
		if (error != 0)
			return error;
		if ((state->ready & 2) != 0)
			return 0;
		if (attempt < 100)
			ops->wait_ms(ops->cookie, 10);
	}
	return ETIMEDOUT;
}

int
rtl8723ds_function_restore(const struct rtl8723ds_function_ops *ops,
    struct rtl8723ds_function_state *state)
{
	uint8_t value;
	int error;

	if (ops == NULL || ops->command == NULL || state == NULL)
		return EINVAL;
	if (!state->changed)
		return 0;
	value = state->original;
	error = rtl8723ds_function_command(ops, 2, true, &value);
	if (error != 0)
		return error;
	error = rtl8723ds_function_command(ops, 2, false, &value);
	if (error != 0)
		return error;
	if (value != state->original)
		return EIO;
	state->changed = false;
	return 0;
}
