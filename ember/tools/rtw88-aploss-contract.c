/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual idle watchdog -> beacon miss -> native fence -> disconnect consumers.
 * Layouts, clock/Giant/PCI, worker dispatch, node cleanup and event delivery are
 * explicit services. RF restart, actual sockets, supplicant autojoin and crypto
 * cleanup are not implemented by this fixture and are not qualified here. */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE 1
#endif
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <stdlib.h>
#include "rtw88_net_policy.h"
#include "aploss-constants.inc"
#include "ieee80211_ht20.h"
#include "ieee80211_rxba.h"

static unsigned checks, failures;
#define CHECK(e) do { checks++; if (!(e)) { failures++; \
    fprintf(stderr,"line %u: %s\n",(unsigned)__LINE__,#e); } } while (0)
#define IEEE80211_DPRINTF(...) ((void)0)
#define MAX(a,b) ((a)>(b)?(a):(b))
#define LINK_STATE_DOWN 0
static const unsigned hz=100;
static uint32_t ticks;
static int giant, pci, pci_token, probe_scan;
static struct ieee80211com *probe_ic;
struct ieee80211com;
struct ieee80211_node { struct ieee80211com *ni_ic; unsigned ni_flags; struct ieee80211_rxba_state ni_rxba; uint8_t ni_bssid[6], ni_essid[32], ni_macaddr[6]; unsigned ni_esslen, ni_fails; struct ieee80211_ht20_node ni_ht20; };
struct ieee80211_node_table { int nt_inact_timer; void (*nt_timeout)(struct ieee80211_node_table *); };
struct ifnet { void *if_softc; int if_timer; unsigned if_oerrors; };
struct work { unsigned unused; };
struct ieee80211com {
    const struct ieee80211_rxba_ops *ic_rxba_ops;
    uint64_t ic_rxba_serial;
    struct ifnet *ic_ifp;
    struct ieee80211_node *ic_bss;
    enum ieee80211_state ic_state;
    enum ieee80211_opmode ic_opmode;
    enum ieee80211_roamingmode ic_roaming;
    unsigned ic_flags, ic_bmiss_count, ic_bmiss_max;
    uint16_t ic_bmisstimeout;
    uint8_t ic_myaddr[6];
    void *ic_opt_ie;
    unsigned ic_opt_ie_len, ic_curchan;
    int ic_mgt_timer;
    struct ieee80211_node_table ic_scan, ic_sta;
    void (*ic_node_cleanup)(struct ieee80211_node *);
    int (*ic_ht20_admit)(struct ieee80211com *,struct ieee80211_ht20_profile *);
};
struct rtw88_softc { int sc_linked; struct { int active,configured; } sc_ra; };
struct rtw88_net {
    struct ieee80211com ic;
    struct rtw88_softc *core;
    struct rtw88_net_policy policy;
    int *lock;
    int scan, dev, work_pending, reset_required, state_arg;
    unsigned channel, tx_timer;
    enum ieee80211_state requested;
    struct work state_work;
    void *wq;
};
static void
rtw88_net_ht20_publish(struct rtw88_net *net,
    const struct ieee80211_ht20_profile *profile)
{
	/* AP-loss extraction exercises only the disabled admission boundary. */
	CHECK(net != NULL);
	CHECK(profile == NULL);
}

struct ieee80211_leave_event { uint8_t iev_addr[6]; };
static unsigned probes, requests, cleanup, events, link_down, begin_scans;
static unsigned user_disconnects, user_aborts, user_tx_clear, user_scan_clear;
static int queued_event;
static void mutex_enter(int *lock) { CHECK(lock==&pci_token && giant && !pci); pci=1; }
static void mutex_exit(int *lock) { CHECK(lock==&pci_token && giant && pci); pci=0; }
static void callout_stop(int *ch) { (void)ch; CHECK(giant && !pci); }
static void workqueue_enqueue(void *wq,struct work *w,void *arg) {
    (void)wq;(void)w;(void)arg;CHECK(giant && !pci);requests++;
}
static unsigned ieee80211_chan2ieee(struct ieee80211com *ic,unsigned channel) { (void)ic;return channel; }
static uint32_t getticks(void) { return ticks; }
static unsigned mstohz(unsigned ms) { return (unsigned)(((uint64_t)ms*hz+999)/1000); }
#define if_statinc(ifp,field) ((ifp)->field++)
#define aprint_error_dev(...) ((void)0)
#define KERNEL_LOCK(n,p) do {(void)(n);(void)(p);CHECK(!giant);giant=1;} while(0)
#define KERNEL_UNLOCK_ONE(p) do {(void)(p);CHECK(giant);giant=0;} while(0)
static int splnet(void) { return 0; }
static void splx(int s) { (void)s; }
static int rtw88_net_newstate(struct ieee80211com *,enum ieee80211_state,int);
static int ieee80211_new_state(struct ieee80211com *ic,enum ieee80211_state s,int arg) {
    assert(ic != NULL);assert(ic->ic_ifp != NULL);assert(ic->ic_ifp->if_softc != NULL);
    CHECK(giant && !pci);return rtw88_net_newstate(ic,s,arg);
}
static void ieee80211_send_probereq(struct ieee80211_node *ni,const uint8_t *src,
    const uint8_t *dst,const uint8_t *bssid,const uint8_t *ssid,unsigned len,
    const void *ie,unsigned ielen) {
    (void)ni;(void)src;(void)dst;(void)bssid;(void)ssid;(void)len;(void)ie;(void)ielen;
    CHECK(giant && !pci);probes++;
    if(probe_scan) { probe_scan=0; assert(probe_ic != NULL); (void)ieee80211_new_state(probe_ic,IEEE80211_S_SCAN,0); }
}
static void rt_ieee80211msg(struct ifnet *ifp,int type,void *data,unsigned len) {
    (void)ifp;(void)data;CHECK(giant && !pci);CHECK(len==0 && type==RTM_IEEE80211_DISASSOC);
    events++;queued_event=type;
}
static void if_link_state_change(struct ifnet *ifp,int state) { (void)ifp;CHECK(state==LINK_STATE_DOWN);link_down++; }
#define IEEE80211_ADDR_COPY(d,s) memcpy(d,s,6)
static void ieee80211_wme_reset(struct ieee80211_node *ni) { (void)ni;CHECK(giant && !pci); }
static void model_node_cleanup(struct ieee80211_node *ni) { (void)ni;CHECK(giant && !pci);cleanup++; }
static struct ieee80211_node *ieee80211_find_node(struct ieee80211_node_table *nt,const uint8_t *addr) { (void)nt;(void)addr;return NULL; }
static void ieee80211_unref_node(struct ieee80211_node **ni) { (void)ni; }
static void ieee80211_begin_scan(struct ieee80211com *ic,int arg) { (void)ic;(void)arg;CHECK(giant && !pci);begin_scans++; }
/* This contract has no HT provider. If an active RUN capsule reaches this
 * service, fail loudly; full current-profile semantics belong to the HT suite. */
static int ht20_current(struct ieee80211com *ic,struct ieee80211_node *ni,
    const struct ieee80211_ht20_node *ht) {
    (void)ic;(void)ni;(void)ht;
    CHECK(0);return 0;
}
static int aploss_checked_ht20_request(struct ieee80211com *ic,int state) {
    CHECK(giant && !pci && ic->ic_ht20_admit==NULL);
    return ieee80211_ht20_state_request(ic,state);
}
static void ieee80211_beacon_miss(struct ieee80211com *);
static void ieee80211_notify_node_leave(struct ieee80211com *,struct ieee80211_node *);
static void ieee80211_sta_leave(struct ieee80211com *,struct ieee80211_node *);
/* BA payloads are outside this default-OFF AP-loss fixture. Actual shared
 * storage destruction is retained; reaching a held mbuf is an explicit error. */
static void m_freem(struct mbuf *m) { (void)m;abort(); }
#include "native-rxba-lifecycle.inc"
#include "aploss-native.inc"

struct bsd_driver_data { int sta_scan_pending,is_ap; void *ctx; };
union wpa_event_data { struct { int aborted; } scan_info; };
enum { EVENT_DISASSOC=1,EVENT_SCAN_RESULTS=2 };
#define os_memset memset
static void bsd_sta_tx_status(struct bsd_driver_data *drv,int a,int b) { (void)drv;(void)a;(void)b;user_tx_clear++; }
static void bsd_sta_scan_clear(struct bsd_driver_data *drv) { drv->sta_scan_pending=0;user_scan_clear++; }
/* Event queue is the actual consumer boundary, not a fake successful scan. */
static void wpa_supplicant_event(void *ctx,int event,union wpa_event_data *data) {
    (void)ctx;
    if(event==EVENT_DISASSOC) { CHECK(data==NULL);user_disconnects++; }
    else { CHECK(event==EVENT_SCAN_RESULTS && data && data->scan_info.aborted);user_aborts++; }
}
#include "aploss-userland.inc"

struct fixture { struct rtw88_net net; struct rtw88_softc core; struct ifnet ifp; struct ieee80211_node ni; };
static void setup(struct fixture *f) {
    memset(f,0,sizeof(*f));giant=1;pci=0;ticks=100;probe_scan=0;
    f->net.lock=&pci_token;
    f->net.core=&f->core;f->net.ic.ic_ifp=&f->ifp;f->ifp.if_softc=&f->net;
    f->net.ic.ic_bss=&f->ni;f->ni.ni_ic=&f->net.ic;f->net.ic.ic_state=IEEE80211_S_RUN;
    f->net.ic.ic_opmode=IEEE80211_M_STA;f->net.ic.ic_roaming=IEEE80211_ROAMING_MANUAL;
    f->net.ic.ic_curchan=8;f->net.ic.ic_bmisstimeout=700;
    f->net.ic.ic_bmiss_max=IEEE80211_BMISS_MAX;
    f->net.ic.ic_node_cleanup=model_node_cleanup;
    f->core.sc_linked=1;f->core.sc_ra.active=f->core.sc_ra.configured=1;
    f->net.policy.epoch=7;f->net.policy.accepting=1;
    f->ni.ni_bssid[0]=2;f->ni.ni_macaddr[0]=2;
    probes=requests=cleanup=events=link_down=begin_scans=0;queued_event=0;
    user_disconnects=user_aborts=user_tx_clear=user_scan_clear=0;
    native_run_supervision(&f->net);CHECK(f->ifp.if_timer==1);giant=0;
}
static void watchdog_at(struct fixture *f,uint32_t when) {
    ticks=when;giant=1;rtw88_net_watchdog(&f->ifp);giant=0;
}
static void second(struct fixture *f) {
    CHECK(!giant && !pci);ticks+=hz;
    if(if_slowtimo_countdown(&f->ifp)) {
        giant=1;rtw88_net_watchdog(&f->ifp);giant=0;
    }
}
static void seconds(struct fixture *f,unsigned n) { for(unsigned i=0;i<n;i++)second(f); }
/* Hardware worker scheduling/commit is modeled; exact parent RUN->SCAN branch
 * and its real sta_leave/notification consumers execute after that boundary. */
static void flush_scan(struct fixture *f) {
    CHECK(f->net.work_pending && f->net.requested==IEEE80211_S_SCAN);
    CHECK(!f->net.policy.accepting && !f->core.sc_ra.active && !f->core.sc_ra.configured);
    CHECK(f->net.reset_required && f->net.policy.epoch==8);
    giant=1;f->net.ic.ic_state=IEEE80211_S_SCAN;
    shared_run_to_scan(&f->net.ic,f->net.state_arg);f->net.work_pending=0;giant=0;
}
static void test_fast_watchdog_floor(void) {
    struct fixture f;setup(&f);
    uint32_t start=ticks;
    /* Default 700 TU is seven 100 TU beacon intervals: 72 clock ticks. */
    watchdog_at(&f,start+71);
    CHECK(probes==0 && requests==0);
    watchdog_at(&f,start+72);
    CHECK(probes==0 && requests==0);
    /* A one-second polling floor caps the additional delay below one second. */
    watchdog_at(&f,start+99);
    CHECK(probes==0 && requests==0);
    watchdog_at(&f,start+100);
    CHECK(probes==1 && requests==0 && f.net.ic.ic_bmiss_count==1);
    watchdog_at(&f,start+199);
    CHECK(probes==1 && requests==0);
    watchdog_at(&f,start+200);
    CHECK(probes==1 && requests==1 && f.net.policy.epoch==8);
    if(requests==1) {
        flush_scan(&f);
        CHECK(cleanup==1 && events==1 && link_down==1);
    }
}
static void test_silent_idle_chain(void) {
    struct fixture f;setup(&f);
    second(&f);CHECK(probes==1 && requests==0 && f.net.ic.ic_bmiss_count==1);
    CHECK(f.net.policy.accepting && f.core.sc_ra.active);
    second(&f);
    CHECK(requests==1 && f.net.policy.epoch==8 && !f.net.policy.beacon_valid);
    CHECK(f.net.requested==IEEE80211_S_SCAN && !f.net.policy.accepting);
    flush_scan(&f);CHECK(cleanup==1 && events==1 && link_down==1 && begin_scans==0);
    CHECK(queued_event==RTM_IEEE80211_DISASSOC);
    struct bsd_driver_data drv={0};bsd_disconnect_route(&drv,queued_event);
    CHECK(user_disconnects==1 && user_aborts==0 && user_tx_clear==1 && user_scan_clear==1);
    seconds(&f,20);CHECK(events==1 && requests==1);
}
static void test_refresh_and_fences(void) {
    struct fixture f;uint8_t beacon[36]={0x80};setup(&f);
    memcpy(beacon+16,f.ni.ni_bssid,6);uint32_t start=ticks;giant=1;
    CHECK(rtw88_net_beacon_note(&f.net.policy,7,ticks,beacon,36,f.ni.ni_bssid)==0);giant=0;
    watchdog_at(&f,start+100);CHECK(probes==1 && requests==0);
    ticks=start+101;giant=1;
    CHECK(rtw88_net_beacon_note(&f.net.policy,7,ticks,beacon,36,f.ni.ni_bssid)==0);giant=0;
    watchdog_at(&f,start+200);CHECK(probes==1 && requests==0);
    giant=1;CHECK(rtw88_net_newstate(&f.net.ic,IEEE80211_S_SCAN,0)==0);giant=0;
    CHECK(!f.net.policy.accepting && !f.core.sc_ra.active && f.net.policy.epoch==8);
    CHECK(rtw88_net_beacon_note(&f.net.policy,7,ticks,beacon,36,f.ni.ni_bssid)==ENXIO);
    CHECK(!rtw88_net_rx_ready(&f.net.policy,7));seconds(&f,20);CHECK(requests==1);
    setup(&f);beacon[16]^=1;
    CHECK(rtw88_net_beacon_note(&f.net.policy,7,ticks+400,beacon,36,f.ni.ni_bssid)==ENOENT);
    seconds(&f,10);CHECK(requests==1);
    setup(&f);rtw88_net_close(&f.net.policy);seconds(&f,20);CHECK(probes==0 && requests==0);
    giant=1;CHECK(rtw88_net_newstate(&f.net.ic,IEEE80211_S_SCAN,0)==ENXIO);giant=0;
    setup(&f);f.net.ic.ic_flags|=IEEE80211_F_SCAN;seconds(&f,20);CHECK(!probes && !requests);
    setup(&f);f.net.ic.ic_opmode=IEEE80211_M_MONITOR;seconds(&f,20);CHECK(!probes && !requests);
    setup(&f);f.net.ic.ic_state=IEEE80211_S_INIT;seconds(&f,20);CHECK(!probes && !requests && f.ifp.if_timer==0);
}
static void test_clock_and_superseded_generation(void) {
    struct fixture f;uint8_t beacon[36]={0x80};setup(&f);
    memcpy(beacon+16,f.ni.ni_bssid,6);
    /* A state request reentered from the first probe withdraws this epoch. */
    probe_scan=1;probe_ic=&f.net.ic;
    seconds(&f,5);CHECK(requests==1 && f.net.policy.epoch==8 && !f.net.policy.accepting);
    CHECK(!f.core.sc_ra.active && f.net.requested==IEEE80211_S_SCAN);
    seconds(&f,10);CHECK(requests==1 && probes==1);
    /* A new RUN gets a fresh clock, even at the same peer/channel. */
    giant=1;CHECK(rtw88_net_commit(&f.net.policy,8,0,1)==0);
    f.net.work_pending=0;f.net.ic.ic_opt_ie=NULL;native_run_supervision(&f.net);
    CHECK(rtw88_net_beacon_note(&f.net.policy,7,ticks,beacon,36,f.ni.ni_bssid)==ECANCELED);
    giant=0;second(&f);CHECK(probes==1 && requests==2);
    /* bmiss_count is shared state: no synthetic reset is added at new RUN. */
    setup(&f);ticks=UINT32_MAX-250;giant=1;native_run_supervision(&f.net);giant=0;
    second(&f);CHECK(probes==1 && requests==0);second(&f);CHECK(requests==1);
    setup(&f);ticks=UINT32_MAX-50;second(&f);CHECK(probes==0 && requests==0);
    /* An explicit interface stop cancels the timer; no daemon scan is faked. */
    setup(&f);f.ifp.if_timer=0;seconds(&f,20);CHECK(!probes && !requests);
}
static void test_ht_off_request_reset(void) {
    struct fixture f;setup(&f);
    f.ni.ni_ht20.attempt=2;f.ni.ni_ht20.offered=1;
    f.ni.ni_ht20.profile.generation=5;
    giant=1;CHECK(rtw88_net_newstate(&f.net.ic,IEEE80211_S_SCAN,0)==0);giant=0;
    CHECK(f.ni.ni_ht20.attempt==3 && !f.ni.ni_ht20.offered &&
        !f.ni.ni_ht20.valid && f.ni.ni_ht20.profile.generation==0);
    CHECK(f.net.policy.epoch==8 && !f.net.policy.accepting && !f.core.sc_ra.active);
}
static void test_roaming_and_userland_boundaries(void) {
    struct fixture f;setup(&f);seconds(&f,10);f.net.ic.ic_roaming=IEEE80211_ROAMING_AUTO;
    flush_scan(&f);CHECK(begin_scans==1 && events==1);
    struct bsd_driver_data drv={.sta_scan_pending=1};bsd_disconnect_route(&drv,queued_event);
    CHECK(user_disconnects==1 && user_aborts==1 && !drv.sta_scan_pending);
    drv.is_ap=1;bsd_disconnect_route(&drv,RTM_IEEE80211_DISASSOC);CHECK(user_disconnects==1);
    /* No running consumer: kernel queues the event, but no fabricated autojoin. */
    setup(&f);seconds(&f,10);flush_scan(&f);CHECK(events==1 && user_disconnects==0);
}
int main(void) {
    test_fast_watchdog_floor();test_silent_idle_chain();test_refresh_and_fences();
    test_clock_and_superseded_generation();test_ht_off_request_reset();
    test_roaming_and_userland_boundaries();
    printf("rtw88 AP-loss chain: %u checks, %u failures\n",checks,failures);
    return failures!=0;
}
