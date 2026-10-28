#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
"""Check the native nvi producer's prepared-source routing with real bmake."""
import argparse
import os
import subprocess
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--make', required=True)
p.add_argument('--source', required=True)
p.add_argument('--objroot', required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
producer = root / 'external/bsd/nvi/usr.bin/nvi'
common = [a.make, '-C', str(producer), 'MAKEOBJDIRPREFIX=' + a.objroot]
env = dict(os.environ)
env.pop('EMBER_NVI_SOURCE', None)

def run(*args):
    return subprocess.run(common + list(args), env=env, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

selected = run('EMBER_NVI_SOURCE=' + a.source, '-V', '${DIST}')
assert selected.returncode == 0 and selected.stdout.strip() == a.source, selected.stdout
objects = run('obj')
assert objects.returncode == 0, objects.stdout
headers = run('includes')
assert headers.returncode == 0, headers.stdout
missing = run('dependall')
assert missing.returncode != 0 and 'Set EMBER_NVI_SOURCE' in missing.stdout, missing.stdout
print('nvi prepared-source routing: PASS')
