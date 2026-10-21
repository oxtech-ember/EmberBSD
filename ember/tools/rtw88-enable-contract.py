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
routing=replace(routing,'#include "routing-constants.inc"','#include "routing-constants.inc"\n#include "enable-constants.inc"\nstruct ifmedia_entry { int ifm_media; };\nstruct ifmedia { struct ifmedia_entry entries[128],*ifm_cur; int ifm_media; unsigned count; };\nstruct ifmediareq { int ifm_active; };\ntypedef int ieee80211_media_lock_t;\nstruct ifnet;\ntypedef int (*ifm_change_cb_t)(struct ifnet *);\ntypedef void (*ifm_stat_cb_t)(struct ifnet *,struct ifmediareq *);')
routing=replace(routing,'    struct ieee80211com ic;','''    struct ieee80211com ic;
    device_t dev;
    unsigned configured_media;
    int (*parent_state)(struct ieee80211com *,enum ieee80211_state,int);''')
routing=replace(routing,'#define ieee80211_new_state route_new_state\n#include "routing-native.inc"','#include "rtw88-enable-services.inc"\n#define ieee80211_new_state route_new_state\n#include "routing-native.inc"\n#include "enable-native.inc"')
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
       '#define ifqueue queue\n', function(bsd,'ieee80211_drain_ifq','static void'),
       '#undef ifqueue\n']
for name,result in [('rtw88_net_basic_rates','static unsigned'),('rtw88_net_wme_apply','static int'),('rtw88_net_edca_prepare','static int'),('rtw88_net_ra_prepare','static int'),('rtw88_net_restart_peer','static int'),('rtw88_net_state_work','static void'),('rtw88_net_attach','int')]:
 parts.append(function(net,name,result))
(out/'enable-native.inc').write_text('\n'.join(parts))
