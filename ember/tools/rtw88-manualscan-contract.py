#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Actual disruptive scan -> native worker -> disconnect -> same-BSSID consumer.

The copied state model supplies only kernel layouts/services. Function bodies,
SCAN_REQ and the owning shared SCAN branch are extracted from their sources.
"""
import os
from pathlib import Path
import re
import sys

src, out = map(Path, sys.argv[1:])
def read(p):
    if p == 'sys/net80211/ieee80211_ioctl.c' and os.environ.get('MANUALSCAN_IOCTL_SOURCE'):
        return Path(os.environ['MANUALSCAN_IOCTL_SOURCE']).read_text()
    return (src/p).read_text()
def notice(p):
    s = read(p)
    return s[:s.index('#include')]
def function(p, name, result):
    s = read(p)
    m = re.search(r'^(?:int |void )?' + re.escape(name) + r'\(.*?^}', s, re.M|re.S)
    if m is None:
        raise ValueError(name)
    body = re.sub(r'^(?:int |void )', '', m.group())
    return notice(p)+result+'\n'+body+'\n'
def replace(s, a, b):
    if s.count(a) != 1:
        raise ValueError(a)
    return s.replace(a, b)

# Extend the existing service model locally, without changing its other users.
s = read('ember/tools/rtw88-state-test.h')
# The shared state model already owns MAC addresses, failure counters,
# roaming and desired channels. Add only this consumer's key-cleanup hook.
s = replace(s, 'struct ieee80211_node {', """struct ieee80211_node_table {
    unsigned nt_scangen, nt_scan_epoch;
};
struct ieee80211_node {
    int key_live;""")
s = replace(s, 'struct ieee80211com {', """struct ieee80211com {
    void (*ic_node_cleanup)(struct ieee80211_node *);""")
s = replace(s, '\tstruct { unsigned nt_scangen,nt_scan_epoch; } ic_scan,ic_sta;',
            '\tstruct ieee80211_node_table ic_scan, ic_sta;')
# This consumer defines the one ANY sentinel after its included base suite.
s = replace(s, 'const struct ieee80211_channel ieee80211_channel_anyc={0,0};',
            'extern const struct ieee80211_channel ieee80211_channel_anyc;')
# Isolate only the band's independent service namespace. Its complete actual
# shared scan bodies stay unchanged; the separate base suite owns their tests.
# This consumer keeps its leave/notification services and scan-start counter.
band_names = ('ieee80211_begin_scan', 'ieee80211_probe_curchan',
              'ieee80211_create_ibss', 'ieee80211_find_node',
              'ieee80211_unref_node', 'ieee80211_cancel_scan')
band_start = '#define IEEE80211_DPRINTF(...) ((void)0)'
band_end = '#undef ieee80211_sta_leave\n'
s = replace(s, band_start,
            ''.join('#define '+n+' manualscan_band_'+n+'\n' for n in band_names)
            + band_start)
s = replace(s, band_end, band_end
            + ''.join('#undef '+n+'\n' for n in band_names))
(out/'manualscan-state-test.h').write_text(s)
s = replace(read('ember/tools/rtw88-start-contract.c'),
            '#include "rtw88-state-test.h"', '#include "manualscan-state-test.h"')
(out/'manualscan-start.inc').write_text(s)
s = replace(read('ember/tools/rtw88-ra-contract.c'),
            '#include "rtw88-start-contract.c"', '#include "manualscan-start.inc"')
# The existing RA consumer renames its included start main. Rename its own too.
(out/'manualscan-ra.inc').write_text(s)

ioctl = 'sys/net80211/ieee80211_ioctl.c'
node = 'sys/net80211/ieee80211_node.c'
nbsd = 'sys/net80211/ieee80211_netbsd.c'
proto = 'sys/net80211/ieee80211_proto.c'
parts = []
parts.append(re.search(r'^#define\s+IEEE80211_IOC_SCAN_REQ\s+.*$',
                      read('sys/net80211/ieee80211_ioctl.h'),re.M).group()+'\n')
for p, names in [(nbsd.replace('.c','.h'), ['RTM_IEEE80211_DISASSOC', 'RTM_IEEE80211_LEAVE', 'RTM_IEEE80211_SCAN']),
                 ('sys/net80211/ieee80211_var.h', ['IEEE80211_F_SCAN', 'IEEE80211_F_ASCAN', 'IEEE80211_F_SIBSS'])]:
    for name in names:
        parts.append(re.search(r'^#define\s+'+name+r'\s+.*$',read(p),re.M).group()+'\n')
parts.append(function(nbsd, 'ieee80211_notify_node_leave', 'static void'))
parts.append(function(nbsd, 'ieee80211_notify_scan_done', 'static void'))
parts.append(function(node, 'ieee80211_sta_leave', 'static void'))
parts.append(function(ioctl, 'ieee80211_setupscan', 'static int'))
s = read(ioctl)
arm = re.search(r'^\tcase IEEE80211_IOC_SCAN_REQ:.*?(?=^\tcase )',s,re.M|re.S).group()
parts.append('static int manualscan_request(struct ieee80211com *ic) { int error=0;\n'
             'switch (IEEE80211_IOC_SCAN_REQ) {\n'+arm+'}\nreturn error; }\n')
# The exact owning shared SCAN branch, including INIT/repeated-SCAN/RUN cases.
s = read(proto)
a = s.index('\tcase IEEE80211_S_SCAN:\n\t\tswitch (ostate)')
b = s.index('\n\tcase IEEE80211_S_AUTH:',a)
parts.append(notice(proto)+'static void manualscan_parent_scan(struct ieee80211com *ic, enum ieee80211_state ostate, int arg) {\n'
             'struct ieee80211_node *ni=ic->ic_bss;\nswitch (IEEE80211_S_SCAN) {\n'+s[a:b]+'\ndefault: break;\n}\n}\n')
# Manual end-scan prefix is exact; automatic candidate selection is not modeled.
s = read(node)
a = s.index('\tieee80211_cancel_scan(ic);', s.index('ieee80211_end_scan('))
b = s.index('\n\t/*\n\t * Automatic sequencing;',a)
parts.append(notice(node)+'static void manualscan_end_scan(struct ieee80211com *ic) {\n'+s[a:b]+
             '\nCHECK(0); /* This fixture selects manual sequencing only. */\n}\n')
(out/'manualscan-shared.inc').write_text(''.join(parts))

wpa = 'external/bsd/wpa/dist/src/drivers/driver_bsd.c'
events = 'external/bsd/wpa/dist/wpa_supplicant/events.c'
parts = [function(events, 'wpa_supplicant_mark_disassoc', 'static void'),
         function(events, 'wpa_supplicant_connect', 'static int'),
         function(wpa, 'bsd_sta_disconnected', 'static void')]
s = read(wpa)
a = s.index('\t\tcase RTM_IEEE80211_DISASSOC:')
b = s.index('\t\tcase RTM_IEEE80211_LEAVE:', a)
parts.append(notice(wpa)+'''static void manualscan_bsd_route(struct bsd_driver_data *drv, int what) {
#ifndef __NetBSD__
#define MANUALSCAN_NETBSD 1
#define __NetBSD__ 1
#endif
#define IEEE80211_IOC_STA_MGMT 1
switch (what) {
'''+s[a:b]+'''default: CHECK(0);break;
}
#ifdef MANUALSCAN_NETBSD
#undef __NetBSD__
#undef MANUALSCAN_NETBSD
#endif
}
''')
(out/'manualscan-userland.inc').write_text(''.join(parts))
