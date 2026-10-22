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
fixture, replaced = re.subn(r'^struct ifnet \{.*?^\};\nstruct queue \{.*?^\};', """struct queue {
    union { struct mbuf *m; struct mbuf *ifq_head; };
    struct mbuf *ifq_tail;
    int ifq_len, ifq_maxlen, ifq_drops;
    kmutex_t *ifq_lock;
    int altq_enabled, tbr_enabled;
};
struct ifnet {
    int if_timer, if_flags;
    void *if_softc;
    struct queue if_snd;
};""", fixture, count=1, flags=re.M|re.S)
if replaced != 1:
    raise ValueError('shared queue model changed')
fixture = fixture.replace('#define ieee80211_crypto_getucastkey(...) NULL', 'static struct ieee80211_key route_key;\nstatic int route_have_key;\n#define ieee80211_crypto_getucastkey(...) (route_have_key ? &route_key : NULL)')
fixture = fixture.replace('unsigned ni_fails;', 'unsigned ni_fails;\nint ni_txrate;')
fixture = fixture.replace('struct queue ic_mgtq;', 'struct queue ic_mgtq;\nvoid *ic_rawbpf;')
(out/'routing-model.inc').write_text(fixture)
net = (src/'sys/dev/ic/rtw88_net.c').read_text()
parts = [net[:net.index('#include')]]
for m in re.finditer(r'^static (?:int|unsigned|void|struct mbuf \*)\n(rtw88_net_(?:tx_[a-z_]+|start|newstate|softintr))\(.*?^}', net, re.M|re.S):
    parts.append(m.group())
if 'rtw88_net_tx_dequeue_same(' in net:
    parts.insert(0, '#define ROUTING_NATIVE_QUEUE_HELPERS 1\n')
(out/'routing-native.inc').write_text('\n'.join(parts)+'\n')
parts=[]
for name in ['rtw88_net_rxba_publish','rtw88_net_rxba_softintr']:
 parts.append(net[:net.index('#include')]+'static void\n'+re.search(r'^'+name+r'\(.*?^}',net,re.M|re.S).group()+'\n')
(out/'routing-rxba-native.inc').write_text('\n'.join(parts))


var = (src/'sys/net80211/ieee80211_var.h').read_text()
proto=(src/'sys/net80211/ieee80211_proto.h').read_text()
(out/'routing-constants.inc').write_text('#ifndef NATIVE_IEEE80211_STATE_DEFINED\n#define NATIVE_IEEE80211_STATE_DEFINED 1\n'+re.search(r'^enum ieee80211_state \{.*?^};',proto,re.M|re.S).group()+'\n#endif\n'+re.search(r'^#define\s+IEEE80211_FIXED_RATE_NONE\s+.*$',var,re.M).group()+'\n')

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

# The wrapper must not use the standalone WME model's separate AC field.
# Execute native flags, the BPF duplicate guard, and real packet-header moves.
fixture=(out/'routing-model.inc').read_text()
# Native management queues may hold multiple frames during a sleeping worker.
# Keep owning enqueue/dequeue tails; the standalone WME capture models one pop.
old_enqueue='#define IF_ENQUEUE(q, m) ((q)->ifq_len++, (q)->m = (m))'
if fixture.count(old_enqueue)!=1: raise ValueError('shared enqueue model changed')
fixture=fixture.replace(old_enqueue,macro('IF_ENQUEUE')+macro('IF_DEQUEUE'))
old_pop='\tactive_ic->ic_mgtq.m = NULL;\n\tactive_ic->ic_mgtq.ifq_len--;'
if fixture.count(old_pop)!=1: raise ValueError('shared capture pop changed')
fixture=fixture.replace(old_pop,'\tIF_DEQUEUE(&active_ic->ic_mgtq, captured);')

fixture=fixture.replace('int m_len, m_flags, ac;', 'int m_len, m_flags, ac, m_type;')
fixture=fixture.replace('int len;\n\t\tstruct ieee80211_node *ctx;', 'int len;\n        unsigned rcvif_index;\n        struct { void *slh_first; } tags;\n\t\tstruct ieee80211_node *ctx;')
fixture=fixture.replace('struct cipher *wk_cipher;', 'struct cipher *wk_cipher;\n    unsigned wk_flags;')
(out/'routing-model.inc').write_text(fixture)
def body(path,name,result):
    text=(src/path).read_text()
    prefix=text[:re.search(r'^#include',text,re.M).start()]
    notices='\n'.join(re.findall(r'/\*.*?\*/',prefix,re.S))
    return notices+'\n'+result+'\n'+re.search(r'^'+name+r'\(.*?^}',text,re.M|re.S).group()+'\n'
def named_macro(path,name):
    lines=(src/path).read_text().splitlines(keepends=True)
    for i,line in enumerate(lines):
        if re.match(r'^#define\s+'+name+r'(?:\(|\s)',line):
            while line.rstrip().endswith('\\'):
                i+=1;line+=lines[i]
            return line
    raise ValueError(name)
parts=[]
for line in (src/'sys/sys/mbuf.h').read_text().splitlines():
    if re.match(r'^#define\s+M_[A-Z0-9_]+\s+0x[0-9a-fA-F]+\b',line):
        name=line.split()[1];parts+=['#undef '+name+'\n',line.split('/*',1)[0].rstrip()+'\n']
parts.append(named_macro('sys/sys/mbuf.h','M_COPYFLAGS'))
for name in ['M_MORE_DATA','M_FF','M_WME_AC_MASK','M_WME_AC_SHIFT','M_WME_SETAC','M_WME_GETAC']:
    parts+=['#undef '+name+'\n',named_macro('sys/net80211/ieee80211_netbsd.h',name)]
parts.append(named_macro('sys/net/if_ether.h','M_PROMISC'))
parts.append('''#ifndef __predict_false
#define __predict_false(x) (x)
#endif
#define m_pktdat data
#ifndef SLIST_FIRST
#define SLIST_FIRST(h) ((h)->slh_first)
#endif
#define MHLEN 256
#define M_NOWAIT M_DONTWAIT
#define IEEE80211_KEY_SWCRYPT 1
#define M_COPYALL 1000000000
#define M_LEADINGSPACE(m) ((int)((m)->m_data-(m)->data))
static struct mbuf *m_gethdr(int how,int type) {
    (void)how;(void)type;
    if(adjust_failure)return NULL;
    struct mbuf *m=newbuf(0);
    if(m){m->m_flags=M_PKTHDR;m->m_data=m->data;}
    return m;
}
/* All fixture storage is writable; COW and MMU permissions are not modeled. */
static int m_makewritable(struct mbuf **m,int off,int len,int how) {
    (void)m;(void)off;(void)len;(void)how;return adjust_failure;
}
''')
parts.append(body('sys/kern/uipc_mbuf.c','m_move_pkthdr','static void'))
parts+=['#define ieee80211_mbuf_adjust routing_mbuf_adjust\n',body('sys/net80211/ieee80211_output.c','ieee80211_mbuf_adjust','static struct mbuf *')]
parts.append('''static struct mbuf *routing_newbuf(size_t len) {
    struct mbuf *m=newbuf(len);if(m)m->m_flags=M_PKTHDR;return m;
}
#define newbuf routing_newbuf
struct bpf_if { unsigned raw; };
static unsigned route_eth_delivered,route_raw_delivered;
static unsigned m_length(struct mbuf *m) { unsigned n=0;for(;m;m=m->m_next)n+=(unsigned)m->m_len;return n; }
static void *bpf_mcpy(void *dst,const void *src,size_t len) {m_copydata((struct mbuf *)src,0,(int)len,dst);return dst;}
static void bpf_deliver(struct bpf_if *bp,void *(*copy)(void *,const void *,size_t),void *arg,unsigned n,unsigned contiguous,unsigned direction) {
    (void)copy;(void)arg;(void)n;(void)contiguous;(void)direction;
    if(bp->raw)route_raw_delivered++;else route_eth_delivered++;
}
''')
parts.append(body('sys/net/bpf.c','_bpf_mtap','static void'))
parts+=['#define ieee80211_classify routing_native_classify\n',body('sys/net80211/ieee80211_output.c','ieee80211_classify','static int')]
encap=body('sys/net80211/ieee80211_output.c','ieee80211_encap','static struct mbuf *')
# GCC16 analyzer conflates the dereferenced input with the reassigned return
# (same/new/NULL) of actual mbuf_adjust. Exempt only this required check;
# the remaining encap and complete adjust body retain all diagnostics.
check='\tm = ieee80211_mbuf_adjust(ic, hdrsize, key, m);\n\tif (m == NULL) {'
assert encap.count(check)==1
encap=encap.replace(check, '''\tm = ieee80211_mbuf_adjust(ic, hdrsize, key, m);
#if defined(__GNUC__) && __GNUC__ == 16 && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wanalyzer-deref-before-check"
#endif
\tif (m == NULL) {
#if defined(__GNUC__) && __GNUC__ == 16 && !defined(__clang__)
#pragma GCC diagnostic pop
#endif''')
parts+=['#define ieee80211_encap routing_native_encap\n',encap]
(out/'routing-bpf.inc').write_text(''.join(parts).replace('#define IEEE80211_KEY_SWCRYPT 1',named_macro('sys/net80211/ieee80211_crypto.h','IEEE80211_KEY_SWCRYPT').strip()))
