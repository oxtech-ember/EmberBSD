#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
"""Check the OS Expat source selector and version using selected tools."""
import argparse
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
for name in ('make', 'cc', 'sed', 'os', 'source', 'sysroot'):
    p.add_argument('--' + name, required=True, type=Path)
p.add_argument('--expected-version', required=True)
p.add_argument('--target', required=True)
p.add_argument('--mk-dir', required=True, type=Path)
a = p.parse_args()
fragment = a.os / 'external/mit/expat/Makefile.inc'
base = [str(a.make), '-m', str(a.mk_dir), '-f', str(fragment),
        '-V', '${EXPATSRCDIR}', 'NETBSDSRCDIR=' + str(a.os),
        'EXPATOBJDIR.expat=/tmp']
assert subprocess.run(base, capture_output=True).returncode != 0
r = subprocess.run(base + ['EMBER_EXPAT_SOURCE=' + str(a.source)],
                   check=True, capture_output=True, text=True)
assert r.stdout.strip() == str(a.source)
cmd = [str(a.cc), '--target=' + a.target, '--sysroot=' + str(a.sysroot),
       '-E', '-I' + str(a.source / 'lib'), '-x', 'c', '-']
r = subprocess.run(cmd, input='#include <expat.h>\nXML_MAJOR_VERSION.XML_MINOR_VERSION.XML_MICRO_VERSION\n',
                   check=True, capture_output=True, text=True)
version = ''.join(r.stdout.splitlines()[-1].split())
assert version == a.expected_version, version
print('expat prepared source routing and version: PASS')

# Compile conditional declarations with the actual NetBSD config preceding expat.h.
config = a.os / 'external/mit/expat/lib/libexpat'
program = '''#include <expat_config.h>
void *api[] = { (void *)&XML_SetBillionLaughsAttackProtectionMaximumAmplification,
                (void *)&XML_SetAllocTrackerMaximumAmplification };\n'''
subprocess.run([str(a.cc), '--target=' + a.target, '--sysroot=' + str(a.sysroot),
                '-fsyntax-only', '-I' + str(config), '-I' + str(a.source / 'lib'),
                '-x', 'c', '-'], input=program, check=True, text=True)

# Exercise the real expat.pc target, including dependency-based regeneration.
import os
import re
import shutil
import tempfile
with tempfile.TemporaryDirectory(prefix='expat-pc-') as tmp:
    root = Path(tmp)
    expat = root / 'external/mit/expat'
    (expat / 'lib/libexpat').mkdir(parents=True)
    for rel in ('Makefile.inc', 'lib/Makefile.inc', 'lib/libexpat/Makefile',
                'lib/libexpat/expat.pc.in', 'lib/libexpat/expat_config.h'):
        shutil.copy2(a.os / 'external/mit/expat' / rel, expat / rel)
    source = root / 'source/lib'
    source.mkdir(parents=True)
    for name in ('expat.h', 'expat_external.h'):
        shutil.copy2(a.source / 'lib' / name, source / name)
    work = expat / 'lib/libexpat'
    cc = str(a.cc) + ' --target=' + a.target + ' --sysroot=' + str(a.sysroot)
    cmd = [str(a.make), '-m', str(a.mk_dir), '-C', str(work), 'expat.pc',
           'NETBSDSRCDIR=' + str(root), 'EMBER_EXPAT_SOURCE=' + str(root / 'source'),
           'EXPATOBJDIR.expat=' + str(root), 'CC=' + cc, 'TOOL_SED=' + str(a.sed)]
    subprocess.run(cmd, check=True, capture_output=True)
    pc = work / 'expat.pc'
    assert 'Version: ' + a.expected_version in pc.read_text()
    stamp = pc.stat().st_mtime_ns
    subprocess.run(cmd, check=True, capture_output=True)
    assert pc.stat().st_mtime_ns == stamp
    header = source / 'expat.h'
    content = re.sub(r'(#\s*define\s+XML_MICRO_VERSION\s+)\d+', r'\g<1>999', header.read_text())
    assert content != header.read_text()
    header.write_text(content)
    os.utime(header, ns=(stamp + 2_000_000_000, stamp + 2_000_000_000))
    subprocess.run(cmd, check=True, capture_output=True)
    assert 'Version: ' + '.'.join(a.expected_version.split('.')[:2] + ['999']) in pc.read_text()
print('expat conditional API and real pkgconfig regeneration: PASS')
