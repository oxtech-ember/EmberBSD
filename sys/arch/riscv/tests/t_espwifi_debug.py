#!/usr/bin/env python3
"""Exercise the opt-in one-attempt ESP vendor debug lifecycle."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument('--cc', required=True)
p.add_argument('--analyze', action='store_true',
               help='run Clang Static Analyzer on the enabled helper')
p.add_argument('--cppcheck',
               help='run Cppcheck on the extracted RX-chain lifecycle helper')
a = p.parse_args()
arch = Path(__file__).resolve().parents[1]
wifi = arch / 'espressif/wifi'
source = (wifi / 'glue.c').read_text()

state = re.search(r'^static unsigned vendor_debug_once_state;$', source,
                  re.M)
assert state, 'debug-once state missing'
chain_state = re.search(
    r'^volatile uint32_t espwifi_rx_chain_diag_active;\n'
    r'volatile uint32_t espwifi_rx_chain_sta_input_count;\n'
    r'volatile uint32_t espwifi_rx_chain_crypto_decap_count;\n'
    r'volatile uint32_t espwifi_rx_chain_decap_count;\n'
    r'volatile uint32_t espwifi_rx_chain_eapol_count;$', source, re.M)
assert chain_state, 'RX chain counters missing'
chain_table_state = re.search(
    r'^extern void \* volatile net80211_funcs;\n.*?'
    r'^static struct espwifi_rx_chain_table_state '
    r'espwifi_rx_chain_table_begin;$', source, re.M | re.S)
assert chain_table_state, 'live RX-chain table snapshot state missing'
start_impl = re.search(r'\nstatic int\nespwifi_start_impl\(void\)\n\{.*?\n\}',
                       source, re.S)
assert start_impl, 'Wi-Fi start implementation missing'
start_body = start_impl.group()
alloc_enable_pos = start_body.index('espwifi_os_alloc_diag_enable();')
init_pos = start_body.index('esp_wifi_init_internal(&cfg)')
alloc_init_pos = start_body.index(
    'espwifi_os_alloc_diag_snapshot(&espwifi_rx_chain_alloc_init);')
hw_init_pos = start_body.index('espwifi_rx_chain_hw_ready = true;')
assert alloc_enable_pos < init_pos < alloc_init_pos < hw_init_pos, \
    'allocator baseline must bracket init and hardware baseline follow start'
counter_delta = re.search(
    r'\nstatic uint16_t\nespwifi_rx_counter_delta\(uint16_t before, '
    r'uint16_t after\)\n\{.*?\n\}', source, re.S)
assert counter_delta, 'wrap-aware RX hardware counter delta missing'
hw_snapshot = re.search(
    r'\nstatic esp_err_t\nespwifi_rx_hw_snapshot\('
    r'esp_test_hw_rx_statistics_t \*statistics\)\n\{.*?\n\}',
    source, re.S)
assert hw_snapshot, 'radio-state guarded hardware snapshot missing'
stop_case = re.search(
    r'case WIFI_EVENT_STA_STOP: \{.*?espwifi_rx_chain_hw_ready = false;'
    r'.*?espwifi_vendor_debug_once_end\(\);', source, re.S)
assert stop_case, 'STA_STOP must invalidate hardware reads before closing window'


def function(name, arguments):
    match = re.search(r'\nvoid\n' + name + r'\(' + arguments +
                      r'\)\n\{.*?\n\}', source, re.S)
    assert match, name
    return match.group()


def table_function(name):
    match = re.search(r'\nstatic struct espwifi_rx_chain_table_state\n' +
                      name + r'\(void\)\n\{.*?\n\}', source, re.S)
    assert match, name
    return match.group()


def slot_function():
    match = re.search(r'\nstatic bool\nespwifi_rx_chain_slot_matches\('
                      r'.*?\n\}', source, re.S)
    assert match, 'espwifi_rx_chain_slot_matches'
    return match.group()


code = r'''
#include <assert.h>
#include <stddef.h>
#include "fullmac_debug.h"
typedef int esp_err_t;
#define WIFI_LOG_VERBOSE 5
#define WIFI_LOG_WARNING 2
static unsigned critical_depth, level, set_calls;
static unsigned espwifi_os_critical_enter(void) { return ++critical_depth; }
static void espwifi_os_critical_exit(unsigned token) {
 assert(token == critical_depth);
 --critical_depth;
}
static int esp_wifi_internal_set_log_level(int next) {
 assert(critical_depth != 0);
 level = (unsigned)next;
 set_calls++;
 return 0;
}
static void espwifi_os_log(const char *fmt, ...) { (void)fmt; }
''' + state.group() + '\n' + function('espwifi_vendor_debug_once_begin',
    r'const unsigned char \*bssid') + '\n' + \
    function('espwifi_vendor_debug_once_end', r'void') + r'''
int main(void) {
 static const unsigned char bssid[6] = { 1, 2, 3, 4, 5, 6 };
 assert(level == 0 && set_calls == 0);
 espwifi_vendor_debug_once_begin(bssid);
 assert(level == WIFI_LOG_VERBOSE && set_calls == 1 && critical_depth == 0);
 espwifi_vendor_debug_once_begin(bssid);
 assert(level == WIFI_LOG_VERBOSE && set_calls == 1);
 espwifi_vendor_debug_once_end();
 assert(level == WIFI_LOG_WARNING && set_calls == 2 && critical_depth == 0);
 espwifi_vendor_debug_once_end();
 espwifi_vendor_debug_once_begin(bssid);
 assert(level == WIFI_LOG_WARNING && set_calls == 2);
 return 0;
}
'''

chain_code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "fullmac_debug.h"
typedef int esp_err_t;
struct espwifi_alloc_diag {
 unsigned long pool_free, pool_min_free, window_min_free;
 unsigned long pool_misses, fallback_successes;
 unsigned long fallback_requested_bytes, final_nulls;
};
typedef struct {
 uint16_t rx_fcs_err, rx_abort, rx_abort_fcs_pass, nrx_err_pwrdrop;
 uint16_t rx_mpdu, rx_end_cnt, rx_datasuc, rx_buf_fullcnt, rx_fifo_ovfcnt;
} esp_test_hw_rx_statistics_t;
typedef enum { WIFI_PS_NONE, WIFI_PS_MIN_MODEM, WIFI_PS_MAX_MODEM }
    wifi_ps_type_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 0x103
#define WIFI_LOG_VERBOSE 5
#define WIFI_LOG_WARNING 2
static unsigned critical_depth, level, set_calls, get_ps_calls, set_ps_calls;
static wifi_ps_type_t current_ps = WIFI_PS_MAX_MODEM;
static char log_message[4096];
static unsigned alloc_window_begin_calls;
static unsigned alloc_window_end_calls, alloc_snapshot_calls;
static unsigned hw_snapshot_calls;
static esp_err_t hw_snapshot_result = ESP_OK;
static struct espwifi_alloc_diag alloc_samples[2];
static esp_test_hw_rx_statistics_t hw_samples[3];
static unsigned espwifi_os_critical_enter(void) { return ++critical_depth; }
static void espwifi_os_critical_exit(unsigned token) {
 assert(token == critical_depth);
 --critical_depth;
}
static int esp_wifi_internal_set_log_level(int next) {
 assert(critical_depth != 0);
 level = (unsigned)next;
 set_calls++;
 return ESP_OK;
}
static esp_err_t esp_wifi_get_ps(wifi_ps_type_t *ps) {
 assert(critical_depth == 0 && ps != NULL);
 get_ps_calls++;
 *ps = current_ps;
 return ESP_OK;
}
static esp_err_t esp_wifi_set_ps(wifi_ps_type_t ps) {
 set_ps_calls++;
 current_ps = ps;
 return ESP_OK;
}
static void espwifi_os_alloc_diag_window_begin(void) {
 alloc_window_begin_calls++;
}
static void espwifi_os_alloc_diag_window_end(void) {
 alloc_window_end_calls++;
}
static void espwifi_os_alloc_diag_snapshot(struct espwifi_alloc_diag *out) {
 unsigned index = alloc_snapshot_calls < 2 ? alloc_snapshot_calls : 1;
 assert(critical_depth != 0 && out != NULL);
 *out = alloc_samples[index];
 alloc_snapshot_calls++;
}
static esp_err_t esp_test_get_hw_rx_statistics(
    esp_test_hw_rx_statistics_t *out) {
 unsigned index = hw_snapshot_calls < 3 ? hw_snapshot_calls : 2;
 assert(critical_depth != 0 && out != NULL);
 *out = hw_samples[index];
 hw_snapshot_calls++;
 return hw_snapshot_result;
}
static void espwifi_os_log(const char *fmt, ...) {
 va_list ap;
 size_t used = strlen(log_message);
 va_start(ap, fmt);
 (void)vsnprintf(log_message + used, sizeof(log_message) - used, fmt, ap);
 va_end(ap);
}
''' + chain_state.group() + '\n' + state.group() + '\n' + \
    chain_table_state.group() + '\n' + \
    counter_delta.group() + '\n' + \
    hw_snapshot.group() + '\n' + \
    slot_function() + '\n' + \
    table_function('espwifi_rx_chain_table_snapshot') + '\n' + \
    function('espwifi_vendor_debug_once_begin',
        r'const unsigned char \*bssid') + '\n' + \
    function('espwifi_vendor_debug_once_end', r'void') + r'''
static void wrap_sta_input(void) { }
static void wrap_crypto_decap(void) { }
static void wrap_decap(void) { }
static void wrap_sta_rx_eapol(void) { }
void __wrap_sta_input(void) { wrap_sta_input(); }
void __wrap_ieee80211_crypto_decap(void) { wrap_crypto_decap(); }
void __wrap_ieee80211_decap(void) { wrap_decap(); }
void __wrap_sta_rx_eapol(void) { wrap_sta_rx_eapol(); }
void * volatile net80211_funcs;
static unsigned char table_a[0x14c], table_b[0x14c];
static void set_table_entry(unsigned char *table, size_t offset,
    void (*entry)(void)) {
 memcpy(table + offset, &entry, sizeof(uint32_t));
}
static void install_wrappers(unsigned char *table) {
 set_table_entry(table, 0x58, __wrap_sta_input);
 set_table_entry(table, 0x60, __wrap_ieee80211_crypto_decap);
 set_table_entry(table, 0x64, __wrap_ieee80211_decap);
 set_table_entry(table, 0x68, __wrap_sta_rx_eapol);
}
int main(void) {
 static const unsigned char bssid[6] = { 1, 2, 3, 4, 5, 6 };
 esp_test_hw_rx_statistics_t stopped_stats = { 0 };
 (void)&esp_wifi_set_ps;
 alloc_samples[0] = (struct espwifi_alloc_diag) {
     .pool_free=900, .pool_min_free=800, .window_min_free=900,
     .pool_misses=10, .fallback_successes=3,
     .fallback_requested_bytes=60, .final_nulls=2 };
 alloc_samples[1] = (struct espwifi_alloc_diag) {
     .pool_free=850, .pool_min_free=790, .window_min_free=760,
     .pool_misses=13, .fallback_successes=5,
     .fallback_requested_bytes=97, .final_nulls=3 };
 hw_samples[0] = (esp_test_hw_rx_statistics_t) {
     .rx_fcs_err=100, .rx_abort=100, .nrx_err_pwrdrop=100,
     .rx_mpdu=100, .rx_end_cnt=100, .rx_datasuc=100,
     .rx_buf_fullcnt=100, .rx_fifo_ovfcnt=100 };
 hw_samples[1] = (esp_test_hw_rx_statistics_t) {
     .rx_fcs_err=65534, .rx_abort=65535, .rx_abort_fcs_pass=7,
     .nrx_err_pwrdrop=10, .rx_mpdu=65533, .rx_end_cnt=65534,
     .rx_datasuc=65534, .rx_buf_fullcnt=4, .rx_fifo_ovfcnt=8 };
 hw_samples[2] = (esp_test_hw_rx_statistics_t) {
     .rx_fcs_err=1, .rx_abort=2, .rx_abort_fcs_pass=9,
     .nrx_err_pwrdrop=12, .rx_mpdu=1, .rx_end_cnt=2,
     .rx_datasuc=1, .rx_buf_fullcnt=7, .rx_fifo_ovfcnt=9 };
 espwifi_rx_chain_alloc_init = (struct espwifi_alloc_diag) {
     .pool_free=950, .pool_min_free=910, .pool_misses=4,
     .fallback_successes=2, .fallback_requested_bytes=32,
     .final_nulls=1 };
 assert(espwifi_rx_hw_snapshot(&stopped_stats) == ESP_ERR_INVALID_STATE);
 assert(stopped_stats.rx_mpdu == 0 && hw_snapshot_calls == 0);
 espwifi_rx_chain_hw_ready = true;
 (void)espwifi_os_critical_enter();
 assert(espwifi_rx_hw_snapshot(&espwifi_rx_chain_hw_init) == ESP_OK);
 espwifi_os_critical_exit(critical_depth);
 espwifi_rx_chain_hw_init_result = ESP_OK;
 assert(level == 0 && set_calls == 0 && get_ps_calls == 0 &&
     set_ps_calls == 0);
 espwifi_rx_chain_sta_input_count=9;
 espwifi_rx_chain_crypto_decap_count=9;
 espwifi_rx_chain_decap_count=9;
 espwifi_rx_chain_eapol_count=9;
 espwifi_vendor_debug_once_begin(bssid);
 assert(level == WIFI_LOG_VERBOSE && set_calls == 1 && critical_depth == 0);
 assert(espwifi_rx_chain_diag_active == 1 &&
     espwifi_rx_chain_sta_input_count == 0 &&
     espwifi_rx_chain_crypto_decap_count == 0 &&
     espwifi_rx_chain_decap_count == 0 && espwifi_rx_chain_eapol_count == 0);
 assert(get_ps_calls == 1 && set_ps_calls == 0 &&
     current_ps == WIFI_PS_MAX_MODEM);
 assert(alloc_window_begin_calls == 1 && alloc_snapshot_calls == 1 &&
     hw_snapshot_calls == 2 && alloc_window_end_calls == 0);
 espwifi_vendor_debug_once_begin(bssid);
 assert(level == WIFI_LOG_VERBOSE && set_calls == 1 && get_ps_calls == 1 &&
     espwifi_rx_chain_diag_active == 1);
 espwifi_rx_chain_sta_input_count=4;
 espwifi_rx_chain_crypto_decap_count=5;
 espwifi_rx_chain_decap_count=6;
 espwifi_rx_chain_eapol_count=7;
 espwifi_vendor_debug_once_end();
 assert(level == WIFI_LOG_WARNING && set_calls == 2 && critical_depth == 0);
 assert(espwifi_rx_chain_diag_active == 0 &&
     espwifi_rx_chain_sta_input_count == 4 &&
     espwifi_rx_chain_crypto_decap_count == 5 &&
     espwifi_rx_chain_decap_count == 6 && espwifi_rx_chain_eapol_count == 7);
 assert(strstr(log_message, "sta_input=4 crypto_decap=5 decap=6 "
     "sta_rx_eapol=7") != NULL);
 assert(alloc_window_end_calls == 1 && alloc_snapshot_calls == 2 &&
     hw_snapshot_calls == 3);
 assert(strstr(log_message, "rx_mpdu=4 rx_end_cnt=4 rx_datasuc=3") != NULL);
 assert(strstr(log_message, "rx_fcs_err=3 rx_abort=3") != NULL);
 assert(strstr(log_message, "init_fallback=2 init_bytes=32") != NULL);
 assert(strstr(log_message, "miss=10->13 fallback=3->5") != NULL);
 assert(espwifi_rx_counter_delta(UINT16_MAX, 1) == 2);
 assert(get_ps_calls == 1 && set_ps_calls == 0 &&
     current_ps == WIFI_PS_MAX_MODEM);
 espwifi_vendor_debug_once_end();
 espwifi_vendor_debug_once_begin(bssid);
 assert(set_calls == 2 && get_ps_calls == 1 && set_ps_calls == 0 &&
     current_ps == WIFI_PS_MAX_MODEM && critical_depth == 0);
 install_wrappers(table_a);
 install_wrappers(table_b);
 net80211_funcs = table_a;
 vendor_debug_once_state = 0;
 espwifi_vendor_debug_once_begin(bssid);
 espwifi_vendor_debug_once_end();
 assert(strstr(log_message, "table_changed=0 wrapper_match=1 "
     "begin_slots=0xf end_slots=0xf") != NULL);
 vendor_debug_once_state = 0;
 net80211_funcs = table_a;
 espwifi_vendor_debug_once_begin(bssid);
 set_table_entry(table_b, 0x60, wrap_crypto_decap);
 net80211_funcs = table_b;
 espwifi_vendor_debug_once_end();
 assert(strstr(log_message, "table_changed=1 wrapper_match=0 "
     "begin_slots=0xf end_slots=0xd") != NULL);
 vendor_debug_once_state = 0;
 net80211_funcs = NULL;
 espwifi_vendor_debug_once_begin(bssid);
 net80211_funcs = table_a;
 espwifi_vendor_debug_once_end();
 assert(strstr(log_message, "table_changed=1 wrapper_match=0 "
     "begin_slots=0 end_slots=0xf") != NULL);
 vendor_debug_once_state = 0;
 net80211_funcs = table_a;
 espwifi_vendor_debug_once_begin(bssid);
 net80211_funcs = NULL;
 espwifi_vendor_debug_once_end();
 assert(strstr(log_message, "table_changed=1 wrapper_match=0 "
     "begin_slots=0xf end_slots=0") != NULL);
 vendor_debug_once_state = 0;
 net80211_funcs = table_a;
 hw_snapshot_result = ESP_ERR_INVALID_STATE;
	memset(log_message, 0, sizeof(log_message));
 espwifi_vendor_debug_once_begin(bssid);
 espwifi_vendor_debug_once_end();
 assert(strstr(log_message, "delta=unavailable") != NULL);
 return 0;
}
'''

disabled = r'''
#include "fullmac_debug.h"
int main(void) {
 espwifi_vendor_debug_once_begin((const unsigned char *)0);
 espwifi_vendor_debug_once_end();
 return 0;
}
'''

with tempfile.TemporaryDirectory(prefix='espwifi-debug-test-') as td:
    td = Path(td)
    enabled_src = td / 'enabled.c'
    disabled_src = td / 'disabled.c'
    disabled_bin = td / 'disabled'
    enabled_src.write_text(code)
    disabled_src.write_text(disabled)
    common = [a.cc, '-std=c11', '-Wall', '-Wextra', '-Werror',
              '-I' + str(wifi)]
    debug_flags = ['-DESPWIFI_VENDOR_DEBUG_ONCE']
    if a.analyze and not a.cppcheck:
        subprocess.run(common + debug_flags + ['--analyze', str(enabled_src),
                        '-o', str(td / 'enabled.plist')], check=True)
    binary = td / 'enabled'
    if not a.cppcheck:
        subprocess.run(common + debug_flags + ['-fsanitize=address,undefined',
                        '-fno-sanitize-recover=all', str(enabled_src),
                        '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
        subprocess.run(common + [str(disabled_src), '-o', str(disabled_bin)],
                       check=True)
        subprocess.run([str(disabled_bin)], check=True)

    chain_src = td / 'chain.c'
    chain_src.write_text(chain_code)
    chain_flags = debug_flags + ['-DESPWIFI_RX_CHAIN_ONCE']
    if a.cppcheck:
        subprocess.run([a.cppcheck, '--enable=warning,performance,portability',
                        '--error-exitcode=1', '--language=c', '--std=c11',
                        '-I' + str(wifi), *chain_flags, str(chain_src)],
                       check=True)
    if a.analyze and not a.cppcheck:
        subprocess.run(common + chain_flags + ['--analyze', str(chain_src),
                        '-o', str(td / 'chain.plist')], check=True)
    if not a.cppcheck:
        chain_bin = td / 'chain'
        subprocess.run(common + chain_flags + ['-fsanitize=address,undefined',
                        '-fno-sanitize-recover=all', str(chain_src),
                        '-o', str(chain_bin)], check=True)
        subprocess.run([str(chain_bin)], check=True)
