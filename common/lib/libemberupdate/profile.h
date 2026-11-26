/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef EMBER_UPDATE_PROFILE_H
#define EMBER_UPDATE_PROFILE_H

#include <string.h>
#include "json.h"

enum eu_adapter {
	EU_ADAPTER_INVALID,
	EU_ADAPTER_SINGLE,
	EU_ADAPTER_UBOOT,
	EU_ADAPTER_EFI
};

/* The storage type and boot adapter form one installed profile contract. */
static inline enum eu_adapter
eu_profile_adapter(const struct eu_json *profile)
{
	const char *storage = eu_string(eu_get(profile, "storage"));
	const char *adapter = eu_string(eu_get(profile, "adapter"));
	if (storage == NULL || adapter == NULL)
		return EU_ADAPTER_INVALID;
	if (!strcmp(storage, "single") && !strcmp(adapter, "none"))
		return EU_ADAPTER_SINGLE;
	if (!strcmp(storage, "ffs")) {
		if (!strcmp(adapter, "uboot-ffs"))
			return EU_ADAPTER_UBOOT;
		if (!strcmp(adapter, "efiboot"))
			return EU_ADAPTER_EFI;
	}
	return EU_ADAPTER_INVALID;
}
#endif
