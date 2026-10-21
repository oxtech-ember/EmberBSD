#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Keep complete native consumers and their source notices in the host contract.
from pathlib import Path
import sys
src, out = map(Path, sys.argv[1:])

def read(path):
    return (src / path).read_text()

def function(text, name, result):
    start = text.index('\n' + name + '(') + 1
    end = text.index('\n}', start) + 2
    return result + '\n' + text[start:end] + '\n'

node = read('sys/net80211/ieee80211_node.c')
ipt = read('sys/net80211/ieee80211_input.c')
text = node[:node.index('#include')] + ipt[:ipt.index('#include')]
text += function(ipt, 'ieee80211_saveie', 'void')
text += function(node, 'saveie', 'static void')
text += function(node, 'node_alloc', 'static struct ieee80211_node *')
text += function(node, 'node_cleanup', 'static void')
text += function(node, 'ieee80211_add_scan', 'void')
(out / 'native-node.inc').write_text(text)

# Use the actual input request and wire layouts, not shortened stand-ins.
headers = ''
for path, names in [('sys/net80211/ieee80211.h', ['ieee80211_frame', 'ieee80211_tim_ie']), ('sys/net80211/ieee80211_node.h', ['ieee80211_scanparams'])]:
    source = read(path)
    headers += source[:source.index('\n#')]
    for name in names:
        begin = source.index('struct ' + name + ' {')
        end = source.index(';', source.index('}', begin)) + 1
        headers += source[begin:end] + '\n'
(out / 'native-layouts.h').write_text(headers)
ioctl = read('sys/net80211/ieee80211_ioctl.c')
path = out / 'get_scan_result.h'
path.write_text(ioctl[:ioctl.index('#include')] + path.read_text())
