#!/usr/bin/env python3
"""Check the S31 management request encoding used by the real adapter."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--cc', required=True)
args = parser.parse_args()

arch = Path(__file__).resolve().parents[1]
source = (arch / 'espressif/wifi/fullmac_wpa2.c').read_text()
match = re.search(r'\nint\nespwifi_ext_action_tx\(.*?\n\}', source, re.S)
assert match, 'actual espwifi_ext_action_tx implementation not found'
function = match.group()

code = r'''
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_NO_MEM 0x101
#define WIFI_IF_STA 0
#define WLAN_FC_STYPE_ACTION 13
typedef struct wifi_mgmt_frm_req {
    int ifx;
    uint8_t subtype;
    uint32_t data_len;
    uint8_t da[6];
    uint8_t data[];
} wifi_mgmt_frm_req_t;

static uint8_t captured_subtype;
static unsigned captured_len;
static uint8_t captured_da[6];
static uint8_t captured_body[16];
static unsigned submit_calls;

static void *espwifi_os_malloc(size_t n) { return malloc(n); }
static void espwifi_os_free(void *p) { free(p); }
static void forced_memzero(void *p, size_t n) {
    volatile uint8_t *b = p;
    while (n-- != 0)
        *b++ = 0;
}
static int espwifi_vendor_action_tx_submit(wifi_mgmt_frm_req_t *req,
    size_t size, uint32_t cookie, uint32_t generation,
    const uint8_t *bssid)
{
    assert(size == offsetof(wifi_mgmt_frm_req_t, data) + req->data_len);
    assert(cookie == 0x1234 && generation == 7);
    assert(memcmp(bssid, req->da, sizeof(req->da)) == 0);
    captured_subtype = req->subtype;
    captured_len = req->data_len;
    memcpy(captured_da, req->da, sizeof(captured_da));
    memcpy(captured_body, req->data, captured_len);
    submit_calls++;
    return ESP_OK;
}
''' + function + r'''

int main(void)
{
    const uint8_t bssid[6] = { 2, 1, 2, 3, 4, 5 };
    const uint8_t body[3] = { 5, 4, 0x71 };
    const uint8_t expected_fc_subtype = WLAN_FC_STYPE_ACTION << 4;

    assert(espwifi_ext_action_tx(bssid, body, sizeof(body), 0x1234, 7)
        == ESP_OK);
    assert(submit_calls == 1);
    assert(captured_subtype == expected_fc_subtype);
    assert(captured_subtype == 0xd0);
    assert(captured_len == sizeof(body));
    assert(memcmp(captured_da, bssid, sizeof(bssid)) == 0);
    assert(memcmp(captured_body, body, sizeof(body)) == 0);

    assert(espwifi_ext_action_tx(bssid, body, sizeof(body), 0, 7)
        == ESP_ERR_INVALID_ARG);
    assert(submit_calls == 1);
    return 0;
}
'''

with tempfile.TemporaryDirectory() as td:
    cfile = Path(td) / 'action-request.c'
    binary = Path(td) / 'action-request'
    cfile.write_text(code)
    subprocess.run([args.cc, '-std=c11', '-Wall', '-Wextra', '-Werror',
                    str(cfile), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('actual S31 Action request uses encoded FC subtype 0xd0')
