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


def function(name, arguments):
    match = re.search(r'\nvoid\n' + name + r'\(' + arguments +
                      r'\)\n\{.*?\n\}', source, re.S)
    assert match, name
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
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "fullmac_debug.h"
typedef int esp_err_t;
typedef enum { WIFI_PS_NONE, WIFI_PS_MIN_MODEM, WIFI_PS_MAX_MODEM }
    wifi_ps_type_t;
#define ESP_OK 0
#define WIFI_LOG_VERBOSE 5
#define WIFI_LOG_WARNING 2
static unsigned critical_depth, level, set_calls, get_ps_calls, set_ps_calls;
static wifi_ps_type_t current_ps = WIFI_PS_MAX_MODEM;
static char log_message[192];
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
static void espwifi_os_log(const char *fmt, ...) {
 va_list ap;
 va_start(ap, fmt);
 (void)vsnprintf(log_message, sizeof(log_message), fmt, ap);
 va_end(ap);
}
''' + chain_state.group() + '\n' + state.group() + '\n' + \
    function('espwifi_vendor_debug_once_begin',
        r'const unsigned char \*bssid') + '\n' + \
    function('espwifi_vendor_debug_once_end', r'void') + r'''
int main(void) {
 static const unsigned char bssid[6] = { 1, 2, 3, 4, 5, 6 };
 (void)&esp_wifi_set_ps;
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
 assert(get_ps_calls == 1 && set_ps_calls == 0 &&
     current_ps == WIFI_PS_MAX_MODEM);
 espwifi_vendor_debug_once_end();
 espwifi_vendor_debug_once_begin(bssid);
 assert(set_calls == 2 && get_ps_calls == 1 && set_ps_calls == 0 &&
     current_ps == WIFI_PS_MAX_MODEM && critical_depth == 0);
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
