#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD (AI-assisted).
"""Check the E-Kermit producer's selected source with a native bmake."""
import argparse
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--make', required=True)
p.add_argument('--source', required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[4]
producer = root / 'external/bsd/ekermit/bin/ekermit'
base = [a.make, '-r', '-m', str(root / 'share/mk'), '-C', str(producer),
        'MAKECONF=/dev/null', '_SRC_TOP_=', 'NETBSDSRCDIR=' + str(root),
        'MACHINE=evbarm', 'MACHINE_ARCH=aarch64', 'USETOOLS=yes']

def run(*args):
    return subprocess.run([*base, *args], capture_output=True, text=True)

for goal in ('obj', 'includes'):
    assert run('-n', goal).returncode == 0, goal
for goal in ('all', 'dependall', 'install'):
    result = run('-n', goal)
    assert result.returncode != 0 and 'Set EMBER_EKERMIT_SOURCE' in result.stderr, goal
selected = run('EMBER_EKERMIT_SOURCE=' + a.source, '-V', '${EKERMITDIST}')
assert selected.returncode == 0 and selected.stdout.strip() == a.source
assert run('EMBER_EKERMIT_SOURCE=/missing/ekermit', '-V', '${EKERMITDIST}').returncode != 0
print('E-Kermit source routing: PASS')
