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
bsd_objdirs = []
for program in ('hostapd', 'hostapd_cli', 'wpa_cli', 'wpa_passphrase', 'wpa_supplicant'):
    make = [str(a.bmake.resolve(strict=True)), '-C', str(base / program)]
    def run(*args):
        return subprocess.run(make + list(args), env=env, text=True, capture_output=True)
    r = run('EMBER_WPA_SOURCE=' + source, 'obj')
    if r.returncode:
        raise SystemExit(program + ': object directory selection failed: ' + r.stderr)
    r = run('EMBER_WPA_SOURCE=' + source, '-V', '${WPA_DISTDIR}', 'all')
    if r.returncode:
        raise SystemExit(program + ': obj metadata failed: ' + r.stderr)
    r = run('-n', 'all')
    if not r.returncode or 'Set EMBER_WPA_SOURCE' not in r.stderr:
        raise SystemExit(program + ': missing source was not rejected')
    r = run('EMBER_WPA_SOURCE=' + source, '-V', '${WPA_DISTDIR}', 'all')
    if r.returncode or r.stdout.strip() != source:
        raise SystemExit(program + ': selected source mismatch: ' + r.stdout + r.stderr)
    if program in ('hostapd', 'wpa_supplicant'):
        r = run('EMBER_WPA_SOURCE=' + source, '-V', '${driver_bsd.c:P}',
                '-V', '.OBJDIR', 'driver_bsd.o')
        expected = (Path(source) / 'src/drivers/driver_bsd.c').resolve()
        selected, objdir = r.stdout.splitlines()[:2] if r.returncode == 0 else ('', '')
        if (not Path(selected).is_absolute() or Path(selected).resolve() != expected or
                not Path(objdir).is_absolute() or Path(objdir).resolve() == (base / program).resolve()):
            raise SystemExit(program + ': BSD adapter source or private object directory mismatch: ' +
                             r.stdout + r.stderr)
        bsd_objdirs.append(Path(objdir).resolve())
        r = run('EMBER_WPA_SOURCE=' + source, '-V', '${SRCS}', 'all')
        if r.returncode or 'driver_bsd.c' not in r.stdout.split() or 'driver_emberbsd.c' in r.stdout.split():
            raise SystemExit(program + ': BSD adapter must compile from the prepared source')
    r = run('EMBER_WPA_SOURCE=' + source, 'WPA_DISTDIR=/unrelated', '-n', 'all')
    if not r.returncode or 'must match' not in r.stderr:
        raise SystemExit(program + ': conflicting source override was accepted')
if len(bsd_objdirs) != 2 or len(set(bsd_objdirs)) != 2:
    raise SystemExit('supplicant and hostapd must own separate adapter object directories')
print('PASS: common WPA source, shared Ports BSD adapter, and conflict guards')
