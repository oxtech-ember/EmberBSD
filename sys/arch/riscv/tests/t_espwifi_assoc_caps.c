/* SPDX-License-Identifier: BSD-2-Clause */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "net80211/ieee80211_assoc_caps.h"
#include "assoc_caps.h"

/* Select the simulated callsite to exercise the wrapper's caller guard. */
extern const uint8_t espwifi_assoc_req_text_start[];
extern const uint8_t espwifi_assoc_req_text_end[];
static uintptr_t simulated_return_address;
static const uint8_t fake_node;

uintptr_t
test_return_address(unsigned int level)
{
	(void)level;
	return simulated_return_address;
}

unsigned int
espwifi_os_critical_enter(void)
{
	return 0;
}

void
espwifi_os_critical_exit(unsigned int s)
{
	(void)s;
}

const uint8_t espwifi_test_bounds[2] = {0, 0};
#define STR1(x) #x
#define STR(x) STR1(x)
__asm__(
    ".globl " STR(__USER_LABEL_PREFIX__) "espwifi_assoc_req_text_start\n"
    ".set " STR(__USER_LABEL_PREFIX__) "espwifi_assoc_req_text_start,"
    STR(__USER_LABEL_PREFIX__) "espwifi_test_bounds\n"
    ".globl " STR(__USER_LABEL_PREFIX__) "espwifi_assoc_req_text_end\n"
    ".set " STR(__USER_LABEL_PREFIX__) "espwifi_assoc_req_text_end,"
    STR(__USER_LABEL_PREFIX__) "espwifi_test_bounds+1\n");

uint8_t *
__real_ieee80211_add_extcap(uint8_t *frame, void *node)
{
	static const uint8_t vendor_caps[12] = {
		0x41, 0x20, 0x02, 0x02, 0x80, 0x01,
		0x55, 0xaa, 0x10, 0x20, 0x40, 0x08
	};
	(void)node;
	frame[0] = 127;
	frame[1] = sizeof(vendor_caps);
	memcpy(frame + 2, vendor_caps, sizeof(vendor_caps));
	return frame + 2 + sizeof(vendor_caps);
}
uint8_t *
__wrap_ieee80211_add_extcap(uint8_t *, void *);

static size_t
count_ie(const uint8_t *p, size_t n, uint8_t id)
{
	size_t count = 0, off = 0;
	while (off < n) {
		assert(n - off >= 2 && p[off + 1] <= n - off - 2);
		if (p[off] == id) count++;
		off += (size_t)p[off + 1] + 2;
	}
	return count;
}

static void
test_extract_and_merge(void)
{
	/* Input app IE is extracted before radio output is appended. */
	uint8_t ies[] = {
		48, 2, 0xaa, 0xbb,
		127, 4, 0x11, 0x22, 0x0a, 0x02,
		70, 5, 1, 2, 3, 4, 5,
		5, 1, 9
	};
	const uint8_t expected[] = {
	    48, 2, 0xaa, 0xbb, 70, 5, 1, 2, 3, 4, 5, 5, 1, 9
	};
	uint8_t host[12], frame[32], combined[64], premerge[64];
	size_t n = sizeof(ies), host_len = 0, combined_len;
	bool rrm = false, valid;
	assert(count_ie(ies, n, 127) == 1);
	valid = ieee80211_assoc_caps_extract(ies, &n, host, sizeof(host),
	    &host_len, &rrm);
	assert(valid);
	assert(host_len == 4 && host[0] == 0x11 && host[2] == 0x0a &&
	    host[3] == 0x02);
	assert(rrm && n == sizeof(expected) && memcmp(ies, expected, n) == 0);
	assert(count_ie(ies, n, 70) == 1 && count_ie(ies, n, 127) == 0);

	/* Host-owned WNM17/BTM19/SSID25 override vendor values in one IE. */
	host[2] = 0x0a; /* bits 17 and 19 */
	host[3] = 0x02; /* bit 25 */
	espwifi_vendor_assoc_caps_set(host, host_len);
	simulated_return_address = (uintptr_t)espwifi_assoc_req_text_start;
	uint8_t *end = __wrap_ieee80211_add_extcap(frame, (void *)&fake_node);
	assert(end == frame + 14 && count_ie(frame, 14, 127) == 1);
	assert(frame[4] == 0x0a && frame[5] == 0x02);
	assert((frame[2] & 0x41) == 0x41 && frame[6] == 0x80 &&
	    frame[7] == 0x01);
	memcpy(premerge, (uint8_t[]){127, 4, 0x11, 0x22, 0x0a, 0x02}, 6);
	memcpy(premerge + 6, frame, 14);
	assert(count_ie(premerge, 20, 127) == 2);
	memcpy(combined, ies, n);
	memcpy(combined + n, frame, 14);
	combined_len = n + 14;
	assert(count_ie(combined, combined_len, 127) == 1);

	/* Explicit host zeroes suppress firmware defaults for host-owned bits. */
	host[2] = host[3] = 0;
	espwifi_vendor_assoc_caps_set(host, host_len);
	end = __wrap_ieee80211_add_extcap(frame, (void *)&fake_node);
	assert(end == frame + 14 && frame[4] == 0 && frame[5] == 0);
	assert((frame[2] & 0x41) == 0x41 && frame[6] == 0x80);
}

static void
assert_rejected_unchanged(const uint8_t *input, size_t input_len)
{
	uint8_t buf[64], before[64], out[12];
	size_t n = input_len, out_len = 99;
	bool rrm = true, valid;
	memcpy(buf, input, input_len); memcpy(before, input, input_len);
	valid = ieee80211_assoc_caps_extract(buf, &n, out, sizeof(out), &out_len,
	    &rrm);
	assert(!valid);
	assert(n == input_len && memcmp(buf, before, input_len) == 0);
}
static void
test_valid_absent_elements(void)
{
	uint8_t none[] = {5, 1, 7};
	uint8_t ext[] = {127, 12, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
	uint8_t out[12];
	size_t n, out_len;
	bool rrm, valid;
	n = sizeof(none); out_len = 99; rrm = true;
	valid = ieee80211_assoc_caps_extract(none, &n, out, sizeof(out),
	    &out_len, &rrm);
	assert(valid);
	assert(n == sizeof(none) && out_len == 0 && !rrm);
	n = sizeof(ext); out_len = 0; rrm = true;
	valid = ieee80211_assoc_caps_extract(ext, &n, out, sizeof(out),
	    &out_len, &rrm);
	assert(valid);
	assert(n == 0 && out_len == 12 && !rrm && out[11] == 11);
}
static void
test_rejections(void)
{
	static const uint8_t duplicate_ext[] = {127, 1, 1, 127, 1, 2};
	static const uint8_t malformed[] = {127, 4, 1, 2};
	static const uint8_t duplicate_rrm[] = {
	    70, 5, 1, 2, 3, 4, 5, 70, 5, 1, 2, 3, 4, 5
	};
	static const uint8_t bad_rrm_len[] = {70, 4, 1, 2, 3, 4};
	static const uint8_t zero_ext_len[] = {127, 0};
	uint8_t too_long[16] = {127, 13};
	assert_rejected_unchanged(duplicate_ext, sizeof(duplicate_ext));
	assert_rejected_unchanged(malformed, sizeof(malformed));
	assert_rejected_unchanged(duplicate_rrm, sizeof(duplicate_rrm));
	assert_rejected_unchanged(bad_rrm_len, sizeof(bad_rrm_len));
	assert_rejected_unchanged(zero_ext_len, sizeof(zero_ext_len));
	assert_rejected_unchanged(too_long, sizeof(too_long));
}
static void
test_merge_rejects_long_host_unchanged(void)
{
	uint8_t radio[4] = {0x11, 0x22, 0x33, 0x44};
	const uint8_t before[4] = {0x11, 0x22, 0x33, 0x44};
	uint8_t host[5] = {1, 2, 3, 4, 5};
	assert(!ieee80211_sta_extcap_merge(radio, sizeof(radio), host,
	    sizeof(host)));
	assert(memcmp(radio, before, sizeof(radio)) == 0);
}
static void
test_clear_retry_and_ap_scope(void)
{
	uint8_t caps[12] = {0}, frame[16], before[16];
	uint8_t *end;
	caps[2] = 0x0a; caps[3] = 0x02;
	espwifi_vendor_assoc_caps_set(caps, sizeof(caps));
	simulated_return_address = (uintptr_t)espwifi_assoc_req_text_start;
	end = __wrap_ieee80211_add_extcap(frame, (void *)&fake_node);
	assert(end == frame + 14 && frame[4] == 0x0a && frame[5] == 0x02);

	/* A failed/new association clears stale host capabilities. */
	espwifi_vendor_assoc_caps_clear();
	memset(frame, 0, sizeof(frame));
	end = __wrap_ieee80211_add_extcap(frame, (void *)&fake_node);
	assert(end == frame + 14 && frame[4] == 0x02 && frame[5] == 0x02);

	/* Non-association caller must retain the unmodified vendor output. */
	espwifi_vendor_assoc_caps_set(caps, sizeof(caps));
	simulated_return_address = (uintptr_t)espwifi_assoc_req_text_end;
	memset(frame, 0, sizeof(frame));
	end = __wrap_ieee80211_add_extcap(frame, (void *)&fake_node);
	memcpy(before, frame, 14);
	assert(end == frame + 14 && before[4] == 0x02 && before[5] == 0x02);
	assert((before[2] & 0x41) == 0x41);
}
int
main(void)
{
	test_extract_and_merge();
	test_valid_absent_elements();
	test_rejections();
	test_merge_rejects_long_host_unchanged();
	test_clear_retry_and_ap_scope();
	puts("PASS: association capability helper/wrapper boundaries");
	return 0;
}
