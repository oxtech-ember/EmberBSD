#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
"""Check OS zlib source routing and the bounded SDK refresh plan."""
import argparse
from pathlib import Path
import subprocess
import re
import sys
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--make', required=True)
parser.add_argument('--cc', required=True)
parser.add_argument('--sysroot', required=True, type=Path)
parser.add_argument('--sdk-source', required=True, type=Path)
parser.add_argument('--zlib-source', required=True, type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
source = args.zlib_source.resolve(strict=True)
sys.path.insert(0, str(args.sdk_source.resolve(strict=True) / 'src'))
from emberbsd_sdk import prepared_refresh


def query(directory, assignments, *variables, success=True):
    cmd = [args.make, '-C', str(root / directory), *assignments]
    for variable in variables:
        cmd.extend(('-V', '${' + variable + '}'))
    result = subprocess.run(cmd, text=True, capture_output=True)
    assert (result.returncode == 0) == success, result.stdout + result.stderr
    return result.stdout + result.stderr

setting = 'EMBER_ZLIB_SOURCE=' + str(source)
assert str(source) in query('lib/libz', [setting], 'ZLIBDIR')
assert query('lib/libz', [setting], 'SHLIB_MAJOR', 'SHLIB_MINOR').splitlines() == ['1', '1']
assert str(source) in query('sys/lib/libz', [setting], 'ZDISTDIR')
assert str(source) in query('distrib/utils/zcat', [setting], 'SRCDIR')
query('sys/lib/libsa', ['SA_USE_CREAD=no', 'EMBER_ZLIB_SOURCE='], 'SRCS')
assert 'Set EMBER_ZLIB_SOURCE' in query('sys/lib/libsa', ['SA_USE_CREAD=yes', 'EMBER_ZLIB_SOURCE='], 'SRCS', success=False)
assert 'cread.c' in query('sys/lib/libsa', ['SA_USE_CREAD=yes', setting], 'SRCS')
assert str(source) in query('sys/lib/libsa', ['SA_USE_CREAD=yes', setting], 'CPPFLAGS')
query('sys/lib/libgnuefi', ['EMBER_ZLIB_SOURCE='], 'SRCS')

with tempfile.TemporaryDirectory(prefix='ember-zlib-routing-') as temp:
    obj = Path(temp)
    for name in ('lib/libz', 'sys/lib/libz', 'lib/unrelated'):
        p = obj / name
        p.mkdir(parents=True)
        (p / 'keep.o').write_bytes(b'object')
    selected = prepared_refresh.plan(root, obj, {'zlib'}, scan_consumers=False)
    assert selected == ['lib/libz'], selected
    assert all((obj / name / 'keep.o').read_bytes() == b'object'
               for name in ('lib/libz', 'sys/lib/libz', 'lib/unrelated'))
    # Run the real OS pc target in an isolated object directory, retaining the
    # selected wrapper's compiler and SDK context.
    wrapper = Path(args.make).read_text().splitlines()
    rows = [i for i, line in enumerate(wrapper) if line.startswith('MAKEOBJDIR=')]
    assert len(rows) == 1
    pcobj = obj / 'pc'
    pcobj.mkdir()
    wrapper[rows[0]] = 'MAKEOBJDIR=' + str(pcobj) + '; export MAKEOBJDIR'
    fixture_make = obj / 'nbmake'
    fixture_make.write_text('\n'.join(wrapper) + '\n')
    fixture_make.chmod(0o755)
    (obj / 'dest/usr/lib/pkgconfig').mkdir(parents=True)
    result = subprocess.run([str(fixture_make), '-C', str(root / 'lib/libz'), setting, 'MKUPDATE=yes', 'DESTDIR=' + str(obj / 'dest'), 'filesinstall'], text=True, capture_output=True)
    assert result.returncode == 0, result.stdout + result.stderr
    match = re.search(r'^#define ZLIB_VERSION "([^"]+)"', (source / 'zlib.h').read_text(), re.M)
    assert match is not None
    assert 'Version: ' + match.group(1) + '\n' in (obj / 'dest/usr/lib/pkgconfig/zlib.pc').read_text()

# The common sources must compile with the actual base-library warning policy.
names = ('adler32.c', 'compress.c', 'crc32.c', 'deflate.c', 'infback.c', 'inffast.c',
         'inflate.c', 'inftrees.c', 'trees.c', 'uncompr.c', 'zutil.c', 'gzwrite.c',
         'gzlib.c', 'gzread.c', 'gzclose.c')
cmd = [args.cc, '--target=x86_64--netbsd', '--sysroot=' + str(args.sysroot), '-I' + str(source),
       '-Wwrite-strings', '-Werror=cast-qual', '-fsyntax-only', *(str(source / name) for name in names)]
result = subprocess.run(cmd, text=True, capture_output=True)
assert result.returncode == 0, result.stdout + result.stderr
print('OS common zlib routing, refresh and compile PASS')
