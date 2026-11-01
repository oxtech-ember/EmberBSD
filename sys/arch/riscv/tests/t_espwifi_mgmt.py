#!/usr/bin/env python3
"""Exercise the ESP vendor management-frame callback contract."""
import argparse
from pathlib import Path
import re
import subprocess
import sys
import tempfile

if not __debug__:
    sys.exit('this test requires Python assertions enabled')

p = argparse.ArgumentParser()
p.add_argument('--cc', required=True)
p.add_argument('--idf-driver-header', required=True)
p.add_argument('--wpa-source', required=True)
a = p.parse_args()
arch = Path(__file__).resolve().parents[1]
source = (arch / 'espressif/wifi/fullmac_wpa2.c').read_text()
parser_source = (arch / 'espressif/wifi/wpa_ie_bridge.c').read_text()
idf_header = Path(a.idf_driver_header).read_text()
wpa_source = Path(a.wpa_source).resolve(strict=True)

idf_funcs = re.search(r'struct wpa_funcs\s*\{(.*?)\n\};', idf_header, re.S)
assert idf_funcs, 'selected IDF wpa_funcs declaration missing'
idf_fields = re.findall(r'\(\s*\*\s*(\w+)\s*\)', idf_funcs.group(1))
assert idf_fields[15] == 'wpa_parse_wpa_ie'
assert idf_fields[21] == 'wpa_sta_rx_mgmt'


def function(name, result):
    match = re.search(r'\n(?:static )?' + result + r'\n' + name +
                      r'\(.*?\)\n\{.*?\n\}', source, re.S)
    assert match, name
    return match.group()


code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int esp_err_t;
#define ESP_OK 0
#define BIT(n) (1U << (n))
#define WPA_CIPHER_NONE BIT(0)
#define WPA_CIPHER_WEP40 BIT(1)
#define WPA_CIPHER_WEP104 BIT(2)
#define WPA_CIPHER_TKIP BIT(3)
#define WPA_CIPHER_CCMP BIT(4)
#define WPA_CIPHER_AES_128_CMAC BIT(5)
#define WPA_CIPHER_GCMP BIT(6)
#define WPA_CIPHER_SMS4 BIT(7)
#define WPA_CIPHER_GCMP_256 BIT(8)
#define WPA_CIPHER_BIP_GMAC_128 BIT(11)
#define WPA_CIPHER_BIP_GMAC_256 BIT(12)
#define WLAN_FC_STYPE_ASSOC_RESP 1
#define WLAN_FC_STYPE_REASSOC_RESP 3
#define WLAN_FC_STYPE_AUTH 11
#define WLAN_FC_STYPE_ACTION 13
typedef struct { int proto, pairwise_cipher, group_cipher, key_mgmt,
 capabilities; size_t num_pmkid; const u8 *pmkid; int mgmt_group_cipher;
 uint8_t rsnxe_capa; } wifi_wpa_ie_t;
typedef enum { WIFI_CIPHER_TYPE_NONE, WIFI_CIPHER_TYPE_WEP40,
 WIFI_CIPHER_TYPE_WEP104, WIFI_CIPHER_TYPE_TKIP, WIFI_CIPHER_TYPE_CCMP,
 WIFI_CIPHER_TYPE_TKIP_CCMP, WIFI_CIPHER_TYPE_AES_CMAC128,
 WIFI_CIPHER_TYPE_SMS4, WIFI_CIPHER_TYPE_GCMP, WIFI_CIPHER_TYPE_GCMP256,
 WIFI_CIPHER_TYPE_AES_GMAC128, WIFI_CIPHER_TYPE_AES_GMAC256,
 WIFI_CIPHER_TYPE_UNKNOWN } wifi_cipher_type_t;
enum espwifi_wpa_cipher { ESPWIFI_WPA_CIPHER_UNKNOWN,
 ESPWIFI_WPA_CIPHER_NONE, ESPWIFI_WPA_CIPHER_WEP40,
 ESPWIFI_WPA_CIPHER_WEP104, ESPWIFI_WPA_CIPHER_TKIP,
 ESPWIFI_WPA_CIPHER_CCMP, ESPWIFI_WPA_CIPHER_TKIP_CCMP,
 ESPWIFI_WPA_CIPHER_AES_CMAC128, ESPWIFI_WPA_CIPHER_SMS4,
 ESPWIFI_WPA_CIPHER_GCMP, ESPWIFI_WPA_CIPHER_GCMP256,
 ESPWIFI_WPA_CIPHER_AES_GMAC128, ESPWIFI_WPA_CIPHER_AES_GMAC256 };
struct wpa_funcs {
 bool (*wpa_sta_init)(void); bool (*wpa_sta_deinit)(void);
 int (*wpa_sta_connect)(uint8_t *); void (*wpa_sta_connected_cb)(uint8_t *);
 void (*wpa_sta_disconnected_cb)(uint8_t); int (*wpa_sta_rx_eapol)(u8 *,u8 *,u32);
 bool (*wpa_sta_in_4way_handshake)(void);
	void *wpa_ap_init;
	void *wpa_ap_deinit;
	void *wpa_ap_join;
	void *wpa_ap_remove;
	void *wpa_ap_get_wpa_ie;
	void *wpa_ap_rx_eapol;
	void *wpa_ap_get_peer_spp_msg;
	void *wpa_config_parse_string;
	int (*wpa_parse_wpa_ie)(const u8 *,size_t,wifi_wpa_ie_t *);
	void *wpa_config_bss;
	void *wpa_michael_mic_failure;
	void *wpa3_build_sae_msg;
	void *wpa3_parse_sae_msg;
	void *wpa3_hostap_handle_auth;
 int (*wpa_sta_rx_mgmt)(u8,u8 *,size_t,u8 *,int8_t,u8,u64);
};
_Static_assert(offsetof(struct wpa_funcs, wpa_parse_wpa_ie) ==
    15 * sizeof(void *), "IDF parser callback offset");
_Static_assert(offsetof(struct wpa_funcs, wpa_sta_rx_mgmt) ==
    21 * sizeof(void *), "IDF RX callback offset");
static unsigned filter_type, filter_subtype;
static int filter_calls, callback_calls;
static struct wpa_funcs *registered;
static unsigned assoc_calls;
static int parser_result, parser_calls;
struct wpa_ie_data { int proto, pairwise_cipher, has_pairwise, group_cipher,
 has_group, key_mgmt, capabilities; size_t num_pmkid; const uint8_t *pmkid;
 int mgmt_group_cipher; };
struct espwifi_wpa_ie { int proto, pairwise_cipher, group_cipher, key_mgmt,
 capabilities; size_t num_pmkid; const uint8_t *pmkid; int mgmt_group_cipher; };
static int wpa_parse_wpa_ie(const u8 *ie,size_t len,struct wpa_ie_data *out) {
 parser_calls++; assert(ie!=NULL && len==3 && ie[0]==48);
 memset(out,0,sizeof(*out)); out->proto=4; out->pairwise_cipher=WPA_CIPHER_CCMP;
 out->group_cipher=WPA_CIPHER_CCMP; out->key_mgmt=32; out->capabilities=64;
 out->mgmt_group_cipher=WPA_CIPHER_AES_128_CMAC; out->num_pmkid=2;
 out->pmkid=ie+1;
 return parser_result;
}
static int esp_wifi_register_mgmt_frame_internal(uint32_t type,uint32_t subtype) {
 filter_type=type; filter_subtype=subtype; filter_calls++; return ESP_OK;
}
static int esp_wifi_register_wpa_cb_internal(struct wpa_funcs *p) {
 registered=p; callback_calls++; return ESP_OK;
}
static int esp_wifi_sta_connect_internal(uint8_t *bssid) { return bssid!=NULL?ESP_OK:1; }
static void *espwifi_os_malloc_internal(unsigned long n) { return calloc(1,n); }
static void espwifi_os_free(void *p) { free(p); }
static int espwifi_os_rx_eapol(u8 *src,u8 *buf,u32 len) { return src&&buf?(int)len:-1; }
static bool espwifi_os_wpa_in_4way(void) { return true; }
static void espwifi_os_link(int up,const unsigned char *bssid) { (void)up;(void)bssid; }
static int espwifi_os_assoc_response(unsigned int subtype,const void *body,
 unsigned int len,const unsigned char *sender) {
 assert(subtype==WLAN_FC_STYPE_ASSOC_RESP && body!=NULL && len==6 && sender[0]==2);
 assoc_calls++; return 1;
}
''' + re.search(r'\nstatic enum espwifi_wpa_cipher\nespwifi_common_cipher\(.*?\n\}',
               parser_source, re.S).group() + \
    re.search(r'\nint\nespwifi_parse_wpa_ie\(.*?\n\}', parser_source, re.S).group() + \
    function('espwifi_wpa2_sta_connect', 'int') + \
    function('espwifi_wpa2_sta_init', 'bool') + \
    function('espwifi_wpa2_sta_deinit', 'bool') + \
    function('espwifi_wpa2_sta_connected', 'void') + \
    function('espwifi_wpa2_sta_disconnected', 'void') + \
    function('espwifi_wpa2_sta_in_4way', 'bool') + \
    function('espwifi_wpa2_sta_rx_eapol', 'int') + \
    function('espwifi_wpa2_sta_rx_mgmt', 'int') + \
    function('espwifi_cipher_type', 'wifi_cipher_type_t') + \
    function('espwifi_wpa2_parse_ie', 'int') + \
    function('espwifi_wpa2_register', 'int') + r'''
int main(void) {
 uint8_t body[6]={1,2,3,4,5,6}, sender[6]={2};
 const unsigned expected=(1U<<WLAN_FC_STYPE_ASSOC_RESP) |
     (1U<<WLAN_FC_STYPE_REASSOC_RESP);
 assert(espwifi_wpa2_register()==0 && filter_calls==1 && callback_calls==1);
 assert(filter_type==expected && filter_subtype==0 && registered!=NULL);
 assert(registered->wpa_sta_rx_mgmt(WLAN_FC_STYPE_ASSOC_RESP,body,6,sender,-35,6,99)==0);
 assert(assoc_calls==1);
 assert(registered->wpa_sta_rx_mgmt(WLAN_FC_STYPE_AUTH,body,6,sender,-35,6,99)==-1);
 assert(registered->wpa_sta_rx_mgmt(WLAN_FC_STYPE_ACTION,body,6,sender,-35,6,99)==-1);
 assert(registered->wpa_sta_rx_mgmt(8,body,6,sender,-35,6,99)==-1);
 assert(assoc_calls==1);
 wifi_wpa_ie_t parsed; uint8_t ie[3]={48,1,0};
 assert(registered->wpa_parse_wpa_ie==espwifi_wpa2_parse_ie);
 assert(registered->wpa_parse_wpa_ie(ie,sizeof(ie),&parsed)==0);
 assert(parser_calls==1 && parsed.proto==4 &&
     parsed.pairwise_cipher==WIFI_CIPHER_TYPE_CCMP &&
     parsed.group_cipher==WIFI_CIPHER_TYPE_CCMP &&
     parsed.key_mgmt==32 && parsed.capabilities==64 && parsed.pmkid==ie+1 &&
     parsed.num_pmkid==2 &&
     parsed.mgmt_group_cipher==WIFI_CIPHER_TYPE_AES_CMAC128 &&
     parsed.rsnxe_capa==0);
 parser_result=-1; memset(&parsed,0xa5,sizeof(parsed));
 assert(registered->wpa_parse_wpa_ie(ie,sizeof(ie),&parsed)==-1);
 assert(parsed.proto==0 && parsed.pairwise_cipher==0 && parsed.pmkid==NULL &&
     parsed.rsnxe_capa==0);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as td:
    c = Path(td) / 'mgmt.c'
    out = Path(td) / 'mgmt'
    c.write_text(code)
    subprocess.run([a.cc, '-Werror', '-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all', str(c), '-o', str(out)],
                   check=True)
    subprocess.run([str(out)], check=True)
print('IDF callback layout, association forwarding, and unsupported AUTH/ACTION rejection passed')

# Exercise the selected common parser implementation itself. Only its logging
# hooks are stubbed; WPA/RSN IE parsing and bridge field conversion are real.
parser_test = r'''
#include <assert.h>
#include "utils/includes.h"
#include "utils/common.h"
#include "common/wpa_common.h"
#include "rsn_supp/wpa.h"
#include "wpa_ie_bridge.h"
void wpa_printf(int level, const char *fmt, ...) { (void)level; (void)fmt; }
void wpa_hexdump(int level, const char *title, const void *data, size_t len) {
    (void)level; (void)title; (void)data; (void)len;
}
int main(void) {
    const u8 rsn[] = {48,20,1,0,0,15,172,4,1,0,0,15,172,4,
        1,0,0,15,172,2,0,0};
    const u8 wpa[] = {221,24,0,80,242,1,1,0,0,80,242,4,
        1,0,0,80,242,4,1,0,0,80,242,2,0,0};
    struct espwifi_wpa_ie data;
    assert(espwifi_parse_wpa_ie(rsn, sizeof(rsn), &data) == 0);
    assert(data.proto == WPA_PROTO_RSN);
    assert(data.group_cipher == ESPWIFI_WPA_CIPHER_CCMP);
    assert(data.pairwise_cipher == ESPWIFI_WPA_CIPHER_CCMP);
    assert(data.key_mgmt == WPA_KEY_MGMT_PSK);
    assert(espwifi_parse_wpa_ie(wpa, sizeof(wpa), &data) == 0);
    assert(data.proto == WPA_PROTO_WPA);
    assert(data.group_cipher == ESPWIFI_WPA_CIPHER_CCMP);
    assert(data.pairwise_cipher == ESPWIFI_WPA_CIPHER_CCMP);
    assert(data.key_mgmt == WPA_KEY_MGMT_PSK);
    memset(&data, 0xa5, sizeof(data));
    assert(espwifi_parse_wpa_ie(rsn, sizeof(rsn) - 1, &data) != 0);
    assert(data.proto == 0 && data.pairwise_cipher == 0 && data.pmkid == NULL);
    return 0;
}
'''
with tempfile.TemporaryDirectory() as td:
    td = Path(td)
    c = td / 'parser.c'
    out = td / 'parser'
    c.write_text(parser_test)
    common_flags = [
        '-I' + str(wpa_source), '-I' + str(wpa_source / 'utils'),
        '-I' + str(wpa_source / 'crypto'), '-I' + str(Path(__file__).resolve().parents[1] / 'espressif/wifi'),
        '-D__ets__', '-DESP_PLATFORM', '-DESP_SUPPLICANT',
        '-DIEEE8021X_EAPOL', '-DESPRESSIF_USE', '-DCONFIG_IEEE80211W',
        '-DCONFIG_SHA256', '-DCONFIG_NO_RADIUS', '-DCONFIG_INTERNAL_SHA256',
        '-DCONFIG_CRYPTO_INTERNAL', '-DCONFIG_GMAC=1',
        '-DWLAN_FC_ISWEP=WLAN_FC_PROTECTED', '-DWLAN_FC_ORDER=WLAN_FC_HTC',
        '-ffunction-sections', '-fdata-sections', '-fsanitize=address,undefined',
        '-fno-sanitize-recover=all']
    linker_gc = '-Wl,-dead_strip' if sys.platform == 'darwin' else '-Wl,--gc-sections'
    subprocess.run([a.cc, '-Werror', *common_flags,
        str(wpa_source / 'rsn_supp/wpa_ie.c'),
        str(wpa_source / 'common/wpa_common.c'),
        str(wpa_source / 'utils/common.c'),
        str(Path(__file__).resolve().parents[1] / 'espressif/wifi/wpa_ie_bridge.c'),
        str(c), linker_gc, '-o', str(out)], check=True)
    subprocess.run([str(out)], check=True)
print('selected shared WPA/RSN parser behavior passed under ASan/UBSan')
