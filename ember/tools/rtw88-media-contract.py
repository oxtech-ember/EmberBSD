#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Complete native media consumers; explicit models cover kernel services/layout.
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

src = Path(os.environ.get('RTW88_MEDIA_SOURCE', Path(__file__).resolve().parents[2]))
tools = Path(__file__).resolve().parent

def read(path):
    return (src / path).read_text()

def function(source, name, result):
    m = re.search(r'^' + name + r'\(.*?^}', source, re.M | re.S)
    if not m:
        raise ValueError(name)
    return result + '\n' + m.group() + '\n'

def defines(source, prefix):
    # Join C logical lines before selection. Do not join unrelated directives.
    clean = re.sub(r'/\*.*?\*/', '', source, flags=re.S)
    clean = clean.replace('\\\n', '')
    return source[:source.index('#ifndef')] + '\n'.join(line for line in clean.splitlines()
                     if re.match(r'^#define\s+' + prefix, line)
                     and (prefix != 'IEEE80211_' or
                          re.match(r'^#define\s+\w+\s+', line)))

with tempfile.TemporaryDirectory(prefix='rtw88-media-') as directory:
    work = Path(directory)
    ieee = read('sys/net80211/ieee80211.c')
    proto = read('sys/net80211/ieee80211_proto.c')
    net = read('sys/dev/ic/rtw88_net.c')
    types = read('sys/dev/ic/rtw88_chip.h') + '\n'
    types += read('sys/net80211/_ieee80211.h') + '\n'
    types += defines(read('sys/net/if_media.h'), '_?IFM_') + '\n'
    types += defines(read('sys/net80211/ieee80211_var.h'), 'IEEE80211_') + '\n'
    types += defines(read('sys/net80211/ieee80211.h'), 'IEEE80211_RATE_') + '\n'
    types += re.search(r'enum ieee80211_state\s*\{.*?};',
                       read('sys/net80211/ieee80211_proto.h'), re.S).group() + '\n'
    types += defines(read('sys/net80211/ieee80211_ioctl.h'),
                     'IEEE80211_IOC_SCAN_REQ') + '\n'

    (work / 'media-types.inc').write_text(types)
    names = [('ieee80211_chan2ieee','unsigned'),
             ('ieee80211_ieee2mhz','unsigned'),
             ('ieee80211_set11gbasicrates','void'),
             ('ieee80211_set_shortslottime','void'),
             ('ieee80211_reset_erp','void'),
             ('ieee80211_setmode','int'),('ieee80211_ifattach','void'),
             ('ieee80211_rate2media','int'),('ieee80211_media2rate','int'),
             ('ieee80211_media_init_with_lock','void'),
             ('ieee80211_media_init','void'),('findrate','static int'),
             ('ieee80211_media_change','int')]
    bodies = [ieee[:ieee.index('#include')],proto[:proto.index('#include')]]
    for name in ('11a','11b','11g'):
        bodies.append(re.search(r'^const struct ieee80211_rateset ieee80211_std_rateset_'+name+r'\s*=.*?;',ieee,re.M|re.S).group())
    for name, result in names:
        body = function(proto if name in ('ieee80211_set11gbasicrates',
                                   'ieee80211_set_shortslottime',
                                   'ieee80211_reset_erp') else ieee,name,result)
        if name in ('ieee80211_ifattach', 'ieee80211_media2rate'):
            # Existing native int/u_int comparisons; do not rewrite their body.
            body = ('#pragma GCC diagnostic push\n'
                    '#pragma GCC diagnostic ignored "-Wsign-compare"\n' + body +
                    '#pragma GCC diagnostic pop\n')
        bodies.append(body)
    (work / 'media-native.inc').write_text('\n'.join(bodies))
    ioctl = read('sys/net80211/ieee80211_ioctl.c')
    setter = function(ioctl, 'ieee80211_ioctl_set80211', 'int')
    scan = re.search(r'^\tcase IEEE80211_IOC_SCAN_REQ:.*?(?=^\tcase )',
                     setter, re.M | re.S).group()
    wrapper = [net[:net.index('#include')], ioctl[:ioctl.index('#include')],
               function(net, 'rtw88_net_media_change', 'static int'),
               function(ioctl, 'ieee80211_setupscan', 'static int'),
               'static int native_scan_req(struct ieee80211com *ic) {\n'
               'int error=0; switch (IEEE80211_IOC_SCAN_REQ) {\n' + scan +
               '\n} return error;\n}']
    (work / 'media-wrapper.inc').write_text('\n'.join(wrapper))
    # Execute the unchanged rate/channel declarations from the owning attach.
    start=net.index('\tic->ic_sup_rates[IEEE80211_MODE_11A] =')
    end=net.index('\tifp->if_softc = net;',start)
    array=re.search(r'static const unsigned channels5\[\].*?;',net,re.S).group()
    (work/'media-driver.inc').write_text(net[:net.index('#include')]+array+'\nunsigned i,ch;\n'+net[start:end])
    compiler=shlex.split(os.environ.get('CC','gcc-16'))
    flags=shlex.split(os.environ.get('RTW88_TEST_CFLAGS',''))
    if os.environ.get('RTW88_MEDIA_ANALYZE') == '1':
        analyzer = compiler + ['-std=gnu17', '-O0', '-g', '-Wall', '-Wextra',
                               '-Werror', '-fanalyzer', '-c', '-I' + str(work),
                               str(tools / 'rtw88-media-contract.c'),
                               '-o', str(work / 'gcc-analysis.o')]
        probe = work / 'analyzer-null.c'
        probe.write_text('void analyzer_causal_null(void) { int *p = 0; *p = 1; }\n')
        negative = [str(probe) if arg == str(tools / 'rtw88-media-contract.c')
                    else arg for arg in analyzer]
        result = subprocess.run(negative, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if result.returncode == 0 or not re.search(
                rb'\[-W(?:error=)?analyzer-null-dereference\]', result.stderr):
            raise RuntimeError('GCC analyzer did not reject the causal NULL dereference')
        print('GCC analyzer causal NULL gate: rejected with analyzer-null-dereference', flush=True)
        print(result.stderr.decode('utf-8', 'replace'), flush=True)
        subprocess.run(analyzer, check=True)
        subprocess.run(['cppcheck', '--enable=warning,performance,portability',
                        '--check-level=exhaustive', '--error-exitcode=1',
                        '-U__NetBSD__', '-I' + str(work),
                        str(tools / 'rtw88-media-contract.c')], check=True)
        subprocess.run([os.environ.get('CLANG', 'clang'), '--analyze',
                        '-std=gnu17', '-Wall', '-Wextra', '-Werror',
                        '-Xanalyzer', '-analyzer-werror', '-I' + str(work),
                        str(tools / 'rtw88-media-contract.c'),
                        '-o', str(work / 'analysis.plist')], check=True)
    subprocess.run(compiler+['-std=gnu17','-Wall','-Wextra','-Werror']+flags+[
        '-I'+str(work),str(tools/'rtw88-media-contract.c'),'-o',str(work/'test')],check=True)
    runner=shlex.split(os.environ.get('TEST_RUNNER',''))
    subprocess.run(runner+[str(work/'test')],check=True)
