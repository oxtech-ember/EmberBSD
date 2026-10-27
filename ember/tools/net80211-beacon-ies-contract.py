#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD bounded beacon contracts and native-host header guards.
"""Build bounded beacon contracts. No kernel, firmware or radio is executed."""
import argparse
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tempfile


def function(text, name, result):
    match = re.search(r'^' + re.escape(name) + r'\(', text, re.M)
    if not match:
        raise ValueError(name)
    start = match.start()
    masked = re.sub(r'/\*.*?\*/|"(?:\\.|[^"\\])*"', lambda m: ' ' * len(m.group()), text, flags=re.S)
    opening = masked.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (masked[end] == '{') - (masked[end] == '}')
        end += 1
    return result + '\n' + text[start:end] + '\n'



def structure(text, name):
    match = re.search(r'^struct ' + name + r' \{.*?^}[^;]*;', text, re.M | re.S)
    if not match:
        raise ValueError(name)
    return match.group() + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work-dir', type=Path, required=True,
                        help='existing shared object directory; a private child is removed on exit')
    parser.add_argument('--baseline-revision', help='run the bwfm regression on this Git revision')
    parser.add_argument('--cc', default=os.environ.get('CC'), required=not os.environ.get('CC'))
    parser.add_argument('--cflags', default='', help='additional host diagnostic flags')
    parser.add_argument('--analyze', action='store_true')
    args = parser.parse_args()
    src = Path(__file__).resolve().parents[2]
    env = os.environ.copy()
    work = Path(tempfile.mkdtemp(prefix='net80211-beacon-', dir=args.work_dir))
    try:
        (work / 'sys').mkdir()
        (work / 'net80211').mkdir()
        for header in ['ieee80211_beacon_ies.h', 'ieee80211_phy_ies.h']:
            shutil.copyfile(src / 'sys/net80211' / header, work / 'net80211' / header)
        (work / 'sys/systm.h').write_text('#include <string.h>\n#include <stdint.h>\n')
        wire = (src / 'sys/net80211/ieee80211.h').read_text()
        node = (src / 'sys/net80211/ieee80211_node.h').read_text()
        reg = (src / 'sys/dev/ic/bwfmreg.h').read_text()
        layouts = '#define IEEE80211_ADDR_LEN 6\n#ifndef __packed\n#define __packed __attribute__((packed))\n#endif\n'
        for text, pattern in ((wire, r'IEEE80211_ELEMID_\w+'), (reg, r'BWFM_(?:MAX_SSID_LEN|MCSSET_LEN)')):
            layouts += '\n'.join(re.findall(r'^#define\s+' + pattern + r'\s+[^\n]*', text, re.M)) + '\n'
        layouts += re.search(r'enum \{\s*IEEE80211_ELEMID_SSID.*?^};', wire, re.M | re.S).group() + '\n'
        layouts += structure(wire, 'ieee80211_frame')
        layouts += structure(node, 'ieee80211_scanparams')
        layouts += structure(reg, 'bwfm_bss_info')
        (work / 'bwfm-layouts.h').write_text(layouts)
        bwfm = subprocess.check_output(['git', 'show', args.baseline_revision + ':sys/dev/ic/bwfm.c'], cwd=src, text=True) if args.baseline_revision else (src / 'sys/dev/ic/bwfm.c').read_text()
        body = bwfm[:bwfm.index('#include')]
        body += function(bwfm, 'bwfm_iswpaoui', 'static inline int')
        body += function(bwfm, 'bwfm_scan_node', 'void')
        (work / 'bwfm-scan.inc').write_text(body)
        common = [str(src / 'sys/net80211/ieee80211_beacon_ies.c'), str(src / 'sys/net80211/ieee80211_phy_ies.c')]
        flags = ['-std=c11', '-D_DARWIN_C_SOURCE', '-D_DEFAULT_SOURCE', '-Wall', '-Wextra', '-Werror', '-I' + str(work)]
        flags += shlex.split(args.cflags)
        if args.baseline_revision:
            # The old adapter has a redundant uint8_t >= 256 guard. Report it.
            flags += ["-Wno-error=type-limits"]
        targets = ['net80211-beacon-bwfm-contract'] if args.baseline_revision else ['net80211-beacon-ies-contract', 'net80211-beacon-bwfm-contract']
        for target in targets:
            source = str(src / 'ember/tools' / (target + '.c'))
            command = [args.cc, *flags, source, *common, '-o', str(work / target)]
            print(shlex.join(command), flush=True)
            subprocess.run(command, check=True, env=env)
            runner = [env['TEST_RUNNER']] if env.get('TEST_RUNNER') else []
            subprocess.run([*runner, str(work / target)], check=True, env=env)
            if args.analyze:
                subprocess.run([args.cc, *flags, "-fanalyzer", "-c", source, "-o", str(work / (target + ".o"))], check=True, env=env)
        if args.analyze:
            for source in common:
                subprocess.run([args.cc, *flags, '-fanalyzer', '-c', source, '-o', str(work / (Path(source).stem + '.o'))], check=True, env=env)
            subprocess.run(['cppcheck', '--enable=warning,style,performance,portability', '--error-exitcode=1', '--check-level=exhaustive', '--std=c11', '--platform=unix64', '-I' + str(work), common[0], str(src / 'ember/tools/net80211-beacon-ies-contract.c'), str(src / 'ember/tools/net80211-beacon-bwfm-contract.c')], check=True, env=env)
    finally:
        shutil.rmtree(work)


if __name__ == '__main__':
    main()
