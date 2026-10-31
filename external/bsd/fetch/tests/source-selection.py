#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Check base libfetch source selection without building or installing."""
import argparse
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--bmake', type=Path, required=True)
parser.add_argument('--source', type=Path, required=True)
args = parser.parse_args()
lib = Path(__file__).resolve().parents[1] / 'lib'
make = [str(args.bmake.resolve(strict=True)), '-C', str(lib)]
env = os.environ.copy()
env.pop('EMBER_LIBFETCH_SOURCE', None)
query = ['-V', '${LIBFETCHDIR}', 'obj']
r = subprocess.run([*make, *query], env=env, text=True, capture_output=True)
if r.returncode:
    raise SystemExit('obj metadata without source failed: ' + r.stderr)
r = subprocess.run([*make, '-n', 'all'], env=env, text=True, capture_output=True)
if not r.returncode or 'Set EMBER_LIBFETCH_SOURCE' not in r.stderr:
    raise SystemExit('missing source was not rejected for build')
# The build.sh wrapper may export its own values, so use make's explicit
# command-line variable assignment for the selected source.
source = str(args.source.resolve(strict=True))
r = subprocess.run([*make, 'EMBER_LIBFETCH_SOURCE=' + source, *query],
                   env=env, text=True, capture_output=True)
if r.returncode or r.stdout.strip() != source:
    raise SystemExit('selected source mismatch: ' + r.stdout + r.stderr)
print('PASS: libfetch source selection and missing-source guard')
