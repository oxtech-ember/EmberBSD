/* SPDX-License-Identifier: BSD-2-Clause */
/* Bounded validation shared by bwfm and its portable regression test. */
#ifndef _DEV_IC_BWFM_SAE_H_
#define _DEV_IC_BWFM_SAE_H_

#include "fullmac_sae.h"

/* EXT_AUTH_REQ itself starts auth. CYW43455 request flags may be zero. */
static inline bool
bwfm_sae_request_valid(const uint8_t *p, size_t len, const uint8_t *peer,
    const uint8_t *ssid, size_t ssid_len)
{
	size_t firmware_len;

	if (len < 60 || ssid_len > 32)
		return false;
	firmware_len = p[8] | (size_t)p[9] << 8 |
	    (size_t)p[10] << 16 | (size_t)p[11] << 24;
	return firmware_len == ssid_len && memcmp(p + 2, peer, 6) == 0 &&
	    memcmp(p + 12, ssid, ssid_len) == 0;
}

static inline bool
bwfm_sae_cap_token(const char *caps, const char *name)
{
	size_t n = strlen(name);
	const char *p = caps;

	while ((p = strstr(p, name)) != NULL) {
		if ((p == caps || p[-1] == ' ') &&
		    (p[n] == '\0' || p[n] == ' '))
			return true;
		p += n;
	}
	return false;
}
#endif
