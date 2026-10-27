#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
"""Check OS libevent source selection, target objects and expected exports.

This compiles selected AArch64 target objects; it does not run them or link the
OS libraries. Run with the prepared common source and selected SDK tools.
"""
import argparse
import re
from pathlib import Path
import subprocess
import sys
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
for name in ('source', 'bmake', 'clang', 'nm', 'sysroot', 'work-dir'):
    parser.add_argument('--' + name, type=Path, required=True)
args = parser.parse_args()
osroot = Path(__file__).resolve().parents[2]
lib = osroot / 'external/bsd/libevent'
base = [str(args.bmake), '-m', str(osroot / 'share/mk'), '-C', str(lib / 'lib/libevent'),
        '-V', '${LIBEVENT_DIST}']
missing = subprocess.run(base, capture_output=True, text=True)
assert missing.returncode != 0 and 'EMBER_LIBEVENT_SOURCE' in missing.stderr
selected = subprocess.run([*base, 'EMBER_LIBEVENT_SOURCE=' + str(args.source)],
                          capture_output=True, text=True, check=True)
assert selected.stdout.strip() == str(args.source)
config = lib / 'include'
configure = (args.source / 'configure.ac').read_text()
versions = re.findall(r'^AC_INIT\(libevent,\s*([0-9]+)\.([0-9]+)\.([0-9]+)-stable\)', configure, re.M)
assert len(versions) == 1, 'prepared source has an unsupported or ambiguous version'
major, minor, patch = map(int, versions[0])
assert all(0 <= part <= 255 for part in (major, minor, patch))
expected_version = f'{major}.{minor}.{patch}-stable'
expected_number = (major << 24) | (minor << 16) | (patch << 8)
for header, prefix in ((config / 'config.h', ''), (config / 'event2/event-config.h', 'EVENT__')):
    text = header.read_text()
    for name in ('VERSION', 'PACKAGE_VERSION'):
        assert re.search(r'^#define ' + prefix + name + r' "' + re.escape(expected_version) + r'"$', text, re.M), (header, name)
    numeric = re.search(r'^#define ' + prefix + r'NUMERIC_VERSION (0x[0-9a-fA-F]+)$', text, re.M)
    assert numeric and int(numeric.group(1), 16) == expected_number, header
core = '''buffer bufferevent bufferevent_filter bufferevent_pair bufferevent_ratelim
bufferevent_sock evdns event event_tagging evmap evrpc evthread evutil evutil_rand
 evutil_time http kqueue listener log poll select signal'''.split()
groups = {'libevent': core, 'libevent_openssl': ['bufferevent_openssl'],
          'libevent_pthreads': ['evthread_pthread']}
flags = [str(args.clang), '--target=aarch64--netbsd', '--sysroot=' + str(args.sysroot),
         '-std=gnu11', '-O2', '-fPIC', '-DHAVE_CONFIG_H', '-I' + str(args.source),
         '-I' + str(config), '-I' + str(args.source / 'include'), '-Wall',
         '-Wstrict-prototypes', '-Wmissing-prototypes',
         # The Clang WARNS=4 options of the OS libevent build (bsd.sys.mk).
         '-Wextra', '-Wno-unused-parameter', '-Wno-sign-compare', '-Wno-pointer-sign',
         '-Wpointer-arith', '-Wreturn-type', '-Wswitch', '-Wshadow', '-Wcast-qual',
         '-Wwrite-strings', '-Wformat=2', '-Wmissing-noreturn', '-Werror']
with tempfile.TemporaryDirectory(dir=args.work_dir) as temporary:
    work = Path(temporary)
    version = work / 'version.c'
    version.write_text('#include <event2/event.h>\n'
                       f'#if LIBEVENT_VERSION_NUMBER != 0x{expected_number:08x}\n'
                       '#error wrong version\n#endif\n')
    subprocess.run([*flags, '-c', str(version), '-o', str(work / 'version.o')], check=True)
    for group, names in groups.items():
        objects = []
        for name in names:
            obj = work / (name + '.o')
            # This existing OS-wide libevent policy is unrelated to the two
            # newly removed source-specific warning demotions.
            extra = ['-Wno-error=unused-function'] if name == 'event' else []
            result = subprocess.run([*flags, *extra, '-c', str(args.source / (name + '.c')),
                                     '-o', str(obj)], check=True, capture_output=True)
            sys.stderr.buffer.write(result.stderr)
            objects.append(obj)
        nm = subprocess.run([str(args.nm), '--extern-only', '--defined-only',
                             *map(str, objects)], capture_output=True, text=True, check=True)
        actual = {line.split()[-1] for line in nm.stdout.splitlines() if len(line.split()) == 3}
        expsym = lib / 'lib' / group / (group.removeprefix('lib') + '.expsym')
        expected = {line.split()[0] for line in expsym.read_text().splitlines()
                    if line.strip() and not line.lstrip().startswith('#')}
        # The OS build compares the exports with the list exactly.
        absent, extra = sorted(expected - actual), sorted(actual - expected)
        if absent or extra:
            raise AssertionError(f'{group} missing exports: {absent}; extra exports: {extra}')
print('libevent prepared source target object and export checks: PASS')
