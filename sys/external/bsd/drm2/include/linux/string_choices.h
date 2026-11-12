/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD native implementation of the interface of Linux 6.18
 * include/linux/string_choices.h (names and returned texts only; no Linux
 * source is copied, the Linux file is GPL-2.0).
 */

#ifndef _LINUX_STRING_CHOICES_H_
#define _LINUX_STRING_CHOICES_H_

#include <linux/types.h>

static inline const char *
str_assert_deassert(bool v)
{

	return v ? "assert" : "deassert";
}

static inline const char *
str_deassert_assert(bool v)
{

	return v ? "deassert" : "assert";
}

static inline const char *
str_enable_disable(bool v)
{

	return v ? "enable" : "disable";
}

static inline const char *
str_disable_enable(bool v)
{

	return v ? "disable" : "enable";
}

static inline const char *
str_enabled_disabled(bool v)
{

	return v ? "enabled" : "disabled";
}

static inline const char *
str_disabled_enabled(bool v)
{

	return v ? "disabled" : "enabled";
}

static inline const char *
str_hi_lo(bool v)
{

	return v ? "hi" : "lo";
}

static inline const char *
str_lo_hi(bool v)
{

	return v ? "lo" : "hi";
}

static inline const char *
str_high_low(bool v)
{

	return v ? "high" : "low";
}

static inline const char *
str_low_high(bool v)
{

	return v ? "low" : "high";
}

static inline const char *
str_input_output(bool v)
{

	return v ? "input" : "output";
}

static inline const char *
str_output_input(bool v)
{

	return v ? "output" : "input";
}

static inline const char *
str_on_off(bool v)
{

	return v ? "on" : "off";
}

static inline const char *
str_off_on(bool v)
{

	return v ? "off" : "on";
}

static inline const char *
str_yes_no(bool v)
{

	return v ? "yes" : "no";
}

static inline const char *
str_no_yes(bool v)
{

	return v ? "no" : "yes";
}

static inline const char *
str_true_false(bool v)
{

	return v ? "true" : "false";
}

static inline const char *
str_false_true(bool v)
{

	return v ? "false" : "true";
}

static inline const char *
str_up_down(bool v)
{

	return v ? "up" : "down";
}

static inline const char *
str_down_up(bool v)
{

	return v ? "down" : "up";
}

static inline const char *
str_read_write(bool v)
{

	return v ? "read" : "write";
}

static inline const char *
str_write_read(bool v)
{

	return v ? "write" : "read";
}

static inline const char *
str_plural(size_t n)
{

	return n == 1 ? "" : "s";
}

#endif	/* _LINUX_STRING_CHOICES_H_ */
