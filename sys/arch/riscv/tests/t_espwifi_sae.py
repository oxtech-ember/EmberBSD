#!/usr/bin/env python3
"""Exercise the actual IDF SAE callback adapters and mailbox contract."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument('--cc', required=True)
p.add_argument('--idf-driver-header', required=True)
a = p.parse_args()
arch = Path(__file__).resolve().parents[1]
source = (arch / 'espressif/wifi/fullmac_wpa2.c').read_text()
header = Path(a.idf_driver_header).read_text()

def function(name, result):
    match = re.search(r'\n(?:static )?' + re.escape(result) + r'\s+' + name +
                      r'\s*\(.*?\)\n\{.*?\n\}', source, re.S)
    assert match, 'missing production callback ' + name
    return match.group()

match = re.search(r'struct wpa_funcs\s*\{(.*?)\n\};', header, re.S)
assert match, 'selected IDF wpa_funcs declaration missing'
fields = re.findall(r'\(\s*\*\s*(\w+)\s*\)', match.group(1))
assert fields[18:20] == ['wpa3_build_sae_msg', 'wpa3_parse_sae_msg']

code = r'''
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_WIFI_TIMEOUT -2
#define ESPWIFI_SAE_COMMIT 1
#define ESPWIFI_SAE_CONFIRM 2
#define ESPWIFI_SAE_BODY_MAX (1536 - 30)
static uint8_t espwifi_sae_body[ESPWIFI_SAE_BODY_MAX];
struct wpa_funcs {
 void *slots[18];
 u8 *(*wpa3_build_sae_msg)(u8 *,u32,size_t *);
 int (*wpa3_parse_sae_msg)(u8 *,size_t,u32,uint16_t);
};
static unsigned sequence[32], sequence_count;
static unsigned build_calls, parse_calls;
static unsigned expected_type;
static unsigned result_build, result_parse;
static uint8_t returned_body[] = {0x91,0x82,0x73};
static uint8_t expected_bssid[6] = {2,1,2,3,4,5};
void forced_memzero(void *p, size_t n) { memset(p, 0, n); }
int espwifi_os_sae_build(const uint8_t *, uint32_t, uint8_t *, unsigned int,
    unsigned int *);
int espwifi_os_sae_parse(const uint8_t *, size_t, uint32_t, unsigned int);
''' + function('espwifi_wpa3_build_sae_msg', 'uint8_t *') + \
    function('espwifi_wpa3_parse_sae_msg', 'int') + r'''
int espwifi_os_sae_build(const uint8_t *bssid, uint32_t type, uint8_t *buf,
    unsigned int capacity, unsigned int *len)
{
 build_calls++; sequence[sequence_count++]=100+type;
 assert(memcmp(bssid, expected_bssid, sizeof(expected_bssid))==0);
 assert(type==expected_type);
 if (type==ESPWIFI_SAE_COMMIT && build_calls==1) sequence[sequence_count++]=1; /* START */
 if (result_build) return result_build;
 assert(capacity>=sizeof(returned_body));
 memcpy(buf,returned_body,sizeof(returned_body)); *len=sizeof(returned_body);
 sequence[sequence_count++]=2; /* supplicant TX_FRAME */
 return 0;
}
int espwifi_os_sae_parse(const uint8_t *buf, size_t len, uint32_t type,
    unsigned int status)
{
 parse_calls++; sequence[sequence_count++]=3; /* RX_FRAME */
 assert(buf!=NULL && len==3 && buf[0]==0xaa);
 assert(status==0 && (type==ESPWIFI_SAE_COMMIT || type==ESPWIFI_SAE_CONFIRM));
 if (type==ESPWIFI_SAE_CONFIRM) sequence[sequence_count++]=4; /* AUTH_STATUS */
 return result_parse;
}
int main(void)
{
 size_t len=0;
 uint8_t rx[]={0xaa,0xbb,0xcc};
 uint8_t *body;
 expected_type=ESPWIFI_SAE_COMMIT;
 body=espwifi_wpa3_build_sae_msg(expected_bssid, ESPWIFI_SAE_COMMIT, &len);
 assert(body!=NULL && len==sizeof(returned_body));
 assert(memcmp(body,returned_body,len)==0);
 assert(sequence_count==3 && sequence[0]==101 && sequence[1]==1 && sequence[2]==2);
 assert(espwifi_wpa3_parse_sae_msg(rx,sizeof(rx),ESPWIFI_SAE_COMMIT,0)==0);
 assert(sequence_count==4 && sequence[3]==3);
 expected_type=ESPWIFI_SAE_CONFIRM;
 body=espwifi_wpa3_build_sae_msg(expected_bssid, ESPWIFI_SAE_CONFIRM, &len);
 assert(body!=NULL && len==sizeof(returned_body));
 assert(sequence[4]==102 && sequence[5]==2);
 len=99;
 result_build=ESP_ERR_WIFI_TIMEOUT;
 assert(espwifi_wpa3_build_sae_msg(expected_bssid,ESPWIFI_SAE_CONFIRM,&len)==NULL);
 assert(len==0);
 result_build=0;
 result_parse=ESP_FAIL;
 assert(espwifi_wpa3_parse_sae_msg(rx,sizeof(rx),ESPWIFI_SAE_CONFIRM,0)==ESP_FAIL);
 assert(sequence[7]==3 && sequence[8]==4);
 assert(build_calls==3 && parse_calls==2);
 /* Timeout and cancellation must not return stale callback-owned bytes. */
 expected_type=ESPWIFI_SAE_COMMIT;
 result_build=ESP_ERR_WIFI_TIMEOUT;
 len=17;
 assert(espwifi_wpa3_build_sae_msg(expected_bssid,ESPWIFI_SAE_COMMIT,&len)==NULL);
 assert(len==0);
 result_build=0;
 body=espwifi_wpa3_build_sae_msg(expected_bssid,ESPWIFI_SAE_COMMIT,&len);
 assert(body!=NULL && len==sizeof(returned_body));
 result_build=ESTALE;
 len=17;
 assert(espwifi_wpa3_build_sae_msg(expected_bssid,ESPWIFI_SAE_COMMIT,&len)==NULL);
 assert(len==0);
 result_build=0;
 result_parse=0;
 assert(espwifi_wpa3_parse_sae_msg(rx,sizeof(rx),ESPWIFI_SAE_COMMIT,0)==0);
 result_parse=ECANCELED;
 assert(espwifi_wpa3_parse_sae_msg(rx,sizeof(rx),ESPWIFI_SAE_CONFIRM,0)==ESP_FAIL);
 assert(espwifi_wpa3_parse_sae_msg(NULL,sizeof(rx),ESPWIFI_SAE_CONFIRM,0)==ESP_FAIL);
 return 0;
}
'''

with tempfile.TemporaryDirectory() as td:
    c = Path(td) / 'sae.c'
    out = Path(td) / 'sae'
    c.write_text(code)
    subprocess.run([a.cc, '-Werror', '-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all', str(c), '-o', str(out)],
                   check=True)
    subprocess.run([str(out)], check=True)
print('IDF SAE callback mailbox order, error, and output-buffer contracts passed')
