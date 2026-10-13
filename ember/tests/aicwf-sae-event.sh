#!/bin/sh
# Exercise the actual event path without a sleeping identity-change window.
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/aicwf-sae-event.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
cat > "$work/check.c" <<'C'
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define ETHER_ADDR_LEN 6
#define IEEE80211_ADDR_EQ(a,b) (memcmp(a,b,6) == 0)
#define IEEE80211_S_AUTH 2
#define IEEE80211_MSG_STATE 0
#define IEEE80211_DPRINTF(...) ((void)0)
#define IEEE80211_SAE_VERSION 1
#define IEEE80211_SAE_START 5
#define IEEE80211_SAE_RX_FRAME 6
#define RTM_IEEE80211_SAE 1
#define KM_SLEEP 0
#define KM_NOSLEEP 1
#define kmem_free(p,n) free(p)
struct ieee80211_node {uint8_t ni_bssid[6], ni_essid[32]; size_t ni_esslen;};
struct ieee80211com {void *ic_ifp; int ic_state; struct ieee80211_node *ic_bss; uint8_t ic_myaddr[6];};
struct ieee80211req_sae {uint32_t generation; uint16_t version, op, len, reserved[2]; uint8_t bssid[6], data[1536];};
struct evcnt {unsigned ev_count;};
struct aicwf_softc {
    struct ieee80211com sc_ic;
    bool sc_if_attached, sc_sae_enabled, sc_connecting, sc_sae_pending;
    uint8_t sc_vif, sc_sae_bssid[6];
    uint32_t sc_sae_generation;
    struct evcnt sc_ev_sae_drop, sc_ev_sae_start, sc_ev_sae_rx;
};
static bool allocation_failure;
static unsigned deliveries;
static struct ieee80211req_sae delivered;
static void *kmem_zalloc(size_t n, int flags)
{
    /* KM_SLEEP would allow CONFIGURE to replace the checked peer here. */
    assert(flags == KM_NOSLEEP);
    return allocation_failure ? NULL : calloc(1, n);
}
static void rt_ieee80211msg(void *ifp, int type, void *req, size_t len)
{
    (void)ifp;
    assert(type == RTM_IEEE80211_SAE && len <= sizeof(delivered));
    memcpy(&delivered, req, len);
    deliveries++;
}
#include "if_aicwf_sae.h"
C
awk '/^aicwf_sae_event\(/ {copy=1; print "static void"}
    copy {print} copy && /^}/ {exit}' \
    "$src/sys/dev/sdmmc/if_aicwf.c" >> "$work/check.c"
cat >> "$work/check.c" <<'C'
int main(void)
{
    struct ieee80211_node node = {.ni_bssid={2,0,0,0,0,1},
        .ni_essid="old", .ni_esslen=3};
    struct aicwf_softc sc = {0};
    uint8_t wire[44] = {0};
    sc.sc_ic.ic_bss = &node;
    sc.sc_ic.ic_state = IEEE80211_S_AUTH;
    sc.sc_if_attached = sc.sc_sae_enabled = sc.sc_connecting = true;
    sc.sc_sae_generation = 42;
    memcpy(sc.sc_sae_bssid, node.ni_bssid, 6);
    wire[1] = 3;
    memcpy(wire+2, "old", 3);
    memcpy(wire+34, node.ni_bssid, 6);
    memcpy(wire+40, "\x08\xac\x0f\x00", 4);
    allocation_failure = true;
    aicwf_sae_event(&sc, true, wire, sizeof(wire));
    assert(deliveries == 0 && !sc.sc_sae_pending);
    assert(sc.sc_ev_sae_drop.ev_count == 1 && sc.sc_ev_sae_start.ev_count == 0);
    allocation_failure = false;
    aicwf_sae_event(&sc, true, wire, sizeof(wire));
    assert(deliveries == 1 && sc.sc_sae_pending);
    assert(delivered.generation == 42 && delivered.bssid[5] == 1);
    assert(delivered.len == 3 && memcmp(delivered.data, "old", 3) == 0);
    assert(sc.sc_ev_sae_drop.ev_count == 1 && sc.sc_ev_sae_start.ev_count == 1);
    sc.sc_ic.ic_bss = NULL;
    aicwf_sae_event(&sc, true, wire, sizeof(wire));
    assert(deliveries == 1 && sc.sc_ev_sae_drop.ev_count == 2);
    puts("PASS: SAE event identity, missing BSS and allocation failure recovery");
    return 0;
}
C
${CC:-cc} -std=c99 -Wall -Wextra -Werror -I"$src/sys/dev/sdmmc" \
    "$work/check.c" -o "$work/check"
"$work/check"
