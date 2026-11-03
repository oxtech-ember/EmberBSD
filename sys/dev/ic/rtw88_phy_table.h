/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: Linux adc218676eef25575469234709c2d87185ca223a rtw88/phy.c;
 * native validation and fallible callbacks are AI-assisted. */
#ifndef _DEV_IC_RTW88_PHY_TABLE_H_
#define _DEV_IC_RTW88_PHY_TABLE_H_

#ifdef _KERNEL
#include <sys/types.h>
#else
#include <stddef.h>
#include <stdint.h>
#endif

struct rtw88_phy_table_ops {
	/* Match the complete positive condition word. Return a boolean in out. */
	int (*match)(void *, uint32_t, int *);
	int (*write)(void *, uint32_t, uint32_t);
	/* Optional owner/lifetime check; return an errno to stop the walk. */
	int (*check)(void *);
};

/* Count is in uint32_t words. Tables contain address/data pairs and flat
 * IF/ELIF/ELSE/ENDIF groups. Each IF/ELIF has a negative-marker pair.
 * The pinned format ignores the second word of condition-marker pairs.
 * Validation performs no callbacks and rejects malformed structure. */
int rtw88_phy_table_validate(const uint32_t *, size_t);

/* Validate the whole structure before any callbacks. Match and write are
 * required. Stop at the first callback error. No locks or queues are taken;
 * the caller owns serialization and hardware unwind after partial writes. */
int rtw88_phy_table_apply(const uint32_t *, size_t,
    const struct rtw88_phy_table_ops *, void *);

#endif /* _DEV_IC_RTW88_PHY_TABLE_H_ */
