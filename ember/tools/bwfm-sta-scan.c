/* SPDX-License-Identifier: BSD-2-Clause */
/* Execute the scan metadata conversion from driver_bsd.c. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#ifndef __NetBSD__
#define __NetBSD__ 1
#endif
#define IEEE80211_IOC_STA_MGMT 75
#define BIT(n) (1U << (n))
#include "scan-flags.h"
typedef uint16_t u16;
struct os_reltime { unsigned sec, usec; };
struct bsd_driver_data {
	int sta_btm, sta_rrm;
	struct os_reltime sta_scan_completed;
};
struct ieee80211req_scan_result {
	uint8_t isr_rssi, isr_noise, isr_capinfo, isr_pad[5];
};
struct wpa_scan_res {
	int level, qual, noise;
	unsigned caps, flags, age;
};
static unsigned checks, failures;

static void
check(bool pass, const char *why)
{
	checks++;
	if (!pass) {
		fprintf(stderr, "FAIL: %s\n", why);
		failures++;
	}
}

static void
os_get_reltime(struct os_reltime *time)
{
	time->sec = 10;
	time->usec = 0;
}

static void
os_reltime_sub(const struct os_reltime *a, const struct os_reltime *b,
    struct os_reltime *result)
{
	result->sec = a->sec - b->sec;
	result->usec = a->usec - b->usec;
}

static void
scan_metadata(struct bsd_driver_data *drv, struct wpa_scan_res *result,
    const struct ieee80211req_scan_result *sr)
{
#include "bsd-scan-metadata.h"
}

int
main(void)
{
	struct bsd_driver_data drv = { .sta_btm = 1 };
	struct ieee80211req_scan_result sr = {
		.isr_rssi = 183, .isr_capinfo = 0x11,
		.isr_pad = { 0x10, 0, (uint8_t)-42, 0, 0 }
	};
	struct wpa_scan_res result = { 0 };

	scan_metadata(&drv, &result, &sr);
	check(result.level == 183 && result.qual == 183 &&
	    !(result.flags & WPA_SCAN_LEVEL_DBM),
	    "legacy quality remains unsigned without explicit dBm validity");
	check(result.caps == 0x11,
	    "unmarked high capability byte is ignored");
	sr.isr_pad[1] = IEEE80211_SCAN_RSSI_DBM;
	scan_metadata(&drv, &result, &sr);
	check(result.level == -42 && result.qual == 183 &&
	    (result.flags & WPA_SCAN_LEVEL_DBM) &&
	    (result.flags & WPA_SCAN_NOISE_INVALID),
	    "explicit dBm preserves quality and marks unmeasured noise invalid");
	check(result.caps == 0x11,
	    "dBm flag alone does not validate high capabilities");
	sr.isr_pad[1] |= IEEE80211_SCAN_CAPINFO_HI;
	scan_metadata(&drv, &result, &sr);
	check(result.caps == 0x1011 && result.level == -42 &&
	    (result.flags & WPA_SCAN_LEVEL_DBM),
	    "combined validity flags preserve RRM capability and dBm");
	sr.isr_pad[2] = (uint8_t)-81;
	scan_metadata(&drv, &result, &sr);
	check(result.level < -80,
	    "weak candidate remains below the supplicant dBm threshold");
	sr.isr_pad[2] = (uint8_t)-128;
	scan_metadata(&drv, &result, &sr);
	check(result.level == -128,
	    "minimum representable dBm survives the ABI byte");
	sr.isr_pad[1] = IEEE80211_SCAN_CAPINFO_HI;
	result = (struct wpa_scan_res){ 0 };
	scan_metadata(&drv, &result, &sr);
	check(result.caps == 0x1011 && result.level == 183 &&
	    !(result.flags & WPA_SCAN_LEVEL_DBM),
	    "high capabilities do not imply a valid dBm measurement");
	printf("%s: %u production BSD scan metadata assertions\n",
	    failures ? "FAIL" : "PASS", checks);
	return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
