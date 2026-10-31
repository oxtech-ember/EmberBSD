#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Check OpenSSL 4 pthread link routing for shared and static consumers."""
import argparse
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--bmake', type=Path, required=True)
p.add_argument('--source', type=Path, required=True)
p.add_argument('--generated', type=Path, required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[5]
make = [str(a.bmake.resolve(strict=True)), 'EMBER_OPENSSL_SOURCE=' + str(a.source.resolve(strict=True)),
        'EMBER_OPENSSL_GENERATED=' + str(a.generated.resolve(strict=True))]
crypto = root / 'crypto/external/apache2/openssl/lib/libcrypto'
prog = root / 'libexec/httpd'
def query(directory, *args):
    result = subprocess.run(make + ['-C', str(directory), *args, 'all'], text=True, capture_output=True)
    if result.returncode:
        raise SystemExit('make metadata failed: ' + result.stderr)
    return result.stdout.strip().split()
if '-lpthread' not in query(crypto, '-V', '${LDADD}'):
    raise SystemExit('libcrypto shared link lacks pthread')
for flags in (['LDSTATIC=-static'], ['MKPIC=no']):
    values = query(prog, *flags, '-V', '${_LDADD.bozohttpd}')
    if '-lcrypto' not in values or '-lpthread' not in values or values.index('-lpthread') < values.index('-lcrypto'):
        raise SystemExit('static crypto consumer lacks trailing pthread: ' + repr(values))
values = query(prog, '-V', '${_LDADD.bozohttpd}')
if '-lcrypto' not in values or '-lpthread' not in values or values.index('-lpthread') < values.index('-lcrypto'):
    raise SystemExit('dynamic crypto consumer lacks trailing pthread: ' + repr(values))
values = query(root / 'usr.bin/true', '-V', '${_LDADD.true}')
if '-lpthread' in values:
    raise SystemExit('consumer without crypto gained a pthread dependency')
print('PASS: OpenSSL pthread link routing')
