#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD actual CSA/BA/CCMP consumer contracts, AI-assisted.
"""Actual BA/Action/data/crypto/native consumers with explicit kernel/peer models.

Full association management exchange and hardware programming are separate gates.
"""
import pathlib,re,subprocess,sys,os
src,out=map(pathlib.Path,sys.argv[1:])
subprocess.run([sys.executable,str(src/'ember/tools/ccmp-contract-extract.py'),str(src),str(out)],check=True)
(out/'net80211').mkdir(exist_ok=True)
for name in ['ieee80211_ht20.h','ieee80211_rxba.h','ieee80211_vht_ies.h',
             'ieee80211_phy_ies.h']:
 header=(src/'sys/net80211'/name).read_text()
 (out/name).write_text(header)
 if name in ('ieee80211_vht_ies.h','ieee80211_phy_ies.h'):
  (out/'net80211'/name).write_text(header)
s=(src/'ember/tools/ccmp-test-kernel.h').read_text()
state_enum=re.search(r'^enum ieee80211_state \{.*?^};',(src/'sys/net80211/ieee80211_proto.h').read_text(),re.M|re.S).group()
s=s.replace('struct ieee80211com {','#ifndef NATIVE_IEEE80211_STATE_DEFINED\n#define NATIVE_IEEE80211_STATE_DEFINED 1\n'+state_enum+'\n#endif\nstruct ieee80211com {',1)
s=s.replace('struct ieee80211_node {','''#include "ieee80211_ht20.h"
#include "ieee80211_rxba.h"
#include <net80211/ieee80211_phy_ies.h>
struct ieee80211_node {
 struct ieee80211_ht20_node ni_ht20;
 struct ieee80211_rxba_state ni_rxba;
 struct ieee80211com *ni_ic;
 int authorized;''')
s=s.replace('struct ieee80211com {','''struct ieee80211com {
 void (*ic_sta_quiet)(struct ieee80211com *);
 const struct ieee80211_rxba_ops *ic_rxba_ops;
 uint64_t ic_rxba_serial;
 int ic_state;
 uint8_t ic_myaddr[6];''')
s=s.replace('static void *\nkmem_intr_zalloc', 'static unsigned rxba_kmem_live;\nstatic void *\nkmem_intr_zalloc')
s=s.replace('assert(p);\n\treturn p;','assert(p);\n\trxba_kmem_live++;\n\treturn p;',1)
s=s.replace('(void)n;\n\tfree(p);','(void)n;\n\tassert(rxba_kmem_live>0);rxba_kmem_live--;\n\tfree(p);',1)
(out/'rxba-test-kernel.h').write_text(s)
s=(src/'ember/tools/ccmp-contract.c').read_text().replace('#include "ccmp-test-kernel.h"','#include "rxba-test-kernel.h"')
(out/'rxba-ccmp-contract.inc').write_text(s)
s=pathlib.Path(os.environ.get('RXBA_SOURCE',src/'sys/net80211/ieee80211_rxba.c')).read_text()
(out/'native-rxba.inc').write_text(re.sub(r'^#include[^\n]*\n','',s,flags=re.M))
# Extend only CCMP's modeled services/layout for the actual Action producer.
s=(out/'rxba-test-kernel.h').read_text()
s=s.replace('struct ifnet {\n\tunsigned unused;','struct ifnet {\n int if_timer;\n struct ieee80211com *model_ic;\n\tunsigned unused;')
s=s.replace('int authorized;', '''int authorized;
 unsigned references,ni_flags,ni_wme_attempt,ni_wme_offer_attempt;
 unsigned ni_wme_offer_type;
 uint16_t ni_txseqs[17];
 uint8_t ni_wme_offer_bssid[6];
 void *ni_challenge;
 struct ieee80211_channel *ni_chan,*ni_wme_offer_chan;''')
s=s.replace('uint8_t ic_myaddr[6];','''uint8_t ic_myaddr[6];
 struct ieee80211_channel *ic_curchan;
 struct { int ifq_len,ifq_maxlen,ifq_drops;struct mbuf *m; } ic_mgtq;
 int ic_mgt_timer;''')
s=s.replace('int len;\n\t} m_pkthdr;', 'int len;\n struct ieee80211_node *ctx;\n\t} m_pkthdr;')
s=s.replace('''	return ni;
}
static void
ieee80211_free_node''','''	ni->references++;
	return ni;
}
static void
ieee80211_free_node''')
s=s.replace('''ieee80211_free_node(struct ieee80211_node *ni)
{
	(void)ni;
}''','''ieee80211_free_node(struct ieee80211_node *ni)
{
 assert(ni->references>0);ni->references--;
}''')
(out/'rxba-test-kernel.h').write_text(s)
def extract(path,name,result):
 s=pathlib.Path(os.environ.get({'sys/net80211/ieee80211_input.c':'RXBA_INPUT_SOURCE','sys/net80211/ieee80211_output.c':'RXBA_OUTPUT_SOURCE','sys/net80211/ieee80211_crypto.c':'RXBA_CRYPTO_SOURCE'}.get(path,''),src/path)).read_text()
 return s[:re.search(r'^#',s,re.M).start()]+result+'\n'+re.search(r'^'+name+r'\(.*?^}',s,re.M|re.S).group()+'\n'
parts=[]
for name,result in [('ieee80211_ht20_write','void'),('ieee80211_ht20_offer','void')]:
 parts.append(extract('sys/net80211/ieee80211_ht20.c',name,result))
parts.append(re.search(r'^#define\s+IEEE80211_KEY_UNDEFINED\([^)]*\)\s+.*$',(src/'sys/net80211/ieee80211_crypto.h').read_text(),re.M).group())
parts.append('#include "ieee80211_vht_ies.h"')
vht=(src/'sys/net80211/ieee80211_vht_ies.c').read_text()
parts.append(vht[vht.index('static uint16_t'):])
for name,result in [('ieee80211_crypto_getucastkey','struct ieee80211_key *'),('ieee80211_send_setup','static void'),('ieee80211_wme_request_offer','static int'),('ieee80211_ht20_request_offer','static int'),('ieee80211_vht_request_offer','static int'),('ieee80211_mgmt_output','static int'),('ieee80211_send_rxba_response','int')]:
 parts.append(extract('sys/net80211/ieee80211_output.c',name,result))
output='\n'.join(parts)
offer=re.search(r'^static int\nieee80211_vht_request_offer\(',output,re.M)
mgmt=re.search(r'^static int\nieee80211_mgmt_output\(',output,re.M)
if offer is None or mgmt is None or offer.start() >= mgmt.start():
 raise ValueError('VHT request-offer definition must precede management-output consumer')
(out/'native-rxba-output.inc').write_text(output)
(out/'native-rxba-crypto-encap.inc').write_text(extract('sys/net80211/ieee80211_crypto.c','ieee80211_crypto_encap','struct ieee80211_key *'))
# Complete data/CCMP/decapsulation/Ethernet consumers, with common kernel services.
s=(out/'rxba-test-kernel.h').read_text()
s=s.replace('int if_timer;', 'int if_timer,if_flags;\n void *if_percpuq;\n unsigned if_ipackets,if_ierrors,if_oerrors,if_obytes;\n struct { int unused; } if_snd;')
s=s.replace('int authorized;', 'int authorized;\n uint16_t ni_rxseqs[17],ni_vlan;\n struct mbuf *ni_rxfrag[3];\n void *ni_table;\n unsigned ni_rxfragstamp;')
s=s.replace('int ic_mgt_timer;', 'int ic_mgt_timer;\n void *ic_rawbpf;\n struct { unsigned wme_hipri_traffic; } ic_wme;')
stats=re.search(r'^struct ieee80211_stats \{.*?^};',(src/'sys/net80211/ieee80211_ioctl.h').read_text(),re.M|re.S).group()
s=s.replace('struct ieee80211com {',stats+'\nstruct ieee80211com {')
s=re.sub(r'struct \{\n\t\tunsigned is_rx_tooshort, is_crypto_nomem, is_crypto_ccmp,\n\t\t    is_rx_ccmpformat, is_rx_ccmpreplay, is_rx_ccmpmic;\n\t} ic_stats;', 'struct ieee80211_stats ic_stats;',s)
s=s.replace('struct mbuf *m_next;', 'struct mbuf *m_next;\n struct mbuf *m_nextpkt;')
s=s.replace('int len;\n struct ieee80211_node *ctx;', 'int len;\n unsigned rcvif_index;\n struct ieee80211_node *ctx;')
(out/'rxba-test-kernel.h').write_text(s)
(out/'ieee80211_amsdu.h').write_text((src/'sys/net80211/ieee80211_amsdu.h').read_text())
amsdu=(src/'sys/net80211/ieee80211_amsdu.c').read_text()
parts=[re.sub(r'^#include[^\n]*\n','',amsdu,flags=re.M)]
parts.append(extract('sys/net80211/ieee80211_crypto.h','ieee80211_crypto_demic','static inline int'))
for name,result in [('ieee80211_defrag','static struct mbuf *'),('ieee80211_decap','static struct mbuf *'),('ieee80211_deliver_data','static void'),('ieee80211_amsdu_read_mbuf','static int'),('ieee80211_input_amsdu','static struct mbuf *'),('ieee80211_accept_data','static int'),('ieee80211_input_data','static int'),('ieee80211_input_ordered','void')]:
 parts.append(extract('sys/net80211/ieee80211_input.c',name,result))
(out/'native-rxba-input.inc').write_text('\n'.join(parts))
# Exact Action switch arm. Other management subtypes remain qualified by A/WME.
s=pathlib.Path(os.environ.get('RXBA_INPUT_SOURCE',src/'sys/net80211/ieee80211_input.c')).read_text()
arm=re.search(r'^\tcase IEEE80211_FC0_SUBTYPE_ACTION: \{.*?^\t}',s,re.M|re.S).group()
announcement = extract('sys/net80211/ieee80211_beacon_ies.c','ieee80211_sta_action_announcement','int')
helpers = ''.join(extract('sys/net80211/ieee80211_input.c',name,'static int') for name in ('ieee80211_sta_csa_current','ieee80211_sta_csa_exit','ieee80211_sta_csa_action')) if '\nieee80211_sta_csa_action(' in s else ''
(out/'native-rxba-action.inc').write_text(s[:s.index('#include')]+announcement+helpers+'static void\nunit_recv_action(struct ieee80211com *ic,struct mbuf *m0,struct ieee80211_node *ni) {\nstruct ieee80211_frame *wh=mtod(m0,struct ieee80211_frame *);\nswitch(IEEE80211_FC0_SUBTYPE_ACTION) {\n'+arm+'\n}\n}\n')
# Execute the full common input before its data/control/Action consumers.
s=(out/'rxba-test-kernel.h').read_text()
s=s.replace('struct { int unused; } if_snd;', 'struct { int unused; } if_snd;\n uint8_t if_broadcastaddr[6];')
s=s.replace('unsigned ni_rxfragstamp;', 'unsigned ni_rxfragstamp,ni_inact,ni_inact_reload,ni_rssi,ni_rstamp;')
s=s.replace('int ic_mgt_timer;', 'int ic_mgt_timer;\n void (*ic_recv_mgmt)(struct ieee80211com *,struct mbuf *,struct ieee80211_node *,int,int,uint32_t);')
flags=[]
for line in (src/'sys/sys/mbuf.h').read_text().splitlines():
 if re.match(r'^#define\s+M_[A-Z0-9_]+\s+0x[0-9a-fA-F]+\b',line):
  flags.extend(['#undef '+line.split()[1],line.split('/*',1)[0].rstrip()])
(out/'rxba-mbuf-flags.inc').write_text('\n'.join(flags)+'\n')
s=s.replace('#include "native-80211.inc"','#include "native-80211.inc"\n#include "rxba-mbuf-flags.inc"')
(out/'rxba-test-kernel.h').write_text(s)
const=[]
for line in (src/'sys/net80211/ieee80211_var.h').read_text().splitlines():
 if re.match(r'^#define\s+IEEE80211_F_(?:PRIVACY|SCAN|DROPUNENC|NOBRIDGE)\s',line):
  const.append(line.split('/*',1)[0].rstrip())
(out/'rxba-stack-constants.inc').write_text('\n'.join(const)+'\n')
parts=[]
for name,result in [('ieee80211_input_management','static int'),('ieee80211_input_control','static void'),('ieee80211_input','int')]:
 parts.append(extract('sys/net80211/ieee80211_input.c',name,result))
(out/'native-rxba-common-input.inc').write_text('\n'.join(parts))
with (out/'rxba-mbuf-flags.inc').open('a') as f:
 f.write(re.search(r'^#define\s+M_HASFCS\s+.*$',(src/'sys/net/if_ether.h').read_text(),re.M).group()+'\n')
# Replace the CCMP-only key mocks with the actual generic key lifecycle.
# Cipher registration and device callbacks are explicit kernel/driver services.
crypto=(src/'sys/net80211/ieee80211_crypto.h').read_text()
constants=crypto[crypto.index('#define\tIEEE80211_KEY_COMMON'):crypto.index('#if defined(__KERNEL__)')]
state=re.search(r'^struct ieee80211_crypto_state \{.*?^};',crypto,re.M|re.S).group()
s=(out/'rxba-test-kernel.h').read_text()
s=s.replace('struct ieee80211com {',state+'\nstruct ieee80211com {\n unsigned ic_caps;\n struct ieee80211_crypto_state ic_crypto;',1)
s=s.replace('#include "native-key.inc"','#include "native-key.inc"\n#undef IEEE80211_KEYIX_NONE\n'+constants)
s=s.replace('static const struct ieee80211_cipher ieee80211_cipher_none = {0};','')
for name in ['ieee80211_crypto_newkey','ieee80211_crypto_setkey']:
 s,n=re.subn(r'^static int\n'+name+r'\(.*?^}\n','',s,count=1,flags=re.M|re.S)
 if n!=1:raise ValueError('key mock: '+name)
none=(src/'sys/net80211/ieee80211_crypto_none.c').read_text()
(out/'native-rxba-none.inc').write_text(re.sub(r'^#include[^\n]*\n','',none,flags=re.M))
services='''
#include "native-rxba-none.inc"
static const struct ieee80211_cipher *ciphers[IEEE80211_CIPHER_MAX] = {
 [IEEE80211_CIPHER_AES_CCM]=&ccmp, [IEEE80211_CIPHER_NONE]=&ieee80211_cipher_none
};
static const char *cipher_modnames[IEEE80211_CIPHER_MAX]={0};
static void ieee80211_load_module(const char *name) { (void)name; }
'''
for name,result,path in [('cipher_detach','static inline void','ieee80211_crypto.c'),('dev_key_alloc','static inline int','ieee80211_crypto.c'),('dev_key_delete','static inline int','ieee80211_crypto.c'),('dev_key_set','static inline int','ieee80211_crypto.c'),('ieee80211_crypto_resetkey','static inline void','ieee80211_crypto.h'),('ieee80211_crypto_newkey','int','ieee80211_crypto.c'),('_ieee80211_crypto_delkey','static int','ieee80211_crypto.c'),('ieee80211_crypto_delkey','int','ieee80211_crypto.c'),('ieee80211_crypto_setkey','int','ieee80211_crypto.c')]:
 services+=extract('sys/net80211/'+path,name,result)
s=s.replace('#include "native-setkey.inc"',services+'\n#include "native-setkey.inc"')
s=s.replace('#include "native-key.inc"','#define IEEE80211_C_TKIPMIC 0x00020000\n#include "native-key.inc"',1)
(out/'rxba-test-kernel.h').write_text(s)
# Real profile/offer/parser and reset guards, with a synthetic provider only.
s=(out/'rxba-test-kernel.h').read_text()
s=s.replace('struct ieee80211com {','struct ieee80211_channel { unsigned ieee; };\nstruct ieee80211_vht_channel;\nstruct ieee80211com {',1)
s=s.replace('unsigned ic_caps;','''unsigned ic_caps;
 int (*ic_ht20_admit)(struct ieee80211com *,struct ieee80211_ht20_profile *);
 int (*ic_vht_admit)(struct ieee80211com *,const struct ieee80211_vht_channel *,struct ieee80211_vht_cap *);''',1)
s=s.replace('int authorized;', 'int authorized;\n struct { unsigned rsn_ucastcipher; } ni_rsn;',1)
s=s.replace('struct { unsigned wme_hipri_traffic; } ic_wme;','struct { unsigned wme_hipri_traffic;void (*wme_sta_apply)(void); } ic_wme;')
(out/'rxba-test-kernel.h').write_text(s)
ht=pathlib.Path(os.environ.get('RXBA_HT20_SOURCE',src/'sys/net80211/ieee80211_ht20.c')).read_text()
(out/'native-rxba-ht20.inc').write_text(re.sub(r'^#include[^\n]*\n','',ht,flags=re.M))
s=(out/'native-rxba-output.inc').read_text()
for name in ['ieee80211_ht20_write','ieee80211_ht20_offer']:
 s,n=re.subn(r'^void\n'+name+r'\(.*?^}\n','',s,count=1,flags=re.M|re.S)
 if n!=1:raise ValueError('duplicate '+name)
(out/'native-rxba-output.inc').write_text(s)
with (out/'rxba-stack-constants.inc').open('a') as f:
 for filename,names in [('ieee80211_var.h',['IEEE80211_F_WME','IEEE80211_C_WME','IEEE80211_F_USEPROT']),('ieee80211_node.h',['IEEE80211_NODE_QOS','IEEE80211_NODE_WME_VALID','IEEE80211_NODE_WME_OFFER','IEEE80211_NODE_ASSOC_PENDING'])]:
  txt=(src/'sys/net80211'/filename).read_text()
  for name in names:f.write(re.search(r'^#define\s+'+name+r'\s+.*$',txt,re.M).group()+'\n')
s=(out/'rxba-test-kernel.h').read_text()
actual=re.search(r'^#define\s+IEEE80211_F_DATAPAD\s+.*$',(src/'sys/net80211/ieee80211_var.h').read_text(),re.M).group()
s,n=re.subn(r'^#define IEEE80211_F_DATAPAD .*$',actual,s,count=1,flags=re.M)
if n!=1:raise ValueError('DATAPAD service constant')
(out/'rxba-test-kernel.h').write_text(s)
# Complete actual node cleanup, including key-map ownership and real key delete.
s=(out/'rxba-test-kernel.h').read_text()
s=s.replace('struct ieee80211com {','struct ieee80211_node_table { struct ieee80211_node **nt_keyixmap;unsigned nt_keyixmax;int locked; };\nstruct ieee80211com {',1)
s=s.replace('int ic_opmode, ic_sta, ic_def_txkey, ic_flags;','int ic_opmode, ic_def_txkey, ic_flags;\n struct ieee80211_node_table ic_sta;\n unsigned ic_ps_sta;\n void (*ic_set_tim)(struct ieee80211_node *,int);')
s=s.replace('int authorized;', 'int authorized;\n unsigned ni_wme_applied_slot,ni_associd;\n struct ieee80211_channel *ni_wme_applied_chan;\n struct { uint8_t len,data[73]; } ni_phy_ies;',1)
(out/'rxba-test-kernel.h').write_text(s)
parts=[]
for name,result in [('ieee80211_node_delucastkey','int'),('node_cleanup','static void')]:
 parts.append(extract('sys/net80211/ieee80211_node.c',name,result))
(out/'native-rxba-node.inc').write_text('\n'.join(parts))
with (out/'rxba-stack-constants.inc').open('a') as f:
 txt=(src/'sys/net80211/ieee80211_node.h').read_text()
 for name in ['IEEE80211_NODE_PWR_MGT','IEEE80211_NODE_AREF']:
  f.write(re.search(r'^#define\s+'+name+r'\s+.*$',txt,re.M).group()+'\n')

s=(out/'rxba-test-kernel.h').read_text().replace('int if_timer,if_flags;', 'int if_timer,if_flags;\n void *if_softc;')
(out/'rxba-test-kernel.h').write_text(s)
net=pathlib.Path(os.environ.get('RXBA_NATIVE_SOURCE',src/'sys/dev/ic/rtw88_net.c')).read_text()
parts=[net[:net.index('#include')]]
# This test extracts the real stop path. It also owns HT20 publication, so
# execute the real provider with host atomic primitives instead of a stub.
parts.extend(['''
static uint32_t rxba_atomic_cas_32(volatile uint32_t *p,uint32_t o,uint32_t n)
{ uint32_t e=o; __atomic_compare_exchange_n(p,&e,n,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST); return e; }
static uint64_t rxba_atomic_cas_64(volatile uint64_t *p,uint64_t o,uint64_t n)
{ uint64_t e=o; __atomic_compare_exchange_n(p,&e,n,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST); return e; }
#define atomic_load_relaxed(p) __atomic_load_n((p),__ATOMIC_RELAXED)
#define atomic_load_acquire(p) __atomic_load_n((p),__ATOMIC_ACQUIRE)
#define atomic_cas_32(p,o,n) rxba_atomic_cas_32((p),(o),(n))
#define atomic_cas_64(p,o,n) rxba_atomic_cas_64((p),(o),(n))
'''])
names=[('rtw88_net_rxba_now','static uint64_t'),('rtw88_net_rxba_ticks','static int'),('rtw88_net_rxba_timeout','static void'),('rtw88_net_rxba_schedule','static int'),('rtw88_net_rxba_cancel','static void'),('rtw88_net_rxba_publish','static void'),('rtw88_net_rxba_softintr','static void'),('rtw88_net_rxba_quiesce','static void'),('rtw88_net_rxba_attach','static void'),('rtw88_net_rxba_detach','static void'),('rtw88_net_ht20_publish','static void')]
if all(re.search(r'^'+name+r'\(',net,re.M) for name,_ in names):
 for name,result in names:
  if name=='rtw88_net_rxba_attach':
   parts.append(re.search(r'^static const struct ieee80211_rxba_ops rtw88_net_rxba_ops = \{.*?^};',net,re.M|re.S).group()+'\n')
  parts.append(result+'\n'+re.search(r'^'+name+r'\(.*?^}',net,re.M|re.S).group()+'\n')
 # Exact owning lifecycle callsites; no success is modeled in their place.
 for name,owner in [('attach','rtw88_net_attach')]:
  body=re.search(r'^'+owner+r'\(.*?^}',net,re.M|re.S).group()
  line=re.findall(r'^\trtw88_net_rxba_'+name+r'\(net\);$',body,re.M)
  parts.append('static void unit_native_'+name+'(struct rtw88_net *net) {\n'+'\n'.join(line)+'\n}\n')
 for name,result in [('rtw88_net_stop','static void'),('rtw88_net_detach','void')]:
  parts.append(result+'\n'+re.search(r'^'+name+r'\(.*?^}',net,re.M|re.S).group()+'\n')
 parts.append('static void unit_native_detach(struct rtw88_net *net) { rtw88_net_detach(net); }\n')
else:
 # Causal baseline: the legacy native attach has no BA provider or lifetime owner.
 parts.append('static void unit_native_attach(struct rtw88_net *net) { (void)net; }\nstatic void unit_native_detach(struct rtw88_net *net) { (void)net; }\nstatic void rtw88_net_rxba_softintr(struct rtw88_net *net) { (void)net; }\n')
(out/'native-rxba-native.inc').write_text('\n'.join(parts))
var=(src/'sys/dev/ic/rtw88var.h').read_text();types=[]
types.append(re.search(r'^#define\s+CALLOUT_MPSAFE\s+.*$',(src/'sys/sys/callout.h').read_text(),re.M).group())
types.append(re.search(r'^#define\s+RTW88_NTXQ\s+.*$',var,re.M).group())
types.append(re.search(r'^struct rtw88_net_rx \{.*?^};',net,re.M|re.S).group())
types.append(re.search(r'^struct rtw88_net_stats \{.*?^};',(src/'sys/dev/ic/rtw88_net.h').read_text(),re.M|re.S).group())
for filename,names in [('ieee80211_var.h',['IEEE80211_BINTVAL_MIN','IEEE80211_BINTVAL_MAX']),('_ieee80211.h',['IEEE80211_CHAN_ANYC'])]:
 txt=(src/'sys/net80211'/filename).read_text()
 for name in names:types.append(re.search(r'^#define\s+'+name+r'\s+.*$',txt,re.M).group())
types.insert(0,'#undef IEEE80211_S_INIT\n#undef IEEE80211_S_RUN\n#undef IEEE80211_S_ASSOC\n#ifndef NATIVE_IEEE80211_STATE_DEFINED\n#define NATIVE_IEEE80211_STATE_DEFINED 1\n'+state_enum+'\n#endif')
(out/'native-rxba-native-types.inc').write_text('\n'.join(types)+'\n')
# The real native softintr observes AP presence before shared input. Keep its
# complete body/parser dependencies instead of replacing the observer service.
parts=[]
for name in ['ieee80211_phy_ies.h','ieee80211_beacon_ies.h']:
 parts.append(re.sub(r'^#include[^\n]*\n','',(src/'sys/net80211'/name).read_text(),flags=re.M))
for path,name,result in [('ieee80211_phy_ies.c','ieee80211_phy_ies_parse','int'),('ieee80211_beacon_ies.c','ieee80211_beacon_ies_parse','int'),('ieee80211_beacon_ies.c','ieee80211_sta_beacon_interval','uint16_t')]:
 parts.append(extract('sys/net80211/'+path,name,result))
(out/'native-rxba-beacon.inc').write_text('\n'.join(parts))
# These are the actual owning IRQ queue and Giant delivery consumers, not an
# unused timer API. The baseline remains explicit until those bodies are present.
with (out/'native-rxba-native.inc').open('a') as f:
 for name,result in [('rtw88_net_rx_frame','static void'),('rtw88_net_beacon_observe','static void'),('rtw88_net_softintr','static void')]:
  f.write(result+'\n'+re.search(r'^'+name+r'\(.*?^}',net,re.M|re.S).group()+'\n')

s=(out/'rxba-test-kernel.h').read_text().replace('unsigned if_ipackets,if_ierrors,if_oerrors,if_obytes;', 'unsigned if_ipackets,if_ierrors,if_oerrors,if_obytes,if_opackets;')
(out/'rxba-test-kernel.h').write_text(s)

s=(out/'rxba-test-kernel.h').read_text()
tag=re.search(r'^struct m_tag \{.*?^};',(src/'sys/sys/mbuf.h').read_text(),re.M|re.S).group()
s=s.replace('struct mbuf {','#include <sys/queue.h>\n'+tag+'\nstruct mbuf {',1)
s=s.replace('struct mbuf *m_next;', 'struct { size_t ext_size; } m_ext;\n struct mbuf *m_next;',1)
s=s.replace('int len;\n unsigned rcvif_index;', 'int len;\n SLIST_HEAD(packet_tags,m_tag) tags;\n unsigned rcvif_index;',1)
# Modeled pullup owns and releases every consumed segment, as the kernel does.
s=s.replace('\tm->m_next = NULL;\n\treturn m;', '\twhile(m->m_next) { struct mbuf *tail=m->m_next;m->m_next=tail->m_next;free(tail); }\n\treturn m;',1)
(out/'rxba-test-kernel.h').write_text(s)
# Use the target's complete SLIST macros. System headers are not a Cppcheck
# implementation model, and the BA metadata walk must be visible to analyzers.
queue=(src/'sys/sys/queue.h').read_text().splitlines(keepends=True);macros=[]
for name in ['SLIST_HEAD','SLIST_ENTRY','SLIST_FIRST','SLIST_END','SLIST_EMPTY','SLIST_NEXT','SLIST_FOREACH','SLIST_INSERT_HEAD','SLIST_REMOVE_HEAD']:
 for i,line in enumerate(queue):
  if re.match(r'^#define\s+'+name+r'\(',line):
   while line.rstrip().endswith('\\'):
    i+=1;line+=queue[i]
   macros.append('#undef '+name+'\n'+line);break
 else:raise ValueError(name)
s=(out/'rxba-test-kernel.h').read_text().replace('#include <sys/queue.h>','#include <sys/queue.h>\n'+''.join(macros),1)
(out/'rxba-test-kernel.h').write_text(s)

with (out/'native-rxba-native.inc').open('a') as f:
 core=(src/'sys/dev/ic/rtw88.c').read_text()
 f.write('void\n'+re.search(r'^rtw88_ra_invalidate\(.*?^}',core,re.M|re.S).group()+'\n')
 f.write('static int\n'+re.search(r'^rtw88_net_newstate\(.*?^}',net,re.M|re.S).group()+'\n')
s=(out/'rxba-test-kernel.h').read_text()
s=s.replace('static unsigned rxba_kmem_live;', 'static unsigned rxba_kmem_live,rxba_kmem_calls,rxba_kmem_fail_at;',1)
a='\tvoid *p = calloc(1, n);\n\tassert(p);\n\trxba_kmem_live++;'
b='\tif(++rxba_kmem_calls==rxba_kmem_fail_at)return NULL;\n'+a
if s.count(a)!=1:raise ValueError('kernel allocation fault model')
s=s.replace(a,b,1)
(out/'rxba-test-kernel.h').write_text(s)
