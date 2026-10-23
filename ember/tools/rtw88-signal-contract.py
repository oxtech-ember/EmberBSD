#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
import pathlib,re,subprocess,sys
src,out=map(pathlib.Path,sys.argv[1:])
def read(p):return (src/p).read_text()
def body(p,n,result):
 s=read(p);prefix=s[:s.index('#include')]
 return prefix+result+'\n'+re.search(r'^'+n+r'\(.*?^}',s,re.M|re.S).group()+'\n'
parts=[]
for p,names in [('sys/net80211/ieee80211.h',['IEEE80211_ADDR_LEN','IEEE80211_NWID_LEN']),('sys/net80211/_ieee80211.h',['IEEE80211_RATE_MAXSIZE']),('sys/net80211/ieee80211_node.h',['IEEE80211_NODE_QOS','IEEE80211_NODE_WME_OFFER','IEEE80211_NODE_WME_VALID','IEEE80211_NODE_ASSOC_PENDING']),('sys/net80211/ieee80211_ioctl.h',['IEEE80211_SCAN_CAPINFO_HI','IEEE80211_SCAN_RSSI_DBM'])]:
 for n in names:parts.append(re.search(r'^#define\s+'+n+r'\s+.*$',read(p),re.M).group()+'\n')
s=read('sys/net80211/ieee80211_ioctl.h');parts.append(re.search(r'^struct ieee80211req_scan_result \{.*?^};',s,re.M|re.S).group()+'\n')
(out/'scan-abi.h').write_text(''.join(parts))
(out/'get_scan_result.h').write_text(body('sys/net80211/ieee80211_ioctl.c','get_scan_result','static void'))
s=body('sys/net80211/ieee80211_ioctl.c','ieee80211_ioctl_getscanresults','static int').replace('\tp = ireq->i_data;','\tmemset(&u,0xa5,sizeof(u));\n\tp = ireq->i_data;')
(out/'getscanresults.h').write_text(s)
subprocess.run([sys.executable,str(src/'ember/tools/net80211-phy-ies-extract.py'),str(src),str(out)],check=True)
s=read('ember/tools/net80211-phy-ies-contract.c').split('static unsigned int checks, failures;')[0]
s=s.replace('unsigned ni_rssi, ni_rstamp, ni_rsn;', 'unsigned ni_rssi, ni_rstamp, ni_rsn, ni_fails;')
s=s.replace('struct ieee80211com;','struct ieee80211_rateset { uint8_t rs_nrates,rs_rates[IEEE80211_RATE_MAXSIZE]; };\nstruct ieee80211com;',1)
s=s.replace('struct {\n\t\tuint8_t rs_nrates, rs_rates[IEEE80211_RATE_MAXSIZE];\n\t} ni_rates;','struct ieee80211_rateset ni_rates;')
(out/'signal-scan-model.inc').write_text(s)
parts=[body('sys/net80211/ieee80211_input.c','ieee80211_saveie','void'),body('sys/net80211/ieee80211_node.c','saveie','static void'),body('sys/net80211/ieee80211_node.c','ieee80211_add_scan','void')]
(out/'signal-add-scan.inc').write_text(''.join(parts))
parts=['#define IEEE80211_CAPINFO_PRIVACY 0x10\n#define IEEE80211_RATE_VAL 0x7f\n#define IEEE80211_IS_CHAN_5GHZ(c) ((c)->ic_freq>3000)\n']
for n,r in [('node_getrssi','static uint8_t'),('maxrate','static uint8_t'),('ieee80211_node_compare','static int')]:parts.append(body('sys/net80211/ieee80211_node.c',n,r))
parts.append(body('sys/net80211/ieee80211_phy_ies.c','ieee80211_phy_ies_size','size_t'))
(out/'signal-node-compare.inc').write_text(''.join(parts))

(out/'signal-native-rx.inc').write_text(body('sys/dev/ic/rtw88_net.c','rtw88_net_rx_frame','static void'))
(out/'signal-routing-model.inc').write_text(read('ember/tools/rtw88-routing-contract.c').replace('int main(void)', 'int signal_existing_routing_main(void)'))
