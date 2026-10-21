#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD complete native state-consumer extraction, AI-assisted.
"""Extract complete native state-work consumers and their source types."""
import pathlib
import re
import sys
src, out = map(pathlib.Path, sys.argv[1:])
def read(path):
    return (src/path).read_text()
def function(name, result):
    match = re.search(r'^'+name+r'\(.*?^}', read('sys/dev/ic/rtw88_net.c'), re.M|re.S)
    if not match:
        raise ValueError(name)
    return result+'\n'+match.group()+'\n'
proto = read('sys/net80211/ieee80211_proto.h')
parts = [read('sys/net80211/_ieee80211.h')]
parts.append(re.search(r'^enum ieee80211_state \{.*?^};', proto, re.M|re.S).group())
parts.append('#define WME_NUM_AC 4\nstruct ieee80211com;\n')
for name in ('wmeParams', 'chanAccParams', 'ieee80211_wme_state'):
    parts.append(re.search(r'^struct '+name+r' \{.*?^};', proto, re.M|re.S).group())
for path, names in [('sys/net80211/ieee80211_var.h',r'IEEE80211_(C_WME|F_WME|F_SHSLOT|FIXED_RATE_NONE)'),
                    ('sys/net80211/ieee80211_node.h',r'IEEE80211_NODE_(QOS|WME_VALID|WME_OFFER)'),
                    ('sys/net80211/ieee80211.h',r'IEEE80211_RATE_(BASIC|VAL)'),
                    ('sys/net/if_media.h',r'IFM_(NONE|L_OWNLOCK|NMASK|MMASK|TYPE\(x\)|MODE\(x\)|AUTO|IEEE80211(?:_11[A-Z]+|_FH|_TURBO)?)')]:
    for line in read(path).splitlines():
        if re.match(r'#define\s+'+names+r'\s',line):
            parts.append(line.split('/*',1)[0])
s='\n'.join(parts)
for bits in (8,16,32,64):
    s=s.replace('u_int'+str(bits)+'_t','uint'+str(bits)+'_t')
s=re.sub(r'\bu_int\b','unsigned',s)
(out/'native-state-types.inc').write_text(s+'\n')
net = read('sys/dev/ic/rtw88_net.c')
parts=[net[:net.index('#include')]]
shared=read('sys/net80211/ieee80211_proto.c')
parts.append(shared[:shared.index('#include')])
parts.append('void\n'+re.search(r'^ieee80211_wme_reset\(.*?^}',shared,re.M|re.S).group()+'\n')
for name, result in [('rtw88_net_basic_rates','static unsigned'),
                     ('rtw88_net_edca_prepare','static int'),
                     ('rtw88_net_state_work','static void'),
                     ('rtw88_net_newstate','static int'),
                     ('rtw88_net_init','static int'),
                     ('rtw88_net_media_change','static int')]:
    if name == 'rtw88_net_edca_prepare' and '\n'+name+'(' not in net:
        continue
    parts.append(function(name,result))
(out/'native-state.inc').write_text('\n'.join(parts)+'\n')

media = read('sys/net/if_media.c')
parts = [media[:media.index('#include')],
         re.search(r'^#define\s+IFM_L_OWNLOCK\s+.*$',media,re.M).group()]
for name in ('ifmedia_init_with_lock', 'ifmedia_set'):
    parts.append('void\n'+re.search(r'^'+name+r'\(.*?^}', media, re.M|re.S).group())
(out/'native-media.inc').write_text('\n'.join(parts)+'\n')
# Execute the exact attach initialization consumer, not a test-only cache seed.
start = net.index('\tieee80211_media_init(ic,')
end = net.index('\tif_register(ifp);', start) + len('\tif_register(ifp);')
(out/'native-media-attach.inc').write_text(net[start:end]+'\n')
