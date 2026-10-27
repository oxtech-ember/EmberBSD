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
pci = (src/'sys/dev/pci/rtw88_pci_hci.c').read_text()
parts.extend(re.findall(r'^#define RTW88_[^\n]*(?:\\\n[^\n]*)*', pci, re.M))
parts.append(re.search(r'static const struct \{.*?\} rtw88_txq_hw\[.*?\n\};', pci, re.S).group())
parts.append(re.search(r'static const unsigned rtw88_queue_ring\[.*?\n\};', s, re.S).group())
# Retain the actual transport producer, not a model of HCI submission.
for name, result in [('rtw88_pci_put16', 'static void'),
                     ('rtw88_pci_put32', 'static void'),
                     ('rtw88_pci_space', 'static unsigned'),
                     ('rtw88_pci_xmit', 'static int')]:
    match = re.search(r'^'+name+r'\([^;]*?\n\{', pci, re.M)
    end = pci.index('\n}', match.end()) + 2
    parts.extend([result, pci[match.start():end]])
parts.append('static const struct rtw88_hci_ops wme_hci = {'
             '.space = rtw88_pci_space, .xmit = rtw88_pci_xmit};')
# This fixture builds without RTW88_VHT_EXPERIMENT; the copied #ifdef
# RTW88_VHT80 branches in rtw88_tx_submit stay inactive here.
for name, result in [('rtw88_channel_dfs','static int'),
                     ('rtw88_dfs_tx_permitted','static int'),
                     ('rtw88_tx_available','unsigned'),
                     ('rtw88_channel_narrow','static int'),('rtw88_tx_submit','static int'),
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
