/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD; written to the Linux
 * interface (the CONFIG_KUNIT=n side), no Linux code.  KUnit is not part
 * of the kernel: no test is ever running.
 */
#ifndef _KUNIT_TEST_BUG_H_
#define _KUNIT_TEST_BUG_H_

struct kunit;

#define	kunit_fail_current_test(fmt, ...)	do {} while (0)

static inline struct kunit *
kunit_get_current_test(void)
{

	return NULL;
}

#endif
