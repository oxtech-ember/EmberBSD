/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: original explicit network profile, AI-assisted. */
#ifndef _DEV_IC_RTW88_PROFILE_H_
#define _DEV_IC_RTW88_PROFILE_H_

#ifdef _KERNEL
#include <sys/types.h>
#include <sys/device.h>
#else
#include <stdbool.h>
#include <stdint.h>
#endif

struct rtw88_caps;

enum rtw88_regdomain {
	RTW88_REG_UNSET,
	RTW88_REG_WORLD_MIN
};

struct rtw88_profile_input {
	bool has_platform_mac, has_regulatory;
	uint8_t platform_mac[6], efuse_mac[6];
	enum rtw88_regdomain domain;
	uint16_t channels_2g;
};

struct rtw88_profile {
	uint8_t mac[6];
	enum rtw88_regdomain domain;
	uint16_t channels_2g;
	bool tx_admitted;
};

/* This admits explicit configuration, not RF calibration or device readiness.
 * The chip must also enforce its channel, power and calibration policy.
 * Every failure leaves the output unchanged. No operation accesses a bus. */
int rtw88_profile_decode(const struct rtw88_profile_input *,
    const struct rtw88_caps *, struct rtw88_profile *);

#ifdef _KERNEL
/* The attach owner pins the device and reads properties in process context.
 * An absent regulatory property never supplies a default transmit policy. */
int rtw88_profile_from_properties(device_t, const uint8_t[6],
    const struct rtw88_caps *, struct rtw88_profile *);
#endif

#endif /* _DEV_IC_RTW88_PROFILE_H_ */
