/* Origin: EmberBSD (AI-assisted). */
/* Run the rtw89 beacon-state hook with the net80211 watchdog contract.
 * net80211 arms if_timer when it sends AUTH or ASSOC and expects
 * ieee80211_watchdog() to move a station without a reply back to SCAN.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

enum ieee80211_state {
    IEEE80211_S_INIT, IEEE80211_S_SCAN, IEEE80211_S_AUTH,
    IEEE80211_S_ASSOC, IEEE80211_S_RUN
};
enum ieee80211_opmode { IEEE80211_M_STA, IEEE80211_M_HOSTAP };
struct ifnet { int if_timer; };
struct ieee80211com {
    struct ifnet *ic_ifp;
    enum ieee80211_opmode ic_opmode;
    enum ieee80211_state ic_state;
    int ic_mgt_timer;
};
struct rtw89_softc {
    struct ieee80211com sc_ic;
    bool sc_running;
    bool sc_beacon_active;
    uint32_t sc_beacon_tick;
};
static uint32_t getticks(void) { return 42; }

#include "beacon-state.inc"

/* Model of the shared ieee80211_watchdog() management-timer path. */
static void
watchdog(struct ieee80211com *ic)
{
    ic->ic_ifp->if_timer = 0;
    if (ic->ic_mgt_timer && --ic->ic_mgt_timer == 0)
        ic->ic_state = IEEE80211_S_SCAN;
    if (ic->ic_mgt_timer != 0)
        ic->ic_ifp->if_timer = 1;
}

static void
check_timeout(enum ieee80211_state st)
{
    struct ifnet ifp = { 0 };
    struct rtw89_softc sc = { .sc_running = true, .sc_beacon_active = true };
    struct ieee80211com *ic = &sc.sc_ic;
    int ticks;

    ic->ic_ifp = &ifp;
    ic->ic_opmode = IEEE80211_M_STA;
    /* net80211 sent the frame and armed its timeout before the hook. */
    ic->ic_state = st;
    ic->ic_mgt_timer = 3;
    ifp.if_timer = 1;
    rtw89_beacon_state(&sc, st);
    assert(!sc.sc_beacon_active);
    for (ticks = 0; ifp.if_timer != 0 && ticks < 10; ticks++)
        watchdog(ic);
    /* No reply: the station must leave AUTH/ASSOC and scan again. */
    assert(ic->ic_state == IEEE80211_S_SCAN);
}

int
main(void)
{
    struct ifnet ifp = { 0 };
    struct rtw89_softc sc = { .sc_running = true };

    check_timeout(IEEE80211_S_AUTH);
    check_timeout(IEEE80211_S_ASSOC);
    /* RUN still arms the driver beacon-loss watchdog. */
    sc.sc_ic.ic_ifp = &ifp;
    sc.sc_ic.ic_opmode = IEEE80211_M_STA;
    rtw89_beacon_state(&sc, IEEE80211_S_RUN);
    assert(sc.sc_beacon_active && ifp.if_timer == 1 && sc.sc_beacon_tick == 42);
    puts("rtwx watchdog: AUTH and ASSOC time out to SCAN");
    return 0;
}
