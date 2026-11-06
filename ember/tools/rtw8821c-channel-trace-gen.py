#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
from pathlib import Path
import hashlib
import re
import sys
if len(sys.argv) != 3:
 raise SystemExit("usage: rtw8821c-channel-trace-gen.py DONOR_DIR OUTPUT_C")
root=Path(sys.argv[1]);
for name, expected in {"rtw8821c.c":"a51946d4496d8856bbf01010f1786230478b951c4e034b67457a47d4acda414b","mac.c":"ec8a96ef305231f15af22bc94ad5708e0daf3bfb3ff470ec96757913e2be59f6","reg.h":"c8246f9305539347fdb62b0294fe12ea24769cb3ad56c2b0235c4269a0307324"}.items():
 if hashlib.sha256((root/name).read_bytes()).hexdigest()!=expected:
  raise SystemExit("unexpected donor digest: "+name)
c=(root/'rtw8821c.c').read_text();mac=(root/'mac.c').read_text()
def extract(text,name):
 m=re.search(r'(?:static )?(?:void|u32)\s+'+name+r'\s*\(',text); assert m,name
 start=m.start();i=text.index('{',m.end());depth=1;j=i+1
 while depth:
  if text[j]=='{':depth+=1
  if text[j]=='}':depth-=1
  j+=1
 return text[start:j]+'\n'
names=['rtw8821c_switch_rf_set','rtw8821c_set_channel_rf','rtw8821c_set_channel_rxdfir','rtw8821c_set_channel_bb','rtw8821c_get_bb_swing','rtw8821c_set_channel_bb_swing','rtw8821c_set_channel']
body='\n'.join(extract(c,x) for x in names)
base=Path(__file__).with_name("rtw8821c-channel-trace-preamble.c").read_text()
Path(sys.argv[2]).write_text(base+"\n"+extract(mac,"rtw_set_channel_mac")+"\n"+body+"\n"+Path(__file__).with_name("rtw8821c-channel-trace-main.c").read_text())
