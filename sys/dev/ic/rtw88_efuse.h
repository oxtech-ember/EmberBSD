/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: native bounded map parser from EmberBSD rtw8822c.c and Linux
 * adc218676eef25575469234709c2d87185ca223a, rtw88/efuse.c. */
#ifndef _DEV_IC_RTW88_EFUSE_H_
#define _DEV_IC_RTW88_EFUSE_H_

#ifdef _KERNEL
#include <sys/types.h>
#include <sys/errno.h>
#include <sys/systm.h>
#else
#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#endif

/* Expand a caller-selected usable physical prefix into private scratch.
 * The chip selects physical/logical sizes and any protected tail. This
 * routine may change scratch on failure; callers publish only on success. */
static inline int
rtw88_efuse_expand(const uint8_t *data, size_t size, uint8_t *logical,
    size_t logical_size)
{
	size_t cursor = 0, offset;
	unsigned block, words, word;
	uint8_t header, extended;

	if (data == NULL || logical == NULL || size == 0 || logical_size < 2)
		return EINVAL;
	memset(logical, 0xff, logical_size);
	while (cursor < size) {
		header = data[cursor++];
		if (header == 0xff)
			break;
		if ((header & 0x1f) == 0x0f) {
			if (cursor == size)
				return EINVAL;
			extended = data[cursor++];
			if (extended == 0xff)
				break;
			block = ((extended & 0xf0) >> 1) | ((header >> 5) & 7);
			words = extended & 0x0f;
		} else {
			block = header >> 4;
			words = header & 0x0f;
		}
		for (word = 0; word < 4; word++) {
			if ((words & (1U << word)) != 0)
				continue;
			/* The seven-bit block index cannot overflow this product. */
			offset = block * 8 + word * 2;
			if (offset > logical_size - 2)
				return EOVERFLOW;
			if (size - cursor < 2)
				return EINVAL;
			logical[offset] = data[cursor++];
			logical[offset + 1] = data[cursor++];
		}
	}
	return 0;
}

#endif /* _DEV_IC_RTW88_EFUSE_H_ */
