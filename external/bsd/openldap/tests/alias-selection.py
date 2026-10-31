#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Check libldap_r link aliases against selected library installation modes."""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--bmake', type=Path, required=True)
parser.add_argument('--source', type=Path, required=True)
args = parser.parse_args()
lib = Path(__file__).resolve().parents[1] / 'lib/libldap'
base = [str(args.bmake.resolve(strict=True)), '-C', str(lib),
        'EMBER_OPENLDAP_SOURCE=' + str(args.source.resolve(strict=True))]
cases = (
    ([], {'libldap_r.a', 'libldap_r.so'}),
    (['MKPIC=no', 'MKPICINSTALL=yes'], {'libldap_r.a'}),
    (['MKLINKLIB=no'], set()),
    (['MKSTATICLIB=no'], {'libldap_r.so'}),
    (['LINKINSTALL=no'], set()),
    (['MKPIC=yes', 'MKPICINSTALL=yes'], {'libldap_r.a', 'libldap_r.so', 'libldap_r_pic.a'}),
)
for options, expected in cases:
    result = subprocess.run(base + list(options) + ['-V', '${SYMLINKS}', 'all'],
                            text=True, capture_output=True)
    if result.returncode:
        raise SystemExit('alias metadata failed: ' + result.stderr)
    words = result.stdout.split()
    if len(words) % 2:
        raise SystemExit('invalid symlink pairs: ' + result.stdout)
    got = {Path(words[i]).name for i in range(1, len(words), 2)}
    if got != expected:
        raise SystemExit(f'{options}: expected {expected}, got {got}')
print('PASS: OpenLDAP link aliases follow library installation modes')
