#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Actual management exchange plus complete native request callback."""
import pathlib,re,subprocess,sys,os
src,out=map(pathlib.Path,sys.argv[1:])
subprocess.run([sys.executable,str(src/'ember/tools/net80211-wme-contract.py'),str(src),str(out)],check=True)
parts=[]
net_source=pathlib.Path(os.environ.get('HT20_NATIVE_SOURCE',src/'sys/dev/ic/rtw88_net.c')).read_text()
parts.extend([net_source[:net_source.index('#include')],
              'static uint32_t ht20_atomic_cas_32(volatile uint32_t *p,uint32_t o,uint32_t n) { uint32_t e=o; __atomic_compare_exchange_n(p,&e,n,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST); return e; }\n',
              'static uint64_t ht20_atomic_cas_64(volatile uint64_t *p,uint64_t o,uint64_t n) { uint64_t e=o; __atomic_compare_exchange_n(p,&e,n,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST); return e; }\n',
              '#define atomic_load_relaxed(p) __atomic_load_n((p), __ATOMIC_RELAXED)\n',
              '#define atomic_load_acquire(p) __atomic_load_n((p), __ATOMIC_ACQUIRE)\n',
              '#define atomic_store_release(p,v) __atomic_store_n((p),(v),__ATOMIC_RELEASE)\n',
              '#define atomic_cas_32(p,o,n) ht20_atomic_cas_32((p),(o),(n))\n',
              '#define atomic_cas_64(p,o,n) ht20_atomic_cas_64((p),(o),(n))\n'])
for path,name,result in [('sys/dev/ic/rtw88.c','rtw88_ra_invalidate','void'),
                         ('sys/dev/ic/rtw88_net_policy.c','rtw88_net_request','uint64_t'),
                         ('sys/dev/ic/rtw88_net_policy.c','rtw88_net_commit','int'),
                         ('sys/dev/ic/rtw88_net_policy.c','rtw88_net_beacon_start','int'),
                         ('sys/dev/ic/rtw88_net_policy.c','rtw88_net_ht20_snapshot_encode','int'),
                         ('sys/dev/ic/rtw88_net.c','rtw88_net_ht20_publish','static void'),
                         ('sys/dev/ic/rtw88_net.c','rtw88_net_newstate','static int')]:
    s=pathlib.Path(os.environ.get('HT20_NATIVE_SOURCE',src/path) if path=='sys/dev/ic/rtw88_net.c' else src/path).read_text()
    parts.extend([s[:s.index('#include')],result,re.search(r'^'+name+r'\(.*?^}',s,re.M|re.S).group()])
(out/'native-ht20-callback.inc').write_text('\n'.join(parts)+'\n')

net=pathlib.Path(os.environ.get('HT20_NATIVE_SOURCE',src/'sys/dev/ic/rtw88_net.c')).read_text()
parts=[net[:net.index('#include')]]
if re.search(r'^rtw88_net_drain_wait\(',net,re.M):
 parts.extend(['static int',re.search(r'^rtw88_net_drain_wait\(.*?^}',net,re.M|re.S).group()])
if re.search(r'^rtw88_net_mailbox_wait\(',net,re.M):
 parts.extend(['static int',re.search(r'^rtw88_net_mailbox_wait\(.*?^}',net,re.M|re.S).group()])
parts.extend(['static void',re.search(r'^rtw88_net_state_work\(.*?^}',net,re.M|re.S).group()])
(out/'native-ht20-worker.inc').write_text('\n'.join(parts)+'\n')
(out/'native-ht20-policy.h').write_text((src/'sys/dev/ic/rtw88_net_policy.h').read_text())

with (out/'native-ht20-policy.h').open('a') as f:
 f.write(re.search(r'^#define\s+IEEE80211_FIXED_RATE_NONE\s+.*$',(src/'sys/net80211/ieee80211_var.h').read_text(),re.M).group()+'\n')

with (out/'native-ht20-policy.h').open('a') as f:
 f.write(re.search(r'^#define\s+RTW88_COMMAND_US\s+.*$',(src/'sys/dev/ic/rtw88var.h').read_text(),re.M).group()+'\n')

with (out/'native-ht20-policy.h').open('a') as f:
 f.write(re.search(r'^struct rtw88_channel_tuple \{.*?^\};',
     (src/'sys/dev/ic/rtw88_hw.h').read_text(),re.M|re.S).group()+'\n')

with (out/'native-ht20-policy.h').open('a') as f:
 f.write(re.search(r'^struct rtw88_phy_state \{.*?^\};',
     (src/'sys/dev/ic/rtw88_hw.h').read_text(),re.M|re.S).group()+'\n')

# The actual optional-IE ioctl the station's RSN gate reads.
ioctl=(src/'sys/net80211/ieee80211_ioctl.c').read_text()
(out/'native-ht20-setoptie.inc').write_text(ioctl[:ioctl.index('#include')]+'static int\n'+
    re.search(r'^ieee80211_ioctl_setoptie\(.*?^}',ioctl,re.M|re.S).group()+'\n')
