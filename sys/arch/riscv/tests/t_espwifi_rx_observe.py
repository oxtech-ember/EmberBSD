#!/usr/bin/env python3
"""Exercise bounded metadata parsing for the one-shot FullMAC RX observer."""
import argparse
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument('--cc', required=True)
p.add_argument('--analyze', action='store_true',
               help='run Clang Static Analyzer on the parser fixture')
a = p.parse_args()
arch = Path(__file__).resolve().parents[1]
wifi = arch / 'espressif/wifi'
glue = (wifi / 'glue.c').read_text()


def glue_function(name):
    import re
    match = re.search(r'\nstatic void\n' + name + r'\([^)]*\)\n\{.*?\n\}',
                      glue, re.S)
    assert match, name
    return match.group()

code = r'''
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "fullmac_rx_observe.h"

static const uint8_t bssid[6] = { 0x02, 0x11, 0x22, 0x33, 0x44, 0x55 };
static const uint8_t mac[6] = { 0x02, 0xaa, 0xbb, 0xcc, 0xdd, 0xee };
static const uint8_t other[6] = { 0x02, 0x66, 0x77, 0x88, 0x99, 0x00 };

static void
fill_header(uint8_t *frame, uint8_t fc0, uint8_t fc1)
{
 memset(frame, 0, 160);
 frame[0] = fc0;
 frame[1] = fc1;
 memcpy(frame + 4, mac, sizeof(mac));
 memcpy(frame + 10, bssid, sizeof(bssid));
 memcpy(frame + 16, other, sizeof(other));
}

static size_t
fill_eapol(uint8_t *frame, uint8_t fc0, uint8_t fc1, int qos,
    int amsdu, uint8_t type, uint16_t key_info)
{
 static const uint8_t snap[6] = { 0xaa, 0xaa, 3, 0, 0, 0 };
 size_t header = 24;
 size_t body_len = type == 3 ? 95 : 1;

 fill_header(frame, fc0, fc1);
 if (qos) {
  frame[header] = amsdu ? 0x80 : 0;
  header += 2;
  if ((fc1 & 0x80) != 0)
   header += 4;
 }
 memcpy(frame + header, snap, sizeof(snap));
 frame[header + 6] = 0x88;
 frame[header + 7] = 0x8e;
 frame[header + 8] = 2;
 frame[header + 9] = type;
 frame[header + 10] = (uint8_t)(body_len >> 8);
 frame[header + 11] = (uint8_t)body_len;
 frame[header + 12] = 2;
 frame[header + 13] = (uint8_t)(key_info >> 8);
 frame[header + 14] = (uint8_t)key_info;
 return header + 12 + body_len;
}

int
main(void)
{
 struct espwifi_rx_observation s = { 0 };
 struct espwifi_rx_observation t = { 0 };
 uint8_t frame[160];
 size_t len;

 /* Selected-AP management frame; unrelated AP management is ignored. */
 fill_header(frame, 0x80, 0);
 espwifi_rx_observe_frame(&s, frame, 24, bssid, mac);
 assert(s.mgmt == 1 && s.data == 0);
 memcpy(frame + 10, other, sizeof(other));
 espwifi_rx_observe_frame(&s, frame, 24, bssid, mac);
 assert(s.mgmt == 1);

 /* Non-QoS From-DS EAPOL-Key records type and key-info metadata. */
 len = fill_eapol(frame, 0x08, 0x02, 0, 0, 3, 0x008a);
 espwifi_rx_observe_frame(&s, frame, len, bssid, mac);
 assert(s.data == 1 && s.llc == 1 && s.eapol == 1);
 assert(s.eapol_type == 3 && s.key_info == 0x008a);

 /* QoS + HT-control header shifts the same metadata by six bytes. */
 len = fill_eapol(frame, 0x88, 0x82, 1, 0, 3, 0x010a);
 espwifi_rx_observe_frame(&s, frame, len, bssid, mac);
 assert(s.data == 2 && s.llc == 2 && s.eapol == 2);
 assert(s.eapol_type == 3 && s.key_info == 0x010a);

 /* Wrong station, AP, or direction is excluded. */
 len = fill_eapol(frame, 0x08, 0x02, 0, 0, 3, 0x008a);
 memcpy(frame + 4, other, sizeof(other));
 espwifi_rx_observe_frame(&s, frame, len, bssid, mac);
 fill_eapol(frame, 0x08, 0x02, 0, 0, 3, 0x008a);
 memcpy(frame + 10, other, sizeof(other));
 espwifi_rx_observe_frame(&s, frame, len, bssid, mac);
 fill_eapol(frame, 0x08, 0x01, 0, 0, 3, 0x008a);
 espwifi_rx_observe_frame(&s, frame, len, bssid, mac);
 assert(s.data == 2 && s.eapol == 2);

 /* Protected frames are counted but their payload is never parsed. */
 len = fill_eapol(frame, 0x08, 0x42, 0, 0, 3, 0x008a);
 espwifi_rx_observe_frame(&s, frame, len, bssid, mac);
 assert(s.data == 3 && s.protected_data == 1);
 assert(s.llc == 2 && s.eapol == 2);

 /* A-MSDU and null-data frames are metadata-only, not LLC/EAPOL. */
 fill_eapol(frame, 0x88, 0x02, 1, 1, 3, 0x008a);
 espwifi_rx_observe_frame(&s, frame, 26, bssid, mac);
 assert(s.data == 4 && s.aggregate == 1);
 fill_header(frame, 0x48, 0x02);
 espwifi_rx_observe_frame(&s, frame, 24, bssid, mac);
 assert(s.data == 5 && s.llc == 2 && s.eapol == 2);

 /* Null and truncated buffers do not read outside their supplied bounds. */
 espwifi_rx_observe_frame(&t, NULL, 160, bssid, mac);
 fill_header(frame, 0x08, 0x02);
 espwifi_rx_observe_frame(&t, frame, 23, bssid, mac);
 assert(t.mgmt == 0 && t.data == 0 && t.truncated == 0);
 espwifi_rx_observe_frame(&t, frame, 31, bssid, mac);
 assert(t.data == 1 && t.truncated == 1 && t.llc == 0);
 fill_header(frame, 0x88, 0x02);
 espwifi_rx_observe_frame(&t, frame, 25, bssid, mac);
 assert(t.data == 2 && t.truncated == 2);
 fill_header(frame, 0x88, 0x82);
 espwifi_rx_observe_frame(&t, frame, 29, bssid, mac);
 assert(t.data == 3 && t.truncated == 3);

 /* Truncated EAPOL after LLC recognition reports truncation without type. */
 len = fill_eapol(frame, 0x08, 0x02, 0, 0, 3, 0x008a);
 espwifi_rx_observe_frame(&t, frame, 24 + 11, bssid, mac);
 assert(len > 24 + 11);
 assert(t.llc == 1 && t.eapol == 1 && t.truncated == 4);
 assert(t.eapol_type == 0 && t.key_info == 0);
 return 0;
}
'''

with tempfile.TemporaryDirectory(prefix='espwifi-rx-observe-test-') as td:
    td = Path(td)
    source = td / 'rx_observe.c'
    binary = td / 'rx_observe'
    source.write_text(code)
    common = [a.cc, '-std=c11', '-Wall', '-Wextra', '-Werror',
              '-I' + str(wifi)]
    if a.analyze:
        subprocess.run(common + ['--analyze', str(source), '-o',
                        str(td / 'rx_observe.plist')], check=True)
    subprocess.run(common + ['-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all', str(source), '-o',
                    str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('actual FullMAC RX observer bounds and metadata parser passed')

lifecycle = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "fullmac_rx_observe.h"
typedef int esp_err_t;
typedef int wifi_interface_t;
typedef int wifi_promiscuous_pkt_type_t;
typedef void (*wifi_promiscuous_cb_t)(void *, wifi_promiscuous_pkt_type_t);
typedef struct { uint32_t filter_mask; } wifi_promiscuous_filter_t;
#define ESP_OK 0
#define WIFI_IF_STA 0
#define WIFI_PROMIS_FILTER_MASK_MGMT 1u
#define WIFI_PROMIS_FILTER_MASK_DATA 2u
static unsigned critical;
static bool rx_observe_saved_mode;
static struct espwifi_rx_observation rx_observation;
static wifi_promiscuous_filter_t rx_observe_saved_filter;
static uint8_t rx_observe_bssid[6], rx_observe_mac[6];
static bool rx_observe_active, rx_observe_configured;
static unsigned fail_at, calls[8];
static bool prior_mode;
static uint32_t prior_filter;
static unsigned espwifi_os_critical_enter(void) { return ++critical; }
static void espwifi_os_critical_exit(unsigned token) {
 assert(token == critical); --critical;
}
static void espwifi_os_log(const char *fmt, ...) { (void)fmt; }
static void espwifi_scan_capture_rx(void *buffer,
    wifi_promiscuous_pkt_type_t type) { (void)buffer; (void)type; }
static esp_err_t esp_wifi_get_promiscuous(bool *mode) {
 calls[1]++; *mode = prior_mode; return fail_at == 1 ? -1 : ESP_OK;
}
static esp_err_t esp_wifi_get_promiscuous_filter(wifi_promiscuous_filter_t *f) {
 calls[2]++; f->filter_mask = prior_filter;
 return fail_at == 2 ? -2 : ESP_OK;
}
static esp_err_t esp_wifi_get_mac(wifi_interface_t iface, uint8_t *mac) {
 static const uint8_t expected[6] = { 1, 2, 3, 4, 5, 6 };
 calls[3]++; assert(iface == WIFI_IF_STA); memcpy(mac, expected, 6);
 return fail_at == 3 ? -3 : ESP_OK;
}
static esp_err_t esp_wifi_set_promiscuous_rx_cb(wifi_promiscuous_cb_t cb) {
 calls[4]++; assert(cb == espwifi_scan_capture_rx);
 return fail_at == 4 ? -4 : ESP_OK;
}
static esp_err_t esp_wifi_set_promiscuous_filter(
    wifi_promiscuous_filter_t *f) {
 calls[5]++;
 if (calls[5] == 1)
  assert(f->filter_mask == (WIFI_PROMIS_FILTER_MASK_MGMT |
      WIFI_PROMIS_FILTER_MASK_DATA));
 else
  assert(f->filter_mask == prior_filter);
 return fail_at == 5 ? -5 : ESP_OK;
}
static esp_err_t esp_wifi_set_promiscuous(bool enabled) {
 calls[6]++;
 return fail_at == 6 && enabled && rx_observe_active ? -6 : ESP_OK;
}
''' + glue_function('espwifi_rx_observe_end') + '\n' + \
    glue_function('espwifi_rx_observe_begin') + r'''
static void reset_case(unsigned failure, bool was_enabled) {
 memset(calls, 0, sizeof(calls));
 memset(&rx_observation, 0, sizeof(rx_observation));
 rx_observe_configured = rx_observe_active = false;
 fail_at = failure; prior_mode = was_enabled; prior_filter = 0x40;
}
int main(void) {
 static const uint8_t peer[6] = { 9, 8, 7, 6, 5, 4 };
 unsigned i;
 reset_case(0, true);
 espwifi_rx_observe_begin(peer);
 assert(rx_observe_configured && rx_observe_active);
 assert(memcmp(rx_observe_bssid, peer, 6) == 0);
 assert(rx_observe_saved_mode && rx_observe_saved_filter.filter_mask == 0x40);
 assert(calls[1] == 1 && calls[2] == 1 && calls[3] == 1 && calls[4] == 1);
 assert(calls[5] == 1 && calls[6] == 1 && critical == 0);
 espwifi_rx_observe_end();
 assert(!rx_observe_configured && !rx_observe_active);
 assert(calls[5] == 2 && calls[6] == 3 && critical == 0);
 espwifi_rx_observe_end();
 assert(calls[5] == 2 && calls[6] == 3);

 /* Getter, MAC, and callback setup failures leave observation unconfigured. */
 for (i = 1; i <= 4; i++) {
  reset_case(i, true);
  espwifi_rx_observe_begin(peer);
  assert(!rx_observe_configured && !rx_observe_active && critical == 0);
  assert(calls[6] == 0);
 }
 /* Filter/enable failures roll back and restore the prior enabled mode. */
 for (i = 5; i <= 6; i++) {
  reset_case(i, true);
  espwifi_rx_observe_begin(peer);
  assert(!rx_observe_configured && !rx_observe_active && critical == 0);
 assert(calls[5] == 2 && calls[6] == (i == 5 ? 2u : 3u));
 }
 /* A previously disabled mode is restored without enabling it on end. */
 reset_case(0, false);
 espwifi_rx_observe_begin(peer);
 assert(rx_observe_configured && calls[6] == 1);
 espwifi_rx_observe_end();
 assert(!rx_observe_configured && calls[6] == 2 && critical == 0);
 return 0;
}
'''

with tempfile.TemporaryDirectory(prefix='espwifi-rx-lifecycle-test-') as td:
    td = Path(td)
    source = td / 'rx_lifecycle.c'
    binary = td / 'rx_lifecycle'
    source.write_text(lifecycle)
    common = [a.cc, '-std=c11', '-Wall', '-Wextra', '-Werror',
              '-I' + str(wifi)]
    if a.analyze:
        subprocess.run(common + ['--analyze', str(source), '-o',
                        str(td / 'rx_lifecycle.plist')], check=True)
    subprocess.run(common + ['-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all', str(source), '-o',
                    str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('actual FullMAC RX observer lifecycle passed')
