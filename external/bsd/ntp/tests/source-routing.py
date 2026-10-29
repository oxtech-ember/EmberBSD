#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD (AI-assisted).
"""Check native NTP and libopts source bindings with selected bmake."""
import argparse
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
for name in ('make', 'source', 'libopts-source', 'tooldir'):
    p.add_argument('--' + name, required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[4]
producer = root / 'external/bsd/ntp/lib/libntp'
base = [a.make, '-m', str(root / 'share/mk'), '-C', str(producer),
        'MAKECONF=/dev/null', '_SRC_TOP_=', 'NETBSDSRCDIR=' + str(root),
        'MACHINE=evbarm', 'MACHINE_ARCH=aarch64', 'USETOOLS=yes', 'TOOLDIR=' + a.tooldir]

def run(*args):
    return subprocess.run([*base, *args], capture_output=True, text=True)

for goal in ('obj', 'includes'):
    assert run('-n', goal).returncode == 0, goal
for goal in ('all', 'dependall', 'install'):
    result = run('-n', goal)
    assert result.returncode and 'Set EMBER_NTP_SOURCE' in result.stderr, goal
    result = run('EMBER_NTP_SOURCE=' + a.source, '-n', goal)
    assert result.returncode and 'Set EMBER_LIBOPTS_SOURCE' in result.stderr, goal
values = ['EMBER_NTP_SOURCE=' + a.source, 'EMBER_LIBOPTS_SOURCE=' + a.libopts_source]
selected = run(*values, '-V', '${IDIST}', '-V', '${NTP_LIBOPTS_SOURCE}')
assert selected.returncode == 0 and selected.stdout.splitlines() == [a.source, a.libopts_source]
assert run('EMBER_NTP_SOURCE=/missing/ntp', '-V', '${IDIST}').returncode
assert run('EMBER_NTP_SOURCE=' + a.source, 'EMBER_LIBOPTS_SOURCE=/missing/libopts', '-V', '${IDIST}').returncode
print('NTP source routing: PASS')
