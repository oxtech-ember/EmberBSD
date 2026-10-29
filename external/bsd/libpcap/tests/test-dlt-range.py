#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD (AI-assisted).
"""Check the native DLT definitions match the selected stable libpcap."""
import argparse
from pathlib import Path
import re
import subprocess

p = argparse.ArgumentParser(description=__doc__)
for name in ('clang', 'sysroot', 'source'):
    p.add_argument('--' + name, required=True, type=Path)
a = p.parse_args()
root = Path(__file__).resolve().parents[4]
base = [str(a.clang), '--target=aarch64--netbsd', '--sysroot=' + str(a.sysroot)]

def macros(header):
    result = subprocess.run([*base, '-E', '-dM', '-x', 'c', '-include', str(header), '-'],
                            input='', capture_output=True, text=True, check=True)
    return {m[1]: m[2] for line in result.stdout.splitlines()
            if (m := re.fullmatch(r'#define (DLT_[A-Z0-9_]+) (.+)', line))}

native = root / 'sys/net/dlt.h'
stable = a.source / 'pcap/dlt.h'
x, y = macros(native), macros(stable)
assert x['DLT_HIGH_MATCHING_MAX'] == y['DLT_HIGH_MATCHING_MAX'] == '303'
assert x['DLT_DECT_NR_TAP'] == '304'
common = x.keys() & y.keys()
assert common and all(x[k] == y[k] for k in common)
program = '#include <net/dlt.h>\n_Static_assert(DLT_HIGH_MATCHING_MAX == 303, "stable range");\n_Static_assert(DLT_DECT_NR_TAP == 304, "reserved ID");\n'
subprocess.run([*base, '-fsyntax-only', '-x', 'c', '-'], input=program, text=True, check=True)
print(f'DLT stable range and {len(common)} shared definitions: PASS')
