#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# AI-assisted native source-slice contract. No target compilation or radio I/O.
"""Run the actual legacy BSD scan and native NWID/SCAN_REQ consumers.

Kernel layout, copyin, ioctl transport and state scheduling are modeled explicitly.
Only the named kernel switch arms are compiled; unrelated ioctl cases are omitted.
The complete BSD scan/media/SSID functions and setupscan body remain unchanged.
"""
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT = Path(os.environ.get('MANUAL_SCAN_SOURCE', Path(__file__).resolve().parents[2]))

def function(source, name):
    match = re.search(r'^' + re.escape(name) + r'\(', source, re.M)
    if not match:
        raise RuntimeError('missing function: ' + name)
    end = source.index('\n}', match.start()) + 2
    return source[match.start():end]

def arm(source, name):
    match = re.search(r'^\tcase ' + name + r':\n', source, re.M)
    if not match:
        raise RuntimeError('missing case: ' + name)
    following = re.search(r'^\t(?:case |default:)', source[match.end():], re.M)
    if not following:
        raise RuntimeError('missing next case: ' + name)
    return source[match.start():match.end() + following.start()]

bsd = (ROOT / 'external/bsd/wpa/dist/src/drivers/driver_bsd.c').read_text()
stack = (ROOT / 'sys/net80211/ieee80211_ioctl.c').read_text()
net = (ROOT / 'sys/dev/ic/rtw88_net.c').read_text()
with tempfile.TemporaryDirectory(prefix='manual-scan-') as directory:
    work = Path(directory)
    generated = ['static int ' + function(stack, 'ieee80211_setupscan')]
    # Both selected dispatcher arms come verbatim from their owning functions.
    native = stack[stack.index('#ifdef __NetBSD__\n\nint\nieee80211_ioctl('):]
    generated += ['static int kernel_nwid(struct ieee80211com *ic, void *data) {\n'
                  'struct ifreq *ifr=data; struct ieee80211_nwid nwid; int error=0;\n'
                  'switch (SIOCS80211NWID) {\n' + arm(native, 'SIOCS80211NWID') + '\n}\nreturn error;\n}']
    setter = function(stack, 'ieee80211_ioctl_set80211')
    generated += ['static int kernel_set(struct ieee80211com *ic, struct ieee80211req *ireq) {\n'
                  'int error=0; switch (ireq->i_type) {\n' + arm(setter, 'IEEE80211_IOC_ROAMING') +
                  arm(setter, 'IEEE80211_IOC_SCAN_REQ') + '\n default: return EINVAL;\n}\n' +
                  setter[setter.rindex('\tif (error == ENETRESET && !IS_UP_AUTO(ic))'):]]
    generated += ['static int ' + function(net, 'rtw88_net_ioctl')]
    for name in ('bsd_set80211', 'set80211param', 'bsd_set_ssid', 'bsd_get_if_media',
                 'bsd_set_if_media', 'bsd_set_mediaopt', 'wpa_driver_bsd_scan'):
        generated += ['static int ' + function(bsd, name)]
    (work / 'actual.inc').write_text('\n\n'.join(generated) + '\n')
    compiler = shlex.split(os.environ.get('CC', 'gcc-16'))
    flags = shlex.split(os.environ.get('MANUAL_SCAN_CFLAGS', ''))
    command = compiler + ['-std=gnu17', '-O1', '-g', '-Wall', '-Wextra', '-Werror'] + flags + [
        '-I' + str(work), str(Path(__file__).with_suffix('.c')), '-o', str(work / 'test')]
    if os.environ.get('MANUAL_SCAN_ANALYZE') == '1':
        subprocess.run(command[:-2] + ['-fanalyzer', '-fsyntax-only'], check=True)
        subprocess.run(['cppcheck', '--enable=warning,performance,portability',
                        '--check-level=exhaustive', '--error-exitcode=1',
                        '-U__NetBSD__', '-UIEEE80211_IOC_STA_MGMT',
                        '-UIEEE80211_IOC_SCAN_MAX_SSID',
                        '-I' + str(work), str(Path(__file__).with_suffix('.c'))], check=True)
        subprocess.run([os.environ.get('CLANG', 'clang'), '--analyze', '-std=gnu17',
                        '-Wall', '-Wextra', '-Werror', '-Xanalyzer', '-analyzer-werror',
                        '-I' + str(work), str(Path(__file__).with_suffix('.c')),
                        '-o', str(work / 'analysis.plist')], check=True)
    subprocess.run(command, check=True)
    subprocess.run(shlex.split(os.environ.get('TEST_RUNNER', '')) + [str(work / 'test')], check=True)
