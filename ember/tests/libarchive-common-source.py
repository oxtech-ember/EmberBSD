#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
"""Check selected libarchive source and native configuration version.

The AArch64 compiler and preprocessor check is syntax-only; it does not emit
an object, execute target code, or qualify the target library.
"""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
for name in ('source', 'bmake', 'clang', 'sysroot', 'work-dir'):
    parser.add_argument('--' + name, type=Path, required=True)
args = parser.parse_args()
for path in vars(args).values():
    if not path.is_absolute() or not path.exists():
        parser.error('all paths must exist and be absolute')
osroot = Path(__file__).resolve().parents[2]
lib = osroot / 'external/bsd/libarchive'
make = [str(args.bmake), '-m', str(osroot / 'share/mk'), '-C', str(lib / 'lib/libarchive'),
        'NETBSDSRCDIR=' + str(osroot), '-V', '${LIBARCHIVEDIR}']
for value in ('', 'relative/path', '/nonexistent/ember-libarchive-source',
              '/unsafe path', '/unsafe;path'):
    result = subprocess.run([*make, 'EMBER_LIBARCHIVE_SOURCE=' + value], capture_output=True, text=True)
    assert result.returncode != 0 and 'EMBER_LIBARCHIVE_SOURCE' in result.stderr, value
result = subprocess.run([*make, 'EMBER_LIBARCHIVE_SOURCE=' + str(args.source)],
                        capture_output=True, text=True, check=True)
assert result.stdout.strip() == str(args.source)
configure = (args.source / 'configure.ac').read_text()
strings = re.findall(r'^m4_define\(\[LIBARCHIVE_VERSION_S\],\[([0-9]+\.[0-9]+\.[0-9]+)\]\)$', configure, re.M)
numbers = re.findall(r'^m4_define\(\[LIBARCHIVE_VERSION_N\],\[([0-9]+)\]\)$', configure, re.M)
assert len(strings) == len(numbers) == 1, 'ambiguous prepared libarchive version'
version, number = strings[0], int(numbers[0])
major, minor, patch = map(int, version.split('.'))
assert number == major * 1000000 + minor * 1000 + patch
header = (args.source / 'libarchive/archive.h').read_text()
assert re.search(r'^#define\s+ARCHIVE_VERSION_NUMBER\s+' + str(number) + r'$', header, re.M)
assert re.search(r'^#define\s+ARCHIVE_VERSION_ONLY_STRING\s+"' + re.escape(version) + r'"$', header, re.M)
config = lib / 'include/config_netbsd.h'
macros = {'BSDCAT_VERSION_STRING': version, 'BSDCPIO_VERSION_STRING': version,
          'BSDTAR_VERSION_STRING': version, 'BSDUNZIP_VERSION_STRING': version,
          'LIBARCHIVE_VERSION_NUMBER': str(number), 'LIBARCHIVE_VERSION_STRING': version,
          'PACKAGE_STRING': 'libarchive ' + version, 'PACKAGE_VERSION': version, 'VERSION': version}
source = '#include "config_netbsd.h"\n#include <archive.h>\n'
source += f'#if ARCHIVE_VERSION_NUMBER != {number}\n#error wrong source version\n#endif\n'
for name, value in macros.items():
    source += f'_Static_assert(__builtin_strcmp({name}, "{value}") == 0, "{name}");\n'
with tempfile.TemporaryDirectory(prefix='libarchive-version-', dir=args.work_dir) as temp:
    temp = Path(temp)
    test = temp / 'version.c'
    test.write_text(source)
    flags = [str(args.clang), '--target=aarch64--netbsd', '--sysroot=' + str(args.sysroot),
             '-std=c11', '-Wall', '-Werror', '-I' + str(lib / 'include'),
             '-I' + str(args.source / 'libarchive'), '-fsyntax-only', str(test)]
    subprocess.run(flags, check=True)
    # Prove the same compiler rejects stale native metadata.
    broken = temp / 'include'
    broken.mkdir()
    (broken / 'config_netbsd.h').write_text(config.read_text().replace('"' + version + '"', '"0.0.0"', 1))
    flags[flags.index('-I' + str(lib / 'include'))] = '-I' + str(broken)
    result = subprocess.run(flags, capture_output=True, text=True)
    assert result.returncode != 0 and 'static assertion' in result.stderr
print('libarchive selected source and target version consistency: PASS')
