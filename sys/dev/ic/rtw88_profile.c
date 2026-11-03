/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: original explicit network profile, AI-assisted. */
#ifdef _KERNEL
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <prop/proplib.h>
#else
#include <errno.h>
#include <stddef.h>
#include <string.h>
#endif

#include <dev/ic/rtw88_chip.h>
#include <dev/ic/rtw88_profile.h>

static int
rtw88_profile_mac_valid(const uint8_t mac[6])
{
	uint8_t any = 0;
	unsigned i;

	if ((mac[0] & 1) != 0)
		return 0;
	for (i = 0; i < 6; i++)
		any |= mac[i];
	return any != 0;
}

int
rtw88_profile_decode(const struct rtw88_profile_input *input,
    const struct rtw88_caps *caps, struct rtw88_profile *profile)
{
	struct rtw88_profile result;
	const uint8_t *mac;

	if (input == NULL || caps == NULL || profile == NULL)
		return EINVAL;
	mac = input->has_platform_mac ? input->platform_mac : input->efuse_mac;
	if (!rtw88_profile_mac_valid(mac))
		return EINVAL;
	if (!input->has_regulatory)
		return EACCES;
	if (input->domain != RTW88_REG_WORLD_MIN || input->channels_2g == 0)
		return EINVAL;
	if ((input->channels_2g & ~UINT16_C(0x1fff)) != 0)
		return EOPNOTSUPP;
	if ((caps->bands & RTW88_BAND_2G) == 0 ||
	    (caps->widths & RTW88_WIDTH_20) == 0)
		return EOPNOTSUPP;
	memset(&result, 0, sizeof(result));
	memcpy(result.mac, mac, sizeof(result.mac));
	result.domain = input->domain;
	result.channels_2g = input->channels_2g;
	result.tx_admitted = true;
	*profile = result;
	return 0;
}

#ifdef _KERNEL
int
rtw88_profile_from_properties(device_t dev, const uint8_t efuse_mac[6],
    const struct rtw88_caps *caps, struct rtw88_profile *profile)
{
	struct rtw88_profile_input input;
	prop_dictionary_t properties, regulatory;
	prop_object_t value;
	const void *bytes;

	if (dev == NULL || efuse_mac == NULL || caps == NULL || profile == NULL)
		return EINVAL;
	memset(&input, 0, sizeof(input));
	memcpy(input.efuse_mac, efuse_mac, sizeof(input.efuse_mac));
	properties = device_properties(dev);
	if (properties == NULL)
		return EACCES;
	value = prop_dictionary_get(properties, "mac-address");
	if (value != NULL) {
		if (prop_object_type(value) != PROP_TYPE_DATA ||
		    prop_data_size((prop_data_t)value) != sizeof(input.platform_mac))
			return EINVAL;
		bytes = prop_data_value((prop_data_t)value);
		if (bytes == NULL)
			return EINVAL;
		memcpy(input.platform_mac, bytes, sizeof(input.platform_mac));
		input.has_platform_mac = true;
		if (!rtw88_profile_mac_valid(input.platform_mac))
			return EINVAL;
	}
	value = prop_dictionary_get(properties, "rtw88-regulatory");
	if (value == NULL)
		return rtw88_profile_decode(&input, caps, profile);
	if (prop_object_type(value) != PROP_TYPE_DICTIONARY)
		return EINVAL;
	regulatory = (prop_dictionary_t)value;
	value = prop_dictionary_get(regulatory, "domain");
	if (value == NULL || prop_object_type(value) != PROP_TYPE_STRING ||
	    !prop_string_equals_string((prop_string_t)value, "world-min"))
		return EINVAL;
	value = prop_dictionary_get(regulatory, "channels-2ghz");
	if (value == NULL || prop_object_type(value) != PROP_TYPE_NUMBER ||
	    !prop_number_uint16_value((prop_number_t)value, &input.channels_2g))
		return EINVAL;
	input.has_regulatory = true;
	input.domain = RTW88_REG_WORLD_MIN;
	return rtw88_profile_decode(&input, caps, profile);
}
#endif
