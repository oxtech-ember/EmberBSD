#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD AP-loss source extractor, AI-assisted.
# Execute actual AP-loss consumers. Only infrastructure is modeled by the C file.
import pathlib
import re
import sys
import subprocess

src, out = map(pathlib.Path, sys.argv[1:3])
subprocess.run([sys.executable,str(src/'ember/tools/net80211-rxba-lifecycle-extract.py'),str(src),str(out)],check=True)
mutant = sys.argv[3] if len(sys.argv) > 3 else 'none'
assert mutant in ('none', 'supervision', 'withdrawal', 'ra', 'notification', 'dfs')

def read(path):
    return (src/path).read_text()

def notice(path):
    s = read(path)
    return '\n'.join(re.findall(r'/\*.*?\*/', s[:re.search(r'^#(?:include|ifndef)',s,re.M).start()], re.S))+'\n'

def body(path, name, result):
    s = read(path)
    m = re.search(r'^'+name+r'\(.*?^}', s, re.M|re.S)
    assert m, name
    return notice(path)+result+'\n'+m.group()+'\n'

def exact_block(s, start, end):
    assert s.count(start) == 1, start
    a = s.index(start)
    b = s.index(end, a)+len(end)
    return s[a:b]

net = 'sys/dev/ic/rtw88_net.c'
proto = 'sys/net80211/ieee80211_proto.c'
node = 'sys/net80211/ieee80211_node.c'
nbsd = 'sys/net80211/ieee80211_netbsd.c'
wpa = 'external/bsd/wpa/dist/src/drivers/driver_bsd.c'
constants = []
for path, name in [('sys/net80211/ieee80211_proto.h', 'ieee80211_state'),
                   ('sys/net80211/_ieee80211.h', 'ieee80211_opmode'),
                   ('sys/net80211/_ieee80211.h', 'ieee80211_roamingmode')]:
    constants.append(notice(path))
    constants.append(re.search(r'^enum '+name+r' \{.*?^};', read(path), re.M|re.S).group()+'\n')
for path, names in [('sys/net80211/ieee80211_var.h', ['IEEE80211_BMISS_MAX', 'IEEE80211_F_SCAN', 'IEEE80211_F_SIBSS']),
                    (nbsd.replace('.c','.h'), ['RTM_IEEE80211_DISASSOC', 'RTM_IEEE80211_LEAVE'])]:
    constants.append(notice(path))
    for name in names:
        constants.append(re.search(r'^#define\s+'+name+r'\s+.*$', read(path), re.M).group()+'\n')
constants.append(re.search(r'^#define\s+IEEE80211_NODE_AMSDU_RX\s+.*$',read('sys/net80211/ieee80211_node.h'),re.M).group()+'\n')
(out/'aploss-constants.inc').write_text(''.join(constants))
(out/'ieee80211_ht20.h').write_text(read('sys/net80211/ieee80211_ht20.h'))
watchdog = body(net, 'rtw88_net_watchdog', 'static void')
close = '\t\tif (missed)\n\t\t\trtw88_dfs_close(net->core);'
beacon_miss = '\t\tif (missed)\n\t\t\tieee80211_beacon_miss(&net->ic);'
poll = '\t\tmissed = rtw88_net_beacon_poll('
enter = '\t\tmutex_enter(net->lock);'
leave = '\t\tmutex_exit(net->lock);'
assert all(watchdog.count(s) == 1 for s in (enter, poll, close, leave, beacon_miss)) and \
    watchdog.index(enter) < watchdog.index(poll) < watchdog.index(close) < \
    watchdog.index(leave) < watchdog.index(beacon_miss), \
    'DFS permit must close before beacon-miss handling'
parts = [body('sys/net80211/ieee80211_ht20.c', 'ieee80211_ht20_reset', 'void'),
         body('sys/net80211/ieee80211_ht20.c', 'ieee80211_ht20_state_request', 'int'),
         '#define ieee80211_ht20_state_request aploss_checked_ht20_request\n',
         body('sys/dev/ic/rtw88.c', 'rtw88_ra_invalidate', 'static void'),
         body(net, 'rtw88_net_newstate', 'static int'),
         body('sys/net80211/ieee80211.c', 'ieee80211_watchdog', 'static void'),
         body(proto, 'ieee80211_beacon_miss', 'static void'),
         watchdog,
         body('sys/net/if.c', 'if_slowtimo_countdown', 'static bool'),
         body(nbsd, 'ieee80211_notify_node_leave', 'static void'),
         body(node, 'ieee80211_sta_leave', 'static void')]
# These two exact owning branches have a deliberately narrow service boundary.
# Full RF restart/scan selection is covered elsewhere; this is AP-loss ownership.
run = exact_block(read(net), '\t\t\t\tif (state == IEEE80211_S_RUN) {\n', '\t\t\t\t}\n')
parts += [notice(net), 'static void native_run_supervision(struct rtw88_net *net) {\nconst enum ieee80211_state state=IEEE80211_S_RUN;\nuint64_t epoch=net->policy.epoch;\nstruct ifnet *ifp=net->ic.ic_ifp;\n', run, '}\n']
scan = exact_block(read(proto), '\t\tcase IEEE80211_S_RUN:\n\t\t\t/* beacon miss */', '\t\t\tbreak;\n')
parts += [notice(proto), 'static void shared_run_to_scan(struct ieee80211com *ic, int arg) {\nstruct ieee80211_node *ni=ic->ic_bss;\nswitch (IEEE80211_S_RUN) {\n', scan, '\ndefault: break;\n}\n}\n']
text=''.join(parts)
if mutant == 'dfs':
    assert text.count(close) == 1
    text=text.replace(close, '\t\t/* Causal mutant: permit retained after beacon loss. */')
if mutant == 'supervision':
    # An idle station must not lose its watchdog after the initial invocation.
    old='\t\t/* Keep checking an idle station even when it has no outstanding TX. */\n\t\tifp->if_timer = 1;'
    assert text.count(old)==1
    text=text.replace(old, '\t\t/* Causal mutant: idle supervision removed. */')
if mutant == 'withdrawal':
    old='\t(void)rtw88_net_request(&net->policy);'
    assert text.count(old)==1
    text=text.replace(old, '\t/* Causal mutant: old association remains accepted. */')
if mutant == 'ra':
    old='\tsc->sc_ra.active = 0;\n\tsc->sc_ra.configured = 0;'
    assert text.count(old)==1
    text=text.replace(old, '\t(void)sc; /* Causal mutant: stale RA remains active. */')
if mutant == 'notification':
    old='\t\trt_ieee80211msg(ifp, RTM_IEEE80211_DISASSOC, NULL, 0);'
    assert text.count(old)==1
    text=text.replace(old, '\t\t/* Causal mutant: disconnect event removed. */')
(out/'aploss-native.inc').write_text(text)
parts=[body(wpa, 'bsd_sta_disconnected', 'static void')]
# Keep the entire real dispatch case, including its target-specific guard.
s=read(wpa);a=s.index('\t\tcase RTM_IEEE80211_DISASSOC:');b=s.index('\t\tcase RTM_IEEE80211_SCAN:',a)
parts += [notice(wpa), '''static void bsd_disconnect_route(struct bsd_driver_data *drv, int what) {
/* Headers are already included; select the actual NetBSD driver branch only. */
#ifndef __NetBSD__
#define APLOSS_DEFINED_NETBSD 1
#define __NetBSD__ 1
#endif
#define IEEE80211_IOC_STA_MGMT 1
switch (what) {
''',s[a:b],'''default: break;
}
#ifdef APLOSS_DEFINED_NETBSD
#undef __NetBSD__
#undef APLOSS_DEFINED_NETBSD
#endif
}
''']
(out/'aploss-userland.inc').write_text(''.join(parts))
