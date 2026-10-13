#!/bin/sh
# Exercise the actual cache-selection block before association RSNE generation.
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/bwfm-sae-pmksa.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
awk '/^#define WPA_DRIVER_FLAGS2_SAE_NO_PMKSA/ {print}' \
    "$src/external/bsd/wpa/dist/src/drivers/driver.h" > "$work/flag.h"
awk '/Select a cached PMKSA only when/ {copy=1; next}
copy && /wpa_ie_len = max_wpa_ie_len/ {exit}
copy {print}' "$src/external/bsd/wpa/dist/wpa_supplicant/wpa_supplicant.c" > "$work/cache.h"
cat > "$work/check.c" <<'C'
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "flag.h"
#define CONFIG_SAE 1
struct state { int current; };
struct wpa_supplicant { uint64_t drv_flags2; struct state *wpa; int eapol; };
struct wpa_ssid { int key_mgmt; };
static int wpa_key_mgmt_sae(int key_mgmt) { return (key_mgmt & 1) != 0; }
static void pmksa_cache_clear_current(struct state *s) { s->current = 0; }
static int pmksa_cache_set_current(struct state *s, const void *pmkid,
    const void *addr, const void *ssid, int opportunistic, const void *cache_id,
    int akmp, bool opportunistic_only)
{
    (void)pmkid; (void)addr; (void)ssid; (void)opportunistic;
    (void)cache_id; (void)akmp; (void)opportunistic_only;
    s->current = 2;
    return 0;
}
static void eapol_sm_notify_pmkid_attempt(int unused) { (void)unused; }
static void check(uint64_t flags, int key_mgmt, int expected)
{
    struct state state = { .current = 1 };
    struct wpa_supplicant wpa = { .drv_flags2 = flags, .wpa = &state };
    struct wpa_supplicant *wpa_s = &wpa;
    struct wpa_ssid network = { .key_mgmt = key_mgmt }, *ssid = &network;
    const void *addr = NULL, *cache_id = NULL;
    int try_opportunistic = 0, pmksa_cached = 0;
    (void)wpa_key_mgmt_sae;
    (void)pmksa_cache_clear_current;
#include "cache.h"
    assert(pmksa_cached == expected);
    assert(state.current == (expected ? 2 : 0));
}
int main(void)
{
    check(WPA_DRIVER_FLAGS2_SAE_NO_PMKSA, 1, 0);
    check(WPA_DRIVER_FLAGS2_SAE_NO_PMKSA, 3, 0);
    check(WPA_DRIVER_FLAGS2_SAE_NO_PMKSA, 2, 1);
    check(0, 1, 1);
    check(0, 2, 1);
    puts("PASS: fresh external SAE; WPA2 and other drivers preserve PMKSA reuse");
    return 0;
}
C
${CC:-cc} -std=c99 -Wall -Wextra -Werror "$work/check.c" -o "$work/check"
"$work/check"
