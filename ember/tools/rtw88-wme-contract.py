#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Extract complete native core consumers, retaining their source notice.
import pathlib
import re
import sys
src, out = map(pathlib.Path, sys.argv[1:])
s = (src/'sys/dev/ic/rtw88.c').read_text()
notice = s[:s.index('#include')]
parts = [notice]
parts.extend(re.findall(r'^#define RTW88_[^\n]*(?:\\\n[^\n]*)*', s, re.M))
parts.append(re.search(r'static const struct \{.*?\} rtw88_txq_hw\[.*?\n\};', s, re.S).group())
for name, result in [('rtw88_put16','static void'),('rtw88_put32','static void'),
                     ('rtw88_tx_available','unsigned'),('rtw88_tx_submit','static int'),
                     ('rtw88_tx_raw','int'),('rtw88_tx_fixed','int'),
                     ('rtw88_edca_apply','int')]:
    match = re.search(r'^'+name+r'\([^;]*?\n\{', s, re.M)
    if match is None:
        raise ValueError(name)
    end = s.index('\n}', match.end()) + 2
    parts.extend([result, s[match.start():end]])
(out/'native-wme-core.inc').write_text('\n'.join(parts)+'\n')

proto = (src/'sys/net80211/ieee80211_proto.h').read_text()
net = (src/'sys/dev/ic/rtw88_net.c').read_text()
parts = []
for name in ('wmeParams', 'chanAccParams'):
    body = re.search(r'^struct '+name+r' \{.*?^\};', proto, re.M|re.S).group()
    parts.append(body.replace('u_int8_t', 'uint8_t').replace('u_int16_t', 'uint16_t'))
parts.append('typedef int (*native_sta_hook_t)(struct ieee80211com *, const struct chanAccParams *, unsigned);')
match = re.search(r'^rtw88_net_wme_apply\(.*?^}', net, re.M|re.S)
if match:
    parts.extend([net[:net.index('#include')], 'static int', match.group(),
        'static native_sta_hook_t native_sta_hook = rtw88_net_wme_apply;'])
else:
    parts.append('static native_sta_hook_t native_sta_hook;')
(out/'native-wme-net.inc').write_text('\n'.join(parts)+'\n')
