#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Check base OpenLDAP source selection without building or installing."""
import argparse
import os
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--bmake', type=Path, required=True)
parser.add_argument('--source', type=Path, required=True)
args = parser.parse_args()
lib = Path(__file__).resolve().parents[1] / 'lib/libldap'
make = [str(args.bmake.resolve(strict=True)), '-C', str(lib)]
env = os.environ.copy()
env.pop('EMBER_OPENLDAP_SOURCE', None)
query = ['-V', '${LDAP_DISTDIR}', 'obj']
r = subprocess.run([*make, *query], env=env, text=True, capture_output=True)
if r.returncode:
    raise SystemExit('obj metadata without source failed: ' + r.stderr)
r = subprocess.run([*make, '-n', 'all'], env=env, text=True, capture_output=True)
if not r.returncode or 'Set EMBER_OPENLDAP_SOURCE' not in r.stderr:
    raise SystemExit('missing source was not rejected for build')
# The build.sh wrapper may export its own values, so use make's explicit
# command-line variable assignment for the selected source.
source = str(args.source.resolve(strict=True))
r = subprocess.run([*make, 'EMBER_OPENLDAP_SOURCE=' + source, *query],
                   env=env, text=True, capture_output=True)
if r.returncode or r.stdout.strip() != source:
    raise SystemExit('selected source mismatch: ' + r.stdout + r.stderr)
tokens = ['LDAP_VERSION', 'LDAP_MAJOR', 'LDAP_MINOR', 'LDAP_PATCH', 'LDAP_API_INC']
query = [part for name in tokens for part in ('-V', '${' + name + '}')]
r = subprocess.run([*make, 'EMBER_OPENLDAP_SOURCE=' + source, *query, 'all'],
                   env=env, text=True, capture_output=True)
version = (Path(source) / 'build/version.var').read_text()
values = [re.search(r'^ol_' + key + r'=([0-9]+)$', version, re.M) for key in ('major', 'minor', 'patch', 'api_inc')]
if any(value is None for value in values):
    raise SystemExit('source version.var is missing numeric version fields')
major, minor, patch, api = (value.group(1) for value in values)
if r.returncode or r.stdout.splitlines() != [f'{major}.{minor}.{patch}', major, minor, patch, api]:
    raise SystemExit('selected source version mismatch: ' + r.stdout + r.stderr)
r = subprocess.run([*make, 'EMBER_OPENLDAP_SOURCE=/nonexistent/openldap',
                    '-V', '${LDAP_VERSION}', 'obj'], env=env, text=True, capture_output=True)
if r.returncode or r.stdout.strip() != '0.0.0':
    raise SystemExit('obj metadata tried to inspect source: ' + r.stdout + r.stderr)
print('PASS: OpenLDAP source selection and missing-source guard')
