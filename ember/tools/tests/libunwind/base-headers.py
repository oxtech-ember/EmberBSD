#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Compile the base unwinder without a target C++ standard library."""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cxx', required=True, type=Path)
parser.add_argument('--sysroot', required=True, type=Path)
parser.add_argument('--source-root', type=Path, default=Path(__file__).resolve().parents[4])
args = parser.parse_args()
root = args.source_root.resolve(strict=True)
cxx = args.cxx.resolve(strict=True)
version = subprocess.check_output([str(cxx), '-dumpfullversion'], text=True).strip()
assert version.split('.')[0] == '16', 'Select GCC16'
subprocess.run([str(cxx), '--sysroot=' + str(args.sysroot.resolve(strict=True)),
                '-std=c++11', '-fno-exceptions', '-fno-rtti', '-nostdinc++',
                '-D_LIBC', '-D_LIBC_INTERNAL', '-D_REENTRANT',
                '-I' + str(root / 'lib/libc/include'),
                '-I' + str(root / 'sys'),
                '-I' + str(root / 'sys/lib/libunwind'),
                '-fsyntax-only', str(root / 'sys/lib/libunwind/libunwind.cxx')], check=True)
print('PASS base libunwind compiles with C headers and no C++ standard library')
