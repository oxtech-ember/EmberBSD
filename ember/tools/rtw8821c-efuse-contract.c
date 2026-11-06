/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD; RTL8821CE EFUSE source contract. */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <dev/ic/rtw8821c.h>
int
main(void)
{
	uint8_t data[RTW8821C_EFUSE_PHYSICAL_SIZE];
	const uint8_t mac[] = { 2, 0x11, 0x22, 0x33, 0x44, 0x55 };
	struct rtw8821c_efuse out, saved;
	struct rtw8821c_identity id = { 0x10ec, 0xc821, 0 };
	const uint8_t calibration[] = {
		0xef, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
		0x0f, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38,
		0x2f, 0x30, 0x41, 0x42, 0xe5, 0x44, 0x45, 0x46, 0x47, 0x48
	};

	/* Extended block 26, words 0..2 are PCI MAC at 0xd0. */
	memset(data, 0xff, sizeof(data));
	data[0] = 0x4f; data[1] = 0x38;
	memcpy(data + 2, mac, sizeof(mac));
	memcpy(data + 8, calibration, sizeof(calibration));
	assert(rtw8821c_efuse_decode(data, sizeof(data), &out) == 0);
	assert(memcmp(out.mac, mac, sizeof(mac)) == 0);
	assert(memcmp(out.logical + 0xd0, mac, sizeof(mac)) == 0);
	assert(out.logical[0x1ff] == 0xff);
	assert(out.channel_plan == 0x21 && out.xtal == 0x22);
	assert(out.thermal == 0x23 && out.pa_type == 0x25);
	assert(out.lna_2g[0] == 0x26 && out.lna_2g[1] == 0x27);
	assert(out.lna_5g[0] == 0x28 && out.lna_5g[1] == 0x31);
	assert(out.board_option == 0x32 && out.bt_setting == 0x34);
	assert(out.rfe_option == 5 && out.package_type == 1);
	assert(out.country[0] == 0x44 && out.country[1] == 0x45);
	saved = out;
	data[2] = 3;
	assert(rtw8821c_efuse_decode(data, sizeof(data), &out) == EINVAL);
	assert(memcmp(&out, &saved, sizeof(out)) == 0);
	data[2] = 2;
	assert(rtw8821c_efuse_decode(data, 3, &out) == EINVAL);
	assert(memcmp(&out, &saved, sizeof(out)) == 0);
	/* Valid MAC followed by empty records up to the protected boundary. */
	memset(data, 0x1f, sizeof(data));
	data[0] = 0x4f; data[1] = 0x38;
	memcpy(data + 2, mac, sizeof(mac));
	data[RTW8821C_EFUSE_USABLE_SIZE] = 0xef;
	data[RTW8821C_EFUSE_USABLE_SIZE + 1] = 0xfe;
	assert(rtw8821c_efuse_decode(data, sizeof(data), &out) == 0);
	assert(memcmp(out.mac, mac, sizeof(mac)) == 0);
	assert(out.logical[0x1ff] == 0xff);
	saved = out;
	/* Extended header cannot borrow its second byte from protected data. */
	data[RTW8821C_EFUSE_USABLE_SIZE - 1] = 0x4f;
	data[RTW8821C_EFUSE_USABLE_SIZE] = 0x38;
	memcpy(data + RTW8821C_EFUSE_USABLE_SIZE + 1, mac, sizeof(mac));
	assert(rtw8821c_efuse_decode(data, sizeof(data), &out) == EINVAL);
	assert(memcmp(&out, &saved, sizeof(out)) == 0);
	data[0] = 0xef; data[1] = 0xfe;
	assert(rtw8821c_efuse_decode(data, sizeof(data), &out) == EOVERFLOW);
	assert(memcmp(&out, &saved, sizeof(out)) == 0);
	assert(rtw8821c_efuse_decode(data, sizeof(data) + 1, &out) == EINVAL);
	assert(rtw8821c_efuse_decode(NULL, sizeof(data), &out) == EINVAL);
	assert(rtw8821c_efuse_decode(data, 0, &out) == EINVAL);
	assert(rtw8821c_efuse_decode(data, sizeof(data), NULL) == EINVAL);
	memset(data, 0xff, sizeof(data));
	data[0] = 0x4f; data[1] = 0x38;
	memset(data + 2, 0, sizeof(mac));
	assert(rtw8821c_efuse_decode(data, sizeof(data), &out) == EINVAL);
	assert(memcmp(&out, &saved, sizeof(out)) == 0);
	assert(rtw8821c_validate_identity(&id) == 0);
	id.product = 0xc822;
	assert(rtw8821c_validate_identity(&id) == ENOTSUP);
	id.product = 0xc821; id.sys_cfg1 = 0x00800000;
	assert(rtw8821c_validate_identity(&id) == ENOTSUP);
	assert(rtw8821c_validate_identity(NULL) == EINVAL);
	puts("PASS: RTL8821CE EFUSE and identity source contract");
	return 0;
}
