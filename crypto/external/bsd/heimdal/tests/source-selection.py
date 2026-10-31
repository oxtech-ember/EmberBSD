#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Exercise the native Heimdal source selector with an OS make wrapper."""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--make', required=True)
parser.add_argument('--source', required=True)
args = parser.parse_args()
base = Path(__file__).resolve().parents[1]
lib = base / 'lib/libhx509'
tool = base.parents[3] / 'tools/compile_et'
source = str(Path(args.source).resolve())
def run(directory, *values):
    return subprocess.run([args.make, '-C', str(directory), *values],
                          text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
result = run(lib, 'EMBER_HEIMDAL_SOURCE=' + source, '-V', '${HEIMDIST}')
assert result.returncode == 0 and result.stdout.strip() == source, result.stdout
result = run(lib, 'EMBER_HEIMDAL_SOURCE=' + source, 'HEIMDIST=/not-the-source', '-V', '${HEIMDIST}')
assert result.returncode != 0 and 'must match' in result.stdout, result.stdout
result = run(lib, '-n', 'all')
assert result.returncode != 0 and 'Set EMBER_HEIMDAL_SOURCE' in result.stdout, result.stdout
result = run(lib, '-n', 'obj')
assert result.returncode == 0, result.stdout
result = run(tool, 'EMBER_HEIMDAL_SOURCE=' + source, '-V', '${HOST_CPPFLAGS}', '-V', '${DESTDIR}')
assert result.returncode == 0, result.stdout
flags, dest = result.stdout.splitlines()[-2:]
assert 'EMBERBSD_BASE_HEIMDAL' in flags
assert all(dest + '/usr/include/' + name not in flags for name in ('krb5', 'gssapi')), flags
print('heimdal source selection PASS')
