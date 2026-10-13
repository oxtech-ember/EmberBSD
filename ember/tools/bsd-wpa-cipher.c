/* SPDX-License-Identifier: BSD-2-Clause */
/* Exercise the production WPA configuration callback and its diagnostics. */
#include "utils/includes.h"
#include "utils/common.h"
#include "drivers/driver.h"
#include "ciphers.h"
#include "ioctls.h"

static unsigned checks, failures, calls, fail_call;
static int operations[8], values[8];
static char diagnostics[2048];
static size_t diagnostic_len;

static void
check(bool pass, const char *why)
{
	checks++;
	if (!pass) {
		fprintf(stderr, "FAIL: %s\n", why);
		failures++;
	}
}

void
wpa_printf(int level, const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(diagnostics + diagnostic_len,
	    sizeof(diagnostics) - diagnostic_len, fmt, ap);
	va_end(ap);
	if (n < 0 || (size_t)n >= sizeof(diagnostics) - diagnostic_len)
		abort();
	diagnostic_len += n;
}

static int
set80211param(void *priv, int op, int value)
{
	if (calls >= sizeof(operations) / sizeof(operations[0]))
		abort();
	operations[calls] = op;
	values[calls] = value;
	calls++;
	return calls == fail_call ? -1 : 0;
}

static int
#include "configure.h"

static void
fixture(void)
{
	calls = fail_call = 0;
	diagnostic_len = 0;
	diagnostics[0] = '\0';
	memset(operations, 0, sizeof(operations));
	memset(values, 0, sizeof(values));
}

int
main(void)
{
	static const struct {
		int suite, native, keylen;
		const char *name;
	} cases[] = {
		{ WPA_CIPHER_NONE, IEEE80211_CIPHER_NONE, 0, "NONE" },
		{ WPA_CIPHER_CCMP, IEEE80211_CIPHER_AES_CCM, 0, "AES-CCM" },
		{ WPA_CIPHER_TKIP, IEEE80211_CIPHER_TKIP, 0, "TKIP" },
		{ WPA_CIPHER_WEP40, IEEE80211_CIPHER_WEP, 5, "WEP" },
		{ WPA_CIPHER_WEP104, IEEE80211_CIPHER_WEP, 13, "WEP" }
	};
	struct wpa_bss_params params;
	unsigned i, failure, count, offset;
	char expected[128];

	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		memset(&params, 0, sizeof(params));
		params.wpa_group = cases[i].suite;
		params.wpa_pairwise = WPA_CIPHER_CCMP | WPA_CIPHER_TKIP | WPA_CIPHER_NONE;
		params.wpa_key_mgmt = WPA_KEY_MGMT_PSK;
		params.rsn_preauth = 1;
		params.wpa = 2;
		count = cases[i].keylen ? 6 : 5;
		for (failure = 0; failure <= count; failure++) {
			fixture();
			fail_call = failure;
			check(bsd_configure_wpa(NULL, &params) == (failure ? -1 : 0),
			    "WPA configuration propagates each ioctl failure");
			check(calls == (failure ? failure : count),
			    "WPA configuration stops at the failed operation");
			(void)snprintf(expected, sizeof(expected), "group key cipher=%s (%u)",
			    cases[i].name, cases[i].native);
			check(strstr(diagnostics, expected) != NULL,
			    "cipher diagnostic uses the actual native cipher number");
			check(operations[0] == IEEE80211_IOC_MCASTCIPHER &&
			    values[0] == cases[i].native,
			    "group cipher ioctl retains its native value");
			if (failure == 1) {
				(void)snprintf(expected, sizeof(expected), "(%s)", cases[i].name);
				check(strstr(diagnostics, "Unable to set group key cipher") != NULL &&
				    strstr(diagnostics, expected) != NULL,
				    "failed group cipher ioctl retains a valid cipher name");
			}
			if (failure)
				continue;
			offset = 1;
			if (cases[i].keylen) {
				check(operations[offset] == IEEE80211_IOC_MCASTKEYLEN &&
				    values[offset] == cases[i].keylen, "WEP key length is unchanged");
				offset++;
			}
			check(operations[offset] == IEEE80211_IOC_UCASTCIPHERS &&
			    values[offset] == ((1 << IEEE80211_CIPHER_AES_CCM) |
			    (1 << IEEE80211_CIPHER_TKIP) | (1 << IEEE80211_CIPHER_NONE)),
			    "pairwise cipher mask is unchanged");
			check(operations[offset + 1] == IEEE80211_IOC_KEYMGTALGS &&
			    values[offset + 1] == params.wpa_key_mgmt &&
			    operations[offset + 2] == IEEE80211_IOC_RSNCAPS &&
			    values[offset + 2] == BIT(0) &&
			    operations[offset + 3] == IEEE80211_IOC_WPA &&
			    values[offset + 3] == params.wpa,
			    "AKM, RSN capability and WPA requests are unchanged");
		}
	}
	fixture();
	params.wpa_group = -1;
	check(bsd_configure_wpa(NULL, &params) == -1 && calls == 0,
	    "unknown group cipher is rejected before any ioctl");
	check(strstr(diagnostics, "Unknown group key cipher") != NULL,
	    "unknown group cipher has a bounded numeric diagnostic");
	if (failures)
		return 1;
	printf("PASS: %u production BSD WPA cipher assertions\n", checks);
	return 0;
}
