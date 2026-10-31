#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Ensure object cleanup preserves the tracked source header of the same name."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--bmake', type=Path, required=True)
parser.add_argument('--source', type=Path, required=True)
args = parser.parse_args()
include = Path(__file__).resolve().parents[1] / 'include'
header = include / 'ldap_features.h'
before = hashlib.sha256(header.read_bytes()).digest()
with tempfile.TemporaryDirectory(prefix='openldap-clean-') as temp:
    obj = Path(temp)
    sentinel = obj / 'ldap_features.h'
    sentinel.write_text('object sentinel\n')
    command = [str(args.bmake.resolve(strict=True)), '-C', str(include),
               'MAKEOBJDIR=' + str(obj), 'EMBER_OPENLDAP_SOURCE=' + str(args.source.resolve(strict=True)),
               'MKCLEANSRC=no', 'CLEANFILES=ldap_features.h', 'cleandir']
    result = subprocess.run(command, text=True, capture_output=True)
    if result.returncode:
        raise SystemExit('clean failed: ' + result.stderr)
    if sentinel.exists():
        raise SystemExit('object sentinel was not removed')
    if not header.is_file() or hashlib.sha256(header.read_bytes()).digest() != before:
        raise SystemExit('tracked source header changed during object cleanup')
    generated = [obj / 'openldap-generated' / name for name in
                 ('ldap_features.h', 'libldap-config.h', 'liblber-config.h')]
    generated[0].parent.mkdir()
    for path in generated:
        path.write_text('generated sentinel\n')
    command[-3:] = ['MKCLEANSRC=yes', 'cleandir']
    result = subprocess.run(command, text=True, capture_output=True)
    if result.returncode or any(path.exists() for path in generated):
        raise SystemExit('absolute object cleanup failed: ' + result.stderr)
    if not header.is_file() or hashlib.sha256(header.read_bytes()).digest() != before:
        raise SystemExit('tracked source header changed during absolute cleanup')
print('PASS: object cleanup preserves tracked source header')
