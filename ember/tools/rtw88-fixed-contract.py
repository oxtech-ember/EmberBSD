#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Keep the complete owning caller. The baseline adapter invokes its old raw API.
import pathlib,re,sys
src,out=map(pathlib.Path,sys.argv[1:])
# The owning caller now reads the actual default-OFF HT capsule layout.
(out/'net80211').mkdir(exist_ok=True)
(out/'net80211/ieee80211_ht20.h').write_bytes((src/'sys/net80211/ieee80211_ht20.h').read_bytes())
(out/'net80211/ieee80211_vht_ies.h').write_bytes((src/'sys/net80211/ieee80211_vht_ies.h').read_bytes())
s=(src/'sys/dev/ic/rtw88_net.c').read_text()
m=re.search(r'^rtw88_net_tx_submit\(.*?^}',s,re.M|re.S)
body=('static int\n'+m.group()) if m else '''
static int rtw88_net_tx_submit(struct rtw88_net *net,
    struct ieee80211_node *ni, unsigned q, const void *frame, size_t len)
{
    (void)ni;
    return rtw88_tx_raw(net->core, q, frame, len);
}'''
(out/'fixed-caller.inc').write_text(s[:s.index('#include')]+body+'\n')
# Constants come from the owning stack headers, not repeated numeric guesses.
parts=[re.search(r'enum ieee80211_state \{.*?\};',(src/'sys/net80211/ieee80211_proto.h').read_text(),re.S).group()]
for header,pattern in [('ieee80211.h',r'IEEE80211_RATE_(BASIC|VAL)'),
 ('ieee80211_var.h',r'IEEE80211_(FIXED_RATE_NONE|F_(USEPROT|DATAPAD))'),
 ('ieee80211_proto.h',r'IEEE80211_R_DO(SORT|FRATE|NEGO|DEL)')]:
 for line in (src/'sys/net80211'/header).read_text().splitlines():
  if re.match(r'^#define\s+'+pattern+r'\s',line):parts.append(line)
(out/'fixed-constants.inc').write_text('\n'.join(parts)+'\n')
# The real association rate negotiation checks the numeric local/peer binding.
s=(src/'sys/net80211/ieee80211_proto.c').read_text()
m=re.search(r'^ieee80211_fix_rate\(.*?^}',s,re.M|re.S)
(out/'fixed-negotiate.inc').write_text(s[:s.index('#include')]+'int\n'+m.group()+'\n')
