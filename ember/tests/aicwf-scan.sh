#!/bin/sh
# Exercise user-requested scans through the actual net80211 and AIC state code.
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/aicwf-scan.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
awk '/^ieee80211_setupscan\(/ {copy=1; print "static int"}
copy {print} copy && /^}/ {exit}' \
    "$src/sys/net80211/ieee80211_ioctl.c" > "$work/setup.h"
awk '/^aicwf_newstate_cb\(/ {copy=1; print "static void"}
copy {print} copy && /^}/ {exit}' \
    "$src/sys/dev/sdmmc/if_aicwf.c" > "$work/state.h"
cat > "$work/check.c" <<'C'
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
typedef uint8_t u_int8_t;
enum ieee80211_state {
    IEEE80211_S_INIT, IEEE80211_S_SCAN, IEEE80211_S_AUTH,
    IEEE80211_S_ASSOC, IEEE80211_S_RUN
};
#define IEEE80211_F_SCAN 1
#define AICWF_SCAN_INTERVAL_MS 3000
#define IS_UP(ic) ((ic)->up)
struct ieee80211com {
    enum ieee80211_state ic_state;
    int ic_flags, up;
    u_int8_t ic_chan_active[32];
    void *ic_bss;
};
struct aicwf_softc {
    struct ieee80211com sc_ic;
    bool sc_connected, sc_connecting, sc_scanning;
    int sc_scan_ticks;
    int (*sc_newstate)(struct ieee80211com *, enum ieee80211_state, int);
};
static int leaves, scans, disconnects;
static int splnet(void) { return 0; }
static void splx(int s) { (void)s; }
static int mstohz(int n) { return n; }
static int getticks(void) { return 10000; }
static void kpause(const char *w, bool intr, int ticks, void *lock)
{ (void)w; (void)intr; (void)ticks; (void)lock; }
static void ieee80211_sta_leave(struct ieee80211com *ic, void *node)
{ assert(ic->ic_bss == node); leaves++; }
static void aicwf_disconnect(struct aicwf_softc *sc)
{
    if (sc->sc_connected || sc->sc_connecting) disconnects++;
    sc->sc_connected = sc->sc_connecting = false;
}
static void aicwf_scan(struct aicwf_softc *sc)
{ sc->sc_scanning = true; scans++; }
/* The relevant generic transition: RUN leaves; INIT begins a fresh scan. */
static int net80211_state(struct ieee80211com *ic,
    enum ieee80211_state state, int arg)
{
    (void)arg;
    if (ic->ic_state == IEEE80211_S_RUN &&
        (state == IEEE80211_S_SCAN || state == IEEE80211_S_INIT))
        ieee80211_sta_leave(ic, ic->ic_bss);
    ic->ic_state = state;
    return 0;
}
#include "setup.h"
#include "state.h"
static void check(bool user_scan, bool connected, enum ieee80211_state old,
    int expected_leaves)
{
    struct aicwf_softc sc = {0};
    u_int8_t channels[32] = {1};
    leaves = scans = disconnects = 0;
    sc.sc_ic.ic_state = old;
    sc.sc_ic.ic_flags = IEEE80211_F_SCAN;
    sc.sc_ic.up = 1;
    sc.sc_ic.ic_bss = &sc;
    sc.sc_connected = connected;
    sc.sc_newstate = net80211_state;
    if (user_scan) {
        assert(ieee80211_setupscan(&sc.sc_ic, channels) == 0);
        assert(sc.sc_ic.ic_state == IEEE80211_S_INIT);
    }
    aicwf_newstate_cb(&sc, IEEE80211_S_SCAN, -1);
    assert(sc.sc_ic.ic_state == IEEE80211_S_SCAN);
    assert(scans == 1);
    assert(disconnects == (connected ? 1 : 0));
    assert(leaves == expected_leaves);
}
int main(void)
{
    check(true, true, IEEE80211_S_RUN, 1);
    check(false, true, IEEE80211_S_RUN, 1);
    check(true, false, IEEE80211_S_INIT, 0);
    check(false, false, IEEE80211_S_AUTH, 0);
    check(false, false, IEEE80211_S_SCAN, 0);
    puts("PASS: connected scans notify departure once; initial/repeated scans do not");
    return 0;
}
C
${CC:-cc} -std=c99 -Wall -Wextra -Werror "$work/check.c" -o "$work/check"
"$work/check"
