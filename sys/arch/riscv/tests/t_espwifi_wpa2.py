#!/usr/bin/env python3
"""Test the actual WPA2-only vendor configuration boundary."""
import argparse
from pathlib import Path
import re
import subprocess
import sys
import tempfile

if not __debug__:
    sys.exit('this test requires Python assertions enabled')

p=argparse.ArgumentParser();p.add_argument('--cc',required=True);a=p.parse_args()
arch=Path(__file__).resolve().parents[1]
s=(arch/'espressif/wifi/fullmac_wpa2.c').read_text()
files=(arch/'espressif/files.espressif').read_text()
assert re.search(r'^defflag\s+opt_espwifi_wpa2\.h\s+ESPWIFI_NET80211_WPA2:\s+ESPWIFI_NET80211\s*$',
                 files,re.M)
configs = {p.name: p.read_text(errors='ignore') for p in (arch/'conf').glob('*') if p.is_file()}
wpa2_profiles = {name for name, text in configs.items()
                  if re.search(r'\bESPWIFI_NET80211_WPA2\b', text)}
assert wpa2_profiles == {'ESP_MOSAICO_SC101IOT_WIFI'}
profile = configs['ESP_MOSAICO_SC101IOT_WIFI']
assert re.search(r'^include "arch/riscv/conf/ESP_MOSAICO_SC101IOT"$', profile, re.M)
for option in ('ESPWIFI', 'ESPWIFI_NET80211', 'ESPWIFI_NET80211_WPA2',
               'FPE', 'RISCV_FPU_SINGLE'):
    assert re.search(r'^options\s+' + option + r'\s*$', profile, re.M), option
assert re.search(r'^makeoptions\s+EXTRA_OBJ="\$\{ESPWIFI_OBJECT\}"$',
                 profile, re.M)
adapter=(arch/'espressif/esp_wifi_if.c').read_text()
assert re.search(r'#ifdef ESPWIFI_NET80211_WPA2\s+sc->sc_ic\.ic_caps\s*=\s*IEEE80211_C_AES_CCM\s*\|\s*IEEE80211_C_WPA2;',adapter)
def fn(name,result):
 m=re.search(r'\n(?:static )?'+result+r'\n'+name+r'\(.*?\)\n\{.*?\n\}',s,re.S)
 assert m,name
 return m.group()
code=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>
typedef int esp_err_t;
typedef struct { struct { uint8_t ssid[32],bssid[6]; bool bssid_set;
 struct {int authmode;} threshold; } sta; } wifi_config_t;
#define WIFI_IF_STA 0
#define WIFI_AUTH_WPA2_PSK 4
#define WIFI_APPIE_ASSOC_REQ 2
#define ESP_OK 0
static int cfgcalls,iecalls,connects,fail,unsets;static uint8_t copied_ie[64];static unsigned copied_len;
static int esp_wifi_set_config(int ifx,wifi_config_t *c) {
 assert(ifx==0 && c->sta.bssid_set && c->sta.threshold.authmode==4);
 assert(!memcmp(c->sta.ssid,"test",4));cfgcalls++;return fail==1?9:0; }
static int esp_wifi_set_appie_internal(int type,uint8_t *p,uint16_t n,int flag) {
 assert(type==2 && flag==0 && n<=sizeof(copied_ie));memcpy(copied_ie,p,n);
 copied_len=n;iecalls++;return fail==2?9:0; }
static int esp_wifi_connect(void) { connects++;return fail==3?9:0; }
static int esp_wifi_unset_appie_internal(int type) { assert(type==2);unsets++;return 0; }
''' + fn('espwifi_wpa2_rsn','bool') + fn('espwifi_ext_connect','int') + r'''
int main(void) {
 uint8_t ssid[4]={'t','e','s','t'},bssid[6]={2,3,4,5,6,7};
 uint8_t rsn[22]={48,20,1,0,0,15,172,4,1,0,0,15,172,4,1,0,0,15,172,2,0,0};
 assert(!espwifi_ext_connect(ssid,4,bssid,rsn,sizeof(rsn)));
 assert(cfgcalls==1 && iecalls==1 && connects==1 && copied_len==sizeof(rsn));
 assert(!memcmp(copied_ie,rsn,sizeof(rsn)));
 uint8_t zero_bssid[6]={0};
 assert(espwifi_ext_connect(ssid,4,zero_bssid,rsn,sizeof(rsn))==-1);
 rsn[19]=8;assert(espwifi_ext_connect(ssid,4,bssid,rsn,sizeof(rsn))==-1);rsn[19]=2;
 rsn[20]=0xc0;assert(espwifi_ext_connect(ssid,4,bssid,rsn,sizeof(rsn))==-1);rsn[20]=0;
 rsn[8]=2;assert(espwifi_ext_connect(ssid,4,bssid,rsn,sizeof(rsn))==-1);rsn[8]=1;
 assert(espwifi_ext_connect(ssid,4,bssid,rsn,21)==-1);
 bssid[0]=1;assert(espwifi_ext_connect(ssid,4,bssid,rsn,sizeof(rsn))==-1);bssid[0]=2;
 ssid[1]=0;assert(espwifi_ext_connect(ssid,4,bssid,rsn,sizeof(rsn))==-1);ssid[1]='e';
 for (int i=1;i<=3;i++) { fail=i;assert(espwifi_ext_connect(ssid,4,bssid,rsn,sizeof(rsn))==-1); }
 assert(unsets==1);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as td:
 c=Path(td)/'wpa2.c';out=Path(td)/'wpa2';c.write_text(code)
 subprocess.run([a.cc,'-Werror','-fsanitize=address,undefined',
                 '-fno-sanitize-recover=all',str(c),'-o',str(out)],check=True)
 subprocess.run([str(out)],check=True)
print('actual WPA2/CCMP vendor boundary rejects SAE, PMF and malformed input')

code=r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define ESP_OK 0
#define WIFI_WPA_ALG_CCMP 3
enum key_flag { KEY_FLAG_MODIFY=1, KEY_FLAG_RX=4, KEY_FLAG_TX=8,
 KEY_FLAG_GROUP=16, KEY_FLAG_PAIRWISE=32 };
static int calls,wipes,fail,expected_tx;static unsigned gotflags;
static int esp_wifi_set_sta_key_internal(int alg,uint8_t *addr,int index,int tx,
 uint8_t *seq,size_t sn,uint8_t *key,size_t kn,enum key_flag flags) {
 assert(alg==3 && addr[0]==2 && index==1 && tx==expected_tx && sn==6 && seq[0]==8);
 assert(kn==16 && key[0]==9);calls++;gotflags=flags;return fail?7:0; }
static void forced_memzero(void *p,size_t n) { memset(p,0,n);wipes++; }
''' + fn('espwifi_ext_set_key','int') + r'''
int main(void) {
 uint8_t addr[6]={2},seq[6]={8},key[16]={9};
 assert(espwifi_ext_set_key(addr,1,0,0,0,seq,6,key,16)==0);
 assert(calls==1 && wipes==2 && gotflags==(4|32));
 assert(espwifi_ext_set_key(addr,1,1,0,0,seq,6,key,16)==0);
 assert(calls==2 && wipes==4 && gotflags==(4|16));
 expected_tx=1;fail=1;assert(espwifi_ext_set_key(addr,1,0,1,1,seq,6,key,16)==-1 && wipes==6);
 assert(gotflags==(1|4|8|32));
 assert(espwifi_ext_set_key(addr,4,0,0,0,seq,6,key,16)==-1);
 assert(espwifi_ext_set_key(addr,1,0,0,0,seq,9,key,16)==-1);
 assert(espwifi_ext_set_key(addr,1,0,0,0,seq,6,key,15)==-1);
 assert(espwifi_ext_set_key(addr,1,2,0,0,seq,6,key,16)==-1);
 assert(calls==3 && wipes==6);return 0;
}
'''
with tempfile.TemporaryDirectory() as td:
 c=Path(td)/'key.c';out=Path(td)/'key';c.write_text(code)
 subprocess.run([a.cc,'-Werror','-fsanitize=address,undefined',
                 '-fno-sanitize-recover=all',str(c),'-o',str(out)],check=True)
 subprocess.run([str(out)],check=True)
print('actual vendor CCMP key boundary, flags, errors and key wipe passed')
