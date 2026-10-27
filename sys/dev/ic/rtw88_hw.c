/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: FreeBSD a143197801c680b958ab662c88cf086668a1958b rtw88. */

#include <sys/types.h>
#ifdef _KERNEL
#include <sys/param.h>
#include <sys/errno.h>
#else
#include <stddef.h>
#include <errno.h>
#endif

#include "rtw88_hw.h"

/*
 * WME queue and EDCA formats follow FreeBSD rtw88 at
 * d2145e9b96448da34d3cfede24c509af6d830c92, pci.c and mac80211.c.
 * The BSD-3-Clause option applies. No LinuxKPI policy is imported.
 */
int
rtw88_wme_map(unsigned tid, struct rtw88_wme_map *out)
{
	static const uint8_t ac[8] = { 0, 1, 1, 0, 2, 2, 3, 3 };
	static const uint8_t ring[4] = { 1, 0, 2, 3 };
	static const uint32_t edca[4] = { 0x0508, 0x050c, 0x0504, 0x0500 };
	struct rtw88_wme_map map;

	if (out == NULL || tid > 15)
		return EINVAL;
	if (tid > 7)
		return EOPNOTSUPP;
	map.ac = ac[tid];
	map.ring = ring[map.ac];
	map.edca = edca[map.ac];
	map.qsel = (uint8_t)tid;
	*out = map;
	return 0;
}

int
rtw88_edca_encode(const struct rtw88_edca_params p[RTW88_WME_NAC],
    unsigned slot, unsigned sifs, uint32_t out[RTW88_WME_NAC])
{
	uint32_t words[RTW88_WME_NAC];
	unsigned i, aifs;

	if (p == NULL || out == NULL || (slot != 9 && slot != 20) ||
	    (sifs != 10 && sifs != 16))
		return EINVAL;
	for (i = 0; i < RTW88_WME_NAC; i++) {
		if (p[i].aifsn > 15 || p[i].ecw_min > 15 ||
		    p[i].ecw_max > 15 || p[i].ecw_min > p[i].ecw_max ||
		    p[i].txop > 0xffff)
			return EINVAL;
		aifs = p[i].aifsn * slot + sifs;
		if (aifs > 0xff)
			return ERANGE;
		words[i] = (uint32_t)p[i].txop << 16 |
		    (uint32_t)p[i].ecw_max << 12 |
		    (uint32_t)p[i].ecw_min << 8 | aifs;
	}
	for (i = 0; i < RTW88_WME_NAC; i++)
		out[i] = words[i];
	return 0;
}

int
rtw88_dma_validate(uint64_t addr, size_t len, unsigned addr_bits)
{
	uint64_t maximum;

	if (len == 0 || addr_bits == 0 || addr_bits > 64)
		return EINVAL;
	if (addr_bits > 32)
		return ENOTSUP;
	maximum = (UINT64_C(1) << addr_bits) - 1;
	/* Check the last byte without forming a wrapping addr + len. */
	if (addr > maximum || len - 1 > maximum - addr)
		return EOVERFLOW;
	return 0;
}
