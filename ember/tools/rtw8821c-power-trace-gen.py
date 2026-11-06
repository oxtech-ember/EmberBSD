#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
from pathlib import Path
import hashlib
import re
import sys
if len(sys.argv) != 3:
 raise SystemExit("usage: rtw8821c-power-trace-gen.py DONOR_DIR OUTPUT_C")
root=Path(sys.argv[1]);
for name, expected in {"phy.c":"669d76c8acfc9f30418dbd119e777eae4404f6e3bb1198292aab536a3aa9e4ec", "main.h":"bab159780b164959b5bd981d24544507558ed816334d49fd1a31d4d17a69285e"}.items():
 if hashlib.sha256((root/name).read_bytes()).hexdigest()!=expected:
  raise SystemExit("unexpected donor digest: "+name)
 p=(root/'phy.c').read_text(); m=(root/'main.h').read_text()
def function(name):
 x=re.search(r'static (?:u8|void)\s+'+name+r'\s*\(',p); assert x,name
 i=p.index('{',x.end()); d=1;j=i+1
 while d:
  d+=(p[j]=='{')-(p[j]=='}');j+=1
 return p[x.start():j]+'\n'
def block(marker):
 a=m.index(marker); match=re.search(r"\}\s*(?:__packed)?;",m[a:]); assert match; b=a+match.end();return m[a:b]+"\n"
head='''#include <stdint.h>\n#include <stdbool.h>\n#include <stdio.h>\n#include <string.h>\n#include "rtw8821c_table.h"\ntypedef uint8_t u8;typedef int8_t s8;typedef uint32_t u32;\n#define __LITTLE_ENDIAN 1\n#define __packed __attribute__((packed))\n#define WARN_ON(x) ((void)0)\n#define fallthrough __attribute__((fallthrough))\n#define rtw_warn(...) ((void)0)\n#define bcd2bin(x) ((((x)>>4)&15)*10+((x)&15))\n#define bcd_to_dec_pwr_by_rate(val,i) bcd2bin(((val)>>(i*8))&255)\n#define RTW_CHANNEL_WIDTH_20 0\n#define RTW_CHANNEL_WIDTH_40 1\nenum rtw_bandwidth { BW_20=0,BW_40=1 };\nstruct rtw_chip_info { int txgi_factor,is_pwr_by_rate_dec; };\nstruct rtw_dev { const struct rtw_chip_info *chip; };\n'''
head+=block('enum rtw_trx_desc_rate')
for name in ['struct rtw_2g_1s_pwr_idx_diff','struct rtw_2g_ns_pwr_idx_diff','struct rtw_2g_txpwr_idx']:head+=block(name)
# Decoder from donor.\n
head+=function('tbl_to_dec_pwr_by_rate')+function('rtw_phy_get_rate_values_of_txpwr_by_rate')+function('rtw_phy_get_2g_tx_power_index')
main='''int main(void) {\n struct rtw_chip_info chip={1,1};struct rtw_dev dev={&chip};struct rtw_2g_txpwr_idx cal={0};\n const struct rtw8821c_pg_pair *pg=rtw8821c_bb_pg_type0_tbl.data;const struct rtw8821c_limit_pair *l=rtw8821c_txpwr_lmt_type0_tbl.data;\n int vals[20]={0},limit[3]={127,127,127},base[3];u8 rates[4],n; s8 powers[4];\n cal.cck_base[0]=30;cal.bw40_base[0]=28;cal.ht_1s_diff.ofdm=1;cal.ht_1s_diff.bw20=-1;\n for(unsigned i=0;i<rtw8821c_bb_pg_type0_tbl.count;i++){if(pg[i].band||pg[i].addr<0xc20||pg[i].addr>0xc30)continue;n=0;rtw_phy_get_rate_values_of_txpwr_by_rate(&dev,pg[i].addr,pg[i].bitmask,pg[i].data,rates,powers,&n);for(unsigned j=0;j<n;j++)if(rates[j]<20)vals[rates[j]]=powers[j];}\n base[0]=vals[3];base[1]=vals[11];base[2]=vals[19];\n for(unsigned i=0;i<rtw8821c_txpwr_lmt_type0_tbl.count;i++)if(!l[i].band&&!l[i].bw&&l[i].ch==1&&l[i].rs<3&&l[i].txpwr_lmt<limit[l[i].rs])limit[l[i].rs]=l[i].txpwr_lmt;\n for(unsigned i=0;i<20;i++){unsigned rs=i<4?0:i<12?1:2;int off=vals[i]-base[rs], cap=limit[rs]-base[rs];int power=rtw_phy_get_2g_tx_power_index(&dev,&cal,BW_20,i,0)+(off<cap?off:cap);if(power>63)power=63;printf("%d%c",power,i==19?'\\n':',');}\n return 0;}\n'''
Path(sys.argv[2]).write_text(head+main)
