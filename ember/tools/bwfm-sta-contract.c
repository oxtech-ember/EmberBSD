/* SPDX-License-Identifier: BSD-2-Clause */
/* Fixtures for current bwfm station functions extracted by bwfm-sta.sh. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#define IEEE80211_ADDR_LEN 6
#define IEEE80211_ELEMID_RSN 48
#define IEEE80211_ADDR_EQ(a,b) (memcmp((a),(b),6) == 0)
#define IEEE80211_ADDR_COPY(a,b) memcpy((a),(b),6)
#define IEEE80211_CHAN_ANYC NULL
#define IFF_RUNNING 1
#define KM_SLEEP 0
#define BWFM_C_GET_VAR 262
#define explicit_memset memset
#define PR_NOWAIT 0
#define BWFM_TASK_STA_MGMT 5
#define BRCM_CC_4345_CHIP_ID 4345
#define RTM_IEEE80211_STA_TX 1
#define RTM_IEEE80211_STA_SCAN 2
#define BWFM_ESCAN_REQ_VERSION 1
#define WL_ESCAN_ACTION_ABORT 3
#ifndef __NetBSD__
#define htole16(x) (x)
#define htole32(x) (x)
#define le32toh(x) (x)
static uint16_t le16dec(const void *p)
{ const uint8_t *b = p; return (uint16_t)(b[0] | (uint16_t)b[1] << 8); }
static uint32_t le32dec(const void *p)
{ const uint8_t *b = p; return b[0] | (uint32_t)b[1]<<8 | (uint32_t)b[2]<<16 | (uint32_t)b[3]<<24; }
static void le16enc(void *p, uint16_t v)
{ uint8_t *b = p; b[0] = (uint8_t)v; b[1] = (uint8_t)(v>>8); }
static void le32enc(void *p, uint32_t v)
{ uint8_t *b = p; b[0] = (uint8_t)v; b[1] = (uint8_t)(v>>8); b[2] = (uint8_t)(v>>16); b[3] = (uint8_t)(v>>24); }
#endif
#include "abi.h"
#include "flags.h"
#include "ieee80211_rsn.h"
#include "bwfm_sta.h"
enum { IEEE80211_M_STA, IEEE80211_M_HOSTAP };
enum { IEEE80211_S_INIT, IEEE80211_S_SCAN, IEEE80211_S_AUTH,
    IEEE80211_S_ASSOC, IEEE80211_S_RUN };
struct ifnet { void *if_softc; unsigned if_flags; };
struct ieee80211_channel { uint16_t ic_freq; };
struct ieee80211_node { uint8_t ni_bssid[6]; struct ieee80211_channel *ni_chan; };
struct ieee80211com {
    struct ifnet *ic_ifp;
    struct ieee80211_node *ic_bss;
    unsigned ic_flags;
    const uint8_t *ic_opt_ie;
    uint16_t ic_opt_ie_len;
    int ic_state, ic_opmode;
    struct { unsigned nt_scan_epoch; } ic_scan;
    uint8_t ic_myaddr[6];
};
struct bwfm_softc;
struct bwfm_proto_ops { int (*proto_query_dcmd)(struct bwfm_softc *, int, unsigned, void *, size_t *); };
struct bwfm_softc {
    struct ieee80211com sc_ic;
    struct ifnet sc_if;
    struct { unsigned ch_chip; } sc_chip;
    bool sc_detaching, sc_sae_enabled, sc_sta_enabled;
    bool sc_sta_wnm_maybe_enabled, sc_sta_tx_pending, sc_sta_bgscan;
    uint32_t sc_sta_generation, sc_sta_scan_generation, sc_sta_packetid;
    uint16_t sc_scan_id, sc_sta_assoc_len;
    uint8_t sc_sta_assoc[1018];
    struct ieee80211req_sta_mgmt sc_sta_tx, sc_sta_scan;
    void *sc_freetask, *sc_taskq; int sc_control_lock; const struct bwfm_proto_ops *sc_proto_ops;
};
struct work { int unused; };
struct bwfm_task {
    struct work t_work;
    struct bwfm_softc *t_sc;
    unsigned t_cmd;
    struct ieee80211req_sta_mgmt t_sta;
};
struct bwfm_escan_params { uint32_t version; uint16_t action, sync_id; };
static struct bwfm_task jobs[64], *queued[64];
static unsigned allocated, head, tail, checks, failures;
static unsigned scans, aborts, tx_events, scan_events, tx_commands, wnm_commands, stops;
static int fw_error, allocation_mutation, firmware_mutation, scan_mutation;
static bool fail_task;
static int wnm_get_error;
static uint32_t wnm_value;
static bool wnm_wrong_value;
static uint32_t assoc_req_len, assoc_resp_len, assoc_flags;
static int assoc_error, assoc_mutation;
static int reply_delta;
static const char *reply_name;
static bool control_locked;
static void *checked_buffer;
static struct bwfm_softc *active;
static struct ieee80211req_sta_mgmt last_tx, last_scan;
static const uint8_t psk_rsn[] = {48,20,1,0,0,15,172,4,1,0,0,15,172,4,1,0,0,15,172,2,0,0};
static const uint8_t sae_rsn[] = {48,20,1,0,0,15,172,4,1,0,0,15,172,4,1,0,0,15,172,8,0xc0,0};
static void check(bool pass, const char *why)
{ checks++; if (!pass) { fprintf(stderr,"FAIL: %s\n",why); failures++; } }
static int splnet(void) { return 0; }
static void splx(int s) { (void)s; }
static void *pool_cache_get(void *p, int flags)
{
    (void)p; (void)flags;
    if (fail_task) return NULL;
    if (allocated == 64) abort();
    return &jobs[allocated++];
}
static void workqueue_enqueue(void *q, struct work *w, void *arg)
{ (void)q; (void)arg; if (tail == 64) abort(); queued[tail++] = (void *)w; }
static void *kmem_zalloc(size_t n, int flags)
{
    void *p;
    (void)flags;
    if (allocation_mutation == 1) active->sc_sta_tx_pending = false;
    if (allocation_mutation == 2) active->sc_sta_packetid++;
    if (allocation_mutation == 3) active->sc_sta_generation++;
    if (allocation_mutation == 4) active->sc_ic.ic_bss->ni_bssid[5]++;
    p = calloc(1,n);
    if (p == NULL) abort();
    return p;
}
static void kmem_free(void *p, size_t n)
{
    if (p == checked_buffer) {
        const uint8_t *data = p; size_t i; bool wiped = true;
        for (i = 0; i < n; i++) if (data[i] != 0) wiped = false;
        check(wiped, "checked GET buffer is wiped before release");
        checked_buffer = NULL;
    }
    free(p);
}
static void mutex_enter(int *lock) { (void)lock; check(!control_locked,"GET control lock is not recursive"); control_locked = true; }
static void mutex_exit(int *lock) { (void)lock; check(control_locked,"GET unlocks acquired control lock"); control_locked = false; }

static unsigned ieee80211_chan2ieee(struct ieee80211com *ic, const struct ieee80211_channel *c)
{ (void)ic; return (c->ic_freq - 5000)/5; }
static void rt_ieee80211msg(struct ifnet *ifp, int type, const void *v, size_t n)
{
    (void)ifp;
    check(type == RTM_IEEE80211_STA_TX || type == RTM_IEEE80211_STA_SCAN, "only versioned station completion is emitted here");
    check(n == offsetof(struct ieee80211req_sta_mgmt,data), "TX event has no uninitialized payload");
    if (type == RTM_IEEE80211_STA_TX) { memcpy(&last_tx,v,n); tx_events++; } else { memcpy(&last_scan,v,n); scan_events++; }
}
static void ieee80211_cancel_scan(struct ieee80211com *ic) { ic->ic_flags &= ~IEEE80211_F_SCAN; }
static void bwfm_stop(struct ifnet *ifp, int disable)
{ (void)disable; stops++; ifp->if_flags &= ~IFF_RUNNING; }
static int bwfm_fwvar_var_set_int(const struct bwfm_softc *sc, const char *name, uint32_t v)
{
    check(strcmp(name,"wnm") == 0, "expected WNM control only");
    check(!v || sc->sc_sta_wnm_maybe_enabled, "record possible enable before firmware can sleep");
    wnm_commands++; wnm_value = v;
    return fw_error;
}
static int bwfm_fwvar_var_get_int(struct bwfm_softc *sc, const char *name, uint32_t *v)
{
    (void)sc; check(strcmp(name,"wnm") == 0, "read back WNM control");
    *v = wnm_value ^ (wnm_wrong_value ? 1U : 0U);
    return wnm_get_error;
}
static int bwfm_fwvar_var_get_data(struct bwfm_softc *sc, const char *name, void *data, size_t n)
{
    uint8_t *p = data;
    if (strcmp(name,"assoc_info") == 0) {
        check(n == 28, "association metadata request is bounded");
        le32enc(p,assoc_req_len); le32enc(p+4,assoc_resp_len); le32enc(p+8,assoc_flags);
        return assoc_error == 1 ? EIO : 0;
    }
    memset(p,0,n);
    if (strcmp(name,"assoc_req_ies") == 0) {
        check(n == assoc_req_len - (assoc_flags ? 10u : 4u), "request fixed header is not copied as IE");
        if (assoc_mutation == 1) p[1] = 255;
        return assoc_error == 2 ? EIO : 0;
    }
    check(strcmp(name,"assoc_resp_ies") == 0, "expected association response IE query");
    check(n == assoc_resp_len - 6, "response fixed header is not copied as IE");
    if (assoc_mutation == 2) p[1] = 255;
    if (assoc_mutation == 3) sc->sc_sta_generation++;
    return assoc_error == 3 ? EIO : 0;
}
static int proto_query(struct bwfm_softc *sc, int ifidx, unsigned command, void *data, size_t *len)
{
    char name[32]; size_t expected; int error;
    check(control_locked && ifidx == 0 && command == BWFM_C_GET_VAR,"checked GET uses serialized firmware query");
    check(strlen(data) < sizeof(name),"GET name fits fixture boundary");
    strcpy(name,data); expected = *len - strlen(name) - 1; checked_buffer = data;
    if (strcmp(name,"wnm") == 0) {
        uint32_t value;
        check(expected == 4,"WNM readback requires four bytes");
        error = bwfm_fwvar_var_get_int(sc,name,&value); le32enc(data,value);
    } else error = bwfm_fwvar_var_get_data(sc,name,data,expected);
    *len = expected;
    if (reply_name != NULL && strcmp(reply_name,name) == 0)
        *len = (size_t)((int)expected + reply_delta);
    return error;
}
static const struct bwfm_proto_ops protocol = { proto_query };
static int bwfm_fwvar_var_set_data

(struct bwfm_softc *sc, const char *name, const void *data, size_t n)
{
    if (strcmp(name,"escan") == 0) {
        const struct bwfm_escan_params *p = data;
        check(!sc->sc_sta_bgscan, "cancel invalidates work before firmware I/O");
        check(n == sizeof(*p) && p->action == WL_ESCAN_ACTION_ABORT, "bounded scan abort message");
        aborts++;
    } else {
        const uint8_t *p = data;
        check(strcmp(name,"actframe") == 0, "expected action frame command");
        check(n >= 28 && n == 28u + le16dec(p+22), "action frame length matches allocated buffer");
        check(le32dec(p+24) == sc->sc_sta_packetid, "wire packet ID matches pending transaction");
        check(IEEE80211_ADDR_EQ(p+8, sc->sc_ic.ic_bss->ni_bssid) &&
            IEEE80211_ADDR_EQ(p+16, sc->sc_ic.ic_bss->ni_bssid), "wire addresses match current BSSID");
        tx_commands++;
        if (firmware_mutation == 1) sc->sc_sta_packetid++;
        if (firmware_mutation == 2) sc->sc_sta_tx_pending = false;
    }
    return fw_error;
}
static int bwfm_scan(struct bwfm_softc *sc, const uint16_t *freqs, size_t n)
{
    (void)freqs; (void)n; scans++;
    if (scan_mutation) { sc->sc_sta_scan_generation++; sc->sc_sta_bgscan = true; }
    return fw_error;
}
static int
#include "bwfm_fwvar_var_get_checked.h"
static bool
#include "bwfm_sta_security.h"
static int
#include "bwfm_sta_wnm.h"
static void
#include "bwfm_sta_assoc_ies.h"
static bool
#include "bwfm_sta_ready.h"
static void
#include "bwfm_sta_tx_done.h"
static void
#include "bwfm_sta_scan_done.h"
static void
#include "bwfm_sta_scan_cancel.h"
static int
#include "bwfm_sta_mgmt.h"
static void
#include "bwfm_sta_mgmt_cb.h"
static void fixture(struct bwfm_softc *sc, struct ieee80211_node *bss,
    struct ieee80211_channel *channel)
{
    memset(sc,0,sizeof(*sc)); memset(bss,0,sizeof(*bss));
    memset(jobs,0,sizeof(jobs)); memset(&last_tx,0,sizeof(last_tx));
    allocated = head = tail = scans = aborts = tx_events = scan_events = 0;
    tx_commands = wnm_commands = stops = 0;
    fw_error = allocation_mutation = firmware_mutation = scan_mutation = 0;
    assoc_req_len = 8; assoc_resp_len = 12; assoc_flags = 0; assoc_error = assoc_mutation = 0; reply_delta = 0; reply_name = NULL; control_locked = false; checked_buffer = NULL; sc->sc_proto_ops = &protocol; fail_task = false; wnm_get_error = 0; wnm_wrong_value = false; wnm_value = 0; active = sc;
    channel->ic_freq = 5300; bss->ni_chan = channel; bss->ni_bssid[0] = 2;
    sc->sc_if.if_softc = sc; sc->sc_if.if_flags = IFF_RUNNING;
    sc->sc_ic.ic_ifp = &sc->sc_if; sc->sc_ic.ic_bss = bss;
    sc->sc_ic.ic_flags = IEEE80211_F_WPA2;
    sc->sc_ic.ic_opt_ie = psk_rsn; sc->sc_ic.ic_opt_ie_len = sizeof(psk_rsn);
    sc->sc_ic.ic_opmode = IEEE80211_M_STA; sc->sc_ic.ic_state = IEEE80211_S_RUN;
    sc->sc_chip.ch_chip = BRCM_CC_4345_CHIP_ID;
    sc->sc_sta_enabled = true; sc->sc_sta_generation = 40;
    sc->sc_sta_scan_generation = 60; sc->sc_sta_packetid = 100;
}
static struct ieee80211req_sta_mgmt request(struct bwfm_softc *sc, unsigned op)
{
    struct ieee80211req_sta_mgmt r;
    memset(&r,0,sizeof(r)); r.version = IEEE80211_STA_VERSION; r.op = (uint16_t)op;
    r.generation = sc->sc_sta_generation; r.cookie = 42;
    r.freq = sc->sc_ic.ic_bss->ni_chan->ic_freq;
    memcpy(r.bssid,sc->sc_ic.ic_bss->ni_bssid,6);
    if (op == IEEE80211_STA_TX) {
        r.len = 5; r.data[0] = 10; r.data[1] = 8; r.data[2] = 1;
        r.data[3] = 1;
    }
    return r;
}
static void drain(void)
{
    while (head != tail) {
        struct bwfm_task *t = queued[head++];
        bwfm_sta_mgmt_cb(t->t_sc,&t->t_sta);
    }
}
static void security_tests(struct bwfm_softc *sc)
{
    uint8_t rsn[sizeof(sae_rsn)]; size_t i;
    check(bwfm_sta_security(sc), "selected WPA2 PSK CCMP without PMF is allowed");
    sc->sc_ic.ic_flags = IEEE80211_F_WPA1;
    check(!bwfm_sta_security(sc), "WPA1 cannot enable station actions");
    sc->sc_ic.ic_flags = IEEE80211_F_WPA2;
    for (i = 0; i < sizeof(psk_rsn); i++) {
        sc->sc_ic.ic_opt_ie_len = i;
        check(!bwfm_sta_security(sc), "truncated selected WPA2 RSN is rejected");
    }
    sc->sc_ic.ic_opt_ie = rsn; sc->sc_ic.ic_opt_ie_len = sizeof(rsn);
    for (i = 0; i < 8; i++) {
        memcpy(rsn,psk_rsn,sizeof(rsn));
        switch (i) {
        case 0: rsn[7] = 2; break;
        case 1: rsn[13] = 2; break;
        case 2: rsn[19] = 8; break;
        case 3: rsn[14] = 2; break;
        case 4: rsn[20] = 0x80; break;
        case 5: rsn[20] = 0x40; break;
        case 6: rsn[1] = 30; break;
        case 7: rsn[2] = 2; break;
        }
        check(!bwfm_sta_security(sc), "WPA2 gate rejects wrong cipher, AKM, count, PMF and framing");
    }
    sc->sc_sae_enabled = true; memcpy(rsn,sae_rsn,sizeof(rsn));
    check(bwfm_sta_security(sc), "selected SAE CCMP with required PMF is allowed");
    for (i = 0; i < sizeof(sae_rsn); i++) {
        sc->sc_ic.ic_opt_ie_len = i;
        check(!bwfm_sta_security(sc), "truncated SAE RSN is rejected");
    }
    sc->sc_ic.ic_opt_ie_len = sizeof(rsn);
    for (i = 0; i < 3; i++) {
        rsn[20] = (uint8_t)(i * 0x40);
        check(!bwfm_sta_security(sc), "SAE requires both PMF capability and requirement");
    }
    memcpy(rsn,psk_rsn,sizeof(rsn)); rsn[20] = 0xc0;
    check(!bwfm_sta_security(sc), "SAE mode cannot admit a PSK selection");
}
static void association_tests(struct bwfm_softc *sc, struct ieee80211_node *bss,
    struct ieee80211_channel *chan)
{
    unsigned i;
    uint8_t ies[] = {70, 1, 2, 127, 0};
    check(bwfm_sta_ies_valid(ies,sizeof(ies)), "complete association IE chain is accepted");
    check(!bwfm_sta_ies_valid(ies,1), "partial IE header is rejected");
    check(!bwfm_sta_ies_valid(ies,2), "partial IE body is rejected");
    check(!bwfm_sta_ies_valid(ies,4), "partial final IE header is rejected");
    for (i = 0; i < 2; i++) {
        fixture(sc,bss,chan); assoc_flags = i; assoc_req_len += i * 6;
        bwfm_sta_assoc_ies(sc);
        check(sc->sc_sta_assoc_len == 14 && le16dec(sc->sc_sta_assoc) == 4 &&
            le16dec(sc->sc_sta_assoc+2) == 6, "association/reassociation IE lengths exclude fixed headers");
    }
    for (i = 0; i < 12; i++) {
        fixture(sc,bss,chan); sc->sc_sta_assoc_len = 77;
        switch (i) {
        case 0: assoc_req_len = 3; break;
        case 1: assoc_resp_len = 5; break;
        case 2: assoc_req_len = 4; break;
        case 3: assoc_resp_len = 6; break;
        case 4: assoc_req_len = 2048; break;
        case 5: assoc_req_len = 1018; assoc_resp_len = 8; break;
        case 6: assoc_error = 1; break;
        case 7: assoc_error = 2; break;
        case 8: assoc_error = 3; break;
        case 9: assoc_mutation = 1; break;
        case 10: assoc_mutation = 2; break;
        case 11: assoc_mutation = 3; break;
        }
        bwfm_sta_assoc_ies(sc);
        check(sc->sc_sta_assoc_len == 0, "invalid/failed/stale association query cannot publish cached IEs");
    }
}
static void action_tests(void)

{
    uint8_t body[1025] = {10,7,1}; size_t i;
    uint8_t capabilities[] = {70, 1, 2, 127, 3, 0, 0, 8, 0, 1, 0};
    check(bwfm_sta_btm_requested(capabilities,sizeof(capabilities)), "selected Extended Capabilities advertises BTM");
    capabilities[7] = 0;
    check(!bwfm_sta_btm_requested(capabilities,sizeof(capabilities)), "RRM-only selection cannot enable firmware WNM");
    capabilities[7] = 8;
    check(!bwfm_sta_btm_requested(capabilities,sizeof(capabilities)-1), "truncated trailing IE invalidates advertised BTM");
    check(!bwfm_sta_btm_requested(capabilities,7), "truncated Extended Capabilities cannot advertise BTM");
    check(!bwfm_sta_btm_requested(capabilities,0), "empty IE selection cannot advertise BTM");
    capabilities[3] = 126;
    check(!bwfm_sta_btm_requested(capabilities,sizeof(capabilities)), "same bit in an unrelated IE cannot advertise BTM");

    for (i = 0; i < 7; i++) check(!bwfm_sta_action_valid(body,i,true), "truncated BTM request rejected");
    check(bwfm_sta_action_valid(body,7,true), "complete fixed BTM request accepted");
    check(!bwfm_sta_action_valid(body,sizeof(body),true), "oversized BTM request rejected");
    check(!bwfm_sta_action_valid(body,7,false), "request cannot be sent through response interface");
    body[1] = 8;
    check(!bwfm_sta_action_valid(body,5,false), "accepted BTM response needs target BSSID");
    check(bwfm_sta_action_valid(body,11,false), "accepted response includes target BSSID");
    body[3] = 1;
    check(bwfm_sta_action_valid(body,5,false), "rejected response needs no target");
    check(!bwfm_sta_action_valid(body,5,true), "response cannot masquerade as request");
    body[1] = 6;
    check(!bwfm_sta_action_valid(body,3,false) && bwfm_sta_action_valid(body,4,false), "BTM query requires reason field");
    body[0] = 5; body[1] = 4;
    check(bwfm_sta_action_valid(body,3,false) && !bwfm_sta_action_valid(body,3,true), "Neighbor Report request is TX only");
    body[1] = 5;
    check(bwfm_sta_action_valid(body,3,true) && !bwfm_sta_action_valid(body,3,false), "Neighbor Report response is RX only");
    body[1] = 1;
    check(bwfm_sta_action_valid(body,3,false) && !bwfm_sta_action_valid(body,3,true), "measurement report direction is bounded");
    body[0] = 0;
    check(!bwfm_sta_action_valid(body,100,true) && !bwfm_sta_action_valid(body,100,false), "unsupported categories rejected");
}
int main(void)
{
    struct bwfm_softc sc; struct ieee80211_node bss;
    struct ieee80211_channel chan; struct ieee80211req_sta_mgmt r, old;
    unsigned i; uint8_t readback[28];
    fixture(&sc,&bss,&chan); security_tests(&sc); action_tests(); association_tests(&sc,&bss,&chan);
        fixture(&sc,&bss,&chan); reply_name = "wnm"; reply_delta = -1;
    memset(readback,0xa5,sizeof(readback));
    check(bwfm_fwvar_var_get_checked(&sc,"wnm",readback,4) == EIO &&
        readback[0] == 0xa5 && readback[3] == 0xa5,"short WNM response does not expose stale destination bytes");
    check(bwfm_sta_wnm(&sc,true) == EIO && sc.sc_sta_wnm_maybe_enabled,
        "short enable readback keeps control state uncertain");
    reply_delta = 1;
    check(bwfm_fwvar_var_get_checked(&sc,"wnm",readback,4) == 0 && le32dec(readback) == 1,
        "transport padding after the required response is permitted");
    for (i = 0; i < 3; i++) {
        fixture(&sc,&bss,&chan); reply_delta = -1;
        reply_name = i == 0 ? "assoc_info" : i == 1 ? "assoc_req_ies" : "assoc_resp_ies";
        bwfm_sta_assoc_ies(&sc);
        check(sc.sc_sta_assoc_len == 0,"short firmware association response cannot publish IEs");
    }
    fixture(&sc,&bss,&chan); wnm_get_error = EIO; memset(readback,0xa5,sizeof(readback));
    check(bwfm_fwvar_var_get_checked(&sc,"wnm",readback,4) == EIO && readback[0] == 0xa5,
        "failed firmware GET preserves caller destination");
    fixture(&sc,&bss,&chan); fw_error = EIO;

    check(bwfm_sta_wnm(&sc,true) == EIO && sc.sc_sta_wnm_maybe_enabled, "uncertain enable remains potentially active");
    check(bwfm_sta_wnm(&sc,false) == EIO && sc.sc_sta_wnm_maybe_enabled, "failed disable cannot establish host control");
    fw_error = 0;
    check(bwfm_sta_wnm(&sc,false) == 0 && !sc.sc_sta_wnm_maybe_enabled, "confirmed disable clears uncertainty");
    check(wnm_commands == 3, "uncertain control operations are not skipped");
        fixture(&sc,&bss,&chan); wnm_get_error = EIO;
    check(bwfm_sta_wnm(&sc,true) == EIO && sc.sc_sta_wnm_maybe_enabled, "failed readback cannot establish enable state");
    check(bwfm_sta_wnm(&sc,false) == EIO && sc.sc_sta_wnm_maybe_enabled, "failed readback cannot establish disable state");
    wnm_get_error = 0; wnm_wrong_value = true;
    check(bwfm_sta_wnm(&sc,false) == EIO && sc.sc_sta_wnm_maybe_enabled, "wrong readback cannot establish host control");
    fixture(&sc,&bss,&chan); sc.sc_sta_wnm_maybe_enabled = true; fw_error = EIO;

    r = request(&sc,IEEE80211_STA_DISABLE);
    check(bwfm_sta_mgmt(&sc.sc_ic,&r) == EIO && stops == 1 && !sc.sc_sta_enabled &&
        !(sc.sc_if.if_flags & IFF_RUNNING), "disable failure stops interface before uncontrolled roaming");
    for (i = 0; i < 2; i++) {
        fixture(&sc,&bss,&chan); r = request(&sc,IEEE80211_STA_TX);
        if (i == 0) r.generation--; else r.bssid[5]++;
        check(bwfm_sta_mgmt(&sc.sc_ic,&r) == ESTALE && tail == 0, "stale association or foreign BSSID cannot queue TX");
    }
    for (i = 0; i < 5; i++) {
        fixture(&sc,&bss,&chan); r = request(&sc,IEEE80211_STA_TX);
        if (i == 0) r.cookie = 0;
        if (i == 1) r.len = 2;
        if (i == 2) r.data[0] = 0;
        if (i == 3) { r.data[3] = 0; r.len = 10; }
        if (i == 4) r.len = IEEE80211_STA_MAX_BODY + 1;
        check(bwfm_sta_mgmt(&sc.sc_ic,&r) == EINVAL && tail == 0, "malformed action never reaches queue");
    }
    fixture(&sc,&bss,&chan); r = request(&sc,IEEE80211_STA_TX); fail_task = true;
    check(bwfm_sta_mgmt(&sc.sc_ic,&r) == ENOBUFS && !sc.sc_sta_tx_pending, "queue allocation failure leaves no TX pending");
    fail_task = false; sc.sc_sta_packetid = UINT32_MAX;
    check(bwfm_sta_mgmt(&sc.sc_ic,&r) == 0 && sc.sc_sta_packetid == 1, "zero packet ID is skipped on wrap");
    check(bwfm_sta_mgmt(&sc.sc_ic,&r) == EBUSY, "second TX cannot overwrite current cookie");
    drain(); check(tx_commands == 1 && tx_events == 0, "firmware submission is not reported as ACK");
    bwfm_sta_tx_done(&sc,true);
    check(tx_events == 1 && last_tx.cookie == 42 && last_tx.generation == 40 &&
        (last_tx.flags & IEEE80211_STA_TX_ACK), "completion preserves caller cookie and association generation");
    bwfm_sta_tx_done(&sc,true); check(tx_events == 1, "TX completion is emitted once");
    fixture(&sc,&bss,&chan); r = request(&sc,IEEE80211_STA_TX);
    check(bwfm_sta_mgmt(&sc.sc_ic,&r) == 0, "queue TX before cancellation");
    r = request(&sc,IEEE80211_STA_CANCEL_TX); r.cookie--;
    check(bwfm_sta_mgmt(&sc.sc_ic,&r) == 0 && sc.sc_sta_tx_pending, "foreign cookie cannot cancel TX");
    r.cookie++; r.generation--;
    check(bwfm_sta_mgmt(&sc.sc_ic,&r) == ESTALE && sc.sc_sta_tx_pending, "old association cannot cancel TX");
    r.generation++;
    check(bwfm_sta_mgmt(&sc.sc_ic,&r) == 0 && !sc.sc_sta_tx_pending && tx_events == 1, "matching cancellation completes without ACK");
    drain(); check(tx_commands == 0 && !(last_tx.flags & IEEE80211_STA_TX_ACK), "cancelled queued TX is never sent");
    for (i = 1; i <= 4; i++) {
        fixture(&sc,&bss,&chan); r = request(&sc,IEEE80211_STA_TX);
        check(bwfm_sta_mgmt(&sc.sc_ic,&r) == 0, "queue TX for sleeping allocation fixture");
        allocation_mutation = (int)i; drain();
        check(tx_commands == 0, "cancelled or replaced transaction cannot transmit after allocation sleeps");
    }
    fixture(&sc,&bss,&chan); r = request(&sc,IEEE80211_STA_TX);
    check(bwfm_sta_mgmt(&sc.sc_ic,&r) == 0, "queue TX before firmware error");
    fw_error = EIO; drain();
    check(tx_events == 1 && !(last_tx.flags & IEEE80211_STA_TX_ACK), "firmware send error is a negative completion");
    fixture(&sc,&bss,&chan); r = request(&sc,IEEE80211_STA_TX);
    check(bwfm_sta_mgmt(&sc.sc_ic,&r) == 0, "queue TX before replacement during firmware I/O");
    fw_error = EIO; firmware_mutation = 1; drain();
    check(tx_events == 0, "old firmware error cannot complete replacement TX");
    fixture(&sc,&bss,&chan); r = request(&sc,IEEE80211_STA_SCAN);
    r.len = 1;
    check(bwfm_sta_mgmt(&sc.sc_ic,&r) == EINVAL && tail == 0, "odd frequency byte count rejected");
    r.len = IEEE80211_STA_MAX_CHANNELS * 2 + 2;
    check(bwfm_sta_mgmt(&sc.sc_ic,&r) == EINVAL && tail == 0, "oversized channel list rejected");
    r.len = 0;
    check(bwfm_sta_mgmt(&sc.sc_ic,&r) == 0, "queue connected scan"); old = queued[0]->t_sta;
        r = request(&sc,IEEE80211_STA_CANCEL_SCAN); r.cookie--;
    check(bwfm_sta_mgmt(&sc.sc_ic,&r) == ESTALE && sc.sc_sta_bgscan, "old scan cookie cannot cancel current scan");
    r.cookie++;
    check(bwfm_sta_mgmt(&sc.sc_ic,&r) == 0, "matching scan cookie can cancel");
    check(last_scan.cookie == 42 && last_scan.generation == 40 &&
        last_scan.flags == IEEE80211_STA_SCAN_ABORTED, "cancel completion retains the request identity");
    r = request(&sc,IEEE80211_STA_SCAN); r.cookie++;
    check(!sc.sc_sta_bgscan
 && aborts == 1 && sc.sc_scan_id == 1, "cancel invalidates queue epoch and firmware scan ID");
    check(bwfm_sta_mgmt(&sc.sc_ic,&r) == 0, "queue replacement scan");
    bwfm_sta_mgmt_cb(&sc,&old);
    check(scans == 0 && sc.sc_sta_bgscan, "cancelled scan cannot execute or clear replacement");
    drain(); check(scans == 1 && sc.sc_ic.ic_scan.nt_scan_epoch == 1 &&
        sc.sc_ic.ic_state == IEEE80211_S_RUN, "one fresh connected scan preserves RUN state");
    bwfm_sta_scan_done(&sc,false);
    check(scan_events == 2 && last_scan.cookie == 43 && last_scan.generation == 40 &&
        last_scan.flags == 0 && !(sc.sc_ic.ic_flags & IEEE80211_F_SCAN),
        "successful scan completion carries independent cookie and clears scan state");
    bwfm_sta_scan_done(&sc,false); check(scan_events == 2, "scan completion is emitted once");

    fixture(&sc,&bss,&chan); r = request(&sc,IEEE80211_STA_SCAN);
    check(bwfm_sta_mgmt(&sc.sc_ic,&r) == 0, "queue scan before association replacement");
    sc.sc_sta_generation++; drain();
    check(scans == 0 && !sc.sc_sta_bgscan, "same-BSSID new association invalidates queued scan");
    fixture(&sc,&bss,&chan); r = request(&sc,IEEE80211_STA_SCAN);
    check(bwfm_sta_mgmt(&sc.sc_ic,&r) == 0, "queue scan before firmware error");
    fw_error = EIO; drain();
    check(scan_events == 1 && !sc.sc_sta_bgscan && !(sc.sc_ic.ic_flags & IEEE80211_F_SCAN), "scan error clears state and wakes supplicant");
    fixture(&sc,&bss,&chan); r = request(&sc,IEEE80211_STA_SCAN);
    check(bwfm_sta_mgmt(&sc.sc_ic,&r) == 0, "queue scan before replacement during firmware I/O");
    fw_error = EIO; scan_mutation = 1; drain();
    check(scan_events == 0 && sc.sc_sta_bgscan, "old scan error cannot complete replacement");
    printf("%s: %u production station management assertions\n",failures ? "FAIL" : "PASS",checks);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
