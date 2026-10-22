#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Extend only layout/services for the actual experimental worker bodies.
from pathlib import Path
import re, sys
src,out=map(Path,sys.argv[1:])
def replace(s,a,b):
    if s.count(a)!=1:raise ValueError(a)
    return s.replace(a,b)
net=(src/'sys/dev/ic/rtw88_net.c').read_text()
layout=re.search(r'^struct rtw88_net_rfk \{.*?^};',net,re.M|re.S).group()
routing=(out/'enable-routing.inc').read_text()
routing=replace(routing,'struct rtw88_net {','#include <dev/ic/rtw8822c_rfk.h>\n'+layout+';\nstruct rtw88_net {')
routing=replace(routing,'    unsigned configured_media;','    unsigned configured_media;\n    struct rtw88_net_rfk rfk;')
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
helpers=(src/'sys/dev/ic/rtw88_net_rfk.inc').read_text()
native=replace(native,'static void\nrtw88_net_state_work(',helpers+'\nstatic void\nrtw88_net_state_work(')
(out/'enable-native.inc').write_text(native)
services=(src/'ember/tools/rtw88-enable-services.inc').read_text()
services=replace(services,'void *kmem_zalloc(size_t n,int flags)',
    'static void rfk_alloc_yield(void);\nvoid *kmem_zalloc(size_t n,int flags)')
services=replace(services,'enable_allocations++;return p;',
    'enable_allocations++;rfk_alloc_yield();return p;')
(out/'rtw88-enable-services.inc').write_text(services)
