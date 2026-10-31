#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Check common Unbound version headers and object-only cleanup."""
import argparse
import hashlib
from pathlib import Path
import re
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--bmake', type=Path, required=True)
p.add_argument('--source', type=Path, required=True)
a = p.parse_args()
owner = Path(__file__).resolve().parents[1]
lib = owner / 'lib/libunbound'
source = a.source.resolve(strict=True)
match = re.search(r"^PACKAGE_VERSION='([0-9]+)\.([0-9]+)\.([0-9]+)'$",
                  (source / 'configure').read_text(), re.M)
if not match:
    raise SystemExit('source version is unavailable')
major, minor, micro = match.groups()
tracked = owner / 'include/config.h'
before = hashlib.sha256(tracked.read_bytes()).digest()
with tempfile.TemporaryDirectory(prefix='unbound-headers-') as temp:
    obj = Path(temp)
    gen = obj / 'unbound-generated'
    command = [str(a.bmake.resolve(strict=True)), '-C', str(lib),
               'MAKEOBJDIR=' + str(obj), 'EMBER_UNBOUND_SOURCE=' + str(source)]
    generated = [gen / 'config.h', gen / 'libunbound/unbound.h']
    subprocess.run(command + [str(path) for path in generated], check=True, stdout=subprocess.DEVNULL)
    config, header = (path.read_text() for path in generated)
    for name, value in [('MAJOR', major), ('MINOR', minor), ('MICRO', micro)]:
        if f'#define UNBOUND_VERSION_{name} {value}' not in header:
            raise SystemExit('incorrect generated public version')
    if f'#define PACKAGE_VERSION "{major}.{minor}.{micro}"' not in config:
        raise SystemExit('incorrect generated configuration version')
    subprocess.run(command + ['MKCLEANSRC=yes', 'cleandir'], check=True, stdout=subprocess.DEVNULL)
    if any(path.exists() for path in generated) or hashlib.sha256(tracked.read_bytes()).digest() != before:
        raise SystemExit('cleanup changed source or left generated headers')
print('PASS: generated Unbound headers and source-safe cleanup')
