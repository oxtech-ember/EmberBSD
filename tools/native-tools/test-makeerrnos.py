#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD (AI-assisted).
"""Check ktruss generation with the exact native tool routing contract."""
import argparse
import os
from pathlib import Path
import re
import subprocess

source = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser()
p.add_argument('--routing', type=Path, required=True)
p.add_argument('--wc', type=Path, required=True)
p.add_argument('--sysroot', type=Path, required=True)
p.add_argument('--work', type=Path, required=True)
a = p.parse_args()
a.work.mkdir(parents=True, exist_ok=False)
rows = [line.split() for line in (source / 'tools/native-tools/contract').read_text().splitlines()[1:]]
route = a.work / 'route'
route.mkdir()
for command, role, _ in rows:
    target = a.wc if role == 'wc' else a.routing / command
    os.symlink(target.resolve(strict=True), route / command)
headers = a.sysroot / 'usr/include/sys'
env = dict(os.environ, PATH=str(route), LC_ALL='C', AWK=str(route / 'awk'),
           CPP=str(route / 'cpp'), CPPFLAGS='--target=aarch64--netbsd --sysroot=' + str(a.sysroot) + ' -E -x c')
cmd = [str(route / 'sh'), str(source / 'usr.bin/ktruss/makeerrnos.sh'),
       str(headers / 'errno.h'), str(headers / 'signal.h'), 'misc']
r = subprocess.run(cmd, cwd=a.work, env=env, text=True, capture_output=True)
if r.returncode or 'wc' not in {row[0] for row in rows}:
    raise SystemExit('valid generation failed or wc missing from contract: ' + r.stderr)
header = (a.work / 'misc.h').read_text()
for name in ('MAXERRNOS', 'MAXSIGNALS'):
    if not re.search(r'^#define\s+' + name + r'\s+[1-9][0-9]*\s*$', header, re.M):
        raise SystemExit('invalid generated count: ' + name)
(route / 'wc').unlink()
r = subprocess.run(cmd, cwd=a.work, env=env, text=True, capture_output=True)
if r.returncode == 0:
    raise SystemExit('missing wc incorrectly returned success: ' + r.stderr)
if (a.work / 'misc.h').exists():
    raise SystemExit('missing wc left a stale header')
print('valid headers generated with contract routing; missing wc fails')
