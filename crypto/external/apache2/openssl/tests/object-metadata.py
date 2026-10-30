#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Exercise the real libapps obj metadata query before OpenSSL generation."""
import argparse
import os
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--bmake', required=True, type=Path)
p.add_argument('--openssl-source', required=True, type=Path)
a = p.parse_args()
source = Path(__file__).resolve().parents[1] / 'lib/libapps'
env = os.environ.copy()
env['EMBER_OPENSSL_SOURCE'] = str(a.openssl_source.resolve(strict=True))
env.pop('EMBER_OPENSSL_GENERATED', None)
command = [str(a.bmake.resolve(strict=True)), '-C', str(source)]
result = subprocess.run([*command, '-V', 'SUBDIR', '-V', '.MAKE.MAKEFILES', 'obj'],
                        env=env, text=True, capture_output=True)
if result.returncode or 'Makefile.openssl' not in result.stdout:
    raise SystemExit('obj metadata failed: ' + result.stderr)
for goals in (['all'], ['obj', 'all']):
    result = subprocess.run([*command, '-n', *goals], env=env, text=True, capture_output=True)
    if not result.returncode or 'EMBER_OPENSSL_GENERATED must name' not in result.stderr:
        raise SystemExit('generated path missing was not rejected for ' + str(goals))
print('PASS: obj metadata before generation; build and mixed goals require generated input')
