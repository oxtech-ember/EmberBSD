#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Check base WPA source selection without building or installing."""
import argparse
import os
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--bmake', type=Path, required=True)
p.add_argument('--source', type=Path, required=True)
a = p.parse_args()
base = Path(__file__).resolve().parents[1] / 'bin'
source = str(a.source.resolve(strict=True))
env = os.environ.copy()
for name in ('EMBER_WPA_SOURCE', 'WPA_DISTDIR', 'WPA_SUPPLICANT_DISTDIR', 'HOSTAPD_DISTDIR'):
    env.pop(name, None)
for program in ('hostapd', 'hostapd_cli', 'wpa_cli', 'wpa_passphrase', 'wpa_supplicant'):
    make = [str(a.bmake.resolve(strict=True)), '-C', str(base / program)]
    def run(*args):
        return subprocess.run(make + list(args), env=env, text=True, capture_output=True)
    r = run('-V', '${WPA_DISTDIR}', 'obj')
    if r.returncode:
        raise SystemExit(program + ': obj metadata failed: ' + r.stderr)
    r = run('-n', 'all')
    if not r.returncode or 'Set EMBER_WPA_SOURCE' not in r.stderr:
        raise SystemExit(program + ': missing source was not rejected')
    r = run('EMBER_WPA_SOURCE=' + source, '-V', '${WPA_DISTDIR}', 'all')
    if r.returncode or r.stdout.strip() != source:
        raise SystemExit(program + ': selected source mismatch: ' + r.stdout + r.stderr)
    r = run('EMBER_WPA_SOURCE=' + source, 'WPA_DISTDIR=/unrelated', '-n', 'all')
    if not r.returncode or 'must match' not in r.stderr:
        raise SystemExit(program + ': conflicting source override was accepted')
print('PASS: common WPA source selection and conflict guards')
