/* SPDX-License-Identifier: BSD-2-Clause */
/* Adapt the S31 association builder to host-owned station capabilities. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <net80211/ieee80211_assoc_caps.h>
#include "../esp_wifi_os.h"
#include "assoc_caps.h"

static uint8_t assoc_extcap[ESPWIFI_EXTCAP_LEN];
static size_t assoc_extcap_len;
static bool assoc_extcap_active;

/* Section boundaries follow linker relaxation, unlike pinned PC offsets. */
extern const uint8_t espwifi_assoc_req_text_start[];
extern const uint8_t espwifi_assoc_req_text_end[];
extern uint8_t *__real_ieee80211_add_extcap(uint8_t *, void *);

void
espwifi_vendor_assoc_caps_clear(void)
{
	unsigned int s = espwifi_os_critical_enter();

	memset(assoc_extcap, 0, sizeof(assoc_extcap));
	assoc_extcap_len = 0;
	assoc_extcap_active = false;
	espwifi_os_critical_exit(s);
}

void
espwifi_vendor_assoc_caps_set(const uint8_t *caps, size_t len)
{
	unsigned int s = espwifi_os_critical_enter();

	memset(assoc_extcap, 0, sizeof(assoc_extcap));
	assoc_extcap_len = 0;
	assoc_extcap_active = len <= sizeof(assoc_extcap);
	if (assoc_extcap_active) {
		memcpy(assoc_extcap, caps, len);
		assoc_extcap_len = len;
	}
	espwifi_os_critical_exit(s);
}

uint8_t *
__wrap_ieee80211_add_extcap(uint8_t *frame, void *node)
{
	uint8_t host[ESPWIFI_EXTCAP_LEN], *end;
	size_t len;
	bool active;
	unsigned int s;
	uintptr_t caller = (uintptr_t)__builtin_return_address(0);

	end = __real_ieee80211_add_extcap(frame, node);
	/* AP responses and beacons retain the unmodified vendor behavior. */
	if (caller < (uintptr_t)espwifi_assoc_req_text_start ||
	    caller >= (uintptr_t)espwifi_assoc_req_text_end)
		return end;
	s = espwifi_os_critical_enter();
	active = assoc_extcap_active;
	len = assoc_extcap_len;
	memcpy(host, assoc_extcap, sizeof(host));
	espwifi_os_critical_exit(s);
	/* The producer verifies this fixed-size builder in the selected archive. */
	if (active && node != NULL && end == frame + ESPWIFI_EXTCAP_LEN + 2 &&
	    frame[0] == 127 && frame[1] == ESPWIFI_EXTCAP_LEN)
		(void)ieee80211_sta_extcap_merge(frame + 2,
		    ESPWIFI_EXTCAP_LEN, host, len);
	return end;
}
