/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: Linux adc218676eef25575469234709c2d87185ca223a rtw88/phy.c;
 * native validation and fallible callbacks are AI-assisted. */
#include <dev/ic/rtw88_phy_table.h>
#ifdef _KERNEL
#include <sys/errno.h>
#include <sys/null.h>
#else
#include <errno.h>
#endif

#define RTW88_PHY_POS 0x80000000U
#define RTW88_PHY_NEG 0x40000000U
#define RTW88_PHY_IF 0U
#define RTW88_PHY_ELIF 1U
#define RTW88_PHY_ELSE 2U
#define RTW88_PHY_ENDIF 3U

int
rtw88_phy_table_validate(const uint32_t *words, size_t count)
{
	size_t i;
	unsigned branch;
	int open = 0, pending = 0, saw_else = 0;

	if ((words == NULL && count != 0) || count % 2 != 0)
		return EINVAL;
	for (i = 0; i < count; i += 2) {
		if ((words[i] & RTW88_PHY_POS) != 0) {
			branch = (words[i] >> 28) & 3U;
			switch (branch) {
			case RTW88_PHY_IF:
				if (open)
					return EINVAL;
				open = pending = 1;
				saw_else = 0;
				break;
			case RTW88_PHY_ELIF:
				if (!open || pending || saw_else)
					return EINVAL;
				pending = 1;
				break;
			case RTW88_PHY_ELSE:
				if (!open || pending || saw_else)
					return EINVAL;
				saw_else = 1;
				break;
			case RTW88_PHY_ENDIF:
				if (!open || pending)
					return EINVAL;
				open = saw_else = 0;
				break;
			}
		} else if ((words[i] & RTW88_PHY_NEG) != 0) {
			if (!open || !pending)
				return EINVAL;
			pending = 0;
		} else if (pending) {
			return EINVAL;
		}
	}
	return open ? EINVAL : 0;
}

int
rtw88_phy_table_apply(const uint32_t *words, size_t count,
    const struct rtw88_phy_table_ops *ops, void *cookie)
{
	size_t i;
	uint32_t positive = 0, word;
	unsigned branch;
	int matched = 1, skipped = 0, take, error;

	if (ops == NULL || ops->match == NULL || ops->write == NULL)
		return EINVAL;
	error = rtw88_phy_table_validate(words, count);
	if (error != 0)
		return error;
	for (i = 0; i < count; i += 2) {
		if (ops->check != NULL) {
			error = ops->check(cookie);
			if (error != 0)
				return error;
		}
		word = words[i];
		if ((word & RTW88_PHY_POS) != 0) {
			branch = (word >> 28) & 3U;
			if (branch == RTW88_PHY_ENDIF) {
				matched = 1;
				skipped = 0;
			} else if (branch == RTW88_PHY_ELSE) {
				matched = !skipped;
			} else {
				positive = word;
			}
		} else if ((word & RTW88_PHY_NEG) != 0) {
			if (skipped) {
				matched = 0;
				continue;
			}
			take = -1;
			error = ops->match(cookie, positive, &take);
			if (error != 0)
				return error;
			if (take != 0 && take != 1)
				return EINVAL;
			matched = skipped = take;
		} else if (matched) {
			error = ops->write(cookie, word, words[i + 1]);
			if (error != 0)
				return error;
		}
	}
	return 0;
}
