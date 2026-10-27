/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: native RX BA scheduling and AP presence contract, AI-assisted. */
/* Actual native timer/provider bodies. Kernel scheduling and locks are models. */
#define RXBA_SHARED_MAIN rxba_shared_main
#include "net80211-rxba-contract.c"
#include "rtw88_net_policy.h"
#include "rtw88_hw.h"
#ifndef __UNCONST
#define __UNCONST(p) ((void *)(uintptr_t)(const void *)(p))
#endif
#include "native-rxba-native-types.inc"
#include "native-rxba-beacon.inc"
/* The channel identity is an explicit service in the common peer model. */
const struct ieee80211_channel ieee80211_channel_anyc={0};
#include <time.h>
#undef ticks
#define hz 1000
#ifndef MAX
#define MAX(a,b) ((a)>(b)?(a):(b))
#endif
#define IFF_RUNNING 0x40
#define IFF_OACTIVE 0x400
struct callout_model { void (*fn)(void *);void *arg;int pending,initialized;unsigned ticks; };
typedef struct callout_model callout_t;
struct rtw88_softc { int sc_linked;struct { int active,configured; } sc_ra;struct rtw88_phy_state sc_phy;void *sc_rx_frame,*sc_tx_complete,*sc_net_cookie;uint32_t sc_generation;struct { unsigned queued; } sc_txq[RTW88_NTXQ]; };
static uint64_t native_rx_sequence;
static void
native_rx_info(struct rtw88_rx_info *info, const struct rtw88_softc *core,
    size_t len)
{
	memset(info, 0, sizeof(*info));
	info->packet_size = (uint16_t)len;
	info->generation = core->sc_generation;
	info->sequence = ++native_rx_sequence;
}

struct rtw88_net {
 struct rtw88_softc *core;
 void *wq;int state_work,requested,state_arg,work_pending,reset_required;unsigned channel;
 struct rtw88_channel_tuple pending_channel;
 int tx_quiet,quiet_error;
 int (*parent_state)(struct ieee80211com *,int,int);
 struct rtw88_net_rx rx[RTW88_NET_RX_LIMIT];
 unsigned rx_head,rx_tail,tx_completed,tx_timer;uint64_t rx_dropped;
 struct rtw88_net_stats stats;
 struct rtw88_net_neg neg;
 int run_pending;
 callout_t scan;
 struct { struct ifnet ec_if; } ec;
 struct ieee80211com ic;
 volatile uint64_t ht20_snapshot;
 volatile uint32_t ht20_generation;
 struct rtw88_net_policy policy;
 callout_t rxba_callout;
 uint64_t rxba_token,rxba_deadline,rxba_due;
 int *lock;void *softint;
};
static int native_clock_overflow;
static unsigned native_schedules,native_halts,native_destroys,native_core_stops,native_released,native_ifdetach;
static int native_lock;
static inline void mutex_enter(int *p) { assert(!*p);*p=1; }
static inline void mutex_exit(int *p) { assert(*p);*p=0; }
static inline int mutex_owned(int *p) { return *p; }
static inline void getnanouptime(struct timespec *ts) { ts->tv_sec=unit_now/1000;ts->tv_nsec=native_clock_overflow?999000000:(unit_now%1000)*1000000; }
#define mstohz(ms) ((int)(ms))
static inline void softint_schedule(void *p) { (void)p;assert(native_lock);native_schedules++; }
static inline void callout_init(callout_t *c,int flags) { assert(flags==CALLOUT_MPSAFE || flags==0);memset(c,0,sizeof(*c));c->initialized=1; }
static inline void callout_setfunc(callout_t *c,void (*fn)(void *),void *arg) { assert(c->initialized);c->fn=fn;c->arg=arg; }
static inline void callout_schedule(callout_t *c,int ticks) { assert(c->initialized && native_lock && ticks>0);c->ticks=ticks;c->pending=1; }
static inline void callout_stop(callout_t *c) { assert(c->initialized);c->pending=0; }
static inline void callout_halt(callout_t *c,void *lock) { assert(c->initialized && !native_lock && lock==NULL);c->pending=0;native_halts++; }
static inline void callout_destroy(callout_t *c) { assert(c->initialized && !c->pending && !native_lock);c->initialized=0;native_destroys++; }
static int native_parent(struct ieee80211com *ic,int state,int arg)
{ (void)arg;CHECK(state==IEEE80211_S_INIT);ieee80211_ht20_state_request(ic,state);ic->ic_state=state;return 0; }
static unsigned native_work_requests;
static void workqueue_enqueue(void *q,int *work,void *cookie) { (void)work;(void)cookie;assert(q!=NULL && !native_lock);native_work_requests++; }
static void workqueue_wait(void *q,int *work) { (void)q;(void)work;assert(!native_lock); }
static void workqueue_destroy(void *q) { (void)q;assert(!native_lock); }
static void softint_disestablish(void *p) { (void)p;assert(!native_lock); }
static int rtw88_stop(struct rtw88_softc *core) { (void)core;assert(!native_lock);native_core_stops++;return 0; }
static void ieee80211_ifdetach(struct ieee80211com *ic)
{
 struct rtw88_net *net=ic->ic_ifp->if_softc;
 CHECK(!net->rxba_callout.pending && ic->ic_rxba_ops!=NULL);
 node_cleanup(ic->ic_bss);native_ifdetach++;
}
static void if_detach(struct ifnet *ifp) { (void)ifp;assert(!native_lock); }
static void kmem_free(void *p,size_t n)
{
 struct rtw88_net *net=p;CHECK(n==sizeof(*net) && !native_lock);
 CHECK(net->ic.ic_rxba_ops==NULL && !net->rxba_callout.initialized && !net->scan.initialized);
 CHECK(net->ic.ic_bss->ni_rxba.bytes==0 && net->ic.ic_bss->ni_ucastkey.wk_cipher==&ieee80211_cipher_none);
 native_released++;
}
static void rtw88_net_stop(struct ifnet *,int);
static unsigned getticks(void) { return unit_now; }
static void rtw88_net_start(struct ifnet *ifp) { (void)ifp;assert(!native_lock); }
static unsigned native_refill,native_refilled;
static void rtw88_net_rx_frame(void *,const uint8_t *,size_t,const struct rtw88_rx_info *);
static struct ieee80211_node *ieee80211_find_rxnode(struct ieee80211com *ic,const struct ieee80211_frame_min *wh)
{ (void)wh;assert(!native_lock && ic->ic_bss!=NULL);
 if(native_refill) {
  struct rtw88_net *net=ic->ic_ifp->if_softc;uint8_t bar[20]={0x84};bar[16]=4;
  memcpy(bar+4,ic->ic_myaddr,6);memcpy(bar+10,ic->ic_bss->ni_bssid,6);
  struct rtw88_rx_info info;native_rx_info(&info,net->core,20);native_refill--;native_refilled++;
  mutex_enter(net->lock);rtw88_net_rx_frame(net,bar,20,&info);mutex_exit(net->lock);
 }
 return ieee80211_ref_node(ic->ic_bss);
}
static struct mbuf *rtw88_net_rx_mbuf(const uint8_t *bytes,size_t len,struct ifnet *ifp)
{ (void)ifp;struct mbuf m={0};assert(len<=sizeof(m.storage));m.m_flags=M_PKTHDR;m.m_data=m.storage;memcpy(m.m_data,bytes,len);m.m_len=m.m_pkthdr.len=(int)len;return duplicate(&m); }
#include "native-rxba-native.inc"
static unsigned native_beacons_delivered;
/* The shared fixture's management service owns Action frames only. Observe
 * beacon delivery here; the actual native observer owns presence validation. */
static void native_beacon_dispatch(struct ieee80211com *ic,struct mbuf *m,
    struct ieee80211_node *ni,int subtype,int rssi,uint32_t stamp)
{
 (void)m;(void)rssi;(void)stamp;
 CHECK(!native_lock && ni==ic->ic_bss);
 CHECK(subtype==IEEE80211_FC0_SUBTYPE_BEACON);native_beacons_delivered++;
}
static void test_native_beacon_presence(void)
{
 for(unsigned malformed=0;malformed<2;malformed++) {
  struct unit_fixture f;if(!unit_setup_mode(&f,10,64,0,0))return;
  struct rtw88_softc core={.sc_generation=7};
  struct rtw88_net net={.core=&core,.lock=&native_lock};
  net.ic=f.ic;net.ec.ec_if=f.ifp;
  net.ic.ic_ifp=&net.ec.ec_if;net.ec.ec_if.if_softc=&net;
  net.ec.ec_if.model_ic=&net.ic;f.ni.ni_ic=&net.ic;f.ni.ni_associd=1;
  net.ic.ic_recv_mgmt=native_beacon_dispatch;
  uint8_t frame[41]={0x80};memset(frame+4,0xff,6);
  memcpy(frame+10,f.ni.ni_macaddr,6);memcpy(frame+16,f.ni.ni_bssid,6);
  frame[32]=100;frame[34]=1;frame[36]=malformed?255:0;
  frame[38]=1;frame[39]=1;frame[40]=12;
  struct rtw88_rx_info info;native_rx_info(&info,&core,sizeof(frame));
  net.policy.epoch=4;net.policy.accepting=1;unit_now=100;
  CHECK(rtw88_net_beacon_start(&net.policy,4,100)==0);
  unsigned delivered=native_beacons_delivered;
  unit_now=101;mutex_enter(net.lock);
  rtw88_net_rx_frame(&net,frame,sizeof(frame),&info);mutex_exit(net.lock);
  CHECK(net.policy.queued==1);
  unit_now=102;rtw88_net_softintr(&net);
  CHECK(net.policy.queued==0 && f.ni.references==1);
  CHECK(native_beacons_delivered==delivered+1);
  CHECK(net.policy.beacon_tick==(malformed?100U:101U));
  CHECK(rtw88_net_beacon_poll(&net.policy,4,102,2)==(int)malformed);
  f.ni.ni_ic=&f.ic;unit_close(&f);
 }
}
static void test_native_timer(void)
{
 struct unit_fixture f;if(!unit_setup(&f,10,64,0))return;
 struct rtw88_net net={0};struct rtw88_softc core={0};net.core=&core;net.parent_state=native_parent;callout_init(&net.scan,0);net.ic=f.ic;net.ec.ec_if=f.ifp;net.lock=&native_lock;
 net.ic.ic_ifp=&net.ec.ec_if;net.ec.ec_if.if_softc=&net;net.ec.ec_if.model_ic=&net.ic;f.ni.ni_ic=&net.ic;
 net.ec.ec_if.if_flags|=IFF_RUNNING;net.policy.accepting=1;
 unit_native_attach(&net);
 CHECK(net.ic.ic_rxba_ops!=&unit_ops && net.ic.ic_rxba_ops!=NULL);
 if(net.ic.ic_rxba_ops==NULL || net.ic.ic_rxba_ops==&unit_ops) { f.ni.ni_ic=&f.ic;unit_close(&f);return; }
 struct ieee80211_ht20_profile profile = {
  .generation=19,.rx_mcs=0xff,.tx_mcs=0xff,
  .ready=RTW88_NET_HT20_READY_ALL,.ampdu=16
 };
 struct rtw88_net_ht20_snapshot snapshot;
 rtw88_net_ht20_publish(&net,&profile);
 CHECK(rtw88_net_ht20_snapshot_admit(net.ht20_snapshot,&snapshot)==0);
 CHECK(snapshot.ready==RTW88_NET_HT20_READY_ALL && snapshot.rx_mcs==0xff && snapshot.tx_mcs==0xff);
 unit_now=UINT64_MAX;native_clock_overflow=0;
 CHECK(net.ic.ic_rxba_ops->schedule(&net.ic,UINT64_MAX,75)==EOVERFLOW);
 native_clock_overflow=1;
 CHECK(net.ic.ic_rxba_ops->now_ms(&net.ic)==UINT64_MAX);
 CHECK(net.ic.ic_rxba_ops->schedule(&net.ic,UINT64_MAX,76)==EOVERFLOW);
 native_clock_overflow=0;unit_now=1000;
 CHECK(net.ic.ic_rxba_ops->schedule(&net.ic,1100,77)==0);
 unit_now=1050;net.rxba_callout.fn(&net);CHECK(native_schedules==0 && net.rxba_due==0);
 unit_now=1100;net.rxba_callout.fn(&net);CHECK(native_schedules==1 && net.rxba_due==77);
 rtw88_net_softintr(&net);CHECK(net.rxba_due==0 && f.ni.references==1);
 CHECK(net.policy.rxba_mask==1);
 unit_now=1200;
 struct mbuf *m=unit_wire(&f,11,11);ieee80211_input(&net.ic,m,&f.ni,0,0);
 CHECK(net.rxba_token!=0 && net.rxba_deadline==1300);
 uint64_t old=net.rxba_token;
 unit_now=1300;net.rxba_callout.fn(&net);rtw88_net_softintr(&net);
 CHECK(rxba_output_count==1 && rxba_output_seq[0]==11 && net.rxba_token==0);
 /* Full actual native IRQ producer -> epoch queue -> Giant softintr ->
  * shared BAR -> ordered CCMP/Ethernet consumer. */
 m=unit_wire(&f,13,13);ieee80211_input(&net.ic,m,&f.ni,0,0);
 rtw88_net_softintr(&net);CHECK(net.policy.rxba_mask==1 && rxba_output_count==1);
 struct mbuf *bar=unit_bar(&f,13,4);struct rtw88_rx_info info;native_rx_info(&info,&core,20);
 mutex_enter(net.lock);rtw88_net_rx_frame(&net,bar->m_data,20,&info);mutex_exit(net.lock);
 m_freem(bar);CHECK(net.policy.queued==1);
 rtw88_net_softintr(&net);CHECK(net.policy.queued==0 && rxba_output_count==2 && rxba_output_seq[1]==13);
 /* A gap frame already queued by IRQ must run before an expired hole. */
 m=unit_wire(&f,15,15);ieee80211_input(&net.ic,m,&f.ni,0,0);
 m=unit_wire(&f,14,14);native_rx_info(&info,&core,m->m_pkthdr.len);info.flags=RTW88_RX_SWDEC;info.security=3;
 mutex_enter(net.lock);rtw88_net_rx_frame(&net,m->m_data,m->m_pkthdr.len,&info);mutex_exit(net.lock);m_freem(m);
 unit_now=1400;net.rxba_callout.fn(&net);rtw88_net_softintr(&net);
 CHECK(rxba_output_count==4 && rxba_output_seq[2]==14 && rxba_output_seq[3]==15);
 /* A gap that actually arrives after the absolute hole deadline is late. */
 m=unit_wire(&f,17,17);ieee80211_input(&net.ic,m,&f.ni,0,0);
 unit_now=1501;net.rxba_callout.fn(&net);
 m=unit_wire(&f,16,16);native_rx_info(&info,&core,m->m_pkthdr.len);info.flags=RTW88_RX_SWDEC;info.security=3;
 mutex_enter(net.lock);rtw88_net_rx_frame(&net,m->m_data,m->m_pkthdr.len,&info);mutex_exit(net.lock);m_freem(m);
 rtw88_net_softintr(&net);CHECK(rxba_output_count==5 && rxba_output_seq[4]==17);
 /* Refill cannot keep an old hole alive by repeatedly rescheduling due work. */
 m=unit_wire(&f,19,19);ieee80211_input(&net.ic,m,&f.ni,0,0);
 bar=unit_bar(&f,18,4);native_rx_info(&info,&core,20);
 mutex_enter(net.lock);rtw88_net_rx_frame(&net,bar->m_data,20,&info);mutex_exit(net.lock);m_freem(bar);
 unit_now=1601;net.rxba_callout.fn(&net);native_refill=128;native_refilled=0;
 rtw88_net_softintr(&net);
 CHECK(native_refilled==1 && net.policy.queued==1);
 CHECK(rxba_output_count==6 && rxba_output_seq[5]==19);
 native_refill=0;rtw88_net_softintr(&net);CHECK(net.policy.queued==0);
 /* Actual native Action ingress and the real shared positive producer use
  * the current native ic/queue, with balanced synchronous node ownership. */
 unsigned posts=unit_positive_posts;struct mbuf action={0};action.m_data=action.storage;
 action.m_len=action.m_pkthdr.len=33;action.m_flags=M_PKTHDR;action.m_data[0]=0xd0;
 memcpy(action.m_data+4,net.ic.ic_myaddr,6);memcpy(action.m_data+10,f.ni.ni_bssid,6);memcpy(action.m_data+16,f.ni.ni_bssid,6);
 unsigned params=2|(16<<6);uint8_t req[9]={3,0,4,params,params>>8,5,0,(uint8_t)(24<<4),24>>4};memcpy(action.m_data+24,req,9);
 native_rx_info(&info,&core,33);
 mutex_enter(net.lock);rtw88_net_rx_frame(&net,action.m_data,33,&info);mutex_exit(net.lock);
 rtw88_net_softintr(&net);
 CHECK(unit_positive_posts==posts+1 && f.ni.references==1 && f.ni.ni_rxba.tid[0]->head==24);
 CHECK(net.rxba_token!=0 && net.rxba_deadline==1607 && net.policy.rxba_mask==1);
 bar=unit_bar(&f,15,4);native_rx_info(&info,&core,20);info.flags=RTW88_RX_CRC_ERROR;
 mutex_enter(net.lock);rtw88_net_rx_frame(&net,bar->m_data,20,&info);mutex_exit(net.lock);
 CHECK(net.policy.queued==0);native_rx_info(&info,&core,20);
 mutex_enter(net.lock);rtw88_net_rx_frame(&net,bar->m_data,20,&info);rtw88_net_request(&net.policy);rtw88_net_commit(&net.policy,net.policy.epoch,0,1);mutex_exit(net.lock);
 rtw88_net_softintr(&net);CHECK(net.policy.queued==0 && net.policy.rx.delivery_epoch==1 && rxba_output_count==6);
 mutex_enter(net.lock);native_rx_info(&info,&core,20);rtw88_net_rx_frame(&net,bar->m_data,20,&info);net.core->sc_generation++;mutex_exit(net.lock);
 rtw88_net_softintr(&net);CHECK(net.policy.queued==0 && net.stats.fw_stale==1 && rxba_output_count==6);
 m_freem(bar);
 /* Stale due work cannot acquire a stale node: only the current BSS is ref'd. */
 net.rxba_due=old;net.ic.ic_bss=NULL;rtw88_net_softintr(&net);
 CHECK(f.ni.references==1 && net.policy.rxba_mask==0);
 net.ic.ic_bss=&f.ni;net.policy.dying=1;
 CHECK(net.ic.ic_rxba_ops->schedule(&net.ic,1400,88)==ENXIO);
 net.rxba_token=88;net.rxba_deadline=1400;unit_now=1400;
 unsigned before=native_schedules;net.rxba_callout.fn(&net);CHECK(native_schedules==before);
 unit_native_detach(&net);
 CHECK(rtw88_net_ht20_snapshot_admit(net.ht20_snapshot,&snapshot)==EOPNOTSUPP);
 CHECK(native_halts>=1 && native_destroys==2 && native_ifdetach==1 && native_released==1 && native_core_stops==1);
 ccmp_detach(&f.tx);CHECK(rxba_kmem_live==0 && f.ni.references==1);
}
static int native_state_hook(struct ieee80211com *ic,int state,int arg)
{ return rtw88_net_newstate(ic,(enum ieee80211_state)state,arg); }
static void test_native_request_purge(void)
{
 for(unsigned kind=0;kind<3;kind++) {
  struct unit_fixture f;if(!unit_setup(&f,10,64,0))return;
  struct rtw88_net net={0};struct rtw88_softc core={0};net.core=&core;net.parent_state=native_parent;
  callout_init(&net.scan,0);net.ic=f.ic;net.ec.ec_if=f.ifp;net.lock=&native_lock;net.wq=&net;
  net.ic.ic_ifp=&net.ec.ec_if;net.ec.ec_if.if_softc=&net;net.ec.ec_if.model_ic=&net.ic;f.ni.ni_ic=&net.ic;
  net.ec.ec_if.if_flags|=IFF_RUNNING;net.policy.accepting=1;core.sc_linked=core.sc_ra.active=core.sc_ra.configured=1;
  unit_native_attach(&net);
  CHECK(net.ic.ic_rxba_ops!=&unit_ops && net.ic.ic_rxba_ops!=NULL);
  if(net.ic.ic_rxba_ops==&unit_ops || net.ic.ic_rxba_ops==NULL) {
   f.ni.ni_ic=&f.ic;unit_close(&f);callout_destroy(&net.scan);continue;
  }
  unit_state_hook=native_state_hook;
  struct mbuf *m=unit_wire(&f,11,11);ieee80211_input(&net.ic,m,&f.ni,0,0);
  unit_now=1100;net.rxba_callout.fn(&net);CHECK(net.rxba_due!=0);
  unsigned requests=native_work_requests;
  if(kind==0) {
   unit_profile_generation++;CHECK(ieee80211_rxba_mask(&net.ic,&f.ni)==0);
   CHECK(net.policy.epoch!=0 && net.requested==IEEE80211_S_INIT && !core.sc_ra.active && !core.sc_ra.configured);
   CHECK(net.work_pending && net.reset_required && native_work_requests==requests+1);
  } else if(kind==1) {
   uint64_t epoch=f.ni.ni_rxba.key_epoch;
   CHECK(ieee80211_crypto_setkey(&net.ic,&f.ni.ni_ucastkey,f.ni.ni_macaddr)==1);
   CHECK(net.rxba_due==0 && f.ni.ni_rxba.key_epoch>epoch);
   CHECK(rtw88_net_newstate(&net.ic,IEEE80211_S_INIT,-1)==0);
   CHECK(native_work_requests==requests+1);
  } else {
   rtw88_net_close(&net.policy);
   CHECK(rtw88_net_newstate(&net.ic,IEEE80211_S_INIT,-1)==ENXIO);
   CHECK(native_work_requests==requests);
  }
  CHECK(!net.policy.accepting && !f.ni.ni_ht20.valid && f.ni.ni_rxba.bytes==0 && net.rxba_due==0 && net.rxba_token==0);
  rtw88_net_softintr(&net);CHECK(rxba_output_count==0 && f.ni.references==1);
  unit_native_detach(&net);unit_state_hook=NULL;ccmp_detach(&f.tx);CHECK(rxba_kmem_live==0);
 }
}
int main(void)
{
 rxba_shared_main();test_native_timer();test_native_request_purge();test_native_beacon_presence();
 printf("RX BA native timer: %u checks, %u failures\n",checks,failures);
 return failures!=0;
}
