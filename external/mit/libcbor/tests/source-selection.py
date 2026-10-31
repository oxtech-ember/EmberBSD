#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Check libcbor source selection without installing or building packages."""
import argparse
import os
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--bmake', type=Path, required=True)
p.add_argument('--source', type=Path, required=True)
a = p.parse_args()
lib = Path(__file__).resolve().parents[1] / 'lib'
make = [str(a.bmake.resolve(strict=True)), '-C', str(lib)]
env = os.environ.copy()
env.pop('EMBER_LIBCBOR_SOURCE', None)
query = ['-V', '${CBOR_VERSION}', '-V', '${SHLIB_MAJOR}.${SHLIB_MINOR}', 'obj']
r = subprocess.run([*make, *query], env=env, text=True, capture_output=True)
if r.returncode:
    raise SystemExit('obj metadata without source failed: ' + r.stderr)
r = subprocess.run([*make, '-n', 'all'], env=env, text=True, capture_output=True)
if not r.returncode or 'Set EMBER_LIBCBOR_SOURCE' not in r.stderr:
    raise SystemExit('missing source was not rejected for build')
env['EMBER_LIBCBOR_SOURCE'] = str(a.source.resolve(strict=True))
r = subprocess.run([*make, *query], env=env, text=True, capture_output=True)
if r.returncode or r.stdout.splitlines() != ['0.14.0', '1.0']:
    raise SystemExit('selected source version/ABI mismatch: ' + r.stdout + r.stderr)
print('PASS: selected libcbor source and ABI; obj without source, build rejects it')
