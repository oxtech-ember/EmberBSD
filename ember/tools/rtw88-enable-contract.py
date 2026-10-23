#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Execute owning attach and state work with actual shared WME and core consumers.
from pathlib import Path
import re
import sys
src,out=map(Path,sys.argv[1:])
def read(p):return (src/p).read_text()
def function(text,name,result):
 m=re.search(r'^'+name+r'\(.*?^}',text,re.M|re.S)
 if not m:raise ValueError(name)
 return result+'\n'+m.group()+'\n'
def replace(s,old,new):
 if s.count(old)!=1:raise ValueError(old)
 return s.replace(old,new)
model=(out/'routing-model.inc').read_text()
# Active HT negotiation must use the same actual channel helper as native code.
model=replace(model,'#define ieee80211_chan2ieee(ic, c) 1U','struct ieee80211com; struct ieee80211_channel;\nunsigned ieee80211_chan2ieee(struct ieee80211com *,struct ieee80211_channel *);')
model=replace(model,'#define isclr(a, n) (((a)[(n) / 8] & (1U << ((n) % 8))) == 0)', '#define isclr(a,n) (!enable_isset((a),(n)))')
model=replace(model,'    int if_timer, if_flags;','''    int if_timer, if_flags;
    uint64_t if_baudrate;
    int (*if_init)(struct ifnet *);
    void (*if_stop)(struct ifnet *,int), (*if_start)(struct ifnet *);
    int (*if_ioctl)(struct ifnet *,u_long,void *);
    void (*if_watchdog)(struct ifnet *);
    char if_xname[16];
    void *if_percpuq;''')
model=replace(model,'unsigned ic_inact_run;', 'unsigned ic_inact_run;\n    unsigned ic_modecaps,ic_bintval,ic_dtim_period,ic_txpowlimit;\n    struct ieee80211_channel *ic_des_chan,*ic_ibss_chan;\n    uint8_t ic_chan_avail[32];\n    int (*ic_reset)(struct ifnet *);\n    int (*ic_newstate)(struct ieee80211com *,enum ieee80211_state,int);\n    struct ifmedia ic_media;')
model=replace(model,'\t(void)ifp;\n\tassert(captured == NULL);','''    if(ifp->if_start!=NULL) { ifp->if_start(ifp);return; }
    (void)ifp;
    assert(captured == NULL);''')
# The shared fixture can dispatch its own test callback before the state model.
# Extend only that model. Preserve callback return values and fallback ordering.
state_model=re.search(r'^static int\nieee80211_new_state\(.*?^}',model,re.M|re.S)
if state_model is None:raise ValueError('shared new-state service model')
old_state=state_model.group()
new_state=replace(old_state,'\tassert(!beacon_lock);','\tassert(!beacon_lock);\n\tif (ic->ic_newstate != NULL)\n\t\treturn ic->ic_newstate(ic, (enum ieee80211_state)state, arg);')
model=replace(model,old_state,new_state)
(out/'routing-model.inc').write_text(model)
# Extend only the service/layout model; never rewrite an owning production body.
routing=read('ember/tools/rtw88-routing-contract.c')
routing=replace(routing,'static uint64_t rtw88_net_rxba_now(struct ieee80211com *ic) { (void)ic;return 1; }','static uint64_t rtw88_net_rxba_now(struct ieee80211com *ic);')
routing=replace(routing,'#include "routing-constants.inc"','#include "routing-constants.inc"\n#include "enable-constants.inc"\nstruct ifmedia_entry { int ifm_media; };\nstruct ifmedia { struct ifmedia_entry entries[128],*ifm_cur; int ifm_media; unsigned count; };\nstruct ifmediareq { int ifm_active; };\ntypedef int ieee80211_media_lock_t;\nstruct ifnet;\ntypedef int (*ifm_change_cb_t)(struct ifnet *);\ntypedef void (*ifm_stat_cb_t)(struct ifnet *,struct ifmediareq *);')
routing=replace(routing,'    struct ieee80211com ic;','''    struct ieee80211com ic;
    volatile uint64_t ht20_snapshot;
    volatile uint32_t ht20_generation;
    device_t dev;
    unsigned configured_media;
    int (*parent_state)(struct ieee80211com *,enum ieee80211_state,int);''')
routing=replace(routing,'static unsigned routing_ht20_withdrawals, routing_ht20_rejections;\n#include "routing-ht20-stub.inc"\n#define ieee80211_new_state route_new_state\n#include "routing-native.inc"','#include "rtw88-enable-services.inc"\nstruct rtw88_net; struct ieee80211_ht20_profile;\nstatic void rtw88_net_ht20_publish(struct rtw88_net *, const struct ieee80211_ht20_profile *);\n#define ieee80211_new_state route_new_state\n#include "routing-native.inc"\n#include "enable-native.inc"')
# Keep the real RUN-to-SCAN checks, but omit counters owned by the routing stub.
routing=replace(routing,'    unsigned ht20_withdrawals_before=routing_ht20_withdrawals;\n    unsigned ht20_rejections_before=routing_ht20_rejections;\n','')
routing=replace(routing,'    CHECK(routing_ht20_withdrawals==ht20_withdrawals_before+1);\n    CHECK(routing_ht20_rejections==ht20_rejections_before);\n','')
routing=replace(routing,'int main(void)\n{','int routing_base_main(void)\n{')
(out/'enable-routing.inc').write_text(routing)
# Native channel/mode constants and IFM declarations; existing WME model owns types.
parts=['static int enable_isset(const uint8_t *a,unsigned n) { assert(n<256);return (a[n/8]&(1U<<(n%8)))!=0; }']
# The shared WME extraction owns the actual PHY mode enum.
for name in ('sys/net80211/_ieee80211.h','sys/net/if_media.h','sys/net80211/ieee80211_var.h'):
 text=read(name);text=re.sub(r'/\*.*?\*/','',text,flags=re.S).replace('\\\n','')
 for line in text.splitlines():
  if re.match(r'^#define\s+(?:_?IEEE80211_(?:CHAN_|IS_CHAN|MODE_AUTO|BINTVAL_DEFAULT|DTIM_DEFAULT|TXPOWER_MAX)|_?IFM_)',line):
   if re.match(r'^#define\s+IEEE80211_IS_CHAN_(?:2GHZ|FHSS)\(',line):continue
   parts.append(line)
parts.append(re.search(r'^#define\s+CALLOUT_MPSAFE\s+.*$',read('sys/sys/callout.h'),re.M).group())
parts+=['#define IEEE80211_BEACON_LOCK_INIT(...) ((void)0)','#define LIST_INSERT_HEAD(...) ((void)0)']
(out/'enable-constants.inc').write_text('\n'.join(parts)+'\n')
# Keep complete actual shared ifattach and setmode, including its disabled default.
ieee=read('sys/net80211/ieee80211.c');proto=read('sys/net80211/ieee80211_proto.c');net=read('sys/dev/ic/rtw88_net.c')
parts=['#undef ieee80211_chan2ieee\n',ieee[:ieee.index('#include')]]
for name in ('11a','11b','11g'):
 parts.append(re.search(r'^const struct ieee80211_rateset ieee80211_std_rateset_'+name+r'\s*=.*?;',ieee,re.M|re.S).group())
for name,result in [('ieee80211_chan2ieee','unsigned'),('ieee80211_ieee2mhz','unsigned'),('ieee80211_set11gbasicrates','void'),('ieee80211_setmode','int'),('ieee80211_ifattach','void'),('ieee80211_rate2media','int'),('ieee80211_media_init_with_lock','void'),('ieee80211_media_init','void')]:
 body=function(proto if name=='ieee80211_set11gbasicrates' else ieee,name,result)
 if name=='ieee80211_ifattach':
  body='#pragma GCC diagnostic push\n#pragma GCC diagnostic ignored "-Wsign-compare"\n'+body+'#pragma GCC diagnostic pop\n'
 parts.append(body)
(out/'enable-shared.inc').write_text('\n'.join(parts))
bsd=read('sys/net80211/ieee80211_netbsd.c')
parts=[net[:net.index('#include')], bsd[:bsd.index('#include')],
       'static volatile uint32_t *enable_ht20_pause_generation;\n',
       'static volatile unsigned enable_ht20_pause_requested, enable_ht20_pause_entered, enable_ht20_pause_release;\n',
       'static pthread_mutex_t enable_ht20_pause_mutex = PTHREAD_MUTEX_INITIALIZER;\n',
       'static pthread_cond_t enable_ht20_pause_cond = PTHREAD_COND_INITIALIZER;\n',
       'static uint32_t enable_atomic_cas_32(volatile uint32_t *p,uint32_t o,uint32_t n) { uint32_t e=o; int updated=__atomic_compare_exchange_n(p,&e,n,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST); if (updated && p==enable_ht20_pause_generation && __atomic_exchange_n(&enable_ht20_pause_requested,0,__ATOMIC_ACQ_REL)) { pthread_mutex_lock(&enable_ht20_pause_mutex); __atomic_store_n(&enable_ht20_pause_entered,1,__ATOMIC_RELEASE); pthread_cond_broadcast(&enable_ht20_pause_cond); while (!__atomic_load_n(&enable_ht20_pause_release,__ATOMIC_ACQUIRE)) pthread_cond_wait(&enable_ht20_pause_cond,&enable_ht20_pause_mutex); pthread_mutex_unlock(&enable_ht20_pause_mutex); } return e; }\n',
       'static uint64_t enable_atomic_cas_64(volatile uint64_t *p,uint64_t o,uint64_t n) { uint64_t e=o; __atomic_compare_exchange_n(p,&e,n,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST); return e; }\n',
       '#ifndef atomic_load_relaxed\n#define atomic_load_relaxed(p) __atomic_load_n((p), __ATOMIC_RELAXED)\n#endif\n',
       '#ifndef atomic_load_acquire\n#define atomic_load_acquire(p) __atomic_load_n((p), __ATOMIC_ACQUIRE)\n#endif\n',
       '#ifndef atomic_store_release\n#define atomic_store_release(p,v) __atomic_store_n((p),(v),__ATOMIC_RELEASE)\n#endif\n',
       '#ifndef atomic_cas_32\n#define atomic_cas_32(p,o,n) enable_atomic_cas_32((p),(o),(n))\n#endif\n',
       '#ifndef atomic_cas_64\n#define atomic_cas_64(p,o,n) enable_atomic_cas_64((p),(o),(n))\n#endif\n',
       '#define ifqueue queue\n', function(bsd,'ieee80211_drain_ifq','static void'),
       '#undef ifqueue\n']
for name,result in [('rtw88_net_rxba_now','static uint64_t'),('rtw88_net_rxba_ticks','static int'),('rtw88_net_rxba_timeout','static void'),('rtw88_net_rxba_schedule','static int'),('rtw88_net_rxba_cancel','static void'),('rtw88_net_rxba_quiesce','static void'),('rtw88_net_rxba_attach','static void'),('rtw88_net_rxba_detach','static void'),('rtw88_net_basic_rates','static unsigned'),('rtw88_net_wme_apply','static int'),('rtw88_net_edca_prepare','static int'),('rtw88_net_ra_prepare','static int'),('rtw88_net_restart_peer','static int'),('rtw88_net_drain_wait','static int'),('rtw88_net_mailbox_wait','static int'),('rtw88_net_state_work','static void'),('rtw88_net_ht20_publish','static void'),
('rtw88_net_ht20_admit','static int'),('rtw88_net_attach','int')]:
 # A source baseline may predate the checkpoint; no replacement body is supplied.
 if name in ('rtw88_net_drain_wait','rtw88_net_mailbox_wait') and not re.search(r'^'+name+r'\(',net,re.M):continue
 if name=='rtw88_net_rxba_attach':
  parts.append(re.search(r'^static const struct ieee80211_rxba_ops rtw88_net_rxba_ops = \{.*?^};',net,re.M|re.S).group()+'\n')
 parts.append(function(net,name,result))
(out/'enable-native.inc').write_text('\n'.join(parts))
