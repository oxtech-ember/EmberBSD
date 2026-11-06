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
a = p.parse_args()
arch = Path(__file__).resolve().parents[1]
wifi = arch / 'espressif/wifi'
source = (wifi / 'glue.c').read_text()

state = re.search(r'^static unsigned vendor_debug_once_state;$', source,
                  re.M)
assert state, 'debug-once state missing'


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
    if a.analyze:
        subprocess.run(common + debug_flags + ['--analyze', str(enabled_src),
                        '-o', str(td / 'enabled.plist')], check=True)
    binary = td / 'enabled'
    subprocess.run(common + debug_flags + ['-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all', str(enabled_src),
                    '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
    subprocess.run(common + [str(disabled_src), '-o', str(disabled_bin)],
                   check=True)
    subprocess.run([str(disabled_bin)], check=True)
