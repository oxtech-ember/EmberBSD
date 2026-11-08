/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD RTL8723D contract tests, AI-assisted. */
/* Actual Giant TX producer against the existing synthetic net80211 model. */

#define rtw88_net_start_async routing_async_dispatch_probe
#include "routing-fixture.c"
#undef rtw88_net_start_async
#undef ifp
#define RTW88_NET_TX_BUDGET 16U
static unsigned async_schedules;
static void rtw88_executor_schedule(struct rtw88_softc *sc,uint32_t why)
{ (void)sc; if (why) async_schedules++; }
static void rtw88_net_fault(struct rtw88_net *net,int error)
{ net->async_fault=error;net->stop_pending=1; }
static uint32_t rtw88_net_now_ms(void){return 1;}
#define RTW88_EXEC_TX 2U
#define ieee80211_new_state route_new_state
#include "routing-async.inc"
#undef ieee80211_new_state
int main(void)
{
 struct rtw88_softc sc={0}; struct rtw88_net net={0};
 struct ieee80211_node ni={0}; struct ieee80211_channel ch={2412,0};
 kmutex_t lock={.initialized=1},qlock={.initialized=1};
 struct rtw88_async_epoch id={.generation=1,.epoch=1,.key_epoch=1};
 struct rtw88_async_ticket ticket; const struct rtw88_tx_job *job;
 struct ifnet *ifp=&net.ec.ec_if;
 ifp->if_softc=&net; ifp->if_flags=IFF_RUNNING; ifp->if_snd.ifq_lock=&qlock;
 net.core=&sc;net.lock=&lock;net.ic.ic_ifp=ifp;net.ic.ic_bss=&ni;net.ic.ic_curchan=&ch;
 net.ic.ic_state=IEEE80211_S_RUN;net.ic.ic_opmode=IEEE80211_M_STA;
 net.ic.ic_fixed_rate=IEEE80211_FIXED_RATE_NONE;net.ic.ic_curmode=IEEE80211_MODE_11G;
 ni.ni_ic=&net.ic;ni.ni_chan=&ch;ni.ni_associd=1;ni.ni_bssid[0]=2;
 net.caps.tx_desc_size=40;net.policy.accepting=1;net.policy.epoch=1;
 net.key_epoch=1;net.hw.generation=1;net.hw.ra.configured=net.hw.ra.active=1;
 sc.sc_generation=1;rtw88_async_init(&net.async,&id);
 struct mbuf *m=ether_packet(0,0,0);queue_packet(&ifp->if_snd,m);
 unsigned writes=f.writes;
 rtw88_net_start_async(ifp);
 assert(ifp->if_snd.m==NULL && net.async.total==1 && async_schedules==1);
 assert(f.writes==writes && !lock.held);
 mutex_enter(&lock);assert(rtw88_async_take(&net.async,1,&ticket,&job)==0);
 assert(job->mode==RTW88_TX_MODE_AUTO && job->id.key_epoch==1 && job->length>=24);
 assert(rtw88_async_finish(&net.async,&ticket,0,RTW88_TX_ACCEPTED,1)==0);mutex_exit(&lock);
 /* Epoch rejection must cancel the reservation before crypto or SDIO I/O. */
 m=newbuf(30); assert(m); m->m_data[0]=0xb0;
 ieee80211_ref_node(&ni); M_SETCTX(m,&ni);
 queue_packet(&net.ic.ic_mgtq,m); route_sae_stale=m;
 unsigned crypto=route_crypto, errors=route_errors, schedules=async_schedules;
 rtw88_net_start_async(ifp); route_sae_stale=NULL;
 assert(net.ic.ic_mgtq.m==NULL && net.async.total==0 && node_refs==1);
 assert(route_errors==errors+1 && route_crypto==crypto && async_schedules==schedules);
 sc.sc_executor=(void *)&sc; routing_async_dispatch_calls=0;
 rtw88_net_start(ifp);
 assert(routing_async_dispatch_calls==1 && f.writes==writes);
 return 0;
}
