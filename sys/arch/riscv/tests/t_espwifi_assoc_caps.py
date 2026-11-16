#!/usr/bin/env python3
"""Exercise the common ExtCap helper and ESP S31 association wrapper."""
import argparse
from pathlib import Path
import shlex
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument('--cc', required=True)
p.add_argument('--cflags', default='', help='optional host compile flags')
p.add_argument('--analyze', action='store_true')
p.add_argument('--cppcheck')
a = p.parse_args()
arch = Path(__file__).resolve().parents[1]
sysroot = arch.parents[1]
wifi = arch / 'espressif/wifi'

with tempfile.TemporaryDirectory(prefix='espwifi-assoc-caps-') as td:
    tmp = Path(td)
    include = tmp / 'include/net80211'
    include.mkdir(parents=True)
    (include / 'ieee80211_assoc_caps.h').symlink_to(
        sysroot / 'net80211/ieee80211_assoc_caps.h')
    os_header = tmp / 'mock_esp_wifi_os.h'
    os_header.write_text('''#include <stdint.h>\n'''
        '''#ifndef _RISCV_ESPRESSIF_ESP_WIFI_OS_H_\n'''
        '''#define _RISCV_ESPRESSIF_ESP_WIFI_OS_H_\n'''
        '''unsigned int espwifi_os_critical_enter(void);\n'''
        '''void espwifi_os_critical_exit(unsigned int);\n'''
        '''uintptr_t test_return_address(unsigned int);\n'''
        '''#endif\n''')
    cfile = Path(__file__).with_name('t_espwifi_assoc_caps.c')
    binary = tmp / 'fixture'
    common = ['-std=c11', '-Wall', '-Wextra', '-Werror',
        '-include', str(os_header), '-I', str(include.parent),
        '-I', str(wifi), '-D__builtin_return_address(level)=test_return_address(level)',
        *shlex.split(a.cflags)]
    sources = [str(cfile), str(wifi / 'assoc_caps.c')]
    if a.analyze:
        subprocess.run([a.cc, '--analyze', *common, *sources], check=True,
            cwd=td)
    subprocess.run([a.cc, *common, '-fsanitize=address,undefined',
        '-fno-sanitize-recover=all', *sources, '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
    if a.cppcheck:
        subprocess.run([a.cppcheck, '--enable=warning,performance,portability',
            '--suppress=missingIncludeSystem', '--error-exitcode=1', '--std=c11',
            '-I'+str(include.parent), '-I'+str(wifi),
            '-D__builtin_return_address(level)=test_return_address(level)',
            *sources], check=True)
