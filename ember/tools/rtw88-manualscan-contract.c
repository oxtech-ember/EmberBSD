/* SPDX-License-Identifier: BSD-2-Clause */
/* Complete actual native worker/core, actual shared leave/SCAN branch and
 * actual BSD disconnect + supplicant identity reset/same-BSSID selection.
 * Clock/locks, node crypto cleanup, routing socket delivery and successful
 * on-air AUTH/ASSOC/WPA replies are explicit host services, not RF proof. */
#include <stdbool.h>
#define RTW88_RA_MAIN manualscan_prior_ra_main
#include "manualscan-ra.inc"

const struct ieee80211_channel ieee80211_channel_anyc = {0};
static unsigned leave_events, key_deletes, scan_starts, auth_posts;
static unsigned associations, disconnections, scan_results;
static int route_events[8];
static unsigned route_count;
static int auth_full;
/* Arm only after supplicant same-BSSID selection. This bus service changes
 * HMETFR samples; the actual worker/core and kpause clock service own waiting. */
static unsigned manualscan_busy, manualscan_busy_reads, manualscan_locked_poll;
static int manualscan_mailbox_read1(void *cookie,uint32_t reg,uint8_t *value)
{
    int error=read_1(cookie,reg,value);
    if(error==0 && reg==0x1cc && manualscan_busy) {
        *value=0x0f;manualscan_busy--;manualscan_busy_reads++;
    }
    return error;
}
static void manualscan_mailbox_delay(void *cookie,unsigned us)
{
    if(us==RTW88_POLL_STEP_US && state_lock.held)manualscan_locked_poll++;
    delay_us(cookie,us);
}
/* AUTH frame construction is modeled. The actual native submit/core own its
 * admission and DMA descriptor; no over-the-air success is fabricated. */
static const uint8_t manualscan_auth[30] = {
    0xb0,0,0,0, 2,0,0,0,0,1, 2,0,0,0,0,2, 2,0,0,0,0,1, 0,0,
    0,0,1,0,0,0
};
#ifndef __UNCONST
#define __UNCONST(a) ((void *)(uintptr_t)(const void *)(a))
#endif
#define IEEE80211_DPRINTF(...) ((void)0)
#define IEEE80211_ADDR_COPY(a,b) memcpy(a,b,6)
#define LINK_STATE_DOWN 0
#define IEEE80211_NO_HOSTAP 1
#define IS_UP(ic) (((ic)->ic_ifp->if_flags & (IFF_UP|IFF_RUNNING)) == (IFF_UP|IFF_RUNNING))
struct ieee80211_leave_event { uint8_t iev_addr[6]; };
static void rt_ieee80211msg(struct ifnet *ifp,int what,void *p,unsigned len)
{
    (void)ifp;CHECK(!state_lock.held);CHECK(p==NULL && len==0);
    CHECK(route_count<__arraycount(route_events));
    if(route_count<__arraycount(route_events))route_events[route_count++]=what;
}
static void if_link_state_change(struct ifnet *ifp,int state)
{ (void)ifp;CHECK(!state_lock.held && state==LINK_STATE_DOWN);leave_events++; }
/* Node/key storage service: the actual sta_leave owns invocation and ordering.
 * Full CCMP/key-map deletion has its independent shared-stack contracts. */
static void manualscan_node_cleanup(struct ieee80211_node *ni)
{
    CHECK(!state_lock.held);
    if(ni->key_live)key_deletes++;
    ni->key_live=0;ni->ni_associd=0;
    ni->ni_flags&=~(IEEE80211_NODE_QOS|IEEE80211_NODE_WME_OFFER|IEEE80211_NODE_WME_VALID|IEEE80211_NODE_ASSOC_PENDING);
    ieee80211_ht20_reset(ni);
}
static void ieee80211_begin_scan(struct ieee80211com *ic,int arg)
{ (void)ic;(void)arg;CHECK(!state_lock.held);scan_starts++; }
static void ieee80211_probe_curchan(struct ieee80211com *ic,int arg)
{ (void)ic;(void)arg;CHECK(!state_lock.held); }
static void ieee80211_create_ibss(struct ieee80211com *ic,struct ieee80211_channel *ch)
{ (void)ic;(void)ch;CHECK(0); }
static struct ieee80211_node *ieee80211_find_node(struct ieee80211_node_table *nt,const uint8_t *mac)
{ (void)nt;(void)mac;return NULL; }
static void ieee80211_unref_node(struct ieee80211_node **ni) { CHECK(*ni!=NULL); }
static void ieee80211_cancel_scan(struct ieee80211com *);
#include "manualscan-shared.inc"
static void ieee80211_cancel_scan(struct ieee80211com *ic) { ic->ic_flags&=~IEEE80211_F_SCAN; }

/* Full real supplicant functions, with unrelated OS/EAP/session services. */
#define CONFIG_NO_RRM 1
#define ETH_ALEN 6
#define MSG_DEBUG 0
#define MSG_INFO 1
#define wpa_msg(...) ((void)0)
#define wpa_dbg(...) ((void)0)
#define os_memset memset
enum { WPA_DISCONNECTED, WPA_INTERFACE_DISABLED, WPA_AUTHENTICATING, WPA_ASSOCIATING, WPA_COMPLETED };
enum { WPA_KEY_MGMT_OWE=3,WPA_KEY_MGMT_DPP=4 };
struct wpa_ssid { int unused; };
struct wpa_bss { uint8_t bssid[6]; };
struct reltime { long sec,usec; };
struct wpa_supplicant {
    int wpa_state,reassociate,key_mgmt,allowed_key_mgmts,drv_authorized_port;
    int assoc_freq,ap_ies_from_associnfo,wnmsleep_used,disable_mbo_oce,ieee80211ac;
    bool enabled_4addr_mode,wps_scan_done,ssid_verified,bigtk_set;
    uint8_t bssid[6],pending_bssid[6];
    struct wpa_ssid *current_ssid;
    struct wpa_bss *current_bss;
    struct reltime session_start,session_length;
    void *eapol;
};
static struct state_fixture *manualscan_current;
static void wnm_bss_keep_alive_deinit(struct wpa_supplicant *s) { (void)s; }
static int os_reltime_initialized(struct reltime *t) { return t->sec||t->usec; }
static void os_reltime_age(struct reltime *a,struct reltime *b) { (void)a;(void)b; }
static void wpas_notify_session_length(struct wpa_supplicant *s) { (void)s; }
static void wpa_supplicant_set_state(struct wpa_supplicant *s,int state) { s->wpa_state=state; }
static int is_zero_ether_addr(const uint8_t *p) { return memcmp(p,"\0\0\0\0\0\0",6)==0; }
static int ether_addr_equal(const uint8_t *a,const uint8_t *b) { return memcmp(a,b,6)==0; }
static void sme_clear_on_disassoc(struct wpa_supplicant *s) { (void)s; }
static void wpas_notify_bssid_changed(struct wpa_supplicant *s) { (void)s; }
static void eapol_sm_notify_portEnabled(void *e,bool b) { (void)e;(void)b; }
static void eapol_sm_notify_portValid(void *e,bool b) { (void)e;(void)b; }
static void eapol_sm_notify_eap_success(void *e,bool b) { (void)e;(void)b; }
static void eapol_sm_notify_config(void *e,void *c,void *ctx) { (void)e;(void)c;(void)ctx; }
static int wpa_key_mgmt_wpa_psk(int k) { return k==1; }
static void wnm_clear_coloc_intf_reporting(struct wpa_supplicant *s) { (void)s; }
static int wpa_drv_set_4addr_mode(struct wpa_supplicant *s,int b) { (void)s;(void)b;return 0; }
static void wpas_reset_mlo_info(struct wpa_supplicant *s) { (void)s; }
static int wpa_supplicant_scard_init(struct wpa_supplicant *s,struct wpa_ssid *ssid) { (void)s;(void)ssid;return 0; }
static void wpa_supplicant_req_new_scan(struct wpa_supplicant *s,unsigned a,unsigned b) { (void)s;(void)a;(void)b;CHECK(0); }
static int wpa_supplicant_connect_ml_missing(struct wpa_supplicant *s,struct wpa_bss *b,struct wpa_ssid *ssid) { (void)s;(void)b;(void)ssid;return 0; }
static void wpa_supplicant_associate(struct wpa_supplicant *s,struct wpa_bss *b,struct wpa_ssid *ssid)
{
    associations++;CHECK(!state_lock.held);
    s->wpa_state=WPA_AUTHENTICATING;s->current_ssid=ssid;
    memcpy(s->pending_bssid,b->bssid,6);
    CHECK(rtw88_net_newstate(&manualscan_current->net.ic,IEEE80211_S_AUTH,-1)==0);
}
enum { EVENT_DISASSOC=1,EVENT_SCAN_RESULTS=2 };
union wpa_event_data { struct { int aborted; } scan_info; };
struct bsd_driver_data { int is_ap,sta_scan_pending; struct reltime sta_scan_completed; struct wpa_supplicant *ctx; };
static void wpa_supplicant_mark_disassoc(struct wpa_supplicant *);
static void bsd_sta_tx_status(struct bsd_driver_data *drv,int ack,int notify) { (void)drv;(void)ack;(void)notify; }
static void bsd_sta_scan_clear(struct bsd_driver_data *drv) { drv->sta_scan_pending=0; }
static void os_get_reltime(struct reltime *r) { r->sec=1;r->usec=0; }
/* OS event dispatch invokes the actual identity-reset consumer synchronously.
 * Full event handler/autoscan scheduling is not included. No fake reassociate
 * flag is set; the real same-BSSID selection decides whether AUTH is requested. */
static void wpa_supplicant_event(void *ctx,int what,union wpa_event_data *data)
{
    struct wpa_supplicant *s=ctx;
    if(what==EVENT_DISASSOC) { CHECK(data==NULL);disconnections++;wpa_supplicant_mark_disassoc(s); }
    else { CHECK(what==EVENT_SCAN_RESULTS);scan_results++; }
}
#include "manualscan-userland.inc"

static int manualscan_parent(struct ieee80211com *ic,enum ieee80211_state state,int arg)
{
    enum ieee80211_state old=(enum ieee80211_state)ic->ic_state;
    CHECK(!state_lock.held);ic->ic_state=state;
    if(state==IEEE80211_S_SCAN)manualscan_parent_scan(ic,old,arg);
    if(state==IEEE80211_S_AUTH) {
        CHECK(ic->ic_bss->ni_associd==0 && !ic->ic_bss->key_live);
        struct rtw88_net *net=&manualscan_current->net;
        if(auth_full) {
            while(rtw88_tx_available(net->core,RTW88_TXQ_MGMT))
                CHECK(rtw88_tx_raw(net->core,RTW88_TXQ_MGMT,
                    manualscan_auth,sizeof(manualscan_auth))==0);
        }
        uint64_t posted=net->core->sc_stats.tx_posted[RTW88_TXQ_MGMT];
        int error=rtw88_net_tx_submit(net,ic->ic_bss,RTW88_TXQ_MGMT,
            manualscan_auth,sizeof(manualscan_auth));
        if(auth_full) {
            CHECK(error==ENOBUFS);
            CHECK(net->core->sc_stats.tx_posted[RTW88_TXQ_MGMT]==posted);
            return error;
        }
        CHECK(error==0);
        CHECK(net->core->sc_stats.tx_posted[RTW88_TXQ_MGMT]==posted+1);
        auth_posts++;
    }
    return 0;
}
static void route_pending(struct bsd_driver_data *drv)
{
    for(unsigned i=0;i<route_count;i++)manualscan_bsd_route(drv,route_events[i]);
    route_count=0;
}
static void test_running_manual_scan(unsigned channel,int fail_auth)
{
    struct state_fixture s;
    if(!state_fixture_start(&s,channel,channel>14?9:20))return;
    manualscan_current=&s;
    state_legacy_peer(&s.net,&s.ni,channel);
    s.ni.ni_associd=0xc001;s.ni.ni_bssid[0]=2;
    s.net.parent_state=manualscan_parent;
    CHECK(rtw88_net_newstate(&s.net.ic,IEEE80211_S_RUN,-1)==0);
    rtw88_net_state_work(&s.net.state_work,&s.net);
    CHECK(s.sc.sc_linked && s.sc.sc_ra.active && s.net.policy.accepting);
    s.net.ic.ic_roaming=IEEE80211_ROAMING_MANUAL;
    s.net.ic.ic_node_cleanup=manualscan_node_cleanup;
    s.net.ec.ec_if.if_flags|=IFF_RUNNING;
    s.ni.key_live=1;s.ni.ni_ht20.attempt=10;
    memset(s.net.ic.ic_chan_active,0,sizeof(s.net.ic.ic_chan_active));
    s.net.ic.ic_chan_active[channel/8]=(uint8_t)(1U<<(channel%8));
    uint8_t chans[32];memcpy(chans,s.net.ic.ic_chan_active,sizeof(chans));
    unsigned mode=s.net.ic.ic_curmode,media=s.net.configured_media;
    uint32_t gen=s.sc.sc_generation;
    uint64_t commands=s.sc.sc_stats.ra_commands;
    struct wpa_ssid ssid={0};struct wpa_bss selected={{2,0,0,0,0,0}};
    struct wpa_supplicant supp={.wpa_state=WPA_COMPLETED,.current_ssid=&ssid,.current_bss=&selected};
    memcpy(supp.bssid,selected.bssid,6);
    struct bsd_driver_data drv={.ctx=&supp};
    leave_events=key_deletes=scan_starts=route_count=auth_posts=associations=disconnections=scan_results=0;
    CHECK(manualscan_request(&s.net.ic)==0);
    CHECK(s.net.ic.ic_state==IEEE80211_S_INIT && s.net.requested==IEEE80211_S_SCAN);
    CHECK(leave_events==1 && key_deletes==1 && !s.ni.key_live && s.ni.ni_associd==0);
    CHECK(s.net.reset_required && !s.sc.sc_ra.active && !s.net.policy.accepting);
    rtw88_net_state_work(&s.net.state_work,&s.net);
    CHECK(s.sc.sc_generation>gen && !s.sc.sc_ra.active && !s.sc.sc_linked);
    CHECK(scan_starts==1);
    manualscan_end_scan(&s.net.ic);route_pending(&drv);
    CHECK(disconnections==1 && scan_results==1);
    CHECK(supp.wpa_state==WPA_DISCONNECTED && is_zero_ether_addr(supp.bssid));
    CHECK(!memcmp(chans,s.net.ic.ic_chan_active,sizeof(chans)));
    CHECK(s.net.ic.ic_curmode==mode && s.net.configured_media==media);
    CHECK(wpa_supplicant_connect(&supp,&selected,&ssid)==0);
    CHECK(associations==1 && supp.wpa_state==WPA_AUTHENTICATING);
    if(associations) {
        auth_full=fail_auth;
        unsigned ticks=state_ticks;
        uint64_t completed=s.sc.sc_stats.tx_done;
        manualscan_busy=3;manualscan_busy_reads=manualscan_locked_poll=0;
        s.bus.read_1=manualscan_mailbox_read1;
        s.bus.delay_us=manualscan_mailbox_delay;
        rtw88_net_state_work(&s.net.state_work,&s.net);
        CHECK(manualscan_busy_reads==3 && manualscan_busy==0);
        /* Descriptor-full AUTH follows the existing INIT request; C1 must
         * also retire those actually posted entries before unlinking. */
        CHECK(state_ticks==ticks+3+(fail_auth?1U:0U) && manualscan_locked_poll==0);
        if(fail_auth)CHECK(s.sc.sc_stats.tx_done==completed+RTW88_TX_SLOTS-1);
        CHECK(!s.sc.sc_mailbox.active);
        s.bus.read_1=read_1;s.bus.delay_us=delay_us;
        auth_full=0;
        if(fail_auth) {
            CHECK(auth_posts==0 && !s.net.policy.accepting && !s.sc.sc_ra.active);
            CHECK(s.net.ic.ic_state==IEEE80211_S_INIT && !s.net.work_pending);
            state_fixture_finish(&s);
            return;
        }
        CHECK(auth_posts==1);
        /* Successful AUTH/ASSOC/WPA replies are a service boundary. Only
         * fresh peer inputs may drive the actual worker back into RUN. */
        state_legacy_peer(&s.net,&s.ni,channel);
        s.ni.ni_associd=0xc001;s.ni.ni_bssid[0]=2;s.ni.key_live=1;
        CHECK(rtw88_net_newstate(&s.net.ic,IEEE80211_S_RUN,-1)==0);
        rtw88_net_state_work(&s.net.state_work,&s.net);
    }
    CHECK(s.sc.sc_stats.ra_commands==commands+1 && s.sc.sc_ra.active && s.sc.sc_linked);
    CHECK(s.net.policy.accepting);
    CHECK(leave_events==1 && key_deletes==1);
    printf("manual scan result: disconnected %u associations %u AUTH %u RA %d/%d\n",
        disconnections,associations,auth_posts,s.sc.sc_ra.configured,s.sc.sc_ra.active);
    state_fixture_finish(&s);
}
static void test_scan_request_guards(void)
{
    static const struct {
        enum ieee80211_opmode mode;
        enum ieee80211_state state;
        int up;
    } cases[] = {
        {IEEE80211_M_STA,IEEE80211_S_INIT,1},
        {IEEE80211_M_STA,IEEE80211_S_SCAN,1},
        {IEEE80211_M_STA,IEEE80211_S_AUTH,1},
        {IEEE80211_M_STA,IEEE80211_S_ASSOC,1},
        {IEEE80211_M_HOSTAP,IEEE80211_S_RUN,1},
        {IEEE80211_M_IBSS,IEEE80211_S_RUN,1},
        {IEEE80211_M_AHDEMO,IEEE80211_S_RUN,1},
        {IEEE80211_M_MONITOR,IEEE80211_S_RUN,1},
        {IEEE80211_M_STA,IEEE80211_S_RUN,0}
    };
    for(unsigned i=0;i<__arraycount(cases);i++) {
        struct state_fixture s;
        if(!state_fixture_start(&s,1,20))continue;
        s.net.ic.ic_opmode=cases[i].mode;s.net.ic.ic_state=cases[i].state;
        s.net.ic.ic_node_cleanup=manualscan_node_cleanup;
        s.net.ec.ec_if.if_flags=cases[i].up?(IFF_UP|IFF_RUNNING):0;
        s.ni.key_live=1;s.ni.ni_associd=7;
        s.net.ic.ic_chan_active[0]=0x42;
        leave_events=key_deletes=route_count=0;
        int error=manualscan_request(&s.net.ic);
        CHECK(error==(cases[i].up?0:EINVAL));
        CHECK(leave_events==0 && key_deletes==0 && route_count==0);
        CHECK(s.ni.key_live && s.ni.ni_associd==7);
        CHECK(s.net.ic.ic_chan_active[0]==0x42);
        if(!cases[i].up || cases[i].mode==IEEE80211_M_HOSTAP)
            CHECK(s.net.ic.ic_state==(int)cases[i].state);
        else CHECK(s.net.ic.ic_state==IEEE80211_S_INIT);
        state_fixture_finish(&s);
    }
    /* The direct helper's distinct list input must still use memmove. */
    struct state_fixture s;
    if(!state_fixture_start(&s,1,20))return;
    s.net.ic.ic_state=IEEE80211_S_SCAN;
    s.net.ic.ic_node_cleanup=manualscan_node_cleanup;
    s.net.ec.ec_if.if_flags|=IFF_RUNNING;
    uint8_t list[32]={0};list[4]=0x10;
    CHECK(ieee80211_setupscan(&s.net.ic,list)==0);
    CHECK(!memcmp(s.net.ic.ic_chan_active,list,sizeof(list)));
    CHECK(leave_events==0 && key_deletes==0);
    state_fixture_finish(&s);
}
int main(void)
{
    test_running_manual_scan(1,0);
    test_running_manual_scan(36,0);
    test_running_manual_scan(1,1);
    test_scan_request_guards();
    printf("rtw88 manual scan chain: %u checks, %u failures\n",assertions,failures);
    return failures!=0;
}
