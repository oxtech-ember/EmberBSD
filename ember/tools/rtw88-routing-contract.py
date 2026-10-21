#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD actual native routing consumer extraction, AI-assisted.
import pathlib
import re
import sys

src, out = map(pathlib.Path, sys.argv[1:])
# Reuse the existing kernel service model, not a second classifier/encap model.
fixture = (src/'ember/tools/net80211-wme-contract.c').read_text()
# Retain the complete existing suite so every shared service model is exercised.
fixture = fixture.replace('struct mbuf *m_next;', 'struct mbuf *m_next, *m_nextpkt;')
fixture = fixture.replace('struct ifnet {\n\tint if_timer, if_flags;\n};\nstruct queue {\n\tstruct mbuf *m;\n};', '''struct queue {
    union { struct mbuf *m; struct mbuf *ifq_head; };
    struct mbuf *ifq_tail;
    int ifq_len;
    kmutex_t *ifq_lock;
    int altq_enabled, tbr_enabled;
};
struct ifnet {
    int if_timer, if_flags;
    void *if_softc;
    struct queue if_snd;
};''')
fixture = fixture.replace('#define ieee80211_crypto_getucastkey(...) NULL', 'static struct ieee80211_key route_key;\nstatic int route_have_key;\n#define ieee80211_crypto_getucastkey(...) (route_have_key ? &route_key : NULL)')
fixture = fixture.replace('unsigned ni_fails;', 'unsigned ni_fails;\nint ni_txrate;')
fixture = fixture.replace('struct queue ic_mgtq;', 'struct queue ic_mgtq;\nint ic_fixed_rate;\nvoid *ic_rawbpf;')
(out/'routing-model.inc').write_text(fixture)
net = (src/'sys/dev/ic/rtw88_net.c').read_text()
parts = [net[:net.index('#include')]]
for m in re.finditer(r'^static (?:int|unsigned|void|struct mbuf \*)\n(rtw88_net_(?:tx_[a-z_]+|start|newstate|softintr))\(.*?^}', net, re.M|re.S):
    parts.append(m.group())
if 'rtw88_net_tx_dequeue_same(' in net:
    parts.insert(0, '#define ROUTING_NATIVE_QUEUE_HELPERS 1\n')
(out/'routing-native.inc').write_text('\n'.join(parts)+'\n')

var = (src/'sys/net80211/ieee80211_var.h').read_text()
proto=(src/'sys/net80211/ieee80211_proto.h').read_text()
(out/'routing-constants.inc').write_text(re.search(r'^enum ieee80211_state \{.*?^};',proto,re.M|re.S).group()+'\n'+re.search(r'^#define\s+IEEE80211_FIXED_RATE_NONE\s+.*$',var,re.M).group()+'\n')

# Use the complete owning FIFO/IFQ macros; only discipline service is modeled.
ifh=(src/'sys/net/if.h').read_text()
def macro(name, occurrence=0):
    # A physical-line parser preserves all continuation lines verbatim.
    found=[]
    lines=ifh.splitlines(keepends=True)
    for n,line in enumerate(lines):
        if re.match(r'^#define\s+'+name+r'\(',line):
            block=line
            while block.rstrip().endswith('\\'):
                n+=1;block+=lines[n]
            found.append(block)
    return found[occurrence]
parts=[macro('IF_DEQUEUE'),macro('IF_POLL'),'#ifdef ALTQ\n',macro('IFQ_DEQUEUE'),macro('IFQ_POLL'),'#else\n',macro('IFQ_DEQUEUE',1),macro('IFQ_POLL',1),'#endif\n',macro('IFQ_LOCK'),macro('IFQ_UNLOCK')]
(out/'routing-ifq.inc').write_text(''.join(parts))
