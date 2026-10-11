#!/bin/sh
# Origin: EmberBSD silent firmware disconnect regression, 2026-10-11.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/bwfm-link-check.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
for fn in bwfm_watchdog bwfm_newstate bwfm_newstate_cb bwfm_link_check_cb; do
    awk -v fn="$fn" '$0 ~ "^" fn "\\(" {copy=1}
        copy {print} copy && /^}/ {exit}' "$src/sys/dev/ic/bwfm.c" > "$work/$fn.h"
done
# The old driver has no worker: keep the old watchdog executable for the red run.
if [ ! -s "$work/bwfm_link_check_cb.h" ]; then
    printf 'bwfm_link_check_cb(struct bwfm_softc *sc, uint32_t gen) { (void)sc; (void)gen; }\n' > "$work/bwfm_link_check_cb.h"
fi
cat > "$work/check.c" <<'C'
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#define IFF_UP 1
#define IFF_RUNNING 2
#define BWFM_STA_ASSOC 0x10
#define BWFM_C_DISASSOC 52
#define ETHER_ADDR_LEN 6
#define PR_NOWAIT 0
#define if_oerrors 0
#define DEVNAME(sc) "test-bwfm"
#define DPRINTF(x) do { } while (0)
#define IEEE80211_ADDR_EQ(a, b) (memcmp((a), (b), 6) == 0)
#define IEEE80211_ADDR_COPY(a, b) memcpy((a), (b), 6)
#define le16toh(x) (x)
#define le32toh(x) (x)
enum ieee80211_state { IEEE80211_S_INIT, IEEE80211_S_SCAN,
    IEEE80211_S_AUTH, IEEE80211_S_ASSOC, IEEE80211_S_RUN };
enum { IEEE80211_M_STA, IEEE80211_M_HOSTAP };
enum { BWFM_TASK_NEWSTATE, BWFM_TASK_LINK_CHECK };
struct ifnet { void *if_softc; int if_timer, if_flags; };
struct ieee80211_node { uint8_t ni_bssid[6], ni_macaddr[6]; };
struct ieee80211com { struct ifnet *ic_ifp; struct ieee80211_node *ic_bss;
    enum ieee80211_state ic_state; int ic_opmode; };
struct bwfm_cmd_newstate { enum ieee80211_state state; int arg; };
struct work { int unused; };
struct bwfm_softc {
    struct ifnet sc_if; struct ieee80211com sc_ic;
    void *sc_taskq, *sc_freetask;
    int sc_tx_timer, sc_link_timer;
    bool sc_link_pending;
    uint32_t sc_link_generation, sc_link_checks, sc_link_recoveries;
    unsigned sc_link_misses, sc_link_errors;
    int (*sc_newstate)(struct ieee80211com *, enum ieee80211_state, int);
};
struct bwfm_task { struct work t_work; struct bwfm_softc *t_sc; int t_cmd;
    uint32_t t_link_generation; struct bwfm_cmd_newstate t_newstate; };
struct bwfm_sta_info { uint16_t ver, len; uint32_t flags;
    uint8_t ea[6]; uint32_t tx_tot_pkts; };
static struct bwfm_task jobs[128], *queued[128];
static struct bwfm_sta_info response;
static unsigned allocated, head, tail, queries, transitions, checks, failures;
static bool fail_alloc, in_watchdog;
static int query_error, mutate;
static void bwfm_link_check_cb(struct bwfm_softc *, uint32_t);
static void check(bool pass, const char *why)
{ checks++; if (!pass) { fprintf(stderr, "FAIL: %s\n", why); failures++; } }
static void *pool_cache_get(void *pool, int flags)
{ (void)pool; (void)flags; return fail_alloc ? NULL : &jobs[allocated++]; }
static void workqueue_enqueue(void *q, struct work *w, void *arg)
{ (void)q; (void)arg; queued[tail++] = (void *)w; }
static void if_statinc(struct ifnet *ifp, int stat) { (void)ifp; (void)stat; }
static void ieee80211_watchdog(struct ieee80211com *ic) { (void)ic; }
static int splnet(void) { return 0; }
static void splx(int s) { (void)s; }
static void bwfm_scan(struct bwfm_softc *sc) { (void)sc; }
static void bwfm_connect(struct bwfm_softc *sc) { (void)sc; }
static int bwfm_fwvar_cmd_set_data(struct bwfm_softc *sc, int c, void *p, size_t n)
{ (void)sc; (void)c; (void)p; (void)n; return 0; }
static int saved_newstate(struct ieee80211com *ic, enum ieee80211_state s, int a)
{ (void)a; ic->ic_state = s; transitions++; return 0; }
int bwfm_fwvar_var_get_data(struct bwfm_softc *sc, const char *name,
    void *data, size_t len)
{
    check(!in_watchdog, "firmware I/O runs in worker, never watchdog");
    check(strcmp(name, "sta_info") == 0, "query firmware association, not IP reachability");
    check(len == sizeof(response), "bounded station response");
    queries++;
    memcpy(data, &response, len);
    if (mutate == 1) sc->sc_link_generation++;
    if (mutate == 2) sc->sc_ic.ic_bss->ni_bssid[5]++;
    if (mutate == 3) sc->sc_if.if_flags = 0;
    if (mutate == 4) sc->sc_ic.ic_state = IEEE80211_S_SCAN;
    return query_error;
}
int
#include "bwfm_newstate.h"
static int ieee80211_new_state(struct ieee80211com *ic, enum ieee80211_state s, int a)
{ return bwfm_newstate(ic, s, a); }
void
#include "bwfm_newstate_cb.h"
void
#include "bwfm_watchdog.h"
static void
#include "bwfm_link_check_cb.h"
static void drain(void)
{
    while (head != tail) {
        struct bwfm_task *t = queued[head++];
        if (t->t_cmd == BWFM_TASK_LINK_CHECK)
            bwfm_link_check_cb(t->t_sc, t->t_link_generation);
        else
            bwfm_newstate_cb(t->t_sc, &t->t_newstate);
    }
}
static void ticks(struct bwfm_softc *sc, unsigned n)
{
    while (n--) { in_watchdog = true; bwfm_watchdog(&sc->sc_if); in_watchdog = false; }
}
static void fixture(struct bwfm_softc *sc, struct ieee80211_node *bss)
{
    memset(sc, 0, sizeof(*sc)); memset(bss, 0, sizeof(*bss));
    bss->ni_bssid[0] = bss->ni_macaddr[0] = 2;
    memset(&response, 0, sizeof(response));
    response.ver = 4; response.len = sizeof(response);
    response.flags = BWFM_STA_ASSOC;
    memcpy(response.ea, bss->ni_bssid, 6);
    allocated = head = tail = queries = transitions = 0;
    fail_alloc = in_watchdog = false; query_error = mutate = 0;
    sc->sc_if.if_softc = sc; sc->sc_if.if_flags = IFF_UP | IFF_RUNNING;
    sc->sc_ic.ic_ifp = &sc->sc_if; sc->sc_ic.ic_bss = bss;
    sc->sc_ic.ic_opmode = IEEE80211_M_STA;
    sc->sc_taskq = sc; sc->sc_freetask = sc; sc->sc_newstate = saved_newstate;
    ieee80211_new_state(&sc->sc_ic, IEEE80211_S_RUN, -1); drain(); transitions = 0;
}
static void sample(struct bwfm_softc *sc) { ticks(sc, 5); drain(); }
int main(void)
{
    struct bwfm_softc sc; struct ieee80211_node bss; unsigned i;
    fixture(&sc, &bss);
    check(sc.sc_if.if_timer == 1, "entering RUN arms watchdog without traffic");
    ticks(&sc, 4); check(head == tail, "no premature poll");
    ticks(&sc, 1); check(tail == head + 1, "one poll after five ticks");
    ticks(&sc, 20); check(tail == head + 1, "slow worker cannot build a poll backlog");
    drain(); check(queries == 1 && transitions == 0, "associated peer remains connected");
    response.flags = 0;
    sample(&sc); check(transitions == 0, "one lost association sample is insufficient");
    sample(&sc); check(sc.sc_ic.ic_state == IEEE80211_S_SCAN &&
        sc.sc_link_recoveries == 1, "silent firmware loss starts host recovery");
    ticks(&sc, 20); drain(); check(sc.sc_link_recoveries == 1, "no duplicate recovery while scanning");
    for (i = 0; i < 11; i++) {
        fixture(&sc, &bss); response.flags = 0; sample(&sc);
        switch (i) {
        case 0: query_error = EIO; break;
        case 1: response.ver = 2; break;
        case 2: response.len = offsetof(struct bwfm_sta_info, ea) + 5; break;
        case 3: response.ea[5]++; break;
        case 4: response.flags = BWFM_STA_ASSOC; break;
        case 5: response.ver = 8; break;
        case 6: response.len = sizeof(response) + 1; break;
        default: mutate = i - 6; break;
        }
        sample(&sc); check(transitions == 0, "invalid, recovered or changed connection cannot disconnect");
        query_error = mutate = 0; response.ver = 4; response.len = sizeof(response);
        memcpy(response.ea, bss.ni_bssid, 6); response.flags = 0;
        sample(&sc); check(transitions == 0, "interrupted negative sequence must start over");
    }
    for (i = 0; i < 6; i++) {
        fixture(&sc, &bss);
        switch (i) {
        case 0: sc.sc_if.if_flags &= ~IFF_UP; break;
        case 1: sc.sc_if.if_flags &= ~IFF_RUNNING; break;
        case 2: sc.sc_ic.ic_state = IEEE80211_S_AUTH; break;
        case 3: sc.sc_ic.ic_opmode = IEEE80211_M_HOSTAP; break;
        case 4: sc.sc_ic.ic_bss = NULL; break;
        case 5: sc.sc_taskq = NULL; break;
        }
        sample(&sc); check(queries == 0, "inactive or unsupported state does not query firmware");
    }
    fixture(&sc, &bss); fail_alloc = true; sample(&sc);
    check(!sc.sc_link_pending, "allocation failure does not wedge pending flag");
    fail_alloc = false; sample(&sc); check(queries == 1, "allocation failure is retried");
    fixture(&sc, &bss); ticks(&sc, 5);
    ieee80211_new_state(&sc.sc_ic, IEEE80211_S_INIT, -1); drain();
    check(queries == 0 && !sc.sc_link_pending, "queued poll invalidated before stop is applied");
    fixture(&sc, &bss); response.flags = 0; sample(&sc); ticks(&sc, 5);
    fail_alloc = true; drain();
    check(sc.sc_link_recoveries == 0 && sc.sc_ic.ic_state == IEEE80211_S_RUN,
        "failed transition allocation is not reported as recovery");
    fail_alloc = false; sample(&sc); sample(&sc);
    check(sc.sc_link_recoveries == 1, "failed recovery transition is retried");
    printf("%s: %u production link-check assertions\n", failures ? "FAIL" : "PASS", checks);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
C
${CC:-cc} -std=c99 -Wall -Wextra -Werror ${CFLAGS:-} -I"$work" "$work/check.c" -o "$work/check"
"$work/check"
