#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD complete native state-consumer extraction, AI-assisted.
"""Extract complete native state-work consumers and their source types."""
import pathlib
import os
import re
import sys
import subprocess
src, out = map(pathlib.Path, sys.argv[1:])
subprocess.run([sys.executable,str(src/"ember/tools/net80211-rxba-lifecycle-extract.py"),str(src),str(out),"--txba"],check=True)
phy_ies = (src/'sys/net80211/ieee80211_phy_ies.h').read_text()
(out/'ieee80211_phy_ies.h').write_text(phy_ies)
(out/'net80211').mkdir(exist_ok=True)
(out/'net80211/ieee80211_phy_ies.h').write_text(phy_ies)
net_source = pathlib.Path(os.environ.get('RTW88_STATE_SOURCE',
    src/'sys/dev/ic/rtw88_net.c')).read_text()
def read(path):
    return (src/path).read_text()
def function(name, result):
    match = re.search(r'^'+name+r'\(.*?^}', net_source, re.M|re.S)
    if not match:
        raise ValueError(name)
    return result+'\n'+match.group()+'\n'
proto = read('sys/net80211/ieee80211_proto.h')
parts = [read('sys/net80211/ieee80211_rxba.h'),read('sys/net80211/_ieee80211.h'), read('sys/net80211/ieee80211_ht20.h'),
         '#include <net80211/ieee80211_phy_ies.h>']
# The complete shared HT helper now reads the real management frame identity.
wire = read('sys/net80211/ieee80211.h')
parts.extend([wire[:wire.index('#ifndef')],
              '#ifndef __packed\n#define __packed __attribute__((__packed__))\n#endif',
              re.search(r'^#define\s+IEEE80211_ADDR_LEN\s+.*$', wire, re.M).group(),
              re.search(r'^struct ieee80211_frame \{.*?^} __packed;', wire, re.M|re.S).group()])
parts.append(re.search(r'^enum ieee80211_state \{.*?^};', proto, re.M|re.S).group())
parts.append('#define WME_NUM_AC 4\nstruct ieee80211com;\n')
for name in ('wmeParams', 'chanAccParams', 'ieee80211_wme_state'):
    parts.append(re.search(r'^struct '+name+r' \{.*?^};', proto, re.M|re.S).group())
for path, names in [('sys/net80211/ieee80211_var.h',r'IEEE80211_(?:C_[A-Z0-9_]+|F_[A-Z0-9_]+|FEXT_[A-Z0-9_]+|FIXED_RATE_NONE)'),
                    ('sys/net80211/ieee80211_node.h',r'IEEE80211_NODE_(QOS|WME_VALID|WME_OFFER|ASSOC_PENDING|AMSDU_RX)'),
                    ('sys/net80211/ieee80211.h',r'IEEE80211_RATE_(BASIC|VAL)'),
                    ('sys/net/if_media.h',r'_?IFM_\w+(?:\([^)]*\))?')]:
    source = re.sub(r'/\*.*?\*/', '', read(path).replace('\\\n', ''), flags=re.S)
    for line in source.splitlines():
        if re.match(r'#define\s+'+names+r'\s',line):
            parts.append(line.split('/*',1)[0])
s='\n'.join(parts)
for bits in (8,16,32,64):
    s=s.replace('u_int'+str(bits)+'_t','uint'+str(bits)+'_t')
s=re.sub(r'\bu_int\b','unsigned',s)
(out/'native-state-types.inc').write_text(s+'\n')
net = net_source
ht=read('sys/net80211/ieee80211_ht20.c')
vht = read('sys/net80211/ieee80211_vht_ies.c')
parts=[net[:net.index('#include')], ht[:ht.index('#include')],
       '#include <net80211/ieee80211_vht_ies.h>',
       vht[vht.index('static uint16_t'):],
       ht[ht.index('int\nieee80211_ht20_optional'):]]
shared=read('sys/net80211/ieee80211_proto.c')
parts.append(shared[:shared.index('#include')])
parts.append('void\n'+re.search(r'^ieee80211_wme_reset\(.*?^}',shared,re.M|re.S).group()+'\n')
for name, result in [('rtw88_net_basic_rates','static unsigned'),
                     ('rtw88_net_edca_prepare','static int'),
                     ('rtw88_net_ht20_local_policy','static int'),
                     ('rtw88_net_ht20_profile_prepare','static int'),
                     ('rtw88_net_tx_ht20_policy','static int'),
                     ('rtw88_net_tx_ra_validate','static int __attribute__((unused))'),
                     ('rtw88_net_ra_prepare','static int'),
                     ('rtw88_net_restart_peer','static int'),
                     ('rtw88_net_drain_wait','static int'),('rtw88_net_mailbox_wait','static int'),
                     ('rtw88_net_state_work','static void'),
                     ('rtw88_net_newstate','static int'),
                     ('rtw88_net_init','static int'),
                     ('rtw88_net_media_change','static int')]:
    if name in ('rtw88_net_drain_wait', 'rtw88_net_mailbox_wait') and '\n'+name+'(' not in net:
        continue
    if name == 'rtw88_net_edca_prepare' and '\n'+name+'(' not in net:
        continue
    # The negotiated-tuple predicate exists only in the VHT experiment.
    if name == 'rtw88_net_tx_ht20_policy' and '\nrtw88_net_vht_tuple(' in net:
        parts.append('#ifdef RTW88_VHT80\n' +
                     function('rtw88_net_vht_tuple', 'static int') + '#endif\n')
    parts.append(function(name,result))
start_body = re.search(r'^rtw88_net_start\(.*?^}', net, re.M|re.S)
if start_body is None or not re.search(
        r'rtw88_net_tx_ra_validate\s*\(\s*net,\s*&ht_policy,\s*ht_epoch\s*\)',
        start_body.group()):
    raise ValueError('RUN path must validate the negotiated HT/VHT RA profile')
parts.append('#ifdef RTW88_STATE_TX_CALLER\n' + function('rtw88_net_tx_submit', 'static int') + '#endif\n')
# The DFS64 slave producer and lease exist only in the experiment build;
# the quiet owner is shared by CSA and Quiet withdrawal.
if '\nrtw88_net_dfs_beacon(' in net:
    rx_type = re.search(r'^struct rtw88_net_rx \{.*?^};', net, re.M|re.S)
    rx_frame = re.search(r'^rtw88_net_rx_frame\(.*?^}', net, re.M|re.S)
    state_work = re.search(r'^rtw88_net_state_work\(.*?^}', net, re.M|re.S)
    dfs_beacon = re.search(r'^rtw88_net_dfs_beacon\(.*?^}', net, re.M|re.S)
    if (rx_type is None or not re.search(r'uint64_t[^;]*\bsequence\b',
            rx_type.group()) or rx_frame is None or
            not re.search(r'rx->sequence\s*=\s*info->sequence\s*;', rx_frame.group()) or
            not re.search(r'rx->generation\s*=\s*info->generation\s*;',
                          rx_frame.group())):
        raise ValueError('DFS RX must retain descriptor sequence and generation')
    if (state_work is None or
            not re.search(r'rtw88_rx_frontier\s*\(\s*net->core\s*,\s*'
                          r'net->core->sc_generation\s*,\s*&net->dfs_frontier\s*\)',
                          state_work.group()) or
            not re.search(r'net->dfs_frontier_valid\s*=\s*1\s*;',
                          state_work.group())):
        raise ValueError('state worker must snapshot a DFS RX descriptor frontier')
    if (dfs_beacon is None or
            not re.search(r'!net->dfs_frontier_valid', dfs_beacon.group()) or
            not re.search(r'net->dfs_frontier_generation\s*!=\s*generation',
                          dfs_beacon.group()) or
            not re.search(r'sequence\s*<=\s*net->dfs_frontier', dfs_beacon.group())):
        raise ValueError('DFS permit must reject descriptors at or before its frontier')
    # Softintr and watchdog run in other fixtures; prove the producer and
    # the lease are called from them before every net80211 input/poll.
    soft = re.search(r'^rtw88_net_softintr\(.*?^}', net, re.M|re.S).group()
    watch = re.search(r'^rtw88_net_watchdog\(.*?^}', net, re.M|re.S).group()
    call = re.search(r'rtw88_net_dfs_beacon\(net, mtod\(rx\.m, const uint8_t \*\),\s*'
                     r'\(size_t\)rx\.m->m_len, rx\.sequence, rx\.generation, '
                     r'rx\.tick, rx\.epoch,\s*'
                     r'\(uint32_t\)getticks\(\), \(uint32_t\)MAX\(hz,', soft)
    if (call is None or
            soft.find('rtw88_net_dfs_beacon(') > soft.find('ieee80211_input(ic, rx.m')):
        raise ValueError('softintr must check DFS beacons before input')
    if not re.search(r'if \(rtw88_net_dfs_expire\(net, \(uint32_t\)getticks\(\), timeout\)\) \{'
                     r'.*?ieee80211_new_state\(&net->ic, IEEE80211_S_INIT, -1\);', watch, re.S):
        raise ValueError('watchdog must request INIT after a DFS beacon loss')
    if watch.find('rtw88_net_dfs_expire(') > watch.find('rtw88_net_beacon_poll('):
        raise ValueError('watchdog must end the DFS lease before the RUN poll')
    var = read('sys/net80211/ieee80211_var.h')
    limits = '\n'.join(re.findall(r'^#define\s+IEEE80211_BINTVAL_M(?:IN|AX)\s.*$', var, re.M))
    proto = re.search(r'^uint16_t ieee80211_sta_beacon_interval\(.*?\);',
                      read('sys/net80211/ieee80211_beacon_ies.h'), re.M|re.S).group()
    parts.append('#ifdef RTW88_DFS64\n' + limits + '\n' + proto + '\n' +
                 function('rtw88_net_dfs_beacon', 'static void') +
                 function('rtw88_net_dfs_expire', 'static int') + '#endif\n')
parts.append(function('rtw88_net_sta_quiet', 'static void __attribute__((unused))'))
# The VHT80 provider exists only in the experiment build.
if '\nrtw88_net_vht_admit(' in net:
    parts.append('#ifdef RTW88_VHT80\n' + function('rtw88_net_vht_admit', 'static int') + '#endif\n')
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

# Exact native management-queue ownership consumer; allocator/node services modelled.
bsd = read('sys/net80211/ieee80211_netbsd.c')
(out/'native-drain.inc').write_text(bsd[:bsd.index('#include')] + 'void\n' + re.search(
    r'^ieee80211_drain_ifq\(.*?^}', bsd, re.M|re.S).group() + '\n')

scan = read('sys/net80211/ieee80211_ioctl.c')
up = scan[scan.index('#ifdef __NetBSD__\n#define\tIS_UP'):]
up = up[:up.index('#endif')]
setup = re.search(r'^ieee80211_setupscan\(.*?^}',scan,re.M|re.S).group()
arm = re.search(r'^\tcase IEEE80211_IOC_SCAN_REQ:.*?(?=^\tcase )',scan,re.M|re.S).group()
request = re.search(r'^#define\s+IEEE80211_IOC_SCAN_REQ\s+.*$',read('sys/net80211/ieee80211_ioctl.h'),re.M).group()
(out/'native-reconnect-scan.inc').write_text(scan[:scan.index('#include')] + up.replace('#ifdef __NetBSD__\n','') +
    '\n'+request+'\nstatic int\n'+setup+'\n' +
    'static int reconnect_scan_request(struct ieee80211com *ic) { int error=0;\n' +
    'switch (IEEE80211_IOC_SCAN_REQ) {\n'+arm+'}\nreturn error; }\n')

# Cross-band bridge: actual shared media/setmode and INIT/SCAN owning arms.
# Node allocation, notifications, timers and unrelated states remain services.
ieee=read('sys/net80211/ieee80211.c')
node=read('sys/net80211/ieee80211_node.c')
def shared_body(source,name,result):
    return result+'\n'+re.search(r'^'+name+r'\(.*?^}',source,re.M|re.S).group()+'\n'
band=[ieee[:ieee.index('#include')],node[:node.index('#include')]]
for name in ('11a','11b','11g'):
    band.append(re.search(r'^const struct ieee80211_rateset ieee80211_std_rateset_'+name+r'\s*=.*?;',ieee,re.M|re.S).group())
for name,result in [('ieee80211_chan2ieee','unsigned __attribute__((noinline))'),('ieee80211_ieee2mhz','unsigned'),('ieee80211_chan2mode','enum ieee80211_phymode'),('findrate','static int')]:
    band.append(shared_body(ieee,name,result))
# Only declaration names are adapted to coexist with legacy model services.
for name,result in [('ieee80211_set11gbasicrates','void'),('ieee80211_set_shortslottime','void'),('ieee80211_reset_erp','void')]:
    band.append(shared_body(shared,name,result))
for name in ('ieee80211_setmode','ieee80211_media_change'):
    band.append(shared_body(ieee,name,'static int').replace('\n'+name+'(','\nstate_shared_'+name+'(',1))
band.append('#pragma GCC diagnostic push\n#pragma GCC diagnostic ignored "-Wsign-compare"\n'+shared_body(ieee,'ieee80211_media2rate','int')+'#pragma GCC diagnostic pop\n')
for name,result in [('ieee80211_reset_scan','static void'),('ieee80211_begin_scan','void'),('ieee80211_next_scan','int')]:
    band.append(shared_body(node,name,result))
(out/'native-band-shared.inc').write_text('\n'.join(band))
full=re.search(r'^ieee80211_newstate\(.*?^}',shared,re.M|re.S).group()
prefix=full[full.index('\tostate = ic->ic_state;'):full.index('\tswitch (nstate) {')]
arms=full[full.index('\tcase IEEE80211_S_INIT:'):full.index('\tcase IEEE80211_S_AUTH:\n\t\tswitch (ostate)')]
(out/'native-band-parent.inc').write_text(prefix+'\tswitch (nstate) {\n'+arms+'\tdefault: abort();\n\t}\nreturn 0;\n')
start=net.index('\tic->ic_sup_rates[IEEE80211_MODE_11A] =')
end=net.index('\tifp->if_softc = net;',start)
array=re.search(r'static const unsigned channels5\[\].*?;',net,re.S).group()
(out/'native-band-driver.inc').write_text(array+'\nunsigned i,ch;\n'+net[start:end])
