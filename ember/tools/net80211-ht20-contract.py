#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Actual management exchange plus complete native request callback."""
import pathlib,re,subprocess,sys,os
src,out=map(pathlib.Path,sys.argv[1:])
subprocess.run([sys.executable,str(src/'ember/tools/net80211-wme-contract.py'),str(src),str(out)],check=True)
parts=[]
for path,name,result in [('sys/dev/ic/rtw88.c','rtw88_ra_invalidate','void'),
                         ('sys/dev/ic/rtw88_net_policy.c','rtw88_net_request','uint64_t'),
                         ('sys/dev/ic/rtw88_net_policy.c','rtw88_net_commit','int'),
                         ('sys/dev/ic/rtw88_net_policy.c','rtw88_net_beacon_start','int'),
                         ('sys/dev/ic/rtw88_net.c','rtw88_net_newstate','static int')]:
    s=pathlib.Path(os.environ.get('HT20_NATIVE_SOURCE',src/path) if path=='sys/dev/ic/rtw88_net.c' else src/path).read_text()
    parts.extend([s[:s.index('#include')],result,re.search(r'^'+name+r'\(.*?^}',s,re.M|re.S).group()])
(out/'native-ht20-callback.inc').write_text('\n'.join(parts)+'\n')

net=pathlib.Path(os.environ.get('HT20_NATIVE_SOURCE',src/'sys/dev/ic/rtw88_net.c')).read_text()
parts=[net[:net.index('#include')]]
if re.search(r'^rtw88_net_mailbox_wait\(',net,re.M):
 parts.extend(['static int',re.search(r'^rtw88_net_mailbox_wait\(.*?^}',net,re.M|re.S).group()])
parts.extend(['static void',re.search(r'^rtw88_net_state_work\(.*?^}',net,re.M|re.S).group()])
(out/'native-ht20-worker.inc').write_text('\n'.join(parts)+'\n')
(out/'native-ht20-policy.h').write_text((src/'sys/dev/ic/rtw88_net_policy.h').read_text())

with (out/'native-ht20-policy.h').open('a') as f:
 f.write(re.search(r'^#define\s+IEEE80211_FIXED_RATE_NONE\s+.*$',(src/'sys/net80211/ieee80211_var.h').read_text(),re.M).group()+'\n')

with (out/'native-ht20-policy.h').open('a') as f:
 f.write(re.search(r'^#define\s+RTW88_COMMAND_US\s+.*$',(src/'sys/dev/ic/rtw88var.h').read_text(),re.M).group()+'\n')
