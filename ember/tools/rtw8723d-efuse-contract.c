/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD; AI-assisted RTL8723D EFUSE decode contract. */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <dev/ic/rtw8723d.h>

int
main(void)
{
	uint8_t input[RTW8723D_EFUSE_PHYSICAL_SIZE];
	const uint8_t mac[] = { 0x02, 0x11, 0x22, 0x33, 0x44, 0x55 };
	struct rtw8723d_efuse result, before;

	/* Extended block 35, words 1..3 encode the SDIO MAC at 0x11a. */
	memset(input, 0xff, sizeof(input));
	input[0] = 0x6f;
	input[1] = 0x41;
	memcpy(input + 2, mac, sizeof(mac));
	assert(rtw8723d_efuse_decode(input, sizeof(input), &result) == 0);
	assert(memcmp(result.mac, mac, sizeof(mac)) == 0);
	assert(result.rfe == 0);
	before = result;
	input[2] = 0x03; /* Multicast address is not a station identity. */
	assert(rtw8723d_efuse_decode(input, sizeof(input), &result) == EINVAL);
	assert(memcmp(&result, &before, sizeof(result)) == 0);
	input[2] = 0x02;
	assert(rtw8723d_efuse_decode(input, 3, &result) == EINVAL);
	assert(memcmp(&result, &before, sizeof(result)) == 0);
	/* The protected tail cannot complete an extended header. */
	memset(input, 0x0f, RTW8723D_EFUSE_USABLE_SIZE);
	input[RTW8723D_EFUSE_USABLE_SIZE - 1] = 0x6f;
	input[RTW8723D_EFUSE_USABLE_SIZE] = 0x41;
	assert(rtw8723d_efuse_decode(input, sizeof(input), &result) == EINVAL);
	assert(memcmp(&result, &before, sizeof(result)) == 0);
	input[0] = 0xef; /* Extended block 127 lies beyond the logical map. */
	input[1] = 0xfe;
	assert(rtw8723d_efuse_decode(input, sizeof(input), &result) == EOVERFLOW);
	assert(memcmp(&result, &before, sizeof(result)) == 0);
	assert(rtw8723d_efuse_decode(input, sizeof(input) + 1, &result) == EINVAL);
	assert(rtw8723d_efuse_decode(NULL, sizeof(input), &result) == EINVAL);
	puts("PASS: RTL8723D EFUSE MAC, protected tail, malformed input and output preservation");
	return 0;
}
