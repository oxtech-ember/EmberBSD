#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Check the native glue selects libcbor's target byte order."""
import argparse
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--clang', required=True, type=Path)
a = p.parse_args()
header = Path(__file__).resolve().parents[1] / 'lib/byte-order.h'
for triple, big in (('aarch64--netbsd', False), ('aarch64_be--netbsd', True)):
    result = subprocess.run([str(a.clang.resolve(strict=True)), '--target=' + triple,
                             '-include', str(header), '-dM', '-E', '-x', 'c', '/dev/null'],
                            text=True, capture_output=True, check=True)
    selected = any(line.startswith('#define IS_BIG_ENDIAN') for line in result.stdout.splitlines())
    if selected != big:
        raise SystemExit('wrong byte order for ' + triple)
print('PASS: target preprocessor selects little and big endian')
