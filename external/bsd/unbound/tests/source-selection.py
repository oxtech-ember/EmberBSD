#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Check common Unbound source selection without building or installing."""
import argparse
import os
from pathlib import Path
import re
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--bmake', type=Path, required=True)
p.add_argument('--source', type=Path, required=True)
a = p.parse_args()
lib = Path(__file__).resolve().parents[1] / 'lib/libunbound'
make = [str(a.bmake.resolve(strict=True)), '-C', str(lib)]
env = os.environ.copy()
env.pop('EMBER_UNBOUND_SOURCE', None)
r = subprocess.run(make + ['-V', '${UNBOUND_VERSION}', 'obj'], env=env, text=True, capture_output=True)
if r.returncode:
    raise SystemExit('obj metadata failed without source: ' + r.stderr)
r = subprocess.run(make + ['-n', 'all'], env=env, text=True, capture_output=True)
if not r.returncode or 'Set EMBER_UNBOUND_SOURCE' not in r.stderr:
    raise SystemExit('missing source was not rejected')
source = str(a.source.resolve(strict=True))
r = subprocess.run(make + ['EMBER_UNBOUND_SOURCE=' + source, '-V', '${UNBOUND_VERSION}', 'all'],
                   env=env, text=True, capture_output=True)
version = re.search(r"^PACKAGE_VERSION='([^']+)'$", (Path(source) / 'configure').read_text(), re.M)
if not version or r.returncode or r.stdout.strip() != version.group(1):
    raise SystemExit('selected source version mismatch: ' + r.stdout + r.stderr)
r = subprocess.run(make + ['EMBER_UNBOUND_SOURCE=/nonexistent/unbound', '-V', '${UNBOUND_VERSION}', 'obj'],
                   env=env, text=True, capture_output=True)
if r.returncode or r.stdout.strip() != '0.0.0':
    raise SystemExit('obj metadata tried to inspect source: ' + r.stdout + r.stderr)
print('PASS: Unbound source and version selection')
