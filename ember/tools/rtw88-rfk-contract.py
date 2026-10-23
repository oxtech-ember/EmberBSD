#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: native RFK worker source and service extraction, AI-assisted.
# Extend only layout/services for the actual experimental worker bodies.
from pathlib import Path
import re, sys
src,out=map(Path,sys.argv[1:])
def replace(s,a,b):
    if s.count(a)!=1:raise ValueError(a)
    return s.replace(a,b)
net=(src/'sys/dev/ic/rtw88_net.c').read_text()
# Preserve the actual wrapper's net80211 clock macro at both RFK boundaries.
# Core-only compilation does not include ieee80211_netbsd.h and misses this.
bsd=(src/'sys/net80211/ieee80211_netbsd.h').read_text()
clock_macro=re.search(r'^#define\s+ticks\s+getticks\(\)\s*$',bsd,re.M).group()+'\n'
layout=re.search(r'^struct rtw88_net_rfk \{.*?^};',net,re.M|re.S).group()
routing=(out/'enable-routing.inc').read_text()
routing=replace(routing,'struct rtw88_net {',clock_macro+'#include <dev/ic/rtw8822c_rfk.h>\n#undef ticks\n'+layout+';\nstruct rtw88_net {')
routing=replace(routing,'    unsigned configured_media;','    unsigned configured_media;\n    struct rtw88_net_rfk rfk;')
routing=replace(routing,'void mutex_enter(kmutex_t *l) { assert(!l->held);l->held=1; }',
    'static void (*rfk_mutex_hook)(kmutex_t *);\n'
    'static void (*rfk_unlock_hook)(kmutex_t *);\n'
    'void mutex_enter(kmutex_t *l) { assert(!l->held);l->held=1; if(rfk_mutex_hook)rfk_mutex_hook(l); }')
routing=replace(routing,'void mutex_exit(kmutex_t *l) { assert(l->held);l->held=0; }',
    'void mutex_exit(kmutex_t *l) { assert(l->held); if(rfk_unlock_hook)rfk_unlock_hook(l); l->held=0; }')
(out/'enable-routing.inc').write_text(routing)
model=(out/'routing-model.inc').read_text()
node=(src/'sys/net80211/ieee80211_node.h').read_text()
notice=node[:node.index('#ifndef')]
rsn=notice+re.search(r'^struct ieee80211_rsnparms \{.*?^};',node,re.M|re.S).group()
model=replace(model,'struct test_rsn { uint8_t rsn_ucastcipher; };',rsn)
model=replace(model,'struct test_rsn ni_rsn;','struct ieee80211_rsnparms ni_rsn;')
model=replace(model,'unsigned ic_flags_ext, ic_bmiss_count, ic_sta_assoc;', 'unsigned ic_flags_ext, ic_bmiss_count, ic_sta_assoc;\n    unsigned ic_protmode;')
(out/'routing-model.inc').write_text(model)
native=(out/'enable-native.inc').read_text()
# Routing owns the complete softintr and is emitted before these RFK helpers.
routing_native=(out/'enable-routing-native.inc').read_text()
(out/'enable-routing-native.inc').write_text('static void rtw88_net_rfk_rx_deliver(struct rtw88_net *);\n'+routing_native)
helpers=(src/'sys/dev/ic/rtw88_net_rfk.inc').read_text()
native=replace(native,'static void\nrtw88_net_state_work(',clock_macro+helpers+'\n#undef ticks\nstatic void\nrtw88_net_state_work(')
(out/'enable-native.inc').write_text(native)
services=(src/'ember/tools/rtw88-enable-services.inc').read_text()
services=replace(services, '#define hz 1000',
    'static unsigned rfk_hz = 1000;\n#define hz rfk_hz\n'
    'static void nanouptime(struct timespec *);\nstatic void yield(void);')
services=replace(services, 'static unsigned mstohz(unsigned ms) { return ms; }',
    'static unsigned mstohz(unsigned ms) { return (ms * hz + 999) / 1000; }')

services=replace(services,'void *kmem_zalloc(size_t n,int flags)',
    'static void rfk_alloc_yield(void);\nvoid *kmem_zalloc(size_t n,int flags)')
services=replace(services,'enable_allocations++;return p;',
    'enable_allocations++;rfk_alloc_yield();return p;')
(out/'rtw88-enable-services.inc').write_text(services)
