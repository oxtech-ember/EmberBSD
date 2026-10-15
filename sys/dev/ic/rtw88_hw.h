/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: FreeBSD a143197801c680b958ab662c88cf086668a1958b rtw88. */

#ifndef _DEV_IC_RTW88_HW_H_
#define _DEV_IC_RTW88_HW_H_

#include <sys/types.h>
#ifndef _KERNEL
#include <stddef.h>
#include <stdint.h>
#endif

#define RTW8822C_TX_PACKET_DESC_SIZE	48
#define RTW8822C_TX_BUFFER_DESC_SIZE	16
#define RTW8822C_RX_PACKET_DESC_SIZE	24
#define RTW8822C_RX_BUFFER_DESC_SIZE	8
#define RTW8822C_FW_NAME			"rtw88/rtw8822c_fw.bin"

#define RTW88_FW_HEADER_SIZE	64
#define RTW88_FW_CHECKSUM_SIZE	8

/* File ranges include the eight checksum bytes after each payload. */
struct rtw88_fw_section {
	size_t offset;
	size_t size;
	uint32_t payload_size;
	uint32_t load_address;	/* Header address with bit 31 removed. */
};

struct rtw88_fw_info {
	uint16_t signature;	/* Shared by RTL8822B and RTL8822C. */
	uint16_t version;
	uint8_t subversion;
	uint8_t subindex;
	uint16_t h2c_version;
	uint32_t feature;
	struct rtw88_fw_section dmem;
	struct rtw88_fw_section imem;
	struct rtw88_fw_section emem;
};

/*
 * Accept only the pinned RTL8822C normal-firmware load profile documented
 * in rtw88-provenance.md. No alignment is required. Return 0, EINVAL for
 * malformed input, EOVERFLOW for arithmetic overflow, or ENOTSUP for an
 * unsupported signature or load profile. Leave *out unchanged on error.
 * Success validates container ranges, not firmware authenticity, checksum
 * contents, silicon compatibility, or readiness to enable the radio.
 */
int rtw88_fw_parse(const uint8_t *, size_t, struct rtw88_fw_info *);

#endif /* _DEV_IC_RTW88_HW_H_ */
