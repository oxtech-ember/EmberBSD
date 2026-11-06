/* SPDX-License-Identifier: BSD-2-Clause */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "rtw8821c_phy.h"
#include "rtw88_hw.h"
int main(void)
{
	static const uint8_t expected[20] = {26, 26, 26, 26, 31, 31, 31,
					     31, 31, 31, 31, 29, 27, 27,
					     27, 27, 27, 27, 27, 27};
	struct rtw8821c_efuse e = {0};
	struct rtw88_channel_tuple t = {1, 1, 20};
	uint8_t out[20], before[20];
	unsigned i;
	memset(e.logical, 0xff, sizeof(e.logical));
	e.logical[0x10] = 30;
	e.logical[0x16] = 28;
	e.logical[0x1b] = 0xf1;
	if (rtw8821c_power_indices(&e, &t, 12, out) ||
	    memcmp(out, expected, 20))
		return 1;
	e.rfe_option = 2;
	memcpy(e.logical + 0x10 + 42, e.logical + 0x10, 18);
	if (rtw8821c_power_indices(&e, &t, 12, out) ||
	    memcmp(out, expected, 20))
		return 2;
	memset(before, 0xa5, sizeof(before));
	memcpy(out, before, 20);
	e.logical[0x10 + 42] = 0xff;
	if (rtw8821c_power_indices(&e, &t, 12, out) != EINVAL ||
	    memcmp(out, before, 20))
		return 3;
	t.primary = 36;
	t.center = 36;
	if (rtw8821c_power_indices(&e, &t, 12, out) != EINVAL ||
	    memcmp(out, before, 20))
		return 4;
	for (i = 0; i < 20; i++)
		if (expected[i] > 63)
			return 5;
	puts("RTL8821C 2 GHz power vectors, RFE swap, invalid calibration and "
	     "channel: PASS");
	return 0;
}
