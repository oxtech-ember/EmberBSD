#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Generate table traces from the pinned BSD donor; no device access.
import hashlib
import pathlib
import re
import sys
if len(sys.argv) != 2:
 raise SystemExit("usage: rtw8821c-table-trace-ref.py PINNED_DONOR_TABLE_C")
data=pathlib.Path(sys.argv[1]).read_bytes()
if hashlib.sha256(data).hexdigest() != "ebc2547ddd66b7df434e0ef91319eddf5565c452863c4a0fd64660839d3e9a0c":
 raise SystemExit("unexpected donor SHA-256")
s=data.decode()
def array(name):
 m=re.search(r'static const u32 rtw8821c_'+name+r'\[\]\s*=\s*\{(.*?)\};',s,re.S)
 return [int(x,16) for x in re.findall(r'0x[0-9a-fA-F]+',m.group(1))]
def run(rfe,cut=0,pkg=0):
 h=14695981039346656037;n=0
 def event(kind,a,b):
  nonlocal h,n
  for x in (kind,a,b):
   for j in range(8):h=((h^((x>>(j*8))&255))*1099511628211)&((1<<64)-1)
  n+=1
 names=['mac','bb','agc']+(['agc_btg_type2'] if rfe in (2,4) else [])+['rf_a']
 for name in names:
  a=array(name); matched=True;skipped=False;positive=0
  for x,v in zip(a[::2],a[1::2]):
   if x&0x80000000:
    branch=(x>>28)&3
    if branch==3:matched=True;skipped=False
    elif branch==2:matched=not skipped
    else:positive=x
   elif x&0x40000000:
    if skipped:matched=False
    else:
     c=(positive>>24)&15;p=(positive>>12)&15;it=(positive>>8)&15;r=positive&255
     matched=(not c or c==(cut or 15)) and (not p or p==(pkg or 15)) and (not it or it==1) and r==rfe
     skipped=matched
   elif matched:
    if name=='mac':event(1,x,v&255)
    elif name=='bb' and x in (0xfe,0xfd,0xfc,0xfb,0xfa,0xf9):event(5,{0xfe:50000,0xfd:5000,0xfc:1000,0xfb:50,0xfa:5,0xf9:1}[x],0)
    elif name=='rf_a':
     if x in (0xffe,0xfe):event(5,50000 if x==0xffe else 100,0)
     else:event(4,0xc90,((x&255)<<20)|(v&0xfffff));event(5,13,0);event(5,1,0)
    else:event(4,x,v)
 print(rfe,cut,pkg,n,hex(h))
for p in [(0,0,0),(2,0,0),(4,1,1),(6,2,0)]:run(*p)
