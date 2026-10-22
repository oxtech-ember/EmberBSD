#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Actual BA storage destruction for legacy/HT negotiation fixtures.

Active BA Action/ordered data is owned by net80211-rxba-contract. This subset
keeps real teardown in older consumers without pretending those run BA crypto.
"""
import pathlib,re,sys
src,out=map(pathlib.Path,sys.argv[1:3]);response='--response' in sys.argv[3:]
text=(src/'sys/net80211/ieee80211_rxba.c').read_text()
(out/'ieee80211_rxba.h').write_text((src/'sys/net80211/ieee80211_rxba.h').read_text())
parts=[text[:text.index('#include')]]
for name in ['rxba_slot','ieee80211_rxba_tid']+(['ieee80211_rxba_candidate'] if response else []):
 parts.append(re.search(r'^struct '+name+r' \{.*?^};',text,re.M|re.S).group())
parts.append('''#ifndef KASSERT
#define KASSERT(c) assert(c)
#endif
static inline void rxba_contract_free(void *p,size_t size) { (void)size;free(p); }
#define kmem_intr_free rxba_contract_free
''')
functions=[('ieee80211_rxba_ready','int'),('rxba_drop','static void'),('rxba_free_tid','static void'),('ieee80211_rxba_reset','void')]
if response:
 node=(src/'sys/net80211/ieee80211_node.h').read_text()
 parts.append('static inline int\n'+re.search(r'^ieee80211_node_is_authorized\(.*?^}',node,re.M|re.S).group()+'\n')
 functions += [('rxba_withdraw','static void'),('rxba_le16','static uint16_t'),('rxba_current','static int'),('rxba_require_current','static int'),('ieee80211_rxba_mask','unsigned'),('ieee80211_rxba_response_check','int'),('ieee80211_rxba_response_commit','void')]
for name,result in functions:parts.append(result+'\n'+re.search(r'^'+name+r'\(.*?^}',text,re.M|re.S).group()+'\n')
parts.append('#undef kmem_intr_free\n')
(out/'native-rxba-lifecycle.inc').write_text('\n'.join(parts))
