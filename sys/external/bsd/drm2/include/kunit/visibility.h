/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD; written to the Linux
 * interface (the CONFIG_KUNIT=n side), no Linux code.
 */
#ifndef _KUNIT_VISIBILITY_H_
#define _KUNIT_VISIBILITY_H_

#define	VISIBLE_IF_KUNIT	static
#define	EXPORT_SYMBOL_IF_KUNIT(symbol)
#define	EXPORT_SYMBOL_FOR_KUNIT_TESTING(symbol)

#endif
