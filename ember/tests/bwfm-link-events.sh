#!/bin/sh
# Origin: EmberBSD fullmac station disconnect regression, 2026-10-11.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/bwfm-link-events.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
# Compile the actual station event dispatch, with net80211 as the test boundary.
awk '/^bwfm_rx_event_cb\(/ {fn=1}
    fn && /case BWFM_E_SET_SSID:/ {copy=1}
    copy && /^[\t]default:/ {exit}
    copy {print}' "$src/sys/dev/ic/bwfm.c" > "$work/events.h"
awk '/^[\t]BWFM_E_[A-Z_0-9]+ =/ {print; if ($1 == "BWFM_E_LAST") exit}' \
    "$src/sys/dev/ic/bwfmreg.h" > "$work/event-ids.h"
awk '/^#define[ \t]+BWFM_EVENT_MSG_LINK[ \t]/ {print}' \
    "$src/sys/dev/ic/bwfmreg.h" > "$work/event-flags.h"
awk '/^#define[ \t]+ENABLE_EVENT\(/ {copy=1}
    copy {print}
    copy && /^#undef[ \t]+ENABLE_EVENT/ {exit}' \
    "$src/sys/dev/ic/bwfm.c" > "$work/event-mask.h"
cat > "$work/check.c" <<'C'
#include <arpa/inet.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum {
#include "event-ids.h"
};
enum { IEEE80211_S_INIT, IEEE80211_S_SCAN, IEEE80211_S_AUTH,
    IEEE80211_S_ASSOC, IEEE80211_S_RUN };
enum { IEEE80211_M_STA, IEEE80211_M_HOSTAP };
#define BWFM_E_STATUS_SUCCESS 0
#define BWFM_E_STATUS_UNSOLICITED 6
#include "event-flags.h"
#define IEEE80211_ADDR_EQ(a, b) (memcmp((a), (b), 6) == 0)
struct ieee80211_node { uint8_t ni_bssid[6]; };
struct ieee80211com {
    int ic_state, ic_opmode;
    struct ieee80211_node *ic_bss;
};
struct event { struct { uint32_t status, reason, event_type;
    uint16_t flags; uint8_t addr[6]; } msg; };
struct bwfm_softc {
    unsigned sc_link_events, sc_link_event, sc_link_reason, sc_link_flags;
};
static unsigned transitions, checks, failures;
static int next_state;
static void
event_mask(struct ieee80211com *ic, uint8_t *evmask)
{
#include "event-mask.h"
}
static void
ieee80211_new_state(struct ieee80211com *ic, int state, int arg)
{
    (void)ic;
    (void)arg;
    transitions++;
    next_state = state;
}
static void
dispatch(struct ieee80211com *ic, struct event *e)
{
    struct bwfm_softc softc = { 0 }, *sc = &softc;
    (void)sc;
    switch (ntohl(e->msg.event_type)) {
#include "events.h"
    default: break;
    }
}
static void
check_event(const char *name, unsigned event, unsigned flags, unsigned reason,
    int state, bool current_peer, int expected)
{
    struct ieee80211_node node = { { 2, 0, 0, 0, 0, 1 } };
    struct ieee80211com ic = { state, IEEE80211_M_STA, &node };
    struct event e = { 0 };
    e.msg.event_type = htonl(event);
    e.msg.flags = htons(flags);
    e.msg.reason = htonl(reason);
    memcpy(e.msg.addr, node.ni_bssid, 6);
    if (!current_peer)
        e.msg.addr[5]++;
    transitions = 0;
    next_state = -1;
    dispatch(&ic, &e);
    checks++;
    if (transitions != (unsigned)(expected >= 0) || next_state != expected) {
        fprintf(stderr, "FAIL: %s: transitions=%u state=%d, expected=%d\n",
            name, transitions, next_state, expected);
        failures++;
    }
}
int
main(void)
{
    const unsigned disconnects[] = { BWFM_E_LINK, BWFM_E_DEAUTH,
        BWFM_E_DEAUTH_IND, BWFM_E_DISASSOC, BWFM_E_DISASSOC_IND };
    struct ieee80211com ic = { IEEE80211_S_RUN, IEEE80211_M_STA, NULL };
    struct event e = { 0 };
    uint8_t mask[32] = { 0 };
    unsigned i;
    int state;
    for (i = 0; i < sizeof(disconnects) / sizeof(disconnects[0]); i++) {
        for (state = IEEE80211_S_INIT; state <= IEEE80211_S_RUN; state++) {
            check_event("current-peer disconnect", disconnects[i], 0, 0,
                state, true, state > IEEE80211_S_SCAN ? IEEE80211_S_SCAN : -1);
            check_event("late previous-peer disconnect", disconnects[i], 0, 8,
                state, false, -1);
        }
    }
    check_event("link up", BWFM_E_LINK, 1, 0, IEEE80211_S_RUN, true, -1);
    check_event("link up with ancillary flags", BWFM_E_LINK, 3, 0,
        IEEE80211_S_RUN, true, -1);
    check_event("reason is not link direction", BWFM_E_LINK, 1, 1,
        IEEE80211_S_RUN, true, -1);
    check_event("current join completes", BWFM_E_SET_SSID, 0, 0,
        IEEE80211_S_AUTH, true, IEEE80211_S_RUN);
    check_event("late join cannot revive stopped interface", BWFM_E_SET_SSID,
        0, 0, IEEE80211_S_INIT, true, -1);
    event_mask(&ic, mask);
    for (i = 0; i < sizeof(disconnects) / sizeof(disconnects[0]); i++) {
        unsigned event = disconnects[i];
        checks++;
        if ((mask[event / 8] & (1U << (event % 8))) == 0) {
            fprintf(stderr, "FAIL: station did not subscribe to event %u\n", event);
            failures++;
        }
        e.msg.event_type = htonl(event);
        transitions = 0;
        dispatch(&ic, &e);
        checks++;
        if (transitions != 0) {
            fprintf(stderr, "FAIL: event %u dereferences absent BSS\n", event);
            failures++;
        }
    }
    ic.ic_opmode = IEEE80211_M_HOSTAP;
    for (i = 0; i < sizeof(disconnects) / sizeof(disconnects[0]); i++) {
        e.msg.event_type = htonl(disconnects[i]);
        transitions = 0;
        dispatch(&ic, &e);
        checks++;
        if (transitions != 0) {
            fprintf(stderr, "FAIL: station recovery entered AP mode\n");
            failures++;
        }
    }
    if (failures != 0) {
        fprintf(stderr, "FAIL: %u/%u link-event checks\n", failures, checks);
        return EXIT_FAILURE;
    }
    printf("PASS: %u production station link-event checks\n", checks);
    return EXIT_SUCCESS;
}
C
${CC:-cc} -std=c99 -Wall -Wextra -Werror ${CFLAGS:-} -I"$work" \
    "$work/check.c" -o "$work/check"
"$work/check"
