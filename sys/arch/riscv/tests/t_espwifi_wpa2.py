#!/usr/bin/env python3
"""Test the actual WPA2 and SAE/PMF vendor configuration boundary."""
import argparse
import importlib.util
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

if not __debug__:
    sys.exit('this test requires Python assertions enabled')

p=argparse.ArgumentParser()
p.add_argument('--cc',required=True)
p.add_argument('--nm',help='symbol tool; defaults to llvm-nm or nm beside --cc')
a=p.parse_args()
arch=Path(__file__).resolve().parents[1]
sysroot=arch.parents[1]
s=(arch/'espressif/wifi/fullmac_wpa2.c').read_text()
glue=(arch/'espressif/wifi/glue.c').read_text()
files=(arch/'espressif/files.espressif').read_text()
marker=re.search(r'^#define ESPWIFI_EXTERNAL_AUTH_PROFILE_MARKER "([^"]+)"$',
                 s,re.M)
assert marker is not None and marker.group(1)
assert re.search(r'#ifndef ESPWIFI_EXTERNAL_WPA2\s+#error',s)
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
producer_path=arch/'espressif/wifi/mkblob.py'
producer_spec=importlib.util.spec_from_file_location('espwifi_mkblob',producer_path)
producer=importlib.util.module_from_spec(producer_spec)
producer_spec.loader.exec_module(producer)
assert "'-DESPWIFI_EXTERNAL_WPA2'" in (arch/'espressif/wifi/mkblob.py').read_text()
with tempfile.TemporaryDirectory() as td:
    idf=Path(td)/'components'
    for native in ('wpa_supplicant/src/rsn_supp/wpa.c',
                   'wpa_supplicant/esp_supplicant/src/esp_wpa_main.c'):
        try:
            producer.check_external_auth_boundary([(idf/native,[])],idf)
        except ValueError:
            pass
        else:
            raise AssertionError('producer accepted native IDF WPA source '+native)
    for symbol in ('wpa_set_profile','wpa_set_bss','wpa_set_passphrase',
                   'esp_supplicant_init'):
        try:
            producer.check_external_auth_boundary([],idf,{symbol})
        except ValueError:
            pass
        else:
            raise AssertionError('producer accepted native IDF WPA symbol '+symbol)
    local_c=Path(td)/'local.c'
    local_o=Path(td)/'local.o'
    local_c.write_text('__attribute__((used)) static void '
                       'wpa_set_passphrase(void) {}\n')
    subprocess.run([a.cc,'-c',str(local_c),'-o',str(local_o)],check=True)
    if a.nm:
        nm=Path(shutil.which(a.nm) or a.nm)
    else:
        cc_path=Path(shutil.which(a.cc) or a.cc).absolute()
        nm=next((candidate for candidate in
                 (cc_path.with_name('llvm-nm'),cc_path.with_name('nm'))
                 if candidate.is_file()),None)
    if nm is None or not nm.is_file():
        raise SystemExit('provide --nm or install llvm-nm/nm beside --cc')
    local_symbols=producer.defined_symbols_for(local_o,nm,os.environ.copy())
    if 'wpa_set_passphrase' not in local_symbols and \
            '_wpa_set_passphrase' in local_symbols:
        local_symbols.add('wpa_set_passphrase')  # Mach-O host-object spelling
    assert 'wpa_set_passphrase' in local_symbols
    try:
        producer.check_external_auth_boundary([],idf,local_symbols)
    except ValueError:
        pass
    else:
        raise AssertionError('producer accepted local/static native PMK symbol')
    producer.check_external_auth_boundary([(producer_path,[])],idf,set())
adapter=(arch/'espressif/esp_wifi_if.c').read_text()
assert re.search(r'#ifdef ESPWIFI_NET80211_WPA2\s+sc->sc_ic\.ic_caps\s*=\s*IEEE80211_C_AES_CCM\s*\|\s*IEEE80211_C_WPA2;',adapter)
disconnect=re.search(r'\nespwifi_disconnect\(void\)\n\{(.*?)\n\}',glue,re.S)
assert disconnect is not None
assert re.search(r'espwifi_ext_clear_assoc_appies\(\).*?'
                 r'esp_wifi_stop\(\)',disconnect.group(1),re.S)
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
#include <net80211/ieee80211_rsn.h>
#include "fullmac_debug.h"
typedef int esp_err_t;
typedef struct { struct { uint8_t ssid[32],bssid[6]; bool bssid_set;
 uint8_t password[64];
 struct {int authmode;} threshold; struct {bool capable,required;} pmf_cfg;
 uint8_t sae_pwe_h2e; } sta; } wifi_config_t;
#define WIFI_IF_STA 0
#define WIFI_AUTH_WPA2_PSK 3
#define WIFI_AUTH_WPA3_PSK 6
#define WIFI_APPIE_ASSOC_REQ 2
#define WIFI_APPIE_RSN 4
#define ESP_OK 0
static int cfgcalls,rsncalls,assoc_calls,connects,fail,unset_rsn;
static int unset_assoc,pmf_configured;
static uint8_t copied_rsn[64],copied_assoc[64];
static unsigned copied_rsn_len,copied_assoc_len;
#define ESPWIFI_EXTERNAL_AUTH_PROFILE_MARKER "@external-auth-marker@"
_Static_assert(sizeof(ESPWIFI_EXTERNAL_AUTH_PROFILE_MARKER) <=
 sizeof(((wifi_config_t *)0)->sta.password), "marker must fit vendor field");
static bool vendor_accepts_encrypted_profile(const wifi_config_t *c) {
 return c->sta.threshold.authmode >= WIFI_AUTH_WPA2_PSK &&
     c->sta.password[0] != 0;
}
static void espwifi_os_log(const char *fmt, ...) { (void)fmt; }
static int esp_wifi_set_config(int ifx,wifi_config_t *c) {
 assert(ifx==0 && c->sta.bssid_set);
 assert(c->sta.threshold.authmode==3 || c->sta.threshold.authmode==6);
 if (c->sta.threshold.authmode==3)
  assert(!c->sta.pmf_cfg.capable && !c->sta.pmf_cfg.required);
 else
  assert(c->sta.pmf_cfg.capable && c->sta.pmf_cfg.required);
 assert(vendor_accepts_encrypted_profile(c));
 assert(!strcmp((const char *)c->sta.password,
     ESPWIFI_EXTERNAL_AUTH_PROFILE_MARKER));
 assert(!memcmp(c->sta.ssid,"test",4));cfgcalls++;return fail==1?9:0; }
static int esp_wifi_set_appie_internal(int type,uint8_t *p,uint16_t n,int flag) {
 assert(n<=sizeof(copied_rsn));
 if (type==WIFI_APPIE_RSN) {
  assert(flag==1);memcpy(copied_rsn,p,n);copied_rsn_len=n;rsncalls++;
  return fail==2?9:0;
 }
 assert(type==WIFI_APPIE_ASSOC_REQ && flag==0);
 memcpy(copied_assoc,p,n);copied_assoc_len=n;assoc_calls++;
 return fail==3?9:0; }
static int esp_wifi_connect(void) { connects++;return fail==4?9:0; }
static int esp_wifi_unset_appie_internal(int type) {
 if (type==WIFI_APPIE_RSN) unset_rsn++;
 else { assert(type==WIFI_APPIE_ASSOC_REQ);unset_assoc++; }
 return 0; }
static int esp_wifi_sta_pmf_enabled(void) { return pmf_configured; }
static unsigned esp_wifi_get_config_sae_pwe_h2e_internal(int ifx) {
 assert(ifx==WIFI_IF_STA);return 0; }
''' + fn('espwifi_wpa2_rsn','bool') + fn('espwifi_ext_clear_assoc_appies','void') + fn('espwifi_ext_connect','int') + fn('espwifi_ext_sta_pmf_configured','int') + r'''
int main(void) {
 uint8_t ssid[4]={'t','e','s','t'},bssid[6]={2,3,4,5,6,7};
 uint8_t rsn[22]={48,20,1,0,0,15,172,4,1,0,0,15,172,4,1,0,0,15,172,2,0,0};
 assert(!espwifi_ext_connect(ssid,4,bssid,rsn,sizeof(rsn),0));
 assert(cfgcalls==1 && rsncalls==1 && assoc_calls==0 && connects==1);
 assert(copied_rsn_len==sizeof(rsn) && !memcmp(copied_rsn,rsn,sizeof(rsn)));
 assert(unset_rsn==1 && unset_assoc==2);
 /* Both stale slots are cleared on entry; empty supplemental IEs are unset. */
 uint8_t zero_bssid[6]={0};
 assert(espwifi_ext_connect(ssid,4,zero_bssid,rsn,sizeof(rsn),0)==-1);
 rsn[19]=8;assert(espwifi_ext_connect(ssid,4,bssid,rsn,sizeof(rsn),0)==-1);rsn[19]=2;
 rsn[20]=0xc0;assert(espwifi_ext_connect(ssid,4,bssid,rsn,sizeof(rsn),0)==-1);rsn[20]=0;
 rsn[8]=2;assert(espwifi_ext_connect(ssid,4,bssid,rsn,sizeof(rsn),0)==-1);rsn[8]=1;
 assert(espwifi_ext_connect(ssid,4,bssid,rsn,21,0)==-1);
 bssid[0]=1;assert(espwifi_ext_connect(ssid,4,bssid,rsn,sizeof(rsn),0)==-1);bssid[0]=2;
 ssid[1]=0;assert(espwifi_ext_connect(ssid,4,bssid,rsn,sizeof(rsn),0)==-1);ssid[1]='e';
 rsn[19]=8;rsn[20]=0xc0;
 uint8_t ies[48],dup[64];
 memcpy(ies,rsn,sizeof(rsn));
 ies[22]=244;ies[23]=1;ies[24]=0x20; /* RSNXE: SAE H2E */
 ies[25]=221;ies[26]=2;ies[27]=0xaa;ies[28]=0xbb;
 assert(!espwifi_ext_connect(ssid,4,bssid,ies,29,1));
 assert(copied_rsn_len==sizeof(rsn) && !memcmp(copied_rsn,rsn,sizeof(rsn)));
 assert(copied_assoc_len==7 && !memcmp(copied_assoc,ies+22,7));
 int before_unset_rsn=unset_rsn, before_unset_assoc=unset_assoc;
 fail=1; /* set_config failure after a prior attempt must still clear both */
 assert(espwifi_ext_connect(ssid,4,bssid,ies,29,1)==-1);
 assert(unset_rsn==before_unset_rsn+2 &&
     unset_assoc==before_unset_assoc+2);
 fail=0;
 uint8_t reordered[48];
 reordered[0]=221;reordered[1]=1;reordered[2]=0x55;
 memcpy(reordered+3,rsn,sizeof(rsn));memcpy(reordered+25,ies+22,7);
 assert(!espwifi_ext_connect(ssid,4,bssid,reordered,32,1));
 assert(copied_rsn_len==sizeof(rsn) && !memcmp(copied_rsn,rsn,sizeof(rsn)));
 assert(copied_assoc_len==10 && !memcmp(copied_assoc,reordered,3) &&
     !memcmp(copied_assoc+3,reordered+25,7));
 memcpy(dup,ies,29);memcpy(dup+29,rsn,sizeof(rsn));
 assert(espwifi_ext_connect(ssid,4,bssid,dup,51,1)==-1);
 memcpy(dup,ies,29);dup[29]=244;dup[30]=1;dup[31]=0x20;
 assert(espwifi_ext_connect(ssid,4,bssid,dup,32,1)==-1);
 memcpy(dup,ies,29);dup[24]=0x21;
 assert(espwifi_ext_connect(ssid,4,bssid,dup,29,1)==-1);
 memcpy(dup,ies,29);dup[26]=20;
 assert(espwifi_ext_connect(ssid,4,bssid,dup,29,1)==-1);
 pmf_configured=1;assert(espwifi_ext_sta_pmf_configured()==1);
 pmf_configured=0;assert(espwifi_ext_sta_pmf_configured()==0);
 for (int i=1;i<=4;i++) { fail=i;assert(espwifi_ext_connect(ssid,4,bssid,ies,29,1)==-1); }
 assert(rsncalls>=4 && assoc_calls>=2 && connects>=2);
 return 0;
}
'''
code=code.replace('#define ESPWIFI_EXTERNAL_AUTH_PROFILE_MARKER "@external-auth-marker@"',
                  '#define ESPWIFI_EXTERNAL_AUTH_PROFILE_MARKER "'+marker.group(1)+'"')
with tempfile.TemporaryDirectory() as td:
 c=Path(td)/'wpa2.c';out=Path(td)/'wpa2';c.write_text(code)
 subprocess.run([a.cc,'-I',str(arch/'espressif/wifi'),'-idirafter',str(sysroot),'-Werror','-fsanitize=address,undefined',
                 '-fno-sanitize-recover=all',str(c),'-o',str(out)],check=True)
 subprocess.run([str(out)],check=True)
print('actual WPA2/SAE parser, RSN/RSNXE appie split, validation and cleanup passed')

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

code=r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define ESP_OK 0
#define WIFI_IF_STA 0
typedef int esp_err_t;
typedef struct { uint8_t keyid[2],pn[6],igtk[32]; } wifi_wpa_igtk_t;
static int calls,wipes,fail,pmf_configured;
static int esp_wifi_sta_pmf_enabled(void) { return pmf_configured; }
static int esp_wifi_set_igtk_internal(uint8_t ifx,const wifi_wpa_igtk_t *p) {
 assert(ifx==WIFI_IF_STA && p->keyid[0]==4 && p->keyid[1]==0);
 assert(p->pn[0]==8 && p->igtk[0]==9 && p->igtk[15]==9);
 for (unsigned i=16;i<sizeof(p->igtk);i++) assert(p->igtk[i]==0);
 calls++;return fail?7:0;
}
static void forced_memzero(void *p,size_t n) { memset(p,0,n);wipes++; }
''' + fn('espwifi_ext_set_igtk','int') + r'''
int main(void) {
 uint8_t seq[6]={8},key[16];memset(key,9,sizeof(key));
 assert(espwifi_ext_set_igtk(4,seq,key)==0 && calls==1 && wipes==1);
 fail=1;assert(espwifi_ext_set_igtk(4,seq,key)==-1 && calls==2 && wipes==2);
 assert(espwifi_ext_set_igtk(3,seq,key)==-1 && calls==2 && wipes==2);
 assert(espwifi_ext_set_igtk(5,NULL,key)==-1 && calls==2 && wipes==2);
 assert(espwifi_ext_set_igtk(5,seq,NULL)==-1 && calls==2 && wipes==2);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as td:
 c=Path(td)/'igtk.c';out=Path(td)/'igtk';c.write_text(code)
 subprocess.run([a.cc,'-Werror','-fsanitize=address,undefined',
                 '-fno-sanitize-recover=all',str(c),'-o',str(out)],check=True)
 subprocess.run([str(out)],check=True)
print('actual vendor IGTK conversion, bounds, errors and key wipe passed')
