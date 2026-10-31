#!/usr/bin/env python3
"""Exercise the actual optional STA controlled-port ioctl helper."""
import argparse
from pathlib import Path
import re
import subprocess
import sys
import tempfile

if not __debug__:
    sys.exit('this test requires Python assertions enabled')

p=argparse.ArgumentParser();p.add_argument('--cc',required=True);a=p.parse_args()
s=(Path(__file__).resolve().parents[1]/'ieee80211_ioctl.c').read_text()
f=re.search(r'\nstatic int\nieee80211_ioctl_sta_port\(.*?\)\n\{.*?\n\}',s,re.S)
assert f
assert 'return ieee80211_ioctl_sta_port(ic, ireq, false);' in s
assert 'return ieee80211_ioctl_sta_port(ic, ireq, true);' in s
assert re.search(r'case SIOCS80211:\s*error = suser\(curthread\);\s*'
                 r'if \(error == 0\)\s*error = ieee80211_ioctl_set80211', s)
code=r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <errno.h>
#define IEEE80211_M_STA 1
struct ieee80211com { int ic_opmode; int (*ic_sta_set_port)(struct ieee80211com *,bool); };
struct ieee80211req { int i_val; };
static int called,last,fail;
static int setport(struct ieee80211com *ic,bool on) { (void)ic;called++;last=on;return fail; }
''' + f.group() + r'''
int main(void) {
 struct ieee80211com ic={0};struct ieee80211req r={.i_val=1};
 assert(ieee80211_ioctl_sta_port(&ic,&r,false)==EOPNOTSUPP);
 ic.ic_opmode=IEEE80211_M_STA;
 assert(ieee80211_ioctl_sta_port(&ic,&r,true)==EOPNOTSUPP);
 ic.ic_sta_set_port=setport;
 assert(!ieee80211_ioctl_sta_port(&ic,&r,false) && r.i_val==1 && !called);
 r.i_val=2;assert(ieee80211_ioctl_sta_port(&ic,&r,true)==EINVAL && !called);
 r.i_val=1;assert(!ieee80211_ioctl_sta_port(&ic,&r,true) && called==1 && last==1);
 r.i_val=0;fail=EIO;assert(ieee80211_ioctl_sta_port(&ic,&r,true)==EIO && called==2 && !last);
 ic.ic_opmode=0;assert(ieee80211_ioctl_sta_port(&ic,&r,true)==EOPNOTSUPP && called==2);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as td:
 c=Path(td)/'port.c';out=Path(td)/'port';c.write_text(code)
 subprocess.run([a.cc,'-Werror','-fsanitize=address,undefined',
                 '-fno-sanitize-recover=all',str(c),'-o',str(out)],check=True)
 subprocess.run([str(out)],check=True)
print('actual STA port ioctl capability, bounds and callback errors passed')
